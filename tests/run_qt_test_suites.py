#!/usr/bin/env python3
"""Run each Qt test suite in its own process to isolate native GUI state."""

from __future__ import annotations

import os
import subprocess
import sys


SUITES = (
    "ImageLoaderTests",
    "FeatureTests",
    "HDRPolicyTests",
    "GraphicsViewTests",
    "ScrollHelperTests",
    "ApplicationEventTests",
    "ImageCoreAndMovieTests",
    "WindowBehaviorTests",
)


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: run_qt_test_suites.py TEST_EXECUTABLE [QTTEST_ARGS...]", file=sys.stderr)
        return 2

    executable = sys.argv[1]
    arguments = sys.argv[2:]
    failures: list[str] = []

    for suite in SUITES:
        environment = os.environ.copy()
        environment["FOVELLE_TEST_SUITE"] = suite
        print(f"\n=== {suite} ===", flush=True)
        result = subprocess.run(
            [executable, *arguments],
            env=environment,
            check=False,
        )
        if result.returncode != 0:
            failures.append(f"{suite} (exit {result.returncode})")

    if failures:
        print("Failed Qt test suites: " + ", ".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
