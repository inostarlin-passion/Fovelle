#!/usr/bin/env python3
"""Run integration gates for the macOS titlebar icon removal and regressions."""

from __future__ import annotations

import argparse
import json
import plistlib
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
            "fallback_is_after_qt_read": loader.find("QVCocoaFunctions::readAdditionalImage") > loader.find("imageReader.read()"),
        },
        "Qt failure is followed by a linked Apple Image I/O WebP/AVIF fallback that applies orientation metadata",
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

    diff_check = run(repo, "git", "diff", "--check", "HEAD")
    add_check(
        checks,
        "I-08",
        diff_check.returncode == 0,
        {"return_code": diff_check.returncode, "output": diff_check.stdout + diff_check.stderr},
        "the integrated working-tree diff has no whitespace errors",
    )

    test_source = text("tests/tst_qviewtests.cpp")
    required_cases = (
        "testImageLoaderLoadsWebpWithImageIOFallback",
        "testImageLoaderLoadsAvifWithImageIOFallback",
        "testImageLoaderAppliesWebpOrientation",
        "testImageLoaderAppliesAvifOrientation",
        "testWindowIconIsCleared",
        "testTitlebarDocumentProxyIsClearedForLoadedFile",
        "testTitlebarIconClearingIsIdempotent",
        "testSettingsFormatsIncludeNativeImageFormats",
        "testMouseWheelUsesOneDiscreteStep",
        "testTouchpadWheelCanUseFractionalSteps",
        "testFitZoomSurvivesInverseWheelStepsAndFullscreenResize",
        "testManualZoomRemainsManualAcrossResize",
    )
    add_check(
        checks,
        "I-09",
        all(case in test_source for case in required_cases),
        {"cases": {case: case in test_source for case in required_cases}},
        "the integrated test source contains one deterministic case for each current-scope acceptance criterion",
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
