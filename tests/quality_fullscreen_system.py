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
    "testFullscreenDefaultShortcutIsEnterAndConfigurable",
    "testEnterDoesNotBypassClearedFullscreenShortcut",
    "testConfiguredFullscreenShortcutStillWorks",
    "testFullScreenPresentationKeepsMoving",
    "testFullScreenLayoutPaintBudget",
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


def motion_summary(output: str) -> dict:
    """Require every row, direction and cycle; absent telemetry cannot pass."""
    metrics = [json.loads(line.split("FULLSCREEN_MOTION ", 1)[1])
               for line in output.splitlines() if "FULLSCREEN_MOTION {" in line]
    expected = {(f"{title}-{kind}-{load}", cycle, entering)
                for title in ("visible", "hidden") for kind in ("raster", "vector")
                for load in ("idle", "busy") for cycle in range(2)
                for entering in (True, False)}
    observed = [(m["row"], m["cycle"], m["entering"]) for m in metrics]
    busy = [m for m in metrics if m["row"].endswith("-busy")]
    passed = (len(observed) == len(expected) and set(observed) == expected
              and all(m["completed"] and m["interior_samples"] >= 8
                      and m["max_frozen_ms"] <= 80.0 for m in metrics)
              and all(m["injected"] and m["blocked_advance"] >= 0.08
                      and m["blocked_image_delta"] >= 5.0 for m in busy))
    return {"passed": passed, "sample_count": len(metrics),
            "max_frozen_ms": max((m["max_frozen_ms"] for m in metrics), default=None),
            "minimum_busy_advance": min((m["blocked_advance"] for m in busy), default=None),
            "metrics": metrics,
            "metric_definition": "Core Animation presentation trajectory; not physical display frame times"}


def paint_summary(output: str) -> dict:
    metrics = [json.loads(line.split("FULLSCREEN_PAINT_BUDGET ", 1)[1])
               for line in output.splitlines() if "FULLSCREEN_PAINT_BUDGET {" in line]
    expected = {(hidden, vector, iteration) for hidden in (False, True)
                for vector in (False, True) for iteration in range(3)}
    observed = [(m["hidden"], m["vector"], m["pass"]) for m in metrics]
    passed = (len(observed) == len(expected) and set(observed) == expected
              and all(m["paints"] == 1 and 40 <= m["elapsed_ms"] <= 75 for m in metrics))
    return {"passed": passed, "sample_count": len(metrics), "metrics": metrics,
            "metric_definition": "synchronous layout update with 40ms per actual viewport Paint event"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    started = time.perf_counter()
    outputs = []
    return_codes = []
    for suite in ("GraphicsViewTests", "WindowBehaviorTests"):
        names = [name for name in FUNCTIONAL_CASES
                 if (name == FUNCTIONAL_CASES[0]) == (suite == "GraphicsViewTests")]
        result = subprocess.run(
            [str(binary), *names], text=True, capture_output=True,
            env={**os.environ, "QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1",
                 "FOVELLE_TEST_SUITE": suite}, check=False, timeout=120)
        outputs.append(result.stdout + result.stderr)
        return_codes.append(result.returncode)
    output = "\n".join(outputs)
    motion = motion_summary(output)
    paint = paint_summary(output)
    cases = []
    for index, name in enumerate(FUNCTIONAL_CASES, start=1):
        suite = "GraphicsViewTests" if name == "testFitZoomSurvivesInverseWheelStepsAndFullscreenResize" else "WindowBehaviorTests"
        qualified_name = f"{suite}::{name}"
        cases.append(
            {
                "id": f"TC-FS-{index:02d}",
                "test": qualified_name,
                "status": "passed" if (motion["passed"] if name == "testFullScreenPresentationKeepsMoving"
                    else paint["passed"] if name == "testFullScreenLayoutPaintBudget"
                    else re.search(rf"PASS\s+: {re.escape(qualified_name)}\(\)", output)) else "failed",
            }
        )
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
        "return_code": max(return_codes),
        "suite_return_codes": return_codes,
        "elapsed_seconds": time.perf_counter() - started,
        "functional_cases": cases,
        "fullscreen_metrics": fullscreen_metrics,
        "performance": performance,
        "thresholds": THRESHOLDS,
        "performance_flags": performance_flags,
        "motion": motion,
        "paint": paint,
        "passed": all(code == 0 for code in return_codes) and all(item["status"] == "passed" for item in cases) and all(performance_flags.values()) and motion["passed"] and paint["passed"],
        "output_tail": output[-12000:],
        "limitations": [
            "The test process sends deterministic Qt key events; it does not depend on a human keyboard or Accessibility permission.",
            "The separate app-launch/resource probe records process-level timing and resource observations.",
            "A 130 ms controlled main-run-loop pause tests animation independence; it does not identify every natural stall or GPU hitch.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
