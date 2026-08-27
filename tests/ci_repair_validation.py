#!/usr/bin/env python3
"""Validate the CI repair and emit the required local machine-readable reports.

The script is intentionally not registered as a CTest or GitHub Actions gate.
It records the requested evidence on demand while the CI path remains limited
to the product test suites and the compiler checks.
"""

from __future__ import annotations

import argparse
import ast
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


STAGE_ORDER = ("static", "unit", "integration", "system")
REPORT_NAMES = (
    "test_case_specification.json",
    "test_completion_report.json",
    "code_quality_assessment_report.json",
)
REQUIRED_CASE_FIELDS = (
    "id",
    "atomic_acceptance_criterion",
    "test_layer",
    "test_code",
    "test_purpose",
    "preconditions",
    "input_data",
    "operation_steps",
    "expected_result",
    "postconditions",
)
EXPECTED_CTEST_TESTS = ("FovelleTests", "FovelleShortcutSettingsTests")
AUDIT_MARKERS = (
    "FovelleSettingsAudit",
    "FovelleTaskAcceptanceAudit",
    "settings_quality_pipeline.py",
    "ci_quality_pipeline.py",
)


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def command_result(
    command: list[str],
    cwd: Path,
    *,
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
            check=False,
            timeout=timeout,
        )
        output = result.stdout + result.stderr
        return {
            "command": command,
            "return_code": result.returncode,
            "passed": result.returncode == 0,
            "timed_out": False,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": output[-8000:],
        }
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        return {
            "command": command,
            "return_code": None,
            "passed": False,
            "timed_out": True,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_tail": (stdout + stderr)[-8000:],
        }
    except OSError as error:
        return {
            "command": command,
            "return_code": None,
            "passed": False,
            "timed_out": False,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "error": str(error),
            "output_tail": str(error),
        }


def check(identifier: str, passed: bool, actual: Any, expected: str) -> dict[str, Any]:
    return {
        "id": identifier,
        "passed": bool(passed),
        "actual": actual,
        "expected": expected,
    }


def result_snapshot(result: dict[str, Any]) -> dict[str, Any]:
    """Return a serializable command result without any later check fields."""
    return {key: value for key, value in result.items() if key != "checks"}


def static_gitignore_contract(repo: Path) -> dict[str, Any]:
    content = read_text(repo / ".gitignore")
    lines = {line.strip() for line in content.splitlines()}
    positive_rule = "reports/" in lines
    negation_rules = sorted(
        line for line in lines if line.startswith("!") and "reports/" in line
    )
    probe = command_result(
        ["git", "check-ignore", "--no-index", "-q", "reports/.ci-repair-probe"],
        repo,
        timeout=30,
    )
    checks = [
        check(
            "STATIC-REPORTS-IGNORED",
            positive_rule and not negation_rules and probe["passed"],
            {
                "reports_rule_present": positive_rule,
                "reports_negations": negation_rules,
                "check_ignore_return_code": probe["return_code"],
            },
            "reports/ is ignored as a directory and no exception re-includes a report file.",
        )
    ]
    return {
        "checks": checks,
        "passed": all(item["passed"] for item in checks),
    }


def static_ctest_contract(repo: Path) -> dict[str, Any]:
    content = read_text(repo / "tests/CMakeLists.txt")
    registered = re.findall(r"add_test\(\s*NAME\s+([A-Za-z0-9_-]+)", content)
    markers = {marker: marker in content for marker in AUDIT_MARKERS}
    checks = [
        check(
            "STATIC-CTEST-PRODUCT-SUITES",
            tuple(registered) == EXPECTED_CTEST_TESTS and not any(markers.values()),
            {"registered_tests": registered, "audit_markers": markers},
            "CTest registers only the two product test suites and no documentation/report audit.",
        )
    ]
    return {
        "checks": checks,
        "passed": all(item["passed"] for item in checks),
    }


def static_workflow_contract(repo: Path) -> dict[str, Any]:
    content = read_text(repo / ".github/workflows/test.yml")
    removed_markers = (
        "audit",
        "reports/",
        "upload-artifact@",
        "settings-audit-reports",
    )
    absent = {marker: marker not in content.lower() for marker in removed_markers}
    required = {
        "checkout": "actions/checkout@v4" in content,
        "build_tests": "-DBUILD_TESTS=ON" in content,
        "cmake_build": "cmake --build build" in content,
        "ctest": "ctest --test-dir build --output-on-failure --timeout 90" in content,
        "clang_tidy": "./build.sh --tidy" in content,
        "clang_format": "./build.sh --format-check" in content,
    }
    checks = [
        check(
            "STATIC-ACTIONS-CI-PATH",
            all(absent.values()) and all(required.values()),
            {"removed_markers_absent": absent, "required_product_steps": required},
            "The workflow keeps checkout/build/test/compiler checks and removes the report-audit step.",
        )
    ]
    return {
        "checks": checks,
        "passed": all(item["passed"] for item in checks),
    }


