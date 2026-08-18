#!/usr/bin/env python3
"""Run static quality gates for the application and GitHub Actions CI contract."""

from __future__ import annotations

import argparse
import ast
import json
import shutil
import subprocess
import sys
from pathlib import Path


def command(*args: str, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(list(args), cwd=cwd, text=True, capture_output=True, check=False)


def add_check(checks: list[dict], identifier: str, passed: bool, actual: object, expected: str) -> None:
    checks.append({"id": identifier, "pass": bool(passed), "actual": actual, "expected": expected})


def contains_all(source: str, needles: tuple[str, ...]) -> bool:
    return all(needle in source for needle in needles)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    checks: list[dict] = []
    relative_sources = (
        "src/mainwindow.cpp",
        "src/qvapplication.cpp",
        "src/qvcocoafunctions.h",
        "src/qvcocoafunctions.mm",
        "src/qvgraphicsview.h",
        "src/qvgraphicsview.cpp",
        "src/qvimageloader.cpp",
        "src/qvoptionsdialog.cpp",
        "src/qvoptionsdialog.ui",
        "src/settingsmanager.cpp",
        "src/shortcutmanager.cpp",
        "src/qvnamespace.h",
        "src/mainwindow.h",
        "tests/tst_qviewtests.cpp",
        "CMakeLists.txt",
        "qView.pro",
        "tests/CMakeLists.txt",
        "build.sh",
        ".clang-tidy",
        ".github/workflows/test.yml",
        ".github/workflows/release.yml",
    )
    source = {relative: (repo / relative).read_text(encoding="utf-8") for relative in relative_sources}

    clang_format = shutil.which("clang-format")
    cpp_files = [repo / relative for relative in relative_sources if relative.endswith((".cpp", ".h", ".mm"))]
    if clang_format:
        format_result = command(
            clang_format,
            "--dry-run",
            "--Werror",
            *(str(path) for path in cpp_files),
            cwd=repo,
        )
        add_check(
            checks,
            "ST-01",
            format_result.returncode == 0,
            {"tool": clang_format, "return_code": format_result.returncode, "output": (format_result.stdout + format_result.stderr)[-2000:]},
            "clang-format reports no changes for the changed C++ files",
        )
    else:
        build_dir = (args.build_dir or repo / "build-fovelle-task").resolve()
        build_result = command("cmake", "--build", str(build_dir), "--parallel", cwd=repo)
        add_check(
            checks,
            "ST-01",
            build_result.returncode == 0,
            {
                "format_tool": None,
                "fallback": "cmake --build",
                "return_code": build_result.returncode,
                "output": (build_result.stdout + build_result.stderr)[-2000:],
            },
            "clang-format is unavailable; the configured build is a clean static/compile gate",
        )

    python_files = sorted((repo / "tests").glob("quality_*.py"))
    syntax_errors: list[dict] = []
    for path in python_files:
        try:
            ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        except (OSError, SyntaxError) as error:
            syntax_errors.append({"file": str(path), "error": str(error)})
    add_check(
        checks,
        "ST-02",
        not syntax_errors,
        {"files": [str(path) for path in python_files], "syntax_errors": syntax_errors},
        "all quality runner Python files parse successfully",
    )

    diff_result = command("git", "diff", "--check", "HEAD", cwd=repo)
    add_check(
        checks,
        "ST-03",
        diff_result.returncode == 0,
        {"return_code": diff_result.returncode, "output": diff_result.stdout + diff_result.stderr},
        "the working-tree diff has no whitespace errors",
    )

    window_cpp = source["src/mainwindow.cpp"]
    application_cpp = source["src/qvapplication.cpp"]
    add_check(
        checks,
        "ST-04",
        "setWindowIcon(QIcon());" in window_cpp
        and "QApplication::setWindowIcon" not in application_cpp
        and "clearTitlebarIcons" in window_cpp
        and "handle->setFilePath(QString());" in window_cpp
        and "windowHandle()->setFilePath" not in window_cpp,
        {
            "window_icon_cleared": "setWindowIcon(QIcon());" in window_cpp,
            "global_icon_assignment_absent": "QApplication::setWindowIcon" not in application_cpp,
            "titlebar_clear_helper": "clearTitlebarIcons" in window_cpp,
            "native_document_path_cleared": "handle->setFilePath(QString());" in window_cpp,
            "no_direct_document_path_assignment": "windowHandle()->setFilePath" not in window_cpp,
            "bundle_resource_retained": "Fovelle.png" in (repo / "resources/resources.qrc").read_text(encoding="utf-8"),
        },
        "the image window clears its native icon and represented document path while the bundle resource remains available",
    )

    graphics_cpp = source["src/qvgraphicsview.cpp"]
    graphics_header = source["src/qvgraphicsview.h"]
    wheel_contract = contains_all(
        graphics_cpp + graphics_header,
        (
            "wheelZoomFactor",
            "event->device()",
            "QInputDevice::DeviceType::TouchPad",
            "useFractionalZoom",
            "wheelDelta > 0 ? 1.0 : -1.0",
            "qPow(zoomMultiplier, wheelSteps)",
        ),
    )
    add_check(
        checks,
        "ST-05",
        wheel_contract,
        {
            "pure_helper": "static qreal wheelZoomFactor" in graphics_header,
            "touch_device_detection": "event->device()" in graphics_cpp and "TouchPad" in graphics_cpp,
            "discrete_mouse_branch": "wheelDelta > 0 ? 1.0 : -1.0" in graphics_cpp,
            "power_calculation": "qPow(zoomMultiplier, wheelSteps)" in graphics_cpp,
        },
        "mouse wheels use one signed step per event and touch devices retain fractional steps",
    )

    cocoa_header = source["src/qvcocoafunctions.h"]
    cocoa_mm = source["src/qvcocoafunctions.mm"]
    loader_cpp = source["src/qvimageloader.cpp"]
    native_decoder_contract = contains_all(
        cocoa_header + cocoa_mm + loader_cpp,
        (
            "CGImageSourceCopyTypeIdentifiers",
            "CGImageSourceCreateWithURL",
            "CGImageSourceCreateThumbnailAtIndex",
            "kCGImageSourceCreateThumbnailWithTransform",
            "kCGImageSourceCreateThumbnailFromImageAlways",
            "sourceMaxPixelSize",
            "supportsAdditionalImageFormat",
            "readAdditionalImage",
            "QVCocoaFunctions::readAdditionalImage",
            "useNativeImageIO",
        ),
    )
    add_check(
        checks,
        "ST-06",
        native_decoder_contract,
        {
            "image_io_type_query": "CGImageSourceCopyTypeIdentifiers" in cocoa_mm,
            "image_io_decode": "CGImageSourceCreateThumbnailAtIndex" in cocoa_mm,
            "orientation_transform": "kCGImageSourceCreateThumbnailWithTransform" in cocoa_mm,
            "loader_native_decoder": "useNativeImageIO" in loader_cpp,
            "loader_image_io_call": "QVCocoaFunctions::readAdditionalImage" in loader_cpp,
        },
        "the macOS Image I/O decoder is the canonical WebP/AVIF path, with Qt retained as a fallback",
    )

    formats_app = contains_all(
        application_cpp,
        (
            "getAdditionalImageFormats()",
            "addExtension(fileExtension)",
            'addExtension(".avifs")',
            "getAdditionalImageMimeTypes()",
        ),
    )
    formats_settings = "getAllFileExtensionList()" in source["src/qvoptionsdialog.cpp"]
    add_check(
        checks,
        "ST-07",
        formats_app and formats_settings,
        {
            "application_registry": formats_app,
            "settings_uses_all_extensions": formats_settings,
            "native_test_assertions": contains_all(source["tests/tst_qviewtests.cpp"], ("contains(\".webp\")", "contains(\".avif\")", "contains(\".avifs\")")),
        },
        "Settings → Formats consumes the same complete extension registry that includes WebP and AVIF",
    )

    frameworks = contains_all(
        source["CMakeLists.txt"] + source["qView.pro"] + source["tests/CMakeLists.txt"],
        ("CoreGraphics", "ImageIO"),
    )
    add_check(
        checks,
        "ST-08",
        frameworks,
        {"frameworks_declared": frameworks},
        "application, qmake, and test targets link the native frameworks",
    )

    test_source = source["tests/tst_qviewtests.cpp"]
    test_markers = (
        "testImageLoaderLoadsWebpWithImageIOFallback",
        "testImageLoaderLoadsAvifWithImageIOFallback",
        "testImageLoaderAppliesWebpOrientation",
        "testImageLoaderAppliesAvifOrientation",
        "testWindowIconIsCleared",
        "testTitlebarDocumentProxyIsClearedForLoadedFile",
        "testTitlebarIconClearingIsIdempotent",
        "testSettingsFormatsIncludeNativeImageFormats",
        "testSmallImageOneToOneSettingIsExposedInImageOptions",
        "testOpenWithWorkerTeardownContract",
        "testMouseWheelUsesOneDiscreteStep",
        "testTouchpadWheelCanUseFractionalSteps",
        "testFitZoomSurvivesInverseWheelStepsAndFullscreenResize",
        "testManualZoomRemainsManualAcrossResize",
        "testSmallImageOneToOnePolicyUsesViewportAndWindowMode",
        "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages",
        "testFullscreenDefaultShortcutIsEnterAndConfigurable",
        "testEnterDoesNotBypassClearedFullscreenShortcut",
        "testConfiguredFullscreenShortcutStillWorks",
        "testPracticalTitlebarTextUsesFilenameAndSequence",
        "testVerboseTitlebarTextUsesAllRequestedFields",
        "testThemeSettingsReplaceRemovedColorControls",
        "testThemeAppliesNativeAppearanceAndViewportBackground",
        "testCheckerboardOverridesThemeAndRestoresBackground",
    )
    add_check(
        checks,
        "ST-09",
        all(marker in test_source for marker in test_markers),
        {"test_markers": {marker: marker in test_source for marker in test_markers}},
        "each atomic feature criterion has a deterministic test implementation",
    )

    zoom_continuity = contains_all(
        graphics_cpp + graphics_header,
        (
            "lastCalculatedZoomMode",
            "lastCalculatedZoomLevel",
            "zoomLevelsEquivalent",
            "shouldRestoreCalculatedZoom",
            "resizeEvent",
        ),
    )
    add_check(
        checks,
        "ST-10",
        zoom_continuity,
        {
            "calculated_mode_snapshot": "lastCalculatedZoomMode" in graphics_cpp,
            "calculated_level_snapshot": "lastCalculatedZoomLevel" in graphics_cpp,
            "floating_point_tolerance": "zoomLevelsEquivalent" in graphics_cpp + graphics_header,
            "resize_restoration_path": "shouldRestoreCalculatedZoom" in graphics_cpp,
        },
        "the fullscreen resize path can restore fit intent after an inverse manual zoom without changing other manual zoom levels",
    )

    options_cpp = source["src/qvoptionsdialog.cpp"]
    options_ui = source["src/qvoptionsdialog.ui"]
    settings_cpp = source["src/settingsmanager.cpp"]
    small_image_contract = contains_all(
        settings_cpp + options_cpp + options_ui + graphics_cpp + graphics_header,
        (
            'settingsLibrary.insert("smallimageoneone", {false, {}});',
            'syncCheckbox(ui->smallImagesOneToOneCheckbox, "smallimageoneone", defaults, makeConnections);',
            'name="smallImagesOneToOneCheckbox"',
            "Show small images at 1:1",
            "shouldDisplaySmallImageAtOneToOne",
            "getUsableViewportRect().size()",
            "WindowResizeMode::Never",
            "CalculatedZoomMode::ZoomToFit",
            "showSmallImagesAtOneToOne",
        ),
    )
    add_check(
        checks,
        "ST-11",
        small_image_contract,
        {
            "setting_default": 'settingsLibrary.insert("smallimageoneone", {false, {}});' in settings_cpp,
            "settings_binding": 'syncCheckbox(ui->smallImagesOneToOneCheckbox, "smallimageoneone"' in options_cpp,
            "image_checkbox": 'name="smallImagesOneToOneCheckbox"' in options_ui,
            "strict_small_image_helper": "shouldDisplaySmallImageAtOneToOne" in graphics_cpp + graphics_header,
            "actual_viewport": "getUsableViewportRect().size()" in graphics_cpp,
            "never_gate": "WindowResizeMode::Never" in graphics_cpp,
            "automatic_mode_override": "calculatedZoomMode.has_value()" in graphics_cpp,
        },
        "the persisted Image option and deterministic viewport/window-mode policy are wired into automatic fit zoom",
    )

    mainwindow_header = source["src/mainwindow.h"]
    issue_864_contract = contains_all(
        window_cpp + mainwindow_header + test_source,
        (
            "populateOpenWithTimer->stop();",
            "openWithFutureWatcher.isRunning()",
            "openWithFutureWatcher.waitForFinished();",
            "openWithFutureFilePath",
            "[filePath]()",
            "openWithPopulationPending",
            "testOpenWithWorkerTeardownContract",
        ),
    )
    add_check(
        checks,
        "ST-12",
        issue_864_contract,
        {
            "timer_stopped_on_teardown": "populateOpenWithTimer->stop();" in window_cpp,
            "future_waited_on_teardown": "openWithFutureWatcher.waitForFinished();" in window_cpp,
            "path_captured_by_value": "[filePath]()" in window_cpp,
            "serial_refresh_guard": "openWithPopulationPending" in window_cpp,
            "regression_test": "testOpenWithWorkerTeardownContract" in test_source,
        },
        "Open With background work is serialized, uses a value-captured path, and is finished before QApplication teardown",
    )

    release_workflow = source[".github/workflows/release.yml"]
    release_contract = contains_all(
        release_workflow,
        (
            "push:",
            "tags:",
            "- 'v*'",
            "permissions:",
            "contents: write",
            "softprops/action-gh-release@v3",
            "GITHUB_TOKEN",
            "generate_release_notes: true",
            "fail_on_unmatched_files: true",
            "ctest --test-dir build --output-on-failure",
        ),
    )
    add_check(
        checks,
        "ST-13",
        release_contract,
        {
            "tag_trigger": "- 'v*'" in release_workflow,
            "write_permission": "contents: write" in release_workflow,
            "action": "softprops/action-gh-release@v3" in release_workflow,
            "token": "GITHUB_TOKEN" in release_workflow,
            "pre_release_tests": "ctest --test-dir build --output-on-failure" in release_workflow,
        },
        "a pushed v-prefixed Git tag builds, tests, packages, and publishes a GitHub Release with softprops/action-gh-release",
    )

    shortcut_contract = (
        'shortcutsList.append({tr("Full Screen"), "fullscreen", QStringList(QKeySequence(Qt::Key_Return).toString()), {}});' in source["src/shortcutmanager.cpp"]
        and "returnShortcut" not in window_cpp
        and "keypadEnterShortcut" not in window_cpp
        and "returnShortcut" not in source["src/mainwindow.h"]
        and "keypadEnterShortcut" not in source["src/mainwindow.h"]
    )
    add_check(
        checks,
        "ST-14",
        shortcut_contract,
        {
            "default_return": 'QStringList(QKeySequence(Qt::Key_Return).toString())' in source["src/shortcutmanager.cpp"],
            "hardcoded_return_removed": "returnShortcut" not in window_cpp,
            "hardcoded_keypad_enter_removed": "keypadEnterShortcut" not in window_cpp,
            "test_coverage": all(marker in test_source for marker in (
                "testFullscreenDefaultShortcutIsEnterAndConfigurable",
                "testEnterDoesNotBypassClearedFullscreenShortcut",
                "testConfiguredFullscreenShortcutStillWorks",
            )),
        },
        "Enter is owned by the configurable Full Screen QAction; MainWindow has no independent Enter shortcut",
    )

    title_contract = all(
        marker in window_cpp + test_source
        for marker in (
            'newString = getFileName() + " - " + getImageIndex() + "/" + getImageCount();',
            'getImageWidth() + "x" + getImageHeight() + " - " + getFileSize() + " - " + getZoomLevel()',
            "testPracticalTitlebarTextUsesFilenameAndSequence",
            "testVerboseTitlebarTextUsesAllRequestedFields",
        )
    )
    add_check(
        checks,
        "ST-15",
        title_contract,
        {"practical_and_verbose_formatters_and_tests": title_contract},
        "Practical and Verbose titlebar modes follow the requested filename-first field order",
    )

    theme_cpp = source["src/qvcocoafunctions.mm"] + source["src/qvcocoafunctions.h"]
    theme_ui_contract = all(
        marker in settings_cpp + options_cpp + options_ui + window_cpp + theme_cpp
        for marker in (
            'settingsLibrary.insert("theme", {static_cast<int>(Qv::Theme::Light), {}});',
            'syncComboBox(ui->themeComboBox, "theme", defaults, makeConnections);',
            'name="themeComboBox"',
            'tr("Light Theme")',
            'tr("Dark Theme")',
            'QVCocoaFunctions::setWindowTheme(theme, windowHandle());',
            'setWindowTheme(Qv::Theme theme, QWindow *window);',
            'NSAppearanceNameAqua',
            'NSAppearanceNameDarkAqua',
            'QColor("#212121")',
            'QColor("#969696")',
        )
    ) and 'name="bgColorCheckbox"' not in options_ui and 'name="darkTitlebarCheckbox"' not in options_ui
    add_check(
        checks,
        "ST-16",
        theme_ui_contract,
        {
            "theme_setting_default": 'settingsLibrary.insert("theme", {static_cast<int>(Qv::Theme::Light), {}});' in settings_cpp,
            "theme_binding": 'syncComboBox(ui->themeComboBox, "theme"' in options_cpp,
            "two_theme_labels": 'tr("Light Theme")' in options_cpp and 'tr("Dark Theme")' in options_cpp,
            "old_controls_removed": 'name="bgColorCheckbox"' not in options_ui and 'name="darkTitlebarCheckbox"' not in options_ui,
            "standard_native_appearances": 'NSAppearanceNameAqua' in theme_cpp and 'NSAppearanceNameDarkAqua' in theme_cpp,
            "viewport_colors": 'QColor("#212121")' in window_cpp and 'QColor("#969696")' in window_cpp,
        },
        "Theme is the only Window color control, defaults to Light, and maps to standard Aqua/DarkAqua plus deterministic viewport colors",
    )

    theme_test_contract = all(marker in test_source for marker in (
        "testThemeSettingsReplaceRemovedColorControls",
        "testThemeAppliesNativeAppearanceAndViewportBackground",
        "testCheckerboardOverridesThemeAndRestoresBackground",
    ))
    add_check(
        checks,
        "ST-17",
        theme_test_contract,
        {"theme_and_checkerboard_tests": theme_test_contract},
        "Theme controls, native appearance, viewport colors, and checkerboard precedence have deterministic tests",
    )

    tidy_config = source[".clang-tidy"]
    tidy_workflow = source[".github/workflows/test.yml"]
    tidy_script = source["build.sh"]
    tidy_contract = contains_all(
        tidy_config,
        (
            "Checks:",
            "bugprone-use-after-move",
            "performance-move-const-arg",
            "clang-diagnostic-*",
        ),
    ) and contains_all(tidy_workflow + tidy_script, ("brew install llvm", "--tidy", "CMAKE_CXX_CLANG_TIDY=clang-tidy"))
    add_check(
        checks,
        "ST-18",
        tidy_contract,
        {
            "config_has_enabled_checks": tidy_contract,
            "workflow_installs_llvm": "brew install llvm" in tidy_workflow,
            "build_script_wires_clang_tidy": "CMAKE_CXX_CLANG_TIDY=clang-tidy" in tidy_script,
        },
        "the CI clang-tidy job has an explicit non-empty check configuration and a reproducible tool invocation",
    )

    result = {"kind": "static", "repo": str(repo), "checks": checks, "passed": all(item["pass"] for item in checks)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
