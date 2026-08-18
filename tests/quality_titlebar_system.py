#!/usr/bin/env python3
"""Run the visible Cocoa titlebar cases in a separate test process."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


CASES = (
    ("TC-TITLEBAR-APP-ICON", "testWindowIconIsCleared"),
    ("TC-TITLEBAR-DOCUMENT-ICON", "testTitlebarDocumentProxyIsClearedForLoadedFile"),
    ("TC-TITLEBAR-IDEMPOTENCE", "testTitlebarIconClearingIsIdempotent"),
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    binary = args.binary.resolve()
    command = [str(binary), "-o", "-,txt"]
    try:
        result = subprocess.run(
            command,
            text=True,
            capture_output=True,
            env={**os.environ, "QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1"},
            timeout=30,
            check=False,
        )
        output = result.stdout + result.stderr
        timed_out = False
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or "") + (error.stderr or "")
        result = None
        timed_out = True

    cases = [
        {
            "id": identifier,
            "test": f"FeatureTests::{test_name}",
            "status": "passed"
            if re.search(rf"PASS\s+: FeatureTests::{re.escape(test_name)}\(\)", output)
            else "failed",
        }
        for identifier, test_name in CASES
    ]
    record = {
        "kind": "system-titlebar",
        "command": command,
        "binary": str(binary),
        "platform": "cocoa" if "macos" in output.lower() else None,
        "return_code": result.returncode if result else None,
        "timed_out": timed_out,
        "cases": cases,
        "observations": {
            "native_cocoa_test_process": "QT_QPA_PLATFORM=cocoa" in str(command) or "macos" in output.lower(),
            "native_window_file_path_asserted": "testTitlebarDocumentProxyIsClearedForLoadedFile" in output,
            "repeated_state_asserted": "testTitlebarIconClearingIsIdempotent" in output,
        },
        "passed": bool(
            result
            and result.returncode == 0
            and not timed_out
            and all(case["status"] == "passed" for case in cases)
        ),
        "output_tail": output[-12000:],
        "limitations": [
            "The functional assertion observes Qt's native QWindow filePath under the Cocoa platform.",
            "It does not use Accessibility screenshot/OCR; the visual conclusion is based on Qt and Apple documented proxy-icon mapping.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
