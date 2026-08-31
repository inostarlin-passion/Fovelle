#!/usr/bin/env python3
"""Static acceptance checks for the fixed adjacent preload policy.

The runtime tests exercise the policy with legacy values 0 and 2.  This
script complements them by checking that the old mode enum and runtime setting
read cannot return through a later refactor, and that the CI geometry
regression remains covered by an integer-pixel-safe contract.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def check(checks: list[tuple[str, bool, str]], identifier: str, passed: bool, detail: str) -> None:
    checks.append((identifier, passed, detail))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (defaults to the parent of tests/)",
    )
    args = parser.parse_args()
    repo = args.repo.resolve()

    relative_paths = (
        "src/qvnamespace.h",
        "src/qvimagecore.h",
        "src/qvimagecore.cpp",
        "src/settingsmanager.cpp",
        "src/qvoptionsdialog.cpp",
        "tests/tst_qviewtests.cpp",
        ".github/workflows/test.yml",
        ".github/workflows/build.yml",
    )
    try:
        source = {
            relative: (repo / relative).read_text(encoding="utf-8")
            for relative in relative_paths
        }
    except OSError as error:
        print(f"ERROR: unable to read policy inputs: {error}", file=sys.stderr)
        return 2

    checks: list[tuple[str, bool, str]] = []
    namespace = source["src/qvnamespace.h"]
    image_core = source["src/qvimagecore.cpp"] + source["src/qvimagecore.h"]
    settings = source["src/settingsmanager.cpp"]
    options_dialog = source["src/qvoptionsdialog.cpp"]
    tests = source["tests/tst_qviewtests.cpp"]

    check(
        checks,
        "PRELOAD-STATIC-001",
        "enum class PreloadMode" not in namespace
        and "PreloadMode::" not in image_core
        and "preloadingMode" not in image_core,
        "the preload mode enum, enum members, and mutable runtime mode are absent",
    )
    check(
        checks,
        "PRELOAD-STATIC-002",
        bool(re.search(r"AdjacentPreloadDistance\s*=\s*1\s*;", namespace)),
        "AdjacentPreloadDistance is a single scalar constant with value 1",
    )
    check(
        checks,
        "PRELOAD-STATIC-003",
        "const int preloadDistance = Qv::AdjacentPreloadDistance;" in source["src/qvimagecore.cpp"]
        and "settingsManager.getEnum<Qv::Preload" not in source["src/qvimagecore.cpp"],
        "QVImageCore always derives its preload radius from the fixed adjacent constant",
    )
    check(
        checks,
        "PRELOAD-STATIC-004",
        '"preloadingmode", Qv::AdjacentPreloadDistance' in settings
        and '"preloadingmode", static_cast<int>(Qv::PreloadMode' not in settings,
        "legacy persisted data is normalized and its compatibility default is Adjacent",
    )
    check(
        checks,
        "PRELOAD-STATIC-005",
        "testPreloadingIgnoresDisabledUserSetting" in tests
        and "testPreloadingIgnoresExtendedUserSetting" in tests
        and '{"preloadingmode", 0}' in tests
        and '{"preloadingmode", 2}' in tests,
        "runtime tests cover both legacy values that must be overridden",
    )
    check(
        checks,
        "PRELOAD-STATIC-006",
        "measuredShortcutsWidth % 2" in options_dialog
        and "const int shortcutsNaturalWidth" in options_dialog,
        "the shortcut page normalizes its natural width to avoid odd-pixel Stretch splits",
    )
    check(
        checks,
        "PRELOAD-STATIC-007",
        not re.search(
            r"QCOMPARE\s*\(\s*(?:table->horizontalHeader\(\)|header)"
            r"->sectionSize\(0\)\s*,\s*(?:table->horizontalHeader\(\)|header)"
            r"->sectionSize\(1\)\s*\)",
            tests,
            flags=re.DOTALL,
        )
        and "qAbs(header->sectionSize(0) - header->sectionSize(1)) <= 1" in tests,
        "all Stretch-column equality contracts allow integer rounding while preserving total width",
    )

    for workflow_name in (".github/workflows/test.yml", ".github/workflows/build.yml"):
        workflow = source[workflow_name]
        check(
            checks,
            f"CI-{Path(workflow_name).stem.upper()}-001",
            "version: '6.11.2'" in workflow
            and "ctest --test-dir build --output-on-failure --timeout 90" in workflow,
            f"{workflow_name} pins Qt 6.11.2 and retains the bounded CTest timeout",
        )
        check(
            checks,
            f"CI-{Path(workflow_name).stem.upper()}-002",
            "python3 tests/preload_policy_quality.py" in workflow,
            f"{workflow_name} runs the fixed-policy static gate",
        )

    failed = [identifier for identifier, passed, _ in checks if not passed]
    for identifier, passed, detail in checks:
        print(f"{'PASS' if passed else 'FAIL'} {identifier}: {detail}")
    print(f"Summary: {len(checks) - len(failed)}/{len(checks)} static checks passed")
    if failed:
        print("Failed checks: " + ", ".join(failed), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
