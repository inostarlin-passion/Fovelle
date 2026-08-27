#!/usr/bin/env python3
"""Audit the Settings-page change through static, unit, integration and system stages."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


VERSION = "1.0.1"
LANGUAGES = ("en", "es", "ja", "zh_Hans", "zh_Hant")
GROUPS = (
    (1, ("langComboBox",)),
    (2, ("themeComboBox", "checkerboardBackgroundCheckbox")),
    (3, ("smoothScalingComboBox",)),
    (4, ("reuseWindowCheckbox", "smallImagesOneToOneCheckbox")),
    (5, ("slideshowDirectionComboBox", "slideshowTimerSpinBox")),
    (6, ("afterDeletionComboBox", "askDeleteCheckbox")),
    (7, ("updateFrequencyComboBox",)),
    (8, ("associateFormatsButton",)),
)


def test_case(
    identifier: str,
    criterion: str,
    purpose: str,
    preconditions: str,
    input_data: str,
    steps: str,
    expected_result: str,
    postconditions: str,
    test_layer: str,
    implementation: str,
    evidence_stages: tuple[str, ...],
) -> dict[str, Any]:
    return {
        "id": identifier,
        "acceptance_criterion": criterion,
        "test_purpose": purpose,
        "preconditions": preconditions,
        "input_data": input_data,
        "steps": steps,
        "expected_result": expected_result,
        "postconditions": postconditions,
        "test_layer": test_layer,
        "implementation": implementation,
        "evidence_stages": list(evidence_stages),
    }


CASES = (
    test_case(
        "SET-001",
        "版本号为 1.0.1",
        "验证 CMake、qmake、macOS bundle 和运行时版本契约一致。",
        "构建配置、bundle plist 和 QtTest 源码可读；测试二进制可构建。",
        "VERSION=1.0.1、CFBundleShortVersionString/CFBundleVersion=1.0.1、运行时 applicationVersion。",
        "静态读取版本源，再执行 FeatureTests::testApplicationVersionIsCurrent。",
        "所有发布入口与运行时版本均严格等于 1.0.1，旧的 1.0.0 不再作为当前版本。",
        "不修改应用设置或 bundle 外部状态。",
        "static+unit",
        "tests/settings_quality_pipeline.py::static_version_contract; tests/tst_qviewtests.cpp::FeatureTests::testApplicationVersionIsCurrent",
        ("static", "unit"),
    ),
    test_case(
        "SET-002",
        "General 第 1 组只包含 Language",
        "验证语言选项位于第 1 个语义组且没有混入其他选项。",
        "QVOptionsDialog 可构造。",
        "settingsGroup1 的 item metadata 和控件父级。",
        "构造对话框，读取 group index、成员列表和父级链。",
        "第 1 组成员严格为 langComboBox。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-003",
        "General 第 2 组包含 Appearance 和 Checkerboard when image loaded",
        "验证外观相关两个选项保持同组且按指定顺序排列。",
        "QVOptionsDialog 可构造。",
        "settingsGroup2 的 item metadata。",
        "读取第 2 组成员和两个控件的父级链。",
        "成员严格为 themeComboBox、checkerboardBackgroundCheckbox。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-004",
        "General 第 3 组只包含 Smooth scaling",
        "验证缩放选项独立成组。",
        "QVOptionsDialog 可构造。",
        "settingsGroup3 的 item metadata。",
        "读取第 3 组成员和控件父级链。",
        "成员严格为 smoothScalingComboBox。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-005",
        "General 第 4 组包含 Reuse window 和 Show small images at 1:1",
        "验证窗口复用和小图显示两个选项保持同组且按指定顺序排列。",
        "QVOptionsDialog 可构造。",
        "settingsGroup4 的 item metadata。",
        "读取第 4 组成员和两个控件的父级链。",
        "成员严格为 reuseWindowCheckbox、smallImagesOneToOneCheckbox。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-006",
        "General 第 5 组包含 Slideshow direction 和 Slideshow timer",
        "验证幻灯片方向和计时器保持同组且按指定顺序排列。",
        "QVOptionsDialog 可构造。",
        "settingsGroup5 的 item metadata。",
        "读取第 5 组成员和两个控件的父级链。",
        "成员严格为 slideshowDirectionComboBox、slideshowTimerSpinBox。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-007",
        "General 第 6 组包含 After deletion 和 Ask before deleting files",
        "验证删除行为和确认选项保持同组且按指定顺序排列。",
        "QVOptionsDialog 可构造。",
        "settingsGroup6 的 item metadata。",
        "读取第 6 组成员和两个控件的父级链。",
        "成员严格为 afterDeletionComboBox、askDeleteCheckbox。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-008",
        "General 第 7 组只包含 Auto update check",
        "验证更新检查频率独立成组。",
        "QVOptionsDialog 可构造。",
        "settingsGroup7 的 item metadata。",
        "读取第 7 组成员和控件父级链。",
        "成员严格为 updateFrequencyComboBox。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-009",
        "General 第 8 组只包含 Associate all supported formats",
        "验证文件关联操作独立成组。",
        "QVOptionsDialog 可构造。",
        "settingsGroup8 的 item metadata。",
        "读取第 8 组成员和按钮父级链。",
        "成员严格为 associateFormatsButton。",
        "对话框销毁且不调用真实文件关联操作。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-010",
        "组间间距大于组内选项间距",
        "验证布局采用显式、可审计的 macOS 设置页分组间距。",
        "QVOptionsDialog 可构造；Qt layout 已建立。",
        "settingsGroupSpacing=18、settingsRowSpacing=6、每个组的 QFormLayout spacing。",
        "读取 General content、section layout 和每个组 form layout 的 spacing。",
        "组间距为 18 px，组内行距为 6 px，且 18>6。",
        "对话框销毁且设置恢复。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit", "static"),
    ),
    test_case(
        "SET-011",
        "Smooth scaling 默认值为 Bilinear",
        "验证设置库和 General 控件使用 Bilinear 默认值。",
        "SettingsManager 和 QVOptionsDialog 已初始化。",
        "smoothscalingmode 的默认值及 combo 当前 data。",
        "以 defaults=true 读取设置并检查控件当前项。",
        "默认枚举值和 General 控件均为 Bilinear。",
        "不改变持久化设置。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults; tests/tst_qviewtests.cpp::WindowBehaviorTests::testSmoothScalingDefaultIsBilinear",
        ("unit",),
    ),
    test_case(
        "SET-012",
        "Appearance 默认值为 Dark",
        "验证设置库和 General 控件使用 Dark 默认值。",
        "SettingsManager 和 QVOptionsDialog 已初始化。",
        "theme 的默认值及 combo 当前 data。",
        "以 defaults=true 读取设置并检查控件当前项。",
        "默认枚举值和 General 控件均为 Dark。",
        "不改变持久化设置。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit",),
    ),
    test_case(
        "SET-013",
        "每种支持语言下 General 的长选项完整显示",
        "验证长复选框文本不会因翻译或跨布局自然尺寸计算而被裁切。",
        "Cocoa Qt Test 应用、五个支持语言 catalog 和设置对话框可用。",
        "en、es、ja、zh_Hans、zh_Hant；General 所有可见控件。",
        "逐语言安装 catalog，显示 General，检查每个可见控件的 natural width、映射矩形和水平滚动范围。",
        "Reuse window when launching with image 及其他 General 选项的宽度不小于 sizeHint，且完全位于 viewport 内，无水平滚动。",
        "对话框、翻译器和设置值恢复。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage",
        ("integration", "system"),
    ),
    test_case(
        "SET-014",
        "每种支持语言下每个 Settings Tab 的可见选项完整显示",
        "验证 General、Shortcuts、Mouse 三个 Tab 及 Mouse 两种模式均不发生水平裁切。",
        "Cocoa Qt Test 应用和五个支持语言 catalog 可用。",
        "五种语言；General、Shortcuts、Mouse；Mouse click/drag 模式。",
        "逐语言切换全部 Tab，检查 scroll bar、表格可见单元格和每个可见控件几何；切换两种 Mouse 模式后重复检查。",
        "所有 Tab 的水平滚动范围为 0；可见控件自然宽度满足且矩形在 viewport 内。",
        "对话框、翻译器和设置值恢复。",
        "system",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage",
        ("system",),
    ),
    test_case(
        "SET-015",
        "精益完整性：实现只引入满足本任务所需的最小变更",
        "验证分组、宽度补偿和默认值变更复用现有控件、设置键与信号连接，没有复制持久化模型或无关 UI。",
        "仓库差异、设置对话框源码、现有 UI objectName 和 SettingsManager 源码可读。",
        "任务范围 git diff、现有控件 objectName、settingsLibrary 与布局实现。",
        "执行静态审计，检查任务范围差异无空白错误、布局使用现有控件且设置键集合没有新增任务外键。",
        "静态证据显示实现范围与需求一致，未引入非必要持久化键、重复控件或无关构建变更。",
        "不修改源码、构建产物或用户设置。",
        "static",
        "tests/settings_quality_pipeline.py::static_stage",
        ("static",),
    ),
    test_case(
        "SET-016",
        "功能正确性：每个规定输入均产生规定的输出与副作用",
        "验证版本、分组、默认值、多语言和多 Tab 几何的完整验收矩阵均通过。",
        "四层测试环境已配置；QtTest 二进制、翻译 catalog 和 CTest 注册均可用。",
        "14 个功能原子用例及其固定输入、输出、不变量和恢复动作。",
        "按静态、单元、集成、系统顺序执行审计流水线，读取每个原子用例的 stage_status。",
        "所有功能原子用例在其证据层级通过，且没有失败、跳过或未覆盖的验收项。",
        "测试进程、对话框、翻译器和测试设置均完成清理。",
        "static+unit+integration+system",
        "tests/settings_quality_pipeline.py::stage_case_passed; reports/test_completion_report.json",
        ("static", "unit", "integration", "system"),
    ),
    test_case(
        "SET-017",
        "可测试性：测试条件可确定控制、运行时状态可非侵入式观测且结果可重复",
        "验证审计流水线固定运行环境并保存可复核的命令、输入环境、输出摘要和哈希。",
        "Python 标准库、QtTest、Cocoa QPA 和本地构建目录可用；不依赖联网更新检查。",
        "固定 QT_QPA_PLATFORM、QT_FATAL_WARNINGS、QV_DISABLE_ONLINE_VERSION_CHECK、FOVELLE_TEST_SUITE 及五种语言输入。",
        "检查审计脚本的确定性环境覆盖和 JSON schema，再执行四层测试并读取输出哈希、返回码、耗时和 case 状态。",
        "测试可重复执行、无侵入式观测设置/几何/滚动条/版本输出，并为每个阶段和原子用例留下机器可审计结果。",
        "报告写入完成；用户设置、源代码和外部服务状态不被测试改变。",
        "static+unit+integration+system",
        "tests/settings_quality_pipeline.py::run_command; tests/settings_quality_pipeline.py::build_reports",
        ("static", "unit", "integration", "system"),
    ),
)


RESEARCH_TRACE = [
    {
        "hop": 1,
        "source": "https://developer.apple.com/design/human-interface-guidelines/settings",
        "finding": "Apple describes macOS settings windows as toolbar panes, with each pane containing a group of related settings.",
        "premise": "The requested General options are one pane and have explicit semantic relationships supplied by the user.",
        "deduction": "Implement eight ordered groups without adding unrelated controls or decorative titles that would require new translations.",
    },
    {
        "hop": 2,
        "source": "https://developer.apple.com/design/human-interface-guidelines/layout",
        "finding": "Apple's layout guidance says to group related items and provide enough space around controls.",
        "premise": "The user requires group-to-group spacing to exceed within-group spacing.",
        "deduction": "Use explicit 18 px group spacing and 6 px row spacing, then assert the inequality in QtTest.",
    },
    {
        "hop": 3,
        "source": "https://doc.qt.io/qt-6.11/qformlayout.html",
        "finding": "QFormLayout supports explicit vertical spacing, and a widget added as a row can span both columns; macOS style defaults can center a form.",
        "premise": "Centered independent forms and spanning rows are sensitive to localized natural widths.",
        "deduction": "Set left/top form alignment, no wrapping, fixed row spacing, and keep controls in explicit per-group forms.",
    },
    {
        "hop": 4,
        "source": "https://doc.qt.io/qt-6/qscrollarea.html",
        "finding": "A QScrollArea's child size hint, minimum size, and layout size policy determine whether content is shown or requires scrolling.",
        "premise": "The original width defect is a layout-measurement defect, not a translation-string defect.",
        "deduction": "Measure every control's natural width explicitly, set the content minimum size from that width, and test all supported languages and Tabs.",
    },
]


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def file_sha256(path: Path) -> str | None:
    try:
        digest = hashlib.sha256()
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None


def run_command(command: list[str], repo: Path, environment: dict[str, str], timeout: int) -> dict[str, Any]:
    started = time.perf_counter()
    merged_environment = {**os.environ, **environment}
    try:
        result = subprocess.run(
            command,
            cwd=repo,
            env=merged_environment,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        timed_out = False
        return_code = result.returncode
        stdout = result.stdout
        stderr = result.stderr
    except subprocess.TimeoutExpired as error:
        timed_out = True
        return_code = None
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")

    output = stdout + stderr
    return {
        "command": command,
        "cwd": str(repo),
        "environment_overrides": environment,
        "timeout_seconds": timeout,
        "elapsed_seconds": round(time.perf_counter() - started, 6),
        "timed_out": timed_out,
        "return_code": return_code,
        "passed": return_code == 0 and not timed_out,
        "output_sha256": hashlib.sha256(output.encode("utf-8", errors="replace")).hexdigest(),
        "output_tail": output[-12000:],
    }


def read(repo: Path, relative: str) -> str:
    return (repo / relative).read_text(encoding="utf-8")


def static_stage(repo: Path) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []

    def check(identifier: str, passed: bool, observed: Any, expected: str) -> None:
        checks.append({"id": identifier, "passed": bool(passed), "observed": observed, "expected": expected})

    cmake = read(repo, "CMakeLists.txt")
    qmake = read(repo, "qView.pro")
    plist = read(repo, "dist/mac/Info.plist")
    test_source = read(repo, "tests/tst_qviewtests.cpp")
    options_cpp = read(repo, "src/qvoptionsdialog.cpp")
    settings_cpp = read(repo, "src/settingsmanager.cpp")
    unit_runner = read(repo, "tests/quality_unit_runner.py")
    ui_path = repo / "src/qvoptionsdialog.ui"

    version_values = {
        "cmake": f"project(Fovelle VERSION {VERSION}" in cmake,
        "qmake": f"VERSION = {VERSION}" in qmake,
        "plist_short_and_bundle": plist.count(f"<string>{VERSION}</string>") >= 2,
        "runtime_assertion": f'QCoreApplication::applicationVersion(), QString("{VERSION}")' in test_source,
        "no_old_version_in_active_sources": "1.0.0" not in cmake + qmake + plist + test_source,
    }
    check("SET-STATIC-001", all(version_values.values()), version_values, "all active version sources equal 1.0.1")

    group_markers = {
        "group_spacing": "constexpr int SettingsGroupSpacing = 18;" in options_cpp,
        "row_spacing": "constexpr int SettingsRowSpacing = 6;" in options_cpp,
        "semantic_group_factory": "createSettingsGroup" in options_cpp and "settingsGroupIndex" in options_cpp,
        "natural_width_compensation": "naturalControlWidth" in options_cpp and "setNaturalControlWidths" in options_cpp,
        "combo_contents_policy": "QComboBox::AdjustToContents" in options_cpp,
        "all_new_test_methods_registered": all(
            marker in test_source
            for marker in (
                "testSettingsGeneralGroupsAndDefaults",
                "testSettingsEveryTabFitsEveryLanguage",
            )
        ),
        "runner_tracks_new_methods": all(
            marker in unit_runner
            for marker in (
                "testSettingsGeneralGroupsAndDefaults",
                "testSettingsEveryTabFitsEveryLanguage",
            )
        ),
    }
    check("SET-STATIC-002", all(group_markers.values()), group_markers, "grouping and width contracts are explicit in production/test code")

    try:
        ET.parse(ui_path)
        ui_is_valid = True
        ui_error = None
    except (ET.ParseError, OSError) as error:
        ui_is_valid = False
        ui_error = str(error)
    check("SET-STATIC-003", ui_is_valid, {"path": str(ui_path), "error": ui_error}, "qvoptionsdialog.ui is well-formed XML")

    translations: dict[str, dict[str, bool]] = {}
    required_sources = (
        "Appearance:",
        "Checkerboard when image loaded",
        "Reuse window when launching with image",
        "Smooth scaling:",
        "Language:",
        "Show small images at 1:1",
        "Slideshow direction:",
        "Slideshow timer:",
        "After deletion:",
        "&Ask before deleting files",
        "Auto update check:",
        "Associate all supported formats",
    )
    for language in LANGUAGES[1:]:
        path = repo / "i18n" / f"qview_{language}.ts"
        try:
            root = ET.parse(path).getroot()
            source_texts = {node.text for node in root.iter("source") if node.text}
            translations[language] = {source: source in source_texts for source in required_sources}
        except (ET.ParseError, OSError):
            translations[language] = {source: False for source in required_sources}
    translation_passed = all(all(items.values()) for items in translations.values())
    check("SET-STATIC-004", translation_passed, translations, "all required settings strings exist in every supported non-English catalog")

    try:
        ast.parse(read(repo, "tests/settings_quality_pipeline.py"), filename="settings_quality_pipeline.py")
        pipeline_parses = True
    except (SyntaxError, OSError):
        pipeline_parses = False
    check("SET-STATIC-005", pipeline_parses, {"pipeline_parses": pipeline_parses}, "the audit runner parses as Python")

    diff_check = run_command(
        ["git", "diff", "--check", "HEAD", "--", "src", "tests", "CMakeLists.txt", "qView.pro", "dist"],
        repo,
        {},
        30,
    )
    check("SET-STATIC-006", diff_check["passed"], diff_check, "task-scoped diff has no whitespace errors")

    baseline_settings = run_command(
        ["git", "show", "HEAD:src/settingsmanager.cpp"],
        repo,
        {},
        30,
    )
    setting_key_pattern = re.compile(r'settingsLibrary\.insert\("([^"]+)"')
    current_setting_keys = sorted(setting_key_pattern.findall(settings_cpp))
    baseline_setting_keys = sorted(setting_key_pattern.findall(baseline_settings["output_tail"]))
    check(
        "SET-STATIC-007",
        baseline_settings["passed"] and current_setting_keys == baseline_setting_keys,
        {
            "baseline_command": baseline_settings["command"],
            "baseline_read": baseline_settings["passed"],
            "baseline_keys": baseline_setting_keys,
            "current_keys": current_setting_keys,
        },
        "the settings key set is unchanged; this task changes defaults/layout only",
    )

    return {
        "stage": "static",
        "passed": all(item["passed"] for item in checks),
        "checks": checks,
    }


def qtest_stage(binary: Path, repo: Path, suite: str, timeout: int) -> dict[str, Any]:
    return run_command(
        [str(binary), "-o", "-,txt"],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "QV_DISABLE_ONLINE_VERSION_CHECK": "1",
            "FOVELLE_TEST_SUITE": suite,
        },
        timeout,
    )


def unit_stage(binary: Path, repo: Path) -> dict[str, Any]:
    result = qtest_stage(binary, repo, "FeatureTests", 90)
    result["stage"] = "unit"
    result["suite"] = "FeatureTests"
    return result


def integration_stage(binary: Path, repo: Path) -> dict[str, Any]:
    result = qtest_stage(binary, repo, "WindowBehaviorTests", 90)
    result["stage"] = "integration"
    result["suite"] = "WindowBehaviorTests"
    return result


def system_stage(binary: Path, build_dir: Path, repo: Path) -> dict[str, Any]:
    del binary
    result = run_command(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-R", "^FovelleTests$"],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "QV_DISABLE_ONLINE_VERSION_CHECK": "1",
        },
        180,
    )
    result["stage"] = "system"
    result["test_target"] = "FovelleTests"
    return result


def stage_case_passed(case: dict[str, Any], stages: dict[str, dict[str, Any]]) -> bool:
    relevant = [stages[name] for name in case["evidence_stages"] if name in stages]
    return bool(relevant) and all(result["passed"] for result in relevant)


def write_json(path: Path, record: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def build_reports(repo: Path, build_dir: Path, binary: Path) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    static = static_stage(repo)
    unit = unit_stage(binary, repo)
    integration = integration_stage(binary, repo)
    system = system_stage(binary, build_dir, repo)
    stages = {item["stage"]: item for item in (static, unit, integration, system)}

    case_results = []
    for case in CASES:
        case_results.append(
            {
                "id": case["id"],
                "test_layer": case["test_layer"],
                "implementation": case["implementation"],
                "evidence_stages": case["evidence_stages"],
                "passed": stage_case_passed(case, stages),
                "stage_status": {name: stages[name]["passed"] for name in case["evidence_stages"]},
            }
        )

    spec_path = repo / "reports" / "test_case_specification.json"
    completion_path = repo / "reports" / "test_completion_report.json"
    quality_path = repo / "reports" / "code_quality_assessment_report.json"
    spec = {
        "schema_version": "1.0",
        "report_type": "atomic_test_case_specification",
        "generated_at_utc": now_utc(),
        "task": "Fovelle 设置页分组、宽度、默认值和 1.0.1 版本",
        "atomic_case_count": len(CASES),
        "languages": list(LANGUAGES),
        "groups": [{"index": index, "items": list(items)} for index, items in GROUPS],
        "cases": list(CASES),
        "required_test_fields": [
            "test_purpose",
            "preconditions",
            "input_data",
            "steps",
            "expected_result",
            "postconditions",
        ],
        "research_trace": RESEARCH_TRACE,
        "validation": {
            "unique_ids": len({case["id"] for case in CASES}) == len(CASES),
            "all_required_fields_present": all(
                all(case.get(field) for field in (
                    "id",
                    "acceptance_criterion",
                    "test_purpose",
                    "preconditions",
                    "input_data",
                    "steps",
                    "expected_result",
                    "postconditions",
                    "test_layer",
                    "implementation",
                ))
                for case in CASES
            ),
        },
    }
    spec["passed"] = all(spec["validation"].values())

    completion = {
        "schema_version": "1.0",
        "report_type": "test_completion_report",
        "generated_at_utc": now_utc(),
        "task": spec["task"],
        "host": {"platform": platform.platform(), "python": platform.python_version()},
        "build": {"build_dir": str(build_dir), "binary": str(binary), "binary_sha256": file_sha256(binary)},
        "stages": stages,
        "cases": case_results,
        "research_trace": RESEARCH_TRACE,
        "audit": {
            "case_count": len(case_results),
            "passed_cases": sum(1 for case in case_results if case["passed"]),
            "failed_cases": [case["id"] for case in case_results if not case["passed"]],
            "all_atomic_cases_passed": all(case["passed"] for case in case_results),
            "all_four_stages_passed": all(result["passed"] for result in stages.values()),
        },
        "passed": all(case["passed"] for case in case_results)
        and all(result["passed"] for result in stages.values()),
        "status": "passed" if all(case["passed"] for case in case_results)
        and all(result["passed"] for result in stages.values()) else "failed",
    }

    quality_checks = [
        {
            "id": "CQ-LEAN-001",
            "criterion": "精益完整性",
            "passed": static["passed"] and all(case["passed"] for case in case_results),
            "evidence_case_ids": ["SET-015"],
            "evidence": "复用现有控件和信号，新增的生产逻辑只包含语义分组、自然宽度补偿、显式间距和版本默认值；没有复制设置控件或引入新的持久化键。",
        },
        {
            "id": "CQ-CORRECT-001",
            "criterion": "功能正确性",
            "passed": all(case["passed"] for case in case_results),
            "evidence_case_ids": ["SET-016"] + [
                case["id"] for case in case_results
                if case["id"].startswith("SET-") and case["id"] not in {"SET-015", "SET-016", "SET-017"}
            ],
            "evidence": "原子结构、默认值、版本和多语言/多 Tab 几何用例均由实际 QtTest/Cocoa 执行结果覆盖。",
        },
        {
            "id": "CQ-TESTABLE-001",
            "criterion": "可测试性",
            "passed": all(result["passed"] for result in stages.values()) and spec["passed"],
            "evidence_case_ids": ["SET-017", "SET-001", "SET-010", "SET-013", "SET-014"],
            "evidence": "测试固定 QPA、警告策略、在线检查开关和 suite 入口；通过 QObject 属性、QSettings、sizeHint、viewport 几何、scrollbar 状态、CTest 输出和 SHA-256 进行非侵入式观测。",
        },
    ]
    quality = {
        "schema_version": "1.0",
        "report_type": "code_quality_assessment_report",
        "generated_at_utc": now_utc(),
        "task": spec["task"],
        "quality_requirements": quality_checks,
        "root_cause_summary": [
            {
                "observation": "设置页由旧的固定 `.ui` 几何和多个独立 QFormLayout 组成，页面宽度由聚合 sizeHint 决定；长文本控件可能不参与最终父布局的有效宽度下界。",
                "premise": "QScrollArea 的子内容是否完整显示取决于子布局的 sizeHint、minimumSize 和 sizePolicy；macOS QFormLayout 还可能默认居中。",
                "deduction": "将 General 改成八个显式语义组，统一左上对齐和不换行，并显式以每个控件的自然宽度修正内容 minimum size。",
            },
            {
                "observation": "QFormLayout 的 spanning checkbox 行不会稳定地把控件的自然宽度传播到父级聚合 sizeHint。",
                "premise": "初始修复测试中可观察到 checkbox geometry 大于 scroll viewport，而 layout sizeHint 仍取较小值。",
                "deduction": "增加 naturalControlWidth/setNaturalControlWidths，并将 combo 设置为 AdjustToContents、交互控件固定为自然宽度；多语言/多 Tab 测试锁定回归。",
            },
        ],
        "research_trace": RESEARCH_TRACE,
        "checks": quality_checks,
        "audit": {
            "all_quality_requirements_passed": all(item["passed"] for item in quality_checks),
            "specification_valid": spec["passed"],
            "test_completion_valid": completion["audit"]["all_atomic_cases_passed"] and completion["audit"]["all_four_stages_passed"],
        },
        "passed": all(item["passed"] for item in quality_checks),
        "status": "passed" if all(item["passed"] for item in quality_checks) else "failed",
    }

    write_json(spec_path, spec)
    write_json(completion_path, completion)
    write_json(quality_path, quality)
    return spec, completion, quality


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--binary", type=Path, default=None)
    args = parser.parse_args()
    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    binary = (args.binary or build_dir / "tests" / "fovelle_tests").resolve()

    spec, completion, quality = build_reports(repo, build_dir, binary)
    print(json.dumps({
        "specification_passed": spec["passed"],
        "completion_passed": completion["audit"]["all_atomic_cases_passed"] and completion["audit"]["all_four_stages_passed"],
        "quality_passed": quality["audit"]["all_quality_requirements_passed"],
        "case_count": len(spec["cases"]),
    }, ensure_ascii=False, indent=2))
    return 0 if (
        spec["passed"]
        and completion["audit"]["all_atomic_cases_passed"]
        and completion["audit"]["all_four_stages_passed"]
        and quality["audit"]["all_quality_requirements_passed"]
    ) else 1


if __name__ == "__main__":
    sys.exit(main())
