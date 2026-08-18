#!/usr/bin/env python3
"""Run static quality gates for the titlebar icon removal and regressions."""

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
        "tests/tst_qviewtests.cpp",
        "CMakeLists.txt",
        "qView.pro",
        "tests/CMakeLists.txt",
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
    decoder_contract = contains_all(
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
        ),
    )
    add_check(
        checks,
        "ST-06",
        decoder_contract,
        {
            "image_io_type_query": "CGImageSourceCopyTypeIdentifiers" in cocoa_mm,
            "image_io_decode": "CGImageSourceCreateThumbnailAtIndex" in cocoa_mm,
            "orientation_transform": "kCGImageSourceCreateThumbnailWithTransform" in cocoa_mm,
            "loader_fallback": "QVCocoaFunctions::readAdditionalImage" in loader_cpp,
        },
        "the macOS Image I/O fallback applies source orientation metadata and is called after Qt decoding fails",
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
        "testMouseWheelUsesOneDiscreteStep",
        "testTouchpadWheelCanUseFractionalSteps",
        "testFitZoomSurvivesInverseWheelStepsAndFullscreenResize",
        "testManualZoomRemainsManualAcrossResize",
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

    result = {"kind": "static", "repo": str(repo), "checks": checks, "passed": all(item["pass"] for item in checks)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
