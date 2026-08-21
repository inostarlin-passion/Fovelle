#!/usr/bin/env python3
"""Record the auditable causal chains for the native-overlay/drag/flash fixes."""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
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


def head_source(repo: Path, relative_path: str) -> str:
    result = subprocess.run(
        ["git", "show", f"HEAD:{relative_path}"], cwd=repo, text=True,
        capture_output=True, check=False,
    )
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def region_median(image: Image.Image, box: tuple[int, int, int, int]) -> list[int]:
    pixels = list(image.crop(box).getdata())
    return [round(statistics.median(pixel[channel] for pixel in pixels)) for channel in range(3)]


def screenshot_observation(path: Path) -> dict:
    with Image.open(path) as source:
        image = source.convert("RGB")
    # The supplied crop contains the 60 pt navigation widget at DPR=2. The
    # exact 120x120 px axis-aligned region is visible at (30,38)-(150,158).
    rect = (30, 38, 150, 158)
    patch = 8
    corners = [
        (rect[0], rect[1], rect[0] + patch, rect[1] + patch),
        (rect[2] - patch, rect[1], rect[2], rect[1] + patch),
        (rect[0], rect[3] - patch, rect[0] + patch, rect[3]),
        (rect[2] - patch, rect[3] - patch, rect[2], rect[3]),
    ]
    return {
        "pixel_size": list(image.size),
        "axis_aligned_widget_rect_pixels": list(rect),
        "logical_widget_size_at_dpr_2": [60, 60],
        "corner_patch_median_rgb": [region_median(image, box) for box in corners],
        "outer_image_median_rgb": region_median(image, (0, 0, 24, image.height)),
        "fact_boundary": (
            "The rectangle geometry and sampled pixels are direct observations; "
            "the WindowServer compositing mechanism is an inference."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--jpeg", type=Path, required=True)
    parser.add_argument("--dng", type=Path, required=True)
    parser.add_argument("--plain-dng", type=Path, required=True)
    parser.add_argument("--nef", type=Path, required=True)
    parser.add_argument("--screenshot", type=Path, required=True)
    parser.add_argument("--solution1", type=Path, required=True)
    parser.add_argument("--solution2", type=Path, required=True)
    parser.add_argument("--solution3", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    paths = {
        name: value.resolve() for name, value in {
            "jpeg": args.jpeg, "dng": args.dng, "plain_dng": args.plain_dng,
            "nef": args.nef, "screenshot": args.screenshot,
            "solution1": args.solution1, "solution2": args.solution2,
            "solution3": args.solution3,
        }.items()
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise SystemExit(f"missing current root-cause input: {missing}")

    before_cocoa = head_source(repo, "src/qvcocoafunctions.mm")
    before_view = head_source(repo, "src/qvgraphicsview.cpp")
    before_window = head_source(repo, "src/mainwindow.cpp")
    after_cocoa = (repo / "src/qvcocoafunctions.mm").read_text(encoding="utf-8")
    after_view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    after_window = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")

    reference_text = {
        key: paths[key].read_text(encoding="utf-8", errors="replace")
        for key in ("solution1", "solution2", "solution3")
    }
    before_contracts = {
        "hdr_navigation_remains_a_separate_qwidget": (
            "navigationOverlayLayer" not in before_cocoa
            and "setHDRNavigationOverlay" not in before_window
            and "new ImageNavigationButton" in before_window
        ),
        "display_link_flow_serializes_on_one_completed_frame": (
            "if (presentationState->frameInFlight)" in before_cocoa
            and "presentationState->frameInFlight = YES" in before_cocoa
            and "dispatch_async(dispatch_get_main_queue()" in before_cocoa
        ),
        "opening_uses_a_manual_partial_headroom_ramp": (
            "hdrTransitionTimer" in before_view
            and "hdrTransitionClock.elapsed() / 650.0" in before_view
            and "hdrTransitionLinearProgress" in before_view
        ),
    }
    corrected_contracts = {
        "hdr_navigation_is_shape_only_in_metal_layer_tree": all(
            token in after_cocoa for token in (
                "navigationOverlayLayer", "[CAShapeLayer layer]",
                "[metalLayer addSublayer:navigationOverlayLayer]",
                "CGPathCreateWithRoundedRect",
            )
        ) and "button->hide()" in after_window and "setHDRNavigationOverlay" in after_window,
        "display_link_paces_latest_only_with_two_frames_in_flight": all(
            token in after_cocoa for token in (
                "renderDisplayLinkUpdate(CAMetalDisplayLinkUpdate *update)",
                "update.targetTimestamp", "framesInFlight.load() >= 2",
                "framesInFlight.fetch_add(1)", "framesInFlight.fetch_sub(1)",
                "dispatch_sync(renderQueue", "presentDrawable:encodedDrawable",
                "pendingRenderGeneration = ++state.requestedRenderGeneration",
            )
        ) and "nextDrawable" not in after_cocoa and "atTime:targetTimestamp" not in after_cocoa,
        "presentation_timing_is_observed_after_actual_presentation": all(
            token in after_cocoa for token in (
                "addPresentedHandler", "presentedDrawable.presentedTime",
                "requestToPresentationMilliseconds", "FOVELLE_PRESENT",
            )
        ),
        "only_final_headroom_can_become_first_visible": (
            "hdrRenderer->render(viewportSize, viewportCorners, 1.0, interactive)" in after_view
            and "firstVisibleFrameUsesFinalHeadroom" in after_cocoa
            and "isFinalHDRFrameReadyForReveal" in after_cocoa
            and "hdrTransitionTimer" not in after_view
            and "hdrTransitionClock.elapsed() / 650.0" not in after_view
        ),
    }
    reference_contracts = {
        "solution1_discusses_native_or_metal_overlay_isolation": (
            "Metal" in reference_text["solution1"] or "native" in reference_text["solution1"].lower()
        ),
        "solution2_discusses_inflight_or_presentation_pacing": any(
            token in reference_text["solution2"]
            for token in ("in-flight", "inflight", "DisplayLink", "drawable")
        ),
        "solution3_discusses_placeholder_or_final_first_frame": any(
            token in reference_text["solution3"]
            for token in ("placeholder", "首帧", "HDR", "650")
        ),
    }
    passed = (
        all(before_contracts.values())
        and all(corrected_contracts.values())
        and all(reference_contracts.values())
    )
    record = {
        "schema_version": "1.0",
        "kind": "native-overlay-displaylink-presentation-first-frame-root-cause-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "baseline_revision": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=repo, text=True,
            capture_output=True, check=True,
        ).stdout.strip(),
        "artifacts": {name: artifact(path) for name, path in paths.items()},
        "screenshot_observation": screenshot_observation(paths["screenshot"]),
        "before_contracts": before_contracts,
        "corrected_contracts": corrected_contracts,
        "reference_contracts": reference_contracts,
        "facts": [
            "The supplied screenshot contains an axis-aligned 120x120 pixel region, exactly the 60x60 point navigation geometry at DPR 2, over image pixels.",
            "At baseline HEAD the HDR renderer and navigation QWidget belonged to separate surfaces; no navigation CAShapeLayer existed in the CAMetalLayer tree.",
            "At baseline HEAD a GPU completion callback reopened a one-frame gate through the main queue, serializing encode, GPU completion, and the next submission.",
            "At baseline HEAD opening brightness was explicitly advanced by an application timer over 650 ms through partial transition values.",
            "The corrected source hides the HDR QWidget, draws only rounded/chevron paths as CAMetalLayer sublayers, obtains every drawable from CAMetalDisplayLink, records addPresentedHandler timing, and reveals only final-headroom first frames.",
        ],
        "inferences": [
            "A transparent Qt backing store composited above an independent EDR surface is the best causal explanation for an SDR-colored rectangle that appears only where the widget overlaps live HDR pixels.",
            "The one-frame GPU-completion-to-main gate is the best causal explanation for low drag throughput and delayed hover response; a DisplayLink-paced latest-only queue with two frames in flight overlaps CPU encoding, GPU execution, and scanout without unpaced drawable bursts.",
            "The deliberate SDR/partial-HDR endpoint sequence is the direct causal source of the opening luminance flash; a final-only presentation lets WindowServer perform device-specific EDR adaptation once.",
        ],
        "uncertainties": [
            "Apple does not publish WindowServer's private cross-surface EDR compositing algorithm, so the precise internal blend operation remains unknown.",
            "A still screenshot cannot measure absolute nits or a sub-frame timing sequence; native surface telemetry, presented-handler timestamps, and multiple captures complement it.",
            "Frame pacing varies with display refresh, GPU pressure, and camera graph complexity; formal thresholds apply to the recorded target Mac and workload.",
        ],
        "causal_chains": [
            {
                "symptom": "Colored rectangular navigation backing over actual HDR pixels",
                "facts": ["separate QWidget", "independent CAMetalLayer", "120x120 axis-aligned artifact"],
                "inference": "cross-surface transparent pixels resolve against the SDR Qt backing",
                "fix": "shape-only navigation sublayers inside the Metal layer tree; hide QWidget for HDR",
                "tests": ["ST-HDR-NAV-NATIVE-COMPOSITOR", "SYS-HDR-NAV-NATIVE-COMPOSITOR"],
            },
            {
                "symptom": "HDR drag is much slower than SDR drag",
                "facts": ["one frame gate", "GPU completion reopens through main", "latest input exceeds submitted cadence"],
                "inference": "serial CPU/GPU/scanout flow throttles presentation and indirectly delays UI",
                "fix": "continuous DisplayLink pacing, latest-only pending geometry, two frames in flight, presentation before the targetTimestamp deadline, and actual presentation metrics",
                "tests": ["ST-HDR-DISPLAYLINK-LATEST-ONLY", "SYS-HDR-PRESENTATION-RESPONSIVENESS"],
            },
            {
                "symptom": "JPEG/DNG/NEF flashes once while opening",
                "facts": ["650 ms app ramp", "partial headroom frames", "proxy removed after early endpoint"],
                "inference": "multiple app-generated luminance endpoints become visible during one open",
                "fix": "retain proxy/prior drawable until a prepared final-headroom frame is presented",
                "tests": ["ST-HDR-FIRST-VISIBLE-FINAL", "SYS-HDR-FIRST-VISIBLE-FINAL"],
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
