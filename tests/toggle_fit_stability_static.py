#!/usr/bin/env python3
"""Static acceptance gate for the synchronous Toggle/fit zoom contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


ATOMIC_CRITERIA = (
    "AC-ZOOM-NO-ANIMATION-STATIC",
    "AC-ZOOM-NO-ANIMATION-SHORTCUT",
    "AC-ZOOM-NO-ANIMATION-MENU",
    "AC-ANCHOR-MOUSE-PREFERRED",
    "AC-ANCHOR-PROJECT-FEASIBLE",
    "AC-ANCHOR-NO-POST-CORRECTION",
)

CASE_IDS = (
    "TC-ZOOM-SYNC-ALL-ENTRY-POINTS",
    "TC-ANCHOR-FEASIBLE-PROJECTION",
)

REQUIRED_CASE_FIELDS = (
    "测试目的",
    "前置条件",
    "输入数据",
    "操作步骤",
    "预期结果",
    "后置条件",
)


def markdown_section(markdown: str, case_id: str) -> str:
    match = re.search(
        rf"^###\s+{re.escape(case_id)}\s*$", markdown, re.MULTILINE
    )
    if not match:
        return ""
    remainder = markdown[match.end() :]
    next_heading = re.search(r"^###\s+", remainder, re.MULTILINE)
    end = match.end() + (next_heading.start() if next_heading else len(remainder))
    return markdown[match.start() : end]


def add(checks: list[dict[str, object]], identifier: str, passed: bool,
        actual: object, expected: str) -> None:
    checks.append({
        "id": identifier,
        "pass": bool(passed),
        "actual": actual,
        "expected": expected,
    })


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path,
                        default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()

    view_h = (repo / "src/qvgraphicsview.h").read_text(encoding="utf-8")
    view_cpp = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    cmake = (repo / "tests/CMakeLists.txt").read_text(encoding="utf-8")
    technical = (repo / "reports/technical_design_document.md").read_text(
        encoding="utf-8"
    )
    specification = (repo / "reports/test_case_specification.md").read_text(
        encoding="utf-8"
    )
    completion = (repo / "reports/test_completion_report.md").read_text(
        encoding="utf-8"
    )

    checks: list[dict[str, object]] = []

    forbidden = (
        "QPropertyAnimation",
        "Q_PROPERTY(qreal animatedZoomLevel",
        "zoomTransitionAnimation",
        "zoomAnchorSettleTimer",
        "zoomAnchorPostLayoutTimer",
        "pendingZoomAnchorScene",
        "displayedZoomLevel",
        "ZoomTransitionDurationMs",
    )
    actual_forbidden = {
        marker: marker in view_h or marker in view_cpp for marker in forbidden
    }
    add(
        checks,
        "ST-TOGGLE-NO-ANIMATION",
        not any(actual_forbidden.values()),
        actual_forbidden,
        "Toggle and fit use the same immediate zoom commit and do not retain a geometric animation path",
    )

    implementation = {
        "immediate_commit": "commitZoomImmediately(makeZoomPlan" in view_cpp,
        "target_recalculation": "projectZoomAnchorForTarget" in view_cpp,
        "center_sentinel_resolved": "requestedViewportCenter" in view_cpp,
        "no_transition_state": "isZoomTransitionRunning() const { return false; }" in view_h,
        "no_general_event_drain": "QCoreApplication::processEvents" not in view_cpp,
    }
    add(
        checks,
        "ST-TOGGLE-IMPLEMENTATION",
        all(implementation.values()),
        implementation,
        "Toggle resolves its anchor once and commits target zoom, layout, and scrollbars in one synchronous path",
    )

    executable = {
        "shortcut_test": "testToggleFitAnd100CenterTrajectoryIsLinear" in tests_cpp,
        "shortcut_input": "QTest::keySequence" in tests_cpp,
        "no_animation_assertion": "findChild<QObject *>" in tests_cpp
        and "zoomTransitionAnimation" in tests_cpp,
        "immediate_state_assertion": "!view->isZoomTransitionRunning()" in tests_cpp,
        "quiet_window_assertion": "QTest::qWait(250)" in tests_cpp,
        "directional_anchor_test": "testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor" in tests_cpp,
        "center_anchor_test": "testToggleFitAnd100FreezesViewportCenterDuringScrollbarTransition" in tests_cpp,
    }
    add(
        checks,
        "ST-TOGGLE-TEST-CODE",
        all(executable.values()),
        executable,
        "the executable suite covers shortcut entry, immediate state, directional anchor, center anchor, and quiet postcondition",
    )

    case_fields = {
        case_id: {
            field: field in markdown_section(specification, case_id)
            for field in REQUIRED_CASE_FIELDS
        }
        for case_id in CASE_IDS
    }
    traceability = {
        criterion: {
            "technical_design": criterion in technical,
            "test_specification": criterion in specification,
            "completion_report": criterion in completion,
            "test_code": criterion in tests_cpp,
        }
        for criterion in ATOMIC_CRITERIA
    }
    add(
        checks,
        "ST-TOGGLE-TRACEABILITY",
        all(all(fields.values()) for fields in case_fields.values())
        and all(all(locations.values()) for locations in traceability.values()),
        {"case_fields": case_fields, "criteria": traceability},
        "Toggle atomic criteria have six-field cases and end-to-end traceability",
    )

    registration = {
        "static": "FovelleToggleFitStabilityStatic" in cmake,
        "dynamic": "FovelleToggleFitStabilityAcceptance" in cmake,
        "anchor": "FovelleToggleFitAnchorAcceptance" in cmake,
        "trajectory": "FovelleToggleFitTrajectoryAcceptance" in cmake,
    }
    add(
        checks,
        "ST-TOGGLE-CTEST",
        all(registration.values()),
        registration,
        "static, anchor, and trajectory acceptance gates are registered in CTest",
    )

    result = {
        "kind": "toggle-fit-synchronous-static-check",
        "repo": str(repo),
        "atomic_criteria": list(ATOMIC_CRITERIA),
        "checks": checks,
        "passed": all(check["pass"] for check in checks),
    }
    output = args.output if args.output.is_absolute() else repo / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
