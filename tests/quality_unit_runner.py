#!/usr/bin/env python3
"""Run the deterministic Qt unit suites and write complete audit evidence."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path


EXPECTED_SUITES = (
    "ImageLoaderTests",
    "FeatureTests",
    "GraphicsViewTests",
    "ApplicationEventTests",
    "ImageCoreAndMovieTests",
    "WindowBehaviorTests",
)

EXPECTED_CASES = (
    ("TC-IMG-WEBP", "ImageLoaderTests", "testImageLoaderLoadsWebpWithImageIOFallback"),
    ("TC-IMG-AVIF", "ImageLoaderTests", "testImageLoaderLoadsAvifWithImageIOFallback"),
    ("TC-ORI-WEBP", "ImageLoaderTests", "testImageLoaderAppliesWebpOrientation"),
    ("TC-ORI-AVIF", "ImageLoaderTests", "testImageLoaderAppliesAvifOrientation"),
    ("TC-IMG-TIFF", "ImageLoaderTests", "testImageLoaderLoadsTiffWithImageIO"),
    ("TC-RAW-TYPE-DETECTION", "ImageLoaderTests", "testImageIOUsesContentTypeInsteadOfFilenameExtension"),
    ("TC-IMG-FULL-RES", "ImageLoaderTests", "testImageLoaderPreservesSourceResolutionForZoom"),
    ("TC-WIN-ICON", "FeatureTests", "testWindowIconIsCleared"),
    ("TC-TITLEBAR-DOCUMENT-ICON", "FeatureTests", "testTitlebarDocumentProxyIsClearedForLoadedFile"),
    ("TC-TITLEBAR-IDEMPOTENCE", "FeatureTests", "testTitlebarIconClearingIsIdempotent"),
    ("TC-FMT-SETTINGS", "FeatureTests", "testSettingsFormatsIncludeNativeImageFormats"),
    ("TC-IMG-SMALL-SETTING", "FeatureTests", "testSmallImageOneToOneSettingIsExposedInImageOptions"),
    ("TC-SETTINGS-GROUPS-DEFAULTS", "FeatureTests", "testSettingsGeneralGroupsAndDefaults"),
    ("TC-ISSUE-864-OPENWITH-TEARDOWN", "FeatureTests", "testOpenWithWorkerTeardownContract"),
    ("TC-APP-VERSION", "FeatureTests", "testApplicationVersionIsCurrent"),
    ("TC-ZOOM-MOUSE", "GraphicsViewTests", "testMouseWheelUsesOneDiscreteStep"),
    ("TC-ZOOM-TOUCHPAD", "GraphicsViewTests", "testTouchpadWheelCanUseFractionalSteps"),
    ("TC-IMAGE-CENTER-WITH-SCROLLBARS", "GraphicsViewTests", "testImageIsCenteredAfterOpeningWithScrollBars"),
    ("TC-WHEEL-ZOOM-SCROLLBAR-REGRESSION", "GraphicsViewTests", "testTouchpadWheelRespectsConfiguredZoomWithScrollBars"),
    ("TC-LAYOUT-OPEN-FIT", "GraphicsViewTests", "testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling"),
    ("TC-LAYOUT-ROTATED-FIT", "GraphicsViewTests", "testRotatedZoomToFitUsesUnobscuredViewport"),
    ("TC-LAYOUT-ZOOM-SCROLLBAR-THRESHOLD", "GraphicsViewTests", "testZoomAcrossScrollbarThresholdKeepsViewportCenterStable"),
    ("TC-GESTURE-TOUCHPAD-PAN", "GraphicsViewTests", "testTouchpadPanUsesPixelsWithoutChangingZoom"),
    ("TC-ZOOM-FULLSCREEN", "GraphicsViewTests", "testFitZoomSurvivesInverseWheelStepsAndFullscreenResize"),
    ("TC-ZOOM-MANUAL-RESIZE", "GraphicsViewTests", "testManualZoomRemainsManualAcrossResize"),
    ("TC-IMG-SMALL-POLICY", "GraphicsViewTests", "testSmallImageOneToOnePolicyUsesViewportAndWindowMode"),
    ("TC-IMG-SMALL-OPEN-BROWSE", "GraphicsViewTests", "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages"),
    ("TC-GESTURE-NATIVE-ZOOM", "GraphicsViewTests", "testNativePinchZoomChangesScaleAtGesturePosition"),
    ("TC-GESTURE-NATIVE-PAN", "GraphicsViewTests", "testNativePanChangesViewport"),
    ("TC-SCROLLBAR-AXES", "GraphicsViewTests", "testScrollBarsFollowImageOverflowAxes"),
    ("TC-SCROLLBAR-THEME", "GraphicsViewTests", "testScrollBarsMatchTheme"),
    ("TC-GESTURE-PERF", "GraphicsViewTests", "testNativeGestureResponsePerformance"),
    ("TC-APNG-PLAY", "ImageCoreAndMovieTests", "testAnimatedPngPlaysBeyondFirstFrame"),
    ("TC-FS-DEFAULT", "WindowBehaviorTests", "testFullscreenDefaultShortcutIsEnterAndConfigurable"),
    ("TC-FS-NO-BYPASS", "WindowBehaviorTests", "testEnterDoesNotBypassClearedFullscreenShortcut"),
    ("TC-FS-CONFIGURED", "WindowBehaviorTests", "testConfiguredFullscreenShortcutStillWorks"),
    ("TC-TITLE-PRACTICAL", "WindowBehaviorTests", "testPracticalTitlebarTextUsesFilenameAndSequence"),
    ("TC-TITLE-DEFAULT", "WindowBehaviorTests", "testDefaultTitlebarTextIsPractical"),
    ("TC-TITLE-VERBOSE", "WindowBehaviorTests", "testVerboseTitlebarTextUsesAllRequestedFields"),
    ("TC-THEME-SETTINGS", "WindowBehaviorTests", "testThemeSettingsReplaceRemovedColorControls"),
    ("TC-SETTINGS-NATIVE-TOOLBAR", "WindowBehaviorTests", "testSettingsDialogUsesNativeTabContractAndImmediatePersistence"),
    ("TC-OPEN-URL-THEME", "WindowBehaviorTests", "testOpenUrlDialogFollowsSelectedTheme"),
    ("TC-THEME-COLORS", "WindowBehaviorTests", "testThemeAppliesNativeAppearanceAndViewportBackground"),
    ("TC-THEME-CHECKERBOARD", "WindowBehaviorTests", "testCheckerboardOverridesThemeAndRestoresBackground"),
    ("TC-SETTINGS-ALL-LANGUAGES-TABS", "WindowBehaviorTests", "testSettingsEveryTabFitsEveryLanguage"),
    ("TC-NAV-EDGE", "WindowBehaviorTests", "testNavigationEdgeActivationExcludesTitlebar"),
    ("TC-NAV-SIZE", "WindowBehaviorTests", "testNavigationButtonSizingAndNoDelay"),
    ("TC-NAV-CONTRAST", "WindowBehaviorTests", "testNavigationButtonsUseActualContentContrast"),
    ("TC-NAV-TRANSITION", "WindowBehaviorTests", "testNavigationButtonsFadeTransition"),
    ("TC-NAV-CLICK", "WindowBehaviorTests", "testNavigationButtonsClickSwitchesFiles"),
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scale-factor", default=None)
    args = parser.parse_args()

    command = [str(args.binary.resolve()), "-o", "-,txt"]
    timeout_seconds = 90
    environment = {"QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1"}
    if args.scale_factor is not None:
        environment["QT_SCALE_FACTOR"] = str(args.scale_factor)
    started = time.perf_counter()
    try:
        result = subprocess.run(
            command,
            text=True,
            capture_output=True,
            env={**os.environ, **environment},
            timeout=timeout_seconds,
            check=False,
        )
        output = result.stdout + result.stderr
        timed_out = False
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        result = None
        output = stdout + stderr
        timed_out = True
    totals = []
    for suite in EXPECTED_SUITES:
        start_marker = f"Start testing of {suite}"
        end_marker = f"Finished testing of {suite}"
        start_index = output.find(start_marker)
        end_index = output.find(end_marker, start_index)
        section = output[start_index:end_index] if start_index >= 0 and end_index >= 0 else ""
        match = re.search(
            r"Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted", section
        )
        if match:
            totals.append({
                "passed": int(match.group(1)),
                "failed": int(match.group(2)),
                "skipped": int(match.group(3)),
                "blacklisted": int(match.group(4)),
            })
    suites = [suite for suite in EXPECTED_SUITES if f"Start testing of {suite}" in output]
    cases = [
        {
            "id": identifier,
            "test": f"{suite}::{test_name}",
            "status": "passed" if f"PASS   : {suite}::{test_name}()" in output else "failed",
        }
        for identifier, suite, test_name in EXPECTED_CASES
    ]
    passed = (
        result
        and result.returncode == 0
        and not timed_out
        and suites == list(EXPECTED_SUITES)
        and len(totals) == len(EXPECTED_SUITES)
        and all(item["failed"] == 0 and item["skipped"] == 0 and item["blacklisted"] == 0 for item in totals)
        and all(item["status"] == "passed" for item in cases)
    )
    record = {
        "kind": "unit",
        "command": command,
        "binary": str(args.binary.resolve()),
        "environment": environment,
        "elapsed_seconds": time.perf_counter() - started,
        "timeout_seconds": timeout_seconds,
        "timed_out": timed_out,
        "return_code": result.returncode if result else None,
        "suites": suites,
        "totals": totals,
        "total_passed": sum(item["passed"] for item in totals),
        "total_failed": sum(item["failed"] for item in totals),
        "total_skipped": sum(item["skipped"] for item in totals),
        "cases": cases,
        "output": output,
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
