#!/usr/bin/env python3
"""Audit the Settings-page change through four deterministic test stages.

The audit is intentionally scoped to the requested Settings behavior.  It
checks source contracts and catalogs, runs the focused QtTest suites, starts
the real macOS application through its opt-in probe, and emits the three JSON
documents required by the task.
"""

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


STAGE_ORDER = ("static", "unit", "integration", "system")
LANGUAGES = ("en", "es", "ja", "zh_Hans", "zh_Hant")
CATALOGS = tuple(f"qview_{language}.ts" for language in LANGUAGES[1:])
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


def make_case(
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
    """Create one atomic case with the six requested test-design fields."""

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
    make_case(
        "SET-LABEL-CHECKERBOARD",
        "通用 Tab 将加载图片时显示棋盘格重命名为打开图片后使用棋盘格背景。",
        "验证棋盘格复选框在实际设置对话框中使用新的英文源文案。",
        "QVApplication 已初始化，设置对话框可构造。",
        "英文语言和 checkerboardBackgroundCheckbox。",
        "构造 QVOptionsDialog，读取复选框文本并与新文案比较。",
        "文本精确等于 Use checkerboard background after opening image。",
        "销毁对话框并恢复临时语言设置。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions",
        ("static", "unit"),
    ),
    make_case(
        "SET-LABEL-SAME-WINDOW",
        "通用 Tab 将使用图片启动时重用窗口重命名为使用同一窗口打开图片。",
        "验证复用窗口复选框的运行时文案和源码文案一致。",
        "QVOptionsDialog 可构造且英文源翻译已安装。",
        "英文 reuseWindowCheckbox。",
        "构造对话框，读取控件文本并比较目标字符串。",
        "文本精确等于 Open images in the same window。",
        "关闭对话框，不改变 reusewindow 的持久化值。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions",
        ("static", "unit"),
    ),
    make_case(
        "SET-LABEL-AFTER-DELETE",
        "删除后标签重命名为删除文件后。",
        "验证删除策略标签的源文案与可见文本均已更新。",
        "QVOptionsDialog 可构造，afterDeletionComboBox 所在 General 页面已组装。",
        "label_10 的可见文本。",
        "构造对话框并读取 label_10 文本。",
        "英文文本精确等于 After deleting files:。",
        "销毁对话框且不执行删除操作。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions",
        ("static", "unit"),
    ),
    make_case(
        "SET-OPTION-NO-ACTION",
        "删除文件后的下拉项不执行重命名为无动作。",
        "验证 DoNothing 枚举仍保留行为值，但显示文本已改为 No Action。",
        "afterDeletionComboBox 已填充枚举项。",
        "Qv::AfterDelete::DoNothing 的 itemData 和 itemText。",
        "按枚举值查找下拉项，读取显示文本。",
        "对应数据值仍是 DoNothing，英文显示文本精确等于 No Action。",
        "销毁对话框，不触发删除策略。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions",
        ("static", "unit"),
    ),
    make_case(
        "SET-MOUSE-NAVIGATION-REMOVED",
        "鼠标 Tab 移除侧边导航区域选项，并固定默认值为不勾选。",
        "验证 UI 不再暴露该选项，同时新安装和迁移后的设置值都是 false。",
        "SettingsManager 默认库已初始化，QVOptionsDialog 可构造。",
        "navigationRegionsCheckbox 对象查找、navigationregionsenabled 默认值。",
        "读取默认设置并搜索生产对话框对象树。",
        "checkbox 不存在，navigationregionsenabled 的默认值为 false。",
        "关闭对话框，不改变其它导航按钮行为。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions",
        ("static", "unit", "integration"),
    ),
    make_case(
        "SET-MOUSE-MODE-REMOVED",
        "鼠标 Tab 移除中键模式选项，并固定默认值为点击。",
        "验证模式标签、单选按钮和不可达的中键拖动设置行均不再出现在界面，内部默认保持 Click。",
        "SettingsManager 默认库已初始化，Mouse 页面可构造。",
        "middleButtonModeLabel/Host/radio 控件、中键拖动控件和 viewportmiddlebuttonmode 默认值。",
        "读取默认枚举并在生产对话框对象树中查找所有已移除控件。",
        "模式及其依赖控件不存在，viewportmiddlebuttonmode 默认值为 Click。",
        "销毁对话框，不产生鼠标输入。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions",
        ("static", "unit", "integration"),
    ),
    make_case(
        "SET-MOUSE-LEGACY-MIGRATION",
        "旧配置中的侧边导航和中键模式值不能在移除选项后复活。",
        "验证迁移逻辑将旧的 true/Drag 配置写回 false/Click。",
        "QSettings 可写，firstlaunch 已标记，SettingsManager 迁移函数可调用。",
        "navigationregionsenabled=true、viewportmiddlebuttonmode=Drag。",
        "保存旧值，调用 migrateOldSettings()，同步并分别读取持久化值与内存值。",
        "持久化和内存结果分别为 false 与 Click。",
        "恢复迁移前的 QSettings 值。",
        "unit",
        "tests/tst_qviewtests.cpp::FeatureTests::testRemovedMouseSettingsMigrateToFixedDefaults",
        ("static", "unit"),
    ),
    make_case(
        "SET-ALL-TRANSLATIONS",
        "四个非英语目录同步更新所有重命名文案，并删除已移除选项的旧翻译。",
        "验证 TS XML 可解析、目标 source 有完成翻译且精确匹配需求语言。",
        "四个 TS 文件和 XML 解析器可用。",
        "五个新 source：两个 General 标签、删除文件后标签、No Action，以及四套期望译文。",
        "逐目录解析 XML，按 source 查找 translation，检查非空、非 unfinished 和精确文本；检查旧 source 不存在。",
        "西班牙语、日语、简体中文、繁体中文均通过，中文目标文案与需求逐字一致。",
        "不加载第五种语言，不改动运行时设置。",
        "static/integration",
        "tests/settings_quality_pipeline.py::static_stage + tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage",
        ("static", "integration"),
    ),
    make_case(
        "SET-CI-FOUR-STAGES",
        "静态、单元、集成、系统四级检查按顺序执行并可在 GitHub Actions 重放。",
        "验证工作流使用仓库声明的 macOS runner、Qt 构建参数、CTest 和报告 artifact。",
        "GitHub Actions workflow、CMake 测试注册和已构建测试/应用目标可读。",
        "test.yml runner/build/test/artifact 步骤与四级审计命令。",
        "静态检查 workflow 契约，依次运行 FeatureTests、WindowBehaviorTests 和真实 Fovelle system probe。",
        "四级结果可观测、顺序明确，三份 JSON 报告写入 reports/ 并可作为 Actions artifact 上传。",
        "保留完整命令、返回码、耗时和输出摘要，失败时返回非零。",
        "system",
        "tests/settings_quality_pipeline.py::build_reports + .github/workflows/test.yml",
        ("static", "unit", "integration", "system"),
    ),
    make_case(
        "CI-UNIT-002-ASYNC-OBSERVATION",
        "修复 CI-UNIT-002：滚动测试不得要求异步 refinement 在某一瞬间仍处于 pending，且 5% 判定只使用滚动后的首个 Paint。",
        "验证 GitHub Actions 失败的唯一观察器修复既接受异步任务已提前完成的合法时序，又保留局部滚动重绘断言。",
        "已构建 fovelle_tests，macOS Cocoa 事件循环可用，解决方案与证明文件存在。",
        "GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip、首个 Paint dirty ratio 和 hasPendingVectorRefinement teardown 状态。",
        "执行静态异步观察合同；直接运行 CI-UNIT-002；检查首个 Paint 的 5% 断言和无瞬时 pending 必须为真的源码合同。",
        "静态合同通过，QtTest 返回 0，首个 Paint dirty ratio 不超过 5%，并在移除 event filter 前等待 refinement 收敛。",
        "测试窗口关闭，event filter 已移除；不修改生产渲染实现。",
        "unit",
        "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip + reports/solution_and_proof.md",
        ("static", "unit"),
    ),
)


