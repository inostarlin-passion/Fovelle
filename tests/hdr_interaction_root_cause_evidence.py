#!/usr/bin/env python3
"""Record auditable pre-fix contracts and the evidence-backed causal chains."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


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
        ["git", "show", f"HEAD:{relative_path}"], cwd=repo, text=True,
        capture_output=True, check=False,
    )
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def section(text: str, start: str, end: str) -> str:
    start_index = text.find(start)
    end_index = text.find(end, start_index + len(start))
    return text[start_index:end_index] if start_index >= 0 and end_index >= 0 else ""


def run_json(command: list[str], cwd: Path) -> tuple[subprocess.CompletedProcess[str], dict]:
    result = subprocess.run(
        command, cwd=cwd, text=True, capture_output=True, timeout=90, check=False
    )
    try:
        record = json.loads(result.stdout) if result.returncode == 0 else {}
    except json.JSONDecodeError:
        record = {}
    return result, record


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--jpeg", type=Path, required=True)
    parser.add_argument("--dng", type=Path, required=True)
    parser.add_argument("--nef", type=Path, required=True)
    parser.add_argument("--screenshot", type=Path, required=True)
    parser.add_argument("--solution1", type=Path, required=True)
    parser.add_argument("--solution2", type=Path, required=True)
    parser.add_argument("--solution3", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    paths = {
        "jpeg": args.jpeg.resolve(),
        "dng": args.dng.resolve(),
        "nef": args.nef.resolve(),
        "screenshot": args.screenshot.resolve(),
        "solution1": args.solution1.resolve(),
        "solution2": args.solution2.resolve(),
        "solution3": args.solution3.resolve(),
        "baseline_probe": repo / "tests/hdr_raw_baseline_probe.swift",
        "gain_map_probe": repo / "tests/hdr_gain_map_probe.swift",
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise SystemExit(f"missing root-cause input: {missing}")

    before_cocoa = head_source(repo, "src/qvcocoafunctions.mm")
    before_view = head_source(repo, "src/qvgraphicsview.cpp")
    before_window = head_source(repo, "src/mainwindow.cpp")
    after_cocoa = (repo / "src/qvcocoafunctions.mm").read_text(encoding="utf-8")
    after_view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    after_window = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")

    before_renderer = section(
        before_cocoa, "struct QVCocoaFunctions::HDRRenderer::Impl", "static void hideMenuShortcuts"
    )
    after_renderer = section(
        after_cocoa, "struct QVCocoaFunctions::HDRRenderer::Impl", "static void hideMenuShortcuts"
    )
    before_raw = section(before_cocoa, "if (result.isRaw)", "else if (result.isImageIOType)")
    after_raw = section(after_cocoa, "if (result.isRaw)", "else if (result.isImageIOType)")
    before_navigation = section(
        before_window, "std::optional<qreal> sampledContentBrightness", "class TitlebarBubble"
    ) + section(before_window, "void MainWindow::initializeNavigationButtons", "void MainWindow::updateNavigationButtonGeometry")
    after_navigation = section(
        after_window, "std::optional<qreal> sampledContentBrightness", "class TitlebarBubble"
    ) + section(after_window, "void MainWindow::initializeNavigationButtons", "void MainWindow::updateNavigationButtonGeometry")

    baseline_command = [
        "xcrun", "swift", str(paths["baseline_probe"]), str(paths["dng"])
    ]
    baseline_result, baseline_probe = run_json(baseline_command, repo)
    gain_command = [
        "xcrun", "swift", str(paths["gain_map_probe"]), str(paths["dng"])
    ]
    gain_result, gain_probe = run_json(gain_command, repo)

    before_contracts = {
        "drawable_wait_on_ui_render_path": "[metalLayer nextDrawable]" in before_renderer,
        "paint_and_both_scrollbars_render_synchronously": (
            "QGraphicsView::paintEvent(event);\n    updateHDRRenderer();" in before_view
            and before_view.count("[this]() { updateHDRRenderer(); }") >= 2
        ),
        "navigation_hover_grabs_and_repaints_viewport": "viewport()->grab(sampleRect)" in before_navigation,
        "navigation_fade_uses_rectangular_graphics_effect": (
            "QGraphicsOpacityEffect" in before_navigation
            and "button->setGraphicsEffect" in before_navigation
        ),
        "dng_hdr_filter_overrides_camera_baseline": (
            "hdrRawFilter.baselineExposure = 0.0F" in before_raw
        ),
        "dng_uses_generic_raw_output_as_primary": (
            "hdrRawFilter.outputImage" in before_raw
            and "camera-raw-processed-gain-map" not in before_raw
        ),
        "renderer_has_no_latest_only_display_link_gate": (
            "CAMetalDisplayLink" not in before_renderer
            and "frameInFlight" not in before_renderer
        ),
    }
    corrected_contracts = {
        "display_link_supplies_drawable_without_next_drawable": (
            "CAMetalDisplayLink" in after_renderer
            and "update.drawable" in after_renderer
            and "nextDrawable" not in after_renderer
        ),
        "ui_events_are_coalesced_before_renderer": (
            "hdrFrameRequestTimer->setInterval(0)" in after_view
            and after_view.count("hdrRenderer->render(") == 1
        ),
        "latest_state_overwrites_while_one_frame_is_in_flight": all(
            token in after_renderer for token in (
                "pendingRenderGeneration = ++state.requestedRenderGeneration",
                "if (presentationState->frameInFlight)",
                "presentationState->frameInFlight = YES",
                "state.submittedRenderGeneration = renderGeneration",
            )
        ),
        "core_image_texture_encoding_is_off_appkit_thread": (
            "com.fovelle.hdr-render-encode" in after_renderer
            and "dispatch_async(renderQueue" in after_renderer
            and "[renderContext render:encodedSource" in after_renderer
        ),
        "navigation_sampling_is_cached_and_bounded": (
            "sampleDisplayedImageBrightness" in after_navigation
            and "viewport()->grab" not in after_navigation
            and "navigationSamplingImage" in after_view
            and "384, 384" in after_view
        ),
        "navigation_fades_only_custom_painted_pixels": (
            'setProperty("paintOpacity", 0.0)' in after_window
            and 'new QPropertyAnimation(button, "paintOpacity"' in after_navigation
            and "button->setGraphicsEffect" not in after_navigation
        ),
        "dng_uses_processed_preview_and_authored_gain_map": all(
            token in after_raw for token in (
                "sdrRawFilter.previewImage",
                "kCIImageAuxiliaryHDRGainMap",
                "imageByApplyingGainMap:gainMap",
                "camera-raw-processed-gain-map",
            )
        ),
        "camera_baseline_is_not_mutated": "baselineExposure =" not in after_raw,
        "processed_dng_avoids_post_gainmap_intermediate": (
            "if (image->metadata().usesProcessedRawPreview)" in after_renderer
            and "preparedSDRImage = [sdrSource retain]" in after_renderer
        ),
    }
    probe_contracts = {
        "both_probes_completed": baseline_result.returncode == 0 and gain_result.returncode == 0,
        "dng_camera_baseline_is_materially_negative": (
            float(baseline_probe.get("default_baseline_exposure", 0)) < -0.5
        ),
        "baseline_override_materially_changes_hdr_peak": (
            float(baseline_probe.get("neutral_baseline_hdr_maximum_component", 0))
            > float(baseline_probe.get("default_baseline_hdr_maximum_component", 0)) + 0.3
        ),
        "processed_preview_is_full_resolution": (
            gain_probe.get("processed_preview_extent", {}).get("width") == 8064
            and gain_probe.get("processed_preview_extent", {}).get("height") == 6048
        ),
        "gain_map_is_half_resolution": (
            gain_probe.get("gain_map_extent", {}).get("width") == 4032
            and gain_probe.get("gain_map_extent", {}).get("height") == 3024
        ),
        "processed_hdr_has_authored_headroom": (
            float(gain_probe.get("applied_content_headroom", 0)) > 3.5
        ),
    }
    passed = all(before_contracts.values()) and all(corrected_contracts.values()) and all(
        probe_contracts.values()
    )
    record = {
        "schema_version": "1.0",
        "kind": "interaction-navigation-raw-root-cause-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "baseline_revision": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True,
            capture_output=True, check=True,
        ).stdout.strip(),
        "artifacts": {name: artifact(path) for name, path in paths.items()},
        "before_contracts": before_contracts,
        "corrected_contracts": corrected_contracts,
        "probe_contracts": probe_contracts,
        "probe_runs": {
            "baseline": {
                "command": baseline_command,
                "return_code": baseline_result.returncode,
                "stderr": baseline_result.stderr,
                "measurements": baseline_probe,
            },
            "gain_map": {
                "command": gain_command,
                "return_code": gain_result.returncode,
                "stderr": gain_result.stderr,
                "measurements": gain_probe,
            },
        },
        "facts": [
            "The pre-fix renderer called CAMetalLayer nextDrawable from the same synchronous render path reached by paintEvent and both scrollbar valueChanged signals.",
            "The pre-fix navigation contrast sampler called viewport()->grab, and both navigation buttons were faded through QGraphicsOpacityEffect-backed source surfaces.",
            "The pre-fix DNG HDR filter changed the camera-authored negative BaselineExposure to zero and used the generic CIRAWFilter output graph as primary content.",
            "For IMG_8625.DNG the measured camera baseline is materially negative, and changing it to zero materially changes the float HDR peak.",
            "The DNG exposes an 8064x6048 camera-processed preview, a 4032x3024 auxiliary gain map, and authored processed HDR headroom above 3.5.",
            "The pre-fix renderer had neither CAMetalDisplayLink scheduling nor an explicit one-frame-in-flight/latest-generation gate.",
        ],
        "inferences": [
            "Synchronous drawable acquisition and repeated paint/scroll submissions occupied the AppKit event loop; viewport grab added repaint work during hover, jointly explaining slow pan and navigation response.",
            "QGraphicsOpacityEffect's rectangular offscreen source over a native EDR Metal sibling explains why transparent button backing appeared before and disappeared after the rounded artwork.",
            "Changing one RAW exposure parameter cannot reproduce the camera's authored processing recipe and discards local/highlight rendering information; the paired full-resolution preview plus gain map is the closest public Quick Look-style representation for this DNG.",
            "Without latest-only frame gating, drawable commands produced by successive NEF zoom geometries can present after newer UI state and appear briefly as a stale-frame ghost.",
            "Applying a half-resolution gain map and then inserting a source intermediate can make the first reduced ROI reusable under a full-size extent; retaining the complete processed source avoids the observed DNG quadrant regression.",
        ],
        "uncertainties": [
            "Apple does not publish Quick Look's private RAW tone recipe, so edge/RGB integration comparison can establish closeness but not exact identity.",
            "Apple does not publish Core Image's internal gain-map ROI/tile cache implementation; the quadrant mechanism is inferred from the 2:1 extents, reproducible behavior, and cache-boundary fix.",
            "A screenshot cannot encode absolute EDR luminance or every sub-frame ghost; float probes, WindowServer headroom telemetry, timed captures, and generation invariants provide complementary evidence.",
        ],
        "causal_chains": [
            {
                "symptom": "zoomed HDR pan and hover controls are sluggish",
                "facts": ["drawable_wait_on_ui_render_path", "paint_and_both_scrollbars_render_synchronously", "navigation_hover_grabs_and_repaints_viewport"],
                "inference": "the main event loop performs avoidable drawable/CI work per UI event",
                "correction": ["display_link_supplies_drawable_without_next_drawable", "core_image_texture_encoding_is_off_appkit_thread", "ui_events_are_coalesced_before_renderer", "navigation_sampling_is_cached_and_bounded"],
                "verification_ids": ["ST-HDR-DISPLAYLINK-LATEST-ONLY", "UT-HDR-NAV-SAMPLING-LATENCY", "SYS-HDR-INTERACTION-RESPONSIVENESS"],
            },
            {
                "symptom": "navigation fade exposes a standard rectangle",
                "facts": ["navigation_fade_uses_rectangular_graphics_effect"],
                "inference": "the effect composites a rectangular source surface independently of rounded pixels",
                "correction": ["navigation_fades_only_custom_painted_pixels"],
                "verification_ids": ["UT-HDR-NAV-TRANSPARENT-FADE", "SYS-HDR-NAV-TRANSPARENT-SURFACE"],
            },
            {
                "symptom": "DNG HDR loses detail compared with Quick Look",
                "facts": ["dng_hdr_filter_overrides_camera_baseline", "dng_uses_generic_raw_output_as_primary", "processed_preview_is_full_resolution", "gain_map_is_half_resolution"],
                "inference": "the generic altered RAW graph omits the camera-authored processed rendering paired with the gain map",
                "correction": ["dng_uses_processed_preview_and_authored_gain_map", "camera_baseline_is_not_mutated", "processed_dng_avoids_post_gainmap_intermediate"],
                "verification_ids": ["IT-HDR-DNG-QUICKLOOK-DETAIL", "SYS-HDR-RAW-DNG-EDR"],
            },
            {
                "symptom": "NEF zoom briefly shows a stale frame",
                "facts": ["renderer_has_no_latest_only_display_link_gate"],
                "inference": "multiple geometry generations can drain in presentation order after the UI has advanced",
                "correction": ["latest_state_overwrites_while_one_frame_is_in_flight"],
                "verification_ids": ["SYS-HDR-DISPLAYLINK-LATEST-ONLY", "SYS-HDR-NEF-ZOOM-NO-GHOST"],
            },
        ],
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "passed": passed}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
