#!/usr/bin/env python3
"""Static traceability checks for the five zoom/pan defects.

This is deliberately a source-level gate, not a replacement for the Cocoa
QtTest run.  It verifies that every atomic acceptance criterion has a
production implementation marker, an executable test function, a CTest
entry, and a six-field Markdown case.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


REQUIRED_FIELDS = (
    "测试目的",
    "前置条件",
    "输入数据",
    "操作步骤",
    "预期结果",
    "后置条件",
)


def add_check(
    checks: list[dict],
    identifier: str,
    passed: bool,
    actual: object,
    expected: str,
) -> None:
    checks.append(
        {
            "id": identifier,
            "pass": bool(passed),
            "actual": actual,
            "expected": expected,
        }
    )


def markdown_section(markdown: str, case_id: str) -> str:
    """Return the complete ### case section, including its field headings."""

    match = re.search(rf"^###\s+{re.escape(case_id)}\b.*$", markdown, re.MULTILINE)
    if not match:
        return ""
    remainder = markdown[match.end() :]
    end = re.search(r"^#{1,3}\s+", remainder, re.MULTILINE)
    return markdown[match.start() : match.end() + (end.start() if end else len(remainder))]


def contains_all(source: str, needles: tuple[str, ...]) -> bool:
    return all(needle in source for needle in needles)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    view_cpp = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    view_header = (repo / "src/qvgraphicsview.h").read_text(encoding="utf-8")
    mainwindow_cpp = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    tests_cmake = (repo / "tests/CMakeLists.txt").read_text(encoding="utf-8")
    specification = (repo / "reports/test_case_specification.md").read_text(encoding="utf-8")
    design = (repo / "reports/technical_design_document.md").read_text(encoding="utf-8")
    completion = (repo / "reports/test_completion_report.md").read_text(encoding="utf-8")

    checks: list[dict] = []

    atomic_to_test = {
        "AC-SB-NO-STALE-RANGE": "testZoomOutClearsStaleVerticalScrollRange",
        "AC-DRAG-CONTINUOUS": "testMousePanKeepsOverflowRangeAndContinuity",
        "AC-DRAG-PRESERVES-OVERFLOW-BARS": "testMousePanKeepsOverflowRangeAndContinuity",
        "AC-KBD-ZOOM-CURSOR-ANCHOR": "testKeyboardZoomUsesCursorAnchor",
        "AC-TOGGLE-DIRECTIONAL-ANCHOR": "testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor",
        "AC-TOGGLE-VISUAL-STATE": "testToggleFitAnd100UsesDisplayedState",
        "AC-WHEEL-CONTENT-ANCHOR": "testMouseWheelKeepsBottomRightAnchor",
        "AC-NO-LATE-REWRITE": "testZoomTerminalStateDoesNotRewriteViewport",
    }
    atomic_inventory = {
        criterion: {
            "in_spec": criterion in specification,
            "in_design": criterion in design,
            "test_function": function_name in tests_cpp,
            "test_marker": f"// {criterion}" in tests_cpp,
        }
        for criterion, function_name in atomic_to_test.items()
    }
    add_check(
        checks,
        "ST-ZOOM-ATOMIC-01",
        all(all(values.values()) for values in atomic_inventory.values()),
        atomic_inventory,
        "every atomic acceptance criterion is traceable through the design, specification, marker, and executable test",
    )

    production_contracts = {
        "stale_range_is_pruned_per_axis": contains_all(
            view_cpp,
            (
                "QMarginsF newRetainedMargins",
                "newRetainedMargins.setLeft(0.0)",
                "newRetainedMargins.setRight(0.0)",
                "newRetainedMargins.setTop(0.0)",
                "newRetainedMargins.setBottom(0.0)",
                "retainedMarginsChanged",
                "updateSceneRect()",
            ),
        ),
        "manual_pan_preserves_real_overflow": contains_all(
            view_cpp,
            (
                "cancelPendingZoomAnchor(const bool preserveSceneMargins)",
                "cancelPendingZoomAnchor(true)",
                "preserveSceneMargins",
                "executeDragAction",
                "scrollHelper->move",
            ),
        ),
        "keyboard_reads_cursor_anchor": contains_all(
            view_cpp,
            (
                "getCursorViewportPosition()",
                "QCursor::pos()",
                "lastMouseViewportPosition",
                "void QVGraphicsView::zoomIn()",
                "void QVGraphicsView::zoomOut()",
                "zoomRelative",
            ),
        ),
        "toggle_uses_displayed_fit_and_direction": contains_all(
            view_cpp,
            (
                "bool QVGraphicsView::isImageAtFit() const",
                "calculateZoomLevelForMode",
                "displayedZoomLevel",
                "calculatedZoomMode",
                "bool zoomingIn",
                "void QVGraphicsView::toggleFitAnd100()",
                "finishZoomTransition",
            ),
        ),
        "anchor_uses_unscaled_scene_geometry": (
            view_cpp.count("scene()->itemsBoundingRect()") >= 3
            and "zoomAnchorViewportPoint" in view_cpp
            and "mapFromScene(QRectF(getDisplayedContentRect()))" not in view_cpp
        ),
        "delayed_anchor_is_quietly_reconciled": contains_all(
            view_header + view_cpp,
            (
                "settledZoomAnchorScene",
                "settledZoomAnchorViewport",
                "zoomAnchorPostLayoutTimer",
                "restoreSettledZoomAnchor",
            ),
        ),
    }
    add_check(
        checks,
        "ST-ZOOM-PRODUCTION-01",
        all(production_contracts.values()),
        production_contracts,
        "the five fixes have explicit production paths for range cleanup, drag authority, cursor anchoring, displayed-state toggle, scene-space anchoring, and delayed reconciliation",
    )

    input_contract = {
        "real_wheel_event": "QWheelEvent" in tests_cpp and "sendDiscreteZoomWheel" in tests_cpp,
        "real_keyboard_shortcut": "QTest::keySequence" in tests_cpp,
        "real_mouse_drag": all(
            token in tests_cpp
            for token in ("QTest::mousePress", "QTest::mouseMove", "QTest::mouseRelease")
        ),
        "terminal_wait_is_state_based": "waitForZoomTerminal" in tests_cpp,
        "content_anchor_oracle": "QLineF" in tests_cpp and "mapToScene" in tests_cpp,
        "range_and_geometry_oracle": all(
            token in tests_cpp
            for token in ("horizontalScrollBar()", "verticalScrollBar()", "itemsBoundingRect()")
        ),
        "quiet_event_loop_oracle": "QTest::qWait(650)" in tests_cpp
        and "testZoomTerminalStateDoesNotRewriteViewport" in tests_cpp,
    }
    add_check(
        checks,
        "ST-ZOOM-INPUT-01",
        all(input_contract.values()),
        input_contract,
        "dynamic tests exercise real event entry points and independently observe anchor, range, geometry, and terminal quietness",
    )

    case_ids = (
        "TC-SB-ZOOMOUT-ATOMIC",
        "TC-DRAG-CONTINUITY-ATOMIC",
        "TC-DRAG-OVERFLOW-ATOMIC",
        "TC-KBD-ZOOM-ATOMIC",
        "TC-TOGGLE-DIRECTIONAL-ATOMIC",
        "TC-TOGGLE-VISUAL-ATOMIC",
        "TC-WHEEL-REAL-ATOMIC",
        "TC-ASYNC-QUIET-ATOMIC",
        "TC-STATIC-TRACEABILITY",
    )
    fields_by_case = {
        case_id: {
            field: field in markdown_section(specification, case_id)
            for field in REQUIRED_FIELDS
        }
        for case_id in case_ids
    }
    specification_contract = (
        all(section for section in (markdown_section(specification, case_id) for case_id in case_ids))
        and all(all(fields.values()) for fields in fields_by_case.values())
        and "静态" in specification
        and "动态" in specification
        and "瞬态" in specification
        and "稳态" in specification
    )
    add_check(
        checks,
        "ST-ZOOM-SPEC-01",
        specification_contract,
        {
            "fields_by_case": fields_by_case,
            "has_static_dynamic": "静态" in specification and "动态" in specification,
            "has_transient_steady": "瞬态" in specification and "稳态" in specification,
        },
        "each structured case documents all six required fields and the specification distinguishes static/dynamic and transient/steady coverage",
    )

    ctest_contract = {
        "static_test_registered": "FovelleZoomIssueStatic" in tests_cmake,
        "dynamic_test_registered": "FovelleFiveIssueZoomAcceptance" in tests_cmake,
        "cocoa_qpa": "QT_QPA_PLATFORM=cocoa" in tests_cmake,
        "static_script_is_called": "zoom_issue_acceptance_static.py" in tests_cmake,
        "all_dynamic_functions_registered": all(
            function_name in tests_cmake
            for function_name in atomic_to_test.values()
        ),
    }
    add_check(
        checks,
        "ST-ZOOM-CTEST-01",
        all(ctest_contract.values()),
        ctest_contract,
        "the static and dynamic acceptance gates are reproducibly registered in CTest",
    )

    report_contract = {
        "design_has_evidence_chain": "联网多跳检索" in design and "交叉验证" in design,
        "spec_has_atomic_decomposition": "AC-ALL" in specification and "原子验收" in specification,
        "completion_has_traceability": "追溯" in completion and "PASS" in completion,
        "official_qt_sources_are_linked": all(
            url in design + specification + completion
            for url in (
                "https://doc.qt.io/qt-6/qabstractscrollarea.html",
                "https://doc.qt.io/qt-6/qgraphicsview.html",
                "https://doc.qt.io/qt-6/qaction.html",
                "https://doc.qt.io/QT-6/qvariantanimation.html",
            )
        ),
    }
    add_check(
        checks,
        "ST-ZOOM-DOC-01",
        all(report_contract.values()),
        report_contract,
        "the three requested Markdown artifacts expose the evidence chain, explicit premises, atomic traceability, and verified sources",
    )

    result = {
        "kind": "five-issue-zoom-acceptance-static",
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