TRANSLATIONS = {
    "qview_es.ts": {
        "Use checkerboard background after opening image": "Usar fondo de tablero de ajedrez después de abrir la imagen",
        "Open images in the same window": "Abrir imágenes en la misma ventana",
        "After deleting files:": "Después de borrar archivos:",
        "No Action": "Sin acción",
    },
    "qview_ja.ts": {
        "Use checkerboard background after opening image": "画像を開いた後にチェック柄の背景を使用",
        "Open images in the same window": "画像を同じウィンドウで開く",
        "After deleting files:": "ファイル削除後の動作:",
        "No Action": "アクションなし",
    },
    "qview_zh_Hans.ts": {
        "Use checkerboard background after opening image": "打开图片后使用棋盘格背景",
        "Open images in the same window": "使用同一窗口打开图片",
        "After deleting files:": "删除文件后:",
        "No Action": "无动作",
    },
    "qview_zh_Hant.ts": {
        "Use checkerboard background after opening image": "開啟影像後使用棋盤格背景",
        "Open images in the same window": "使用同一視窗開啟影像",
        "After deleting files:": "刪除檔案後:",
        "No Action": "無動作",
    },
}
NEW_SOURCES = tuple(next(iter(TRANSLATIONS.values())).keys())
OLD_SOURCES = (
    "Checkerboard when image loaded",
    "Reuse window when launching with image",
    "After deletion:",
    "Do Nothing",
    "Side navigation regions",
    "Navigate when clicking on the left/right edges of the viewport",
    "Mode:",
    "Middle Drag:",
    "%1 + Middle Drag:",
)
REMOVED_UI_MARKERS = (
    "navigationRegionsCheckbox",
    "middleButtonModeLabel",
    "middleButtonModeHost",
    "middleButtonModeClickRadioButton",
    "middleButtonModeDragRadioButton",
    "middleDragLabel",
    "middleDragComboBox",
    "altMiddleDragLabel",
    "altMiddleDragComboBox",
)


