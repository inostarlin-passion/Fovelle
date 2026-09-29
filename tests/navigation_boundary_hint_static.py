#!/usr/bin/env python3
"""Static acceptance checks for image-boundary hints and focus-safe rendering."""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def check(identifier: str, passed: bool, detail: str) -> bool:
    print(f"{'PASS' if passed else 'FAIL'} {identifier}: {detail}")
    return passed


def main() -> int:
    qv_view = read("src/qvgraphicsview.cpp")
    qv_view_h = read("src/qvgraphicsview.h")
    mainwindow = read("src/mainwindow.cpp")
    mainwindow_h = read("src/mainwindow.h")
    cocoa = read("src/qvcocoafunctions.mm")
    cocoa_h = read("src/qvcocoafunctions.h")
    test_cpp = read("tests/tst_qviewtests.cpp")

    cases: list[bool] = []
    boundary_gate = (
        "if (result.reachedEnd)" in qv_view
        and "reportNavigationBoundary" in qv_view
        and "navigationBoundaryReached(mode)" in qv_view
        and "mode == Qv::GoToFileMode::Previous" in qv_view
        and "mode == Qv::GoToFileMode::Next" in qv_view
        and 'tr("No previous image")' in mainwindow
        and 'tr("No next image")' in mainwindow
        and "navigationBoundaryReached" in mainwindow
    )
    cases.append(check(
        "AC-NAV-HINT-BOUNDARY",
        boundary_gate and "testNavigationBoundaryHintFeedbackAndAppearance" in test_cpp,
        "Previous/Next messages are emitted only for a rejected boundary request and have a dynamic test",
    ))

    expected_translations = {
        "qview_zh_Hans.ts": ("没有上一张图片", "没有下一张图片"),
        "qview_zh_Hant.ts": ("沒有上一張圖片", "沒有下一張圖片"),
        "qview_es.ts": ("No hay imagen anterior", "No hay imagen siguiente"),
        "qview_ja.ts": ("前の画像はありません", "次の画像はありません"),
    }
    localization_ok = True
    for filename, expected in expected_translations.items():
        catalog = ET.parse(ROOT / "i18n" / filename).getroot()
        messages = {
            message.findtext("source"): message.findtext("translation")
            for context in catalog.findall("context")
            if context.findtext("name") == "MainWindow"
            for message in context.findall("message")
        }
        localization_ok &= (
            messages.get("No previous image") == expected[0]
            and messages.get("No next image") == expected[1]
        )
    cases.append(check(
        "AC-NAV-HINT-LOCALIZED",
        localization_ok and "QAccessible::Alert" in mainwindow,
        "All four shipped non-English catalogs translate both strings; the accessible alert uses the same text",
    ))

    appearance = (
        "QVCocoaFunctions::resolvedTheme(configuredTheme)" in mainwindow
        and 'QColor(242, 242, 242, 238)' in mainwindow
        and 'QColor(34, 34, 34, 232)' in mainwindow
        and "NavigationBoundaryHintAnimationDuration = 180" in mainwindow_h
        and "NavigationBoundaryHintDisplayDuration = 4000" in mainwindow_h
        and "viewport->height() - hintSize.height() - 24" in mainwindow
        and "setHDRBoundaryHintOverlay" in qv_view
        and "boundaryHintLayer" in cocoa
        and "setBoundaryHintOverlay" in cocoa_h
        and "hintAppearance" in test_cpp
    )
    cases.append(check(
        "AC-NAV-HINT-APPEARANCE-ANIMATION",
        appearance and "testNavigationBoundaryHintFeedbackAndAppearance" in test_cpp,
        "Light/Dark theme, bottom placement, 180 ms fades, four-second dwell, and native HDR overlay are covered",
    ))

    focus = (
        "hdrRenderer->setPresentationActive(active, true);" in qv_view
        and "Keep the aligned SDR proxy underneath the HDR surface throughout the" in qv_view
        and "Both directions use the renderer's same 450 ms opacity curve." in qv_view
        and "if (active != hdrPresentationActive)" in qv_view
        and "QEvent::WindowDeactivate" in mainwindow
        and "setHDRPresentationActive(false)" in mainwindow
        and "testViewportImageRemainsSharpAfterFocusLoss" in test_cpp
        and "screen->grabWindow" in test_cpp
        and "window.isActiveWindow()" in test_cpp
        and "usesNativeSDRMetalRenderer()" in test_cpp
        and "inactive.copy(contentRect) == focused.copy(contentRect)" in test_cpp
        and "transparentBackgroundSample(inactive), focusedBackdrop" in test_cpp
    )
    cases.append(check(
        "AC-VIEWPORT-FOCUS-SHARP",
        focus,
        "a real top-level window deactivates the image window and screen pixels are compared, not QWidget::render output",
    ))

    transparent_background = (
        (ROOT / "tests/data/focus_transparency.png").is_file()
        and "fixture.hasAlphaChannel()" in test_cpp
        and "nativeMetalRendererDiagnostics().presentationActiveRequested" in test_cpp
        and "QGraphicsView::NoViewportUpdate" in test_cpp
        and "if (getCurrentFileDetails().isNativeSDRLoaded)" in qv_view
        and "hdrRenderer->setPresentationActive(true, false)" in qv_view
        and "hdrPresentationActive || nativeSDRPresentation" in qv_view
    )
    cases.append(check(
        "AC-TRANSPARENT-RASTER-BACKGROUND",
        transparent_background,
        "transparent PNG pixels use a persistent native SDR layer with its opaque viewport background while inactive",
    ))

    return 0 if all(cases) else 1


if __name__ == "__main__":
    sys.exit(main())
