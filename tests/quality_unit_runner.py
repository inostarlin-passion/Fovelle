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
    )
    record = {
        "kind": "unit",
        "binary": str(args.binary.resolve()),
        "return_code": result.returncode,
        "suites": suites,
        "totals": totals,
        "total_passed": sum(item["passed"] for item in totals),
        "output": output,
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
