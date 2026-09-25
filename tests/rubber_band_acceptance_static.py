#!/usr/bin/env python3
"""Static acceptance checks for the image-pan rubber-band removal."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def add_check(checks: list[dict], identifier: str, passed: bool, actual: object, expected: str) -> None:
    checks.append(
        {
            "id": identifier,
            "pass": bool(passed),
            "actual": actual,
            "expected": expected,
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    scroll_cpp = (repo / "src/scrollhelper.cpp").read_text(encoding="utf-8")
    scroll_header = (repo / "src/scrollhelper.h").read_text(encoding="utf-8")
    view_cpp = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")

    checks: list[dict] = []
    clamp_contract = (
        "return qBound(static_cast<qreal>(minValue) - currentValue,"
        in scroll_cpp
        and "static_cast<qreal>(maxValue) - currentValue);" in scroll_cpp
    )
    add_check(
        checks,
        "ST-RB-01",
        clamp_contract,
        {
            "uses_qbound": "return qBound(" in scroll_cpp,
            "lower_delta": "static_cast<qreal>(minValue) - currentValue" in scroll_cpp,
            "upper_delta": "static_cast<qreal>(maxValue) - currentValue" in scroll_cpp,
        },
        "constrained movement is synchronously bounded to the calculated range",
    )

    removed_symbols = (
        "overflowScaleFactor",
        "overscrollDistance",
        "animatedScrollTimer",
        "animatedScrollTotalDelta",
        "animatedScrollAppliedDelta",
        "beginAnimatedScroll",
        "handleAnimatedScroll",
        "smoothAnimation",
        "cancelAnimation",
    )
    stale_symbols = [symbol for symbol in removed_symbols if symbol in scroll_cpp or symbol in scroll_header]
    add_check(
        checks,
        "ST-RB-02",
        not stale_symbols,
        {"stale_symbols": stale_symbols},
        "the helper has no resistance, overscroll state, or rebound timer",
    )

    call_site_contract = (
        "scrollHelper->constrain();" in view_cpp
        and "scrollHelper->constrain(disableDelayedConstraint);" not in view_cpp
        and "scrollHelper->constrain(true);" not in view_cpp
        and "scrollHelper->cancelAnimation();" not in view_cpp
        and "void constrain();" in scroll_header
    )
    add_check(
        checks,
        "ST-RB-03",
        call_site_contract,
        {
            "parameterless_constraint": "void constrain();" in scroll_header,
            "updated_call_sites": "scrollHelper->constrain();" in view_cpp,
            "old_constraint_call_absent": "scrollHelper->constrain(true);" not in view_cpp
            and "scrollHelper->constrain(disableDelayedConstraint);" not in view_cpp,
            "old_cancel_call_absent": "scrollHelper->cancelAnimation();" not in view_cpp,
        },
        "all production call sites use the synchronous constraint contract",
    )

    test_ids = (
        "AC-RB-MIN-EDGE",
        "AC-RB-MAX-EDGE",
        "AC-RB-NO-RETURN-ANIMATION",
        "AC-RB-INTERIOR-MOTION",
        "AC-RB-CONSTRAINT-OPT-OUT",
    )
    test_markers = {test_id: test_id in tests_cpp for test_id in test_ids}
    add_check(
        checks,
        "ST-RB-04",
        all(test_markers.values())
        and "ScrollHelperTests" in tests_cpp
        and "runSuite(\"ScrollHelperTests\"" in tests_cpp,
        {"markers": test_markers, "suite_registered": "runSuite(\"ScrollHelperTests\"" in tests_cpp},
        "every atomic acceptance criterion has executable QtTest coverage",
    )

    result = {
        "kind": "rubber-band-removal-static",
        "passed": all(check["pass"] for check in checks),
        "checks": checks,
    }
    output = args.output if args.output.is_absolute() else repo / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
