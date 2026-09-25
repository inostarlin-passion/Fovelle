#!/usr/bin/env python3
"""Static source and executable-test checks for the four zoom regressions."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


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
    tests_cpp = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    tests_cmake = (repo / "tests/CMakeLists.txt").read_text(encoding="utf-8")

    checks: list[dict] = []

    atomic_to_test = {
        "AC-P1-ANCHOR-CONTINUITY": "testWheelZoomHasNoPositionJumpTrajectory",
        "AC-P1-NO-LATE-JUMP": "testWheelZoomHasNoPositionJumpTrajectory",
        "AC-P2-NO-TRANSIENT-VBAR": "testZoomOutHasNoTransientVerticalScrollBar",
        "AC-SB-NO-STALE-RANGE": "testZoomOutHasNoTransientVerticalScrollBar",
        "AC-P3-NO-TRANSIENT-HBAR": "testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar",
        "AC-P3-CROSS-AXIS-STABILITY": "testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar",
        "AC-P4-NO-AVOIDABLE-BLANK": "testToggleFitTo100HasNoAvoidableBlankSpace",
        "AC-P4-OPTIMAL-CLAMP": "testToggleFitTo100HasNoAvoidableBlankSpace",
        "AC-P4-TOGGLE-DIRECTIONAL-ANCHOR": "testToggleFitTo100HasNoAvoidableBlankSpace",
    }
    atomic_inventory = {
        criterion: {
            "test_function": function_name in tests_cpp,
            "test_marker": f"// {criterion}" in tests_cpp,
        }
        for criterion, function_name in atomic_to_test.items()
    }
    add_check(
        checks,
        "ST-4Q-ATOMIC-01",
        all(all(values.values()) for values in atomic_inventory.values()),
        atomic_inventory,
        "every atomic criterion is marked by an executable test function",
    )

    production_contracts = {
        "real_scene_rect_is_authoritative": contains_all(
            view_cpp,
            (
                "QRect QVGraphicsView::getScrollContentRect() const",
                "return getDisplayedContentRect();",
                "loadedPixmapItem->boundingRect()",
                "scene()->itemsBoundingRect()",
                "restorePendingZoomAnchor();",
            ),
        ),
        "anchor_projection_uses_one_coordinate_conversion": contains_all(
            view_cpp,
            (
                "QPoint QVGraphicsView::zoomAnchorViewportPoint",
                "projectZoomAnchor",
                "mapToScene(pos.value())",
                "mapFromScene(",
                "scene()->itemsBoundingRect()).boundingRect()",
            ),
        ),
        "post_layout_anchor_reconciliation": contains_all(
            view_header + view_cpp,
            (
                "zoomAnchorPostLayoutTimer",
                "zoomAnchorPostLayoutTimer->start(0)",
                "pendingZoomAnchorScene.has_value()",
                "restoreSettledZoomAnchor",
            ),
        ),
        "titlebar_padding_cannot_fabricate_fit_range": contains_all(
            view_cpp,
            (
                "usableAxisSize",
                "paddingPixels",
                "displayedAxisSize > usableAxisSize",
            ),
        ),
        "keyboard_and_toggle_anchor_contract": contains_all(
            view_cpp,
            (
                "getCursorViewportPosition()",
                "lastMouseViewportPosition",
                "void QVGraphicsView::zoomIn()",
                "void QVGraphicsView::zoomOut()",
                "void QVGraphicsView::toggleFitAnd100()",
                "displayedZoomLevel",
                "calculateZoomLevelForMode",
            ),
        ),
        "fit_state_is_displayed_state": contains_all(
            view_cpp,
            (
                "bool QVGraphicsView::isImageAtFit() const",
                "horizontalScrollBar()->maximum()",
                "verticalScrollBar()->maximum()",
            ),
        ),
    }
    add_check(
        checks,
        "ST-4Q-PRODUCTION-01",
        all(production_contracts.values()),
        production_contracts,
        "production code prevents synthetic ranges, restores anchors after layout, caps titlebar padding, and distinguishes displayed fit state",
    )

    input_contract = {
        "real_wheel_event": contains_all(
            tests_cpp, ("QWheelEvent", "sendDiscreteZoomWheel", "sendEvent")
        ),
        "real_toggle_shortcut": contains_all(
            tests_cpp, ("QTest::keySequence", "togglefitand100")
        ),
        "initial_transient_terminal_observation": contains_all(
            tests_cpp,
            (
                'QStringLiteral("initial-fit")',
                'QStringLiteral("terminal")',
                "QEvent::Paint",
                "QEvent::Resize",
            ),
        ),
        "independent_geometry_oracle": contains_all(
            tests_cpp,
            (
                "itemsBoundingRect()",
                "mapToScene",
                "mapFromScene",
                "horizontalScrollBar()",
                "verticalScrollBar()",
            ),
        ),
        "state_based_terminal_wait": contains_all(
            tests_cpp,
            (
                "waitForZoomTerminal",
                "isZoomTransitionRunning",
                "zoomAnchorSettleTimer",
                "verticalScrollBarGeometryTimer",
            ),
        ),
    }
    add_check(
        checks,
        "ST-4Q-INPUT-01",
        all(input_contract.values()),
        input_contract,
        "dynamic cases use real input entry points and independently observe initial, transient, and terminal states",
    )

    ctest_contract = {
        "four_issue_test_registered": "FovelleFourIssueZoomAcceptance" in tests_cmake,
        "cocoa_qpa": "QT_QPA_PLATFORM=cocoa" in tests_cmake,
        "all_four_functions_registered": all(
            function_name in tests_cmake
            for function_name in set(atomic_to_test.values())
        ),
    }
    add_check(
        checks,
        "ST-4Q-CTEST-01",
        all(ctest_contract.values()),
        ctest_contract,
        "the static gate and all four dynamic acceptance functions are registered in reproducible CTest entries",
    )

    result = {
        "kind": "four-issue-zoom-acceptance-static",
        "passed": all(check["pass"] for check in checks),
        "checks": checks,
    }
    output = args.output if args.output.is_absolute() else repo / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
