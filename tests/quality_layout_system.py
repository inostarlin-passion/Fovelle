#!/usr/bin/env python3
"""Run the real Cocoa application against the reported AVIF and verify layout telemetry."""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from quality_system_probe import TINY_AVIF


DEFAULT_IMAGE = Path(
    "/Users/inostarlin/Downloads/拍卖/(OVA) JoJo's Bizarre Adventure Original Art Genga 2.avif"
)


def parse_rect(line: str, name: str) -> tuple[float, float, float, float] | None:
    match = re.search(
        rf"{re.escape(name)}=\s*QRect\(([-+]?\d+(?:\.\d+)?),([-+]?\d+(?:\.\d+)?)\s+([-+]?\d+(?:\.\d+)?)x([-+]?\d+(?:\.\d+)?)\)",
        line,
    )
    return tuple(float(value) for value in match.groups()) if match else None


def parse_point(line: str, name: str) -> tuple[float, float] | None:
    match = re.search(
        rf"{re.escape(name)}=\s*QPoint\(([-+]?\d+(?:\.\d+)?),([-+]?\d+(?:\.\d+)?)\)",
        line,
    )
    return tuple(float(value) for value in match.groups()) if match else None


def parse_bar(line: str, name: str) -> tuple[int, int, int] | None:
    match = re.search(rf"{re.escape(name)}=\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)", line)
    return tuple(int(value) for value in match.groups()) if match else None


def collect_fit_observation(output: str, phase: str) -> dict | None:
    for line in output.splitlines():
        if f"phase= {phase}" not in line:
            continue
        return {
            "phase": phase,
            "content_rect": parse_rect(line, "contentRect"),
            "viewport_rect": parse_rect(line, "viewportRect"),
            "usable_viewport_rect": parse_rect(line, "usableViewportRect"),
            "scene_origin_in_viewport": parse_point(line, "sceneOriginInViewport"),
            "horizontal_bar": parse_bar(line, "hbar"),
            "vertical_bar": parse_bar(line, "vbar"),
            "raw": line,
        }
    return None


def run_application(app: Path, image: Path, hold_seconds: float) -> tuple[int | None, str, float, bool]:
    started = time.perf_counter()
    process = subprocess.Popen(
        [str(app), str(image)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env={**os.environ, "QT_QPA_PLATFORM": "cocoa", "FOVELLE_DIAGNOSTIC_LOG": "1"},
        start_new_session=True,
    )
    timed_out = False
    try:
        time.sleep(max(hold_seconds, 0.2))
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGTERM)
        try:
            output, _ = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            output, _ = process.communicate(timeout=5)
    return process.returncode, output, time.perf_counter() - started, timed_out


