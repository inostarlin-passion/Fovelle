#!/usr/bin/env python3
"""Run the new image-policy and Issue #864 cases in a separate Cocoa process."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path


CASES = (
    ("TC-IMG-SMALL-SETTING", "FeatureTests", "testSmallImageOneToOneSettingIsExposedInImageOptions"),
    ("TC-ISSUE-864-OPENWITH-TEARDOWN", "FeatureTests", "testOpenWithWorkerTeardownContract"),
    ("TC-IMG-SMALL-POLICY", "GraphicsViewTests", "testSmallImageOneToOnePolicyUsesViewportAndWindowMode"),
    ("TC-IMG-SMALL-OPEN-BROWSE", "GraphicsViewTests", "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages"),
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    binary = args.binary.resolve()
    command = [str(binary), "-o", "-,txt"]
    started = time.perf_counter()
    try:
        result = subprocess.run(
            command,
            text=True,
            capture_output=True,
            env={**os.environ, "QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1"},
            timeout=45,
            check=False,
        )
        output = result.stdout + result.stderr
        timed_out = False
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        output = stdout + stderr
        result = None
        timed_out = True

    cases = [
        {
            "id": identifier,
            "test": f"{suite}::{test_name}",
            "status": "passed"
            if re.search(rf"PASS\s+: {re.escape(suite)}::{re.escape(test_name)}\(\)", output)
            else "failed",
        }
        for identifier, suite, test_name in CASES
    ]
    issue_864_safety_observations = {
        "no_sigabrt": "SIGABRT" not in output,
        "no_qpixmap_after_teardown_error": "QPixmap: Must construct a QGuiApplication" not in output,
        "teardown_case_completed": "testOpenWithWorkerTeardownContract" in output,
    }
    record = {
        "kind": "system-feature",
        "command": command,
        "binary": str(binary),
        "platform": "macOS Cocoa",
        "elapsed_seconds": time.perf_counter() - started,
        "return_code": result.returncode if result else None,
        "timed_out": timed_out,
        "cases": cases,
        "observations": {
            "qt_platform": "cocoa",
            "issue_864_safety": issue_864_safety_observations,
            "opening_and_browsing_case_present": "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages" in output,
        },
        "passed": bool(
            result
            and result.returncode == 0
            and not timed_out
            and all(case["status"] == "passed" for case in cases)
            and all(issue_864_safety_observations.values())
        ),
        "output_tail": output[-16000:],
        "limitations": [
            "The test runs the real compiled application test binary under Cocoa, but the deterministic image fixtures are created by the test process.",
            "A separate repeated-launch probe supplies P99 and resource measurements; this record is the functional system gate for the new cases.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
