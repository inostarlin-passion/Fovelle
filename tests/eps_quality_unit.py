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
    "UT-EPS-RENDER": "testEPSPostScriptRender",
    "UT-EPS-LOADER": "testImageLoaderLoadsEPS",
    "UT-EPS-CONSISTENCY": "testEPSInitialFrameMatchesStableRender",
    "UT-EPS-CACHE": "testEPSRenderCacheCutsConversionLatency",
    "UT-EPS-STATIC": "testEPSRenderSurvivesStaticMovieProbe",
    "UT-EPS-MALFORMED": "testMalformedEPSFailsSafely",
    "UT-EPS-DEPENDENCY": "testEPSMissingRendererFailsActionably",
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
            "The EPS render case uses the supplied sample when present and falls back to a deterministic vector EPS otherwise.",
            "The renderer case verifies retained PDF bytes, a persistent Core Graphics document, a bounded PDF-derived fallback, and an independently requested 2048-pixel final-density render.",
            "A partial upper-page tile is compared with the matching region of a full-page render to verify top-left scene to bottom-left PDF coordinate conversion.",
            "The asynchronous loader and delayed movie-probe cases exercise QVImageLoader and QVImageCore, not only the native bridge.",
            "The first/stable consistency case proves that the first EPS result already contains the authoritative PDF and that its fallback pixels come from that same PDF.",
            "The conversion-cache case measures a same-identity miss/hit pair and verifies that the cached result retains identical authoritative PDF bytes.",
            "The dependency case forces an invalid Ghostscript path and verifies that the UI cannot expose an unrelated embedded preview.",
        ],
        "inferences": [
            "A zero-failure Qt run supports the inference that the authoritative EPS document preserves the loader Result contract and remains stable after delayed static-document probing.",
        ],
        "uncertainties": [
            "The deterministic fixture does not represent every PostScript dialect, external font dependency, or Ghostscript version.",
        ],
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
