#!/usr/bin/env python3
"""Run the compiled Qt test process as a system-level fullscreen zoom smoke test."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from statistics import mean


FUNCTIONAL_CASES = (
    "testFitZoomSurvivesInverseWheelStepsAndFullscreenResize",
)

THRESHOLDS = {
    "response_average_ms": 2000.0,
    "response_p99_ms": 2000.0,
    "response_max_ms": 2000.0,
    "transition_ack_throughput_per_second": 0.5,
}


def percentile99(values: list[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * 0.99) - 1)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    started = time.perf_counter()
    result = subprocess.run(
        [str(binary)],
        text=True,
        capture_output=True,
        env={**os.environ, "QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1"},
        check=False,
    )
    output = result.stdout + result.stderr
    cases = [
        {
            "id": f"TC-FS-{index:02d}",
            "test": f"GraphicsViewTests::{name}",
            "status": "passed" if re.search(rf"PASS\s+: GraphicsViewTests::{re.escape(name)}\(\)", output) else "failed",
        }
        for index, name in enumerate(FUNCTIONAL_CASES, start=1)
    ]
    fullscreen_metrics = [
        {"phase": phase, "milliseconds": float(milliseconds)}
        for phase, milliseconds in re.findall(r"FS_METRIC\s+(enter|exit)_ms=([0-9]+(?:\.[0-9]+)?)", output)
    ]
    response_values = [item["milliseconds"] for item in fullscreen_metrics]
    response_total_seconds = sum(response_values) / 1000.0
    performance = {
        "response_average_ms": mean(response_values) if response_values else None,
        "response_p99_ms": percentile99(response_values),
        "response_max_ms": max(response_values, default=None),
        "transition_ack_throughput_per_second": len(response_values) / max(response_total_seconds, 0.001),
        "sample_count": len(response_values),
        "metric_definition": "time from synthetic key/shortcut request until Qt reports the target full-screen state",
    }
    performance_flags = {
        "average": performance["response_average_ms"] is not None and performance["response_average_ms"] <= THRESHOLDS["response_average_ms"],
        "p99": performance["response_p99_ms"] is not None and performance["response_p99_ms"] <= THRESHOLDS["response_p99_ms"],
        "maximum": performance["response_max_ms"] is not None and performance["response_max_ms"] <= THRESHOLDS["response_max_ms"],
        "throughput": performance["transition_ack_throughput_per_second"] >= THRESHOLDS["transition_ack_throughput_per_second"],
    }
    record = {
        "kind": "system-functional",
        "binary": str(binary),
        "return_code": result.returncode,
        "elapsed_seconds": time.perf_counter() - started,
        "functional_cases": cases,
        "fullscreen_metrics": fullscreen_metrics,
        "performance": performance,
        "thresholds": THRESHOLDS,
        "performance_flags": performance_flags,
        "passed": result.returncode == 0 and all(item["status"] == "passed" for item in cases) and all(performance_flags.values()),
        "output_tail": output[-12000:],
        "limitations": [
            "The test process sends deterministic Qt key events; it does not depend on a human keyboard or Accessibility permission.",
            "The separate app-launch/resource probe records process-level timing and resource observations.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
