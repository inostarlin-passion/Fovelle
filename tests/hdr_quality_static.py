#!/usr/bin/env python3
"""Static/build contract tests for the native RAW/HDR pipeline."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def between(text: str, start: str, end: str) -> str:
    start_index = text.find(start)
    end_index = text.find(end, start_index + len(start))
    if start_index < 0 or end_index < 0:
        return ""
    return text[start_index:end_index]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = args.build_dir.resolve()
    cocoa = (repo / "src/qvcocoafunctions.mm").read_text(encoding="utf-8")
    cocoa_header = (repo / "src/qvcocoafunctions.h").read_text(encoding="utf-8")
    view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    loader = (repo / "src/qvimageloader.cpp").read_text(encoding="utf-8")
    cmake = (repo / "CMakeLists.txt").read_text(encoding="utf-8")
    qmake = (repo / "qView.pro").read_text(encoding="utf-8")
    plist = (repo / "dist/mac/Info.plist").read_text(encoding="utf-8")
    tests = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")

    cases: list[dict] = []

    def case(identifier: str, observations: dict[str, bool | str | int | float]) -> None:
        boolean_values = [value for value in observations.values() if isinstance(value, bool)]
        passed = bool(boolean_values) and all(boolean_values)
        cases.append({
            "id": identifier,
            "test_code": "tests/hdr_quality_static.py",
            "observations": observations,
            "status": "passed" if passed else "failed",
        })

    raw_decode = between(cocoa, "if (result.isRaw)", "else if (result.isImageIOType)")
    nonraw_decode = between(cocoa, "else if (result.isImageIOType)", "CFRelease(source);")
    renderer = between(cocoa, "struct QVCocoaFunctions::HDRRenderer::Impl", "static void hideMenuShortcuts")

    case("ST-HDR-RAW-CONTENT-UTI", {
        "camera_raw_conformance_check": "UTTypeRAWImage" in cocoa and "conformsToType" in cocoa,
        "classification_uses_source_type": "result.isRaw = isRawImageType(sourceType)" in cocoa,
        "filename_suffix_not_used_in_decoder": "QFileInfo(filePath).suffix" not in raw_decode,
    })
    case("ST-HDR-RAW-CIRAWFILTER", {
        "native_raw_filter": "CIRAWFilter filterWithImageURL" in raw_decode,
        "sdr_raw_graph": "extendedDynamicRangeAmount = 0.0F" in raw_decode,
        "edr_raw_graph": "extendedDynamicRangeAmount = 1.0F" in raw_decode,
        "native_graph_published": "make_shared<NativeHDRImage>" in raw_decode,
    })
    case("ST-HDR-RAW-PREVIEW-FALLBACK", {
        "preview_is_conditional_fallback": raw_decode.find("if (result.image.isNull())") < raw_decode.find("rawFilter.previewImage"),
        "preview_usage_is_observable": "result.usedRawPreview" in raw_decode,
        "unsupported_camera_is_explicit": "does not support this camera model" in raw_decode,
    })
    case("ST-HDR-NONRAW-METADATA", {
        "apple_gain_map": "kCGImageAuxiliaryDataTypeHDRGainMap" in cocoa,
        "iso_gain_map": "kCGImageAuxiliaryDataTypeISOGainMap" in cocoa,
        "pq_detection": "CGColorSpaceIsPQBased" in cocoa,
        "hlg_detection": "CGColorSpaceIsHLGBased" in cocoa,
        "headroom_read": "CGImageGetContentHeadroom" in cocoa and ".contentHeadroom" in cocoa,
    })
    case("ST-HDR-NONRAW-RECONSTRUCTION", {
        "imageio_hdr_request": "kCGImageSourceDecodeToHDR" in cocoa,
        "core_image_expand": "kCIImageExpandToHDR" in nonraw_decode,
        "orientation_applied": "kCIImageApplyOrientationProperty" in nonraw_decode,
        "hdr_candidate_from_metadata": "const bool hdrCandidate = hasGainMap" in nonraw_decode,
    })
    case("ST-HDR-FLOAT-INTERMEDIATE", {
        "half_float_context": cocoa.count("kCIFormatRGBAh") >= 2,
        "retained_ci_graph": "CIImage *hdr" in cocoa and "[hdrImage retain]" in cocoa,
        "hdr_handle_crosses_loader": "nativeResult.hdrImage" in loader,
        "rgba8_limited_to_named_fallback_helper": "QImage imageFromCIImage" in cocoa and "kCIFormatRGBA8" in cocoa,
    })
    case("ST-HDR-COLORSYNC", {
        "colorsync_profile": "ColorSyncProfileCreateWithName(kColorSyncDisplayP3Profile)" in cocoa,
        "extended_linear_p3": "CGColorSpaceCreateExtendedLinearized" in cocoa,
        "ci_working_space": "kCIContextWorkingColorSpace" in renderer,
        "ci_output_space": "kCIContextOutputColorSpace" in renderer,
    })
    case("ST-HDR-METAL-EDR-SURFACE", {
        "metal_ci_context": "contextWithMTLDevice" in renderer,
        "rgba16_float_layer": "MTLPixelFormatRGBA16Float" in renderer,
        "edr_requested": "wantsExtendedDynamicRangeContent = YES" in renderer,
        "quartzcore_linked": "-framework QuartzCore" in cmake and "-framework QuartzCore" in qmake,
    })
    case("ST-HDR-DISPLAY-ADAPTATION", {
        "current_headroom_per_render": "maximumExtendedDynamicRangeColorComponentValue" in renderer,
        "potential_headroom_per_render": "maximumPotentialExtendedDynamicRangeColorComponentValue" in renderer,
        "window_screen_selected": "nativeView.window.screen" in renderer,
        "test_override": "FOVELLE_TEST_DISPLAY_HEADROOM" in renderer,
    })
    case("ST-HDR-TONE-MAPPING", {
        "system_tone_map_filter": "CIToneMapHeadroom" in renderer,
        "source_headroom_parameter": "inputSourceHeadroom" in renderer,
        "target_headroom_parameter": "inputTargetHeadroom" in renderer,
        "layer_automatic_tone_map": "CAToneMapModeAutomatic" in renderer,
    })
    case("ST-HDR-GEOMETRY", {
        "four_source_corners": "const QPolygonF sourceCorners" in view,
        "qt_viewport_transform": "viewportTransform().map(sourceCorners)" in view,
        "metal_affine_transform": "CGAffineTransformMake(a, b, c, d, tx, ty)" in renderer,
        "source_size_scene_rect": "QRectF(QPointF(), getCurrentFileDetails().loadedPixmapSize)" in view,
        "actual_drawable_texture_drives_coordinates": "textureSize.width / viewportSize.width()" in renderer,
        "drawable_size_observed": "drawable.texture.width" in renderer and "drawableGeometryMatches" in renderer,
    })
    case("ST-HDR-STAGED-FIRST-FRAME", {
        "layout_gate_closed_before_fit": view.count("hdrLayoutReady = false") >= 2,
        "layout_gate_armed_after_fit": "hdrLayoutReady = hdrRendererActive" in view,
        "sdr_proxy_kept_visible": "loadedPixmapItem->setVisible(true)" in view,
        "metal_starts_transparent": "metalLayer.opacity = 0.0F" in renderer,
        "reveal_after_presented_handler": "addPresentedHandler" in renderer and "firstFramePresented = YES" in renderer,
    })
    case("ST-HDR-OFFSCREEN-PREPARATION", {
        "preparation_is_after_first_frame": "!presentationState->firstFramePresented" in renderer,
        "two_float_endpoint_textures": "preparedSDRTexture" in renderer and "preparedHDRTexture" in renderer,
        "raw_graph_rendered_offscreen": "scheduleHDRPreparation" in renderer and "toMTLTexture:preparedHDRTexture" in renderer,
        "transition_waits_for_preparation": "shouldStartHDRTransition" in view and "hdrPrepared" in view,
        "prepared_endpoints_used_for_transition": "preparedDisplayImage" in renderer,
    })
    case("ST-HDR-OBSERVABILITY", {
        "json_telemetry": '"FOVELLE_HDR"' in view and "QJsonDocument::Compact" in view,
        "decode_timing": "decodeTimer.nsecsElapsed()" in loader,
        "render_timing": "lastRenderMilliseconds" in renderer,
        "headroom_and_transition_fields": "display_current_headroom" in view and "transition_progress" in view,
    })
    case("ST-HDR-VERSION-0.1.4", {
        "cmake_version": "project(Fovelle VERSION 0.1.4" in cmake,
        "qmake_version": "VERSION = 0.1.4" in qmake,
        "plist_version_twice": plist.count("<string>0.1.4</string>") >= 2,
        "runtime_assertion": 'QString("0.1.4")' in tests,
    })

    started = time.perf_counter()
    build_command = ["cmake", "--build", str(build_dir), "--parallel", "8"]
    build = subprocess.run(build_command, cwd=repo, text=True, capture_output=True, check=False)
    diff = subprocess.run(["git", "diff", "--check"], cwd=repo, text=True, capture_output=True, check=False)
    clang_tidy_binary = shutil.which("clang-tidy")
    if not clang_tidy_binary:
        bundled_tidy = Path("/opt/homebrew/opt/llvm/bin/clang-tidy")
        clang_tidy_binary = str(bundled_tidy) if bundled_tidy.is_file() else None
    clang_tidy_command = [
        clang_tidy_binary or "clang-tidy",
        "-p",
        str(build_dir),
        str(repo / "src/qvcocoafunctions.mm"),
        str(repo / "src/qvgraphicsview.cpp"),
        str(repo / "src/qvimagecore.cpp"),
        str(repo / "src/qvimageloader.cpp"),
        "--quiet",
    ]
    if clang_tidy_binary:
        clang_tidy = subprocess.run(
            clang_tidy_command, cwd=repo, text=True, capture_output=True, check=False, timeout=120
        )
        clang_tidy_output = clang_tidy.stdout + clang_tidy.stderr
        clang_tidy_passed = (
            clang_tidy.returncode == 0
            and "warning:" not in clang_tidy_output
            and "error:" not in clang_tidy_output
        )
    else:
        clang_tidy = None
        clang_tidy_output = "clang-tidy executable not found"
        clang_tidy_passed = False
    elapsed = time.perf_counter() - started
    build_passed = build.returncode == 0
    diff_passed = diff.returncode == 0

    source_paths = [
        repo / "src/qvcocoafunctions.h",
        repo / "src/qvcocoafunctions.mm",
        repo / "src/qvgraphicsview.cpp",
        repo / "src/qvimageloader.cpp",
        repo / "docs/hdr_pipeline.md",
    ]
    passed = (
        all(item["status"] == "passed" for item in cases)
        and build_passed
        and diff_passed
        and clang_tidy_passed
    )
    record = {
        "schema_version": "1.0",
        "kind": "static-test-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "host": {"platform": platform.platform(), "python": platform.python_version()},
        "commands": [build_command, ["git", "diff", "--check"], clang_tidy_command],
        "build": {
            "return_code": build.returncode,
            "elapsed_seconds": elapsed,
            "stdout": build.stdout,
            "stderr": build.stderr,
            "passed": build_passed,
        },
        "diff_check": {"return_code": diff.returncode, "output": diff.stdout + diff.stderr, "passed": diff_passed},
        "clang_tidy": {
            "binary": clang_tidy_binary,
            "return_code": clang_tidy.returncode if clang_tidy else None,
            "output": clang_tidy_output,
            "warnings_or_errors": [
                line for line in clang_tidy_output.splitlines() if "warning:" in line or "error:" in line
            ],
            "passed": clang_tidy_passed,
        },
        "source_hashes": [
            {"path": str(path.relative_to(repo)), "sha256": sha256(path), "bytes": path.stat().st_size}
            for path in source_paths
        ],
        "cases": cases,
        "summary": {
            "total": len(cases),
            "passed": sum(item["status"] == "passed" for item in cases),
            "failed": sum(item["status"] != "passed" for item in cases),
        },
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "summary": record["summary"], "passed": passed}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
