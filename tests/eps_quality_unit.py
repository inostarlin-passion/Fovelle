#!/usr/bin/env python3
"""Run the deterministic EPS native/loader Qt tests and record evidence."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


CASES = {
    "UT-EPS-FORMAT": "testEPSFormatIsAdvertised",
    "UT-EPS-DECODE": "testEPSPreviewDecode",
    "UT-EPS-LOADER": "testImageLoaderLoadsEPS",
    "UT-EPS-MALFORMED": "testMalformedEPSFailsSafely",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    binary = args.binary.resolve()
    command = [str(binary), "-o", "-,txt"]
    environment = {
        **os.environ,
        "FOVELLE_TEST_SUITE": "ImageLoaderTests",
        "QT_QPA_PLATFORM": "cocoa",
        "QT_FATAL_WARNINGS": "1",
        "QTEST_FUNCTION_TIMEOUT": "30000",
    }
    started = time.perf_counter()
    completed = None
    timed_out = False
    if binary.is_file():
        try:
            completed = subprocess.run(
                command,
                text=True,
                capture_output=True,
                env=environment,
                cwd=binary.parents[2],
                timeout=args.timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            timed_out = True
            completed = subprocess.CompletedProcess(
                command,
                -1,
                error.stdout or "",
                (error.stderr or "") + "\nunit test timeout",
            )
    else:
        completed = subprocess.CompletedProcess(command, 2, "", "test binary does not exist")

    output = (completed.stdout or "") + (completed.stderr or "")
    pass_lines = re.findall(r"PASS\s+: ImageLoaderTests::(test\w+)", output)
    fail_lines = re.findall(r"FAIL\s+: ImageLoaderTests::(test\w+)", output)
    totals = re.search(
        r"Totals:\s+(\d+) passed,\s+(\d+) failed,\s+(\d+) skipped,\s+(\d+) blacklisted",
        output,
    )
    required_case_results = {
        identifier: {
            "test_method": method,
            "passed": method in pass_lines and method not in fail_lines,
            "observed_pass_line": method in pass_lines,
            "observed_fail_line": method in fail_lines,
        }
        for identifier, method in CASES.items()
    }
    totals_data = {
        "passed": int(totals.group(1)) if totals else None,
        "failed": int(totals.group(2)) if totals else None,
        "skipped": int(totals.group(3)) if totals else None,
        "blacklisted": int(totals.group(4)) if totals else None,
    }
    passed = (
        completed.returncode == 0
        and not timed_out
        and all(item["passed"] for item in required_case_results.values())
        and totals_data["failed"] == 0
        and totals_data["skipped"] == 0
        and totals_data["blacklisted"] == 0
    )
    result = {
        "schema_version": "1.0",
        "kind": "eps-unit-test-evidence",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "test_level": "unit",
        "command": command,
        "environment": {
            key: environment[key]
            for key in ("FOVELLE_TEST_SUITE", "QT_QPA_PLATFORM", "QT_FATAL_WARNINGS", "QTEST_FUNCTION_TIMEOUT")
        },
        "binary": str(binary),
        "elapsed_seconds": time.perf_counter() - started,
        "return_code": completed.returncode,
        "timed_out": timed_out,
        "cases": [
            {
                "id": identifier,
                "test_method": item["test_method"],
                "passed": item["passed"],
                "actual": item,
            }
            for identifier, item in required_case_results.items()
        ],
        "qt_totals": totals_data,
        "all_observed_pass_methods": pass_lines,
        "failed_methods": fail_lines,
        "raw_output": output[-24000:],
        "facts": [
            "Each deterministic EPS unit case is executed by the production fovelle_tests binary.",
            "The EPS decode case uses the supplied sample when present and falls back to a deterministic EPSI fixture otherwise.",
            "The asynchronous loader case exercises QVImageLoader rather than only the native bridge.",
        ],
        "inferences": [
            "A zero-failure Qt run supports the inference that native EPS preview results preserve the loader's existing Result contract.",
        ],
        "uncertainties": [
            "The unit fallback fixture does not represent the full PostScript language; pure EPS without an embedded preview remains outside this decoder's contract.",
        ],
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
