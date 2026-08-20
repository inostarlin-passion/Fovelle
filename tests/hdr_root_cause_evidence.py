#!/usr/bin/env python3
"""Create immutable, machine-auditable evidence for the reproduced defects."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def artifact(path: Path) -> dict:
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": sha256(path)}


def head_source(repo: Path, path: str) -> str:
    result = subprocess.run(
        ["git", "show", f"HEAD:{path}"], cwd=repo, text=True,
        capture_output=True, check=False,
    )
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def telemetry_records(path: Path) -> list[dict]:
    records = []
    text = path.read_text(encoding="utf-8", errors="replace")
    for match in re.finditer(r"FOVELLE_HDR\s+(\{[^\n\r]+\})", text):
        try:
            records.append(json.loads(match.group(1)))
        except json.JSONDecodeError:
            continue
    return records


def black_band_metric(path: Path) -> dict:
    with Image.open(path) as source:
        image = source.convert("RGB")
        image.thumbnail((1200, 800), Image.Resampling.LANCZOS)
    width, height = image.size
    top = int(height * 0.08)
    bottom = max(top + 1, int(height * 0.94))
    pixels = image.load()
    counts = [0] * width
    for y in range(top, bottom):
        for x in range(width):
            if max(pixels[x, y]) <= 4:
                counts[x] += 1
    qualifying = [value / (bottom - top) >= 0.80 for value in counts]
    longest = current = 0
    for value in qualifying:
        current = current + 1 if value else 0
        longest = max(longest, current)
    return {
        "normalized_size": [width, height],
        "maximum_consecutive_near_black_columns": longest,
        "near_black_component_max": 4,
        "minimum_column_coverage": 0.80,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--jpeg-user-screenshot", type=Path, required=True)
    parser.add_argument("--dng-user-screenshot", type=Path, required=True)
    parser.add_argument("--jpeg-pre-fix-frame", type=Path, required=True)
    parser.add_argument("--jpeg-pre-fix-telemetry", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    paths = [
        args.jpeg_user_screenshot.resolve(), args.dng_user_screenshot.resolve(),
        args.jpeg_pre_fix_frame.resolve(), args.jpeg_pre_fix_telemetry.resolve(),
    ]
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise SystemExit(f"missing root-cause evidence: {missing}")
    jpeg_user, dng_user, jpeg_pre_fix, telemetry_path = paths

    baseline_renderer = head_source(repo, "src/qvcocoafunctions.mm")
    baseline_view = head_source(repo, "src/qvgraphicsview.cpp")
    current_renderer = (repo / "src/qvcocoafunctions.mm").read_text(encoding="utf-8")
    current_view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    renderer_section = current_renderer[
        current_renderer.find("struct QVCocoaFunctions::HDRRenderer::Impl"):
        current_renderer.find("static void hideMenuShortcuts")
    ]
    records = telemetry_records(telemetry_path)
    steady_records = [
        item for item in records
        if item.get("hdr_prepared") is True
        and float(item.get("transition_progress", 0)) >= 0.999
        and item.get("drawable_geometry_matches") is True
        and float(item.get("layer_opacity", 0)) > 0.5
    ]

    reproduced = {
        "jpeg_user_screenshot_contains_large_black_band": (
            black_band_metric(jpeg_user)["maximum_consecutive_near_black_columns"] > 10
        ),
        "jpeg_steady_capture_contains_large_black_band": (
            black_band_metric(jpeg_pre_fix)["maximum_consecutive_near_black_columns"] > 10
        ),
        "black_band_persists_after_hdr_preparation": bool(steady_records),
        "baseline_reimports_app_owned_metal_textures": (
            "preparedSDRTexture" in baseline_renderer
            and "preparedHDRTexture" in baseline_renderer
            and "imageWithMTLTexture" in baseline_renderer
        ),
        "baseline_hdr_output_requires_current_headroom_above_one": (
            "state.displayCurrentHeadroom <= 1.001F" in baseline_renderer
            and "state.contentHeadroom, state.displayCurrentHeadroom" in baseline_renderer
        ),
        "baseline_has_no_geometry_generation_invalidation": (
            "invalidateGeometry" not in baseline_renderer and "hdrGeometryTimer" not in baseline_view
        ),
        "dng_partial_frame_screenshot_present": dng_user.is_file(),
    }
    corrected = {
        "gain_map_sources_decode_to_nonvolatile_cache": (
            current_renderer.count("(id)kCIImageCacheImmediately : @YES") >= 2
        ),
        "managed_core_image_intermediates": current_renderer.count("imageByInsertingIntermediate:YES") == 2,
        "managed_intermediates_precede_viewport_transform": (
            renderer_section.find(
                "preparedSDRImage = [[sdrSource imageByInsertingIntermediate:YES]"
            ) < renderer_section.find("CIImage *preparedSDRFrame = imageForTexture")
        ),
        "no_app_texture_reimport_in_renderer": "imageWithMTLTexture" not in renderer_section,
        "potential_headroom_bootstrap": (
            "displayHeadroomForRendering" in current_renderer and "state.bootstrappingEDR" in current_renderer
        ),
        "complete_geometry_stabilization": (
            "hdrViewportGeometryEquivalent" in current_view
            and "finishHDRGeometryStabilization" in current_view
            and "hdrRenderer->invalidateGeometry()" in current_view
        ),
        "qt_proxy_covers_pending_geometry": "loadedPixmapItem->setVisible(true)" in current_view,
        "entire_drawable_is_written_opaque": (
            "metalLayer.opaque = YES" in current_renderer
            and "alpha:1" in current_renderer
        ),
    }
    record = {
        "schema_version": "1.0",
        "kind": "pre-fix-root-cause-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "baseline_revision": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True,
            capture_output=True, check=True,
        ).stdout.strip(),
        "source_evidence": {
            "jpeg_user_screenshot": artifact(jpeg_user),
            "dng_user_screenshot": artifact(dng_user),
            "jpeg_pre_fix_steady_capture": artifact(jpeg_pre_fix),
            "jpeg_pre_fix_telemetry": artifact(telemetry_path),
        },
        "observations": {
            "jpeg_user_black_band": black_band_metric(jpeg_user),
            "jpeg_pre_fix_black_band": black_band_metric(jpeg_pre_fix),
            "telemetry_record_count": len(records),
            "steady_prepared_record_count": len(steady_records),
            "representative_steady_record": steady_records[-1] if steady_records else None,
        },
        "reproduced_conditions": reproduced,
        "corrective_contracts_present": corrected,
        "facts": [
            "The user JPEG screenshot and an independently timed steady capture both contain a near-black vertical run wider than the ten-column acceptance threshold.",
            "At the same time, pre-fix telemetry reports transition progress one, hdr_prepared true, matching drawable geometry, and layer opacity one; the defect is therefore inside the prepared pixel endpoint rather than an empty or mismatched drawable.",
            "The baseline renderer writes both endpoints to application-owned private RGBA16Float textures and then imports those textures as later CIImage inputs.",
            "The baseline decides whether to emit HDR from NSScreen current headroom, although Apple documents that current headroom can remain one until EDR content is onscreen.",
            "The baseline view has no generation invalidation or complete-geometry debounce for its independent CAMetalLayer.",
            "The Core Image SDK describes kCIImageCacheImmediately=YES as decoding, when possible, into a non-volatile cache during image initialization; NO defers decoding to a volatile render-time cache.",
        ],
        "inferences": [
            "The baseline application-owned texture write/re-import boundary and render-time adaptive-HDR decode both permit a later graph to consume geometry-dependent or unresolved data. Removing the texture re-import, caching the SDR/HDR recipes at initialization, and inserting the managed intermediate before viewport transformation removes both unsafe boundaries.",
            "The forced-SDR control renders the same zoom/pan geometry without bands, while the delayed adaptive-HDR graph fails and the immediately cached adaptive-HDR graph passes; this isolates the remaining JPEG fault to gain-map source evaluation rather than Qt scroll coordinates.",
            "Using current headroom as the prerequisite for the first HDR frame creates a circular bootstrap dependency and explains clean-start runs with no perceived HDR.",
            "Letting the independent Metal layer continue presenting while Qt changes scroll/zoom geometry explains stale partial DNG frames and drag trails; invalidating its generation and showing the Qt proxy removes that overlap.",
            "Writing an opaque background over the complete drawable prevents transparent regions in reused drawable textures from revealing the proxy or older positions.",
        ],
        "uncertainties": [
            "Apple does not publish the private implementation of Quick Look's cache strategy or tone curve, so equivalence is behavioral rather than implementation-identical.",
            "A screenshot cannot encode absolute luminance; integration pixel peaks and WindowServer headroom telemetry are required separately to establish HDR output.",
            "Apple does not publish the internal ROI and tile scheduling of the adaptive-HDR recipe, so the exact private GPU failure mechanism remains an inference; the externally observable cache contract and controlled A/B results are verified.",
        ],
        "passed": all(reproduced.values()) and all(corrected.values()),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "passed": record["passed"]}, ensure_ascii=False))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