def static_python_contract(repo: Path) -> dict[str, Any]:
    script_path = repo / "tests/ci_repair_validation.py"
    try:
        ast.parse(read_text(script_path), filename=str(script_path))
        error = None
    except (OSError, SyntaxError) as exception:
        error = str(exception)
    checks = [
        check(
            "STATIC-VALIDATION-SCRIPT",
            error is None,
            {"path": str(script_path.relative_to(repo)), "syntax_error": error},
            "The executable validation/test source parses successfully.",
        )
    ]
    return {
        "checks": checks,
        "passed": all(item["passed"] for item in checks),
    }


def static_stage(repo: Path) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    for result in (
        static_gitignore_contract(repo),
        static_ctest_contract(repo),
        static_workflow_contract(repo),
        static_python_contract(repo),
    ):
        checks.extend(result["checks"])
    return {
        "stage": "static",
        "passed": all(item["passed"] for item in checks),
        "checks": checks,
    }


def unit_stage(repo: Path, build_dir: Path) -> dict[str, Any]:
    result = command_result(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "--output-on-failure",
            "--timeout",
            "90",
            "-R",
            "^(FovelleTests|FovelleShortcutSettingsTests)$",
        ],
        repo,
        environment={"QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1"},
        timeout=210.0,
    )
    result["stage"] = "unit"
    result["checks"] = [
        check(
            "UNIT-PRODUCT-SUITES",
            result["passed"],
            result_snapshot(result),
            "Both product QtTest suites return zero with no failing test process.",
        )
    ]
    return result


def integration_stage(repo: Path, build_dir: Path) -> dict[str, Any]:
    result = command_result(
        ["ctest", "--test-dir", str(build_dir), "-N"],
        repo,
        timeout=30.0,
    )
    output = result["output_tail"]
    listed_tests = re.findall(r"Test #\d+: ([A-Za-z0-9_-]+)", output)
    registration_check = check(
        "INTEGRATION-CTEST-DISCOVERY",
        result["passed"] and tuple(listed_tests) == EXPECTED_CTEST_TESTS,
        {"listed_tests": listed_tests, "command_result": result_snapshot(result)},
        "The configured build exposes exactly the two product test suites to CTest.",
    )
    result["stage"] = "integration"
    result["checks"] = [registration_check]
    result["passed"] = result["passed"] and registration_check["passed"]
    return result


def system_stage(repo: Path, build_dir: Path) -> dict[str, Any]:
    app = build_dir / "Fovelle.app" / "Contents" / "MacOS" / "Fovelle"
    expected_version = "1.0.1"
    version_result = command_result(
        [str(app), "--version"],
        repo,
        environment={"QT_QPA_PLATFORM": "cocoa"},
        timeout=30.0,
    )
    probe_result = command_result(
        [str(app)],
        repo,
        environment={
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "FOVELLE_SYSTEM_PROBE": "1",
        },
        timeout=30.0,
    )
    version_output = version_result["output_tail"].strip()
    probe_output = probe_result["output_tail"]
    checks = [
        check(
            "SYSTEM-APP-VERSION",
            version_result["passed"] and version_output == f"Fovelle {expected_version}",
            {"version_output": version_output, "command_result": result_snapshot(version_result)},
            "The built macOS application starts its CLI path and reports version 1.0.1.",
        ),
        check(
            "SYSTEM-COCOA-PROBE",
            probe_result["passed"]
            and re.search(r"FOVELLE_SYSTEM_PROBE windows=\d+ maximized=(true|false)", probe_output)
            is not None,
            {"probe_output": probe_output, "command_result": result_snapshot(probe_result)},
            "The built app starts a Cocoa window and exits through the deterministic system probe.",
        ),
    ]
    return {
        "stage": "system",
        "passed": all(item["passed"] for item in checks),
        "checks": checks,
    }


