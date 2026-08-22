#!/usr/bin/env python3
"""Run the EPS Settings/UI integration case in a separate Cocoa process."""

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


CASES = {"IT-EPS-SETTINGS": "testSettingsFormatsIncludeEPS"}


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
        "FOVELLE_TEST_SUITE": "FeatureTests",
        "QT_QPA_PLATFORM": "cocoa",
        "QT_FATAL_WARNINGS": "1",
        "QTEST_FUNCTION_TIMEOUT": "30000",
    }
    started = time.perf_counter()
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
                command, -1, error.stdout or "", (error.stderr or "") + "\nintegration test timeout"
            )
    else:
        completed = subprocess.CompletedProcess(command, 2, "", "test binary does not exist")

    output = (completed.stdout or "") + (completed.stderr or "")
    pass_methods = re.findall(r"PASS\s+: FeatureTests::(test\w+)", output)
    fail_methods = re.findall(r"FAIL\s+: FeatureTests::(test\w+)", output)
    totals = re.search(
        r"Totals:\s+(\d+) passed,\s+(\d+) failed,\s+(\d+) skipped,\s+(\d+) blacklisted",
        output,
    )
    cases = []
    for identifier, method in CASES.items():
        observed = method in pass_methods and method not in fail_methods
        cases.append(
            {
                "id": identifier,
                "test_method": method,
                "passed": observed,
                "actual": {
                    "observed_pass_line": method in pass_methods,
                    "observed_fail_line": method in fail_methods,
                },
            }
        )
    totals_data = {
        "passed": int(totals.group(1)) if totals else None,
        "failed": int(totals.group(2)) if totals else None,
        "skipped": int(totals.group(3)) if totals else None,
        "blacklisted": int(totals.group(4)) if totals else None,
    }
    passed = (
        completed.returncode == 0
        and not timed_out
        and all(item["passed"] for item in cases)
        and totals_data["failed"] == 0
        and totals_data["skipped"] == 0
        and totals_data["blacklisted"] == 0
    )
    result = {
        "schema_version": "1.0",
        "kind": "eps-integration-test-evidence",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "test_level": "integration",
        "command": command,
        "environment": {
            key: environment[key]
            for key in ("FOVELLE_TEST_SUITE", "QT_QPA_PLATFORM", "QT_FATAL_WARNINGS", "QTEST_FUNCTION_TIMEOUT")
        },
        "binary": str(binary),
        "elapsed_seconds": time.perf_counter() - started,
        "return_code": completed.returncode,
        "timed_out": timed_out,
        "cases": cases,
        "qt_totals": totals_data,
        "raw_output": output[-24000:],
        "facts": [
            "The integration case constructs the production QVOptionsDialog and reads its formatsTable widget.",
            "The same test also checks that EPS aliases are present in QVApplication's runtime extension set.",
        ],
        "inferences": [
            "Passing this case supports the inference that Settings and folder enumeration share one EPS registry rather than independent lists.",
        ],
        "uncertainties": [
            "The UI test verifies table presence and default enabled state, not macOS Launch Services database refresh timing after installation.",
        ],
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
