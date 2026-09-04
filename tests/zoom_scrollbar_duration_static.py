#!/usr/bin/env python3
"""Static acceptance gate for synchronous zoom and feasible-anchor commits."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


ATOMIC_CRITERIA = (
    "AC-ZOOM-NO-ANIMATION-STATIC",
    "AC-ZOOM-NO-ANIMATION-INPUT",
    "AC-ZOOM-NO-ANIMATION-SHORTCUT",
    "AC-ZOOM-NO-ANIMATION-MENU",
    "AC-ANCHOR-MOUSE-PREFERRED",
    "AC-ANCHOR-PROJECT-FEASIBLE",
    "AC-ANCHOR-NO-POST-CORRECTION",
    "AC-ANCHOR-HBAR-TOPOLOGY",
    "AC-VBAR-TOPOLOGY-ANCHOR",
)

CASE_IDS = (
    "TC-ZOOM-SYNC-ALL-ENTRY-POINTS",
    "TC-ANCHOR-FEASIBLE-PROJECTION",
    "TC-HBAR-FOUR-IN-ONE-OUT",
    "TC-VBAR-TOPOLOGY-ANCHOR",
)

REQUIRED_CASE_FIELDS = (
    "测试目的",
    "前置条件",
    "输入数据",
    "操作步骤",
    "预期结果",
    "后置条件",
)


def section(markdown: str, case_id: str) -> str:
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
    mainwindow_cpp = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    cmake = (repo / "tests/CMakeLists.txt").read_text(encoding="utf-8")
    reports = {
        name: (repo / "reports" / name).read_text(encoding="utf-8")
        for name in (
            "technical_design_document.md",
            "test_case_specification.md",
            "test_completion_report.md",
        )
    }

    checks: list[dict[str, object]] = []

    forbidden_zoom_animation = (
        "QPropertyAnimation",
        "Q_PROPERTY(qreal animatedZoomLevel",
        "zoomTransitionAnimation",
        "zoomAnchorSettleTimer",
        "zoomAnchorPostLayoutTimer",
        "pendingZoomAnchorScene",
        "displayedZoomLevel",
        "ZoomTransitionDurationMs",
        "finishZoomTransition",
    )
    forbidden_actual = {
        marker: marker in view_h or marker in view_cpp
        for marker in forbidden_zoom_animation
    }
    add(
        checks,
        "ST-ZOOM-NO-ANIMATION",
        not any(forbidden_actual.values()),
        forbidden_actual,
        "QVGraphicsView zoom has no geometric animation object, displayed-frame state, or delayed anchor writer",
    )

    synchronous_markers = {
        "plan_type": "struct ZoomPlan" in view_h,
        "single_commit": "commitZoomImmediately(makeZoomPlan" in view_cpp,
        "updates_suppressed": "setUpdatesEnabled(false)" in view_cpp,
        "target_transform": "setTransformScale(zoomLevel * appliedDpiAdjustment)" in view_cpp,
        "target_layout_fixed_point": "settleTargetScrollAreaLayout" in view_cpp,
        "one_zoom_signal": "emit zoomLevelChanged();" in view_cpp,
        "no_event_drain": "QCoreApplication::processEvents" not in view_cpp,
        "post_layout_anchor_reconciliation":
            "postLayoutZoomAnchorScene" in view_h
            and "restorePostLayoutZoomAnchor" in view_cpp,
    }
    add(
        checks,
        "ST-ZOOM-SYNCHRONOUS-COMMIT",
        all(synchronous_markers.values()),
        synchronous_markers,
        "one GUI-thread transaction computes, lays out, anchors, and publishes the target without a general event-loop drain",
    )

    projection_markers = {
        "public_pure_helper": "projectZoomAnchorForTarget" in view_h,
        "target_feasible_interval": "minimumOrigin = viewportOrigin + viewportSize - targetSize" in view_cpp,
        "affine_inverse": "const qreal slope = 1.0 - ratio" in view_cpp,
        "box_projection": "qBound(qMin(first, second), requestedAnchor" in view_cpp,
        "actual_target_replan": "const QRectF targetImage = imageViewportRect();" in view_cpp,
        "normalized_backing_anchor": "oldImageSceneRect" in view_h and "targetSceneRect.left() + uv.x()" in view_cpp,
    }
    add(
        checks,
        "ST-ANCHOR-PROJECTION",
        all(projection_markers.values()),
        projection_markers,
        "the requested mouse anchor is projected to the nearest target-feasible image origin and replanned after scrollbar topology settles",
    )

    dynamic_markers = {
        "all_entry_points": "testZoomTransitionCoversWheelKeyboardAndMenus" in tests_cpp,
        "wheel_round_trip": "testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump" in tests_cpp,
        "mouse_anchor": "testZoomAnchorProjectsInsideAndOutsideImage" in tests_cpp,
        "keyboard_anchor": "testKeyboardZoomUsesCursorAnchor" in tests_cpp,
        "no_animation_assertion": "findChild<QObject *>" in tests_cpp
        and "zoomTransitionAnimation" in tests_cpp,
        "no_post_correction_assertion": "QTest::qWait(250)" in tests_cpp,
        "provided_fixture": "FOVELLE_SCROLLBAR_ZOOM_SAMPLE" in tests_cpp,
        "four_forward_one_reverse": "four-forward-terminal" in tests_cpp and "one-reverse-terminal" in tests_cpp,
        "vertical_topology": "testZoomKeepsVerticalScrollbarPositionWhenVerticalRangeAppears" in tests_cpp,
    }
    add(
        checks,
        "ST-DYNAMIC-TEST-CODE",
        all(dynamic_markers.values()),
        dynamic_markers,
        "QtTest exercises wheel, keyboard, title-bar menu, context menu, outside-image projection, and the reported four-in/one-out fixture",
    )

    required_fields = {
        case_id: {field: field in section(reports["test_case_specification.md"], case_id)
                  for field in REQUIRED_CASE_FIELDS}
        for case_id in CASE_IDS
    }
    traceability = {
        criterion: {
            "technical_design": criterion in reports["technical_design_document.md"],
            "test_specification": criterion in reports["test_case_specification.md"],
            "completion_report": criterion in reports["test_completion_report.md"],
            "test_code": criterion in tests_cpp,
        }
        for criterion in ATOMIC_CRITERIA
    }
    add(
        checks,
        "ST-REPORT-TRACEABILITY",
        all(all(fields.values()) for fields in required_fields.values())
        and all(all(locations.values()) for locations in traceability.values()),
        {"case_fields": required_fields, "criteria": traceability},
        "each atomic criterion has a six-field case and is traceable through design, executable test code, and completion report",
    )

    registration = {
        "static_gate": "FovelleZoomScrollbarDurationStatic" in cmake,
        "sync_entry_points": "testZoomTransitionCoversWheelKeyboardAndMenus" in cmake,
        "anchor_round_trip": "testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump" in cmake,
        "anchor_projection": "testZoomAnchorProjectsInsideAndOutsideImage" in cmake,
        "keyboard_anchor": "testKeyboardZoomUsesCursorAnchor" in cmake,
        "vertical_topology": "FovelleZoomScrollbarVerticalTopology" in cmake,
    }
    add(
        checks,
        "ST-CTEST-REGISTRATION",
        all(registration.values()),
        registration,
        "static and dynamic gates are registered for every zoom entry family and anchor regression",
    )

    # QAction routes menu and shortcut activation to MainWindow.  This source
    # check records that those routes still converge on the view API.
    action_routing = {
        "zoom_absolute_route": "graphicsView->zoomAbsolute" in mainwindow_cpp,
        "zoom_in_route": "graphicsView->zoomIn" in mainwindow_cpp,
        "zoom_out_route": "graphicsView->zoomOut" in mainwindow_cpp,
    }
    add(
        checks,
        "ST-ACTION-ROUTING",
        all(action_routing.values()),
        action_routing,
        "title-bar and context-menu actions reach the same synchronous view API as keyboard zoom actions",
    )

    result = {
        "kind": "zoom-synchronous-anchor-static-check",
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