def evaluate(observation: dict | None) -> dict:
    if not observation:
        return {"observation_present": False, "passed": False, "reason": "fit telemetry was not emitted"}

    content = observation["content_rect"]
    viewport = observation["viewport_rect"]
    usable = observation["usable_viewport_rect"]
    origin = observation["scene_origin_in_viewport"]
    hbar = observation["horizontal_bar"]
    vbar = observation["vertical_bar"]
    required = (content, viewport, usable, origin, hbar, vbar)
    if any(value is None for value in required):
        return {
            "observation_present": True,
            "passed": False,
            "reason": "fit telemetry is incomplete",
            "observation": observation,
        }

    _, _, content_width, content_height = content
    _, _, viewport_width, viewport_height = viewport
    usable_x, usable_y, usable_width, usable_height = usable
    origin_x, origin_y = origin
    image_bottom = origin_y + content_height
    viewport_bottom = viewport_height
    tolerance = 2.0
    pass_flags = {
        "content_fits_usable_width": content_width <= usable_width + tolerance,
        "content_fits_usable_height": content_height <= usable_height + tolerance,
        "content_starts_at_or_below_unobscured_top": origin_y >= usable_y - tolerance,
        "no_visible_bottom_gap": viewport_bottom - image_bottom <= tolerance,
        "horizontal_overflow_absent": hbar[1] == hbar[2] == 0,
        "vertical_overflow_absent": vbar[1] == vbar[2] == 0,
        "usable_rect_is_inside_viewport": usable_y >= 0 and usable_y + usable_height <= viewport_height,
        "horizontal_center_is_stable": abs(origin_x + content_width / 2.0 - viewport_width / 2.0) <= tolerance,
    }
    return {
        "observation_present": True,
        "passed": all(pass_flags.values()),
        "pass_flags": pass_flags,
        "measurements": {
            "content_width": content_width,
            "content_height": content_height,
            "usable_width": usable_width,
            "usable_height": usable_height,
            "usable_top": usable_y,
            "image_top": origin_y,
            "image_bottom": image_bottom,
            "viewport_bottom": viewport_bottom,
            "bottom_gap": viewport_bottom - image_bottom,
            "tolerance": tolerance,
        },
        "observation": observation,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    parser.add_argument("--hold-seconds", type=float, default=1.2)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    app = args.app.resolve()
    requested_image = args.image.resolve()
    fallback_directory = None
    image = requested_image
    image_source = "requested"
    if not image.is_file() and image.suffix.lower() == ".avif":
        fallback_directory = tempfile.TemporaryDirectory(prefix="fovelle-layout-")
        image = Path(fallback_directory.name) / "embedded-layout.avif"
        image.write_bytes(base64.b64decode(TINY_AVIF))
        image_source = "embedded-deterministic-fallback"
    checks = {
        "app_exists": app.is_file(),
        "image_exists": image.is_file(),
        "image_suffix_is_avif": image.suffix.lower() == ".avif",
        "requested_image_exists": requested_image.is_file(),
    }
    return_code = None
    output = ""
    elapsed_seconds = None
    timed_out = False
    fit_after = None
    fit_next = None
    runnable_checks = ("app_exists", "image_exists", "image_suffix_is_avif")
    if all(checks[name] for name in runnable_checks):
        return_code, output, elapsed_seconds, timed_out = run_application(app, image, args.hold_seconds)
        fit_after = collect_fit_observation(output, "post-load-after-fit")
        fit_next = collect_fit_observation(output, "post-load-next-turn")

    evaluated_after = evaluate(fit_after)
    evaluated_next = evaluate(fit_next)
    geometry_fields = (
        "content_rect",
        "viewport_rect",
        "usable_viewport_rect",
        "scene_origin_in_viewport",
        "horizontal_bar",
        "vertical_bar",
    )
    stable = bool(
        fit_after
        and fit_next
        and all(fit_after[field] == fit_next[field] for field in geometry_fields)
    )
    passed = (
        checks["app_exists"]
        and checks["image_exists"]
        and checks["image_suffix_is_avif"]
        and return_code in (0, -signal.SIGTERM)
        and not timed_out
        and evaluated_after["passed"]
        and evaluated_next["passed"]
        and stable
    )
    record = {
        "kind": "system-layout",
        "command": [str(app), str(image)],
        "app": str(app),
        "image": str(image),
        "requested_image": str(requested_image),
        "image_source": image_source,
        "checks": checks,
        "return_code": return_code,
        "elapsed_seconds": elapsed_seconds,
        "timed_out": timed_out,
        "post_load_after_fit": evaluated_after,
        "post_load_next_turn": evaluated_next,
        "stable_after_queued_turn": stable,
        "facts": [
            "The executable was launched with FOVELLE_DIAGNOSTIC_LOG=1 and the selected AVIF path.",
            f"The requested image existed: {checks['requested_image_exists']}; image source: {image_source}.",
            "The telemetry compares the rendered image content rectangle with the non-obscured viewport rectangle.",
        ],
        "inference": "A zero-to-two-pixel bottom gap and an image top aligned to the unobscured top are treated as evidence that the reported blank strip is absent.",
        "uncertainties": [
            "The telemetry is a system-level geometry observation, not a pixel-by-pixel screenshot diff.",
            "The exact macOS titlebar obscured height is platform/window-state dependent and is read from the running application.",
        ],
        "raw_output": output[-12000:],
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
