#!/usr/bin/env python3
"""Run the Settings-page audit and emit atomic, machine-auditable JSON."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import platform
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


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
    """Create one atomic case with all required audit fields."""

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
        "SET-GENERAL-GROUPS",
        "General 的选项按八个有序逻辑组直接组织",
        "验证 General 不再由多个平级内容区拼接，且每组只包含需求指定的选项。",
        "QVOptionsDialog 可构造；Qt UI 控件和 Settings 信号已初始化。",
        "settingsGroup1..settingsGroup8 的成员元数据、父级和顺序。",
        "构造 Settings，读取 General content 的直接布局项、每个组的成员列表和 size policy。",
        "General content 直接包含八个组，成员顺序严格等于需求清单；组间没有重复页面或旧的 general/misc 内容容器。",
        "销毁对话框；不写入用户设置。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralGroupsAndDefaults",
        ("unit", "static"),
    ),
    test_case(
        "SET-GROUP-SPACING",
        "组间距严格大于组内选项间距，General 与 Mouse 使用同一组间距",
        "验证间距由当前 QStyle 的布局规则推导，而不是由旧的固定回归常量决定。",
        "Cocoa QPA、QMacStyle 和 Settings 对话框可用。",
        "两页的 settingsGroupSpacing、settingsIntraGroupMaxSpacing、QFormLayout verticalSpacing，以及直接 QVBoxLayout item geometry。",
        "显示 Settings，读取两个页面的间距属性；扫描每个表单相邻可见行；比较直接组 item 的同坐标系几何间隙；切换 Mouse click/drag 模式后重复扫描。",
        "General 与 Mouse 的组间距相同且大于组内最大间距；表单保留 verticalSpacing=-1 以使用原生样式；页面末尾只有一个 stretch 承接剩余空间。",
        "关闭对话框；Mouse 模式和设置值恢复。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsSpacingUsesNativeStyle",
        ("integration", "static"),
    ),
    test_case(
        "SET-MOUSE-GROUPS",
        "Mouse 的 Left Button、Middle Button 和 Scroll Wheel/Touchpad 组内间距与 General 一致",
        "验证 Mouse 的多个逻辑组复用 General 的表单配置，并正确承载复合的 middle-button mode 控件。",
        "Mouse 页面可构造；两个 middle-button radio 状态可切换。",
        "Mouse 三个 QGroupBox、QFormLayout、middleButtonModeHost 和两种模式下的可见行。",
        "显示 Mouse，检查三组固定垂直 size policy、共享 form 配置和 host 的零边距；分别选择 Click、Drag 并重新读取布局。",
        "三组使用相同的共享组间距；组内相邻选项没有异常的大间距；middleButtonModeHost 不再依赖空白行或额外内边距。",
        "关闭对话框；恢复 middle-button mode。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsSpacingUsesNativeStyle",
        ("integration", "static"),
    ),
    test_case(
        "SET-FORM-HORIZONTAL-ALIGNMENT",
        "General 与 Mouse 的每个选项名称和值共享右标签列、左字段列",
        "验证 Language 等名称和值不会因独立表单宽度或翻译长度而漂移。",
        "五种支持语言的 catalog 可用；Settings 对话框已显示。",
        "所有 General/Mouse QFormLayout 的 labelAlignment、formAlignment、FieldRole/SpanningRole alignment 和运行时列坐标。",
        "逐语言显示 Settings，读取每个表单和可见行；比较共享标签列、字段列和 value-only 行；切换 Mouse 两种模式后重复。",
        "标签为 Right|Trailing，值为 Left|Top，表单原点为 Left|Top；所有有名称行的字段起点一致；value-only 控件位于字段列；Associate action 单独居中。",
        "关闭对话框；翻译器和测试设置恢复。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsFormsAlignLabelsAndValues",
        ("integration", "static"),
    ),
    test_case(
        "SET-FORM-VERTICAL-ALIGNMENT",
        "每个有名称选项的名称和值在最终布局中垂直对齐",
        "验证 QMacStyle 的原生控件边距不会造成 Slideshow direction 等值控件相对名称下移。",
        "Cocoa QPA、五种语言 catalog 和 Settings 对话框可用。",
        "每个可见 label/FieldRole item 的同坐标系 top、最终控件高度、内容对齐属性和两种 Mouse 模式。",
        "逐语言显示 Settings，先完成 polish 和布局激活，再读取 label/value item geometry 与控件高度；切换 Mouse click/drag 后重复。",
        "每个名称和值使用同一逻辑行顶边；名称内容 Right|Trailing|VCenter，值项 Left|Top；不会出现由原生高度差导致的可见下移。",
        "关闭对话框；翻译器和测试设置恢复。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsFormsAlignLabelsAndValues",
        ("integration", "static"),
    ),
    test_case(
        "SET-ASSOCIATE-NATIVE-BUTTON",
        "Associate all supported formats 恢复为直接的原生默认按钮",
        "验证按钮未被额外 stretch 包装或主题 QSS 改写，且保留参考截图对应的原生按钮绘制路径。",
        "Cocoa QPA、QMacStyle 和 Settings 对话框可用；不调用真实文件关联动作。",
        "associateFormatsButton 的 SpanningRole、alignment、stylesheet、flat、autoDefault、default、minimum width、同坐标系 sizeHint 和抓取图像。",
        "显示 Settings，读取 settingsGroup8 的直接布局项；检查原生属性和 item geometry；抓取按钮图像并确认 macOS 样式有可见垂直绘制变化。",
        "按钮直接位于 settingsGroup8 的 SpanningRole，水平/垂直居中、stylesheet 为空、非 flat、autoDefault=true、default=true，且不含人工最小宽度。",
        "关闭对话框；真实文件关联动作未触发。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsAssociateButtonUsesNativeStyle",
        ("integration", "static"),
    ),
    test_case(
        "SET-ALL-LANGUAGES-NO-CLIP",
        "五种支持语言下 General、Shortcuts、Mouse 的可见选项均完整显示",
        "验证自然宽度、页面尺寸和两种 Mouse 模式不会让名称、值或长复选框被裁切。",
        "Cocoa Qt Test 应用、en/es/ja/zh_Hans/zh_Hant catalog、可写的隔离 Settings 存储和构建好的测试二进制可用。",
        "五种语言；三个 Tab；Mouse Click/Drag；可见控件的 mapped geometry、minimumSizeHint 和水平滚动条。",
        "逐语言安装 catalog，显示 Settings，依次切换三个 Tab；检查 viewport 包含关系、水平滚动范围和可见控件最小尺寸；在 Mouse 中切换两种模式后重复。",
        "所有 Tab 的水平滚动范围为 0；每个可见控件完全位于 viewport，且不小于最终 minimumSizeHint；长文本和复合控件无裁切。",
        "关闭对话框；翻译器、临时目录和设置值恢复。",
        "system",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage",
        ("system", "static"),
    ),
    test_case(
        "SET-COOLDOWN-DEFAULT",
        "离散动作 cooldown 的内部默认值为勾选（true）",
        "验证移除用户选项后，旧设置键仍以勾选状态作为默认行为，避免改变触控板离散动作保护策略。",
        "SettingsManager 已初始化默认值库；测试进程不依赖持久化的 scrollactioncooldown 值。",
        "SettingsManager 中 scrollactioncooldown 的 defaultValue。",
        "读取设置项并直接检查 defaultValue 是否有效且转换为 true。",
        "scrollactioncooldown 的默认值为 true。",
        "只读 SettingsManager；不写入用户设置。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsCooldownOptionIsRemovedAndDefaultEnabled",
        ("static", "unit"),
    ),
    test_case(
        "SET-COOLDOWN-UI-REMOVED",
        "Mouse 不再暴露 Cooldown for discrete actions 选项",
        "验证废弃选项从 Qt UI、同步逻辑和可见控件树中完整移除，同时保留内部兼容设置键。",
        "QVOptionsDialog 可构造；Mouse 页的 .ui、同步函数和翻译源可读。",
        "scrollActionCooldownCheckbox object name、英文选项文本、syncSettings 引用和五个翻译源文本。",
        "运行静态源契约，再构造 Settings 并搜索旧 checkbox 和所有可见 checkbox 文本。",
        "源码不再创建或同步旧 checkbox，翻译不再包含旧选项文本，运行时控件树不包含该 checkbox。",
        "对话框销毁；内部设置键和其它 Mouse 选项保持不变。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsCooldownOptionIsRemovedAndDefaultEnabled",
        ("static", "unit"),
    ),
    test_case(
        "SET-GENERAL-COLON-ALIGNMENT",
        "所有支持语言下 General 的冒号结尾选项名称右对齐到同一列",
        "验证英文及非英文翻译不会因自然文本宽度不同而改变 General 标签列的右边界。",
        "五种翻译目录可加载；QVOptionsDialog 已 polish、显示并激活 General 页。",
        "en、es、ja、zh_Hans、zh_Hant 和 General 中所有可见 ASCII/全角冒号结尾 QLabel 的映射几何。",
        "逐语言安装翻译，显示 Settings，激活 General，读取每个可见冒号结尾标签的右边界和 alignment。",
        "每个可见标签同时具有 AlignRight 和 AlignTrailing，且同一 General 页所有标签右边界完全相等。",
        "关闭对话框；翻译器和临时设置恢复。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsColonAlignmentSurvivesTranslations",
        ("static", "integration"),
    ),
    test_case(
        "SET-MOUSE-COLON-ALIGNMENT",
        "所有支持语言下 Mouse 的冒号结尾选项名称右对齐到同一列",
        "验证 Mouse 的三个逻辑组使用与 General 相同的共享标签宽度，不因语言或组而漂移。",
        "五种翻译目录可加载；QVOptionsDialog 已 polish、显示并激活 Mouse 页。",
        "en、es、ja、zh_Hans、zh_Hant 和 Mouse 中所有可见 ASCII/全角冒号结尾 QLabel 的映射几何。",
        "逐语言安装翻译，显示 Settings，激活 Mouse，读取每个可见冒号结尾标签的右边界和 alignment。",
        "每个可见标签同时具有 AlignRight 和 AlignTrailing，且同一 Mouse 页所有标签右边界完全相等。",
        "关闭对话框；翻译器和临时设置恢复。",
        "integration",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsColonAlignmentSurvivesTranslations",
        ("static", "integration"),
    ),
    test_case(
        "CQ-LEAN-COMPLETENESS",
        "精益完整性：实现覆盖全部需求且没有非必要布局/样式分支",
        "用静态契约检查确认实现复用现有控件、信号和设置键，只增加语义分组、共享布局策略、自然尺寸测量和可观测测试。",
        "源代码、.ui、测试源、设置键基线和本审计脚本可读。",
        "静态 marker、XML 解析结果、git diff --check、当前/基线设置键集合。",
        "运行静态 stage，检查共享 helper、直接组层级、FieldRole、native button、row normalization、无旧常量和无新设置键。",
        "静态检查全部通过；无旧的固定间距/按钮 QSS 回归契约；没有新增持久化设置键或重复页面。",
        "不修改业务运行时状态；静态检查只读源文件。",
        "static",
        "tests/settings_quality_pipeline.py::static_stage",
        ("static",),
    ),
    test_case(
        "CQ-FUNCTIONAL-CORRECTNESS",
        "功能正确性：规定输入产生规定输出并保持副作用边界",
        "汇总 General/Mouse 布局、按钮和多语言 Tab 的实际 QtTest 结果。",
        "静态、单元、集成和系统执行环境均已准备；测试二进制可运行。",
        "原子功能 case 的固定语言、模式、控件属性、布局几何和输出状态。",
        "按 static → unit → integration → system 顺序执行流水线，读取每个原子 case 的 stage_status 和原始输出哈希。",
        "所有功能原子 case 通过；没有 failed、skipped、blacklisted 或超时；真实文件关联动作不被测试触发。",
        "测试进程、对话框、翻译器和隔离设置完成清理。",
        "static+unit+integration+system",
        "tests/settings_quality_pipeline.py::build_reports; reports/test_completion_report.json",
        ("static", "unit", "integration", "system"),
    ),
    test_case(
        "CQ-TESTABILITY",
        "可测试性：条件可确定控制、运行时状态可非侵入式观测、输出可重复审计",
        "验证四级流水线保存命令、环境、耗时、返回码、输出哈希、Qt Test totals 和每条 case 状态。",
        "本地构建目录、QtTest、CTest、Python 标准库和 Cocoa QPA 可用；在线版本检查被关闭。",
        "QT_QPA_PLATFORM=cocoa、QT_FATAL_WARNINGS=1、QV_DISABLE_ONLINE_VERSION_CHECK=1、FOVELLE_TEST_SUITE、固定超时和 SHA-256。",
        "执行静态、单元、集成、系统 stage；校验 JSON schema、stage 顺序、case 字段和每个 stage 的原始输出摘要。",
        "所有 stage 可重复运行并返回 0；每个 case 有明确实现引用和证据 stage；报告可以只凭 JSON 审计通过/失败原因。",
        "报告写入 reports；用户设置、源码和外部文件关联状态不被改变。",
        "static+unit+integration+system",
        "tests/settings_quality_pipeline.py::run_command; tests/settings_quality_pipeline.py::build_reports",
        ("static", "unit", "integration", "system"),
    ),
)


RESEARCH_TRACE = [
    {
        "hop": 1,
        "source": "https://developer.apple.com/design/human-interface-guidelines/settings",
        "finding": "Apple describes macOS settings windows as panes and recommends grouping related settings within each pane.",
        "explicit_premise": "General and Mouse are the two settings pages in scope, and each page has one semantic collection of named options.",
        "deduction": "A page-level layout invariant is the appropriate unit for label alignment; the solution must not depend on one language's rendered text.",
    },
    {
        "hop": 2,
        "source": "https://developer.apple.com/design/human-interface-guidelines/layout",
        "finding": "Apple's layout guidance treats alignment and consistent spacing as signals of organization and hierarchy.",
        "explicit_premise": "The requested observable is the common right edge of option names, including non-English translations.",
        "deduction": "The stable invariant is a common label-column boundary, not padded source strings or hand-tuned language-specific offsets.",
    },
    {
        "hop": 3,
        "source": "https://doc.qt.io/qt-6.11/qformlayout.html",
        "finding": "QFormLayout provides LabelRole and FieldRole columns and exposes label/form alignment controls.",
        "explicit_premise": "Each option name is a label widget whose natural width changes with translation.",
        "deduction": "Measure LabelRole natural widths, give every label the same shared width, and keep form origins left-aligned.",
    },
    {
        "hop": 4,
        "source": "https://doc.qt.io/qt-6/qlayout.html",
        "finding": "Qt layouts can apply alignment to widgets and nested layouts and calculate their final geometry from those constraints.",
        "explicit_premise": "Correctness must be observable from final widget geometry rather than only from source markers.",
        "deduction": "The integration test maps each label's right edge into its page coordinate system and checks the alignment flags.",
    },
    {
        "hop": 5,
        "source": "https://raw.githubusercontent.com/qt/qtbase/v6.11.1/src/widgets/kernel/qformlayout.cpp",
        "finding": "Qt's QFormLayout implementation computes label/field column geometry and honors label/form alignment when placing rows.",
        "explicit_premise": "Independent forms may have different natural size hints when translated labels have different widths.",
        "deduction": "A shared post-polish maximum width removes the independent-size-hint degree of freedom while retaining Qt's normal form layout.",
    },
    {
        "hop": 6,
        "source": "https://raw.githubusercontent.com/qt/qtbase/v6.11.1/src/widgets/kernel/qlayoutitem.cpp",
        "finding": "Qt converts layout-item geometry to QWidget geometry through a defined coordinate conversion.",
        "explicit_premise": "Labels live under different group/form parents, so local x coordinates are not sufficient evidence of page-level alignment.",
        "deduction": "The test compares all label right edges after mapping them to the common page widget, avoiding mixed coordinate systems.",
    },
    {
        "hop": 7,
        "source": "https://doc.qt.io/qt-6/i18n-source-translation.html",
        "finding": "Qt translation changes are delivered through the application translation/event path, so translated UI text can be different before the next layout pass.",
        "explicit_premise": "The reported defect appears after changing from English to Simplified Chinese and must also cover the other supported catalogs.",
        "deduction": "Recompute natural widths after translated text and polish; run the executable geometry test once for each supported language.",
    },
]


REQUIRED_CASE_FIELDS = (
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
    "evidence_stages",
)


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


def run_command(
    command: list[str],
    repo: Path,
    environment: dict[str, str],
    timeout: int,
) -> dict[str, Any]:
    started = time.perf_counter()
    merged_environment = {**os.environ, **environment}
    timed_out = False
    return_code: int | None
    stdout: str
    stderr: str
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
        "output_tail": output[-20000:],
    }


def read(repo: Path, relative: str) -> str:
    return (repo / relative).read_text(encoding="utf-8")


def static_stage(repo: Path) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []

    def check(identifier: str, passed: bool, observed: Any, expected: str) -> None:
        checks.append(
            {
                "id": identifier,
                "passed": bool(passed),
                "observed": observed,
                "expected": expected,
            }
        )

    options_cpp = read(repo, "src/qvoptionsdialog.cpp")
    options_header = read(repo, "src/qvoptionsdialog.h")
    options_ui = read(repo, "src/qvoptionsdialog.ui")
    test_source = read(repo, "tests/tst_qviewtests.cpp")
    pipeline_source = read(repo, "tests/settings_quality_pipeline.py")
    settings_source = read(repo, "src/settingsmanager.cpp")

    hierarchy_markers = {
        "direct_general_content": all(
            f"contentLayout->addWidget(group{index});" in options_cpp
            for index in range(1, 9)
        ),
        "single_bottom_stretch": "contentLayout->addStretch(1);" in options_cpp,
        "no_legacy_general_content_widget": "contentLayout->addWidget(generalWidget)" not in options_cpp,
        "factory_and_group_index": "createSettingsGroup" in options_cpp and "settingsGroupIndex" in options_cpp,
        "mouse_groups_fixed": "setSettingsGroupFixedHeight(group)" in options_cpp,
    }
    check(
        "STATIC-HIERARCHY",
        all(hierarchy_markers.values()),
        hierarchy_markers,
        "General has one direct ordered hierarchy and Mouse groups have fixed vertical policy",
    )

    spacing_markers = {
        "native_metric_helper": "settingsLayoutMetrics" in options_cpp,
        "native_form_spacing": "layout->setVerticalSpacing(-1);" in options_cpp,
        "strict_hierarchy": "metrics.maximumIntraGroupSpacing + 1" in options_cpp,
        "same_metrics_applied": options_cpp.count("configureSettingsGroupsLayout(") >= 4,
        "no_old_manual_constants": all(
            token not in options_cpp
            for token in (
                "SettingsGroupSpacing",
                "SettingsRowSpacing",
                "SettingsGroupBottomPadding",
                "SettingsControlHeightPadding",
            )
        ),
    }
    check(
        "STATIC-SPACING",
        all(spacing_markers.values()),
        spacing_markers,
        "group spacing is derived from QStyle and remains greater than native intra-form spacing",
    )

    form_markers = {
        "shared_form_helper": "void configureSettingsForm(QFormLayout *layout)" in options_cpp,
        "mouse_uses_helper": "configureSettingsForm(form);" in options_cpp,
        "left_top_form": "layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);" in options_cpp,
        "right_trailing_label": "SettingsLabelAlignment =" in options_cpp and "Qt::AlignRight | Qt::AlignTrailing;" in options_cpp,
        "left_top_values": "SettingsValueAlignment = Qt::AlignLeft | Qt::AlignTop;" in options_cpp,
        "fixed_shared_label_width": "labelItem->widget()->setFixedWidth(labelColumnWidth);" in options_cpp,
        "fixed_width_is_reset_before_measurement": "setMaximumWidth(QWIDGETSIZE_MAX);" in options_cpp,
        "shared_width_is_language_derived": "qMax(formLabelColumnWidth(generalContent)," in options_cpp,
        "row_normalization": all(
            marker in options_cpp
            for marker in (
                "normalizeNamedRows",
                "setFixedHeight(rowHeight)",
                "SettingsLabelContentAlignment",
                "Qt::WA_LayoutUsesWidgetRect",
            )
        ),
        "form_geometry_test": "testSettingsFormsAlignLabelsAndValues" in test_source,
    }
    check(
        "STATIC-FORM-ALIGNMENT",
        all(form_markers.values()),
        form_markers,
        "General and Mouse share one deterministic form and row-alignment contract",
    )

    cooldown_markers = {
        "default_enabled": 'settingsLibrary.insert("scrollactioncooldown", {true, {}});' in settings_source,
        "runtime_reader_retained": 'getBoolean("scrollactioncooldown")' in read(repo, "src/qvgraphicsview.cpp"),
        "checkbox_removed_from_ui": "scrollActionCooldownCheckbox" not in options_ui,
        "sync_entry_removed": "scrollActionCooldownCheckbox" not in options_cpp,
        "english_text_removed_from_ui": "Cooldown for discrete actions" not in options_ui,
        "catalog_text_removed": all(
            "Cooldown for discrete actions" not in read(repo, f"i18n/qview_{language}.ts")
            for language in LANGUAGES[1:]
        ) and "Cooldown for discrete actions" not in read(repo, "i18n/template.ts"),
        "executable_test": "testSettingsCooldownOptionIsRemovedAndDefaultEnabled" in test_source,
    }
    check(
        "STATIC-COOLDOWN",
        all(cooldown_markers.values()),
        cooldown_markers,
        "the cooldown default remains enabled while the obsolete user-facing option is absent",
    )

    colon_alignment_markers = {
        "shared_fixed_column": "setFixedWidth(labelColumnWidth)" in options_cpp,
        "right_trailing_contract": "SettingsLabelContentAlignment" in options_cpp,
        "post_polish_measurement": "ensurePolished();" in options_cpp and "formLabelColumnWidth" in options_cpp,
        "cross_language_geometry_test": "testSettingsColonAlignmentSurvivesTranslations" in test_source,
        "ascii_and_fullwidth_colon_test": "QChar(0xFF1A)" in test_source,
    }
    check(
        "STATIC-COLON-ALIGNMENT",
        all(colon_alignment_markers.values()),
        colon_alignment_markers,
        "translated colon-terminated labels are measured post-polish and fixed to one shared column",
    )

    button_markers = {
        "direct_spanning_role": "setWidget(0, QFormLayout::SpanningRole," in options_cpp,
        "native_state": all(
            marker in options_cpp
            for marker in (
                "setStyleSheet(QString())",
                "setFlat(false)",
                "setAutoDefault(true)",
                "setDefault(true)",
                "setMinimumSize(0, 0)",
            )
        ),
        "no_theme_qss_helper": "updateAssociationButtonAppearance" not in options_cpp
        and "QPalette::Accent" not in options_cpp
        and "settingsAssociationStyle" not in options_cpp,
        "native_button_test": "testSettingsAssociateButtonUsesNativeStyle" in test_source,
        "no_removed_header_api": "updateAssociationButtonAppearance" not in options_header,
    }
    check(
        "STATIC-NATIVE-BUTTON",
        all(button_markers.values()),
        button_markers,
        "the action row restores the native QPushButton contract without hand-authored theme QSS",
    )

    value_role_markers = {
        "field_role_helper": "layout->setWidget(layout->rowCount(), QFormLayout::FieldRole, value)" in options_cpp,
        "all_value_only_rows": all(
            f"addValueOnlyRow(groupLayout, ui->{name})" in options_cpp
            for name in (
                "checkerboardBackgroundCheckbox",
                "reuseWindowCheckbox",
                "smallImagesOneToOneCheckbox",
                "askDeleteCheckbox",
            )
        ),
        "middle_host": all(
            marker in options_cpp + options_ui
            for marker in (
                "middleButtonModeHost",
                "setControlType(QSizePolicy::RadioButton)",
            )
        ),
        "no_empty_middle_row": "<item row=\"1\" column=\"1\">\n              <spacer" not in options_ui,
    }
    check(
        "STATIC-FIELD-ROLE",
        all(value_role_markers.values()),
        value_role_markers,
        "value-only rows use the field column and Mouse mode uses an explicit zero-margin host",
    )

    test_markers = {
        name: marker in test_source
        for name, marker in {
            "groups": "testSettingsGeneralGroupsAndDefaults",
            "spacing": "testSettingsSpacingUsesNativeStyle",
            "forms": "testSettingsFormsAlignLabelsAndValues",
            "cooldown": "testSettingsCooldownOptionIsRemovedAndDefaultEnabled",
            "colon_alignment": "testSettingsColonAlignmentSurvivesTranslations",
            "button": "testSettingsAssociateButtonUsesNativeStyle",
            "all_languages": "testSettingsEveryTabFitsEveryLanguage",
        }.items()
    }
    test_markers["no_removed_theme_test"] = (
        "testSettingsAssociateButtonFollowsThemeAccent" not in test_source
    )
    check(
        "STATIC-TEST-COVERAGE",
        all(test_markers.values()),
        test_markers,
        "every task-specific observable contract has executable test code",
    )

    try:
        ET.parse(repo / "src/qvoptionsdialog.ui")
        ui_valid = True
        ui_error = None
    except (ET.ParseError, OSError) as error:
        ui_valid = False
        ui_error = str(error)
    check(
        "STATIC-UI-XML",
        ui_valid,
        {"path": str(repo / "src/qvoptionsdialog.ui"), "error": ui_error},
        "qvoptionsdialog.ui is well-formed XML",
    )

    required_sources = (
        "Language:",
        "Appearance:",
        "Checkerboard when image loaded",
        "Smooth scaling:",
        "Reuse window when launching with image",
        "Show small images at 1:1",
        "Slideshow direction:",
        "Slideshow timer:",
        "After deletion:",
        "&Ask before deleting files",
        "Auto update check:",
        "Associate all supported formats",
    )
    translation_observations: dict[str, dict[str, bool]] = {}
    for language in LANGUAGES[1:]:
        path = repo / "i18n" / f"qview_{language}.ts"
        try:
            root = ET.parse(path).getroot()
            source_texts = {node.text for node in root.iter("source") if node.text}
            translation_observations[language] = {
                source: source in source_texts for source in required_sources
            }
        except (ET.ParseError, OSError):
            translation_observations[language] = {
                source: False for source in required_sources
            }
    check(
        "STATIC-TRANSLATIONS",
        all(all(values.values()) for values in translation_observations.values()),
        translation_observations,
        "all required settings source strings exist in every supported non-English catalog",
    )

    try:
        ast.parse(pipeline_source, filename="settings_quality_pipeline.py")
        pipeline_valid = True
    except SyntaxError as error:
        pipeline_valid = False
        pipeline_error = str(error)
    else:
        pipeline_error = None
    check(
        "STATIC-PIPELINE-PYTHON",
        pipeline_valid,
        {"error": pipeline_error},
        "the audit runner parses as Python",
    )

    diff_check = run_command(
        ["git", "diff", "--check", "HEAD", "--", "src", "tests", "i18n"],
        repo,
        {},
        30,
    )
    check("STATIC-DIFF", diff_check["passed"], diff_check, "task-scoped source/test diff has no whitespace errors")

    baseline_result = run_command(
        ["git", "show", "HEAD:src/settingsmanager.cpp"],
        repo,
        {},
        30,
    )
    key_pattern = re.compile(r'settingsLibrary\.insert\("([^"]+)"')
    current_keys = sorted(key_pattern.findall(settings_source))
    baseline_keys = sorted(key_pattern.findall(baseline_result["output_tail"]))
    key_observation = {
        "baseline_read": baseline_result["passed"],
        "baseline_keys": baseline_keys,
        "current_keys": current_keys,
    }
    check(
        "STATIC-SETTINGS-KEYS",
        baseline_result["passed"] and current_keys == baseline_keys,
        key_observation,
        "the task does not add or remove persistence keys",
    )

    return {
        "stage": "static",
        "passed": all(item["passed"] for item in checks),
        "checks": checks,
    }


def parse_qtest_totals(output: str) -> dict[str, int] | None:
    matches = re.findall(
        r"Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted",
        output,
    )
    if not matches:
        return None
    passed, failed, skipped, blacklisted = matches[-1]
    return {
        "passed": int(passed),
        "failed": int(failed),
        "skipped": int(skipped),
        "blacklisted": int(blacklisted),
    }


def qtest_stage(binary: Path, repo: Path, suite: str, timeout: int) -> dict[str, Any]:
    result = run_command(
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
    totals = parse_qtest_totals(result["output_tail"])
    result["stage"] = suite
    result["suite"] = suite
    result["qtest_totals"] = totals
    result["passed"] = bool(
        result["passed"]
        and totals
        and totals["failed"] == 0
        and totals["skipped"] == 0
        and totals["blacklisted"] == 0
    )
    return result


def unit_stage(binary: Path, repo: Path) -> dict[str, Any]:
    result = qtest_stage(binary, repo, "FeatureTests", 120)
    result["stage"] = "unit"
    return result


def integration_stage(binary: Path, repo: Path) -> dict[str, Any]:
    result = qtest_stage(binary, repo, "WindowBehaviorTests", 120)
    result["stage"] = "integration"
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
        240,
    )
    result["stage"] = "system"
    result["test_target"] = "FovelleTests"
    result["ctest_summary"] = re.findall(
        r"\d+% tests passed(?:, \d+ tests failed)? out of \d+",
        result["output_tail"],
    )
    return result


def stage_case_passed(case: dict[str, Any], stages: dict[str, dict[str, Any]]) -> bool:
    relevant = [stages[name] for name in case["evidence_stages"] if name in stages]
    return bool(relevant) and all(stage["passed"] for stage in relevant)


def write_json(path: Path, record: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def validate_cases() -> dict[str, Any]:
    ids = [case["id"] for case in CASES]
    missing = {
        case["id"]: [field for field in REQUIRED_CASE_FIELDS if not case.get(field)]
        for case in CASES
        if any(not case.get(field) for field in REQUIRED_CASE_FIELDS)
    }
    return {
        "unique_ids": len(ids) == len(set(ids)),
        "all_required_fields_present": not missing,
        "missing_fields": missing,
        "all_evidence_stages_known": all(
            stage in {"static", "unit", "integration", "system"}
            for case in CASES
            for stage in case["evidence_stages"]
        ),
    }


def build_reports(repo: Path, build_dir: Path, binary: Path) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    generated_at = now_utc()
    static = static_stage(repo)
    unit = unit_stage(binary, repo)
    integration = integration_stage(binary, repo)
    system = system_stage(binary, build_dir, repo)
    stages = {item["stage"]: item for item in (static, unit, integration, system)}
    validation = validate_cases()

    case_results = []
    for case in CASES:
        stage_status = {
            name: stages[name]["passed"]
            for name in case["evidence_stages"]
            if name in stages
        }
        case_results.append(
            {
                "id": case["id"],
                "acceptance_criterion": case["acceptance_criterion"],
                "test_layer": case["test_layer"],
                "implementation": case["implementation"],
                "evidence_stages": case["evidence_stages"],
                "stage_status": stage_status,
                "passed": bool(stage_status) and all(stage_status.values()),
            }
        )

    report_files = {
        "production_cpp": repo / "src/qvoptionsdialog.cpp",
        "production_ui": repo / "src/qvoptionsdialog.ui",
        "production_header": repo / "src/qvoptionsdialog.h",
        "test_source": repo / "tests/tst_qviewtests.cpp",
        "audit_runner": repo / "tests/settings_quality_pipeline.py",
        "solution_proof": repo / "reports/solution_and_proof.md",
    }
    input_hashes = {name: file_sha256(path) for name, path in report_files.items()}

    task = "Fovelle 设置页 Mouse cooldown 选项移除与 General/Mouse 多语言冒号标签右对齐修复"
    spec = {
        "schema_version": "1.0",
        "report_type": "atomic_test_case_specification",
        "generated_at_utc": generated_at,
        "task": task,
        "atomic_case_count": len(CASES),
        "languages": list(LANGUAGES),
        "groups": [{"index": index, "items": list(items)} for index, items in GROUPS],
        "required_test_fields": list(REQUIRED_CASE_FIELDS),
        "cases": list(CASES),
        "research_trace": RESEARCH_TRACE,
        "input_sha256": input_hashes,
        "validation": validation,
        "passed": (
            validation["unique_ids"]
            and validation["all_required_fields_present"]
            and validation["all_evidence_stages_known"]
        ),
    }

    completion = {
        "schema_version": "1.0",
        "report_type": "test_completion_report",
        "generated_at_utc": generated_at,
        "task": task,
        "host": {
            "platform": platform.platform(),
            "python": platform.python_version(),
            "cwd": str(repo),
        },
        "build": {
            "build_dir": str(build_dir),
            "binary": str(binary),
            "binary_sha256": file_sha256(binary),
        },
        "stage_order": ["static", "unit", "integration", "system"],
        "stages": stages,
        "cases": case_results,
        "research_trace": RESEARCH_TRACE,
        "audit": {
            "case_count": len(case_results),
            "passed_cases": sum(1 for case in case_results if case["passed"]),
            "failed_cases": [case["id"] for case in case_results if not case["passed"]],
            "all_atomic_cases_passed": all(case["passed"] for case in case_results),
            "all_four_stages_passed": all(stage["passed"] for stage in stages.values()),
            "stage_order_is_explicit": list(stages) == ["static", "unit", "integration", "system"],
        },
    }
    completion["passed"] = bool(
        completion["audit"]["all_atomic_cases_passed"]
        and completion["audit"]["all_four_stages_passed"]
        and completion["audit"]["stage_order_is_explicit"]
    )
    completion["status"] = "passed" if completion["passed"] else "failed"

    behavior_ids = {
        case["id"]
        for case in CASES
        if not case["id"].startswith("CQ-")
    }
    behavior_passed = all(
        case["passed"] for case in case_results if case["id"] in behavior_ids
    )
    quality_checks = [
        {
            "id": "CQ-LEAN-001",
            "criterion": "精益完整性",
            "passed": static["passed"] and next(
                case["passed"] for case in case_results if case["id"] == "CQ-LEAN-COMPLETENESS"
            ),
            "evidence_case_ids": ["CQ-LEAN-COMPLETENESS"],
            "evidence": "静态契约确认 General/Mouse 复用现有控件和信号，使用共享固定标签列、动态样式间距、直接原生按钮和最小必要的 row normalization；cooldown 保留兼容键但移除 UI，设置键集合与基线一致。",
        },
        {
            "id": "CQ-CORRECT-001",
            "criterion": "功能正确性",
            "passed": behavior_passed,
            "evidence_case_ids": sorted(behavior_ids),
            "evidence": "QtTest/Cocoa 实际执行覆盖 cooldown 默认与控件移除、八组 General、三组 Mouse、动态间距、两列/垂直对齐、原生按钮及五种语言三个 Tab。",
        },
        {
            "id": "CQ-TESTABLE-001",
            "criterion": "可测试性",
            "passed": spec["passed"] and completion["passed"],
            "evidence_case_ids": ["CQ-TESTABILITY"],
            "evidence": "报告保存四级命令、环境覆盖、超时、返回码、耗时、stdout/stderr SHA-256、Qt Test totals 和原子 case 状态；运行时只读观察 QObject 属性、布局几何、滚动条和按钮渲染。",
        },
    ]
    quality = {
        "schema_version": "1.0",
        "report_type": "code_quality_assessment_report",
        "generated_at_utc": generated_at,
        "task": task,
        "quality_requirements": quality_checks,
        "root_cause_summary": [
            {
                "observation": "General 原来由多个 legacy form/content 区构成，独立布局和旧的固定 spacing 使组间距与组内行距的层级不可审计。",
                "explicit_premise": "用户要求逻辑组之间明显更疏，且 General/Mouse 采用相同规则；Qt QFormLayout 在 verticalSpacing 未指定时可向 style 请求原生 spacing。",
                "deduction": "改为一个 General content 下的八个直接组，统一使用共享的动态 groupSpacing，并保留 form verticalSpacing=-1。",
            },
            {
                "observation": "Mouse 的中键模式控件曾依赖旧表单空白行/内边距，导致组内间距不稳定。",
                "explicit_premise": "RadioButton 是一个复合字段值，应作为一个零边距 host 参与同一 QFormLayout 行。",
                "deduction": "使用 middleButtonModeHost，显式设置 RadioButton control type、垂直 Fixed 和零边距，并移除空白行。",
            },
            {
                "observation": "value-only 选项若使用 QFormLayout::addRow(widget) 会成为 SpanningRole，不能稳定落在有名称选项的字段列。",
                "explicit_premise": "用户要求 General/Mouse 的名称和值垂直、水平形成共同网格；value-only 控件仍需从字段列开始。",
                "deduction": "所有 value-only rows 使用 FieldRole；没有标签的第四组以共享 labelColumnWidth 作为 form 左 inset。",
            },
            {
                "observation": "QMacStyle 的控件 layout-item rectangle 与 QWidget rectangle 可能因 QWidgetItem layout margins 不同，直接混用坐标会制造假回归或掩盖真实对齐问题。",
                "explicit_premise": "Qt 6.11.1 QWidgetItem 明确在 layout-item 和 widget 坐标之间做转换；控件自然 sizeHint 在 polish 前后也可能变化。",
                "deduction": "post-polish 固定每个有名称行的 label/value host 高度，设置相同的逻辑行顶边，并让测试在同一坐标系中比较布局 geometry，同时单独检查控件最终最小尺寸。",
            },
            {
                "observation": "Associate all supported formats 的回归来自按钮结构/状态被改写，而不是需要新增主题 QSS。",
                "explicit_premise": "参考图是 macOS 原生默认按钮；Qt QPushButton 将 default/autoDefault 状态交给当前 style 的 sizeHint 和 paint 路径。",
                "deduction": "清空 stylesheet、恢复 flat=false/autoDefault=true/default=true、保留直接 SpanningRole 居中 action，并消除人工最小宽度。",
            },
            {
                "observation": "长翻译或复合复选框可能超出旧 page sizeHint，造成 horizontalScrollBar=0 但实际控件越过 viewport。",
                "explicit_premise": "QScrollArea 的完整显示由 child layout 的自然尺寸、minimum size 和最终父级 geometry 共同决定，且用户要求所有支持语言可重复验证。",
                "deduction": "在 polish 后测量控件自然宽度、重新激活布局并校验 mapped viewport geometry；系统用例覆盖五种语言、三个 Tab 和两种 Mouse 模式。",
            },
            {
                "observation": "不同语言的标签自然宽度不同；仅设置 AlignRight 而不固定共享标签列时，各独立 QFormLayout 仍可产生不同的右边界。",
                "explicit_premise": "用户要求 General 和 Mouse 在英文及非英文语言中冒号右边界都一致；翻译文本宽度只能在运行时由 sizeHint 确定。",
                "deduction": "在翻译和 polish 完成后取 General/Mouse 全体标签自然宽度最大值 L，并将每个标签固定为 L；清除旧固定宽度后在下一次语言/样式测量中重算。",
            },
            {
                "observation": "Cooldown 选项的持久化键和图形视图读取逻辑早已存在，但用户不再需要可见开关。",
                "explicit_premise": "需求同时要求默认勾选和移除选项；删除内部键会使旧版本配置失去兼容读取路径，且改变非 UI 行为边界。",
                "deduction": "保留 scrollactioncooldown 键并固定默认 true，移除 .ui 控件、同步连接和翻译文本；已有显式配置仍可兼容读取，新安装默认启用。",
            },
        ],
        "research_trace": RESEARCH_TRACE,
        "input_sha256": input_hashes,
        "checks": quality_checks,
        "audit": {
            "all_quality_requirements_passed": all(item["passed"] for item in quality_checks),
            "specification_valid": spec["passed"],
            "test_completion_valid": completion["passed"],
        },
    }
    quality["passed"] = all(item["passed"] for item in quality_checks)
    quality["status"] = "passed" if quality["passed"] else "failed"

    write_json(repo / "reports/test_case_specification.json", spec)
    write_json(repo / "reports/test_completion_report.json", completion)
    write_json(repo / "reports/code_quality_assessment_report.json", quality)
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
    summary = {
        "specification_passed": spec["passed"],
        "completion_passed": completion["passed"],
        "quality_passed": quality["passed"],
        "case_count": len(spec["cases"]),
        "stage_order": completion["stage_order"],
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if all(summary[key] for key in ("specification_passed", "completion_passed", "quality_passed")) else 1


if __name__ == "__main__":
    raise SystemExit(main())
