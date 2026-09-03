#!/usr/bin/env python3
"""Static acceptance checks for scrollbar endpoints and zoom-anchor stability."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


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
    match = re.search(rf"^#{{1,6}}\s+.*\b{re.escape(heading)}\b.*$", markdown, re.MULTILINE)
    if not match:
        return ""
    remainder = markdown[match.end() :]
    end = re.search(r"^#{1,6}\s+", remainder, re.MULTILINE)
    return markdown[match.start() : match.end() + (end.start() if end else len(remainder))]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    view_cpp = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    view_header = (repo / "src/qvgraphicsview.h").read_text(encoding="utf-8")
    action_cpp = (repo / "src/actionmanager.cpp").read_text(encoding="utf-8")
    mainwindow_cpp = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")
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

    manual_pan_anchor_contract = (
        "void cancelPendingZoomAnchor(bool preserveSceneMargins = false);" in view_header
        and "const bool isExternalViewportChange" in view_cpp
        and "cancelPendingZoomAnchor();" in view_cpp
        and "&QScrollBar::sliderPressed" in view_cpp
        and "&QScrollBar::sliderMoved" in view_cpp
        and "&QScrollBar::actionTriggered" in view_cpp
        and "pendingZoomAnchorGeneration;" in view_cpp
        and "testManualScrollCancelsPendingZoomAnchor" in tests_cpp
    )
    add_check(
        checks,
        "ST-ZOOM-03",
        manual_pan_anchor_contract,
        {
            "cancellation_api_declared": "void cancelPendingZoomAnchor(bool preserveSceneMargins = false);" in view_header,
            "viewport_change_guard_is_present": "const bool isExternalViewportChange" in view_cpp,
            "user_scroll_signals_cancel_anchor": "&QScrollBar::sliderPressed" in view_cpp
            and "&QScrollBar::sliderMoved" in view_cpp
            and "&QScrollBar::actionTriggered" in view_cpp,
            "pan_paths_cancel_anchor": "cancelPendingZoomAnchor(true);" in view_cpp,
            "generation_invalidates_delayed_callback": "pendingZoomAnchorGeneration;" in view_cpp,
            "manual_pan_regression_present": "testManualScrollCancelsPendingZoomAnchor" in tests_cpp,
        },
        "an explicit scrollbar pan cancels the preceding zoom anchor before its delayed callback can recenter the view",
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
        and "pendingZoomAnchorFollowsViewportCenter = followsViewportCenter;" in view_cpp
        and "QTimer *zoomAnchorSettleTimer;" in view_header
        and "zoomAnchorSettleTimer->setInterval(" in view_cpp
        and "zoomAnchorSettleTimer->start();" in view_cpp
        and "zoomAnchorSettleGeneration = anchorGeneration;" in view_cpp
        and "void QVGraphicsView::settlePendingZoomAnchor()" in view_cpp
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
            "member_settle_timer": "zoomAnchorSettleTimer->setInterval(" in view_cpp
            and "zoomAnchorSettleTimer->start();" in view_cpp,
            "settle_generation_guard": "zoomAnchorSettleGeneration = anchorGeneration;" in view_cpp
            and "void QVGraphicsView::settlePendingZoomAnchor()" in view_cpp,
            "center_anchor_tracks_new_usable_center": "getUsableViewportRect().center()" in view_cpp,
            "both_axes_restore_with_rtl": "qRound(delta.x() * getRtlFlip())" in view_cpp
            and "qRound(delta.y())" in view_cpp,
        },
        "zoom restores the scene point at the correct viewport target across scrollbar layout and delayed geometry changes",
    )

    transition_contract = (
        "QPropertyAnimation" in view_cpp
        and "Q_PROPERTY(qreal animatedZoomLevel" in view_header
        and "ZoomTransitionDurationMs = 200" in view_header
        and "zoomAnimation->setDuration(ZoomTransitionDurationMs);" in view_cpp
        and "zoomAnimation->setEasingCurve(QEasingCurve::OutCubic);" in view_cpp
        and "zoomAnimation->start();" in view_cpp
        and "void QVGraphicsView::finishZoomTransition()" in view_cpp
    )
    add_check(
        checks,
        "ST-ZOOM-TRANSITION-01",
        transition_contract,
        {
            "property_animation_declared": "Q_PROPERTY(qreal animatedZoomLevel" in view_header,
            "duration_is_200ms": "ZoomTransitionDurationMs = 200" in view_header
            and "zoomAnimation->setDuration(ZoomTransitionDurationMs);" in view_cpp,
            "easing_is_configured": "zoomAnimation->setEasingCurve(QEasingCurve::OutCubic);" in view_cpp,
            "transition_is_started": "zoomAnimation->start();" in view_cpp,
            "terminal_frame_is_normalized": "void QVGraphicsView::finishZoomTransition()" in view_cpp,
        },
        "all zoom entry points share one 200 ms property animation and an exact terminal frame",
    )

    geometry_timer_contract = (
        "QTimer *verticalScrollBarGeometryTimer;" in view_header
        and "verticalScrollBarGeometryTimer->setObjectName(" in view_cpp
        and 'QStringLiteral("verticalScrollBarGeometryTimer")' in view_cpp
        and "verticalScrollBarGeometryTimer->setSingleShot(true);" in view_cpp
        and "verticalScrollBarGeometryTimer->setInterval(0);" in view_cpp
        and "verticalScrollBarGeometryTimer->start();" in view_cpp
    )
    add_check(
        checks,
        "ST-ZOOM-GEOMETRY-TIMER-01",
        geometry_timer_contract,
        {
            "member_timer_declared": "QTimer *verticalScrollBarGeometryTimer;" in view_header,
            "member_timer_named": 'QStringLiteral("verticalScrollBarGeometryTimer")' in view_cpp,
            "zero_delay_single_shot": "verticalScrollBarGeometryTimer->setSingleShot(true);" in view_cpp
            and "verticalScrollBarGeometryTimer->setInterval(0);" in view_cpp,
            "schedule_uses_member_timer": "verticalScrollBarGeometryTimer->start();" in view_cpp,
        },
        "the post-layout scrollbar geometry writer is a coalesced, observable zero-delay member timer",
    )

    expensive_anchor_rebase_contract = (
        "const QRectF oldImageRect = scene()->itemsBoundingRect();" in view_cpp
        and "std::optional<QPointF> pendingAnchorUV;" in view_cpp
        and "const QRectF newImageRect = scene()->itemsBoundingRect();" in view_cpp
        and "pendingZoomAnchorScene = QPointF(" in view_cpp
        and "same normalized image point" in view_cpp
    )
    trajectory_contract = (
        "// AC-ZOOM-VBAR-TRANSIENT" in tests_cpp
        and "void testZoomKeepsVerticalScrollbarTrajectoryStable_data()" in tests_cpp
        and "void testZoomKeepsVerticalScrollbarTrajectoryStable()" in tests_cpp
        and "QTest::addColumn<int>(\"anchorPolicy\")" in tests_cpp
        and "enum class ZoomAnchorPolicy" in tests_cpp
        and "ZoomAnchorPolicy::FixedImagePoint" in tests_cpp
        and "ZoomTraceProbe" in tests_cpp
        and "QTest::keySequence" in tests_cpp
        and "QWheelEvent" in tests_cpp
        and "QNativeGestureEvent" in tests_cpp
        and "sendNativeGesture(view, Qt::ZoomNativeGesture" in tests_cpp
        and "inputSource == QStringLiteral(\"pinch\")" in tests_cpp
        and "imageScene.width() * 0.40" in tests_cpp
        and "imageScene.height() * 0.35" in tests_cpp
        and "animation->setCurrentTime(animationTime)" in tests_cpp
        and "manual-time-" in tests_cpp
        and "zoomAnchorSettleTimer-timeout" in tests_cpp
        and "constrainBoundsTimer-timeout" in tests_cpp
        and "expensiveScaleTimer-timeout" in tests_cpp
        and "verticalScrollBarGeometryTimer-timeout" in tests_cpp
        and "verticalExpected" in tests_cpp
        and "verticalBarContainerGlobalRect" in tests_cpp
        and "firstCheckableBarGeometry" in tests_cpp
        and "vertical scrollbar/container geometry moved" in tests_cpp
        and "SC_ScrollBarSlider" in tests_cpp
        and "writeZoomTraceFailure" in tests_cpp
        and "firstImmediateErrorMessage" in tests_cpp
        and "saveFailureFrames" in tests_cpp
        and "FovelleZoomScrollbarTrajectory" in tests_cmake
        and "FovelleZoomScrollbarTrajectoryHiDpi" in tests_cmake
        and "QT_SCALE_FACTOR=2" in tests_cmake
        and expensive_anchor_rebase_contract
    )
    add_check(
        checks,
        "ST-ZOOM-VBAR-TRAJECTORY-01",
        trajectory_contract,
        {
            "atomic_marker_and_data_rows": "// AC-ZOOM-VBAR-TRANSIENT" in tests_cpp
            and "testZoomKeepsVerticalScrollbarTrajectoryStable_data" in tests_cpp
            and "QTest::addColumn<int>(\"anchorPolicy\")" in tests_cpp,
            "real_keyboard_input": "QTest::keySequence" in tests_cpp,
            "real_wheel_input": "QWheelEvent" in tests_cpp,
            "real_native_pinch_input": "QNativeGestureEvent" in tests_cpp
            and "sendNativeGesture(view, Qt::ZoomNativeGesture" in tests_cpp,
            "non_center_anchor_policy": "ZoomAnchorPolicy::FixedImagePoint" in tests_cpp
            and "imageScene.width() * 0.40" in tests_cpp
            and "imageScene.height() * 0.35" in tests_cpp,
            "deterministic_integer_time_scan": "animation->setCurrentTime(animationTime)" in tests_cpp
            and "manual-time-" in tests_cpp,
            "delayed_callbacks_observed": "zoomAnchorSettleTimer-timeout" in tests_cpp
            and "constrainBoundsTimer-timeout" in tests_cpp
            and "expensiveScaleTimer-timeout" in tests_cpp
            and "verticalScrollBarGeometryTimer-timeout" in tests_cpp,
            "independent_vertical_oracle": "verticalExpected" in tests_cpp,
            "physical_geometry_oracle": "verticalBarContainerGlobalRect" in tests_cpp
            and "firstCheckableBarGeometry" in tests_cpp
            and "vertical scrollbar/container geometry moved" in tests_cpp,
            "style_thumb_oracle": "SC_ScrollBarSlider" in tests_cpp,
            "immediate_sample_decision": "firstImmediateErrorMessage" in tests_cpp,
            "distinct_failure_artifacts": "saveFailureFrames" in tests_cpp
            and "first-bad-frame.png" in tests_cpp
            and "worst-frame.png" in tests_cpp
            and "terminal-frame.png" in tests_cpp,
            "normal_and_hidpi_ctest": "FovelleZoomScrollbarTrajectory" in tests_cmake
            and "FovelleZoomScrollbarTrajectoryHiDpi" in tests_cmake
            and "QT_SCALE_FACTOR=2" in tests_cmake,
            "expensive_pixmap_anchor_rebased": expensive_anchor_rebase_contract,
        },
        "the gray-box trajectory test covers real input, every animation millisecond, delayed callbacks, style geometry, failure artifacts, and the expensive-backing-pixmap coordinate rebase",
    )

    menu_contract = (
        'addCloneOfAction(helpMenu, "projecthomepage");' in action_cpp
        and 'addCloneOfAction(helpMenu, "website");' in action_cpp
        and 'addCloneOfAction(helpMenu, "checkupdates");' in action_cpp
        and 'QDesktopServices::openUrl(QUrl(QStringLiteral("https://fovelle-viewer.onrender.com/")))' in action_cpp
        and 'addCloneOfAction(contextMenu, "sortmenu")' not in mainwindow_cpp
        and "buildSortMenu" not in action_cpp
        and "buildSortMenu" not in mainwindow_cpp
    )
    add_check(
        checks,
        "ST-MENU-CONTRACT-01",
        menu_contract,
        {
            "website_is_between_help_actions": 'addCloneOfAction(helpMenu, "projecthomepage");' in action_cpp
            and 'addCloneOfAction(helpMenu, "website");' in action_cpp
            and 'addCloneOfAction(helpMenu, "checkupdates");' in action_cpp,
            "website_url_is_exact": 'https://fovelle-viewer.onrender.com/' in action_cpp,
            "sort_menu_builder_removed": "buildSortMenu" not in action_cpp
            and "buildSortMenu" not in mainwindow_cpp,
        },
        "the titlebar and context Help menus share Website while the context Sort Files By branch is absent",
    )

    file_enumerator_cpp = (repo / "src/qvfileenumerator.cpp").read_text(encoding="utf-8")
    settings_cpp = (repo / "src/settingsmanager.cpp").read_text(encoding="utf-8")
    sort_contract = (
        'sortMode = Qv::SortMode::Name;' in file_enumerator_cpp
        and 'sortDescending = false;' in file_enumerator_cpp
        and 'settingsManager.getEnum<Qv::SortMode' not in file_enumerator_cpp
        and 'settingsManager.getBoolean("sortdescending")' not in file_enumerator_cpp
        and 'settingsLibrary.insert("sortmode"' not in settings_cpp
        and 'settingsLibrary.insert("sortdescending"' not in settings_cpp
    )
    add_check(
        checks,
        "ST-SORT-CONTRACT-01",
        sort_contract,
        {
            "fixed_name_order": 'sortMode = Qv::SortMode::Name;' in file_enumerator_cpp,
            "fixed_ascending_order": 'sortDescending = false;' in file_enumerator_cpp,
            "enumerator_does_not_read_sort_mode": 'settingsManager.getEnum<Qv::SortMode' not in file_enumerator_cpp,
            "settings_library_has_no_legacy_keys": 'settingsLibrary.insert("sortmode"' not in settings_cpp
            and 'settingsLibrary.insert("sortdescending"' not in settings_cpp,
        },
        "legacy sort preferences cannot override the fixed Name/Ascending policy",
    )

    anchor_projection_contract = (
        "QPointF QVGraphicsView::projectZoomAnchor" in view_cpp
        and "qBound(imageViewportRect.left()" in view_cpp
        and "qBound(imageViewportRect.top()" in view_cpp
        and "testZoomAnchorProjectsInsideAndOutsideImage" in tests_cpp
    )
    add_check(
        checks,
        "ST-ZOOM-ANCHOR-PROJECTION-01",
        anchor_projection_contract,
        {
            "pure_projection_helper": "QPointF QVGraphicsView::projectZoomAnchor" in view_cpp,
            "horizontal_clamp": "qBound(imageViewportRect.left()" in view_cpp,
            "vertical_clamp": "qBound(imageViewportRect.top()" in view_cpp,
            "dynamic_regression_present": "testZoomAnchorProjectsInsideAndOutsideImage" in tests_cpp,
        },
        "inside anchors remain unchanged and outside anchors clamp independently to image edges",
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
            "AC-ZOOM-MANUAL-PAN-OVERRIDES-ANCHOR",
            "AC-ZOOM-TRANSITION-200MS",
            "AC-ZOOM-ANCHOR-PROJECTION",
            "AC-ZOOM-VBAR-TRANSIENT",
            "AC-ZOOM-VBAR-VALUE",
            "AC-ZOOM-VBAR-GEOMETRY",
            "AC-ZOOM-VBAR-ANCHOR",
            "AC-ZOOM-VBAR-THUMB",
            "AC-ZOOM-VBAR-ASYNC",
            "AC-ZOOM-VBAR-MATRIX",
            "AC-SORT-FIXED-DEFAULT",
            "AC-HELP-WEBSITE",
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
            "testManualScrollCancelsPendingZoomAnchor",
            "testZoomTransitionCoversWheelKeyboardAndMenus",
            "testZoomAnchorProjectsInsideAndOutsideImage",
            "testZoomKeepsVerticalScrollbarTrajectoryStable_data",
            "testZoomKeepsVerticalScrollbarTrajectoryStable",
            "testSortConfigurationIsIgnoredAndContextMenuHasNoSortMenu",
            "testWebsiteHelpActionAndContextMenuContract",
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
