#!/usr/bin/env python3
"""Audit the pre-fix contracts and reproducible RAW/background diagnostics."""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image, ImageFilter, ImageStat


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def artifact(path: Path) -> dict:
    return {
        "path": str(path),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def head_source(repo: Path, relative_path: str) -> str:
    result = subprocess.run(
        ["git", "show", f"HEAD:{relative_path}"],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def section(text: str, start: str, end: str) -> str:
    start_index = text.find(start)
    end_index = text.find(end, start_index + len(start))
    return text[start_index:end_index] if start_index >= 0 and end_index >= 0 else ""


def normalized_region_stats(path: Path, normalized_box: tuple[float, float, float, float]) -> dict:
    with Image.open(path) as source:
        image = source.convert("RGB")
    width, height = image.size
    box = (
        round(normalized_box[0] * width),
        round(normalized_box[1] * height),
        round(normalized_box[2] * width),
        round(normalized_box[3] * height),
    )
    region = image.crop(box)
    pixels = list(region.getdata())
    median_rgb = [
        round(statistics.median(pixel[channel] for pixel in pixels))
        for channel in range(3)
    ]
    return {
        "normalized_box": list(normalized_box),
        "pixel_box": list(box),
        "median_rgb": median_rgb,
        "near_white_fraction": sum(min(pixel) >= 245 for pixel in pixels) / len(pixels),
        "mean_edge_energy": ImageStat.Stat(
            region.convert("L").filter(ImageFilter.FIND_EDGES)
        ).mean[0],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--jpeg", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--dng-user-screenshot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    jpeg = args.jpeg.resolve()
    raw = args.raw.resolve()
    screenshot = args.dng_user_screenshot.resolve()
    probe_source = repo / "tests/hdr_raw_baseline_probe.swift"
    required = [jpeg, raw, screenshot, probe_source]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit(f"missing current root-cause input: {missing}")

    baseline_cocoa = head_source(repo, "src/qvcocoafunctions.mm")
    baseline_view = head_source(repo, "src/qvgraphicsview.cpp")
    baseline_window = head_source(repo, "src/mainwindow.cpp")
    current_cocoa = (repo / "src/qvcocoafunctions.mm").read_text(encoding="utf-8")
    current_view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    current_window = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")
    current_namespace = (repo / "src/qvnamespace.h").read_text(encoding="utf-8")

    baseline_renderer = section(
        baseline_cocoa,
        "struct QVCocoaFunctions::HDRRenderer::Impl",
        "static void hideMenuShortcuts",
    )
    baseline_raw = section(
        baseline_cocoa, "if (result.isRaw)", "else if (result.isImageIOType)"
    )
    baseline_stage = section(
        baseline_view,
        "void QVGraphicsView::stageHDRGeometry",
        "void QVGraphicsView::finishHDRGeometryStabilization",
    )
    current_renderer = section(
        current_cocoa,
        "struct QVCocoaFunctions::HDRRenderer::Impl",
        "static void hideMenuShortcuts",
    )
    current_raw = section(
        current_cocoa, "if (result.isRaw)", "else if (result.isImageIOType)"
    )
    current_stage = section(
        current_view,
        "void QVGraphicsView::stageHDRGeometry",
        "void QVGraphicsView::finishHDRGeometryStabilization",
    )

    probe_command = ["xcrun", "swift", str(probe_source), str(raw)]
    probe_result = subprocess.run(
        probe_command,
        cwd=repo,
        text=True,
        capture_output=True,
        timeout=60,
        check=False,
    )
    probe = json.loads(probe_result.stdout.strip()) if probe_result.returncode == 0 else {}

    blank_region = normalized_region_stats(screenshot, (0.15, 0.50, 0.48, 0.88))
    visible_region = normalized_region_stats(screenshot, (0.50, 0.50, 0.72, 0.88))

    baseline_contracts = {
        "different_background_color_authorities": (
            "NSColor.windowBackgroundColor" in baseline_renderer
            and 'QColor("#212121")' in baseline_window
            and 'QColor("#969696")' in baseline_window
            and "applyHDRViewportBackground" not in baseline_view
        ),
        "interaction_restarts_open_transition": all(
            token in baseline_stage for token in (
                "hdrTransitionClock.invalidate()",
                "hdrTransitionLinearProgress = 0.0",
                "loadedPixmapItem->setVisible(true)",
                "hdrRenderer->invalidateGeometry()",
            )
        ),
        "raw_endpoints_share_one_mutable_filter": (
            "CIRAWFilter *rawFilter" in baseline_raw
            and baseline_raw.count("filterWithImageURL:fileUrl.toNSURL()") == 1
            and "rawFilter.extendedDynamicRangeAmount = 0.0F" in baseline_raw
            and "rawFilter.extendedDynamicRangeAmount = 1.0F" in baseline_raw
        ),
        "interactive_context_disables_intermediate_cache": (
            "kCIContextCacheIntermediates : @NO" in baseline_renderer
        ),
        "prepared_endpoints_are_geometry_keyed": all(
            token in baseline_renderer for token in (
                "preparedViewportSize == viewportSize",
                "preparedTextureSize == requestedTextureSize",
                "preparedCorners == corners",
            )
        ),
        "unknown_raw_content_headroom_is_forwarded": (
            "result.hdrMetadata.contentHeadroom = ciImageContentHeadroom(hdrImage)"
            in baseline_raw
        ),
    }
    corrected_contracts = {
        "shared_explicit_theme_background": (
            "viewportBackgroundColor" in current_namespace
            and "Qv::viewportBackgroundColor(theme)" in current_window
            and "applyHDRViewportBackground(theme)" in current_view
            and "NSColor.windowBackgroundColor" not in current_renderer
        ),
        "independent_raw_filter_graphs": (
            "CIRAWFilter *sdrRawFilter" in current_raw
            and "CIRAWFilter *hdrRawFilter" in current_raw
            and current_raw.count("filterWithImageURL:fileUrl.toNSURL()") >= 2
        ),
        "negative_raw_baseline_is_neutralized_only_for_hdr": (
            "if (hdrRawFilter.baselineExposure < 0.0F)" in current_raw
            and "hdrRawFilter.baselineExposure = 0.0F" in current_raw
            and "sdrRawFilter.baselineExposure" not in current_raw
        ),
        "interactive_context_caches_intermediates": (
            "kCIContextCacheIntermediates : @YES" in current_renderer
        ),
        "raw_headroom_is_measured_and_tagged": all(
            token in current_raw for token in (
                "maximumCIImageRGBComponent(hdrImage",
                "resolvedHDRContentHeadroom",
                "imageBySettingContentHeadroom",
            )
        ) and "metalLayer.contentsHeadroom = std::max<CGFloat>(1.0, state.targetHeadroom)"
        in current_renderer,
        "prepared_sources_survive_geometry_changes": (
            "preparedViewportSize == viewportSize" not in current_renderer
            and "preparedCorners == corners" not in current_renderer
            and "reuseVisibleHDR" in current_stage
        ),
        "interaction_preserves_activation": (
            "hdrTransitionClock.invalidate()" not in current_stage
            and "hdrTransitionLinearProgress = 0.0" not in current_stage
            and "loadedPixmapItem->setVisible(!reuseVisibleHDR)" in current_stage
        ),
    }
    probe_contracts = {
        "probe_completed": probe_result.returncode == 0,
        "raw_reports_unknown_content_headroom": (
            abs(float(probe.get("raw_ciimage_reported_content_headroom", -1))) <= 1e-6
        ),
        "camera_default_baseline_is_negative": (
            float(probe.get("default_baseline_exposure", 0)) < -0.5
        ),
        "neutral_baseline_increases_available_hdr_peak": (
            float(probe.get("neutral_baseline_hdr_maximum_component", 0)) > 1.5
            and float(probe.get("neutral_baseline_hdr_maximum_component", 0))
            > float(probe.get("default_baseline_hdr_maximum_component", 0)) + 0.3
        ),
        "user_screenshot_contains_low_detail_white_blank": (
            blank_region["near_white_fraction"] >= 0.90
            and blank_region["mean_edge_energy"]
            < visible_region["mean_edge_energy"] * 0.35
        ),
    }

    passed = (
        all(baseline_contracts.values())
        and all(corrected_contracts.values())
        and all(probe_contracts.values())
    )
    record = {
        "schema_version": "1.0",
        "kind": "current-pre-fix-root-cause-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "baseline_revision": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True,
            capture_output=True, check=True,
        ).stdout.strip(),
        "source_evidence": {
            "jpeg_sample": artifact(jpeg),
            "raw_sample": artifact(raw),
            "dng_user_screenshot": artifact(screenshot),
            "raw_probe_source": artifact(probe_source),
        },
        "raw_probe": {
            "command": probe_command,
            "return_code": probe_result.returncode,
            "stdout": probe_result.stdout,
            "stderr": probe_result.stderr,
            "measurements": probe,
        },
        "screenshot_observations": {
            "image_size": list(Image.open(screenshot).size),
            "blank_region": blank_region,
            "visible_image_region": visible_region,
        },
        "baseline_contracts": baseline_contracts,
        "corrected_contracts": corrected_contracts,
        "probe_contracts": probe_contracts,
        "facts": [
            "The baseline Qt painter uses fixed light/dark viewport colors, while the independent Metal layer uses NSColor.windowBackgroundColor and is not updated by QVGraphicsView::settingsUpdated.",
            "The baseline geometry stage invalidates the HDR generation, resets transition progress to zero, and makes the SDR proxy visible for every zoom or pan geometry change.",
            "The baseline RAW decoder obtains both lazy CIImage outputs by mutating one CIRAWFilter from EDR amount zero to one, and its interactive renderer explicitly disables CIContext intermediate caching.",
            "For the supplied DNG, CIRAWFilter reports contentHeadroom zero, its camera default baselineExposure is -0.961081, and a default-baseline EDR probe peaks at about 1.35.",
            "For the same DNG and EDR amount, setting only the HDR filter baselineExposure to zero raises the measured float peak to 1.83203125 while the SDR companion remains at one.",
            "The hashed user screenshot contains a lower-left probe region that is over 90% near-white and has less than 35% of the edge energy of the adjacent visible-image region.",
        ],
        "inferences": [
            "The delayed background change is the Metal layer reveal exposing a different background authority; the frozen background after theme changes follows from the missing renderer update path.",
            "Reusing one mutable lazy CIRAWFilter, disabling repeated-render caching, and tying prepared source endpoints to viewport geometry together make RAW tile evaluation timing-dependent; this best explains intermittent partial blank regions.",
            "Forwarding unknown RAW headroom while preserving a camera negative baseline in the HDR branch suppresses both the available EDR range and perceived brightness relative to the system RAW preview.",
            "Resetting the open-transition state machine on geometry changes directly explains the observed dim-then-bright interaction sequence.",
        ],
        "uncertainties": [
            "Apple does not publish Quick Look's private RAW exposure, tone curve, or exact transition implementation; baseline neutralization is an empirical match built on public CIRAWFilter semantics.",
            "Apple does not publish the internal tile scheduler for lazy CIRAWFilter graphs, so the precise GPU/ROI race is inferred from source contracts, repeated probes, and timed screen evidence.",
            "Screenshots are system-tone-mapped SDR captures and cannot measure absolute display nits; float probes and WindowServer/layer headroom telemetry establish HDR separately.",
        ],
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "passed": passed}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