def make_case(
    identifier: str,
    criterion: str,
    layer: str,
    test_code: str,
    purpose: str,
    preconditions: list[str],
    input_data: dict[str, Any],
    steps: list[str],
    expected: str,
    postconditions: list[str],
) -> dict[str, Any]:
    return {
        "id": identifier,
        "atomic_acceptance_criterion": criterion,
        "test_layer": layer,
        "test_code": test_code,
        "test_purpose": purpose,
        "preconditions": preconditions,
        "input_data": input_data,
        "operation_steps": steps,
        "expected_result": expected,
        "postconditions": postconditions,
    }


CASES = (
    make_case(
        "CI-REPAIR-001",
        "Git 必须忽略 reports/ 目录，且不得保留报告例外规则。",
        "static",
        "tests/ci_repair_validation.py::static_gitignore_contract",
        "验证生成式审计报告不会重新成为版本控制输入。",
        ["仓库根目录和 .gitignore 可读。"],
        {"path_rule": "reports/", "probe": "reports/.ci-repair-probe"},
        ["读取 .gitignore。", "用 git check-ignore --no-index 检查 reports 下的探针路径。"],
        "reports/ 规则存在，探针被忽略，且没有 reports/ 的 ! 例外。",
        ["不创建持久化探针文件。"],
    ),
    make_case(
        "CI-REPAIR-002",
        "CTest 只能注册产品测试套件，不得注册文档/报告审计。",
        "static",
        "tests/ci_repair_validation.py::static_ctest_contract",
        "验证非产品审计不会阻断 GitHub Actions 的产品测试路径。",
        ["tests/CMakeLists.txt 可读。"],
        {"expected_ctest_tests": list(EXPECTED_CTEST_TESTS), "forbidden_markers": list(AUDIT_MARKERS)},
        ["解析 add_test(NAME ...) 注册项。", "搜索 Python 审计目标和脚本标记。"],
        "只有 FovelleTests 和 FovelleShortcutSettingsTests 被注册。",
        ["不修改构建目录。"],
    ),
    make_case(
        "CI-REPAIR-003",
        "Checks workflow 必须保留构建、CTest、clang-tidy 和 clang-format，并删除报告审计上传。",
        "static",
        "tests/ci_repair_validation.py::static_workflow_contract",
        "验证 CI 功能检查仍在，而文档审计副作用已从工作流删除。",
        [".github/workflows/test.yml 可读。"],
        {"workflow": ".github/workflows/test.yml", "forbidden": ["reports/", "upload-artifact@", "audit"]},
        ["检查必需的产品步骤。", "检查报告上传、reports 路径和 audit 标记均不存在。"],
        "产品构建/测试/编译器检查存在，文档审计步骤不存在。",
        ["不触发远端工作流。"],
    ),
    make_case(
        "CI-REPAIR-004",
        "正常 Qt 单元测试套件必须在 Cocoa 环境下通过。",
        "unit",
        "tests/ci_repair_validation.py::unit_stage",
        "验证移除审计后产品功能测试仍可独立运行。",
        ["fovelle_tests 已由 CMake 构建。", "Cocoa Qt 平台可用。"],
        {"environment": {"QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1"}, "suites": list(EXPECTED_CTEST_TESTS)},
        ["按正则只选择两个产品 CTest 套件。", "记录返回码、超时、耗时和输出尾部。"],
        "两个产品测试套件返回 0。",
        ["测试进程结束；不改变用户配置。"],
    ),
    make_case(
        "CI-REPAIR-005",
        "CMake 配置生成的 CTest 清单必须与产品测试套件一致。",
        "integration",
        "tests/ci_repair_validation.py::integration_stage",
        "验证 CMake 到 CTest 的集成边界没有残留审计目标。",
        ["构建目录已成功配置。"],
        {"command": ["ctest", "--test-dir", "build", "-N"]},
        ["读取 CTest 的 dry-run 清单。", "比较列出的测试名与预期产品测试集合。"],
        "CTest 清单精确列出两个产品测试套件。",
        ["不执行额外测试，不生成报告文件以外的仓库文件。"],
    ),
    make_case(
        "CI-REPAIR-006",
        "构建出的 macOS 应用必须能报告版本并通过 Cocoa 系统探针启动/退出。",
        "system",
        "tests/ci_repair_validation.py::system_stage",
        "验证最终应用产物的最小系统启动契约。",
        ["Fovelle.app 已成功构建。", "macOS Cocoa 桌面会话可用。"],
        {"version_command": "Fovelle --version", "probe_environment": {"FOVELLE_SYSTEM_PROBE": "1"}},
        ["执行 --version 并匹配 Fovelle 1.0.1。", "启动系统探针并匹配 windows/maximized 标记。"],
        "版本输出正确，Cocoa 探针返回 0 且输出结构化状态标记。",
        ["探针退出；不保留应用进程。"],
    ),
    make_case(
        "CI-REPAIR-007",
        "三份指定报告必须是可解析 JSON，并分别记录规格、完成结果和质量评估。",
        "static",
        "tests/ci_repair_validation.py::validate_reports",
        "验证外部输出可由机器重复读取和审计。",
        ["验证脚本已完成四级执行。"],
        {"reports": list(REPORT_NAMES)},
        ["解析三份 JSON。", "检查 report_type、case 字段、stage_order 和质量条目。"],
        "三份报告存在、JSON 可解析、原子用例字段完整且所有结果一致为 passed。",
        ["报告写入 reports/ 并由 .gitignore 忽略。"],
    ),
)


