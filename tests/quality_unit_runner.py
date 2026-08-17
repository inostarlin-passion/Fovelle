#!/usr/bin/env python3
"""Run the Qt unit binary and record its complete suite output."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    result = subprocess.run(
        [str(args.binary.resolve())],
        text=True,
        capture_output=True,
        env={**os.environ, "QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1"},
        check=False,
    )
    output = result.stdout + result.stderr
    totals = [
        {"passed": int(passed), "failed": int(failed), "skipped": int(skipped)}
        for passed, failed, skipped in re.findall(
            r"Totals: (\d+) passed, (\d+) failed, (\d+) skipped", output
        )
    ]
    suites = [
        suite
        for suite in (
            "ImageLoaderTests",
            "ActionManagerTests",
            "ApplicationEventTests",
            "ImageCoreAndMovieTests",
        )
        if f"Start testing of {suite}" in output
    ]
    functional_cases = [
        "testReturnKeyEntersFullscreen",
        "testKeypadEnterEntersFullscreen",
        "testEnterDoesNotExitFullscreen",
        "testEscapeRestoresLoadedImageWithoutGeometryJump",
    ]
    functional_case_results = [
        {
            "id": f"TC-FS-{index:02d}",
            "test": f"ActionManagerTests::{test_name}",
            "status": "passed" if f"PASS   : ActionManagerTests::{test_name}()" in output else "failed",
        }
        for index, test_name in enumerate(functional_cases, start=1)
    ]
    fullscreen_metrics = [
        {"phase": phase, "milliseconds": float(milliseconds)}
        for phase, milliseconds in re.findall(r"FS_METRIC\s+(enter|exit)_ms=([0-9]+(?:\.[0-9]+)?)", output)
    ]
    passed = (
        result.returncode == 0
        and suites == [
            "ImageLoaderTests",
            "ActionManagerTests",
            "ApplicationEventTests",
            "ImageCoreAndMovieTests",
        ]
        and len(totals) == 4
        and all(item["failed"] == 0 for item in totals)
        and all(item["status"] == "passed" for item in functional_case_results)
        and all(item["skipped"] == 0 for item in totals)
        and len(fullscreen_metrics) >= 7
        and all(item["milliseconds"] <= 2000 for item in fullscreen_metrics)
    )
    record = {
        "kind": "unit",
        "binary": str(args.binary.resolve()),
        "return_code": result.returncode,
        "suites": suites,
        "totals": totals,
        "total_passed": sum(item["passed"] for item in totals),
        "total_failed": sum(item["failed"] for item in totals),
        "total_skipped": sum(item["skipped"] for item in totals),
        "functional_cases": functional_case_results,
        "fullscreen_metrics": fullscreen_metrics,
        "output": output,
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
