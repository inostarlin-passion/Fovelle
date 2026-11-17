#!/usr/bin/env python3
"""Validate vector drag geometry and sampling with the native HID helper."""

import argparse
import math
import os
from pathlib import Path
import re
import statistics
import subprocess


def measure_drag(helper, app, image, label, vector_format):
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
    opaque_copies = 0
    lines = Path(log[1]).read_text().splitlines()
    for render, paint, timing in zip(lines, lines[1:], lines[2:]):
        if "FOVELLE_VECTOR_PAINT" not in paint or "mouse_pan_active= true" not in paint:
            continue
        if "update_mode= full" not in paint or not re.search(r"dirty_ratio= 1$", paint):
            raise RuntimeError(f"{label} drag paint did not cover the viewport")
        match = re.search(r"FOVELLE_SDR_PAINT duration_ms= ([\d.]+)", timing)
        if match is None:
            raise RuntimeError(f"{label} drag paint timing missing")
        frames.append(f"format={vector_format}" in render and "sampling= nearest" in render)
        durations.append(float(match[1]))
        opaque_copies += "opaque_copy= true" in render
    if not frames:
        raise RuntimeError(f"{label} app recorded no drag frames")
    median = statistics.median(durations)
    p95 = sorted(durations)[math.ceil(0.95 * len(durations)) - 1]
    print(
        f"VECTOR_NATIVE_PAN format={vector_format} {label} full_frames={len(frames)} aligned_frames={sum(frames)} "
        f"opaque_copies={opaque_copies} paint_p50_ms={median:.3f} paint_p95_ms={p95:.3f}"
    )
    return sum(frames) > len(frames) // 2, opaque_copies, median, p95


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, type=Path)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--baseline-app", type=Path)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--require-opaque-copy", action="store_true")
    args = parser.parse_args()

    vector_format = {".eps": "pdf", ".svg": "svg"}.get(args.image.suffix.lower())
    if vector_format is None or args.repeat < 1:
        parser.error("provide an EPS or SVG image and a positive repeat count")

    baseline = []
    candidate = []
    try:
        for iteration in range(1, args.repeat + 1):
            if args.baseline_app:
                _, _, median, p95 = measure_drag(
                    args.helper, args.baseline_app, args.image, f"baseline-{iteration}", vector_format
                )
                baseline.append((median, p95))
            aligned, opaque_copies, median, p95 = measure_drag(
                args.helper, args.app, args.image, f"candidate-{iteration}", vector_format
            )
            if not aligned:
                print(f"At most half of candidate drag frames used aligned {vector_format} tiles")
                return 1
            if args.require_opaque_copy and not opaque_copies:
                print("No candidate drag frames used the opaque tile copy")
                return 1
            candidate.append((median, p95))
    except RuntimeError as error:
        print(error)
        return 1
    if baseline:
        old_median, old_p95 = map(statistics.median, zip(*baseline))
        median, p95 = map(statistics.median, zip(*candidate))
        print(
            f"VECTOR_NATIVE_PAN_COMPARISON trials={args.repeat} "
            f"p50_ms={old_median:.3f}->{median:.3f} p95_ms={old_p95:.3f}->{p95:.3f}"
        )
        if median >= old_median or p95 >= old_p95:
            print("Candidate median and p95 drag paint durations must improve")
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
