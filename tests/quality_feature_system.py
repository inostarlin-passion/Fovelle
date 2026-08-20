#!/usr/bin/env python3
"""Run the new image-policy and Issue #864 cases in a separate Cocoa process."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path


CASES = (
    ("TC-IMG-TIFF", "ImageLoaderTests", "testImageLoaderLoadsTiffWithImageIO"),
    ("TC-RAW-TYPE-DETECTION", "ImageLoaderTests", "testImageIOUsesContentTypeInsteadOfFilenameExtension"),
    ("TC-FMT-TIFF-RAW", "FeatureTests", "testSettingsFormatsIncludeTiffAndSystemRawFormats"),
    ("TC-IMG-SMALL-SETTING", "FeatureTests", "testSmallImageOneToOneSettingIsExposedInImageOptions"),
    ("TC-ISSUE-864-OPENWITH-TEARDOWN", "FeatureTests", "testOpenWithWorkerTeardownContract"),
    ("TC-APP-VERSION", "FeatureTests", "testApplicationVersionIsCurrent"),
    ("TC-IMG-SMALL-POLICY", "GraphicsViewTests", "testSmallImageOneToOnePolicyUsesViewportAndWindowMode"),
    ("TC-IMG-SMALL-OPEN-BROWSE", "GraphicsViewTests", "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages"),
    ("TC-IMAGE-CENTER-WITH-SCROLLBARS", "GraphicsViewTests", "testImageIsCenteredAfterOpeningWithScrollBars"),
    ("TC-WHEEL-ZOOM-SCROLLBAR-REGRESSION", "GraphicsViewTests", "testTouchpadWheelRespectsConfiguredZoomWithScrollBars"),
    ("TC-LAYOUT-OPEN-FIT", "GraphicsViewTests", "testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling"),
    ("TC-LAYOUT-ROTATED-FIT", "GraphicsViewTests", "testRotatedZoomToFitUsesUnobscuredViewport"),
    ("TC-LAYOUT-ZOOM-SCROLLBAR-THRESHOLD", "GraphicsViewTests", "testZoomAcrossScrollbarThresholdKeepsViewportCenterStable"),
    ("TC-GESTURE-TOUCHPAD-PAN", "GraphicsViewTests", "testTouchpadPanUsesPixelsWithoutChangingZoom"),
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
    ("TC-THEME-COLORS", "WindowBehaviorTests", "testThemeAppliesNativeAppearanceAndViewportBackground"),
    ("TC-THEME-CHECKERBOARD", "WindowBehaviorTests", "testCheckerboardOverridesThemeAndRestoresBackground"),
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
    args = parser.parse_args()

    binary = args.binary.resolve()
    base_command = [str(binary), "-o", "-,txt"]
    started = time.perf_counter()
    case_runs: list[dict] = []
    output_parts: list[str] = []
    timed_out = False
    for identifier, suite, test_name in CASES:
        command = base_command + [test_name]
        try:
            result = subprocess.run(
                command,
                text=True,
                capture_output=True,
                env={
                    **os.environ,
                    "QT_QPA_PLATFORM": "cocoa",
                    "QT_FATAL_WARNINGS": "1",
                    "FOVELLE_TEST_SUITE": suite,
                },
                timeout=15,
                check=False,
            )
            case_output = result.stdout + result.stderr
            case_timed_out = False
        except subprocess.TimeoutExpired as error:
            stdout = error.stdout or ""
            stderr = error.stderr or ""
            if isinstance(stdout, bytes):
                stdout = stdout.decode(errors="replace")
            if isinstance(stderr, bytes):
                stderr = stderr.decode(errors="replace")
            case_output = stdout + stderr
            result = None
            case_timed_out = True
        output_parts.append(case_output)
        timed_out = timed_out or case_timed_out
        case_runs.append({
            "id": identifier,
            "test": f"{suite}::{test_name}",
            "command": command,
            "return_code": result.returncode if result else None,
            "timed_out": case_timed_out,
            "status": "passed"
            if result and result.returncode == 0 and re.search(rf"PASS\s+: {re.escape(suite)}::{re.escape(test_name)}\(\)", case_output)
            else "failed",
            "output": case_output[-4000:],
        })

    output = "\n".join(output_parts)
    cases = [
        {key: value for key, value in case_run.items() if key in {"id", "test", "status"}}
        for case_run in case_runs
    ]
    issue_864_safety_observations = {
        "no_sigabrt": "SIGABRT" not in output,
        "no_qpixmap_after_teardown_error": "QPixmap: Must construct a QGuiApplication" not in output,
        "teardown_case_completed": "testOpenWithWorkerTeardownContract" in output,
    }
    performance_match = re.search(
        r"GESTURE_PERF average_ms=(?P<average>[0-9.]+) p99_ms=(?P<p99>[0-9.]+) max_ms=(?P<maximum>[0-9.]+) throughput_events_per_second=(?P<throughput>[0-9.]+) count=(?P<count>[0-9]+)",
        output,
    )
    performance_observations = {
        "average_ms": float(performance_match.group("average")) if performance_match else None,
        "p99_ms": float(performance_match.group("p99")) if performance_match else None,
        "maximum_ms": float(performance_match.group("maximum")) if performance_match else None,
        "throughput_events_per_second": float(performance_match.group("throughput")) if performance_match else None,
        "event_count": int(performance_match.group("count")) if performance_match else None,
        "contract": bool(performance_match),
    }
    record = {
        "kind": "system-feature",
        "command": base_command,
        "case_runs": case_runs,
        "binary": str(binary),
        "platform": "macOS Cocoa",
        "elapsed_seconds": time.perf_counter() - started,
        "return_code": 0 if all(case_run["return_code"] == 0 for case_run in case_runs) else 1,
        "timed_out": timed_out,
        "cases": cases,
        "observations": {
            "qt_platform": "cocoa",
            "issue_864_safety": issue_864_safety_observations,
            "opening_and_browsing_case_present": "testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages" in output,
            "native_gesture_performance": performance_observations,
        },
        "passed": bool(
            all(case_run["return_code"] == 0 for case_run in case_runs)
            and not timed_out
            and all(case["status"] == "passed" for case in cases)
            and all(issue_864_safety_observations.values())
            and performance_observations["contract"]
        ),
        "output_tail": output[-16000:],
        "limitations": [
            "The test runs the real compiled application test binary under Cocoa, but the deterministic image fixtures are created by the test process.",
            "A separate repeated-launch probe supplies P99 and resource measurements; this record is the functional system gate for the new cases.",
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
