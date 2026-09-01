#!/usr/bin/env python3
"""Static acceptance checks for scrollbar endpoints and zoom-anchor stability."""

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


def section_for(markdown: str, heading: str) -> str:
    start = markdown.find(f"## {heading}")
    if start < 0:
        return ""
    end = markdown.find("\n## ", start + 1)
    return markdown[start : end if end >= 0 else len(markdown)]


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
    specification = (repo / "reports/test_case_specification.md").read_text(encoding="utf-8")

    checks: list[dict] = []

    vertical_rule = "QScrollBar::handle:vertical { min-height: 24px; margin: 0px 1px; }"
    horizontal_rule = "QScrollBar::handle:horizontal { min-width: 24px; margin: 1px 0px; }"
    fixed_thickness_absent = (
        "QScrollBar:vertical { width:" not in view_cpp
        and "QScrollBar:horizontal { height:" not in view_cpp
        and "width: 12px" not in view_cpp
        and "height: 12px" not in view_cpp
    )
    scrollbar_contract = (
        "setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);" in view_cpp
        and "setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);" in view_cpp
        and vertical_rule in view_cpp
        and horizontal_rule in view_cpp
        and "margin: 2px 1px" not in view_cpp
        and "margin: 1px 2px" not in view_cpp
        and fixed_thickness_absent
    )
    add_check(
        checks,
        "ST-SB-01",
        scrollbar_contract,
        {
            "horizontal_policy_as_needed": "setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);" in view_cpp,
            "vertical_policy_as_needed": "setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);" in view_cpp,
            "vertical_motion_margin_zero": vertical_rule in view_cpp,
            "horizontal_motion_margin_zero": horizontal_rule in view_cpp,
            "legacy_endpoint_margins_absent": "margin: 2px 1px" not in view_cpp
            and "margin: 1px 2px" not in view_cpp,
            "fixed_thickness_absent": fixed_thickness_absent,
        },
        "AsNeeded bars use the native view geometry and the handle has no movement-direction endpoint inset",
    )

    scene_geometry_contract = (
        "const QRectF desiredSceneRect = getSceneRectForViewport();" in view_cpp
        and "QScopedValueRollback<bool> sceneRectUpdateGuard(isUpdatingSceneRect, true);" in view_cpp
        and "if (sceneRect() != desiredSceneRect)" in view_cpp
        and "setSceneRect(desiredSceneRect);" in view_cpp
        and "loadedPixmapItem->boundingRect();" in view_cpp
    )
    add_check(
        checks,
        "ST-SB-02",
        scene_geometry_contract,
        {
            "desired_rect_from_view": "const QRectF desiredSceneRect = getSceneRectForViewport();" in view_cpp,
            "scene_rect_update_guard": "QScopedValueRollback<bool> sceneRectUpdateGuard(isUpdatingSceneRect, true);" in view_cpp,
            "skip_redundant_scene_rect_write": "if (sceneRect() != desiredSceneRect)" in view_cpp,
            "active_item_geometry_source": "loadedPixmapItem->boundingRect();" in view_cpp,
        },
        "the explicit scene rectangle follows active image geometry and cannot recursively rebuild itself",
    )

    native_extent_contract = (
        "QStyle::PM_ScrollBarExtent" in tests_cpp
        and "verticalScrollBar()->sizeHint().width()" in tests_cpp
        and "horizontalScrollBar()->sizeHint().height()" in tests_cpp
        and "testScrollBarGeometryMatchesViewMetricAndDoesNotRebound" in tests_cpp
    )
    add_check(
        checks,
        "ST-SB-03",
        native_extent_contract,
        {
            "platform_extent_is_observed": "QStyle::PM_ScrollBarExtent" in tests_cpp,
            "vertical_extent_is_compared": "verticalScrollBar()->sizeHint().width()" in tests_cpp,
            "horizontal_extent_is_compared": "horizontalScrollBar()->sizeHint().height()" in tests_cpp,
            "delayed_endpoint_test_present": "testScrollBarGeometryMatchesViewMetricAndDoesNotRebound" in tests_cpp,
        },
        "the regression test compares both styled scrollbar thicknesses with the view metric and checks the delayed endpoint",
    )

    ci_contract = (
        "option(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION" in tests_cmake
        and "if(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION)" in tests_cmake
        and "add_test(NAME FovelleTests" in tests_cmake
        and "add_test(NAME FovelleShortcutSettingsTests" in tests_cmake
    )
    add_check(
        checks,
        "ST-CI-01",
        ci_contract,
        {
            "product_suites_registered": "add_test(NAME FovelleTests" in tests_cmake
            and "add_test(NAME FovelleShortcutSettingsTests" in tests_cmake,
            "native_driver_is_explicitly_opt_in": "if(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION)" in tests_cmake,
        },
        "the default GitHub Actions CTest gate contains product suites only; the permission-dependent native driver is explicitly opt-in",
    )

    anchor_contract = (
        "void restorePendingZoomAnchor();" in view_header
        and "pendingZoomAnchorScene = scenePos;" in view_cpp
        and "pendingZoomAnchorViewport = pos;" in view_cpp
        and "pendingZoomAnchorFollowsViewportCenter = targetPos == Qv::CalculateViewportCenterPos;" in view_cpp
        and "QTimer::singleShot(150, this" in view_cpp
        and "restorePendingZoomAnchor();" in view_cpp
        and "const QPoint anchorViewport = pendingZoomAnchorFollowsViewportCenter" in view_cpp
        and "getUsableViewportRect().center()" in view_cpp
        and "qRound(delta.x() * getRtlFlip())" in view_cpp
        and "qRound(delta.y())" in view_cpp
    )
    add_check(
        checks,
        "ST-ZOOM-01",
        anchor_contract,
        {
            "anchor_state_declared": "pendingZoomAnchorScene" in view_header
            and "pendingZoomAnchorViewport" in view_header,
            "anchor_captured_before_transform_commit": "pendingZoomAnchorScene = scenePos;" in view_cpp
            and "pendingZoomAnchorViewport = pos;" in view_cpp,
            "delayed_restore_window": "QTimer::singleShot(150, this" in view_cpp,
            "center_anchor_tracks_new_usable_center": "getUsableViewportRect().center()" in view_cpp,
            "both_axes_restore_with_rtl": "qRound(delta.x() * getRtlFlip())" in view_cpp
            and "qRound(delta.y())" in view_cpp,
        },
        "zoom restores the scene point at the correct viewport target across scrollbar layout and delayed geometry changes",
    )

    resize_transaction_contract = (
        "if (isUpdatingSceneRect)" in view_cpp
        and "if (pendingZoomAnchorScene.has_value()" in view_cpp
        and "restorePendingZoomAnchor();" in view_cpp
        and "logViewportState(\"resize-zoom-anchor-restored\");" in view_cpp
    )
    add_check(
        checks,
        "ST-ZOOM-02",
        resize_transaction_contract,
        {
            "nested_scene_rect_resize_guarded": "if (isUpdatingSceneRect)" in view_cpp,
            "pending_zoom_resize_path": "if (pendingZoomAnchorScene.has_value()" in view_cpp,
            "restore_before_normal_resize_compensation": view_cpp.find(
                "if (pendingZoomAnchorScene.has_value()"
            )
            < view_cpp.find("const QSize sizeDelta = event->size() - event->oldSize();"),
        },
        "automatic scrollbar viewport resize does not enter the ordinary half-delta repositioning path",
    )

    test_markers = {
        marker: marker in tests_cpp
        for marker in (
            "AC-SCROLLBAR-IMAGE-EDGES",
            "AC-SCROLLBAR-VISUAL-ENDPOINT",
            "TC-LAYOUT-ZOOM-SCROLLBAR-THRESHOLD",
            "AC-ZOOM-BOTTOM-RIGHT-STABLE",
            "AC-SCROLLBAR-NATIVE-EXTENT",
            "AC-ZOOM-ENDPOINT-NO-REBOUND",
        )
    }
    function_markers = {
        function: function in tests_cpp
        for function in (
            "testScrollBarsReachImageEdges",
            "testScrollBarGeometryMatchesViewMetricAndDoesNotRebound",
            "testScrollBarHandleTrackEndpoints",
            "testZoomAcrossScrollbarThresholdKeepsViewportCenterStable",
            "testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar",
        )
    }
    add_check(
        checks,
        "ST-TEST-01",
        all(test_markers.values()) and all(function_markers.values()),
        {"acceptance_markers": test_markers, "test_functions": function_markers},
        "each atomic acceptance criterion has an executable QtTest entry point",
    )

    required_fields = (
        "测试目的",
        "前置条件",
        "输入数据",
        "操作步骤",
        "预期结果",
        "后置条件",
    )
    case_ids = (
        "TC-SB-IMAGE-EDGES",
        "TC-SB-NATIVE-EXTENT",
        "TC-SB-VISUAL-ENDPOINT",
        "TC-ZOOM-CENTER-THRESHOLD",
        "TC-ZOOM-BOTTOM-RIGHT",
        "TC-ZOOM-ENDPOINT",
        "TC-STATIC-CONTRACT",
    )
    fields_by_case: dict[str, dict[str, bool]] = {}
    for case_id in case_ids:
        section = section_for(specification, case_id)
        fields_by_case[case_id] = {field: field in section for field in required_fields}
    specification_contract = all(
        section_for(specification, case_id) and all(fields.values())
        for case_id, fields in fields_by_case.items()
    )
    add_check(
        checks,
        "ST-TEST-02",
        specification_contract,
        {"fields_by_case": fields_by_case},
        "the Markdown specification records all six required fields for every acceptance test case",
    )

    result = {
        "kind": "scrollbar-zoom-acceptance-static",
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