def validate_cases() -> dict[str, Any]:
    ids = [case["id"] for case in CASES]
    missing = {
        case["id"]: [field for field in REQUIRED_CASE_FIELDS if not case.get(field)]
        for case in CASES
        if any(not case.get(field) for field in REQUIRED_CASE_FIELDS)
    }
    return {
        "case_count": len(CASES),
        "unique_ids": len(ids) == len(set(ids)),
        "all_required_fields_present": not missing,
        "missing_fields": missing,
        "known_layers": sorted({case["test_layer"] for case in CASES}),
        "all_layers_known": all(case["test_layer"] in STAGE_ORDER for case in CASES),
    }


def validate_reports(output_dir: Path, completion: dict[str, Any], quality: dict[str, Any]) -> dict[str, Any]:
    observations: list[dict[str, Any]] = []
    specification_path = output_dir / REPORT_NAMES[0]
    completion_path = output_dir / REPORT_NAMES[1]
    quality_path = output_dir / REPORT_NAMES[2]
    try:
        specification = json.loads(specification_path.read_text(encoding="utf-8"))
        specification_error = None
    except (OSError, json.JSONDecodeError) as error:
        specification = {}
        specification_error = str(error)
    try:
        stored_completion = json.loads(completion_path.read_text(encoding="utf-8"))
        completion_error = None
    except (OSError, json.JSONDecodeError) as error:
        stored_completion = {}
        completion_error = str(error)
    try:
        stored_quality = json.loads(quality_path.read_text(encoding="utf-8"))
        quality_error = None
    except (OSError, json.JSONDecodeError) as error:
        stored_quality = {}
        quality_error = str(error)
    required_case_fields = all(
        all(field in case for field in REQUIRED_CASE_FIELDS)
        for case in specification.get("cases", [])
    )
    observations.extend(
        [
            check(
                "REPORT-SPECIFICATION",
                not specification_error
                and specification.get("report_type") == "atomic_test_case_specification"
                and specification.get("passed") is True
                and specification.get("cases")
                and required_case_fields,
                {
                    "error": specification_error,
                    "report_type": specification.get("report_type"),
                    "case_count": len(specification.get("cases", [])),
                    "required_case_fields": required_case_fields,
                },
                "The test specification is parseable and contains complete atomic cases.",
            ),
            check(
                "REPORT-COMPLETION",
                not completion_error
                and stored_completion.get("report_type") == "test_completion_report"
                and stored_completion.get("stage_order") == list(STAGE_ORDER)
                and stored_completion.get("passed") is True,
                {
                    "error": completion_error,
                    "report_type": stored_completion.get("report_type"),
                    "stage_order": stored_completion.get("stage_order"),
                },
                "The completion report records the ordered four-stage result and passes.",
            ),
            check(
                "REPORT-QUALITY",
                not quality_error
                and stored_quality.get("report_type") == "code_quality_assessment_report"
                and stored_quality.get("passed") is True
                and all(item.get("passed") is True for item in stored_quality.get("quality_requirements", [])),
                {
                    "error": quality_error,
                    "report_type": stored_quality.get("report_type"),
                    "quality_requirements": stored_quality.get("quality_requirements", []),
                },
                "The quality report is parseable and all required quality dimensions pass.",
            ),
        ]
    )
    return {
        "passed": all(item["passed"] for item in observations)
        and completion.get("passed") is True
        and quality.get("passed") is True,
        "checks": observations,
    }


