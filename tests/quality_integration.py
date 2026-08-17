#!/usr/bin/env python3
"""Run deterministic integration checks for the Fovelle macOS-only change set.

The checks observe repository text, the generated bundle, the resource files, and
CTest/qmake results.  They do not mutate source files and write a JSON audit record.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(repo: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(list(args), cwd=repo, text=True, capture_output=True, check=False)


def check(checks: list[dict], identifier: str, passed: bool, actual: object, expected: str) -> None:
    checks.append(
        {
            "id": identifier,
            "pass": bool(passed),
            "actual": actual,
            "expected": expected,
        }
    )


def sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


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

    head_result = run(repo, "git", "rev-parse", "HEAD")
    head = head_result.stdout.strip()
    default_branch_result = run(repo, "git", "symbolic-ref", "--short", "refs/remotes/origin/HEAD")
    default_branch = default_branch_result.stdout.strip().removeprefix("origin/")
    check(
        checks,
        "I-01",
        head_result.returncode == 0 and default_branch == "main",
        {"head": head, "origin_default_branch": default_branch or "<unknown>"},
        "the repository is readable and origin's default branch is main",
    )

    cmake = text("CMakeLists.txt")
    qmake = text("qView.pro")
    check(
        checks,
        "I-02",
        all(
            needle in cmake
            for needle in (
                "project(Fovelle VERSION 0.1.0",
                'CMAKE_SYSTEM_NAME STREQUAL "Darwin"',
                'MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.inostarlin-passion.Fovelle"',
                'set(MACOSX_BUNDLE_ICON_FILE "qView.icns")',
            )
        )
        and all(needle in qmake for needle in ("TARGET = Fovelle", "VERSION = 0.1.0", "!macx", "Fovelle supports macOS only.")),
        {
            "cmake_project": "Fovelle VERSION 0.1.0" in cmake,
            "cmake_darwin_guard": 'CMAKE_SYSTEM_NAME STREQUAL "Darwin"' in cmake,
            "bundle_id": "io.github.inostarlin-passion.Fovelle" in cmake,
            "qmake_mac_guard": "!macx" in qmake,
        },
        "CMake and qmake identify Fovelle and reject non-macOS builds",
    )

    main_cpp = text("src/main.cpp")
    application_cpp = text("src/qvapplication.cpp")
    window_cpp = text("src/mainwindow.cpp")
    about_cpp = text("src/qvaboutdialog.cpp")
    translation_text = "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in (repo / "i18n").glob("*.ts"))
    check(
        checks,
        "I-03",
        all(
            needle in main_cpp
            for needle in (
                'setOrganizationName("Fovelle")',
                'setOrganizationDomain("io.github.inostarlin-passion")',
                'setApplicationName("Fovelle")',
                'setApplicationDisplayName("Fovelle")',
            )
        )
        and 'setWindowIcon(QIcon(":/icons/Fovelle.png"))' in application_cpp
        and "setQuitOnLastWindowClosed(true)" in application_cpp
        and 'QString newString = "Fovelle"' in window_cpp
        and 'escShortcut->setKey(Qt::Key_Escape)' in window_cpp
        and "else\n            close();" in window_cpp,
        {
            "identity": all(needle in main_cpp for needle in ("Fovelle", "io.github.inostarlin-passion")),
            "runtime_icon": ':/icons/Fovelle.png' in application_cpp,
            "quit_policy": "setQuitOnLastWindowClosed(true)" in application_cpp,
            "escape_policy": "else\n            close();" in window_cpp,
        },
        "runtime identity, icon, last-window policy, and Escape branches are present",
    )

    exact_about_lines = (
        "Based on qView",
        "Copyright © 2018–2025 jurplel and qView contributors",
        "Fovelle modifications © 2026",
        "jdpurcell/qView",
        "jdpurcell",
        "Licensed under GPLv3",
    )
    check(
        checks,
        "I-04",
        all(needle in about_cpp for needle in exact_about_lines)
        and "https://github.com/inostarlin-passion/Fovelle" in about_cpp
        and "https://github.com/jdpurcell/qView" in about_cpp
        and "interversehq.com" not in about_cpp
        and "interversehq.com" not in translation_text
        and "github.com/jurplel/qView" not in translation_text,
        {
            "about_lines": {needle: needle in about_cpp for needle in exact_about_lines},
            "github_url": "https://github.com/inostarlin-passion/Fovelle" in about_cpp,
            "qview_attribution": "https://github.com/jdpurcell/qView" in about_cpp and "jdpurcell" in about_cpp,
            "translation_old_urls_absent": "interversehq.com" not in translation_text and "github.com/jurplel/qView" not in translation_text,
        },
        "the About page and translation catalogs render the requested identity, declared qView source attribution, and no old website",
    )

    updater = text("src/updatechecker.h") + text("src/updatechecker.cpp")
    check(
        checks,
        "I-05",
        "api.github.com/repos/inostarlin-passion/Fovelle/releases" in updater
        and "github.com/inostarlin-passion/Fovelle/releases" in updater
        and "QCoreApplication::applicationVersion()" in updater,
        {
            "api_repository": "api.github.com/repos/inostarlin-passion/Fovelle/releases" in updater,
            "download_repository": "github.com/inostarlin-passion/Fovelle/releases" in updater,
            "runtime_version_comparison": "QCoreApplication::applicationVersion()" in updater,
        },
        "the updater checks the Fovelle GitHub releases endpoint and current app version",
    )

    icon_png = repo / "resources" / "Fovelle.png"
    icon_icns = repo / "dist" / "mac" / "qView.icns"
    source_icon = Path("/Users/inostarlin/Downloads/ChatGPT Image 2026年8月17日 12_12_41.png")
    source_match = source_icon.is_file() and icon_png.is_file() and source_icon.read_bytes() == icon_png.read_bytes()
    check(
        checks,
        "I-06",
        icon_png.is_file()
        and icon_png.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        and icon_icns.is_file()
        and icon_icns.read_bytes()[:4] == b"icns"
        and "Fovelle.png" in text("resources/resources.qrc")
        and source_match,
        {
            "source_path": str(source_icon),
            "source_matches_repository_png": source_match,
            "png_sha256": sha256(icon_png),
            "icns_sha256": sha256(icon_icns),
        },
        "the requested PNG is copied byte-for-byte, embedded as a Qt resource, and converted to a valid macOS ICNS",
    )

    source_platform_files = [
        "src/qvwin32functions.cpp",
        "src/qvwin32functions.h",
        "src/qvlinuxx11functions.cpp",
        "src/qvlinuxx11functions.h",
        "src/qvwindows11style.cpp",
        "src/qvwindows11style.h",
        "src/qvopenwithdialog.ui",
        "resources/resources_linux.qrc",
        "dist/scripts/linuxdeployqt.sh",
        "dist/scripts/windeployqt.ps1",
    ]
    nonmac_paths = [path for path in (repo / relative for relative in source_platform_files) if path.exists()]
    nonmac_directories = [path for path in (repo / "dist" / "win", repo / "dist" / "linux") if path.exists()]
    scan_files = [
        *list((repo / "src").glob("*.cpp")),
        *list((repo / "src").glob("*.h")),
        *list((repo / "src").glob("*.mm")),
        repo / "CMakeLists.txt",
        repo / "qView.pro",
        *list((repo / ".github" / "workflows").glob("*.yml")),
        *list((repo / "dist" / "scripts").glob("*.ps1")),
        *list((repo / "dist" / "scripts").glob("*.sh")),
    ]
    forbidden_platform_tokens = ("Q_OS_WIN", "Q_OS_LINUX", "Q_OS_UNIX", "WIN32", "X11_LOADED", "$IsWindows", "$IsLinux")
    platform_hits = {
        str(path.relative_to(repo)): [token for token in forbidden_platform_tokens if token in path.read_text(encoding="utf-8", errors="ignore")]
        for path in scan_files
        if path.is_file()
        and any(token in path.read_text(encoding="utf-8", errors="ignore") for token in forbidden_platform_tokens)
    }
    check(
        checks,
        "I-07",
        not nonmac_paths and not nonmac_directories and not platform_hits,
        {"existing_nonmac_paths": [str(path) for path in nonmac_paths], "existing_nonmac_directories": [str(path) for path in nonmac_directories], "platform_tokens": platform_hits},
        "non-macOS files, directories, and implementation branches are absent",
    )

    build_workflow = text(".github/workflows/build.yml")
    test_workflow = text(".github/workflows/test.yml")
    check(
        checks,
        "I-08",
        all(workflow.count("macos-14") >= 1 and "windows" not in workflow.lower() and "ubuntu" not in workflow.lower() for workflow in (build_workflow, test_workflow))
        and all("branches: [main]" in workflow for workflow in (build_workflow, test_workflow))
        and "Fovelle-macOS.zip" in build_workflow
        and "name: Fovelle-macOS" in build_workflow,
        {
            "build_macos_only": "macos-14" in build_workflow and "windows" not in build_workflow.lower() and "ubuntu" not in build_workflow.lower(),
            "test_macos_only": "macos-14" in test_workflow and "windows" not in test_workflow.lower() and "ubuntu" not in test_workflow.lower(),
            "main_branch_filters": all("branches: [main]" in workflow for workflow in (build_workflow, test_workflow)),
            "artifact": "Fovelle-macOS.zip" in build_workflow,
        },
        "build and test workflows target macOS, main, and the Fovelle artifact only",
    )

    bundle = build_dir / "Fovelle.app"
    executable = bundle / "Contents" / "MacOS" / "Fovelle"
    bundle_icon = bundle / "Contents" / "Resources" / "qView.icns"
    info_path = bundle / "Contents" / "Info.plist"
    info: dict = {}
    if info_path.is_file():
        try:
            info = plistlib.loads(info_path.read_bytes())
        except (plistlib.InvalidFileException, ValueError):
            info = {}
    bundle_expected = {
        "bundle": bundle.is_dir(),
        "executable": executable.is_file(),
        "icon": bundle_icon.is_file(),
        "identifier": info.get("CFBundleIdentifier") == "io.github.inostarlin-passion.Fovelle",
        "name": info.get("CFBundleName") == "Fovelle",
        "executable_name": info.get("CFBundleExecutable") == "Fovelle",
        "icon_name": info.get("CFBundleIconFile") == "qView.icns",
        "version": info.get("CFBundleShortVersionString") == "0.1.0" and info.get("CFBundleVersion") == "0.1.0",
        "copyright": "Fovelle modifications © 2026 Fovelle contributors" in str(info.get("NSHumanReadableCopyright", "")),
    }
    check(checks, "I-09", all(bundle_expected.values()), bundle_expected, "the built bundle has the Fovelle identity, icon, version, and copyright")

    ctest = subprocess.run(["ctest", "--test-dir", str(build_dir), "--output-on-failure"], cwd=repo, text=True, capture_output=True, check=False)
    check(checks, "I-10", ctest.returncode == 0, ctest.stdout + ctest.stderr, "CTest exits with code 0")

    qmake_path = shutil.which("qmake")
    qmake_result: subprocess.CompletedProcess[str] | None = None
    if qmake_path:
        with tempfile.TemporaryDirectory(prefix="fovelle-qmake-") as directory:
            qmake_result = subprocess.run([qmake_path, str(repo / "qView.pro"), "-o", str(Path(directory) / "Makefile")], cwd=repo, text=True, capture_output=True, check=False)
    check(
        checks,
        "I-11",
        qmake_result is not None and qmake_result.returncode == 0,
        {"qmake": qmake_path, "return_code": qmake_result.returncode if qmake_result else None, "output": (qmake_result.stdout + qmake_result.stderr)[-2000:] if qmake_result else "qmake not found"},
        "the macOS qmake project configures successfully",
    )

    diff_check = run(repo, "git", "diff", "--check", "HEAD")
    check(checks, "I-12", diff_check.returncode == 0, diff_check.stdout + diff_check.stderr, "the complete working-tree diff has no whitespace errors")

    readme = text("README.md")
    readme_last_line = readme.rstrip().splitlines()[-1] if readme.strip() else ""
    check(
        checks,
        "I-13",
        "does not perform an automatic network version check during tests." not in readme
        and "https://github.com/jdpurcell/qView" in readme
        and "jdpurcell" in readme
        and readme_last_line == "Fovelle incorporates portions of commits from [jdpurcell/qView](https://github.com/jdpurcell/qView) by jdpurcell.",
        {
            "last_line": readme_last_line,
            "old_last_line_absent": "does not perform an automatic network version check during tests." not in readme,
            "qview_attribution": "https://github.com/jdpurcell/qView" in readme and "jdpurcell" in readme,
        },
        "README removes the former final line and records the jdpurcell/qView attribution",
    )

    window_header = text("src/mainwindow.h")
    test_source = text("tests/tst_qviewtests.cpp")
    fullscreen_section = window_cpp[window_cpp.find("void MainWindow::toggleFullScreen()") :]
    exit_match = re.search(
        r"if \(windowState\(\)\.testFlag\(Qt::WindowFullScreen\)\)\s*\{(?P<body>.*?)\n\s*\}\s*else",
        fullscreen_section,
        re.DOTALL,
    )
    exit_body = exit_match.group("body") if exit_match else ""
    entry_contract = all(
        needle in window_cpp
        for needle in (
            "returnShortcut = new QShortcut(Qt::Key_Return, this);",
            "keypadEnterShortcut = new QShortcut(Qt::Key_Enter, this);",
            "returnShortcut->setAutoRepeat(false);",
            "keypadEnterShortcut->setAutoRepeat(false);",
            "if (!windowState().testFlag(Qt::WindowFullScreen))",
            "toggleFullScreen();",
        )
    )
    exit_contract = bool(exit_match) and exit_body.count("setWindowState(storedWindowState);") == 1 and "showNormal(" not in exit_body
    check(
        checks,
        "I-FS-01",
        entry_contract
        and "QShortcut *returnShortcut;" in window_header
        and "QShortcut *keypadEnterShortcut;" in window_header,
        {
            "entry_contract": entry_contract,
            "return_member": "QShortcut *returnShortcut;" in window_header,
            "keypad_enter_member": "QShortcut *keypadEnterShortcut;" in window_header,
        },
        "Return and keypad Enter are non-repeating entry-only full-screen shortcuts",
    )
    check(
        checks,
        "I-FS-02",
        exit_contract
        and all(
            marker in test_source
            for marker in (
                "TC-FS-01",
                "TC-FS-02",
                "TC-FS-03",
                "TC-FS-04",
                "testEscapeRestoresLoadedImageWithoutGeometryJump",
            )
        ),
        {
            "single_restore_request": exit_body.count("setWindowState(storedWindowState);") == 1,
            "no_second_normal_request": "showNormal(" not in exit_body,
            "test_cases_present": all(marker in test_source for marker in ("TC-FS-01", "TC-FS-02", "TC-FS-03", "TC-FS-04")),
        },
        "the asynchronous macOS exit path has one restore-state request and a regression test",
    )
    check(
        checks,
        "I-FS-03",
        "QElapsedTimer" in test_source
        and 'reportFullscreenMetric("enter"' in test_source
        and 'reportFullscreenMetric("exit"' in test_source,
        {
            "elapsed_timer": "QElapsedTimer" in test_source,
            "enter_metric": 'reportFullscreenMetric("enter"' in test_source,
            "exit_metric": 'reportFullscreenMetric("exit"' in test_source,
        },
        "fullscreen tests emit deterministic state-acknowledgement response metrics",
    )

    result = {
        "kind": "integration",
        "repo": str(repo),
        "head": head,
        "checks": checks,
        "passed": all(item["pass"] for item in checks),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
