#!/usr/bin/env python3
"""Launch the real Fovelle Cocoa app with EPS and measure observable response."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import selectors
import signal
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean


THRESHOLDS = {
    "response_average_seconds_max": 5.0,
    "response_p99_seconds_max": 8.0,
    "response_max_seconds_max": 10.0,
    "response_throughput_runs_per_second_min": 0.2,
}


def percentile99(values: list[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * 0.99) - 1)]


def default_sample() -> Path:
    return Path(
        "/Users/inostarlin/Downloads/Download-on-the-App-Store/US/Download_on_App_Store/Black_lockup/EPS/Download_on_the_App_Store_Badge_US-UK_blk_092917.eps"
    )


def write_fallback_sample(directory: Path) -> Path:
    path = directory / "system-fallback.eps"
    path.write_text(
        "%!PS-Adobe-3.0 EPSF-3.0\n"
        "%%BoundingBox: 0 0 1200 400\n"
        "%%HiResBoundingBox: 0 0 1200 400\n"
        "%%Pages: 1\n"
        "%%EndComments\n"
        "0 setgray 0 0 1200 400 rectfill\n"
        "1 setgray 100 100 300 200 rectfill 800 100 300 200 rectfill\n"
        "showpage\n"
        "%%EOF\n",
        encoding="ascii",
    )
    return path


def launch_once(app: Path, image: Path, hold_seconds: float) -> dict:
    started = time.perf_counter()
    process = subprocess.Popen(
        [str(app), str(image)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env={
            **os.environ,
            "QT_QPA_PLATFORM": "cocoa",
            "FOVELLE_DIAGNOSTIC_LOG": "1",
            "FOVELLE_VECTOR_RENDER_LOG": "1",
            "QV_DISABLE_ONLINE_VERSION_CHECK": "1",
        },
        start_new_session=True,
        bufsize=0,
    )
    selector = selectors.DefaultSelector()
    if process.stdout:
        selector.register(process.stdout, selectors.EVENT_READ, "stdout")
    if process.stderr:
        selector.register(process.stderr, selectors.EVENT_READ, "stderr")
    output = {"stdout": bytearray(), "stderr": bytearray()}
    response_seconds: float | None = None
    deadline = started + max(hold_seconds, 0.2)
    while time.perf_counter() < deadline:
        for key, _ in selector.select(timeout=0.05):
            try:
                data = os.read(key.fileobj.fileno(), 8192)
            except OSError:
                data = b""
            if data:
                output[key.data].extend(data)
                if response_seconds is None and b"FOVELLE_VIEW" in output[key.data]:
                    response_seconds = time.perf_counter() - started
        if process.poll() is not None:
            break

    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    try:
        remaining_stdout, remaining_stderr = process.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        remaining_stdout, remaining_stderr = process.communicate(timeout=3)
    output["stdout"].extend(remaining_stdout or b"")
    output["stderr"].extend(remaining_stderr or b"")
    selector.close()
    stdout = bytes(output["stdout"]).decode(errors="replace")
    stderr = bytes(output["stderr"]).decode(errors="replace")
    combined = stdout + stderr
    if response_seconds is None and "FOVELLE_VIEW" in combined:
        response_seconds = time.perf_counter() - started
    geometry = re.findall(r"itemRect= QRectF\([^)]* ([0-9.]+)x([0-9.]+)\)", combined)
    nonzero_geometry = [
        (float(width), float(height))
        for width, height in geometry
        if float(width) > 0 and float(height) > 0
    ]
    vector_renders = re.findall(
        r"FOVELLE_VECTOR_RENDER format=pdf source=vector.*?tilePixels= QSize\((\d+), (\d+)\)",
        combined,
    )
    nonzero_vector_tiles = [
        (int(width), int(height))
        for width, height in vector_renders
        if int(width) > 0 and int(height) > 0
    ]
    unsupported = any(
        marker in combined.lower()
        for marker in (
            "unsupported image format",
            "eps rendering requires ghostscript",
            "ghostscript could not",
            "ghostscript exceeded",
        )
    )
    return {
        "image": str(image),
        "response_seconds": response_seconds,
        "elapsed_seconds": time.perf_counter() - started,
        "return_code": process.returncode,
        "terminated": True,
        "decoded_geometry_observed": bool(nonzero_geometry),
        "vector_pdf_render_observed": bool(nonzero_vector_tiles),
        "unsupported_error_absent": not unsupported,
        "geometry_observations": geometry,
        "vector_tile_observations": vector_renders,
        "stdout_tail": stdout[-6000:],
        "stderr_tail": stderr[-6000:],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--image", type=Path, default=None)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--hold-seconds", type=float, default=2.0)
    args = parser.parse_args()

    app = args.app.resolve()
    with tempfile.TemporaryDirectory(prefix="fovelle-eps-system-") as temporary_directory:
        fixture_directory = Path(temporary_directory)
        requested_image = (args.image or default_sample()).expanduser().resolve()
        uses_external_sample = requested_image.is_file()
        image = requested_image if uses_external_sample else write_fallback_sample(fixture_directory)
        runs = (
            [launch_once(app, image, args.hold_seconds) for _ in range(max(args.runs, 1))]
            if app.is_file()
            else []
        )

    responses = [run["response_seconds"] for run in runs if run["response_seconds"] is not None]
    total_elapsed = sum(run["elapsed_seconds"] for run in runs)
    metrics = {
        "response_average_seconds": mean(responses) if responses else None,
        "response_p99_seconds": percentile99(responses),
        "response_max_seconds": max(responses, default=None),
        "response_throughput_runs_per_second": len(responses) / total_elapsed if total_elapsed else None,
        "response_observation_count": len(responses),
    }
    pass_flags = {
        "app_exists": app.is_file(),
        "all_runs_completed": len(runs) == max(args.runs, 1),
        "all_runs_observed_decoded_geometry": bool(runs) and all(run["decoded_geometry_observed"] for run in runs),
        "all_runs_observed_vector_pdf_render": bool(runs)
        and all(run["vector_pdf_render_observed"] for run in runs),
        "all_runs_absent_unsupported_error": bool(runs) and all(run["unsupported_error_absent"] for run in runs),
        "response_average": metrics["response_average_seconds"] is not None
        and metrics["response_average_seconds"] <= THRESHOLDS["response_average_seconds_max"],
        "response_p99": metrics["response_p99_seconds"] is not None
        and metrics["response_p99_seconds"] <= THRESHOLDS["response_p99_seconds_max"],
        "response_max": metrics["response_max_seconds"] is not None
        and metrics["response_max_seconds"] <= THRESHOLDS["response_max_seconds_max"],
        "response_throughput": metrics["response_throughput_runs_per_second"] is not None
        and metrics["response_throughput_runs_per_second"] >= THRESHOLDS["response_throughput_runs_per_second_min"],
    }
    result = {
        "schema_version": "1.0",
        "kind": "eps-system-test-evidence",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "test_level": "system",
        "app": str(app),
        "image": str(image),
        "requested_image": str(requested_image),
        "uses_external_sample": uses_external_sample,
        "runs_per_case": max(args.runs, 1),
        "hold_seconds": args.hold_seconds,
        "runs": runs,
        "metrics": metrics,
        "thresholds": THRESHOLDS,
        "pass_flags": pass_flags,
        "cases": [
            {
                "id": "SYS-EPS-OPEN",
                "passed": all(
                    pass_flags[key]
                    for key in (
                        "app_exists",
                        "all_runs_completed",
                        "all_runs_observed_decoded_geometry",
                        "all_runs_observed_vector_pdf_render",
                        "all_runs_absent_unsupported_error",
                    )
                ),
                "actual": {
                    key: pass_flags[key]
                    for key in (
                        "app_exists",
                        "all_runs_completed",
                        "all_runs_observed_decoded_geometry",
                        "all_runs_observed_vector_pdf_render",
                        "all_runs_absent_unsupported_error",
                    )
                },
            },
            {
                "id": "SYS-EPS-TIME",
                "passed": all(
                    pass_flags[key]
                    for key in (
                        "response_average",
                        "response_p99",
                        "response_max",
                        "response_throughput",
                    )
                ),
                "actual": {
                    "metrics": metrics,
                    "thresholds": THRESHOLDS,
                },
            },
        ],
        "facts": [
            "The system case launches the built Fovelle.app executable with the EPS path as a command-line input.",
            "Decoded geometry is observed through the application's existing non-invasive FOVELLE_VIEW diagnostic output.",
            "The vector-render diagnostic must report format=pdf, source=vector, and a non-empty final-device tile; logical item geometry is allowed to remain the EPS 120x40 BoundingBox.",
            "The response window is process launch through the first observed decoded viewport record; average, P99, maximum, and throughput are retained.",
        ],
        "inferences": [
            "A persistent PDF-vector diagnostic without a renderer error supports the inference that the end-to-end path kept the normalized PostScript document rather than the embedded preview.",
        ],
        "uncertainties": [
            "The timing values are host observations and include macOS process/window startup; they are not a cross-machine performance guarantee.",
            "The system fixture matrix covers the supplied DOS EPS and a no-preview deterministic vector EPS, but not every PostScript dialect or external font dependency.",
        ],
        "passed": all(pass_flags.values()),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
