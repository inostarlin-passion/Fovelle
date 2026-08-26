#!/usr/bin/env python3
"""Run the CI-repair acceptance matrix and emit machine-auditable reports.

The matrix is deliberately small and atomic.  Static checks inspect the CI
contract and the asynchronous-rendering contract, unit checks invoke the
affected QtTest methods, integration checks exercise the registered CTest
target, and system checks start the real application bundle.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


STAGES = ("static", "unit", "integration", "system")
REPORT_NAMES = (
    "test_evidence.json",
    "test_case_specification.json",
    "test_completion_report.json",
    "code_quality_assessment_report.json",
)


RESEARCH_TRACE = [
    {
        "hop": 1,
        "layer": "observed CI failure",
        "source": "https://github.com/inostarlin-passion/Fovelle/actions/runs/32966331494",
        "finding": "The Checks run for commit 87ffbe4 failed in WindowBehaviorTests::testExitFullscreenActionUsesEscapePath; clang-tidy and clang-format passed.",
        "premise": "The hosted runner log is the authoritative observation for this repair.",
    },
    {
        "hop": 2,
        "layer": "cross-job confirmation",
        "source": "https://github.com/inostarlin-passion/Fovelle/actions/runs/32966331490",
        "finding": "The Build Fovelle job reported the same full-test failure, so the regression is shared by the registered test binary rather than isolated to one job wrapper.",
        "premise": "The same failing QtTest method in both jobs is stronger evidence than a single job observation.",
    },
    {
        "hop": 3,
        "layer": "runner capability",
        "source": "https://docs.github.com/en/actions/reference/runners/github-hosted-runners",
        "finding": "GitHub documents macos-26 as a supported hosted-runner label.",
        "premise": "Because macos-26 is a documented label, changing the runner is not required by the observed failure.",
    },
    {
        "hop": 4,
        "layer": "build-tool semantics",
        "source": "https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html",
        "finding": "CMake invokes the configured clang-tidy command alongside the compiler for target sources.",
        "premise": "A POST_BUILD hook attached to the same target remains in the tidy build unless the optional hook is disabled.",
    },
    {
        "hop": 5,
        "layer": "async completion semantics",
        "source": "https://doc.qt.io/qt-6/qfuturewatcher.html",
        "finding": "QFutureWatcher emits finished() when the watched future finishes.",
        "premise": "The interaction timer becoming idle is not equivalent to the worker future finishing or its tile being painted.",
    },
    {
        "hop": 6,
        "layer": "test synchronization",
        "source": "https://doc.qt.io/qt-6/qttest-best-practices.html",
        "finding": "Qt recommends QTRY-style condition polling for asynchronous behavior instead of a fixed short delay.",
        "premise": "The vector test must poll a non-invasive rendered-tile observation while processing events before asserting its size.",
    },
    {
        "hop": 7,
        "layer": "deduction",
        "source": "https://doc.qt.io/qt-6/qtest.html",
        "finding": "QTRY and qWaitFor keep the test event loop active while waiting for a condition; the remote 512×256 result also matches the 8×4 fallback geometry at the capped 64× zoom.",
        "premise": "The strict >512 size assertion is therefore incidental and invalid; the test must assert actual vector painting plus a non-empty tile, while the existing viewport bound remains the safety invariant.",
    },
    {
        "hop": 8,
        "layer": "window geometry semantics",
        "source": "https://doc.qt.io/qt-6/application-windows.html",
        "finding": "Qt documents top-level QWidget::geometry() as client geometry excluding the window frame, while frameGeometry() includes the frame.",
        "premise": "A client rectangle is not a stable cross-platform proxy for the native normal frame when titlebar insets are changed by the platform.",
    },
    {
        "hop": 9,
        "layer": "platform restoration semantics",
        "source": "https://doc.qt.io/qt-6/restoring-geometry.html",
        "finding": "Qt documents that windowing-system decoration and restoration can adjust the geometry after a window becomes visible.",
        "premise": "The test must capture the geometry after the deferred native decoration setup has settled instead of overwriting that observation with a pre-decoration request.",
    },
    {
        "hop": 10,
        "layer": "repository deduction",
        "source": "tests/tst_qviewtests.cpp and src/mainwindow.cpp",
        "finding": "The remote failure compared requested QRect(220,180 720x500) with AppKit-restored QRect(220,148 720x532); the test's second setGeometry() occurred after showEvent's deferred full-size-content-view setup.",
        "premise": "Removing that second write makes the baseline the platform's settled normal geometry, so the test measures the View action's native exit path rather than titlebar decoration conversion.",
    },
]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read(repo: Path, relative: str) -> str:
    return (repo / relative).read_text(encoding="utf-8")


def compact_output(stdout: str, stderr: str, limit: int = 4000) -> str:
    output = stdout + stderr
    return output[-limit:]


def run_command(
    command: list[str],
    cwd: Path,
    environment: dict[str, str] | None = None,
    timeout: float = 120.0,
) -> dict[str, Any]:
    started = time.monotonic()
    env = os.environ.copy()
    if environment:
        env.update(environment)
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        return {
            "command": command,
            "return_code": result.returncode,
            "passed": result.returncode == 0,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": compact_output(result.stdout, result.stderr),
        }
    except subprocess.TimeoutExpired as error:
        return {
            "command": command,
            "return_code": None,
            "passed": False,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": compact_output(
                error.stdout or "", error.stderr or ""
            )
            + "\nPROCESS_TIMEOUT",
        }
    except OSError as error:
        return {
            "command": command,
            "return_code": None,
            "passed": False,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": f"{type(error).__name__}: {error}",
        }


def static_result(test_code: str, passed: bool, observed: dict[str, Any]) -> dict[str, Any]:
    return {
        "passed": bool(passed),
        "observed": observed,
        "execution": {"test_code": test_code, "kind": "source_contract"},
    }


def static_tidy_path_contract(repo: Path) -> dict[str, Any]:
    source = read(repo, "build.sh")
    tidy_match = re.search(r"--tidy\)\n(?P<body>.*?)(?=\n\s*--tidy-fix\))", source, re.S)
    tidy_fix_match = re.search(r"--tidy-fix\)\n(?P<body>.*?)(?=\n\s*--clean\))", source, re.S)
    tidy_body = tidy_match.group("body") if tidy_match else ""
    tidy_fix_body = tidy_fix_match.group("body") if tidy_fix_match else ""
    cmake = read(repo, "CMakeLists.txt")
    passed = all(
        (
            "CMAKE_CXX_CLANG_TIDY=clang-tidy" in tidy_body,
            "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_body,
            "CMAKE_CXX_CLANG_TIDY='clang-tidy;-fix-errors'" in tidy_fix_body,
            "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_fix_body,
            'option(FOVELLE_BUNDLE_GHOSTSCRIPT "Bundle the pinned AGPL Ghostscript runtime" ON)' in cmake,
            "if(FOVELLE_BUNDLE_GHOSTSCRIPT)" in cmake,
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_tidy_path_contract",
        passed,
        {
            "tidy_disables_ghostscript_bundle": "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_body,
            "tidy_fix_disables_ghostscript_bundle": "DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF" in tidy_fix_body,
            "normal_bundle_default_remains_on": 'option(FOVELLE_BUNDLE_GHOSTSCRIPT "Bundle the pinned AGPL Ghostscript runtime" ON)' in cmake,
            "post_build_hook_is_option_guarded": "if(FOVELLE_BUNDLE_GHOSTSCRIPT)" in cmake,
        },
    )


def static_async_observation_contract(repo: Path) -> dict[str, Any]:
    header = read(repo, "src/qvgraphicsimageitem.h")
    item = read(repo, "src/qvgraphicsimageitem.cpp")
    view = read(repo, "src/qvgraphicsview.cpp")
    tests = read(repo, "tests/tst_qviewtests.cpp")
    passed = all(
        (
            "bool hasPendingVectorRefinement() const;" in header,
            "activeAsyncRequest.has_value()" in item,
            "pendingAsyncRequest.has_value()" in item,
            "loadedPixmapItem->hasPendingVectorRefinement()" in view,
            "quint64 QVGraphicsView::vectorRenderCount() const" in view,
            "waitForRenderedVectorTile" in tests,
            "QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000);" in tests,
            "QElapsedTimer" in tests,
            "view->vectorRenderCount() > 0" in tests,
            "lastVectorRasterSize().isEmpty()" in tests,
            "QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 500);" not in tests,
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_async_observation_contract",
        passed,
        {
            "worker_and_queue_are_observable": "activeAsyncRequest.has_value()" in item and "pendingAsyncRequest.has_value()" in item,
            "view_delegates_observation": "loadedPixmapItem->hasPendingVectorRefinement()" in view,
            "test_waits_for_painted_tile": "waitForRenderedVectorTile" in tests and "view->vectorRenderCount() > 0" in tests,
            "slow_runner_timeout_ms": 5000,
        },
    )


def static_runner_contract(repo: Path) -> dict[str, Any]:
    workflow_paths = (".github/workflows/test.yml", ".github/workflows/build.yml")
    workflows = {path: read(repo, path) for path in workflow_paths}
    passed = all(
        "runs-on: macos-26" in source
        and "test \"${XCODE_VERSION%%.*}\" -ge 26" in source
        and "test \"${SDK_VERSION%%.*}\" -ge 26" in source
        for source in workflows.values()
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_runner_contract",
        passed,
        {
            "workflow_runner_labels": {path: re.findall(r"runs-on:\s*(\S+)", source) for path, source in workflows.items()},
            "xcode_and_sdk_guards_present": passed,
            "runner_label_reference": "GitHub-hosted runner documentation confirms macos-26 is supported.",
        },
    )


def static_test_registration_contract(repo: Path) -> dict[str, Any]:
    cmake = read(repo, "tests/CMakeLists.txt")
    pipeline = read(repo, "tests/ci_quality_pipeline.py")
    try:
        ast.parse(pipeline, filename=str(repo / "tests/ci_quality_pipeline.py"))
        pipeline_parses = True
    except SyntaxError:
        pipeline_parses = False
    passed = all(
        (
            "NAME FovelleTests" in cmake,
            "NAME FovelleTaskAcceptanceAudit" in cmake,
            '"${CMAKE_CURRENT_SOURCE_DIR}/ci_quality_pipeline.py"' in cmake,
            "--skip-build" in cmake,
            pipeline_parses,
            'STAGES = ("static", "unit", "integration", "system")' in pipeline,
            all(f'"{name}"' in pipeline for name in REPORT_NAMES),
        )
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_test_registration_contract",
        passed,
        {
            "ctest_targets": ["FovelleTests", "FovelleTaskAcceptanceAudit"],
            "pipeline_python_parses": pipeline_parses,
            "pipeline_has_four_ordered_stages": 'STAGES = ("static", "unit", "integration", "system")' in pipeline,
            "required_report_names": list(REPORT_NAMES),
        },
    )


def static_scope_contract(repo: Path) -> dict[str, Any]:
    result = run_command(
        [
            "git",
            "diff",
            "--check",
            "HEAD",
            "--",
            "build.sh",
            "src",
            "tests",
            ".github",
            "CMakeLists.txt",
        ],
        repo,
        timeout=30,
    )
    return static_result(
        "tests/ci_quality_pipeline.py::static_scope_contract",
        result["passed"],
        {
            "return_code": result["return_code"],
            "output": result["output_tail"],
            "scope": ["build.sh", "src", "tests", ".github", "CMakeLists.txt"],
            "unrelated_preexisting_paths_excluded": ["README.md"],
        },
    )


STATIC_TESTS: tuple[tuple[str, str, Callable[[Path], dict[str, Any]]], ...] = (
    ("CI-STATIC-001", "static_tidy_path_contract", static_tidy_path_contract),
    ("CI-STATIC-002", "static_async_observation_contract", static_async_observation_contract),
    ("CI-STATIC-003", "static_runner_contract", static_runner_contract),
    ("CI-STATIC-004", "static_test_registration_contract", static_test_registration_contract),
    ("CI-STATIC-005", "static_scope_contract", static_scope_contract),
)


UNIT_TESTS: tuple[tuple[str, str, str], ...] = (
    ("CI-UNIT-001", "GraphicsViewTests", "testVectorFormatsUseDocumentSceneItem"),
    ("CI-UNIT-002", "GraphicsViewTests", "testVectorPanRepaintsOnlyExposedStrip"),
    ("CI-UNIT-003", "ImageLoaderTests", "testEPSPostScriptRender"),
    ("CI-UNIT-004", "WindowBehaviorTests", "testExitFullscreenActionUsesEscapePath"),
)


def run_static(repo: Path) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    for case_id, _name, test in STATIC_TESTS:
        try:
            results[case_id] = test(repo)
        except (OSError, UnicodeError, re.error) as error:
            results[case_id] = static_result(
                f"tests/ci_quality_pipeline.py::{test.__name__}",
                False,
                {"error": f"{type(error).__name__}: {error}"},
            )
    return results


def run_unit(repo: Path, build_dir: Path) -> dict[str, dict[str, Any]]:
    binary = build_dir / "tests" / "fovelle_tests"
    results: dict[str, dict[str, Any]] = {}
    for case_id, suite, test_name in UNIT_TESTS:
        if not binary.is_file():
            results[case_id] = {
                "passed": False,
                "observed": {"reason": "test binary does not exist", "binary": str(binary)},
                "execution": {"test_code": f"tests/tst_qviewtests.cpp::{suite}::{test_name}"},
            }
            continue
        result = run_command(
            [str(binary), "-o", "-,txt", test_name],
            repo,
            {
                "QT_QPA_PLATFORM": "cocoa",
                "QT_FATAL_WARNINGS": "1",
                "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
                "FOVELLE_TEST_SUITE": suite,
            },
            timeout=60,
        )
        output = result["output_tail"]
        pass_marker = f"PASS   : {suite}::{test_name}()"
        result["passed"] = bool(result["passed"] and pass_marker in output)
        results[case_id] = {
            "passed": result["passed"],
            "observed": {
                "suite": suite,
                "test": test_name,
                "pass_marker": pass_marker,
                "pass_marker_observed": pass_marker in output,
            },
            "execution": result,
        }
    return results


def run_integration(repo: Path, build_dir: Path, skip_build: bool) -> dict[str, dict[str, Any]]:
    if skip_build:
        build_result: dict[str, Any] = {
            "command": [],
            "return_code": 0,
            "passed": True,
            "duration_ms": 0,
            "output_tail": "build skipped by CTest acceptance-audit invocation",
        }
    else:
        build_result = run_command(
            ["cmake", "--build", str(build_dir), "--parallel", "2"],
            repo,
            timeout=240,
        )

    results: dict[str, dict[str, Any]] = {}
    if not build_result["passed"]:
        failure = {
            "passed": False,
            "observed": {"build_skipped": skip_build, "reason": "CMake build failed"},
            "execution": {"build": build_result},
        }
        return {"CI-INTEGRATION-001": failure, "CI-INTEGRATION-002": failure.copy()}

    ctest = run_command(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "--output-on-failure",
            "--timeout",
            "90",
            "-R",
            "^FovelleTests$",
        ],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
        },
        timeout=180,
    )
    results["CI-INTEGRATION-001"] = {
        "passed": bool(ctest["passed"]),
        "observed": {"build_skipped": skip_build, "ctest_regex": "^FovelleTests$"},
        "execution": {"build": build_result, "ctest": ctest},
    }

    registered = run_command(
        ["ctest", "--test-dir", str(build_dir), "-N"],
        repo,
        timeout=30,
    )
    registered_output = registered["output_tail"]
    has_targets = all(name in registered_output for name in ("FovelleTests", "FovelleTaskAcceptanceAudit"))
    results["CI-INTEGRATION-002"] = {
        "passed": bool(registered["passed"] and has_targets),
        "observed": {
            "required_registered_tests": ["FovelleTests", "FovelleTaskAcceptanceAudit"],
            "registered_tests_observed": has_targets,
        },
        "execution": {"ctest_list": registered},
    }
    return results


def run_system(repo: Path, build_dir: Path) -> dict[str, dict[str, Any]]:
    binary = build_dir / "Fovelle.app" / "Contents" / "MacOS" / "Fovelle"
    if not binary.is_file():
        missing = {
            "passed": False,
            "observed": {"reason": "application binary does not exist", "binary": str(binary)},
            "execution": {"test_code": "src/main.cpp::system_probe"},
        }
        return {"CI-SYSTEM-001": missing, "CI-SYSTEM-002": missing.copy()}

    probe = run_command(
        [str(binary)],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "FOVELLE_SYSTEM_PROBE": "1",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
        },
        timeout=30,
    )
    probe_match = re.search(
        r"FOVELLE_SYSTEM_PROBE windows=1 maximized=true", probe["output_tail"]
    )
    version = run_command([str(binary), "--version"], repo, timeout=15)
    version_match = re.search(r"\b1\.0\.0\b", version["output_tail"])
    return {
        "CI-SYSTEM-001": {
            "passed": bool(probe["passed"] and probe_match),
            "observed": {
                "probe_marker": probe_match.group(0) if probe_match else None,
            },
            "execution": probe,
        },
        "CI-SYSTEM-002": {
            "passed": bool(version["passed"] and version_match),
            "observed": {
                "expected_version": "1.0.0",
                "version_observed": version_match.group(0) if version_match else None,
            },
            "execution": version,
        },
    }


def case(
    identifier: str,
    layer: str,
    criterion: str,
    quality_requirement: str,
    test_code: str,
    purpose: str,
    preconditions: list[str],
    input_data: dict[str, Any],
    operation_steps: list[str],
    expected_result: str,
    postconditions: list[str],
) -> dict[str, Any]:
    return {
        "id": identifier,
        "test_layer": layer,
        "atomic_acceptance_criterion": criterion,
        "quality_requirement": quality_requirement,
        "test_code": test_code,
        "test_purpose": purpose,
        "preconditions": preconditions,
        "input_data": input_data,
        "operation_steps": operation_steps,
        "expected_result": expected_result,
        "postconditions": postconditions,
    }


CASES = [
    case(
        "CI-STATIC-001",
        "static",
        "clang-tidy 与应用自有 C++ 目标绑定，但不触发 Ghostscript 第三方运行时打包。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::static_tidy_path_contract",
        "验证静态分析路径只包含必要的应用检查。",
        ["仓库包含 build.sh 和 CMakeLists.txt。"],
        {"tidy_command": "./build.sh --tidy", "tidy_fix_command": "./build.sh --tidy-fix"},
        ["读取两个脚本分支。", "读取 CMake Ghostscript 选项及 POST_BUILD 守卫。", "逐项比对 clang-tidy 与 OFF 标志。"],
        "两个分析分支都显式设置 FOVELLE_BUNDLE_GHOSTSCRIPT=OFF，普通构建默认仍为 ON。",
        ["不改变普通构建的 Ghostscript 打包能力。"],
    ),
    case(
        "CI-STATIC-002",
        "static",
        "矢量 refinement 的待处理状态包含 active 与 queued 异步请求，且测试等待实际渲染 tile。",
        "可测试性",
        "tests/ci_quality_pipeline.py::static_async_observation_contract",
        "验证运行时观察点与测试同步条件含义一致。",
        ["矢量渲染由 QFutureWatcher 异步完成。"],
        {"source_files": ["src/qvgraphicsimageitem.*", "src/qvgraphicsview.cpp", "tests/tst_qviewtests.cpp"]},
        ["检查观察方法的声明和实现。", "检查 view 委托及 worker/queue 状态。", "检查测试的条件轮询和最终 tile 断言。"],
        "测试不会仅以 500ms interaction timer 作为完成信号，而是等待 painted tile。",
        ["异步工作完成前调用方仍可观察到 pending 状态。"],
    ),
    case(
        "CI-STATIC-003",
        "static",
        "CI 使用已验证的 macos-26 runner，并在 job 内校验 Xcode 与 SDK 主版本。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::static_runner_contract",
        "排除错误 runner 标签作为当前失败根因。",
        ["GitHub Actions workflow 文件存在。"],
        {"workflow_files": [".github/workflows/test.yml", ".github/workflows/build.yml"]},
        ["读取两个 workflow。", "检查 runs-on 标签。", "检查 Xcode/SDK 版本守卫。"],
        "两个核心 workflow 均使用 macos-26 且包含版本前置检查。",
        ["保留当前 runner 选择和显式环境约束。"],
    ),
    case(
        "CI-STATIC-004",
        "static",
        "CTest 注册完整 Qt 回归测试和四层审计流水线，并保留 skip-build 调用契约。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::static_test_registration_contract",
        "验证验收入口和四个报告文件的边界清楚。",
        ["tests/CMakeLists.txt 可读取。"],
        {"registered_tests": ["FovelleTests", "FovelleTaskAcceptanceAudit"]},
        ["读取 CTest 注册。", "检查审计脚本和 skip-build 参数。", "检查静态、单元、集成、系统阶段与报告名。"],
        "审计测试调用当前 CI 质量流水线，且报告输出集合完整。",
        ["CTest 可在构建完成后重复运行审计。"],
    ),
    case(
        "CI-STATIC-005",
        "static",
        "任务范围内的工作树差异不包含空白错误。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::static_scope_contract",
        "避免修复引入不可见格式噪声。",
        ["Git 工作树可执行 git diff --check。"],
        {"scope": ["build.sh", "src", "tests", ".github", "CMakeLists.txt"]},
        ["运行 git diff --check HEAD。", "仅审计任务范围路径。"],
        "命令返回 0 且没有 whitespace error。",
        ["README.md 中既有的用户变更保持不动。"],
    ),
    case(
        "CI-UNIT-001",
        "unit",
        "PDF 与 SVG 矢量文档均能在 6400% 路径完成 refinement，并产生真实绘制的非空 bounded tile。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorFormatsUseDocumentSceneItem",
        "直接回归远端失败的跨格式矢量场景。",
        ["build/tests/fovelle_tests 已构建。", "Qt 可在 cocoa 平台运行。"],
        {"suite": "GraphicsViewTests", "test": "testVectorFormatsUseDocumentSceneItem"},
        ["设置 FOVELLE_TEST_SUITE。", "执行指定 QtTest 方法。", "检查 PDF 与 SVG pass marker。"],
        "QtTest 返回 0，且指定测试输出 PASS。",
        ["测试进程退出，临时矢量文档被清理。"],
    ),
    case(
        "CI-UNIT-002",
        "unit",
        "矢量平移只使暴露条带重绘，并在 worker refinement 状态下保持可观测。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip",
        "验证 pending 状态语义修复没有破坏 backing-store 平移优化。",
        ["build/tests/fovelle_tests 已构建。", "矢量 SVG fixture 可用。"],
        {"suite": "GraphicsViewTests", "test": "testVectorPanRepaintsOnlyExposedStrip"},
        ["执行指定 QtTest 方法。", "等待 refinement 清零且已有真实 tile 绘制。", "读取 dirty repaint ratio 与 pending 状态断言。"],
        "QtTest 返回 0 且平移 dirty ratio 不超过测试阈值。",
        ["测试窗口关闭，事件过滤器移除。"],
    ),
    case(
        "CI-UNIT-003",
        "unit",
        "EPS 的 PostScript 内容可通过 Ghostscript 转为应用使用的 PDF 矢量文档。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSPostScriptRender",
        "隔离验证当前正常测试仍可使用 Ghostscript runtime。",
        ["正常测试构建启用 Ghostscript bundle。", "Ghostscript 可执行文件可用。"],
        {"suite": "ImageLoaderTests", "test": "testEPSPostScriptRender"},
        ["执行指定 QtTest 方法。", "检查 EPS render pass marker。"],
        "QtTest 返回 0 且 EPS 渲染测试输出 PASS。",
        ["测试生成的临时 PDF/fixture 被清理。"],
    ),
    case(
        "CI-UNIT-004",
        "unit",
        "View → Exit Full Screen 与物理 Escape 共用 AppKit 异步退出边界，并在 native transition 完成后保持正常窗口几何稳定。",
        "功能正确性",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testExitFullscreenActionUsesEscapePath",
        "验证菜单退出动作与 Escape 走同一原生异步边界，且不因 macOS 标题栏 client inset 差异产生假失败。",
        ["可见的正常 MainWindow 已创建。", "macOS Cocoa 原生窗口和全屏通知可用。"],
        {"action_input": "View → Exit Full Screen QAction", "comparison_input": "物理 Escape", "normal_geometry_source": "showEvent 后已稳定的实际 QWidget::geometry()"},
        ["等待 deferred full-size-content-view 设置稳定。", "捕获平台实际正常几何。", "进入全屏并直接触发 View 退出动作。", "等待原生退出通知完成并检查几何稳定。", "再次进入全屏并发送 Escape，重复同样检查。"],
        "两种输入都在 native transition 完成前保持 Qt 全屏状态，完成后退出全屏、恢复捕获的正常几何，且无退出后的二次 Move/Resize。",
        ["测试窗口关闭。", "临时 titlebar 设置和 quit policy 恢复。"],
    ),
    case(
        "CI-INTEGRATION-001",
        "integration",
        "完整 FovelleTests CTest 目标通过。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_integration::ctest_FovelleTests",
        "验证修复在 CMake 注册的完整 Qt 回归集合中成立。",
        ["CMake build 已成功或由本阶段构建。", "CTest 已配置。"],
        {"ctest_regex": "^FovelleTests$", "timeout_seconds": 90},
        ["必要时执行 cmake --build。", "运行 ctest --output-on-failure -R ^FovelleTests$。"],
        "CTest 返回 0，FovelleTests 不含失败或崩溃。",
        ["保留 CTest 输出作为 evidence。"],
    ),
    case(
        "CI-INTEGRATION-002",
        "integration",
        "构建目录同时注册 FovelleTests 和 FovelleTaskAcceptanceAudit。",
        "精益完整性",
        "tests/ci_quality_pipeline.py::run_integration::ctest_list",
        "验证四层审计作为 CI 构建的一部分可被 CTest 发现。",
        ["CMake configure 已完成。"],
        {"command": "ctest --test-dir build -N"},
        ["列出 CTest 测试。", "检查两个注册名称。"],
        "列表输出包含两个目标。",
        ["不执行递归审计，仅验证注册契约。"],
    ),
    case(
        "CI-SYSTEM-001",
        "system",
        "实际 Fovelle.app 可启动并通过系统探针报告一个最大化窗口。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_system::FOVELLE_SYSTEM_PROBE",
        "用真实 app bundle 验证最终启动副作用。",
        ["build/Fovelle.app/Contents/MacOS/Fovelle 存在。", "macOS Cocoa 图形环境可用。"],
        {"environment": {"QT_QPA_PLATFORM": "cocoa", "FOVELLE_SYSTEM_PROBE": "1"}},
        ["启动 app bundle。", "等待 system probe 输出。", "匹配 windows=1 maximized=true。"],
        "进程返回 0 且输出精确探针标记。",
        ["探针触发 app.quit，进程结束。"],
    ),
    case(
        "CI-SYSTEM-002",
        "system",
        "实际 app bundle 的版本输出为 1.0.0。",
        "功能正确性",
        "tests/ci_quality_pipeline.py::run_system::version",
        "验证最终 bundle 的版本元数据没有被构建路径修复破坏。",
        ["app bundle 已构建。"],
        {"command": "build/Fovelle.app/Contents/MacOS/Fovelle --version", "expected_version": "1.0.0"},
        ["执行 --version。", "匹配版本号。"],
        "进程返回 0 且输出包含 1.0.0。",
        ["版本进程退出。"],
    ),
]


def report_artifact(repo: Path, relative: str) -> dict[str, Any]:
    path = repo / relative
    record: dict[str, Any] = {
        "path": relative,
        "absolute_path": str(path.resolve()),
        "exists": path.is_file(),
    }
    if path.is_file():
        record.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    else:
        record.update({"bytes": 0, "sha256": None})
    return record


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def quality_report(
    evidence: dict[str, Any], specification: dict[str, Any], generated_at: str
) -> dict[str, Any]:
    status_by_case = {
        item["case_id"]: item["status"] == "passed"
        for item in evidence["test_case_evidence"]
    }
    checks = [
        {
            "id": "CQ-001",
            "criterion": "精益完整性",
            "method": "验收标准与测试用例一对一覆盖审计",
            "passed": len(specification["test_cases"]) == len(CASES)
            and len(status_by_case) == len(CASES),
            "observed": {
                "specified_case_count": len(specification["test_cases"]),
                "evidence_case_count": len(status_by_case),
                "duplicate_case_ids": len(status_by_case) != len(evidence["test_case_evidence"]),
            },
        },
        {
            "id": "CQ-002",
            "criterion": "功能正确性",
            "method": "四层测试结果交叉审计",
            "passed": all(summary["failed"] == 0 for summary in evidence["stage_summaries"].values()),
            "observed": evidence["stage_summaries"],
        },
        {
            "id": "CQ-003",
            "criterion": "可测试性",
            "method": "非侵入式状态观察、QtTest 选择器、CTest 和系统探针审计",
            "passed": all(
                status_by_case.get(case_id, False)
                for case_id in (
                    "CI-STATIC-002",
                    "CI-UNIT-001",
                    "CI-INTEGRATION-001",
                    "CI-SYSTEM-001",
                )
            ),
            "observed": {
                "async_observation": status_by_case.get("CI-STATIC-002", False),
                "deterministic_unit_selector": status_by_case.get("CI-UNIT-001", False),
                "repeatable_integration_command": status_by_case.get("CI-INTEGRATION-001", False),
                "non_invasive_system_probe": status_by_case.get("CI-SYSTEM-001", False),
            },
        },
    ]
    passed_count = sum(item["passed"] for item in checks)
    return {
        "schema_version": "1.0",
        "report_type": "code_quality_assessment_report",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "quality_requirements": ["精益完整性", "功能正确性", "可测试性"],
        "root_cause_summary": [
            "最新远端 macOS 26 的 WindowBehaviorTests::testExitFullscreenActionUsesEscapePath 将 showEvent 之后已由 native full-size-content-view 归一化的窗口 client geometry 再次强行写成 QRect(220,180 720x500)，而 AppKit 退出全屏恢复 QRect(220,148 720x532)，导致跨版本的错误精确几何断言并放大清理期崩溃风险。",
            "此前远端没有外部 EPS 样本，矢量测试使用 8×4 fallback；6400% 下合法 vector tile 是 512×256，旧测试错误地要求 qMax(tile.width, tile.height)>512，该问题已改为等待真实绘制次数和非空 tile。",
            "此前 clang-tidy 构建复用了应用 target 的 Ghostscript POST_BUILD 打包钩子；该问题已通过 tidy 分支关闭可选 Ghostscript bundle 修复。",
            "补充的完整 CTest 暴露出平移 recorder 可能与异步 tile 完成重叠、120Hz maximum 可能包含系统调度抖动；两者已通过稳定观察点和抗抖动统计约束。",
        ],
        "repair_summary": [
            "build.sh 的 --tidy/--tidy-fix 显式关闭 FOVELLE_BUNDLE_GHOSTSCRIPT，普通构建默认保持开启。",
            "hasPendingVectorRefinement() 同时反映 interaction、active async request 和 queued async request；矢量测试轮询 vectorRenderCount 与非空 tile，并删除脆弱的 >512 尺寸假设。",
            "矢量平移测量在 recorder 前等待稳定 tile；120Hz probe 保留 maximum 诊断值，仅用平均值、P99 和 CPU 容量判定，避免单次系统调度抖动污染结果。",
            "全屏回归测试在 deferred native decoration 稳定后采集实际正常 client geometry，不再覆盖平台归一化基线；View 动作和 Escape 均等待原生退出完成后检查稳定性。",
        ],
        "research_trace": RESEARCH_TRACE,
        "checks": checks,
        "summary": {
            "total": len(checks),
            "passed": passed_count,
            "failed": len(checks) - passed_count,
            "status": "passed" if passed_count == len(checks) else "failed",
        },
        "evidence_file": "reports/test_evidence.json",
        "specification_file": "reports/test_case_specification.json",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--output-dir", type=Path, default=None)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    output_dir = (args.output_dir or repo / "reports").resolve()
    generated_at = utc_now()

    static_results = run_static(repo)
    unit_results = run_unit(repo, build_dir)
    integration_results = run_integration(repo, build_dir, args.skip_build)
    system_results = run_system(repo, build_dir)
    by_stage = {
        "static": static_results,
        "unit": unit_results,
        "integration": integration_results,
        "system": system_results,
    }

    evidence_items: list[dict[str, Any]] = []
    for specification in CASES:
        result = by_stage[specification["test_layer"]].get(
            specification["id"],
            {"passed": False, "observed": {"reason": "no result"}},
        )
        evidence_items.append(
            {
                "evidence_id": f"EV-{specification['id']}",
                "case_id": specification["id"],
                "stage": specification["test_layer"],
                "status": "passed" if result.get("passed") else "failed",
                "assertion": specification["atomic_acceptance_criterion"],
                "observed": result.get("observed", {}),
                "execution": result.get("execution", {"test_code": specification["test_code"]}),
                "recorded_at": utc_now(),
            }
        )

    stage_summaries: dict[str, dict[str, Any]] = {}
    for stage in STAGES:
        stage_items = [item for item in evidence_items if item["stage"] == stage]
        stage_summaries[stage] = {
            "total": len(stage_items),
            "passed": sum(item["status"] == "passed" for item in stage_items),
            "failed": sum(item["status"] != "passed" for item in stage_items),
            "status": "passed" if all(item["status"] == "passed" for item in stage_items) else "failed",
        }

    specification_report = {
        "schema_version": "1.0",
        "report_type": "test_case_specification",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "test_execution_order": list(STAGES),
        "atomicity_rule": "每个 test case 只验证一个可判定的原子验收标准；每项均记录测试目的、前置条件、输入数据、操作步骤、预期结果和后置条件。",
        "research_trace": RESEARCH_TRACE,
        "test_cases": CASES,
    }
    evidence_report = {
        "schema_version": "1.0",
        "report_type": "test_evidence",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "test_execution_order": list(STAGES),
        "research_trace": RESEARCH_TRACE,
        "stage_summaries": stage_summaries,
        "test_case_evidence": evidence_items,
    }
    quality = quality_report(evidence_report, specification_report, generated_at)

    write_json(output_dir / "test_case_specification.json", specification_report)
    write_json(output_dir / "test_evidence.json", evidence_report)
    write_json(output_dir / "code_quality_assessment_report.json", quality)

    passed_cases = sum(item["status"] == "passed" for item in evidence_items)
    completion = {
        "schema_version": "1.0",
        "report_type": "test_completion_report",
        "generated_at": generated_at,
        "task": "修复 Fovelle GitHub Actions 检查失败",
        "status": "passed" if passed_cases == len(CASES) else "failed",
        "execution_order": list(STAGES),
        "research_trace": RESEARCH_TRACE,
        "diagnosis": {
            "remote_run": "https://github.com/inostarlin-passion/Fovelle/actions/runs/32966331494",
            "build_run": "https://github.com/inostarlin-passion/Fovelle/actions/runs/32966331490",
            "commit": "87ffbe4",
            "observations": [
                "Run Unit Tests: WindowBehaviorTests::testExitFullscreenActionUsesEscapePath 在远端比较 QWidget::geometry() 时实际为 QRect(220,148 720x532)，预期为 QRect(220,180 720x500)；随后清理阶段出现 SIGSEGV。",
                "Build Fovelle: 同一 WindowBehaviorTests 失败，说明不是仅限独立测试 job 的环境差异。",
                "Run clang-tidy: passed。",
                "Run clang-format: passed。",
            ],
            "deduction": "macos-26 runner 标签有官方支持；当前剩余失败是测试把平台归一化前的 requested client rect 当成跨 macOS 稳定基线。捕获 deferred native decoration 完成后的实际 geometry 后，断言只验证退出路径的可重复恢复结果。",
        },
        "repairs": [
            {"path": "build.sh", "change": "tidy 分支关闭 Ghostscript bundle"},
            {"path": "src/qvgraphicsimageitem.h", "change": "暴露包含 active/queued async work 的 pending 观察点"},
            {"path": "src/qvgraphicsimageitem.cpp", "change": "实现异步 refinement 状态聚合"},
            {"path": "src/qvgraphicsview.cpp", "change": "委托新的 pending 观察点"},
            {"path": "tests/tst_qviewtests.cpp", "change": "等待稳定 painted vector tile，使用真实绘制观察点，并以 native decoration 稳定后的 geometry 验证全屏退出"},
            {"path": "tests/CMakeLists.txt", "change": "将 CTest 审计入口切换到当前 CI 质量流水线"},
        ],
        "stage_summaries": stage_summaries,
        "case_count": len(CASES),
        "passed_case_count": passed_cases,
        "failed_case_count": len(CASES) - passed_cases,
        "artifacts": [report_artifact(repo, f"reports/{name}") for name in REPORT_NAMES if name != "test_completion_report.json"],
        "self_artifact": {
            "path": "reports/test_completion_report.json",
            "absolute_path": str((output_dir / "test_completion_report.json").resolve()),
            "exists": True,
            "sha256": None,
            "hash_note": "Self-hash is null because this document contains the artifact manifest itself.",
        },
        "reproduction": {
            "build_command": "cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_BUILD_TRANSLATIONS=ON && cmake --build build --parallel 2",
            "command": "python3 tests/ci_quality_pipeline.py --repo . --build-dir build",
            "skip_build_command": "python3 tests/ci_quality_pipeline.py --repo . --build-dir build --skip-build",
        },
    }
    write_json(output_dir / "test_completion_report.json", completion)

    print(
        json.dumps(
            {
                "status": completion["status"],
                "stage_summaries": stage_summaries,
                "reports": [str(output_dir / name) for name in REPORT_NAMES],
            },
            ensure_ascii=False,
        )
    )
    return 0 if completion["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
