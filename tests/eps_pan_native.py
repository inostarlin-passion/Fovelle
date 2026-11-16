#!/usr/bin/env python3
"""Validate EPS drag geometry and the PDF sampling path with the native HID helper."""

import argparse
import math
import os
from pathlib import Path
import re
import statistics
import subprocess


def measure_drag(helper, app, image, label):
    result = subprocess.run(
        [helper, "--app", app, "--image", image],
        env={**os.environ, "FOVELLE_SDR_PERF_LOG": "1"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=90,
    )
    print(result.stdout, end="")
    if result.returncode or "NATIVE_DRAG_RESULT passed=true" not in result.stdout:
        raise RuntimeError(f"{label} native drag geometry failed")

    log = re.search(r"^NATIVE_DRAG_LOG (.+)$", result.stdout, re.MULTILINE)
    if log is None:
        raise RuntimeError(f"{label} app log missing")
    frames = []
    durations = []
    lines = Path(log[1]).read_text().splitlines()
    for render, paint, timing in zip(lines, lines[1:], lines[2:]):
        if "FOVELLE_VECTOR_PAINT" not in paint or "mouse_pan_active= true" not in paint:
            continue
        if "update_mode= full" not in paint or not re.search(r"dirty_ratio= 1$", paint):
            raise RuntimeError(f"{label} drag paint did not cover the viewport")
        match = re.search(r"FOVELLE_SDR_PAINT duration_ms= ([\d.]+)", timing)
        if match is None:
            raise RuntimeError(f"{label} drag paint timing missing")
        frames.append("format=pdf" in render and "sampling= nearest" in render)
        durations.append(float(match[1]))
    if not frames:
        raise RuntimeError(f"{label} app recorded no drag frames")
    median = statistics.median(durations)
    p95 = sorted(durations)[math.ceil(0.95 * len(durations)) - 1]
    print(
        f"EPS_NATIVE_PAN {label} full_frames={len(frames)} aligned_frames={sum(frames)} "
        f"paint_p50_ms={median:.3f} paint_p95_ms={p95:.3f}"
    )
    return sum(frames) > len(frames) // 2, median, p95


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, type=Path)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--baseline-app", type=Path)
    args = parser.parse_args()

    try:
        if args.baseline_app:
            _, old_median, old_p95 = measure_drag(
                args.helper, args.baseline_app, args.image, "baseline"
            )
        aligned, median, p95 = measure_drag(args.helper, args.app, args.image, "candidate")
    except RuntimeError as error:
        print(error)
        return 1
    if not aligned:
        print("At most half of the candidate drag frames used aligned PDF tiles")
        return 1
    if args.baseline_app and (median >= old_median or p95 >= old_p95):
        print("Candidate median and p95 drag paint durations must improve")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