RESEARCH_TRACE = [
    {
        "hop": 1,
        "source": "https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax",
        "finding": "GitHub Actions jobs select their execution environment through the workflow runs-on field.",
        "explicit_premise": "The repository test workflow declares macos-26 for the build, static, and test jobs.",
        "deduction": "The CI contract must be checked against the workflow file, not inferred from a local machine.",
    },
    {
        "hop": 2,
        "source": "https://docs.github.com/en/actions/reference/runners/github-hosted-runners",
        "finding": "GitHub-hosted runner labels identify the hosted operating-system image used by a job.",
        "explicit_premise": "The application uses Cocoa-specific Qt tests and the workflow installs the pinned Qt version on macOS.",
        "deduction": "The system gate is intentionally a macOS application probe and is not replaced by a headless non-Cocoa substitute.",
    },
    {
        "hop": 3,
        "source": "https://docs.github.com/en/actions/tutorials/store-and-share-data",
        "finding": "A workflow can upload generated files as artifacts after a test step, including when the test step fails.",
        "explicit_premise": "The required reports are generated under the repository reports/ directory and that directory is ignored by Git.",
        "deduction": "The workflow uploads reports/*.json so CI evidence remains machine-auditable after the run.",
    },
    {
        "hop": 4,
        "source": "https://docs.github.com/en/actions/concepts/workflows-and-actions/workflow-artifacts",
        "finding": "Artifacts preserve files produced by a workflow for later inspection outside the runner workspace.",
        "explicit_premise": "The local report paths are the acceptance interface, while the CI runner is ephemeral.",
        "deduction": "The report artifact is part of the CI acceptance contract rather than an optional diagnostic side effect.",
    },
    {
        "hop": 5,
        "source": "https://github.com/inostarlin-passion/Fovelle/blob/main/.github/workflows/test.yml",
        "finding": "The repository's test workflow configures BUILD_TESTS, enables translations, builds with CMake, and runs CTest.",
        "explicit_premise": "The focused audit is registered as a CTest test and receives the built test binary and build directory.",
        "deduction": "The same audit command can be run locally and as a GitHub Actions check without a second CI-only implementation.",
    },
    {
        "hop": 6,
        "source": "local:src/qvgraphicsview.cpp",
        "finding": "The graphics view reads navigationregionsenabled and viewportmiddlebuttonmode at runtime.",
        "explicit_premise": "Removing only the visible controls would leave old persisted values able to change behavior.",
        "deduction": "Keep compatibility keys, reset them during migration, and remove only their user-facing controls; fixed policies are false and Click.",
    },
]


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256_bytes(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8", errors="replace")).hexdigest()


def file_sha256(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return ""


def execute(
    command: list[str],
    cwd: Path,
    environment: dict[str, str] | None = None,
    timeout: int = 120,
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
        output = (completed.stdout or "") + (completed.stderr or "")
        return {
            "command": [str(part) for part in command],
            "return_code": completed.returncode,
            "passed": completed.returncode == 0,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "output_sha256": sha256_bytes(output),
            "output_tail": output[-12000:],
        }
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        output = stdout + stderr
        return {
            "command": [str(part) for part in command],
            "return_code": None,
            "passed": False,
            "duration_ms": round((time.monotonic() - started) * 1000, 3),
            "timeout_seconds": timeout,
            "output_sha256": sha256_bytes(output),
            "output_tail": output[-12000:],
        }


def check(
    identifier: str,
    passed: bool,
    evidence: Any,
    deduction: str,
) -> dict[str, Any]:
    return {
        "id": identifier,
        "passed": bool(passed),
        "evidence": evidence,
        "deduction": deduction,
    }


def parse_catalog(path: Path) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    root = ET.parse(path).getroot()
    for message in root.iter("message"):
        source = message.findtext("source")
        translation_node = message.find("translation")
        if not source or translation_node is None:
            continue
        translation = "".join(translation_node.itertext()).strip()
        result[source] = {
            "translation": translation,
            "unfinished": translation_node.get("type") == "unfinished",
        }
    return result


def static_stage(repo: Path) -> dict[str, Any]:
    ui = read_text(repo / "src/qvoptionsdialog.ui")
    options_cpp = read_text(repo / "src/qvoptionsdialog.cpp")
    options_header = read_text(repo / "src/qvoptionsdialog.h")
    settings_cpp = read_text(repo / "src/settingsmanager.cpp")
    test_cpp = read_text(repo / "tests/tst_qviewtests.cpp")
    workflow = read_text(repo / ".github/workflows/test.yml")
    cmake = read_text(repo / "tests/CMakeLists.txt")
    pipeline = read_text(repo / "tests/settings_quality_pipeline.py")
    ci_pipeline = read_text(repo / "tests/ci_quality_pipeline.py")
    solution = read_text(repo / "reports/solution_and_proof.md")
    gitignore = read_text(repo / ".gitignore")
    production_text = "\n".join((ui, options_cpp, options_header))
    checks: list[dict[str, Any]] = []

    try:
        ET.parse(repo / "src/qvoptionsdialog.ui")
        ui_xml_valid = True
        ui_xml_error = None
    except (ET.ParseError, OSError) as error:
        ui_xml_valid = False
        ui_xml_error = str(error)
    checks.append(check(
        "STATIC-UI-XML",
        ui_xml_valid,
        {"path": "src/qvoptionsdialog.ui", "error": ui_xml_error},
        "The Qt Designer form must be well-formed before it can be compiled.",
    ))

    required_source_markers = {
        "checkerboard": "Use checkerboard background after opening image" in ui,
        "same_window": "Open images in the same window" in ui,
        "after_delete": "After deleting files:" in ui,
        "no_action": 'tr("No Action")' in options_cpp,
    }
    checks.append(check(
        "STATIC-RENAMED-SOURCES",
        all(required_source_markers.values()),
        required_source_markers,
        "The production UI and combo-box mapping must contain the four new source labels.",
    ))

    removed_markers = {
        marker: marker not in production_text for marker in REMOVED_UI_MARKERS
    }
    checks.append(check(
        "STATIC-REMOVED-MOUSE-SURFACE",
        all(removed_markers.values()),
        removed_markers,
        "Removed Mouse controls must not remain in the production dialog object tree or its synchronization code.",
    ))

    try:
        ui_root = ET.fromstring(ui)
        production_sources = {
            node.text for node in ui_root.iter("string") if node.text
        }
    except ET.ParseError:
        production_sources = set()
    production_sources.update(re.findall(r'tr\("([^"]+)"\)', options_cpp))
    catalog_sources: set[str] = set()
    for name in CATALOGS:
        try:
            catalog_sources.update(parse_catalog(repo / "i18n" / name).keys())
        except (ET.ParseError, OSError):
            pass
    old_markers = {
        source: source not in production_sources and source not in catalog_sources
        for source in OLD_SOURCES
    }
    checks.append(check(
        "STATIC-OBSOLETE-SOURCES",
        all(old_markers.values()),
        old_markers,
        "Old visible labels and removed-option tooltip sources must not be shipped in the production catalogs.",
    ))

    default_markers = {
        "navigation_default_false": bool(re.search(
            r'settingsLibrary\.insert\("navigationregionsenabled",\s*\{false,\s*\{\}\}\);',
            settings_cpp,
        )),
        "middle_mode_default_click": 'settingsLibrary.insert("viewportmiddlebuttonmode", {static_cast<int>(Qv::ClickOrDrag::Click), {} });' in settings_cpp
        or 'settingsLibrary.insert("viewportmiddlebuttonmode", {static_cast<int>(Qv::ClickOrDrag::Click), {}});' in settings_cpp,
        "migration_navigation_false": '{ "navigationregionsenabled", false }' in settings_cpp,
        "migration_middle_click": '{ "viewportmiddlebuttonmode", static_cast<int>(Qv::ClickOrDrag::Click) }' in settings_cpp,
    }
    checks.append(check(
        "STATIC-FIXED-MOUSE-POLICIES",
        all(default_markers.values()),
        default_markers,
        "New defaults and legacy migration must converge on the same fixed policies.",
    ))

    catalog_observations: dict[str, Any] = {}
    catalogs_passed = True
    for name in CATALOGS:
        path = repo / "i18n" / name
        observation: dict[str, Any] = {"path": str(path), "entries": {}}
        try:
            messages = parse_catalog(path)
            for source, expected in TRANSLATIONS[name].items():
                actual = messages.get(source)
                observation["entries"][source] = {
                    "expected": expected,
                    "actual": actual,
                    "passed": bool(
                        actual
                        and actual["translation"] == expected
                        and not actual["unfinished"]
                    ),
                }
            observation["passed"] = all(item["passed"] for item in observation["entries"].values())
        except (ET.ParseError, OSError) as error:
            observation["error"] = str(error)
            observation["passed"] = False
        catalogs_passed = catalogs_passed and observation["passed"]
        catalog_observations[name] = observation
    checks.append(check(
        "STATIC-TRANSLATIONS",
        catalogs_passed,
        catalog_observations,
        "Every renamed source must have an exact, completed translation in all four supported catalogs.",
    ))

    template_path = repo / "i18n/template.ts"
    try:
        template_sources = set(parse_catalog(template_path).keys())
    except (ET.ParseError, OSError):
        template_sources = set()
    template_markers = {
        source: source in template_sources for source in NEW_SOURCES
    }
    template_markers["removed_sources_absent"] = all(
        source not in template_sources for source in OLD_SOURCES
    )
    checks.append(check(
        "STATIC-TRANSLATION-TEMPLATE",
        all(template_markers.values()),
        template_markers,
        "The translation template must describe the current source inventory without removed option messages.",
    ))

    test_markers = {
        "renamed_labels_test": "testSettingsRenamedLabelsAndRemovedMouseOptions" in test_cpp,
        "migration_test": "testRemovedMouseSettingsMigrateToFixedDefaults" in test_cpp,
        "all_language_layout_test": "testSettingsEveryTabFitsEveryLanguage" in test_cpp,
        "fixed_default_assertion": 'getBoolean("navigationregionsenabled", true)' in test_cpp,
        "fixed_mode_assertion": 'getEnum<Qv::ClickOrDrag>("viewportmiddlebuttonmode", true)' in test_cpp,
    }
    checks.append(check(
        "STATIC-TEST-COVERAGE",
        all(test_markers.values()),
        test_markers,
        "Each UI/default/migration contract must have executable QtTest coverage.",
    ))

    workflow_markers = {
        "macos_runner": "runs-on: macos-26" in workflow,
        "tests_enabled": "-DBUILD_TESTS=ON" in workflow,
        "translations_enabled": "-DFOVELLE_BUILD_TRANSLATIONS=ON" in workflow,
        "ctest": "ctest --test-dir build --output-on-failure" in workflow,
        "report_artifact": "uses: actions/upload-artifact@v4" in workflow,
        "report_path": "path: reports/*.json" in workflow,
        "four_stage_runner": 'STAGE_ORDER = ("static", "unit", "integration", "system")' in pipeline,
        "audit_registered": "NAME FovelleSettingsAudit" in cmake,
        "other_audit_isolated": '--output-dir "${CMAKE_BINARY_DIR}/ci-reports"' in cmake,
    }
    checks.append(check(
        "STATIC-GITHUB-ACTIONS",
        all(workflow_markers.values()),
        workflow_markers,
        "The repository workflow must build translations, run CTest, and preserve the required reports without overwriting them.",
    ))

    async_observation_markers = {
        "solution_file": bool(
            solution
            and "## 3. 唯一解决方案" in solution
            and "## 4. 数学正确性证明" in solution
            and "## 4.4 唯一性" in solution
        ),
        "solution_file_not_ignored": "!reports/solution_and_proof.md" in gitignore,
        "first_paint_assertion": "const qint64 scrollPaintArea = recorder.recordedAreas().constFirst();" in test_cpp,
        "teardown_wait": "QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000);" in test_cpp,
        "no_instant_pending_assertion": "bar->setValue(bar->value() + 6);\n    QVERIFY(view->hasPendingVectorRefinement());" not in test_cpp,
        "ci_static_guard": "no_instant_pending_assertion" in ci_pipeline,
    }
    checks.append(check(
        "STATIC-CI-ASYNC-OBSERVATION",
        all(async_observation_markers.values()),
        async_observation_markers,
        "The CI repair must document and enforce causal Paint observation without an instantaneous asynchronous-state precondition.",
    ))

    try:
        ast.parse(pipeline, filename="tests/settings_quality_pipeline.py")
        python_valid = True
        python_error = None
    except SyntaxError as error:
        python_valid = False
        python_error = str(error)
    checks.append(check(
        "STATIC-AUDIT-PYTHON",
        python_valid,
        {"error": python_error},
        "The audit itself must be syntactically valid before it can be trusted.",
    ))

    diff = execute(
        ["git", "diff", "--check", "HEAD", "--", "src", "i18n", "tests", ".github"],
        repo,
        timeout=30,
    )
    checks.append(check(
        "STATIC-DIFF",
        diff["passed"],
        diff,
        "Task-scoped source, test, translation, and workflow changes must have no whitespace errors.",
    ))

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


def qtest_stage(binary: Path, repo: Path, suite: str) -> dict[str, Any]:
    if not binary.is_file():
        return {
            "stage": suite,
            "suite": suite,
            "passed": False,
            "error": f"test binary does not exist: {binary}",
        }
    result = execute(
        [str(binary), "-o", "-,txt"],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "QV_DISABLE_ONLINE_VERSION_CHECK": "1",
            "FOVELLE_TEST_SUITE": suite,
        },
        timeout=180,
    )
    totals = parse_qtest_totals(result["output_tail"])
    result.update({
        "stage": suite,
        "suite": suite,
        "qtest_totals": totals,
        "passed": bool(
            result["passed"]
            and totals
            and totals["failed"] == 0
            and totals["skipped"] == 0
            and totals["blacklisted"] == 0
        ),
    })
    return result


def unit_stage(binary: Path, repo: Path) -> dict[str, Any]:
    result = qtest_stage(binary, repo, "FeatureTests")
    vector_test = execute(
        [str(binary), "-o", "-,txt", "testVectorPanRepaintsOnlyExposedStrip"],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "QV_DISABLE_ONLINE_VERSION_CHECK": "1",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
            "FOVELLE_TEST_SUITE": "GraphicsViewTests",
            "QTEST_FUNCTION_TIMEOUT": "30000",
        },
        timeout=60,
    ) if binary.is_file() else {
        "passed": False,
        "return_code": None,
        "output_tail": "test binary does not exist",
    }
    vector_pass_marker = "PASS   : GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip()"
    vector_test["passed"] = bool(
        vector_test["passed"] and vector_pass_marker in vector_test["output_tail"]
    )
    result["vector_pan_test"] = {
        "passed": vector_test["passed"],
        "pass_marker": vector_pass_marker,
        "pass_marker_observed": vector_pass_marker in vector_test["output_tail"],
        "execution": vector_test,
    }
    result["passed"] = bool(result["passed"] and vector_test["passed"])
    result["stage"] = "unit"
    return result


def integration_stage(binary: Path, repo: Path) -> dict[str, Any]:
    result = qtest_stage(binary, repo, "WindowBehaviorTests")
    result["stage"] = "integration"
    return result


def system_stage(build_dir: Path, repo: Path) -> dict[str, Any]:
    candidates = (
        build_dir / "Fovelle.app/Contents/MacOS/Fovelle",
        build_dir / "Fovelle",
    )
    application = next((candidate for candidate in candidates if candidate.is_file()), None)
    if application is None:
        return {
            "stage": "system",
            "passed": False,
            "error": "Fovelle application binary was not found",
            "candidates": [str(candidate) for candidate in candidates],
        }
    result = execute(
        [str(application)],
        repo,
        {
            "QT_QPA_PLATFORM": "cocoa",
            "QT_FATAL_WARNINGS": "1",
            "QV_DISABLE_ONLINE_VERSION_CHECK": "1",
            "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
            "FOVELLE_SYSTEM_PROBE": "1",
        },
        timeout=60,
    )
    match = re.search(r"FOVELLE_SYSTEM_PROBE windows=(\d+) maximized=(true|false)", result["output_tail"])
    observation = {
        "application": str(application),
        "probe_found": bool(match),
        "windows": int(match.group(1)) if match else None,
        "maximized": match.group(2) == "true" if match else None,
    }
    result.update({
        "stage": "system",
        "system_probe": observation,
        "passed": bool(result["passed"] and match and observation["windows"] >= 1),
    })
    return result


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
            stage in STAGE_ORDER
            for case in CASES
            for stage in case["evidence_stages"]
        ),
    }


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def build_reports(repo: Path, build_dir: Path, binary: Path) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    generated_at = now_utc()
    static = static_stage(repo)
    unit = unit_stage(binary, repo)
    integration = integration_stage(binary, repo)
    system = system_stage(build_dir, repo)
    stages = {item["stage"]: item for item in (static, unit, integration, system)}
    validation = validate_cases()

    case_results = []
    for case in CASES:
        stage_status = {
            stage: stages[stage]["passed"]
            for stage in case["evidence_stages"]
            if stage in stages
        }
        case_results.append({
            "id": case["id"],
            "acceptance_criterion": case["acceptance_criterion"],
            "evidence_stages": case["evidence_stages"],
            "stage_status": stage_status,
            "passed": bool(stage_status) and all(stage_status.values()),
        })

    source_files = (
        "src/qvoptionsdialog.ui",
        "src/qvoptionsdialog.cpp",
        "src/settingsmanager.cpp",
        "tests/tst_qviewtests.cpp",
        "tests/settings_quality_pipeline.py",
        "tests/ci_quality_pipeline.py",
        "reports/solution_and_proof.md",
        ".gitignore",
        ".github/workflows/test.yml",
        *[f"i18n/{name}" for name in CATALOGS],
    )
    input_hashes = {
        path: file_sha256(repo / path) for path in source_files
    }
    task = "设置页文案重命名与鼠标选项移除；修复 GitHub Actions 检查失败"
    specification = {
        "schema_version": "2.0",
        "report_type": "atomic_test_case_specification",
        "generated_at_utc": generated_at,
        "task": task,
        "stage_order": list(STAGE_ORDER),
        "required_test_fields": list(REQUIRED_CASE_FIELDS),
        "cases": list(CASES),
        "research_trace": RESEARCH_TRACE,
        "input_sha256": input_hashes,
        "validation": validation,
        "passed": bool(
            validation["unique_ids"]
            and validation["all_required_fields_present"]
            and validation["all_evidence_stages_known"]
        ),
    }

    all_stages_passed = all(stages[stage]["passed"] for stage in STAGE_ORDER)
    all_cases_passed = all(case["passed"] for case in case_results)
    completion = {
        "schema_version": "2.0",
        "report_type": "test_completion_report",
        "generated_at_utc": generated_at,
        "task": task,
        "host": {
            "platform": platform.platform(),
            "python": platform.python_version(),
            "repository": str(repo),
        },
        "build": {
            "build_dir": str(build_dir),
            "binary": str(binary),
            "binary_sha256": file_sha256(binary),
        },
        "stage_order": list(STAGE_ORDER),
        "stages": stages,
        "cases": case_results,
        "research_trace": RESEARCH_TRACE,
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
            "evidence": "界面只保留可达设置；兼容键保留在运行时，但旧 UI、同步连接和不可达中键拖动行已移除；CI 观察器合同与证明文件纳入静态审计。",
        },
        {
            "id": "CQ-CORRECT-001",
            "criterion": "功能正确性",
            "passed": all_cases_passed,
            "evidence": "QtTest 覆盖精确文案、枚举数据、对象树、默认值、旧配置迁移、多语言布局和 CI-UNIT-002 的因果 Paint 观察；四级流水线汇总外部结果。",
        },
        {
            "id": "CQ-TESTABLE-001",
            "criterion": "可测试性",
            "passed": specification["passed"] and completion["passed"],
            "evidence": "每个原子用例包含测试目的、前置条件、输入、步骤、预期和后置条件；报告保存命令、返回码、耗时、输出摘要和 SHA-256。",
        },
    ]
    quality = {
        "schema_version": "2.0",
        "report_type": "code_quality_assessment_report",
        "generated_at_utc": generated_at,
        "task": task,
        "quality_requirements": quality_requirements,
        "explicit_assumptions": [
            "界面移除不等于删除持久化兼容键，因为 qvgraphicsview.cpp 仍读取两个键。",
            "固定策略的有效范围是新安装与 migrateOldSettings() 完成后的启动；没有 UI 路径再写入旧策略。",
            "GitHub Actions 的 Cocoa 系统测试在 macos-26 runner 上执行，非 macOS 本地环境只能完成静态阶段。",
            "异步 refinement 的完成时刻不属于滚动 Paint 的验收输入；首个可见 Paint 才是面积断言输入，pending 状态只用于 teardown。",
        ],
        "research_trace": RESEARCH_TRACE,
        "audit": {
            "all_quality_requirements_passed": all(item["passed"] for item in quality_requirements),
            "specification_valid": specification["passed"],
            "test_completion_valid": completion["passed"],
        },
    }
    quality["passed"] = all(item["passed"] for item in quality_requirements)
    quality["status"] = "passed" if quality["passed"] else "failed"

    write_json(repo / "reports/test_case_specification.json", specification)
    write_json(repo / "reports/test_completion_report.json", completion)
    write_json(repo / "reports/code_quality_assessment_report.json", quality)
    return specification, completion, quality


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--binary", type=Path, default=None)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    binary = (args.binary or build_dir / "tests/fovelle_tests").resolve()
    specification, completion, quality = build_reports(repo, build_dir, binary)
    summary = {
        "specification_passed": specification["passed"],
        "completion_passed": completion["passed"],
        "quality_passed": quality["passed"],
        "case_count": len(specification["cases"]),
        "stage_order": completion["stage_order"],
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if all(summary.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
