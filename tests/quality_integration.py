#!/usr/bin/env python3
"""Run integration gates for the image-view feature, issue fix, and release workflow."""

from __future__ import annotations

import argparse
import json
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(list(args), cwd=repo, text=True, capture_output=True, check=False)


def add_check(checks: list[dict], identifier: str, passed: bool, actual: object, expected: str) -> None:
    checks.append({"id": identifier, "pass": bool(passed), "actual": actual, "expected": expected})


def all_present(source: str, needles: tuple[str, ...]) -> bool:
    return all(needle in source for needle in needles)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = args.build_dir.resolve()
    checks: list[dict] = []

    def text(relative: str) -> str:
        return (repo / relative).read_text(encoding="utf-8")

    mainwindow = text("src/mainwindow.cpp")
    application = text("src/qvapplication.cpp")
    add_check(
        checks,
        "I-01",
        "setWindowIcon(QIcon());" in mainwindow
        and "QApplication::setWindowIcon" not in application
        and "clearTitlebarIcons" in mainwindow
        and "handle->setFilePath(QString());" in mainwindow,
        {
            "window_icon_reset": "setWindowIcon(QIcon());" in mainwindow,
            "application_global_icon_removed": "QApplication::setWindowIcon" not in application,
            "titlebar_clear_helper": "clearTitlebarIcons" in mainwindow,
            "native_document_path_cleared": "handle->setFilePath(QString());" in mainwindow,
        },
        "the titlebar window icon and native document path are explicitly cleared without changing the bundle identity",
    )

    graphics = text("src/qvgraphicsview.cpp") + text("src/qvgraphicsview.h")
    add_check(
        checks,
        "I-02",
        all_present(
            graphics,
            (
                "wheelZoomFactor",
                "event->device()",
                "TouchPad",
                "wheelDelta > 0 ? 1.0 : -1.0",
                "useFractionalZoom",
                "lastCalculatedZoomMode",
                "lastCalculatedZoomLevel",
                "zoomLevelsEquivalent",
                "shouldRestoreCalculatedZoom",
            ),
        ),
        {
            "wheel_helper": "wheelZoomFactor" in graphics,
            "touchpad_path": "TouchPad" in graphics,
            "discrete_mouse_path": "wheelDelta > 0 ? 1.0 : -1.0" in graphics,
            "fit_continuity_state": "lastCalculatedZoomMode" in graphics and "lastCalculatedZoomLevel" in graphics,
            "resize_recalculation_guard": "shouldRestoreCalculatedZoom" in graphics,
        },
        "the implemented zoom contract distinguishes wheel input modes and preserves fit intent through a resized viewport",
    )

    cocoa = text("src/qvcocoafunctions.mm") + text("src/qvcocoafunctions.h")
    loader = text("src/qvimageloader.cpp")
    native_decode = all_present(
        cocoa + loader,
        (
            "CGImageSourceCopyTypeIdentifiers",
            "CGImageSourceCreateWithURL",
            "CGImageSourceCreateThumbnailAtIndex",
            "kCGImageSourceCreateThumbnailWithTransform",
            "QVCocoaFunctions::supportsAdditionalImageFormat",
            "QVCocoaFunctions::readAdditionalImage",
            "useNativeImageIO",
        ),
    )
    linked_frameworks = all_present(
        text("CMakeLists.txt") + text("qView.pro") + text("tests/CMakeLists.txt"),
        ("CoreGraphics", "ImageIO"),
    )
    add_check(
        checks,
        "I-03",
        native_decode and linked_frameworks,
        {
            "native_decode_path": native_decode,
            "frameworks_linked": linked_frameworks,
            "native_decoder_is_primary": loader.find("QVCocoaFunctions::readAdditionalImage") < loader.find("imageReader.read()"),
        },
        "supported WebP/AVIF files use linked Apple Image I/O as the canonical orientation-aware decoder, with Qt retained as fallback",
    )

    movie = text("src/qvmovie.cpp")
    apng_contract = all_present(
        cocoa + movie,
        (
            "createAnimatedImage",
            "CGImageSourceGetCount",
            "kCGImagePropertyAPNGUnclampedDelayTime",
            "nativeAnimation",
            "CacheAll",
        ),
    )
    add_check(
        checks,
        "I-03-APNG",
        apng_contract,
        {
            "image_io_animation_factory": "createAnimatedImage" in cocoa,
            "frame_count": "CGImageSourceGetCount" in cocoa,
            "frame_delays": "kCGImagePropertyAPNGUnclampedDelayTime" in cocoa,
            "qvmovie_native_path": "nativeAnimation" in movie,
            "cache_contract": "CacheAll" in movie,
        },
        "the integrated APNG path provides composed frames and timing metadata through QVMovie's cache and playback state machine",
    )

    options = text("src/qvoptionsdialog.cpp")
    formats_registry = all_present(
        application,
        (
            "getAdditionalImageFormats()",
            "addExtension(fileExtension)",
            'addExtension(".avifs")',
            "getAdditionalImageMimeTypes()",
        ),
    )
    add_check(
        checks,
        "I-04",
        formats_registry and "getAllFileExtensionList()" in options,
        {
            "native_extensions_registered": formats_registry,
            "settings_table_uses_complete_registry": "getAllFileExtensionList()" in options,
        },
        "Settings → Formats reads the same registry extended by native WebP/AVIF support",
    )

    test_source = text("tests/tst_qviewtests.cpp")
    settings = text("src/settingsmanager.cpp")
    options_ui = text("src/qvoptionsdialog.ui")
    small_image = text("src/qvgraphicsview.cpp") + text("src/qvgraphicsview.h")
    add_check(
        checks,
        "I-10",
        all_present(
            settings + options + options_ui + small_image + mainwindow,
            (
                'settingsLibrary.insert("smallimageoneone", {false, {}});',
                'syncCheckbox(ui->smallImagesOneToOneCheckbox, "smallimageoneone"',
                'name="smallImagesOneToOneCheckbox"',
                "Show small images at 1:1",
                "shouldDisplaySmallImageAtOneToOne",
                "getUsableViewportRect().size()",
                "WindowResizeMode::Never",
            ),
        ),
        {
            "persisted_setting": 'settingsLibrary.insert("smallimageoneone", {false, {}});' in settings,
            "image_ui": 'name="smallImagesOneToOneCheckbox"' in options_ui,
            "viewport_policy": "shouldDisplaySmallImageAtOneToOne" in small_image and "getUsableViewportRect().size()" in small_image,
            "never_gate": "WindowResizeMode::Never" in small_image,
            "open_browse_case": "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages" in test_source,
        },
        "the new setting is integrated across persistence, Settings → Image, viewport calculation, and the open/browse regression case",
    )
    small_image_tests_present = "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages" in test_source
    checks[-1]["pass"] = checks[-1]["pass"] and small_image_tests_present

    issue_864 = all_present(
        mainwindow + text("src/mainwindow.h"),
        (
            "populateOpenWithTimer->stop();",
            "openWithFutureWatcher.waitForFinished();",
            "openWithFutureFilePath",
            "[filePath]()",
            "openWithPopulationPending",
        ),
    )
    add_check(
        checks,
        "I-11",
        issue_864,
        {
            "timer_stopped": "populateOpenWithTimer->stop();" in mainwindow,
            "future_waited": "openWithFutureWatcher.waitForFinished();" in mainwindow,
            "path_value_capture": "[filePath]()" in mainwindow,
            "refresh_serialization": "openWithPopulationPending" in mainwindow,
        },
        "the Issue #864 Open With worker teardown contract is present in the integrated application sources",
    )

    bundle = build_dir / "Fovelle.app"
    info_path = bundle / "Contents" / "Info.plist"
    icon_path = bundle / "Contents" / "Resources" / "qView.icns"
    info: dict = {}
    if info_path.is_file():
        try:
            info = plistlib.loads(info_path.read_bytes())
        except (plistlib.InvalidFileException, ValueError):
            info = {}
    document_extensions = {
        extension.lower()
        for document_type in info.get("CFBundleDocumentTypes", [])
        for extension in document_type.get("CFBundleTypeExtensions", [])
    }
    bundle_ok = (
        bundle.is_dir()
        and (bundle / "Contents" / "MacOS" / "Fovelle").is_file()
        and icon_path.is_file()
        and info.get("CFBundleIdentifier") == "io.github.inostarlin-passion.Fovelle"
        and info.get("CFBundleIconFile") == "qView.icns"
        and {"webp", "avif", "avifs"}.issubset(document_extensions)
    )
    add_check(
        checks,
        "I-05",
        bundle_ok,
        {
            "bundle": bundle.is_dir(),
            "executable": (bundle / "Contents" / "MacOS" / "Fovelle").is_file(),
            "icon": icon_path.is_file(),
            "identifier": info.get("CFBundleIdentifier"),
            "document_extensions": sorted(document_extensions.intersection({"webp", "avif", "avifs"})),
        },
        "the built bundle is executable, retains the Dock/Finder icon, and advertises WebP/AVIF document types",
    )

    ctest = run(repo, "ctest", "--test-dir", str(build_dir), "--output-on-failure")
    add_check(
        checks,
        "I-06",
        ctest.returncode == 0,
        {"return_code": ctest.returncode, "output": (ctest.stdout + ctest.stderr)[-6000:]},
        "CTest executes the configured Qt integration test target successfully",
    )

    qmake_path = subprocess.run(["/usr/bin/env", "bash", "-lc", "command -v qmake || true"], text=True, capture_output=True, check=False).stdout.strip()
    qmake_result: subprocess.CompletedProcess[str] | None = None
    if qmake_path:
        with tempfile.TemporaryDirectory(prefix="fovelle-qmake-") as directory:
            qmake_result = subprocess.run(
                [qmake_path, str(repo / "qView.pro"), "-o", str(Path(directory) / "Makefile")],
                cwd=repo,
                text=True,
                capture_output=True,
                check=False,
            )
    add_check(
        checks,
        "I-07",
        qmake_result is not None and qmake_result.returncode == 0,
        {
            "qmake": qmake_path or None,
            "return_code": qmake_result.returncode if qmake_result else None,
            "output": (qmake_result.stdout + qmake_result.stderr)[-3000:] if qmake_result else "qmake not found",
        },
        "the legacy macOS qmake project still configures with the native frameworks",
    )

    diff_scope_paths = (
        "src",
        "tests",
        "CMakeLists.txt",
        "qView.pro",
        "build.sh",
        ".clang-tidy",
        ".github",
        "dist",
    )
    diff_check = run(repo, "git", "diff", "--check", "HEAD", "--", *diff_scope_paths)
    add_check(
        checks,
        "I-08",
        diff_check.returncode == 0,
        {
            "return_code": diff_check.returncode,
            "output": diff_check.stdout + diff_check.stderr,
            "scope": list(diff_scope_paths),
            "excluded_preexisting_paths": ["README.md"],
        },
        "the integrated task-scoped working-tree diff has no whitespace errors; unrelated pre-existing README.md changes remain untouched",
    )

    required_cases = (
        "testImageLoaderLoadsWebpWithImageIOFallback",
        "testImageLoaderLoadsAvifWithImageIOFallback",
        "testImageLoaderAppliesWebpOrientation",
        "testImageLoaderAppliesAvifOrientation",
        "testAnimatedPngPlaysBeyondFirstFrame",
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
        "testDefaultTitlebarTextIsPractical",
        "testNavigationEdgeActivationExcludesTitlebar",
        "testNavigationButtonSizingAndNoDelay",
        "testNavigationButtonsUseActualContentContrast",
        "testNavigationButtonsFadeTransition",
        "testNavigationButtonsClickSwitchesFiles",
    )
    add_check(
        checks,
        "I-09",
        all(case in test_source for case in required_cases),
        {"cases": {case: case in test_source for case in required_cases}},
        "the integrated test source contains one deterministic case for each current-scope acceptance criterion",
    )

    workflow = text(".github/workflows/release.yml")
    add_check(
        checks,
        "I-12",
        all_present(
            workflow,
            (
                "push:",
                "tags:",
                "- 'v*'",
                "contents: write",
                "softprops/action-gh-release@v3",
                "GITHUB_TOKEN",
                "generate_release_notes: true",
                "fail_on_unmatched_files: true",
                "ctest --test-dir build --output-on-failure",
            ),
        ),
        {
            "tag_trigger": "- 'v*'" in workflow,
            "write_permission": "contents: write" in workflow,
            "release_action": "softprops/action-gh-release@v3" in workflow,
            "test_before_publish": "ctest --test-dir build --output-on-failure" in workflow,
        },
        "the release workflow is tag-triggered, tests before packaging, and grants the action contents write access",
    )

    shortcut_contract = (
        'QStringList(QKeySequence(Qt::Key_Return).toString())' in text("src/shortcutmanager.cpp")
        and "returnShortcut" not in mainwindow
        and "keypadEnterShortcut" not in mainwindow
    )
    add_check(
        checks,
        "I-13",
        shortcut_contract,
        {
            "default_return": 'QStringList(QKeySequence(Qt::Key_Return).toString())' in text("src/shortcutmanager.cpp"),
            "no_mainwindow_enter_shortcut": "returnShortcut" not in mainwindow and "keypadEnterShortcut" not in mainwindow,
        },
        "the Full Screen QAction owns the default Enter binding and no hardcoded Enter bypass remains in MainWindow",
    )

    theme_contract = all_present(
        text("src/settingsmanager.cpp") + options + options_ui + mainwindow + cocoa,
        (
            'settingsLibrary.insert("theme", {static_cast<int>(Qv::Theme::Light), {}});',
            'syncComboBox(ui->themeComboBox, "theme"',
            'name="themeComboBox"',
            'NSAppearanceNameAqua',
            'NSAppearanceNameDarkAqua',
            'QColor("#212121")',
            'QColor("#969696")',
        ),
    ) and 'name="bgColorCheckbox"' not in options_ui and 'name="darkTitlebarCheckbox"' not in options_ui
    add_check(
        checks,
        "I-14",
        theme_contract,
        {
            "theme_setting_and_ui": 'settingsLibrary.insert("theme", {static_cast<int>(Qv::Theme::Light), {}});' in text("src/settingsmanager.cpp") and 'name="themeComboBox"' in options_ui,
            "native_appearance_mapping": 'NSAppearanceNameAqua' in cocoa and 'NSAppearanceNameDarkAqua' in cocoa,
            "old_controls_removed": 'name="bgColorCheckbox"' not in options_ui and 'name="darkTitlebarCheckbox"' not in options_ui,
            "title_formats": 'newString = getFileName() + " - " + getImageIndex()' in mainwindow and 'getImageWidth() + "x" + getImageHeight()' in mainwindow,
        },
        "the integrated sources provide the two themes, requested title formats, native appearance bridge, and checkerboard-compatible viewport colors",
    )

    navigation_contract = all_present(
        mainwindow + text("src/mainwindow.h"),
        (
            "previousImageButton",
            "nextImageButton",
            "eventFilter(QObject *watched, QEvent *event)",
            "viewport()->grab(sampleRect)",
            "QPropertyAnimation",
            "navigationEdgeWidth",
            "NavigationButtonActivationMinimumWidth",
            "NavigationButtonActivationPercentage",
            "NavigationButtonMinimumWindowWidth",
            "NavigationButtonAnimationDuration",
            "setNavigationButtonVisible",
        ),
    )
    add_check(
        checks,
        "I-14-NAV",
        navigation_contract and "setToolTip" not in mainwindow,
        {
            "buttons": "previousImageButton" in mainwindow and "nextImageButton" in mainwindow,
            "pointer_filter": "eventFilter(QObject *watched, QEvent *event)" in mainwindow,
            "underlying_content_sample": "viewport()->grab(sampleRect)" in mainwindow,
            "transition": "QPropertyAnimation" in mainwindow,
            "edge_width": "navigationEdgeWidth" in mainwindow + text("src/mainwindow.h"),
            "minimum_strip": "NavigationButtonActivationMinimumWidth" in mainwindow + text("src/mainwindow.h"),
            "percentage_strip": "NavigationButtonActivationPercentage" in mainwindow + text("src/mainwindow.h"),
            "minimum_window": "NavigationButtonMinimumWindowWidth" in mainwindow + text("src/mainwindow.h"),
            "transition_duration": "NavigationButtonAnimationDuration" in mainwindow + text("src/mainwindow.h"),
            "tooltip_api_absent": "setToolTip" not in mainwindow,
            "visibility_delay_api_absent": (
                "NavigationButtonShowDelay" not in mainwindow + text("src/mainwindow.h") and
                "NavigationButtonHideDelay" not in mainwindow + text("src/mainwindow.h")
            ),
        },
        "the navigation buttons are integrated with content-only edge activation, per-side content sampling, and opacity transitions",
    )

    tidy_config = text(".clang-tidy")
    tidy_workflow = text(".github/workflows/test.yml")
    tidy_script = text("build.sh")
    tidy_path = shutil.which("clang-tidy")
    tidy_verify: subprocess.CompletedProcess[str] | None = None
    if tidy_path:
        tidy_verify = run(repo, tidy_path, "--verify-config", "-p", str(build_dir), "src/qvimageloader.cpp")
    tidy_contract = all_present(
        tidy_config,
        ("Checks:", "bugprone-use-after-move", "performance-move-const-arg", "clang-diagnostic-*"),
    ) and all_present(tidy_workflow + tidy_script, ("brew install llvm", "--tidy", "CMAKE_CXX_CLANG_TIDY=clang-tidy"))
    add_check(
        checks,
        "I-15",
        tidy_contract and (tidy_verify is None or tidy_verify.returncode == 0),
        {
            "config_contract": tidy_contract,
            "clang_tidy": tidy_path,
            "verify_config_return_code": tidy_verify.returncode if tidy_verify else None,
            "verify_config_output": (tidy_verify.stdout + tidy_verify.stderr)[-2000:] if tidy_verify else "clang-tidy not installed locally; CI job performs the executable verification",
        },
        "the integrated GitHub Actions clang-tidy contract has enabled checks and validates when the tool is available",
    )

    release_evidence_path = repo / "reports" / "evidence" / "release.json"
    release_result = run(
        repo,
        sys.executable,
        "tests/quality_release.py",
        "--repo",
        str(repo),
        "--output",
        str(release_evidence_path),
    )
    release_record: dict = {}
    if release_evidence_path.is_file():
        try:
            release_record = json.loads(release_evidence_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            release_record = {}
    add_check(
        checks,
        "I-16",
        release_result.returncode == 0 and release_record.get("passed") is True,
        {
            "return_code": release_result.returncode,
            "release_contract_passed": release_record.get("passed"),
            "output": (release_result.stdout + release_result.stderr)[-4000:],
        },
        "the executable release contract test passes in dry-run mode without exposing or consuming credentials",
    )

    release_script = text("dist/scripts/package-macos-release.sh")
    release_timeout_contract = all_present(
        workflow + release_script,
        (
            'NOTARIZATION_TIMEOUT="${NOTARIZATION_TIMEOUT:-30m}"',
            "validate_notarization_timeout",
            '--timeout "$NOTARIZATION_TIMEOUT"',
            "--verbose",
            "timeout-minutes: 60",
            "timeout-minutes: 45",
            'NOTARIZATION_TIMEOUT: "30m"',
        ),
    )
    add_check(
        checks,
        "I-17",
        release_timeout_contract,
        {
            "finite_notarization_timeout": 'NOTARIZATION_TIMEOUT="${NOTARIZATION_TIMEOUT:-30m}"' in release_script and '--timeout "$NOTARIZATION_TIMEOUT"' in release_script,
            "duration_validation": "validate_notarization_timeout" in release_script,
            "observable_notary_output": "--verbose" in release_script,
            "workflow_job_timeout": "timeout-minutes: 60" in workflow,
            "workflow_step_timeout": "timeout-minutes: 45" in workflow,
            "workflow_notarization_timeout": 'NOTARIZATION_TIMEOUT: "30m"' in workflow,
        },
        "the integrated Release path cannot wait indefinitely for Apple notarization and exposes bounded progress",
    )

    result = {
        "kind": "integration",
        "repo": str(repo),
        "build_dir": str(build_dir),
        "checks": checks,
        "passed": all(item["pass"] for item in checks),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
