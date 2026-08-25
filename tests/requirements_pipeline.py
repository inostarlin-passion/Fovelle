#!/usr/bin/env python3
"""Run the Preferences/navigation acceptance matrix and write audit JSON."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


EXECUTION_ORDER = ("static", "unit", "integration", "system")
REPORT_NAMES = (
    "test_evidence.json",
    "test_case_specification.json",
    "test_completion_report.json",
    "code_quality_assessment_report.json",
)

RESEARCH_TRACE = [
    {
        "hop": 1,
        "dimension": "macOS file association",
        "source": "https://developer.apple.com/documentation/coreservices/1444955-lssetdefaultrolehandlerforconten",
        "finding": "Launch Services provides the default handler operation for a content type; the implementation uses the viewer role.",
        "assumption": "Association means the default application for opening/viewing the format.",
    },
    {
        "hop": 2,
        "dimension": "extension to UTI",
        "source": "https://developer.apple.com/documentation/uniformtypeidentifiers/uttagclass/filenameextension",
        "finding": "Uniform Type Identifiers resolve filename extensions through the filename-extension tag class.",
        "assumption": "The operating system owns the authoritative extension-to-UTI mapping.",
    },
    {
        "hop": 3,
        "dimension": "native dialogs",
        "source": "https://doc.qt.io/qt-6/qmessagebox.html",
        "finding": "QMessageBox follows the platform-native dialog path unless explicitly disabled.",
        "assumption": "The existing NativeDialogs adapter satisfies the Light/Dark native prompt requirement.",
    },
    {
        "hop": 4,
        "dimension": "settings migration",
        "source": "https://doc.qt.io/qt-6/qsettings.html",
        "finding": "QSettings supports grouped values, contains(), key enumeration, and remove().",
        "assumption": "Removed controls can keep compatible runtime keys while no longer exposing them in Preferences.",
    },
]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def git_head(repo: Path) -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, text=True, capture_output=True, check=False
    )
    return result.stdout.strip() or None


def artifact(repo: Path, path: Path) -> dict[str, Any]:
    resolved = path.resolve()
    try:
        relative = str(resolved.relative_to(repo))
    except ValueError:
        relative = str(resolved)
    result: dict[str, Any] = {
        "path": relative,
        "absolute_path": str(resolved),
        "exists": resolved.is_file(),
    }
    if resolved.is_file():
        result["bytes"] = resolved.stat().st_size
        result["sha256"] = sha256(resolved)
    else:
        result["bytes"] = 0
        result["sha256"] = None
    return result


def run_command(
    command: list[str],
    cwd: Path,
    environment: dict[str, str] | None = None,
    timeout: float = 120.0,
) -> dict[str, Any]:
    started = time.monotonic()
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
        )
        return {
            "command": [str(item) for item in command],
            "return_code": completed.returncode,
            "passed": completed.returncode == 0,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
        }
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        return {
            "command": [str(item) for item in command],
            "return_code": None,
            "passed": False,
            "stdout": stdout,
            "stderr": stderr,
            "timeout_seconds": timeout,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
        }


def command_record(result: dict[str, Any], output_limit: int = 8000) -> dict[str, Any]:
    return {
        "command": result.get("command"),
        "return_code": result.get("return_code"),
        "passed": result.get("passed", False),
        "duration_ms": result.get("duration_ms"),
        "output_tail": (result.get("stdout", "") + result.get("stderr", ""))[-output_limit:],
    }


def make_case(
    identifier: str,
    criterion: str,
    layer: str,
    test_code: str,
    *,
    purpose: str | None = None,
    preconditions: str | None = None,
    input_data: str | None = None,
    steps: str | None = None,
    expected: str | None = None,
    postconditions: str | None = None,
) -> dict[str, Any]:
    return {
        "id": identifier,
        "atomic_criterion": criterion,
        "atomic_acceptance_criterion": criterion,
        "test_purpose": purpose or f"验证原子要求：{criterion}",
        "preconditions": preconditions or f"{layer} 阶段的测试入口 {test_code} 可执行。",
        "input_data": input_data or criterion,
        "operation_steps": steps or f"执行 {test_code} 并记录可观测输出。",
        "expected_result": expected or f"{criterion}成立。",
        "postconditions": postconditions or "测试不留下未声明的外部副作用。",
        "test_layer": layer,
        "test_code": test_code,
        "evidence_file": "reports/test_evidence.json",
    }


def c(identifier: str, criterion: str, layer: str, test_code: str, **kwargs: str) -> dict[str, Any]:
    return make_case(identifier, criterion, layer, test_code, **kwargs)


CASES = [
    c("ST-NAV-SINGLE-OPACITY", "原生 HDR 悬浮按钮由单一父图层统一控制透明度。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-NAV-CHILD-OPAQUE", "原生按钮矩形与中心符号不会各自独立淡出。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-NAV-PAINT-FADE", "非 HDR 路径只淡出按钮绘制像素，不产生矩形 opacity effect。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationButtonUsesTransparentPaintOnlyFade"),
    c("UT-NAV-BOTH-SIDES", "左右按钮使用同一动画时长和同一显隐状态机。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationButtonsFadeTransition"),
    c("ST-WINDOW-CONTROLS-REMOVED", "Window 页移除匹配图像大小、匹配后行为、最小尺寸和最大尺寸。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-WINDOW-MAXIMIZE-PATH", "应用创建窗口时调用 showMaximized。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-WINDOW-MAXIMIZED", "每次打开的新窗口实际处于最大化状态。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNewWindowStartsMaximized"),
    c("ST-FULLSCREEN-TITLEBAR-REMOVED", "Show titlebar text in full screen 控件被移除且默认值为不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-MAIN-MENU-ICONS-REMOVED", "Show icons in main menus 控件被移除且默认值为不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-CONTEXT-MENU-ICONS-REMOVED", "Show icons in context menus 控件被移除且默认值为勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-SUBMENU-ICONS-REMOVED", "Show icons in submenus 控件被移除且默认值为勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-SESSION-REMOVED", "Persist session across app restarts 控件被移除且默认值为不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-PREFERENCES-REMOVED", "已移除的 Window/Image/Misc 控件不出现在实际对象树。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testPreferencesDefaultsAndRemovedControls"),
    c("UT-THEME-LABELS", "Theme 项目命名为 Light、Dark，且顺序保持稳定。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testThemeSettingsReplaceRemovedColorControls"),
    c("ST-THEME-LABELS", "Theme 映射不再生成 Light Theme/Dark Theme 文案。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-SCALINGTWO", "Expensive scaling above window size 控件移除，默认勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-SMOOTH-LIMIT", "Disable above 控件移除，默认不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-ZOOM-AMOUNT", "Zoom amount 控件移除。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-CURSOR-ZOOM", "Zoom towards cursor 控件移除，默认勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-PIXEL-RELATIVE", "Zoom level is relative to screen pixels 控件移除，默认不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-ZOOM-DEFAULT", "Zoom default 控件移除，默认 Zoom to Fit。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-FIT-LIMIT", "Limit fit/fill zoom 控件移除，默认不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-OVERSCAN", "pixel overscan 输入框移除。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-NAV-RESET", "Navigation resets zoom 控件移除，默认勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-CONSTRAIN", "Constrain image position 控件移除，默认勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-CENTER-SMALL", "Keep centered if smaller 控件移除，默认勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-ORIGINAL-TOGGLE", "Original Size functions as toggle 控件移除，默认不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-IMAGE-COLORSPACE", "Color space conversion 控件移除，默认 Auto-detect。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-IMAGE-DEFAULTS", "Image 固定默认值在 SettingsManager 中可重复读取。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testPreferencesDefaultsAndRemovedControls"),
    c("ST-MISC-NAV-SPEED", "Navigation speed 控件移除，默认值为 50ms。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-MISC-LOOP", "Loop through folders 控件移除，默认不勾选。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-MISC-UPDATE-OLD", "Update notifications on startup 控件和旧设置键移除。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-UPDATE-FREQUENCY-UI", "Automatically check for updates 下拉列表包含四项且默认 Weekly。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-UPDATE-FREQUENCY", "Never/Daily/Weekly/Monthly 的更新策略按日历间隔判定。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testUpdateCheckFrequencyPolicy"),
    c("ST-ASSOCIATE-BUTTON", "Miscellaneous 末尾有 Associate all supported formats 按钮。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("ST-ASSOCIATE-LAUNCHSERVICES", "关联实现按扩展名解析 UTI 并调用 Launch Services。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-ASSOCIATE-DRYRUN", "文件关联计算支持无副作用 dry-run。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testAssociateAllSupportedFormatsDryRun"),
    c("ST-ASSOCIATE-DIALOG", "文件关联完成后使用原生主题感知弹窗提示。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-ASSOCIATE-DIALOG-THEME", "Light/Dark 原生弹窗适配器可实际应用。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNativeDialogsFollowSelectedTheme"),
    c("ST-FORMATS-REMOVED", "Preferences 分类和原生设置 toolbar 中移除 Formats。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("UT-FORMATS-REMOVED", "实际 Preferences 对象树中不存在 Formats 页面。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testSettingsFormatsPaneIsRemoved"),
    c("ST-MIGRATION-OBSOLETE", "旧更新通知和格式禁用键在迁移时被清理，新频率按 Weekly 初始化。", "static", "tests/requirements_pipeline.py::static_contracts"),
    c("IT-BUILD-UI", "当前 CMake 构建产物包含新 UI 和 Cocoa bridge。", "integration", "tests/requirements_pipeline.py::run_integration"),
    c("IT-CTEST-REGRESSION", "CTest 注册的 FovelleTests 全量回归通过。", "integration", "tests/requirements_pipeline.py::run_integration"),
    c("IT-APP-VERSION", "构建出的 App 可被命令行正常解析。", "integration", "tests/requirements_pipeline.py::run_integration"),
    c("SYS-APP-START-MAXIMIZED", "真实 Fovelle.app 启动后创建一个最大化窗口。", "system", "src/main.cpp::FOVELLE_SYSTEM_PROBE"),
    c("SYS-APP-CLEAN-EXIT", "真实 App 探针不需要网络和外部更新状态即可结束。", "system", "tests/requirements_pipeline.py::run_system"),
    c("ST-TEST-SCHEMA", "四类报告中的测试规格具备完整原子字段和执行顺序。", "static", "tests/requirements_pipeline.py::build_specification"),
    c("UT-TASK-REGRESSION", "本任务涉及的 Qt Feature/WindowBehavior 回归套件与新增原子用例全部通过。", "unit", "tests/requirements_pipeline.py::run_unit"),
]

UNIT_METHOD_MARKERS = {
    "UT-NAV-PAINT-FADE": "WindowBehaviorTests::testNavigationButtonUsesTransparentPaintOnlyFade",
    "UT-NAV-BOTH-SIDES": "WindowBehaviorTests::testNavigationButtonsFadeTransition",
    "UT-WINDOW-MAXIMIZED": "WindowBehaviorTests::testNewWindowStartsMaximized",
    "UT-PREFERENCES-REMOVED": "FeatureTests::testPreferencesDefaultsAndRemovedControls",
    "UT-THEME-LABELS": "WindowBehaviorTests::testThemeSettingsReplaceRemovedColorControls",
    "UT-IMAGE-DEFAULTS": "FeatureTests::testPreferencesDefaultsAndRemovedControls",
    "UT-UPDATE-FREQUENCY": "FeatureTests::testUpdateCheckFrequencyPolicy",
    "UT-ASSOCIATE-DRYRUN": "FeatureTests::testAssociateAllSupportedFormatsDryRun",
    "UT-ASSOCIATE-DIALOG-THEME": "WindowBehaviorTests::testNativeDialogsFollowSelectedTheme",
    "UT-FORMATS-REMOVED": "FeatureTests::testSettingsFormatsPaneIsRemoved",
    "UT-TASK-REGRESSION": "Finished testing of WindowBehaviorTests",
}


def read(repo: Path, relative: str) -> str:
    return (repo / relative).read_text(encoding="utf-8")


def check(passed: bool, actual: dict[str, Any]) -> dict[str, Any]:
    return {"passed": bool(passed), "actual": actual}


def static_contracts(repo: Path, specification_valid: bool) -> dict[str, dict[str, Any]]:
    options_cpp = read(repo, "src/qvoptionsdialog.cpp")
    options_header = read(repo, "src/qvoptionsdialog.h")
    options_ui = read(repo, "src/qvoptionsdialog.ui")
    settings = read(repo, "src/settingsmanager.cpp")
    namespace = read(repo, "src/qvnamespace.h")
    application = read(repo, "src/qvapplication.cpp")
    mainwindow = read(repo, "src/mainwindow.cpp")
    main_cpp = read(repo, "src/main.cpp")
    cocoa = read(repo, "src/qvcocoafunctions.mm")
    cocoa_header = read(repo, "src/qvcocoafunctions.h")
    update_checker = read(repo, "src/updatechecker.cpp") + read(repo, "src/updatechecker.h")
    native_dialogs = read(repo, "src/nativedialogs.cpp") + read(repo, "src/nativedialogs.h")
    test_source = read(repo, "tests/tst_qviewtests.cpp")

    try:
        root = ET.fromstring(options_ui)
        widgets = {item.attrib.get("name") for item in root.iter("widget")}
        ui_parse = True
    except ET.ParseError:
        widgets = set()
        ui_parse = False

    removed_widgets = {
        "windowResizeComboBox", "afterMatchingSizeComboBox", "minWindowResizeSpinBox",
        "maxWindowResizeSpinBox", "detailsInFullscreen", "mainMenuIconsCheckbox",
        "contextMenuIconsCheckbox", "submenuIconsCheckbox", "persistSessionCheckbox",
        "scalingTwoCheckbox", "smoothScalingLimitCheckbox", "scaleFactorSpinBox",
        "cursorZoomCheckbox", "oneToOnePixelSizingCheckbox", "zoomDefaultComboBox",
        "fitZoomLimitCheckbox", "fitOverscanSpinBox", "navResetsZoomCheckbox",
        "constrainImagePositionCheckbox", "constrainCentersSmallImageCheckbox",
        "originalSizeAsToggleCheckbox", "colorSpaceConversionComboBox",
        "navSpeedSpinBox", "loopFoldersCheckbox", "updateCheckbox", "formatsTable",
    }
    removed_labels = (
        "Window matches image size", "After matching image size", "Minimum size",
        "Maxium size", "Show titlebar text in full screen", "Show icons in main menus",
        "Show icons in context menus", "Show icons in submenus",
        "Persist session across app restarts", "Expensive scaling above window size",
        "Disable above", "Zoom amount", "Zoom towards cursor",
        "Zoom level is relative to screen pixels", "Zoom default", "Limit fit/fill zoom",
        "pixel overscan", "Navigation resets zoom", "Constrain image position",
        "Keep centered if smaller", "Original Size functions as toggle",
        "Color space conversion", "Navigation speed", "Loop through folders",
        "Update notifications on startup",
    )
    old_controls_absent = all(name not in widgets for name in removed_widgets) and all(
        name not in options_cpp + options_header for name in removed_widgets
    )
    old_labels_absent = all(label not in options_ui for label in removed_labels)
    defaults = {
        "full_false": '"fullscreendetails", {false' in settings,
        "main_false": '"mainmenuicons", {false' in settings,
        "context_true": '"contextmenuicons", {true' in settings,
        "submenu_true": '"submenuicons", {true' in settings,
        "session_false": '"persistsession", {false' in settings,
        "scaling_true": '"scalingtwoenabled", {true' in settings,
        "limit_false": '"smoothscalinglimitenabled", {false' in settings,
        "cursor_true": '"cursorzoom", {true' in settings,
        "pixel_false": '"onetoonepixelsizing", {false' in settings,
        "zoom_fit": "CalculatedZoomMode::ZoomToFit" in settings,
        "fit_false": '"fitzoomlimitenabled", {false' in settings,
        "nav_true": '"navresetszoom", {true' in settings,
        "constrain_true": '"constrainimageposition", {true' in settings,
        "center_true": '"constraincentersmallimage", {true' in settings,
        "toggle_false": '"originalsizeastoggle", {false' in settings,
        "color_auto": "ColorSpaceConversion::AutoDetect" in settings,
        "nav_50": '"navspeed", {50' in settings,
        "loop_false": '"loopfoldersenabled", {false' in settings,
        "weekly": "UpdateCheckFrequency::Weekly" in settings,
    }
    theme_section = options_cpp[options_cpp.find("QVOptionsDialog::mapTheme") :]
    theme_ok = (
        'Qv::Theme::Light, tr("Light")' in theme_section
        and 'Qv::Theme::Dark, tr("Dark")' in theme_section
        and "Qv::Theme::System" in theme_section
        and "Light Theme" not in theme_section
        and "Dark Theme" not in options_ui
    )
    category_ok = (
        all(f'tr("{name}")' in options_cpp for name in ("Window", "Image", "Miscellaneous", "Shortcuts", "Mouse"))
        and 'tr("Formats")' not in options_cpp
    )
    nav_ok = all(
        marker in cocoa
        for marker in (
            "navigationButtonLayers[index] = [CALayer layer]",
            "[navigationButtonLayers[index] addSublayer:navigationBackgroundLayers[index]]",
            "[navigationButtonLayers[index] addSublayer:navigationChevronLayers[index]]",
            "buttonLayer.opacity = boundedOpacity",
            "backgroundLayer.opacity = 1.0F",
            "chevronLayer.opacity = 1.0F",
        )
    ) and "backgroundLayer.opacity = boundedOpacity" not in cocoa and "chevronLayer.opacity = boundedOpacity" not in cocoa
    association_ok = all(
        marker in cocoa + cocoa_header
        for marker in (
            "associateAllSupportedFormats", "UTTagClassFilenameExtension",
            "LSSetDefaultRoleHandlerForContentType", "failedExtensions",
        )
    )
    popup_ok = (
        "associateSupportedFormats" in options_cpp
        and "NativeDialogs::showMessage" in options_cpp
        and "applyTheme(" in native_dialogs
    )
    migration_ok = (
        'settings.remove("updatenotifications")' in settings
        and 'settings.remove("disabledfileextensions")' in settings
        and "UpdateCheckFrequency::Weekly" in settings
    )
    toolbar_item_count = cocoa.count("io.github.inostarlin-passion.Fovelle.settings.")

    result: dict[str, dict[str, Any]] = {}
    result["ST-NAV-SINGLE-OPACITY"] = check(nav_ok, {"single_opacity_owner": nav_ok})
    result["ST-NAV-CHILD-OPAQUE"] = check(nav_ok, {"children_are_full_opacity": nav_ok})
    result["ST-WINDOW-CONTROLS-REMOVED"] = check(ui_parse and old_controls_absent and old_labels_absent, {"ui_parse": ui_parse, "controls_absent": old_controls_absent, "labels_absent": old_labels_absent})
    result["ST-WINDOW-MAXIMIZE-PATH"] = check("w->showMaximized();" in application and "FOVELLE_SYSTEM_PROBE" in main_cpp, {"show_maximized": "w->showMaximized();" in application, "probe": "FOVELLE_SYSTEM_PROBE" in main_cpp})
    result["ST-FULLSCREEN-TITLEBAR-REMOVED"] = check("detailsInFullscreen" not in options_ui and defaults["full_false"] and 'getBoolean("fullscreendetails")' not in mainwindow, {"control_absent": "detailsInFullscreen" not in options_ui, "default_false": defaults["full_false"], "runtime_always_hidden": 'getBoolean("fullscreendetails")' not in mainwindow})
    result["ST-MAIN-MENU-ICONS-REMOVED"] = check("mainMenuIconsCheckbox" not in options_ui and defaults["main_false"] and "showMainMenuIcons = false" in application, {"control_absent": "mainMenuIconsCheckbox" not in options_ui, "default_false": defaults["main_false"], "runtime_false": "showMainMenuIcons = false" in application})
    result["ST-CONTEXT-MENU-ICONS-REMOVED"] = check("contextMenuIconsCheckbox" not in options_ui and defaults["context_true"] and "showContextMenuIcons = true" in application, {"control_absent": "contextMenuIconsCheckbox" not in options_ui, "default_true": defaults["context_true"], "runtime_true": "showContextMenuIcons = true" in application})
    result["ST-SUBMENU-ICONS-REMOVED"] = check("submenuIconsCheckbox" not in options_ui and defaults["submenu_true"] and "showSubmenuIcons = true" in application, {"control_absent": "submenuIconsCheckbox" not in options_ui, "default_true": defaults["submenu_true"], "runtime_true": "showSubmenuIcons = true" in application})
    result["ST-SESSION-REMOVED"] = check("persistSessionCheckbox" not in options_ui and defaults["session_false"], {"control_absent": "persistSessionCheckbox" not in options_ui, "default_false": defaults["session_false"]})
    result["ST-THEME-LABELS"] = check(theme_ok, {"theme_mapping": theme_ok})

    image_checks = {
        "ST-IMAGE-SCALINGTWO": ("scalingTwoCheckbox", defaults["scaling_true"]),
        "ST-IMAGE-SMOOTH-LIMIT": ("smoothScalingLimitCheckbox", defaults["limit_false"]),
        "ST-IMAGE-CURSOR-ZOOM": ("cursorZoomCheckbox", defaults["cursor_true"]),
        "ST-IMAGE-PIXEL-RELATIVE": ("oneToOnePixelSizingCheckbox", defaults["pixel_false"]),
        "ST-IMAGE-ZOOM-DEFAULT": ("zoomDefaultComboBox", defaults["zoom_fit"]),
        "ST-IMAGE-FIT-LIMIT": ("fitZoomLimitCheckbox", defaults["fit_false"]),
        "ST-IMAGE-NAV-RESET": ("navResetsZoomCheckbox", defaults["nav_true"]),
        "ST-IMAGE-CONSTRAIN": ("constrainImagePositionCheckbox", defaults["constrain_true"]),
        "ST-IMAGE-CENTER-SMALL": ("constrainCentersSmallImageCheckbox", defaults["center_true"]),
        "ST-IMAGE-ORIGINAL-TOGGLE": ("originalSizeAsToggleCheckbox", defaults["toggle_false"]),
        "ST-IMAGE-COLORSPACE": ("colorSpaceConversionComboBox", defaults["color_auto"]),
    }
    for identifier, (widget, default_ok) in image_checks.items():
        result[identifier] = check(widget not in options_ui and default_ok, {"widget_absent": widget not in options_ui, "default": default_ok})
    result["ST-IMAGE-ZOOM-AMOUNT"] = check("scaleFactorSpinBox" not in options_ui and '"scalefactor", {25' in settings, {"widget_absent": "scaleFactorSpinBox" not in options_ui, "compat_default_25": '"scalefactor", {25' in settings})
    result["ST-IMAGE-OVERSCAN"] = check("fitOverscanSpinBox" not in options_ui and '"fitoverscan", {0' in settings, {"widget_absent": "fitOverscanSpinBox" not in options_ui, "compat_default_0": '"fitoverscan", {0' in settings})

    result["ST-THEME-LABELS"] = check(theme_ok, {"theme_mapping": theme_ok, "old_labels_absent": "Light Theme" not in options_ui and "Dark Theme" not in options_ui})
    result["ST-MISC-NAV-SPEED"] = check("navSpeedSpinBox" not in options_ui and defaults["nav_50"], {"widget_absent": "navSpeedSpinBox" not in options_ui, "default_50": defaults["nav_50"]})
    result["ST-MISC-LOOP"] = check("loopFoldersCheckbox" not in options_ui and defaults["loop_false"], {"widget_absent": "loopFoldersCheckbox" not in options_ui, "default_false": defaults["loop_false"]})
    result["ST-MISC-UPDATE-OLD"] = check("updateCheckbox" not in options_ui and '"updatenotifications", {' not in settings and "getBoolean(\"updatenotifications\")" not in update_checker, {"widget_absent": "updateCheckbox" not in options_ui, "settings_library_absent": '"updatenotifications", {' not in settings, "runtime_read_absent": "getBoolean(\"updatenotifications\")" not in update_checker})
    frequency_ok = all(marker in options_ui + options_cpp for marker in ("updateFrequencyComboBox", "updatecheckfrequency", "Never", "Daily", "Weekly", "Monthly")) and defaults["weekly"]
    result["ST-UPDATE-FREQUENCY-UI"] = check(frequency_ok, {"frequency_contract": frequency_ok, "default_weekly": defaults["weekly"]})
    result["ST-ASSOCIATE-BUTTON"] = check("associateFormatsButton" in options_ui and "associateSupportedFormats" in options_cpp and "clicked" in options_cpp, {"button": "associateFormatsButton" in options_ui, "slot": "associateSupportedFormats" in options_cpp, "connection": "clicked" in options_cpp})
    result["ST-ASSOCIATE-LAUNCHSERVICES"] = check(association_ok, {"association_contract": association_ok})
    result["ST-ASSOCIATE-DIALOG"] = check(popup_ok, {"native_message": "NativeDialogs::showMessage" in options_cpp, "theme_adapter": "NativeDialogs::applyTheme" in native_dialogs})
    toolbar_items = re.findall(r'Fovelle\.settings\.(window|image|miscellaneous|shortcuts|mouse)', cocoa)
    formats_ok = category_ok and "formatsTable" not in options_ui + options_cpp + options_header and "puzzlepiece.extension" not in cocoa and len(set(toolbar_items)) == 5
    result["ST-FORMATS-REMOVED"] = check(formats_ok, {"categories": category_ok, "formats_table_absent": "formatsTable" not in options_ui + options_cpp + options_header, "native_toolbar_items": sorted(set(toolbar_items))})
    result["ST-MIGRATION-OBSOLETE"] = check(migration_ok, {"migration_contract": migration_ok})
    result["ST-TEST-SCHEMA"] = check(specification_valid, {"specification_valid": specification_valid, "case_count": len(CASES)})

    # Keep source variables referenced in the evidence path so a future
    # refactor cannot accidentally make this gate depend on an incomplete file.
    result["ST-TEST-SCHEMA"]["actual"].update({
        "namespace_enum": "UpdateCheckFrequency" in namespace,
        "test_methods_present": "testPreferencesDefaultsAndRemovedControls" in test_source,
    })
    return result


def run_unit(repo: Path, binary: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    suites = ("FeatureTests", "WindowBehaviorTests")
    executions: dict[str, dict[str, Any]] = {}
    combined_output = ""
    for suite in suites:
        environment = os.environ.copy()
        environment.update({
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "QTEST_FUNCTION_TIMEOUT": "30000",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
            "FOVELLE_TEST_SUITE": suite,
        })
        execution = run_command([str(binary), "-maxwarnings", "0"], repo, environment, timeout=120.0)
        executions[suite] = command_record(execution)
        combined_output += execution.get("stdout", "") + execution.get("stderr", "")
    all_executions_passed = all(item["passed"] for item in executions.values())
    cases: dict[str, dict[str, Any]] = {}
    for item in CASES:
        if item["test_layer"] != "unit":
            continue
        marker = UNIT_METHOD_MARKERS[item["id"]]
        present = marker in combined_output
        cases[item["id"]] = check(all_executions_passed and present, {"marker": marker, "marker_present": present, "suite_executions": executions})
    stage = {"test_level": "unit", "passed": all_executions_passed and all(item["passed"] for item in cases.values()), "execution": {"suite_executions": executions}, "case_count": len(cases)}
    return stage, cases


def run_integration(repo: Path, build_dir: Path, app: Path, binary: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    generated_candidates = (
        build_dir / "Fovelle_autogen/include/ui_qvoptionsdialog.h",
        build_dir / "tests/fovelle_tests_autogen/include/ui_qvoptionsdialog.h",
    )
    generated_ui = next((path for path in generated_candidates if path.is_file()), Path())
    ui_text = generated_ui.read_text(encoding="utf-8") if generated_ui.is_file() else ""
    cases: dict[str, dict[str, Any]] = {
        "IT-BUILD-UI": check(
            app.is_dir() and binary.is_file() and "updateFrequencyComboBox" in ui_text and "formatsTable" not in ui_text,
            {"app_exists": app.is_dir(), "test_binary_exists": binary.is_file(), "generated_ui": str(generated_ui), "new_widget": "updateFrequencyComboBox" in ui_text, "old_formats_widget": "formatsTable" in ui_text},
        )
    }
    environment = os.environ.copy()
    environment.update({"QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1", "QTEST_FUNCTION_TIMEOUT": "30000", "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1"})
    ctest = run_command(["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-R", "^FovelleTests$"], repo, environment, timeout=240.0)
    cases["IT-CTEST-REGRESSION"] = check(ctest["passed"], {"execution": command_record(ctest)})

    version_environment = os.environ.copy()
    version_environment.update({"QT_QPA_PLATFORM": "cocoa", "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1"})
    version = run_command([str(app / "Contents/MacOS/Fovelle"), "--version"], repo, version_environment, timeout=30.0)
    version_output = version.get("stdout", "") + version.get("stderr", "")
    cases["IT-APP-VERSION"] = check(version["passed"] and bool(re.search(r"0\.1\.4", version_output)), {"version_marker": bool(re.search(r"0\.1\.4", version_output)), "execution": command_record(version)})
    stage = {"test_level": "integration", "passed": all(item["passed"] for item in cases.values()), "cases": cases}
    return stage, cases


def run_system(repo: Path, app: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    environment = os.environ.copy()
    environment.update({"QT_QPA_PLATFORM": "cocoa", "FOVELLE_SYSTEM_PROBE": "1", "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1"})
    execution = run_command([str(app / "Contents/MacOS/Fovelle")], repo, environment, timeout=20.0)
    output = execution.get("stdout", "") + execution.get("stderr", "")
    marker = bool(re.search(r"FOVELLE_SYSTEM_PROBE windows=1 maximized=true", output))
    actual = {"probe_marker": marker, "execution": command_record(execution)}
    cases = {
        "SYS-APP-START-MAXIMIZED": check(execution["passed"] and marker, actual),
        "SYS-APP-CLEAN-EXIT": check(execution["passed"] and marker and "timeout_seconds" not in execution, actual),
    }
    return {"test_level": "system", "passed": all(item["passed"] for item in cases.values()), "cases": cases}, cases


def build_specification(repo: Path) -> dict[str, Any]:
    required = ("id", "atomic_criterion", "atomic_acceptance_criterion", "test_purpose", "preconditions", "input_data", "operation_steps", "expected_result", "postconditions", "test_layer", "test_code", "evidence_file")
    errors: list[str] = []
    ids = [item["id"] for item in CASES]
    if len(ids) != len(set(ids)):
        errors.append("duplicate case id")
    for item in CASES:
        missing = [field for field in required if not item.get(field)]
        if missing:
            errors.append(f"{item['id']} missing {missing}")
        if item["test_layer"] not in EXECUTION_ORDER:
            errors.append(f"{item['id']} invalid layer")
    syntax_errors = []
    for path in sorted((repo / "tests").glob("*.py")):
        try:
            ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        except (OSError, SyntaxError) as error:
            syntax_errors.append({"file": str(path), "error": str(error)})
    if syntax_errors:
        errors.append("python syntax errors")
    return {
        "schema_version": "2.0",
        "kind": "atomic-test-case-specification",
        "generated_at_utc": utc_now(),
        "repository": str(repo),
        "execution_order": list(EXECUTION_ORDER),
        "required_case_fields": list(required),
        "case_count": len(CASES),
        "phase_counts": {layer: sum(item["test_layer"] == layer for item in CASES) for layer in EXECUTION_ORDER},
        "cases": CASES,
        "research_trace": RESEARCH_TRACE,
        "python_syntax_errors": syntax_errors,
        "validation_errors": errors,
        "passed": not errors,
    }


def run_pipeline(repo: Path, build_dir: Path, skip_build: bool) -> int:
    report_dir = repo / "reports"
    binary = (build_dir / "tests/fovelle_tests").resolve()
    app = (build_dir / "Fovelle.app").resolve()
    preflight: dict[str, Any] = {"passed": True, "skipped": skip_build}
    if not skip_build:
        preflight = command_record(run_command(["cmake", "--build", str(build_dir), "--parallel", "2"], repo, timeout=300.0))

    specification = build_specification(repo)
    static = static_contracts(repo, specification["passed"])
    static_cases = {item["id"]: static[item["id"]] for item in CASES if item["test_layer"] == "static"}
    if binary.is_file():
        unit_stage, unit_cases = run_unit(repo, binary)
    else:
        unit_stage = {"test_level": "unit", "passed": False, "reason": "test binary missing"}
        unit_cases = {item["id"]: {"passed": False, "actual": {"reason": "test binary missing"}} for item in CASES if item["test_layer"] == "unit"}
    if binary.is_file() and app.is_dir():
        integration_stage, integration_cases = run_integration(repo, build_dir, app, binary)
    else:
        integration_stage = {"test_level": "integration", "passed": False, "reason": "build artifacts missing"}
        integration_cases = {item["id"]: {"passed": False, "actual": {"reason": "build artifacts missing"}} for item in CASES if item["test_layer"] == "integration"}
    if app.is_dir():
        system_stage, system_cases = run_system(repo, app)
    else:
        system_stage = {"test_level": "system", "passed": False, "reason": "application bundle missing"}
        system_cases = {item["id"]: {"passed": False, "actual": {"reason": "application bundle missing"}} for item in CASES if item["test_layer"] == "system"}

    stage_cases = {**static_cases, **unit_cases, **integration_cases, **system_cases}
    stage_passed = {
        "static": bool(preflight["passed"]) and all(item["passed"] for item in static_cases.values()),
        "unit": unit_stage["passed"],
        "integration": integration_stage["passed"],
        "system": system_stage["passed"],
    }
    evidence_cases = []
    missing = []
    for item in CASES:
        observed = stage_cases.get(item["id"])
        if observed is None:
            missing.append(item["id"])
            observed = {"passed": False, "actual": {"missing": True}}
        evidence_cases.append({"id": item["id"], "test_layer": item["test_layer"], "test_code": item["test_code"], "passed": observed["passed"], "actual": observed.get("actual", {})})
    all_passed = specification["passed"] and not missing and all(stage_passed.values())
    evidence = {
        "schema_version": "2.0",
        "kind": "atomic-test-evidence-index",
        "generated_at_utc": utc_now(),
        "repository": str(repo),
        "head_sha": git_head(repo),
        "execution_order": list(EXECUTION_ORDER),
        "preflight_build": preflight,
        "stages": {
            "static": {"passed": stage_passed["static"], "cases": static_cases},
            "unit": unit_stage,
            "integration": integration_stage,
            "system": system_stage,
        },
        "cases": evidence_cases,
        "summary": {
            "atomic_case_count": len(CASES),
            "passed_case_count": sum(item["passed"] for item in evidence_cases),
            "failed_case_count": sum(not item["passed"] for item in evidence_cases),
            "missing_case_ids": missing,
            "stage_passed": stage_passed,
        },
        "research_trace": RESEARCH_TRACE,
        "passed": all_passed,
    }
    write_json(report_dir / "test_case_specification.json", specification)
    write_json(report_dir / "test_evidence.json", evidence)

    completion = {
        "schema_version": "2.0",
        "kind": "test-completion-report",
        "generated_at_utc": utc_now(),
        "repository": str(repo),
        "head_sha": git_head(repo),
        "execution_order": list(EXECUTION_ORDER),
        "preflight_build": preflight,
        "stages": {
            layer: {
                "passed": stage_passed[layer],
                "case_count": sum(item["test_layer"] == layer for item in CASES),
                "passed_case_count": sum(item["passed"] for item in evidence_cases if item["test_layer"] == layer),
            }
            for layer in EXECUTION_ORDER
        },
        "counts": evidence["summary"],
        "artifacts": {
            "test_case_specification.json": artifact(repo, report_dir / "test_case_specification.json"),
            "test_evidence.json": artifact(repo, report_dir / "test_evidence.json"),
        },
        "facts": [
            "The execution order is static, unit, integration, system.",
            "The association bridge has a dry-run unit path; the system stage never changes Launch Services.",
            "The system stage launches the actual Fovelle.app and observes the maximized window through a bounded probe.",
        ],
        "uncertainties": [
            "The native HDR compositor observation is host-specific to macOS/CALayer.",
            "The visual perception of opacity depends on the active display compositor; the fix removes independent alpha owners.",
        ],
        "passed": evidence["passed"],
    }
    write_json(report_dir / "test_completion_report.json", completion)

    quality = {
        "schema_version": "2.0",
        "kind": "code-quality-assessment-report",
        "generated_at_utc": utc_now(),
        "repository": str(repo),
        "head_sha": git_head(repo),
        "scope": "floating navigation fade, Preferences Window/Image/Miscellaneous, file association, and auditable tests",
        "dimensions": [
            {"id": "精益完整性", "passed": stage_passed["static"], "evidence": ["test_evidence.json#stages.static", "test_case_specification.json#cases"]},
            {"id": "功能正确性", "passed": stage_passed["unit"] and stage_passed["integration"] and stage_passed["system"], "evidence": ["test_evidence.json#stages.unit", "test_evidence.json#stages.integration", "test_evidence.json#stages.system"]},
            {"id": "可测试性", "passed": specification["passed"] and not missing and all("actual" in item for item in evidence_cases), "evidence": ["test_case_specification.json", "test_evidence.json"]},
        ],
        "facts": [
            "The native HDR button has one opacity owner and full-opacity child artwork.",
            "The association prompt uses NativeDialogs, which applies the selected Light/Dark Cocoa appearance.",
            "The frequency policy is a pure timestamp function and the association bridge exposes dry-run mode.",
        ],
        "inferences": ["When all four stages pass, the requested behavior is covered for the tested macOS host."],
        "uncertainties": completion["uncertainties"],
        "evidence_artifacts": {name: artifact(repo, report_dir / name) for name in REPORT_NAMES[:-1]},
        "passed": evidence["passed"],
    }
    write_json(report_dir / "code_quality_assessment_report.json", quality)
    print(json.dumps({"passed": quality["passed"], "case_count": len(CASES), "summary": evidence["summary"], "reports": [str(report_dir / name) for name in REPORT_NAMES]}, ensure_ascii=False, indent=2))
    return 0 if quality["passed"] else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    return run_pipeline(repo, build_dir, args.skip_build)


if __name__ == "__main__":
    sys.exit(main())