def build_reports(
    repo: Path,
    build_dir: Path,
    output_dir: Path,
    preparation: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    generated_at = now_utc()
    static = static_stage(repo)
    unit = unit_stage(repo, build_dir)
    integration = integration_stage(repo, build_dir)
    system = system_stage(repo, build_dir)
    stages = {item["stage"]: item for item in (static, unit, integration, system)}
    validation = validate_cases()
    case_results = []
    for case in CASES:
        stage_status = {
            stage: stages[stage]["passed"]
            for stage in {case["test_layer"]}
            if stage in stages
        }
        case_results.append(
            {
                "id": case["id"],
                "atomic_acceptance_criterion": case["atomic_acceptance_criterion"],
                "test_layer": case["test_layer"],
                "stage_status": stage_status,
                "passed": bool(stage_status) and all(stage_status.values()),
            }
        )
    all_stages_passed = all(stages[stage]["passed"] for stage in STAGE_ORDER)
    all_cases_passed = all(case["passed"] for case in case_results)
    specification = {
        "schema_version": "1.0",
        "report_type": "atomic_test_case_specification",
        "generated_at_utc": generated_at,
        "task": "修复 GitHub Actions 检查：移除文档报告审计并忽略 reports/",
        "stage_order": list(STAGE_ORDER),
        "required_case_fields": list(REQUIRED_CASE_FIELDS),
        "cases": list(CASES),
        "validation": validation,
        "passed": bool(
            validation["unique_ids"]
            and validation["all_required_fields_present"]
            and validation["all_layers_known"]
        ),
    }
    completion = {
        "schema_version": "1.0",
        "report_type": "test_completion_report",
        "generated_at_utc": generated_at,
        "task": specification["task"],
        "build_preparation": preparation,
        "stage_order": list(STAGE_ORDER),
        "stages": stages,
        "cases": case_results,
        "audit": {
            "case_count": len(case_results),
            "passed_cases": sum(case["passed"] for case in case_results),
            "failed_cases": [case["id"] for case in case_results if not case["passed"]],
            "all_atomic_cases_passed": all_cases_passed,
            "all_four_stages_passed": all_stages_passed,
            "stage_order_is_explicit": list(stages) == list(STAGE_ORDER),
        },
    }
    completion["passed"] = bool(
        specification["passed"]
        and all_cases_passed
        and all_stages_passed
        and completion["audit"]["stage_order_is_explicit"]
    )
    completion["status"] = "passed" if completion["passed"] else "failed"
    quality_requirements = [
        {
            "id": "CQ-LEAN-001",
            "criterion": "精益完整性",
            "passed": static["passed"],
            "evidence": "CI 仅保留产品测试和编译器检查；报告目录是生成物且不进入版本控制。",
        },
        {
            "id": "CQ-CORRECT-001",
            "criterion": "功能正确性",
            "passed": all_cases_passed,
            "evidence": "产品 QtTest、CTest 集成发现和 Cocoa 应用探针均通过，且移除审计不改变产品测试套件。",
        },
        {
            "id": "CQ-TESTABLE-001",
            "criterion": "可测试性",
            "passed": specification["passed"] and completion["passed"],
            "evidence": "每个原子用例包含目的、前置条件、输入、步骤、预期和后置条件；命令、返回码、超时、耗时和输出尾部均被记录。",
        },
    ]
    quality = {
        "schema_version": "1.0",
        "report_type": "code_quality_assessment_report",
        "generated_at_utc": generated_at,
        "task": specification["task"],
        "quality_requirements": quality_requirements,
        "explicit_assumptions": [
            "远端失败 run 的可观测直接边界是 Run Unit Tests 的 Run tests 步骤；产品源代码未因本次配置清理而改变。",
            "reports/ 下的 JSON 是本地/运行时证据，不是产品源文件；忽略目录不会影响 CTest 或应用构建。",
            "在当前 macOS Cocoa 会话中执行的系统探针仅验证应用启动/退出和结构化状态输出，不替代远端 macOS-26 重跑。",
        ],
        "research_trace": [
            {
                "hop": 1,
                "source": "https://github.com/inostarlin-passion/Fovelle/actions/runs/33110540912",
                "finding": "The Checks run for commit 119f5fe reports Run Unit Tests as failed while clang-format and clang-tidy succeeded.",
                "explicit_premise": "The run summary and Checks API job metadata identify the failed job/step boundary.",
                "deduction": "The first repair boundary is the test execution graph, not a product rendering change.",
            },
            {
                "hop": 2,
                "source": "https://docs.github.com/en/actions/how-tos/monitor-workflows/use-workflow-run-logs",
                "finding": "GitHub documents diagnosing a workflow by locating the failed step and reviewing its build logs; a workflow run has check runs for jobs and steps.",
                "explicit_premise": "A failed job step is a distinct CI predicate from successful sibling jobs.",
                "deduction": "The failed CTest path can be isolated without weakening clang-tidy or clang-format.",
            },
            {
                "hop": 3,
                "source": "https://docs.github.com/en/actions/tutorials/store-and-share-data",
                "finding": "upload-artifact archives selected generated files and is an optional workflow step.",
                "explicit_premise": "The report artifact is not required to compile or execute Fovelle.",
                "deduction": "Removing the report upload step removes a non-product side effect without removing product tests.",
            },
            {
                "hop": 4,
                "source": "https://git-scm.com/docs/gitignore",
                "finding": "A gitignore pattern applies to intentionally untracked files; already tracked files require git rm --cached to stop tracking.",
                "explicit_premise": "reports/ contains generated evidence rather than source inputs.",
                "deduction": "The correct repository contract is reports/ plus removal of the old exception; tracked legacy reports must be untracked separately.",
            },
            {
                "hop": 5,
                "source": "local:tests/CMakeLists.txt and .github/workflows/test.yml",
                "finding": "The prior configuration registered FovelleSettingsAudit and FovelleTaskAcceptanceAudit and uploaded reports/*.json.",
                "explicit_premise": "Those targets are not the product QtTest suites.",
                "deduction": "Deleting their CTest registration and workflow upload is the minimal configuration fix.",
            },
        ],
        "audit": {
            "all_quality_requirements_passed": all(item["passed"] for item in quality_requirements),
            "specification_valid": specification["passed"],
            "test_completion_valid": completion["passed"],
        },
    }
    quality["passed"] = all(item["passed"] for item in quality_requirements)
    quality["status"] = "passed" if quality["passed"] else "failed"
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / REPORT_NAMES[0]).write_text(
        json.dumps(specification, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (output_dir / REPORT_NAMES[1]).write_text(
        json.dumps(completion, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (output_dir / REPORT_NAMES[2]).write_text(
        json.dumps(quality, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    report_validation = validate_reports(output_dir, completion, quality)
    completion["report_validation"] = report_validation
    completion["passed"] = completion["passed"] and report_validation["passed"]
    completion["status"] = "passed" if completion["passed"] else "failed"
    quality["audit"]["test_completion_valid"] = completion["passed"]
    quality["passed"] = quality["passed"] and report_validation["passed"]
    quality["status"] = "passed" if quality["passed"] else "failed"
    (output_dir / REPORT_NAMES[1]).write_text(
        json.dumps(completion, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (output_dir / REPORT_NAMES[2]).write_text(
        json.dumps(quality, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return specification, completion, quality


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    output_dir = (args.output_dir or repo / "reports").resolve()

    preparation: dict[str, Any] = {"skipped": args.skip_build, "passed": True, "commands": []}
    if not args.skip_build:
        configure = command_result(
            [
                "cmake",
                "-S",
                str(repo),
                "-B",
                str(build_dir),
                "-DBUILD_TESTS=ON",
                "-DFOVELLE_BUILD_TRANSLATIONS=ON",
                "-DQV_DISABLE_ONLINE_VERSION_CHECK=ON",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0",
            ],
            repo,
            timeout=180.0,
        )
        build = command_result(
            ["cmake", "--build", str(build_dir), "--parallel", "2"],
            repo,
            timeout=300.0,
        )
        preparation["commands"] = [configure, build]
        preparation["passed"] = configure["passed"] and build["passed"]

    if not preparation["passed"]:
        print(json.dumps({"preparation": preparation}, ensure_ascii=False, indent=2))
        return 1

    specification, completion, quality = build_reports(
        repo, build_dir, output_dir, preparation
    )
    summary = {
        "specification_passed": specification["passed"],
        "completion_passed": completion["passed"],
        "quality_passed": quality["passed"],
        "case_count": len(specification["cases"]),
        "stage_order": completion["stage_order"],
        "reports": [str(output_dir / name) for name in REPORT_NAMES],
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if all(
        (summary["specification_passed"], summary["completion_passed"], summary["quality_passed"])
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
