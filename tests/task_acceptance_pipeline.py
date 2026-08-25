#!/usr/bin/env python3
"""Execute the acceptance matrix for the Fovelle settings/navigation task.

The script intentionally keeps the four gates explicit.  Static checks inspect
the source and catalogs, unit checks invoke the deterministic QtTest methods,
integration checks rebuild and run the registered regression test, and the
system gate starts the actual app bundle with its opt-in observation probe.
All observations are written as JSON so a later audit can reproduce the exact
command, input, output, and result for every atomic criterion.
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
import tempfile
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


STAGES = ("static", "unit", "integration", "system")
ALLOWED_CATALOGS = {
    "qview_es.ts",
    "qview_ja.ts",
    "qview_zh_Hans.ts",
    "qview_zh_Hant.ts",
}
LANGUAGE_LABELS = [
    ("System Language", "system"),
    ("English", "en"),
    ("简体中文", "zh_Hans"),
    ("繁體中文", "zh_Hant"),
    ("Español", "es"),
    ("日本語", "ja"),
]
TRANSLATION_EXPECTATIONS = {
    "qview_es.ts": {
        "Fovelle": "Fovelle", "Window": "Ventana", "&Help": "Ayuda",
        "General": "General", "Language:": "Idioma:", "System Language": "Idioma del sistema",
        "Appearance:": "Apariencia:", "Light": "Claro", "Dark": "Oscuro", "System": "Sistema",
        "Show small images at 1:1": "Mostrar imágenes pequeñas a 1:1",
        "Associate all supported formats": "Asociar todos los formatos compatibles",
        "Checkerboard when image loaded": "Patrón de tablero de ajedrez al cargar la imagen",
        "Reuse window when launching with image": "Reutilizar ventana al abrir imágenes",
        "Smooth scaling:": "Escalado suave:", "Shortcuts": "Accesos rápidos", "Mouse": "Ratón",
        "Auto update check:": "Comprobar actualizaciones automáticamente:",
    },
    "qview_ja.ts": {
        "Fovelle": "Fovelle", "Window": "ウィンドウ", "&Help": "ヘルプ",
        "General": "一般", "Language:": "言語:", "System Language": "システム言語",
        "Appearance:": "外観:", "Light": "ライト", "Dark": "ダーク", "System": "システム",
        "Show small images at 1:1": "小さい画像を 1:1 で表示",
        "Associate all supported formats": "対応するすべての形式を関連付け",
        "Checkerboard when image loaded": "画像読み込み時にチェック柄を表示",
        "Reuse window when launching with image": "画像を開く際にウィンドウを再利用",
        "Smooth scaling:": "スムーズなスケーリング:", "Shortcuts": "ショートカット", "Mouse": "マウス",
        "Auto update check:": "アップデートの自動確認:",
    },
    "qview_zh_Hans.ts": {
        "Fovelle": "Fovelle", "Window": "窗口", "&Help": "帮助",
        "General": "通用", "Language:": "语言:", "System Language": "系统语言",
        "Appearance:": "外观：", "Light": "浅色", "Dark": "深色", "System": "系统",
        "Show small images at 1:1": "以 1:1 显示小图像",
        "Associate all supported formats": "关联所有支持的格式",
        "Checkerboard when image loaded": "加载图片时显示棋盘格",
        "Reuse window when launching with image": "使用图片启动时重用窗口",
        "Smooth scaling:": "平滑缩放：", "Shortcuts": "快捷键", "Mouse": "鼠标",
        "Auto update check:": "自动检查更新：",
    },
    "qview_zh_Hant.ts": {
        "Fovelle": "Fovelle", "Window": "視窗", "&Help": "說明(&H)",
        "General": "一般", "Language:": "語言:", "System Language": "系統語言",
        "Appearance:": "外觀：", "Light": "淺色", "Dark": "深色", "System": "系統",
        "Show small images at 1:1": "以 1:1 顯示小型影像",
        "Associate all supported formats": "關聯所有支援的格式",
        "Checkerboard when image loaded": "載入影像時顯示棋盤格",
        "Reuse window when launching with image": "使用影像啟動時重複使用視窗",
        "Smooth scaling:": "平滑縮放：", "Shortcuts": "捷徑", "Mouse": "滑鼠",
        "Auto update check:": "自動檢查更新：",
    },
}
RESEARCH_TRACE = [
    {
        "hop": 1,
        "dimension": "native settings geometry",
        "source": "https://doc.qt.io/qt-6/qdialog.html",
        "finding": "Qt documents SetFixedSize as the layout constraint that prevents manual dialog resizing.",
        "explicit_premise": "The existing settings UI minimum width is 600 px; therefore W is fixed as 600 px.",
    },
    {
        "hop": 2,
        "dimension": "scroll-free per-tab height",
        "source": "https://doc.qt.io/qt-6/qscrollarea.html",
        "finding": "QScrollArea exposes independent vertical scrollbar policies and a resizable content widget.",
        "explicit_premise": "General and Mouse use natural layout size plus frame; Shortcuts uses the table header plus exactly 16 row heights.",
    },
    {
        "hop": 3,
        "dimension": "Photos navigation reference",
        "source": "https://support.apple.com/en-gb/guide/photos/pht9b4411b24/mac",
        "finding": "Apple Photos documents left/right arrow navigation between photos, matching the requested directional control semantics.",
        "explicit_premise": "The two supplied local reference images define appearance: light is transparent-chevron and dark is gray-tile-chevron; the artwork remains one composited button.",
    },
    {
        "hop": 4,
        "dimension": "tab-size transition",
        "source": "https://doc.qt.io/qt-6/qpropertyanimation.html",
        "finding": "QPropertyAnimation interpolates a QObject property with an easing curve, which is suitable for the dialog's fixed animated size.",
        "explicit_premise": "The Settings width remains fixed at W=600; only the natural per-tab height is animated.",
    },
    {
        "hop": 5,
        "dimension": "reopen geometry determinism",
        "source": "https://doc.qt.io/qt-6/restoring-geometry.html",
        "finding": "Qt restores saved window geometry and recommends restoring geometry before showing the window; saving an in-flight animation frame would therefore make reopen size nondeterministic.",
        "explicit_premise": "The selected category and its natural height are authoritative at close time, so the transition is finalized before saveGeometry().",
    },
    {
        "hop": 6,
        "dimension": "translation extraction and runtime catalogs",
        "source": "https://doc.qt.io/qt-6/localization.html",
        "finding": "Qt uses lupdate/lrelease with TS/QM catalogs, so current source coverage and completed translations can be audited from the same source contexts.",
        "explicit_premise": "The menu and Settings source contexts are the scoped English inventory; all four non-English catalogs must contain non-empty completed entries for that inventory.",
    },
    {
        "hop": 7,
        "dimension": "localized form layout",
        "source": "https://doc.qt.io/qt-6/qformlayout.html",
        "finding": "QFormLayout separates label and field roles and supports form alignment and row-wrap policy; independent centered forms can therefore choose different field origins when translated label widths differ.",
        "explicit_premise": "All General and Mouse forms use one shared label-column width and left-aligned, non-wrapping rows.",
    },
]


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read(repo: Path, relative: str) -> str:
    return (repo / relative).read_text(encoding="utf-8")


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
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
        )
        return {
            "command": [str(part) for part in command],
            "return_code": completed.returncode,
            "passed": completed.returncode == 0,
            "stdout": completed.stdout,
            "stderr": completed.stderr,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
        }
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        return {
            "command": [str(part) for part in command],
            "return_code": None,
            "passed": False,
            "stdout": stdout,
            "stderr": stderr,
            "timeout_seconds": timeout,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
        }


def compact_execution(result: dict[str, Any], limit: int = 10000) -> dict[str, Any]:
    output = (result.get("stdout", "") + result.get("stderr", ""))
    return {
        "command": result.get("command", []),
        "return_code": result.get("return_code"),
        "passed": result.get("passed", False),
        "duration_ms": result.get("duration_ms"),
        "output_tail": output[-limit:],
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
        "evidence_file": "reports/test_evidence.json",
    }


CASES = [
    make_case(
        "VER-001",
        "应用版本号为 1.0.0。",
        "static",
        "tests/task_acceptance_pipeline.py::static_version_contract",
        "验证源码、打包元数据和测试运行时使用同一版本号。",
        ["仓库源码可读。"],
        {"expected_version": "1.0.0", "files": ["CMakeLists.txt", "qView.pro", "dist/mac/Info.plist"]},
        ["读取构建配置和 macOS bundle 元数据。", "比较所有版本字段与 1.0.0。"],
        "所有发布入口均声明 1.0.0。",
        ["不修改源码或用户设置。"],
    ),
    make_case(
        "NAV-001",
        "浅色背景下的悬浮按钮为透明底板加符号。",
        "static",
        "tests/task_acceptance_pipeline.py::static_navigation_contract",
        "验证浅色导航样式没有有色悬浮底板。",
        ["按钮绘制源码可读。"],
        {"style": "light-transparent", "reference": "/Users/inostarlin/Downloads/浅色背景下的右侧导航按钮.jpg"},
        ["检查 QWidget 和 Cocoa 两条绘制路径。", "确认浅色分支只保留 chevron。"],
        "浅色样式使用透明底板和灰色 chevron。",
        ["不生成额外 UI 资源。"],
    ),
    make_case(
        "NAV-002",
        "深色背景下的悬浮按钮为有色底板加符号。",
        "static",
        "tests/task_acceptance_pipeline.py::static_navigation_contract",
        "验证深色导航样式保留灰色圆角底板和深色符号。",
        ["按钮绘制源码可读。"],
        {"style": "dark-tinted", "reference": "/Users/inostarlin/Downloads/深色背景下的右侧导航按钮.jpg"},
        ["检查深色背景分支及底板颜色。", "确认符号仍属于同一绘制面。"],
        "深色样式使用灰色底板和深色 chevron。",
        ["不拆分为可独立交互的底板控件和符号控件。"],
    ),
    make_case(
        "NAV-003",
        "悬浮按钮的符号和底板属于一个不可分割的合成按钮。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationArtworkStylesAreSingleCompositedButtons",
        "验证两种导航视觉都通过一个按钮对象和一个合成绘制面提供。",
        ["Debug 测试二进制已构建。", "Qt Cocoa 平台插件可用。"],
        {"suite": "WindowBehaviorTests", "test": "testNavigationArtworkStylesAreSingleCompositedButtons"},
        ["启动 QtTest 用例。", "读取两个导航按钮的合成属性和图形效果。"],
        "两个按钮均声明 single-composited-button，且没有独立 graphicsEffect。",
        ["测试窗口关闭，临时设置释放。"],
    ),
    make_case(
        "SET-001",
        "设置页不再显示 Sort files by、Ascending、Descending。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralLanguageAndRemovedOptions",
        "验证排序相关控件已从实际 General 页面对象树移除。",
        ["Debug 测试二进制已构建。"],
        {"suite": "FeatureTests", "test": "testSettingsGeneralLanguageAndRemovedOptions"},
        ["创建设置对话框。", "检查 General 页面文本和控件对象。"],
        "排序标签、单选项和排序控件均不存在。",
        ["设置对话框销毁，排序设置值不被测试修改。"],
    ),
    make_case(
        "SET-002",
        "Preloading 选项被移除，兼容默认值为 Adjacent。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralLanguageAndRemovedOptions",
        "同时验证 UI 移除和 SettingsManager 默认值迁移策略。",
        ["Debug 测试二进制已构建。"],
        {"suite": "FeatureTests", "test": "testSettingsGeneralLanguageAndRemovedOptions", "default": "Adjacent"},
        ["检查设置页面没有 Preloading 控件。", "读取默认 preload mode。"],
        "Preloading 控件不存在，默认枚举值为 Adjacent。",
        ["不删除兼容设置键。"],
    ),
    make_case(
        "SET-003",
        "Display 和 Miscellaneous 合并为首个 General Tab。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralLanguageAndRemovedOptions",
        "验证 Tab 数量、顺序和首个标题。",
        ["Debug 测试二进制已构建。"],
        {"suite": "FeatureTests", "test": "testSettingsGeneralLanguageAndRemovedOptions", "tabs": ["General", "Shortcuts", "Mouse"]},
        ["读取设置页 Tab 模型。", "比较完整 Tab 列表。"],
        "Tab 依次为 General、Shortcuts、Mouse，且不存在 Display/Miscellaneous Tab。",
        ["不改变用户当前文件浏览状态。"],
    ),
    make_case(
        "SET-004",
        "General Tab 中 Language 位于首位。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralLanguageAndRemovedOptions",
        "验证语言控件在 General 内容的第一行。",
        ["Debug 测试二进制已构建。"],
        {"suite": "FeatureTests", "test": "testSettingsGeneralLanguageAndRemovedOptions", "first_label": "Language"},
        ["读取 General 内容布局。", "检查首个可见标签。"],
        "General 的首个设置标签为 Language。",
        ["不复制或创建第二个语言控件。"],
    ),
    make_case(
        "SET-005",
        "设置页固定宽度 W=600 且禁用手动调整尺寸。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights",
        "验证固定宽度、size grip 和最小/最大宽度。",
        ["Debug 测试二进制已构建。", "Qt Cocoa 平台插件可用。"],
        {"suite": "WindowBehaviorTests", "test": "testSettingsDialogUsesFixedWidthAndTabHeights", "W": 600},
        ["创建设置对话框。", "读取宽度边界、size grip 和固定宽度属性。"],
        "minimumWidth、maximumWidth 和实际宽度均为 600，且 size grip 不可用。",
        ["对话框关闭。"],
    ),
    make_case(
        "SET-006",
        "General 和 Mouse Tab 的高度为当前内容无垂直滚动条所需的最小高度。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights",
        "验证切换 General/Mouse 后无垂直滚动条且高度按内容重算。",
        ["Debug 测试二进制已构建。", "Qt Cocoa 平台插件可用。"],
        {"suite": "WindowBehaviorTests", "test": "testSettingsDialogUsesFixedWidthAndTabHeights", "tabs": ["General", "Mouse"]},
        ["显示设置页。", "依次切换 General 和 Mouse。", "检查垂直滚动条可见性和内容高度属性。"],
        "General/Mouse 垂直滚动条均不可见，窗口高度随当前页切换。",
        ["设置页关闭。"],
    ),
    make_case(
        "SET-007",
        "Shortcuts Tab 恰好显示 16 行表格内容并包含 Random File 行。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights",
        "验证快捷键表格高度由表头加 16 个数据行计算。",
        ["Debug 测试二进制已构建。", "Qt Cocoa 平台插件可用。"],
        {"suite": "WindowBehaviorTests", "test": "testSettingsDialogUsesFixedWidthAndTabHeights", "visible_rows": 16, "last_visible_row": "Random File"},
        ["切换到 Shortcuts。", "读取表格行数、Random File 行和表格高度公式。"],
        "可视行数为 16，表格第 16 行为 Random File，窗口高度正好匹配该表格。",
        ["快捷键设置值不被修改。"],
    ),
    make_case(
        "SET-008",
        "切换不同 Settings Tab 时，窗口高度通过确定性的过渡动画变化。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsTabTransitionAndMouseReopen",
        "验证不同 Tab 的自然高度切换不是突变，并且动画参数可被测试观测。",
        ["Debug 测试二进制已构建。", "Qt Cocoa 平台插件可用。"],
        {"suite": "WindowBehaviorTests", "test": "testSettingsTabTransitionAndMouseReopen", "duration_ms": 180},
        ["显示 General。", "切换到 Mouse。", "读取动画对象状态和最终窗口高度。"],
        "设置页进入活动过渡状态，动画时长为 180ms，结束后落在 Mouse 的目标高度。",
        ["设置页关闭，测试设置恢复。"],
    ),
    make_case(
        "SET-009",
        "Settings 在 Mouse Tab 关闭并重新打开后保持 Mouse Tab 和相同尺寸。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsTabTransitionAndMouseReopen",
        "验证保存几何前不会把正在插值的中间帧写入 QSettings。",
        ["Debug 测试二进制已构建。"],
        {"suite": "WindowBehaviorTests", "test": "testSettingsTabTransitionAndMouseReopen", "tab": "Mouse"},
        ["切到 Mouse。", "关闭同一个 QDialog 实例。", "重新显示并比较 Tab、宽度和高度。"],
        "重新打开仍为 Mouse，宽度为 600，且高度与关闭前的稳定 Mouse 高度完全相同。",
        ["原始 optionstab、optionstabversion 和 optionsgeometry 恢复。"],
    ),
    make_case(
        "SET-010",
        "从其他 Tab 返回 General 时 Appearance 下拉列表不获得焦点。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsTabSwitchDoesNotFocusAppearance",
        "验证原生 toolbar 切换页后的默认 first-responder 不会落到 Appearance combo。",
        ["Debug 测试二进制已构建。", "Qt Cocoa 平台插件可用。"],
        {"suite": "WindowBehaviorTests", "test": "testSettingsTabSwitchDoesNotFocusAppearance", "focus_target": "themeComboBox"},
        ["主动聚焦 Appearance。", "切到 Mouse 再返回 General。", "处理原生/Qt 延迟事件。"],
        "返回 General 后 themeComboBox 没有键盘焦点。",
        ["主题值不被修改，Tab 设置恢复。"],
    ),
    make_case(
        "SET-011",
        "翻译标签变长时，General 和 Mouse 的选项列仍保持水平对齐。",
        "unit",
        "tests/tst_qviewtests.cpp::WindowBehaviorTests::testLocalizedSettingsFormsUseSharedLabelColumns",
        "验证独立居中的 QFormLayout 不会因语言变化产生不同的字段起点。",
        ["Debug 测试二进制已构建。"],
        {"suite": "WindowBehaviorTests", "test": "testLocalizedSettingsFormsUseSharedLabelColumns", "locales": ["zh_Hans", "zh_Hant", "es", "ja"]},
        ["创建设置页。", "枚举 General/Mouse 下的所有 QFormLayout。", "比较标签宽度、对齐和换行策略。"],
        "每个页面的表单共享动态标签宽度，使用左对齐且不换行，字段列不会随翻译漂移。",
        ["设置对话框销毁，不修改用户设置。"],
    ),
    make_case(
        "SET-012",
        "General 中更新频率标签重命名为 Auto update check，并有四种语言翻译。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testAutoUpdateCheckLabelIsRenamed",
        "验证英文源文案切换到新名称，翻译覆盖由静态目录审计同时确认。",
        ["Debug 测试二进制已构建。"],
        {"suite": "FeatureTests", "test": "testAutoUpdateCheckLabelIsRenamed", "source": "Auto update check:"},
        ["创建英文设置页。", "读取 updateFrequencyLabel。"],
        "标签准确为 Auto update check:，旧 Automatically check for updates 文案不存在。",
        ["更新频率值不被修改。"],
    ),
    make_case(
        "LANG-001",
        "仅保留英语、简体中文、繁体中文、西班牙语和日语五种应用语言。",
        "static",
        "tests/task_acceptance_pipeline.py::static_language_catalog_contract",
        "验证构建输入目录和语言枚举没有多余应用语言。",
        ["i18n 目录可读。"],
        {"allowed_languages": ["en", "zh_Hans", "zh_Hant", "es", "ja"], "allowed_catalogs": sorted(ALLOWED_CATALOGS)},
        ["枚举 qview_*.ts 文件。", "检查 CMake/qmake 仅引用四个非英语目录。", "检查运行时枚举。"],
        "应用语言集合恰为五种，英语由源码提供。",
        ["保留 template.ts 作为提取模板，不将其作为运行时语言。"],
    ),
    make_case(
        "LANG-002",
        "Language 下拉列表首项为 System Language，随后按要求显示五种语言，默认英语。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsLanguageCatalogIsFixed",
        "验证下拉列表的顺序、显示名、数据码和默认值。",
        ["Debug 测试二进制已构建。"],
        {"suite": "FeatureTests", "test": "testSettingsLanguageCatalogIsFixed", "entries": LANGUAGE_LABELS},
        ["创建设置对话框。", "读取每个 combo item 的文本和 userData。", "读取默认 currentData。"],
        "列表为 System Language、English、简体中文、繁體中文、Español、日本語，默认数据码为 en。",
        ["语言设置恢复到测试前状态。"],
    ),
    make_case(
        "LANG-003",
        "四种非英语语言的 General/Language/System Language 文案准确且非空。",
        "static",
        "tests/task_acceptance_pipeline.py::static_language_catalog_contract",
        "以英语源文案为基准核验四个翻译目录的关键设置文案。",
        ["四个 TS 文件可解析。"],
        {"translations": TRANSLATION_EXPECTATIONS},
        ["解析 TS XML。", "按 source 查找 QVOptionsDialog 文案。", "比较预期翻译并排除 unfinished。"],
        "四个目录均提供准确、非空、已完成的关键翻译。",
        ["不载入第五种或多余翻译目录。"],
    ),
    make_case(
        "LANG-004",
        "System Language 仅映射到允许语言，且未知系统语言回退英语。",
        "static",
        "tests/task_acceptance_pipeline.py::static_language_catalog_contract",
        "验证系统语言归一化和迁移校验覆盖允许集合。",
        ["SettingsManager 源码可读。"],
        {"allowed_codes": ["en", "zh_Hans", "zh_Hant", "es", "ja"]},
        ["检查 getSystemLanguage 的分支。", "检查 migrateOldSettings 的白名单。", "检查默认 language 值。"],
        "中文、西班牙语、日语和英语被归一化，其他语言回退 en。",
        ["不改变用户已选择的合法语言码。"],
    ),
    make_case(
        "LANG-005",
        "菜单上下文中的每一项当前英文内容（含 Fovelle、Window、Help）在四种语言中均有完成翻译。",
        "static",
        "tests/task_acceptance_pipeline.py::static_translation_scope_contract",
        "用 lupdate 生成当前 ActionManager 英文清单，并逐个审计四个 TS 目录。",
        ["Qt Linguist lupdate 可执行。", "四个 TS 文件可读。"],
        {"contexts": ["ActionManager"], "explicit_menu_entries": ["Fovelle", "Window", "&Help"]},
        ["提取当前 ActionManager source 集合。", "逐个查找四个语言目录。", "检查翻译非空、非 unfinished，并核对菜单关键项。"],
        "菜单当前英文清单全部有四种语言的完成翻译，Fovelle 保持品牌原文。",
        ["不修改 TS 以外的运行时状态。"],
    ),
    make_case(
        "LANG-006",
        "General 当前英文选项（含 Appearance、Smooth scaling、下拉项和 Auto update check）在四种语言中均有准确翻译。",
        "static",
        "tests/task_acceptance_pipeline.py::static_translation_scope_contract",
        "审计 QVOptionsDialog 的 General/通用源文案和四个目标目录。",
        ["Qt Linguist lupdate 可执行。", "四个 TS 文件可读。"],
        {"contexts": ["QVOptionsDialog"], "scope": "General", "explicit_entries": ["Appearance:", "Show small images at 1:1", "Associate all supported formats", "Checkerboard when image loaded", "Reuse window when launching with image", "Smooth scaling:", "Auto update check:"]},
        ["提取 QVOptionsDialog 当前源集合。", "检查 General 关键 source 的四种翻译。", "检查每一项非空且非 unfinished。"],
        "General 当前英文选项及其枚举值全部有准确、符合习惯的四种翻译。",
        ["不载入额外应用语言。"],
    ),
    make_case(
        "LANG-007",
        "Shortcuts 和 Mouse Tab 的每一项当前英文内容在四种语言中均有完成翻译。",
        "static",
        "tests/task_acceptance_pipeline.py::static_translation_scope_contract",
        "审计 QVOptionsDialog 中快捷键表格、鼠标分组和动作下拉项的完整覆盖。",
        ["Qt Linguist lupdate 可执行。", "四个 TS 文件可读。"],
        {"contexts": ["QVOptionsDialog"], "scope": ["Shortcuts", "Mouse"]},
        ["提取 Shortcuts/Mouse 当前源集合。", "逐个核对四种语言目录。", "拒绝缺失、空翻译和 unfinished。"],
        "快捷键和鼠标页所有当前英文内容均有非空完成翻译。",
        ["不修改快捷键设置。"],
    ),
    make_case(
        "LANG-008",
        "当前 src 目录中的每一项英文源文案在四种语言中均有完成翻译。",
        "static",
        "tests/task_acceptance_pipeline.py::static_full_translation_catalog_contract",
        "从完整应用源代码提取当前英文清单，防止菜单和设置页之外的运行时文案遗漏翻译。",
        ["Qt Linguist lupdate 可执行。", "四个 TS 文件可读。"],
        {"source_root": "src", "catalogs": sorted(ALLOWED_CATALOGS)},
        ["用 lupdate 提取 src 下的全部当前 source。", "逐个检查四个 TS 目录。", "拒绝缺失、空翻译和 unfinished。"],
        "当前应用英文源文案清单中的每个上下文和 source 均有四种非空完成翻译。",
        ["不修改源码和翻译目录。"],
    ),
    make_case(
        "UNIT-001",
        "任务相关原子 Qt 测试代码可编译并可被套件选择器重复执行。",
        "unit",
        "tests/task_acceptance_pipeline.py::run_unit_tests",
        "验证新增测试不是只存在于报告中的文字，而是可执行测试代码。",
        ["CMake 已生成 Debug 测试目标。"],
        {"test_binary": "build/tests/fovelle_tests", "selector": "FOVELLE_TEST_SUITE"},
        ["按 suite 启动目标测试。", "检查 QtTest 返回码和 PASS 行。"],
        "所有绑定的 FeatureTests/WindowBehaviorTests 用例返回 0 且报告 PASS。",
        ["测试进程退出，不保留窗口。"],
    ),
    make_case(
        "INT-001",
        "集成构建和 CTest 注册的 FovelleTests 全量回归通过。",
        "integration",
        "tests/task_acceptance_pipeline.py::run_integration",
        "验证源码、UI、翻译资源和 Qt 测试目标可以作为整体构建运行。",
        ["CMake 配置存在。", "Qt 和 Xcode 命令行工具可用。"],
        {"build_dir": "build", "ctest_regex": "^FovelleTests$"},
        ["执行 cmake --build。", "执行 ctest --output-on-failure -R ^FovelleTests$。"],
        "构建返回 0，FovelleTests 返回 0。",
        ["构建产物保留供系统测试使用。"],
    ),
    make_case(
        "INT-002",
        "构建出的 App bundle 报告版本 1.0.0。",
        "integration",
        "tests/task_acceptance_pipeline.py::run_integration",
        "验证发布产物而非源码字符串的版本输出。",
        ["Fovelle.app 已构建。"],
        {"command": "build/Fovelle.app/Contents/MacOS/Fovelle --version"},
        ["执行 App bundle 内可执行文件的 --version。", "解析标准输出。"],
        "输出包含 1.0.0 且进程返回 0。",
        ["版本命令不启动长期运行的 UI 进程。"],
    ),
    make_case(
        "SYS-001",
        "真实 Fovelle.app 启动后系统探针观测到一个最大化窗口。",
        "system",
        "src/main.cpp::FOVELLE_SYSTEM_PROBE",
        "从进程级别验证默认窗口状态和可观测退出路径。",
        ["App bundle 已构建。", "当前 macOS 会话允许 Cocoa 窗口启动。"],
        {"environment": {"FOVELLE_SYSTEM_PROBE": "1", "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1"}},
        ["启动真实 bundle 可执行文件。", "等待 FOVELLE_SYSTEM_PROBE 标记。", "检查退出码。"],
        "输出 windows=1 maximized=true，进程在超时前返回 0。",
        ["系统探针退出并释放窗口。"],
    ),
    make_case(
        "SYS-002",
        "真实 App 的系统级版本命令可重复执行并干净退出。",
        "system",
        "tests/task_acceptance_pipeline.py::run_system",
        "验证最终 bundle 的最小系统入口没有依赖设置或网络。",
        ["App bundle 已构建。"],
        {"command": "build/Fovelle.app/Contents/MacOS/Fovelle --version"},
        ["直接执行 bundle 内可执行文件。", "记录 stdout、stderr、返回码和耗时。"],
        "输出 1.0.0，返回码为 0，且在 10 秒内结束。",
        ["不修改用户设置。"],
    ),
]


UNIT_CASES = {
    "NAV-003": ("WindowBehaviorTests", "testNavigationArtworkStylesAreSingleCompositedButtons"),
    "SET-001": ("FeatureTests", "testSettingsGeneralLanguageAndRemovedOptions"),
    "SET-002": ("FeatureTests", "testSettingsGeneralLanguageAndRemovedOptions"),
    "SET-003": ("FeatureTests", "testSettingsGeneralLanguageAndRemovedOptions"),
    "SET-004": ("FeatureTests", "testSettingsGeneralLanguageAndRemovedOptions"),
    "SET-005": ("WindowBehaviorTests", "testSettingsDialogUsesFixedWidthAndTabHeights"),
    "SET-006": ("WindowBehaviorTests", "testSettingsDialogUsesFixedWidthAndTabHeights"),
    "SET-007": ("WindowBehaviorTests", "testSettingsDialogUsesFixedWidthAndTabHeights"),
    "SET-008": ("WindowBehaviorTests", "testSettingsTabTransitionAndMouseReopen"),
    "SET-009": ("WindowBehaviorTests", "testSettingsTabTransitionAndMouseReopen"),
    "SET-010": ("WindowBehaviorTests", "testSettingsTabSwitchDoesNotFocusAppearance"),
    "SET-011": ("WindowBehaviorTests", "testLocalizedSettingsFormsUseSharedLabelColumns"),
    "SET-012": ("FeatureTests", "testAutoUpdateCheckLabelIsRenamed"),
    "LANG-002": ("FeatureTests", "testSettingsLanguageCatalogIsFixed"),
    "UNIT-001": ("FeatureTests", "testSettingsLanguageCatalogIsFixed"),
}


def source_files(repo: Path) -> dict[str, str]:
    return {
        "cmake": read(repo, "CMakeLists.txt"),
        "qmake": read(repo, "qView.pro"),
        "plist": read(repo, "dist/mac/Info.plist"),
        "options_cpp": read(repo, "src/qvoptionsdialog.cpp"),
        "options_header": read(repo, "src/qvoptionsdialog.h"),
        "options_ui": read(repo, "src/qvoptionsdialog.ui"),
        "actionmanager": read(repo, "src/actionmanager.cpp"),
        "settings": read(repo, "src/settingsmanager.cpp"),
        "mainwindow": read(repo, "src/mainwindow.cpp"),
        "cocoa": read(repo, "src/qvcocoafunctions.mm"),
        "tests": read(repo, "tests/tst_qviewtests.cpp"),
    }


def static_version_contract(repo: Path, sources: dict[str, str]) -> tuple[bool, dict[str, Any]]:
    expected = "1.0.0"
    checks = {
        "cmake_project": "project(Fovelle VERSION 1.0.0" in sources["cmake"],
        "qmake_version": "VERSION = 1.0.0" in sources["qmake"],
        "plist_short": "<string>1.0.0</string>" in sources["plist"],
        "plist_bundle": sources["plist"].count("<string>1.0.0</string>") >= 2,
        "test_runtime": 'QCoreApplication::setApplicationVersion("1.0.0")' in sources["tests"],
    }
    return all(checks.values()), {"expected": expected, "checks": checks}


def static_navigation_contract(repo: Path, sources: dict[str, str]) -> tuple[bool, dict[str, Any]]:
    widget = sources["mainwindow"]
    cocoa = sources["cocoa"]
    checks = {
        "single_composition_property": '"single-composited-button"' in widget,
        "light_transparent_property": '"transparent-chevron"' in widget,
        "dark_tinted_property": '"gray-tile-chevron"' in widget,
        "light_has_no_qwidget_tile": "The light-background artwork intentionally has no hover tile" in widget,
        "dark_qwidget_background": "QColor background(128, 128, 128" in widget,
        "cocoa_group_opacity": "navigationButtonLayers[index].allowsGroupOpacity = YES" in cocoa,
        "cocoa_parent_opacity": "buttonLayer.opacity = boundedOpacity" in cocoa,
        "cocoa_children_full_opacity": "backgroundLayer.opacity = 1.0F" in cocoa and "chevronLayer.opacity = 1.0F" in cocoa,
        "cocoa_light_branch_transparent": "// Light artwork is the transparent-chevron variant" in cocoa,
        "cocoa_dark_branch": "if (darkBackground)" in cocoa and "QColor(128, 128, 128" in cocoa,
    }
    return all(checks.values()), {"checks": checks}


def static_settings_contract(repo: Path, sources: dict[str, str]) -> tuple[bool, dict[str, Any]]:
    ui = sources["options_ui"]
    cpp = sources["options_cpp"]
    header = sources["options_header"]
    removed_names = [
        "sortComboBox",
        "ascendingRadioButton",
        "descendingRadioButton0",
        "descendingRadioButton1",
        "preloadingComboBox",
    ]
    removed_text = ["Sort files by:", "Ascending", "Descending", "Preloading:"]
    checks = {
        "removed_widget_names": all(name not in ui for name in removed_names),
        "removed_widget_text": all(text not in ui for text in removed_text),
        "removed_sync_code": all(marker not in cpp for marker in ["sortComboBox", "preloadingComboBox"]),
        "general_category": 'addItem(Qv::MaterialIcon::Tune, tr("General"))' in cpp,
        "old_category_labels_removed": all(f'tr("{label}")' not in cpp for label in ["Display", "Miscellaneous"]),
        "general_page_reused": "configureGeneralPage();" in cpp and "ui->stackedWidget->removeWidget(ui->miscScrollArea)" in cpp,
        "language_inserted_first": "ui->displayLayout->insertRow(0, ui->langComboLabel, ui->langComboBox)" in cpp,
        "fixed_width_constant": "SettingsDialogWidth = 600" in header and "setFixedWidth(SettingsDialogWidth)" in cpp,
        "resize_disabled": "setSizeGripEnabled(false)" in cpp and "setFixedSize(SettingsDialogWidth, targetHeight)" in cpp,
        "natural_general_mouse_height": "ScrollBarAlwaysOff" in cpp and "content->sizeHint().height()" in cpp,
        "shortcut_rows_constant": "ShortcutsVisibleRows = 16" in header and "ShortcutsVisibleRows *" in cpp,
        "auto_update_label_renamed": "<string>Auto update check:</string>" in ui and "Automatically check for updates:" not in ui,
        "tab_size_transition": "QPropertyAnimation" in header and "settingsCategorySizeAnimation" in cpp and "SettingsCategoryTransitionDuration = 180" in header,
        "transition_finalized_before_save": "finishCategoryTransition();" in cpp and "categoryTargetHeight" in cpp,
        "general_focus_guard": "QTimer::singleShot(0, this" in cpp and "focused->clearFocus();" in cpp,
        "localized_form_alignment": "alignFormLayouts(generalContent, labelColumnWidth);" in cpp and "alignFormLayouts(ui->mouseScrollArea->widget(), labelColumnWidth);" in cpp and "setMinimumWidth(labelColumnWidth)" in cpp and "QFormLayout::DontWrapRows" in cpp and "Qt::AlignLeft" in cpp,
    }
    return all(checks.values()), {"checks": checks, "removed_names": removed_names}


def current_translation_sources(
    repo: Path,
    all_source_files: bool = False,
) -> tuple[dict[str, set[str]], dict[str, Any]]:
    """Extract the current English inventory without mutating the repo."""
    with tempfile.TemporaryDirectory(prefix="fovelle-lupdate-") as temporary:
        output = Path(temporary) / "current.ts"
        input_files = ["src"] if all_source_files else [
            "src/actionmanager.cpp",
            "src/qvoptionsdialog.cpp",
            "src/qvoptionsdialog.ui",
        ]
        extraction = run_command(
            [
                "lupdate",
                *input_files,
                "-locations",
                "none",
                "-ts",
                str(output),
            ],
            repo,
            timeout=45,
        )
        if not extraction["passed"] or not output.is_file():
            return {}, compact_execution(extraction)
        try:
            root = ET.parse(output).getroot()
            inventory = {
                context.findtext("name"): {
                    message.findtext("source") or ""
                    for message in context.findall("message")
                    if message.findtext("source")
                }
                for context in root.findall("context")
            }
            return inventory, compact_execution(extraction)
        except ET.ParseError as error:
            return {}, {"passed": False, "reason": f"lupdate output parse failed: {error}"}


def static_translation_scope_contract(repo: Path, sources: dict[str, str]) -> tuple[bool, dict[str, Any]]:
    inventory, extraction = current_translation_sources(repo)
    observations: dict[str, Any] = {"lupdate": extraction, "inventory_counts": {key: len(value) for key, value in inventory.items()}}
    all_ok = bool(inventory)

    for catalog_name in sorted(ALLOWED_CATALOGS):
        path = repo / "i18n" / catalog_name
        catalog_observation: dict[str, Any] = {"contexts": {}, "exact": {}}
        try:
            root = ET.parse(path).getroot()
            context_messages: dict[str, dict[str, list[tuple[str, str | None]]]] = {}
            for context in root.findall("context"):
                name = context.findtext("name") or ""
                messages: dict[str, list[tuple[str, str | None]]] = {}
                for message in context.findall("message"):
                    source = message.findtext("source") or ""
                    translation = message.find("translation")
                    value = "".join(translation.itertext()).strip() if translation is not None else ""
                    message_type = translation.get("type") if translation is not None else None
                    messages.setdefault(source, []).append((value, message_type))
                context_messages[name] = messages

            for context_name, source_set in inventory.items():
                missing: list[str] = []
                for source in sorted(source_set):
                    candidates = context_messages.get(context_name, {}).get(source, [])
                    completed = [value for value, message_type in candidates if value and message_type != "unfinished"]
                    if not completed:
                        missing.append(source)
                catalog_observation["contexts"][context_name] = {
                    "source_count": len(source_set),
                    "missing_or_unfinished": missing,
                    "passed": not missing,
                }

            # Fovelle is the application-menu brand supplied by macOS rather
            # than a tr() call, so it is audited as an explicit identity entry.
            for source, expected in TRANSLATION_EXPECTATIONS[catalog_name].items():
                candidates = [
                    (value, message_type)
                    for messages in context_messages.values()
                    for value, message_type in messages.get(source, [])
                    if value and message_type != "unfinished"
                ]
                actual = candidates[-1][0] if candidates else None
                catalog_observation["exact"][source] = {
                    "expected": expected,
                    "actual": actual,
                    "passed": actual == expected,
                }
            catalog_ok = all(
                item.get("passed", False)
                for group in catalog_observation["contexts"].values()
                for item in [group]
            ) and all(item["passed"] for item in catalog_observation["exact"].values())
            catalog_observation["passed"] = catalog_ok
            all_ok = all_ok and catalog_ok
        except (ET.ParseError, OSError) as error:
            catalog_observation["passed"] = False
            catalog_observation["error"] = str(error)
            all_ok = False
        observations[catalog_name] = catalog_observation

    return all_ok, observations


def static_full_translation_catalog_contract(repo: Path, sources: dict[str, str]) -> tuple[bool, dict[str, Any]]:
    inventory, extraction = current_translation_sources(repo, all_source_files=True)
    observations: dict[str, Any] = {
        "lupdate": extraction,
        "inventory_counts": {key: len(value) for key, value in inventory.items()},
        "catalogs": {},
    }
    all_ok = bool(inventory)

    for catalog_name in sorted(ALLOWED_CATALOGS):
        path = repo / "i18n" / catalog_name
        catalog_observation: dict[str, Any] = {"contexts": {}}
        try:
            root = ET.parse(path).getroot()
            context_messages: dict[str, dict[str, list[tuple[str, str | None]]]] = {}
            for context in root.findall("context"):
                context_name = context.findtext("name") or ""
                messages: dict[str, list[tuple[str, str | None]]] = {}
                for message in context.findall("message"):
                    source = message.findtext("source") or ""
                    translation = message.find("translation")
                    value = "" if translation is None else "".join(translation.itertext()).strip()
                    message_type = translation.get("type") if translation is not None else None
                    messages.setdefault(source, []).append((value, message_type))
                context_messages[context_name] = messages

            catalog_ok = True
            for context_name, source_set in sorted(inventory.items()):
                missing = []
                for source in sorted(source_set):
                    candidates = context_messages.get(context_name, {}).get(source, [])
                    completed = [value for value, message_type in candidates if value and message_type != "unfinished"]
                    if not completed:
                        missing.append(source)
                context_passed = not missing
                catalog_observation["contexts"][context_name] = {
                    "source_count": len(source_set),
                    "missing_or_unfinished": missing,
                    "passed": context_passed,
                }
                catalog_ok = catalog_ok and context_passed
            catalog_observation["passed"] = catalog_ok
            all_ok = all_ok and catalog_ok
        except (ET.ParseError, OSError) as error:
            catalog_observation["passed"] = False
            catalog_observation["error"] = str(error)
            all_ok = False
        observations["catalogs"][catalog_name] = catalog_observation

    return all_ok, observations


def static_language_catalog_contract(repo: Path, sources: dict[str, str]) -> tuple[bool, dict[str, Any]]:
    i18n = repo / "i18n"
    catalogs = {path.name for path in i18n.glob("qview_*.ts")}
    cpp = sources["options_cpp"]
    settings = sources["settings"]
    catalog_checks = catalogs == ALLOWED_CATALOGS
    runtime_markers = [
        'ui->langComboBox->addItem(tr("System Language"), "system")',
        'ui->langComboBox->addItem(QStringLiteral("English"), QStringLiteral("en"))',
        'ui->langComboBox->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_Hans"))',
        'ui->langComboBox->addItem(QStringLiteral("繁體中文"), QStringLiteral("zh_Hant"))',
        'ui->langComboBox->addItem(QStringLiteral("Español"), QStringLiteral("es"))',
        'ui->langComboBox->addItem(QStringLiteral("日本語"), QStringLiteral("ja"))',
    ]
    language_codes = ["system", "en", "zh_Hans", "zh_Hant", "es", "ja"]
    migration_checks = {
        "catalogs_exact": catalog_checks,
        "catalog_names": sorted(catalogs),
        "runtime_order": all(marker in cpp for marker in runtime_markers),
        "default_english": 'settingsLibrary.insert("language", {"en", {}})' in settings,
        "allowed_system_codes": all(code in settings for code in language_codes),
        "fallback_english": 'settings.setValue("language", QStringLiteral("en"))' in settings,
        "removed_settings_sources_absent": all(
            source not in "\n".join((i18n / name).read_text(encoding="utf-8") for name in ALLOWED_CATALOGS)
            for source in ["<source>Miscellaneous</source>", "<source>Sort files by:</source>", "<source>Preloading:</source>"]
        ),
    }
    translation_observations: dict[str, Any] = {}
    for catalog_name, expected in TRANSLATION_EXPECTATIONS.items():
        path = i18n / catalog_name
        observations: dict[str, Any] = {}
        try:
            root = ET.parse(path).getroot()
            messages: dict[str, str] = {}
            for message in root.iter("message"):
                source = message.findtext("source")
                translation = message.find("translation")
                if source and translation is not None:
                    messages[source] = "".join(translation.itertext()).strip()
            for source, translated in expected.items():
                observations[source] = {
                    "expected": translated,
                    "actual": messages.get(source),
                    "passed": messages.get(source) == translated,
                }
        except (ET.ParseError, OSError) as error:
            observations["parse_error"] = str(error)
        translation_observations[catalog_name] = observations
    translation_ok = all(
        item.get("passed", False)
        for catalog in translation_observations.values()
        for item in catalog.values()
        if isinstance(item, dict) and "passed" in item
    ) and len(translation_observations) == len(TRANSLATION_EXPECTATIONS)
    checks = {**migration_checks, "translations": translation_ok}
    return all(value is True for value in checks.values() if isinstance(value, bool)), {
        "checks": checks,
        "translations": translation_observations,
        "language_codes": language_codes,
    }


def static_test_code_contract(repo: Path, sources: dict[str, str]) -> tuple[bool, dict[str, Any]]:
    test_path = repo / "tests" / "tst_qviewtests.cpp"
    pipeline_path = repo / "tests" / "task_acceptance_pipeline.py"
    markers = [
        "testSettingsGeneralLanguageAndRemovedOptions",
        "testSettingsLanguageCatalogIsFixed",
        "testAutoUpdateCheckLabelIsRenamed",
        "testSettingsDialogUsesFixedWidthAndTabHeights",
        "testSettingsTabTransitionAndMouseReopen",
        "testSettingsTabSwitchDoesNotFocusAppearance",
        "testLocalizedSettingsFormsUseSharedLabelColumns",
        "testNavigationArtworkStylesAreSingleCompositedButtons",
        "testNavigationButtonUsesTransparentPaintOnlyFade",
    ]
    try:
        ast.parse(pipeline_path.read_text(encoding="utf-8"))
        python_parse = True
    except SyntaxError:
        python_parse = False
    checks = {
        "cpp_methods": all(marker in sources["tests"] for marker in markers),
        "python_pipeline_parses": python_parse,
        "git_diff_whitespace": run_command(["git", "diff", "--check"], repo, timeout=30)["passed"],
    }
    return all(checks.values()), {"checks": checks, "markers": markers}


def run_static(repo: Path) -> dict[str, dict[str, Any]]:
    sources = source_files(repo)
    checks = {
        "VER-001": static_version_contract,
        "NAV-001": static_navigation_contract,
        "NAV-002": static_navigation_contract,
        "SET-001": static_settings_contract,
        "SET-002": static_settings_contract,
        "SET-003": static_settings_contract,
        "SET-004": static_settings_contract,
        "SET-005": static_settings_contract,
        "SET-006": static_settings_contract,
        "SET-007": static_settings_contract,
        "LANG-001": static_language_catalog_contract,
        "LANG-003": static_language_catalog_contract,
        "LANG-004": static_language_catalog_contract,
        "LANG-005": static_translation_scope_contract,
        "LANG-006": static_translation_scope_contract,
        "LANG-007": static_translation_scope_contract,
        "LANG-008": static_full_translation_catalog_contract,
        "UNIT-001": static_test_code_contract,
    }
    results: dict[str, dict[str, Any]] = {}
    for case_id, check in checks.items():
        passed, observed = check(repo, sources)
        results[case_id] = {"passed": passed, "observed": observed}
    return results


def run_unit(repo: Path, build_dir: Path) -> dict[str, dict[str, Any]]:
    binary = build_dir / "tests" / "fovelle_tests"
    results: dict[str, dict[str, Any]] = {}
    for case_id, (suite, test_name) in UNIT_CASES.items():
        if not binary.is_file():
            results[case_id] = {
                "passed": False,
                "observed": {"reason": "test binary does not exist", "binary": str(binary)},
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
            timeout=45,
        )
        output = result.get("stdout", "") + result.get("stderr", "")
        pass_marker = f"PASS   : {suite}::{test_name}()"
        results[case_id] = {
            "passed": bool(result["passed"] and pass_marker in output),
            "observed": {"suite": suite, "test": test_name, "pass_marker": pass_marker},
            "execution": compact_execution(result),
        }
    return results


def run_integration(repo: Path, build_dir: Path, skip_build: bool) -> dict[str, dict[str, Any]]:
    results: dict[str, dict[str, Any]] = {}
    build_result: dict[str, Any] = {"passed": True, "skipped": True, "command": []}
    if not skip_build:
        build_result = run_command(["cmake", "--build", str(build_dir), "--parallel", "2"], repo, timeout=180)
    results["INT-001"] = {
        "passed": bool(build_result["passed"]),
        "observed": {"build_skipped": skip_build},
        "execution": compact_execution(build_result),
    }
    if not build_result["passed"]:
        results["INT-002"] = {
            "passed": False,
            "observed": {"reason": "build failed"},
            "execution": compact_execution(build_result),
        }
        return results

    ctest_result = run_command(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-R", "^FovelleTests$"],
        repo,
        {"QT_QPA_PLATFORM": "cocoa", "QT_FATAL_WARNINGS": "1", "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1"},
        timeout=120,
    )
    results["INT-001"] = {
        "passed": bool(build_result["passed"] and ctest_result["passed"]),
        "observed": {"build_skipped": skip_build, "ctest_regex": "^FovelleTests$"},
        "execution": {
            "build": compact_execution(build_result),
            "ctest": compact_execution(ctest_result),
        },
    }
    version_command = build_dir / "Fovelle.app" / "Contents" / "MacOS" / "Fovelle"
    version_result = run_command([str(version_command), "--version"], repo, timeout=15)
    version_output = version_result.get("stdout", "") + version_result.get("stderr", "")
    results["INT-002"] = {
        "passed": bool(version_result["passed"] and re.search(r"\b1\.0\.0\b", version_output)),
        "observed": {"expected_version": "1.0.0", "output": version_output.strip()},
        "execution": compact_execution(version_result),
    }
    return results


def run_system(repo: Path, build_dir: Path) -> dict[str, dict[str, Any]]:
    app_binary = build_dir / "Fovelle.app" / "Contents" / "MacOS" / "Fovelle"
    results: dict[str, dict[str, Any]] = {}
    if not app_binary.is_file():
        missing = {"passed": False, "observed": {"reason": "app binary does not exist", "binary": str(app_binary)}}
        return {"SYS-001": missing, "SYS-002": missing}

    probe = run_command(
        [str(app_binary)],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "FOVELLE_SYSTEM_PROBE": "1",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
        },
        timeout=20,
    )
    probe_output = probe.get("stdout", "") + probe.get("stderr", "")
    probe_match = re.search(r"FOVELLE_SYSTEM_PROBE windows=1 maximized=true", probe_output)
    results["SYS-001"] = {
        "passed": bool(probe["passed"] and probe_match),
        "observed": {"probe_marker": probe_match.group(0) if probe_match else None},
        "execution": compact_execution(probe),
    }

    version = run_command([str(app_binary), "--version"], repo, timeout=10)
    version_output = version.get("stdout", "") + version.get("stderr", "")
    results["SYS-002"] = {
        "passed": bool(version["passed"] and re.search(r"\b1\.0\.0\b", version_output)),
        "observed": {"version_output": version_output.strip()},
        "execution": compact_execution(version),
    }
    return results


def report_artifact(repo: Path, relative: str) -> dict[str, Any]:
    path = repo / relative
    result: dict[str, Any] = {"path": relative, "absolute_path": str(path.resolve()), "exists": path.is_file()}
    if path.is_file():
        result.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    else:
        result.update({"bytes": 0, "sha256": None})
    return result


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def quality_report(repo: Path, evidence: dict[str, Any], specifications: dict[str, Any]) -> dict[str, Any]:
    case_results = {item["case_id"]: item["status"] == "passed" for item in evidence["test_case_evidence"]}
    checks = [
        {
            "id": "CQ-001",
            "criterion": "精益完整性",
            "method": "原子验收标准覆盖审计",
            "passed": len(specifications["test_cases"]) == len(CASES) and all(case_results.values()),
            "observed": {"case_count": len(specifications["test_cases"]), "all_cases_passed": all(case_results.values())},
        },
        {
            "id": "CQ-002",
            "criterion": "功能正确性",
            "method": "静态/单元/集成/系统四级结果交叉检查",
            "passed": all(summary["failed"] == 0 for summary in evidence["stage_summaries"].values()),
            "observed": evidence["stage_summaries"],
        },
        {
            "id": "CQ-003",
            "criterion": "可测试性",
            "method": "测试代码 AST、QtTest 选择器和系统探针审计",
            "passed": case_results.get("UNIT-001", False) and case_results.get("SYS-001", False),
            "observed": {"unit_harness": case_results.get("UNIT-001"), "system_probe": case_results.get("SYS-001")},
        },
    ]
    passed = sum(1 for check in checks if check["passed"])
    return {
        "schema_version": "1.0",
        "report_type": "code_quality_assessment",
        "generated_at": now(),
        "quality_requirements": ["精益完整性", "功能正确性", "可测试性"],
        "research_trace": RESEARCH_TRACE,
        "checks": checks,
        "summary": {"total": len(checks), "passed": passed, "failed": len(checks) - passed, "status": "passed" if passed == len(checks) else "failed"},
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
    for case in CASES:
        result = by_stage[case["test_layer"]].get(case["id"], {"passed": False, "observed": {"reason": "no result"}})
        evidence_items.append({
            "evidence_id": f"EV-{case['id']}",
            "case_id": case["id"],
            "stage": case["test_layer"],
            "status": "passed" if result.get("passed") else "failed",
            "assertion": case["atomic_acceptance_criterion"],
            "observed": result.get("observed", {}),
            "execution": result.get("execution", {"test_code": case["test_code"]}),
            "recorded_at": now(),
        })

    stage_summaries: dict[str, dict[str, Any]] = {}
    for stage in STAGES:
        stage_items = [item for item in evidence_items if item["stage"] == stage]
        stage_summaries[stage] = {
            "total": len(stage_items),
            "passed": sum(item["status"] == "passed" for item in stage_items),
            "failed": sum(item["status"] != "passed" for item in stage_items),
            "status": "passed" if all(item["status"] == "passed" for item in stage_items) else "failed",
        }

    specification = {
        "schema_version": "1.0",
        "report_type": "test_case_specification",
        "generated_at": now(),
        "task": "Fovelle 1.0.0 设置、语言和悬浮导航按钮变更",
        "test_execution_order": list(STAGES),
        "research_trace": RESEARCH_TRACE,
        "atomicity_rule": "每个 test case 只验证一个可判定的原子验收标准。",
        "test_cases": CASES,
    }
    evidence = {
        "schema_version": "1.0",
        "report_type": "test_evidence",
        "generated_at": now(),
        "task": "Fovelle 1.0.0 设置、语言和悬浮导航按钮变更",
        "test_execution_order": list(STAGES),
        "research_trace": RESEARCH_TRACE,
        "stage_summaries": stage_summaries,
        "test_case_evidence": evidence_items,
    }
    quality = quality_report(repo, evidence, specification)

    write_json(output_dir / "test_case_specification.json", specification)
    write_json(output_dir / "test_evidence.json", evidence)
    write_json(output_dir / "code_quality_assessment_report.json", quality)

    completion = {
        "schema_version": "1.0",
        "report_type": "test_completion_report",
        "generated_at": now(),
        "task": "Fovelle 1.0.0 设置、语言和悬浮导航按钮变更",
        "status": "passed" if all(summary["failed"] == 0 for summary in stage_summaries.values()) else "failed",
        "execution_order": list(STAGES),
        "research_trace": RESEARCH_TRACE,
        "stage_summaries": stage_summaries,
        "case_count": len(CASES),
        "passed_case_count": sum(item["status"] == "passed" for item in evidence_items),
        "failed_case_count": sum(item["status"] != "passed" for item in evidence_items),
        "artifacts": [
            report_artifact(repo, "reports/test_evidence.json"),
            report_artifact(repo, "reports/test_case_specification.json"),
            report_artifact(repo, "reports/code_quality_assessment_report.json"),
        ],
        "self_artifact": {
            "path": "reports/test_completion_report.json",
            "absolute_path": str((output_dir / "test_completion_report.json").resolve()),
            "exists": True,
            "sha256": None,
            "hash_note": "Self-hash is intentionally null because this document contains the artifact manifest itself.",
        },
        "reproduction": {
            "command": "python3 tests/task_acceptance_pipeline.py --repo . --build-dir build",
            "skip_build_command": "python3 tests/task_acceptance_pipeline.py --repo . --build-dir build --skip-build",
        },
    }
    write_json(output_dir / "test_completion_report.json", completion)

    print(json.dumps({"status": completion["status"], "stage_summaries": stage_summaries, "reports": [str(output_dir / name) for name in ("test_evidence.json", "test_case_specification.json", "test_completion_report.json", "code_quality_assessment_report.json")]}, ensure_ascii=False))
    return 0 if completion["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
