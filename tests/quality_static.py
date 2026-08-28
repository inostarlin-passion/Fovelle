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

from project_version import read_project_version


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
    project_version = read_project_version(repo)
    checks: list[dict] = []
    relative_sources = (
        "src/mainwindow.cpp",
        "src/qvapplication.cpp",
        "src/actionmanager.cpp",
        "src/qvcocoafunctions.h",
        "src/qvcocoafunctions.mm",
        "src/qvgraphicsview.h",
        "src/qvgraphicsview.cpp",
        "src/qvimageloader.cpp",
        "src/qvmovie.cpp",
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
        ".github/workflows/build.yml",
        ".github/workflows/release.yml",
        ".github/workflows/release-compatibility.yml",
        "dist/scripts/package-macos-release.sh",
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

    task_scope_paths = (
        "src",
        "tests",
        "CMakeLists.txt",
        "qView.pro",
        "build.sh",
        ".clang-tidy",
        ".github",
        "dist",
    )
    diff_result = command("git", "diff", "--check", "HEAD", "--", *task_scope_paths, cwd=repo)
    add_check(
        checks,
        "ST-03",
        diff_result.returncode == 0,
        {
            "return_code": diff_result.returncode,
            "output": diff_result.stdout + diff_result.stderr,
            "scope": list(task_scope_paths),
            "excluded_preexisting_paths": ["README.md"],
        },
        "the task-scoped working-tree diff has no whitespace errors; unrelated pre-existing README.md changes remain untouched",
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
            "event->pixelDelta()",
            "Qt::NoScrollPhase",
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
            "touch_device_fractional_zoom": "event->device()" in graphics_cpp and "TouchPad" in graphics_cpp
            and "Qt::NoScrollPhase" in graphics_cpp,
            "wheel_event_remains_configured": "executeScrollAction" in graphics_cpp,
            "discrete_mouse_branch": "wheelDelta > 0 ? 1.0 : -1.0" in graphics_cpp,
            "power_calculation": "qPow(zoomMultiplier, wheelSteps)" in graphics_cpp,
        },
        "discrete wheel events use configured actions; unmodified phased touchpad streams pan in pixels, while native pinch remains a separate zoom path",
    )

    cocoa_header = source["src/qvcocoafunctions.h"]
    cocoa_mm = source["src/qvcocoafunctions.mm"]
    loader_cpp = source["src/qvimageloader.cpp"]
    native_decoder_contract = contains_all(
        cocoa_header + cocoa_mm + loader_cpp,
        (
            "CGImageSourceCopyTypeIdentifiers",
            "CGImageSourceCreateWithURL",
            "CGImageSourceGetType",
            "CGImageSourceCreateThumbnailAtIndex",
            "kCGImageSourceCreateThumbnailWithTransform",
            "kCGImageSourceCreateThumbnailFromImageAlways",
            "fullResolutionThumbnailOptions",
            "sourceMaxPixelSize",
            "supportsAdditionalImageFormat",
            "readAdditionalImage",
            "readImageWithImageIO",
            "QVCocoaFunctions::readImageWithImageIO",
            "CIRAWFilter",
            "filterWithImageURL",
            "previewImage",
            "CIContext",
            "contextWithMTLDevice",
            "MTLCreateSystemDefaultDevice",
            "ColorSyncProfileCreate",
            "CGColorSpaceCreateWithColorSyncProfile",
            "kCIContextWorkingColorSpace",
            "kCIContextOutputColorSpace",
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
            "loader_image_io_call": "QVCocoaFunctions::readImageWithImageIO" in loader_cpp,
            "full_resolution_native_decode": "fullResolutionThumbnailOptions" in cocoa_mm,
            "loader_bounds_only_hdr_fallback": "readImageWithImageIO(absoluteFilePath, qMin(largestDimension, 2048))" in loader_cpp,
            "raw_uses_content_type": "nativeResult.isRaw" in loader_cpp and "!nativeResult.isRaw" in loader_cpp,
            "preview_fallback": "rawFilter.previewImage" in cocoa_mm and "usedRawPreview" in cocoa_mm,
            "metal_context": "contextWithMTLDevice" in cocoa_mm and "MTLCreateSystemDefaultDevice" in cocoa_mm,
            "colorsync_context": "ColorSyncProfileCreate" in cocoa_mm and "kCIContextOutputColorSpace" in cocoa_mm,
        },
        "Image I/O identifies every native image by UTI; RAW uses CIRAWFilter/CIImage, embedded preview fallback, ColorSync, and Metal-backed CIContext, while Qt remains a non-RAW fallback",
    )

    movie_cpp = source["src/qvmovie.cpp"]
    apng_contract = contains_all(
        cocoa_header + cocoa_mm + movie_cpp,
        (
            "class AnimatedImage",
            "createAnimatedImage",
            "CGImageSourceGetCount",
            "kCGImagePropertyAPNGDelayTime",
            "nativeAnimation",
            "playCounter = nativeAnimation ? nativeAnimation->loopCount() : reader->loopCount();",
        ),
    )
    add_check(
        checks,
        "ST-06-APNG",
        apng_contract,
        {
            "native_animation_interface": "class AnimatedImage" in cocoa_header,
            "image_io_frame_count": "CGImageSourceGetCount" in cocoa_mm,
            "apng_delay_metadata": "kCGImagePropertyAPNGDelayTime" in cocoa_mm,
            "movie_uses_native_animation": "nativeAnimation" in movie_cpp,
            "native_loop_count": "playCounter = nativeAnimation ? nativeAnimation->loopCount() : reader->loopCount();" in movie_cpp,
        },
        "APNG uses macOS Image I/O frame composition, per-frame delays, loop metadata, and the existing QVMovie timer/cache contract",
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
        ("CoreGraphics", "ImageIO", "CoreImage", "Metal", "ColorSync", "CoreServices"),
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
        "testApplicationVersionIsCurrent",
        "testImageLoaderLoadsWebpWithImageIOFallback",
        "testImageLoaderLoadsAvifWithImageIOFallback",
        "testImageLoaderAppliesWebpOrientation",
        "testImageLoaderAppliesAvifOrientation",
        "testImageLoaderLoadsTiffWithImageIO",
        "testImageIOUsesContentTypeInsteadOfFilenameExtension",
        "testImageLoaderPreservesSourceResolutionForZoom",
        "testAnimatedPngPlaysBeyondFirstFrame",
        "testWindowIconIsCleared",
        "testTitlebarDocumentProxyIsClearedForLoadedFile",
        "testTitlebarIconClearingIsIdempotent",
        "testSettingsFormatsIncludeNativeImageFormats",
        "testSettingsFormatsIncludeTiffAndSystemRawFormats",
        "testSmallImageOneToOneSettingIsExposedInImageOptions",
        "testOpenWithWorkerTeardownContract",
        "testMouseWheelUsesOneDiscreteStep",
        "testTouchpadWheelCanUseFractionalSteps",
        "testImageIsCenteredAfterOpeningWithScrollBars",
        "testTouchpadWheelRespectsConfiguredZoomWithScrollBars",
        "testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling",
        "testRotatedZoomToFitUsesUnobscuredViewport",
        "testZoomAcrossScrollbarThresholdKeepsViewportCenterStable",
        "testTouchpadPanUsesPixelsWithoutChangingZoom",
        "testFitZoomSurvivesInverseWheelStepsAndFullscreenResize",
        "testManualZoomRemainsManualAcrossResize",
        "testSmallImageOneToOnePolicyUsesViewportAndWindowMode",
        "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages",
        "testNativePinchZoomChangesScaleAtGesturePosition",
        "testNativePanChangesViewport",
        "testScrollBarsFollowImageOverflowAxes",
        "testScrollBarsMatchTheme",
        "testNativeGestureResponsePerformance",
        "testFullscreenDefaultShortcutIsEnterAndConfigurable",
        "testEnterDoesNotBypassClearedFullscreenShortcut",
        "testConfiguredFullscreenShortcutStillWorks",
        "testPracticalTitlebarTextUsesFilenameAndSequence",
        "testDefaultTitlebarTextIsPractical",
        "testVerboseTitlebarTextUsesAllRequestedFields",
        "testThemeSettingsReplaceRemovedColorControls",
        "testThemeAppliesNativeAppearanceAndViewportBackground",
        "testCheckerboardOverridesThemeAndRestoresBackground",
        "testNavigationEdgeActivationExcludesTitlebar",
        "testNavigationButtonSizingAndNoDelay",
        "testNavigationButtonsUseActualContentContrast",
        "testNavigationButtonsFadeTransition",
        "testNavigationButtonsClickSwitchesFiles",
    )
    add_check(
        checks,
        "ST-09",
        all(marker in test_source for marker in test_markers),
        {"test_markers": {marker: marker in test_source for marker in test_markers}},
        "each atomic feature criterion has a deterministic test implementation",
    )

    native_gesture_contract = contains_all(
        graphics_cpp + graphics_header,
        (
            "QNativeGestureEvent",
            "QEvent::NativeGesture",
            "Qt::ZoomNativeGesture",
            "Qt::PanNativeGesture",
            "viewportEvent",
            "nativeGestureZoomFactor",
            "nativeGesturePanScrollDelta",
            "isPhasedTouchpadScroll",
            "wheel-trackpad-pan",
        ),
    )
    scrollbar_contract = contains_all(
        graphics_cpp + graphics_header,
        (
            "Qt::ScrollBarAsNeeded",
            "setSceneRect",
            "scrollBarStyleSheet",
            "QScrollBar::handle",
            "QScrollBar::add-page",
            "Qv::Theme::Dark",
        ),
    )
    add_check(
        checks,
        "ST-23-GESTURE-SCROLLBAR",
        native_gesture_contract and scrollbar_contract,
        {
            "native_event_path": native_gesture_contract,
            "scrollbar_overflow_path": scrollbar_contract,
            "native_gesture_markers": {marker: marker in graphics_cpp + graphics_header for marker in (
                "QNativeGestureEvent",
                "QEvent::NativeGesture",
                "Qt::ZoomNativeGesture",
                "Qt::PanNativeGesture",
                "viewportEvent",
                "nativeGestureZoomFactor",
                "nativeGesturePanScrollDelta",
                "isPhasedTouchpadScroll",
                "wheel-trackpad-pan",
            )},
            "scrollbar_markers": {marker: marker in graphics_cpp + graphics_header for marker in (
                "Qt::ScrollBarAsNeeded",
                "setSceneRect",
                "scrollBarStyleSheet",
                "QScrollBar::handle",
                "QScrollBar::add-page",
                "Qv::Theme::Dark",
            )},
        },
        "Apple native gesture events drive zoom/pan, overflow drives AsNeeded bars, and the selected Theme drives handle/track styles",
    )

    layout_contract = contains_all(
        graphics_cpp + graphics_header,
        (
            "getSceneRectForViewport",
            "getViewportPosition().obscuredHeight",
            "scenePadding",
            "updateSceneRect();",
        ),
    )
    layout_test_contract = contains_all(
        test_source,
        (
            "testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling",
            "testRotatedZoomToFitUsesUnobscuredViewport",
            "imageRectInViewport.top() >= usableViewport.top() - 2",
            "imageRectInViewport.bottom() <= usableViewport.bottom() + 2",
            "usableViewport.center()",
        ),
    )
    add_check(
        checks,
        "ST-24-LAYOUT-TITLEBAR",
        layout_contract and layout_test_contract,
        {
            "scene_rect_compensation": layout_contract,
            "regression_test_contract": layout_test_contract,
            "markers": {
                "getSceneRectForViewport": "getSceneRectForViewport" in graphics_cpp + graphics_header,
                "obscured_height": "getViewportPosition().obscuredHeight" in graphics_cpp + graphics_header,
                "scene_padding": "scenePadding" in graphics_cpp,
                "usable_top_bottom_assertions": "imageRectInViewport.top() >= usableViewport.top() - 2" in test_source
                and "imageRectInViewport.bottom() <= usableViewport.bottom() + 2" in test_source,
            },
        },
        "fit layout compensates for the macOS titlebar-obscured region and verifies both image edges against the unobscured viewport",
    )

    reentrancy_contract = contains_all(
        graphics_cpp + graphics_header,
        (
            "isUpdatingSceneRect",
            "QScopedValueRollback",
            "if (isUpdatingSceneRect)",
            "if (sceneRect() != desiredSceneRect)",
        ),
    )
    workflow_timeout_contract = contains_all(
        source[".github/workflows/test.yml"] + source[".github/workflows/build.yml"] + source["tests/CMakeLists.txt"],
        (
            "QTEST_FUNCTION_TIMEOUT=30000",
            "TIMEOUT 90",
            "--timeout 90",
            "timeout-minutes: 10",
        ),
    )
    add_check(
        checks,
        "ST-25-CI-TEST-BOUNDS",
        reentrancy_contract and workflow_timeout_contract,
        {
            "scene_rect_reentrancy_guard": reentrancy_contract,
            "test_window_contract": workflow_timeout_contract,
            "markers": {
                "QScopedValueRollback": "QScopedValueRollback" in graphics_cpp,
                "scene_rect_change_filter": "if (sceneRect() != desiredSceneRect)" in graphics_cpp,
                "qtest_function_timeout": "QTEST_FUNCTION_TIMEOUT=30000" in source["tests/CMakeLists.txt"],
                "ctest_timeout": "--timeout 90" in source[".github/workflows/test.yml"] + source[".github/workflows/build.yml"],
            },
        },
        "scene-rect updates cannot recursively trigger fit passes, and CI/CTest terminate a stuck test within a documented bounded window",
    )

    workflow_sources = (
        source[".github/workflows/test.yml"],
        source[".github/workflows/build.yml"],
        source[".github/workflows/release.yml"],
        source[".github/workflows/release-compatibility.yml"],
    )
    build_workflow = source[".github/workflows/build.yml"]
    checks_workflow = source[".github/workflows/test.yml"]
    release_workflow = source[".github/workflows/release.yml"]
    compatibility_workflow = source[".github/workflows/release-compatibility.yml"]
    runner_sdk_contract = (
        all(
            "runs-on: macos-26" in workflow
            and "runs-on: macos-14" not in workflow
            and "xcodebuild -version" in workflow
            and "xcrun --sdk macosx --show-sdk-version" in workflow
            and 'test "${XCODE_VERSION%%.*}" -ge 26' in workflow
            and 'test "${SDK_VERSION%%.*}" -ge 26' in workflow
            and "version: '6.11.2'" in workflow
            and "version: '6.8" not in workflow
            and "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0" in workflow
            for workflow in (checks_workflow, build_workflow)
        )
        and "runs-on: macos-15" in release_workflow
        and "runs-on: macos-14" not in release_workflow
        and "xcodebuild -version" in release_workflow
        and "xcrun --sdk macosx --show-sdk-version" in release_workflow
        and 'test "${XCODE_VERSION%%.*}" -ge 16' in release_workflow
        and 'test "${SDK_VERSION%%.*}" -eq 15' in release_workflow
        and "version: '6.11.2'" in release_workflow
        and "version: '6.8" not in release_workflow
        and "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0" in release_workflow
        and "runs-on: macos-15" in compatibility_workflow
        and 'test "${XCODE_VERSION%%.*}" -ge 16' in compatibility_workflow
        and 'test "${SDK_VERSION%%.*}" -eq 15' in compatibility_workflow
        and "version: '6.11.2'" in compatibility_workflow
        and "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0" in compatibility_workflow
    )
    add_check(
        checks,
        "ST-27-CI-APPLE-SDK",
        runner_sdk_contract,
        {
            "build_and_checks_use_macos_26": all("runs-on: macos-26" in workflow for workflow in (build_workflow, checks_workflow)),
            "release_uses_macos_15": "runs-on: macos-15" in release_workflow,
            "release_compatibility_uses_macos_15": "runs-on: macos-15" in compatibility_workflow,
            "legacy_macos_14_runner_absent": all("runs-on: macos-14" not in workflow for workflow in workflow_sources),
            "xcode_version_is_verified": all("xcodebuild -version" in workflow for workflow in workflow_sources),
            "sdk_version_is_verified": all("xcrun --sdk macosx --show-sdk-version" in workflow for workflow in workflow_sources),
            "build_and_checks_xcode_26_minimum": all('test "${XCODE_VERSION%%.*}" -ge 26' in workflow for workflow in (build_workflow, checks_workflow)),
            "build_and_checks_sdk_26_minimum": all('test "${SDK_VERSION%%.*}" -ge 26' in workflow for workflow in (build_workflow, checks_workflow)),
            "release_xcode_16_minimum": 'test "${XCODE_VERSION%%.*}" -ge 16' in release_workflow,
            "release_sdk_15_exact": 'test "${SDK_VERSION%%.*}" -eq 15' in release_workflow,
            "release_compatibility_sdk_15_exact": 'test "${SDK_VERSION%%.*}" -eq 15' in compatibility_workflow,
            "qt_6_11_2_is_pinned": all("version: '6.11.2'" in workflow for workflow in workflow_sources),
            "legacy_qt_6_8_is_absent": all("version: '6.8" not in workflow for workflow in workflow_sources),
            "deployment_target_15_is_explicit": all("-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0" in workflow for workflow in workflow_sources),
        },
        "build/check jobs use macOS 26 for HDR headers while Release uses the macOS 15 SDK and all jobs pin Qt 6.11.2 and deployment target 15.0",
    )

    bounded_build_contract = (
        all("run: cmake --build build --parallel 2" in source[path] for path in (
            ".github/workflows/test.yml",
            ".github/workflows/build.yml",
            ".github/workflows/release.yml",
            ".github/workflows/release-compatibility.yml",
        ))
        and "run: cmake --build build --parallel\n" not in source[".github/workflows/test.yml"]
    )
    add_check(
        checks,
        "ST-28-CI-BUILD-PARALLELISM",
        bounded_build_contract,
        {
            "test_workflow_uses_parallel_2": "run: cmake --build build --parallel 2" in source[".github/workflows/test.yml"],
            "build_workflow_uses_parallel_2": "run: cmake --build build --parallel 2" in source[".github/workflows/build.yml"],
            "release_workflow_uses_parallel_2": "run: cmake --build build --parallel 2" in source[".github/workflows/release.yml"],
            "unbounded_test_build_is_absent": "run: cmake --build build --parallel\n" not in source[".github/workflows/test.yml"],
        },
        "CI compilation uses a bounded parallelism contract that keeps the Checks job inside its timeout window",
    )

    cross_dpi_test_contract = (
        "itemsBoundingRect().width() >= 1200" in test_source
        and "itemsBoundingRect().width() > 1200" not in test_source
        and "Retina and non-Retina runners" in test_source
    )
    add_check(
        checks,
        "ST-26-CROSS-DPI-GEOMETRY",
        cross_dpi_test_contract,
        {
            "non_retina_safe_lower_bound": "itemsBoundingRect().width() >= 1200" in test_source,
            "retina_only_strict_bound_absent": "itemsBoundingRect().width() > 1200" not in test_source,
            "cross_dpi_documentation": "Retina and non-Retina runners" in test_source,
        },
        "geometry regression tests assert the rendering invariant without requiring a Retina backing scale",
    )

    version_contract = (
        (repo / "VERSION").read_text(encoding="utf-8").strip() == project_version
        and 'set(FOVELLE_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")'
        in source["CMakeLists.txt"]
        and "project(Fovelle VERSION ${FOVELLE_VERSION}" in source["CMakeLists.txt"]
        and "VERSION = $$cat($$VERSION_FILE, lines)" in source["qView.pro"]
        and "QStringLiteral(VERSION_STRING)" in test_source
    )
    apng_fixture_contract = contains_all(
        test_source,
        ("FOVELLE_APNG_FIXTURE", "tinyAnimatedPngBase64", "createBase64Image(fallbackDirectory"),
    )
    add_check(
        checks,
        "ST-22-VERSION-CI",
        version_contract and apng_fixture_contract,
        {
            "version_contract": version_contract,
            "apng_fixture_contract": apng_fixture_contract,
            "old_version_absent": "0.1.0" not in source["CMakeLists.txt"] + source["qView.pro"] + test_source,
        },
        f"the released version is read from VERSION ({project_version}) and APNG tests have a hermetic fallback instead of requiring a developer-machine path",
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
    actionmanager_cpp = source["src/actionmanager.cpp"]
    issue_864_contract = contains_all(
        window_cpp + mainwindow_header + actionmanager_cpp + test_source,
        (
            "openWithFutureWatcher.isRunning()",
            "openWithFutureWatcher.waitForFinished();",
            "openWithFutureFilePath",
            "[filePath]()",
            "openWithPopulationPending",
            "testOpenWithWorkerTeardownContract",
        ),
    ) and "aboutToShow" in actionmanager_cpp and "requestPopulateOpenWithMenu" in actionmanager_cpp
    add_check(
        checks,
        "ST-12",
        issue_864_contract,
        {
            "lazy_menu_trigger": "aboutToShow" in actionmanager_cpp and "requestPopulateOpenWithMenu" in actionmanager_cpp,
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

    theme_cpp = (
        source["src/qvcocoafunctions.mm"]
        + source["src/qvcocoafunctions.h"]
        + source["src/qvnamespace.h"]
        + source["src/qvgraphicsview.cpp"]
        + window_cpp
    )
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
            'viewportBackgroundColor',
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
            "viewport_colors": (
                "viewportBackgroundColor" in source["src/qvnamespace.h"]
                and 'QColor("#212121")' in source["src/qvnamespace.h"]
                and 'QColor("#969696")' in source["src/qvnamespace.h"]
            ),
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

    navigation_contract = contains_all(
        window_cpp + mainwindow_header + test_source,
        (
            "eventFilter(QObject *watched, QEvent *event)",
            "sampledContentBrightness",
            "sampleDisplayedImageBrightness",
            "QGraphicsOpacityEffect",
            "NavigationButtonAnimationDuration",
            "NavigationButtonMinimumWindowWidth",
            "navigationEdgeWidth",
            "setDarkBackground",
            "setNavigationButtonVisible",
            "testNavigationButtonSizingAndNoDelay",
            "testNavigationButtonsUseActualContentContrast",
            "testNavigationButtonsFadeTransition",
            "testNavigationButtonsClickSwitchesFiles",
        ),
    )
    add_check(
        checks,
        "ST-21",
        navigation_contract and "setToolTip" not in window_cpp,
        {
            "mouse_filter": "eventFilter(QObject *watched, QEvent *event)" in mainwindow_header,
            "actual_content_sample": "sampleDisplayedImageBrightness" in window_cpp,
            "per_side_style": "sampledContentBrightness" in window_cpp and "setDarkBackground" in window_cpp,
            "fade_effect": "QGraphicsOpacityEffect" in window_cpp and "NavigationButtonAnimationDuration" in window_cpp,
            "deterministic_tests": all(marker in test_source for marker in (
                "testNavigationEdgeActivationExcludesTitlebar",
                "testNavigationButtonSizingAndNoDelay",
                "testNavigationButtonsUseActualContentContrast",
            "testNavigationButtonsFadeTransition",
            "testNavigationButtonsClickSwitchesFiles",
            )),
        },
        "the navigation feature observes pointer position in content space, samples each button's underlying pixels, and animates visibility with deterministic tests",
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

    release_workflow = source[".github/workflows/release.yml"]
    release_script = source["dist/scripts/package-macos-release.sh"]
    release_syntax = command("bash", "-n", str(repo / "dist/scripts/package-macos-release.sh"), cwd=repo)
    release_contract = contains_all(
        release_workflow + release_script,
        (
            'CMAKE_OSX_ARCHITECTURES="x86_64;arm64"',
            "macOS-universal.zip",
            "macdeployqt",
            "security import",
            "-A",
            "-t cert",
            "-T /usr/bin/codesign",
            "security list-keychain -d user -s",
            "security find-key -s -t private",
            "security set-key-partition-list",
            "if ! security set-key-partition-list",
            '-l "$SIGNING_IDENTITY"',
            "continuing with the imported codesign ACL",
            '--keychain "$KEYCHAIN_PATH"',
            "Developer ID Application:",
            "xcrun notarytool submit",
            "xcrun stapler staple",
            "spctl --assess --type execute",
            "lipo -archs",
        ),
    )
    add_check(
        checks,
        "ST-19",
        release_contract
        and release_syntax.returncode == 0
        and "codesign --sign -" not in release_script,
        {
            "universal_architecture": 'CMAKE_OSX_ARCHITECTURES="x86_64;arm64"' in release_workflow and "lipo -archs" in release_script,
            "dependency_deployment": "macdeployqt" in release_script,
            "developer_id_signing": "Developer ID Application:" in release_script and "codesign --sign -" not in release_script,
            "ephemeral_keychain_access": "-A" in release_script and '--keychain "$KEYCHAIN_PATH"' in release_script,
            "codesign_private_key_acl": "-T /usr/bin/codesign" in release_script,
            "keychain_search_list": "security list-keychain -d user -s" in release_script,
            "private_signing_key_preflight": "security find-key -s -t private" in release_script,
            "identity_scoped_partition_update": "security set-key-partition-list" in release_script and '-l "$SIGNING_IDENTITY"' in release_script,
            "partition_update_is_best_effort": "if ! security set-key-partition-list" in release_script and "continuing with the imported codesign ACL" in release_script,
            "notarization_and_gatekeeper": all(marker in release_script for marker in ("xcrun notarytool submit", "xcrun stapler staple", "spctl --assess --type execute")),
            "shell_syntax_return_code": release_syntax.returncode,
        },
        "the Release workflow has a syntactically valid Universal build and signed/notarized artifact verification path",
    )

    timeout_contract = contains_all(
        release_workflow + release_script,
        (
            'NOTARIZATION_TIMEOUT="${NOTARIZATION_TIMEOUT:-30m}"',
            "validate_notarization_timeout",
            'if ! xcrun notarytool submit',
            '--timeout "$NOTARIZATION_TIMEOUT"',
            "--verbose",
            "no release artifact was created",
            "timeout-minutes: 60",
            "timeout-minutes: 45",
            'NOTARIZATION_TIMEOUT: "30m"',
        ),
    )
    add_check(
        checks,
        "ST-20",
        timeout_contract and release_syntax.returncode == 0,
        {
            "finite_notarization_timeout": 'NOTARIZATION_TIMEOUT="${NOTARIZATION_TIMEOUT:-30m}"' in release_script and '--timeout "$NOTARIZATION_TIMEOUT"' in release_script,
            "duration_validation": "validate_notarization_timeout" in release_script,
            "observable_notary_output": "--verbose" in release_script,
            "fail_closed_timeout": "no release artifact was created" in release_script,
            "workflow_job_timeout": "timeout-minutes: 60" in release_workflow,
            "workflow_step_timeout": "timeout-minutes: 45" in release_workflow,
            "workflow_notarization_timeout": 'NOTARIZATION_TIMEOUT: "30m"' in release_workflow,
            "shell_syntax_return_code": release_syntax.returncode,
        },
        "the notarization wait is bounded, observable, validated, and protected by workflow timeouts",
    )

    result = {"kind": "static", "repo": str(repo), "checks": checks, "passed": all(item["pass"] for item in checks)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
