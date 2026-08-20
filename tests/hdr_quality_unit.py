#!/usr/bin/env python3
"""Run deterministic unit policy tests and the complete regression suite."""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


EXPECTED = (
    ("UT-HDR-TRANSITION", "HDRPolicyTests", "testTransitionCurveIsBoundedAndMonotonic"),
    ("UT-HDR-HEADROOM-CLAMP", "HDRPolicyTests", "testHDRHeadroomIsClampedToContentAndDisplay"),
    ("UT-HDR-SDR-FALLBACK", "HDRPolicyTests", "testSDRDisplayForcesUnitHeadroom"),
    ("UT-HDR-RENDERER-CONTRACT", "HDRPolicyTests", "testRendererUsesFloatEDRColorManagedSurface"),
    ("UT-HDR-SDR-CLASSIFICATION", "HDRPolicyTests", "testSDRImageStaysOnSDRPath"),
    ("UT-HDR-FORMAT-COVERAGE", "HDRPolicyTests", "testRequiredHDRFormatsAreAdvertised"),
    ("UT-HDR-VERSION", "FeatureTests", "testApplicationVersionIsCurrent"),
)


def run_suite(binary: Path, suite: str, environment: dict[str, str]) -> dict:
    command = [str(binary), "-o", "-,txt"]
    started = time.perf_counter()
    result = subprocess.run(
        command,
        text=True,
        capture_output=True,
        env={**os.environ, **environment, "FOVELLE_TEST_SUITE": suite},
        timeout=90,
        check=False,
    )
    output = result.stdout + result.stderr
    totals_match = re.search(
        r"Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted", output
    )
    totals = {
        "passed": int(totals_match.group(1)) if totals_match else 0,
        "failed": int(totals_match.group(2)) if totals_match else -1,
        "skipped": int(totals_match.group(3)) if totals_match else -1,
        "blacklisted": int(totals_match.group(4)) if totals_match else -1,
    }
    return {
        "suite": suite,
        "command": command,
        "return_code": result.returncode,
        "elapsed_seconds": time.perf_counter() - started,
        "totals": totals,
        "output": output,
        "passed": result.returncode == 0 and totals["failed"] == 0 and totals["skipped"] == 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    binary = args.binary.resolve()
    build_dir = args.build_dir.resolve()
    environment = {
        "QT_QPA_PLATFORM": "cocoa",
        "QT_FATAL_WARNINGS": "1",
        "QTEST_FUNCTION_TIMEOUT": "30000",
    }
    suites = {
        name: run_suite(binary, name, environment)
        for name in ("HDRPolicyTests", "FeatureTests")
    }
    cases = []
    for identifier, suite, function in EXPECTED:
        marker = f"PASS   : {suite}::{function}()"
        status = "passed" if marker in suites[suite]["output"] else "failed"
        cases.append({
            "id": identifier,
            "test_code": f"tests/tst_qviewtests.cpp::{suite}::{function}",
            "suite": suite,
            "function": function,
            "status": status,
            "evidence_marker": marker,
        })

    ctest_command = [
        "ctest", "--test-dir", str(build_dir), "--output-on-failure", "--timeout", "120"
    ]
    started = time.perf_counter()
    ctest = subprocess.run(
        ctest_command,
        text=True,
        capture_output=True,
        env={**os.environ, **environment},
        timeout=150,
        check=False,
    )
    regression = {
        "command": ctest_command,
        "return_code": ctest.returncode,
        "elapsed_seconds": time.perf_counter() - started,
        "output": ctest.stdout + ctest.stderr,
        "passed": ctest.returncode == 0 and "100% tests passed" in (ctest.stdout + ctest.stderr),
    }
    passed = (
        all(item["passed"] for item in suites.values())
        and all(item["status"] == "passed" for item in cases)
        and regression["passed"]
    )
    record = {
        "schema_version": "1.0",
        "kind": "unit-test-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "host": {"platform": platform.platform(), "python": platform.python_version()},
        "environment": environment,
        "suites": list(suites.values()),
        "regression_ctest": regression,
        "cases": cases,
        "summary": {
            "total": len(cases),
            "passed": sum(item["status"] == "passed" for item in cases),
            "failed": sum(item["status"] != "passed" for item in cases),
        },
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "summary": record["summary"], "passed": passed}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
