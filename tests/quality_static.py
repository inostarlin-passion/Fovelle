#!/usr/bin/env python3
"""Run read-only static quality gates and write an audit record."""

from __future__ import annotations

import argparse
import ast
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


def command(*args: str, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(list(args), cwd=cwd, text=True, capture_output=True, check=False)


def add_check(checks: list[dict], identifier: str, passed: bool, actual: object, expected: str) -> None:
    checks.append({"id": identifier, "pass": bool(passed), "actual": actual, "expected": expected})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    checks: list[dict] = []

    clang_format = shutil.which("clang-format")
    cpp_files = [repo / "src/mainwindow.cpp", repo / "src/mainwindow.h", repo / "tests/tst_qviewtests.cpp"]
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
            "clang-format is unavailable; the fallback compile/static check succeeds",
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

    window_cpp = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")
    exit_section = window_cpp[window_cpp.find("void MainWindow::toggleFullScreen()") :]
    exit_match = re.search(
        r"if \(windowState\(\)\.testFlag\(Qt::WindowFullScreen\)\)\s*\{(?P<body>.*?)\n\s*\}\s*else",
        exit_section,
        re.DOTALL,
    )
    exit_body = exit_match.group("body") if exit_match else ""
    add_check(
        checks,
        "ST-04",
        bool(exit_match)
        and exit_body.count("setWindowState(storedWindowState);") == 1
        and "showNormal(" not in exit_body,
        {
            "restore_state_requests": exit_body.count("setWindowState(storedWindowState);"),
            "contains_second_normal_request": "showNormal(" in exit_body,
        },
        "the fullscreen exit branch contains exactly one restore-state request",
    )

    result = {"kind": "static", "repo": str(repo), "checks": checks, "passed": all(item["pass"] for item in checks)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
