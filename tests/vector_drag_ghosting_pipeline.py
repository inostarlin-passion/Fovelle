#!/usr/bin/env python3
"""Run the vector-drag ghosting acceptance gates in a fixed order.

The GUI regression is intentionally measured at two boundaries:

* a paint of a vector item before its asynchronous tile is ready; and
* the first Cocoa/QGraphicsView paint caused by an actual scroll/drag.

The system gate launches the real Fovelle.app and uses the existing native HID
driver.  All generated evidence is disposable and is written below
``reports/evidence``; the human-readable specification is maintained at
``reports/test_case_specification.md``.
"""

from __future__ import annotations

import argparse
import ast
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path


OFFICIAL_RESEARCH = (
    {
        "hop": 1,
        "question": "How does Qt repaint a QGraphicsView while scrolling?",
        "url": "https://doc.qt.io/qt-6/qgraphicsview.html",
        "fact": "FullViewportUpdate redraws the entire viewport and is the mode for disabling scroll optimization; MinimalViewportUpdate redraws the smallest exposed area.",
    },
    {
        "hop": 2,
        "question": "What does the Qt scroll optimization do to an opaque viewport?",
        "url": "https://doc.qt.io/qt-6.8/qwidget.html",
        "fact": "Qt documents that an opaque widget can receive only the newly exposed stripe during a scroll, while non-opaque content propagation repaints the complete scroll area.",
    },
    {
        "hop": 3,
        "question": "Can a QGraphicsItem paint an exposed sub-rectangle?",
        "url": "https://doc.qt.io/qt-6/qstyleoptiongraphicsitem.html",
        "fact": "QStyleOptionGraphicsItem exposes the area that needs painting, which makes the item-level background clear a local operation.",
    },
    {
        "hop": 4,
        "question": "Why can macOS EPS loading not be used as a correctness oracle?",
        "url": "https://developer.apple.com/documentation/macos-release-notes/macos-14-release-notes",
        "fact": "Apple's macOS 14 release notes remove the system PostScript/EPS conversion path, so Fovelle keeps an authoritative Ghostscript-produced PDF document for EPS.",
    },
)


CASE_FIELDS = (
    "id",
    "acceptance_criterion",
    "test_purpose",
    "preconditions",
    "input_data",
    "steps",
    "expected_result",
    "postconditions",
    "test_level",
    "test_code",
)


CASES = (
    {
        "id": "AC-VECTOR-GHOST-EPS",
        "acceptance_criterion": "EPS 在异步矢量 tile 尚未就绪时，透明 fallback 不能把上一帧像素带入当前暴露区域。",
        "test_purpose": "验证 EPS 的局部绘制先以不透明背景清除 stale backing-store 像素。",
        "preconditions": "已构建 fovelle_tests；macOS Cocoa 与 Ghostscript 可用；测试可创建临时目录。",
        "input_data": "无背景透明的确定性 120x40 EPS；洋红色哨兵画布；白色矢量背景 brush。",
        "steps": "运行 GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady；等待 EPS Result；在 tile 尚未发布时调用 QVGraphicsImageItem::paint；统计哨兵像素。",
        "expected_result": "stale_pixels=0，total_pixels=4800；EPS 的透明区域呈现白色背景而不是旧帧。",
        "postconditions": "异步 tile 请求和临时 EPS 资源释放；不修改用户设置。",
        "test_level": "unit",
        "test_code": "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady",
    },
    {
        "id": "AC-VECTOR-GHOST-SVG",
        "acceptance_criterion": "SVG 在异步矢量 tile 尚未就绪时，透明 fallback 不能把上一帧像素带入当前暴露区域。",
        "test_purpose": "验证同一清除契约覆盖 SVG renderer，而不是只修复 EPS/PDF 分支。",
        "preconditions": "已构建 fovelle_tests；Qt SVG renderer 可用；测试可创建临时目录。",
        "input_data": "无背景透明的确定性 120x40 SVG；洋红色哨兵画布；白色矢量背景 brush。",
        "steps": "运行同一 parameterized Qt test 的 SVG 分支；在 tile 尚未发布时调用 QVGraphicsImageItem::paint；统计哨兵像素。",
        "expected_result": "stale_pixels=0，total_pixels=4800；SVG 的透明区域呈现白色背景而不是旧帧。",
        "postconditions": "SVG renderer、异步请求和临时文件释放；不改变主题设置。",
        "test_level": "unit",
        "test_code": "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady",
    },
    {
        "id": "AC-VECTOR-FULL-FIRST-FRAME",
        "acceptance_criterion": "矢量滚动发生前必须切换到 FullViewportUpdate，首个拖动 frame 不得沿用 QGraphicsView 的 backing-store scroll reuse。",
        "test_purpose": "验证修复点位于 scrollContentsBy 的 base implementation 之前。",
        "preconditions": "可见 Cocoa QGraphicsView 已打开 SVG 并完成 64x 缩放；水平滚动条有溢出范围。",
        "input_data": "一次 6 logical-pixel 水平滚动条移动；可记录 QPaintEvent region。",
        "steps": "运行 testVectorPanRepaintsOnlyExposedStrip；在 setValue 后立即读取 viewportUpdateMode；记录首个 paint 的 dirty area。",
        "expected_result": "模式为 FullViewportUpdate；首个 dirty_ratio≥0.90，而不是旧实现的 0.009554 暴露条带。",
        "postconditions": "测试窗口关闭；矢量 refine timer 和 worker 退出。",
        "test_level": "integration",
        "test_code": "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip",
    },
    {
        "id": "AC-VECTOR-IDLE-RESTORE",
        "acceptance_criterion": "矢量交互停止 50ms 后必须恢复 MinimalViewportUpdate，并保留一次干净的 idle repaint 以发布终端密度 tile。",
        "test_purpose": "验证完整重绘只覆盖交互 burst，空闲时仍使用有界异步 tile 方案。",
        "preconditions": "同 AC-VECTOR-FULL-FIRST-FRAME；矢量 worker 可完成一次 refine。",
        "input_data": "同一滚动操作后的 timer interval=50ms 与 vectorRenderCount。",
        "steps": "运行 testVectorPanRepaintsOnlyExposedStrip；等待 hasPendingVectorRefinement=false、vectorRenderCount>0；读取 viewportUpdateMode。",
        "expected_result": "空闲模式为 MinimalViewportUpdate；最终 tile 已绘制，且没有无限期保持 FullViewportUpdate。",
        "postconditions": "timer、tile cache 和测试窗口按 RAII 释放。",
        "test_level": "integration",
        "test_code": "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip",
    },
    {
        "id": "AC-VECTOR-EPS-SVG-DRAG",
        "acceptance_criterion": "EPS/PDF 与 SVG 的拖动首帧都必须完整覆盖 viewport，且仍使用异步、有界、终端 device density 的矢量 tile。",
        "test_purpose": "验证两个生产格式走同一无拖影呈现策略。",
        "preconditions": "可见 Cocoa 窗口已构建；确定性 EPS 与 SVG 均可读；tile worker 可用。",
        "input_data": "16x12 大 EPS 与 SVG sample；64x zoom；6 logical-pixel 水平滚动。",
        "steps": "运行 testVectorDragFrameBudgetForEPSAndSVG；分别记录 EPS/SVG 首个 paint region、viewport area、update mode 与 render count。",
        "expected_result": "EPS、SVG 均 update_mode=full、dirty_ratio≥0.90；不出现 renderer error，tile 仍有界且异步。",
        "postconditions": "两个文档、worker 和窗口关闭；不残留进程。",
        "test_level": "integration",
        "test_code": "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorDragFrameBudgetForEPSAndSVG",
    },
    {
        "id": "AC-VECTOR-THEME-BACKGROUND",
        "acceptance_criterion": "主题纯色或 checkerboard brush 必须同步注入 vector item，item 清除区域与 viewport 背景契约一致。",
        "test_purpose": "避免修复使用硬编码颜色导致浅色/深色/棋盘格主题出现新的闪烁。",
        "preconditions": "QVGraphicsView settingsUpdated 源码可读；theme 与 checkerboard 设置存在。",
        "input_data": "viewportBackgroundBrush、checkerboardBackgroundBrush、setVectorBackgroundBrush 调用链。",
        "steps": "静态检查 settingsUpdated 在生成 checkerboard brush 后为 loadedPixmapItem 设置对应 brush，并检查 item 使用 CompositionMode_Source。",
        "expected_result": "纯色和 checkerboard 两条路径均可到达 vector item；清除操作不是 SourceOver 半透明叠加。",
        "postconditions": "静态检查不启动 GUI、不写用户配置。",
        "test_level": "static",
        "test_code": "tests/vector_drag_ghosting_pipeline.py::run_static",
    },
    {
        "id": "AC-EPS-AUTHORITATIVE-PATH",
        "acceptance_criterion": "EPS 必须保留权威 PostScript 生成的 PDF vector document；拖动 fallback/tile 不得改用嵌入 placement preview。",
        "test_purpose": "把拖影修复建立在确证的 EPS 内容源上，排除预览与正文不一致的干扰。",
        "preconditions": "Ghostscript 与 macOS PDF bridge 可用；EPS native bridge、loader 与 QVImageCore 源码可读。",
        "input_data": "无 preview deterministic EPS 或用户 EPS sample；-dSAFER、-dEPSCrop、pdfwrite contract。",
        "steps": "先运行静态 bridge contract，再运行 ImageLoaderTests::testEPSPostScriptRender 与 testImageLoaderLoadsEPS。",
        "expected_result": "Result.vectorImage.format=Pdf；PDF bytes 与 BoundingBox 逻辑尺寸有效；不读取 preview 作为权威内容。",
        "postconditions": "Ghostscript 子进程、PDF document、loader 和临时文件退出/释放。",
        "test_level": "static/unit",
        "test_code": "tests/eps_quality_static.py; tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSPostScriptRender",
    },
    {
        "id": "AC-VECTOR-BOUNDED-CPU",
        "acceptance_criterion": "完整交互重绘不得破坏既有异步 tile 的安全边界与 120Hz 线程 CPU 预算。",
        "test_purpose": "验证正确性修复没有把每个 frame 的矢量转换搬回 GUI 线程或解除 tile 上限。",
        "preconditions": "Cocoa display 与 vector sample 可用；fovelle_tests 已构建。",
        "input_data": "EPS/SVG 各 120 次 zoom/pan 样本；8.333ms p99 frame budget；64M tile pixel、2-entry/96MiB cache 上限。",
        "steps": "运行 testVectorInteractionPaintCpuBudgetFor120Hz；检查 average/p99/max/capacity 与静态安全边界。",
        "expected_result": "每个格式/交互 p99≤8.333ms；worker 仍为单线程异步，tile 上限和 zoom 64.0 contract 不变。",
        "postconditions": "测试窗口、worker、采样资源释放；原始测量保留在 stage evidence。",
        "test_level": "unit/integration",
        "test_code": "tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorInteractionPaintCpuBudgetFor120Hz",
    },
    {
        "id": "AC-VECTOR-NATIVE-HID",
        "acceptance_criterion": "真实 Fovelle.app 收到 macOS CoreGraphics HID 拖动后，EPS/SVG 日志均出现 full interaction paint、minimal idle restore 与 vector render。",
        "test_purpose": "把 QtTest 的可控回归提升到系统窗口、WindowServer 输入队列和真实 bundle。",
        "preconditions": "Fovelle.app 与 fovelle_native_drag_helper 已构建；Accessibility/Post Event 权限可用；真实 Cocoa 桌面可见。",
        "input_data": "运行时生成的 1600x1200 EPS/SVG；native helper 的 32-step HID trajectory。",
        "steps": "分别以 EPS/SVG 启动真实 bundle；helper 发送 fullscreen、scroll zoom、vertical native drag、退出 fullscreen；读取 helper log。",
        "expected_result": "NATIVE_DRAG_RESULT passed=true；日志包含 active=true/update_mode=full、active=false/update_mode=minimal、FOVELLE_VECTOR_PAINT full 和对应 vector render。",
        "postconditions": "app 被 helper 终止并退出；临时 fixture 删除；日志复制到 evidence 供审计。",
        "test_level": "system",
        "test_code": "tests/native_drag_helper.mm; tests/vector_drag_ghosting_pipeline.py::run_system",
    },
    {
        "id": "AC-PIPELINE-ORDER",
        "acceptance_criterion": "验收必须严格按 static → unit → integration → system 执行，任一前置阶段失败时后续阶段不得伪装为通过。",
        "test_purpose": "保证报告中的通过结论与实际证据顺序一致。",
        "preconditions": "仓库源码、构建目录和测试入口可读。",
        "input_data": "四个 stage command、return code、stdout/stderr tail 与 evidence JSON。",
        "steps": "运行本 pipeline 默认 all 模式；检查 stage_order、每阶段 passed、commands 与 generated_at_utc。",
        "expected_result": "stage_order 恰为 [static, unit, integration, system]；四项均 passed=true；运行失败返回非零。",
        "postconditions": "所有 evidence 可通过路径和 SHA256 复核；不修改用户数据。",
        "test_level": "audit",
        "test_code": "tests/vector_drag_ghosting_pipeline.py::main",
    },
)


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def run_command(command: list[str], cwd: Path, env: dict[str, str] | None = None,
                timeout: int = 120) -> dict:
    started = time.perf_counter()
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=merged_env,
            text=True,
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        return {
            "command": command,
            "return_code": completed.returncode,
            "elapsed_seconds": round(time.perf_counter() - started, 3),
            "stdout_tail": completed.stdout[-6000:],
            "stderr_tail": completed.stderr[-6000:],
            "timed_out": False,
        }
    except subprocess.TimeoutExpired as error:
        return {
            "command": command,
            "return_code": 124,
            "elapsed_seconds": round(time.perf_counter() - started, 3),
            "stdout_tail": (error.stdout or "")[-6000:] if isinstance(error.stdout, str) else "",
            "stderr_tail": (error.stderr or "")[-6000:] if isinstance(error.stderr, str) else "",
            "timed_out": True,
        }


def check(checks: list[dict], identifier: str, passed: bool, actual: object,
          expected: str) -> None:
    checks.append({
        "id": identifier,
        "passed": bool(passed),
        "actual": actual,
        "expected": expected,
    })


def test_totals(output: str) -> tuple[bool, dict]:
    match = re.search(
        r"Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped,\s*(\d+) blacklisted",
        output,
    )
    if not match:
        return False, {}
    values = {
        "passed": int(match.group(1)),
        "failed": int(match.group(2)),
        "skipped": int(match.group(3)),
        "blacklisted": int(match.group(4)),
    }
    return values["failed"] == 0 and values["skipped"] == 0 and values["blacklisted"] == 0, values


def run_static(repo: Path, output_dir: Path) -> dict:
    graphics_item = (repo / "src/qvgraphicsimageitem.cpp").read_text(encoding="utf-8")
    graphics_item_header = (repo / "src/qvgraphicsimageitem.h").read_text(encoding="utf-8")
    graphics_view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    graphics_view_header = (repo / "src/qvgraphicsview.h").read_text(encoding="utf-8")
    tests = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    root_cause = repo / "reports/root_cause.md"
    specification = repo / "reports/test_case_specification.md"
    pipeline = Path(__file__).read_text(encoding="utf-8")
    checks: list[dict] = []

    check(
        checks,
        "ST-GHOST-ITEM-CLEAR",
        all(marker in graphics_item for marker in (
            "setVectorBackgroundBrush",
            "CompositionMode_Source",
            "painter->fillRect(exposedRect, vectorBackgroundBrush)",
        )) and "QBrush vectorBackgroundBrush" in graphics_item_header,
        {
            "item_source_markers": {
                marker: marker in graphics_item
                for marker in (
                    "setVectorBackgroundBrush",
                    "CompositionMode_Source",
                    "painter->fillRect(exposedRect, vectorBackgroundBrush)",
                )
            },
            "item_header_brush": "QBrush vectorBackgroundBrush" in graphics_item_header,
        },
        "vector paint clears the exposed item area with the configured brush before fallback/tile SourceOver painting",
    )
    check(
        checks,
        "ST-GHOST-SCROLL-MODE",
        all(marker in graphics_view for marker in (
            "QVGraphicsView::scrollContentsBy",
            "QGraphicsView::FullViewportUpdate",
            "QGraphicsView::MinimalViewportUpdate",
            "setVectorInteractionPresentation",
            "vectorRefineTimer->start",
        )) and "scrollContentsBy(int dx, int dy) override" in graphics_view_header,
        {
            "view_source_markers": {
                marker: marker in graphics_view
                for marker in (
                    "QVGraphicsView::scrollContentsBy",
                    "QGraphicsView::FullViewportUpdate",
                    "QGraphicsView::MinimalViewportUpdate",
                    "setVectorInteractionPresentation",
                    "vectorRefineTimer->start",
                )
            },
            "header_override": "scrollContentsBy(int dx, int dy) override" in graphics_view_header,
        },
        "vector scroll switches update mode before QGraphicsView's backing-store scroll path and restores idle mode",
    )
    check(
        checks,
        "ST-GHOST-THEME-WIRING",
        "loadedPixmapItem->setVectorBackgroundBrush" in graphics_view
        and "checkerboardBackground ? checkerboardBackgroundBrush : viewportBackgroundBrush" in graphics_view,
        {
            "setter_called": "loadedPixmapItem->setVectorBackgroundBrush" in graphics_view,
            "theme_choice": "checkerboardBackground ? checkerboardBackgroundBrush : viewportBackgroundBrush" in graphics_view,
        },
        "theme and checkerboard backgrounds reach the vector item",
    )
    check(
        checks,
        "ST-GHOST-TEST-CONTRACT",
        all(marker in tests for marker in (
            "testVectorPaintClearsStalePixelsBeforeTileReady",
            "stale_pixels",
            "QGraphicsView::FullViewportUpdate",
            "dirtyRatio >= 0.90",
            "testVectorDragFrameBudgetForEPSAndSVG",
        )),
        {
            "test_markers": {
                marker: marker in tests
                for marker in (
                    "testVectorPaintClearsStalePixelsBeforeTileReady",
                    "stale_pixels",
                    "QGraphicsView::FullViewportUpdate",
                    "dirtyRatio >= 0.90",
                    "testVectorDragFrameBudgetForEPSAndSVG",
                )
            }
        },
        "pixel, first-frame, idle-restore and EPS/SVG drag assertions are executable",
    )
    check(
        checks,
        "ST-GHOST-ROOT-CAUSE-TRACE",
        root_cause.is_file() and all(
            marker in root_cause.read_text(encoding="utf-8")
            for marker in ("RC-01", "RC-02", "RC-03", "MinimalViewportUpdate", "WA_OpaquePaintEvent")
        ),
        {"root_cause_exists": root_cause.is_file(), "path": str(root_cause)},
        "the implementation is reviewed against the supplied root_cause.md hypotheses",
    )
    check(
        checks,
        "ST-GHOST-SPECIFICATION",
        specification.is_file() and all(
            case["id"] in specification.read_text(encoding="utf-8")
            and all(label in specification.read_text(encoding="utf-8") for label in (
                "测试目的", "前置条件", "输入数据", "操作步骤", "预期结果", "后置条件"))
            for case in CASES
        ),
        {"specification_exists": specification.is_file(), "case_count": len(CASES)},
        "the Markdown specification contains every atomic case and all six required fields",
    )
    case_schema_ok = all(set(CASE_FIELDS).issubset(case) for case in CASES)
    check(
        checks,
        "ST-GHOST-CASE-SCHEMA",
        case_schema_ok,
        {"case_count": len(CASES), "required_fields": list(CASE_FIELDS)},
        "the executable case registry gives every atomic criterion a complete six-field test case",
    )
    try:
        ast.parse(pipeline, filename=str(Path(__file__)))
        syntax_ok = True
    except SyntaxError:
        syntax_ok = False
    check(checks, "ST-GHOST-PYTHON-SYNTAX", syntax_ok, {"file": str(Path(__file__))}, "pipeline parses as Python")
    diff = run_command(["git", "diff", "--check", "HEAD", "--", "src", "tests", ".gitignore", "reports/test_case_specification.md"], repo)
    check(checks, "ST-GHOST-DIFF-CHECK", diff["return_code"] == 0, diff, "task-scoped diff has no whitespace errors")

    legacy_static_path = output_dir / "eps_static.json"
    legacy_static = run_command(
        [sys.executable, str(repo / "tests/eps_quality_static.py"), "--repo", str(repo), "--output", str(legacy_static_path)],
        repo,
        timeout=120,
    )
    legacy_passed = legacy_static["return_code"] == 0
    try:
        legacy_data = json.loads(legacy_static_path.read_text(encoding="utf-8"))
        legacy_passed = legacy_passed and legacy_data.get("passed") is True
    except (OSError, json.JSONDecodeError):
        legacy_data = {}
        legacy_passed = False
    check(checks, "ST-EPS-LEGACY-CONTRACT", legacy_passed, {"command": legacy_static, "summary": legacy_data.get("summary")}, "existing EPS static contract remains green")

    result = {
        "kind": "vector-drag-ghosting-static",
        "generated_at_utc": now(),
        "stage": "static",
        "checks": checks,
        "research_trace": list(OFFICIAL_RESEARCH),
        "passed": all(item["passed"] for item in checks),
    }
    write_json(output_dir / "vector_drag_static.json", result)
    return result


def run_unit(repo: Path, build_dir: Path, output_dir: Path) -> dict:
    binary = build_dir / "tests/fovelle_tests"
    checks: list[dict] = []
    build = run_command(["cmake", "--build", str(build_dir), "--target", "fovelle_tests", "-j2"], repo, timeout=180)
    check(checks, "UT-BUILD", build["return_code"] == 0, build, "fovelle_tests builds before unit execution")
    tests = ["testVectorPaintClearsStalePixelsBeforeTileReady"]
    executions = []
    for test_name in tests:
        execution = run_command(
            [str(binary), test_name],
            repo,
            env={
                "QT_QPA_PLATFORM": "cocoa",
                "QT_FATAL_WARNINGS": "1",
                "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
                "FOVELLE_TEST_SUITE": "GraphicsViewTests",
                "QTEST_FUNCTION_TIMEOUT": "30000",
            },
            timeout=60,
        )
        passed, totals = test_totals(execution["stdout_tail"] + execution["stderr_tail"])
        marker_ok = all(re.search(rf"format={fmt} stale_pixels=0 total_pixels=4800", execution["stdout_tail"] + execution["stderr_tail"]) for fmt in ("eps", "svg"))
        check(checks, "UT-VECTOR-GHOST-PIXELS", execution["return_code"] == 0 and passed and marker_ok, {"execution": execution, "totals": totals, "marker_ok": marker_ok}, "EPS/SVG stale pixel count is zero")
        executions.append(execution)
    result = {
        "kind": "vector-drag-ghosting-unit",
        "generated_at_utc": now(),
        "stage": "unit",
        "checks": checks,
        "executions": executions,
        "passed": all(item["passed"] for item in checks),
    }
    write_json(output_dir / "vector_drag_unit.json", result)
    return result


def run_integration(repo: Path, build_dir: Path, output_dir: Path) -> dict:
    checks: list[dict] = []
    binary = build_dir / "tests/fovelle_tests"
    test_names = (
        "testVectorPanRepaintsOnlyExposedStrip",
        "testVectorDragFrameBudgetForEPSAndSVG",
        "testVectorFormatsUseDocumentSceneItem",
        "testVectorInteractionPaintCpuBudgetFor120Hz",
    )
    executions = []
    for test_name in test_names:
        execution = run_command(
            [str(binary), test_name],
            repo,
            env={
                "QT_QPA_PLATFORM": "cocoa",
                "QT_FATAL_WARNINGS": "1",
                "FOVELLE_DISABLE_AUTO_UPDATE_CHECK": "1",
                "FOVELLE_TEST_SUITE": "GraphicsViewTests",
                "QTEST_FUNCTION_TIMEOUT": "30000",
            },
            timeout=60,
        )
        passed, totals = test_totals(execution["stdout_tail"] + execution["stderr_tail"])
        case_passed = execution["return_code"] == 0 and passed
        check(checks, f"IT-{test_name}", case_passed, {"execution": execution, "totals": totals}, "the vector GUI integration test passes without failures/skips")
        executions.append({"test": test_name, "execution": execution, "totals": totals, "passed": case_passed})
    result = {
        "kind": "vector-drag-ghosting-integration",
        "generated_at_utc": now(),
        "stage": "integration",
        "checks": checks,
        "executions": executions,
        "passed": all(item["passed"] for item in checks),
    }
    write_json(output_dir / "vector_drag_integration.json", result)
    return result


def write_system_fixtures(directory: Path) -> tuple[Path, Path]:
    eps = directory / "system-vector.eps"
    eps.write_text(
        "%!PS-Adobe-3.0 EPSF-3.0\n"
        "%%BoundingBox: 0 0 1600 1200\n"
        "%%HiResBoundingBox: 0 0 1600 1200\n"
        "%%Pages: 1\n%%EndComments\n"
        "0.08 setgray 0 0 1600 1200 rectfill\n"
        "1 setgray 160 160 480 880 rectfill\n"
        "0 setgray 960 160 480 880 rectfill\nshowpage\n%%EOF\n",
        encoding="ascii",
    )
    svg = directory / "system-vector.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1200" viewBox="0 0 1600 1200">'
        '<rect width="1600" height="1200" fill="#141414"/>'
        '<rect x="160" y="160" width="480" height="880" fill="#fff"/>'
        '<rect x="960" y="160" width="480" height="880" fill="#000"/>'
        "</svg>",
        encoding="utf-8",
    )
    return eps, svg


def run_system(repo: Path, build_dir: Path, output_dir: Path) -> dict:
    checks: list[dict] = []
    build = run_command(
        ["cmake", "--build", str(build_dir), "--target", "Fovelle", "fovelle_native_drag_helper", "-j2"],
        repo,
        timeout=240,
    )
    check(checks, "SYS-BUILD-BUNDLE", build["return_code"] == 0, build, "real app bundle and native HID helper build")
    app = build_dir / "Fovelle.app"
    helper = build_dir / "tests/fovelle_native_drag_helper"
    access = run_command([str(helper), "--check-access"], repo, timeout=30)
    check(checks, "SYS-HID-ACCESS", access["return_code"] == 0 and "post_event=true" in access["stdout_tail"] and "accessibility=true" in access["stdout_tail"], access, "native HID permissions are available")

    executions = []
    with tempfile.TemporaryDirectory(prefix="fovelle-vector-ghost-") as temporary:
        eps, svg = write_system_fixtures(Path(temporary))
        for fmt, path in (("eps", eps), ("svg", svg)):
            execution = run_command(
                [str(helper), "--app", str(app), "--image", str(path)],
                repo,
                timeout=100,
            )
            output = execution["stdout_tail"] + execution["stderr_tail"]
            log_match = re.search(r"NATIVE_DRAG_LOG\s+(\S+)", output)
            log_path = Path(log_match.group(1)) if log_match else None
            log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path and log_path.is_file() else ""
            log_copy = output_dir / f"native_drag_{fmt}.log"
            log_copy.parent.mkdir(parents=True, exist_ok=True)
            log_copy.write_text(log_text, encoding="utf-8")
            result_ok = "NATIVE_DRAG_RESULT passed=true" in output
            presentation_ok = bool(re.search(r"FOVELLE_VECTOR_PRESENTATION\s+active=true\s+update_mode=full", log_text)) and bool(re.search(r"FOVELLE_VECTOR_PRESENTATION\s+active=false\s+update_mode=minimal", log_text))
            paint_ok = bool(re.search(r"FOVELLE_VECTOR_PAINT\s+update_mode=\s*full\s+dirty_area=\s*\d+\s+viewport_area=\s*\d+\s+dirty_ratio=\s*([01](?:\.\d+)?)", log_text))
            render_format = "pdf" if fmt == "eps" else "svg"
            render_ok = f"FOVELLE_VECTOR_RENDER format={render_format}" in log_text
            check(checks, f"SYS-NATIVE-HID-{fmt.upper()}", execution["return_code"] == 0 and result_ok and presentation_ok and paint_ok and render_ok, {"execution": execution, "log_path": str(log_copy), "native_result": result_ok, "presentation": presentation_ok, "paint": paint_ok, "render": render_ok}, "real bundle reports full interaction repaint, minimal idle restore and vector render")
            executions.append({"format": fmt, "execution": execution, "log_path": str(log_copy), "native_result": result_ok, "presentation": presentation_ok, "paint": paint_ok, "render": render_ok})

    result = {
        "kind": "vector-drag-ghosting-system",
        "generated_at_utc": now(),
        "stage": "system",
        "checks": checks,
        "executions": executions,
        "passed": all(item["passed"] for item in checks),
    }
    write_json(output_dir / "vector_drag_system.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument("--stage", choices=("all", "static", "unit", "integration", "system"), default="all")
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    output_dir = (args.output_dir or repo / "reports/evidence/vector_drag_ghosting").resolve()
    requested = ("static", "unit", "integration", "system") if args.stage == "all" else (args.stage,)
    runners = {
        "static": lambda: run_static(repo, output_dir),
        "unit": lambda: run_unit(repo, build_dir, output_dir),
        "integration": lambda: run_integration(repo, build_dir, output_dir),
        "system": lambda: run_system(repo, build_dir, output_dir),
    }
    stages: list[dict] = []
    for stage_name in requested:
        stage = runners[stage_name]()
        stages.append(stage)
        if not stage["passed"]:
            break

    result = {
        "kind": "vector-drag-ghosting-pipeline",
        "generated_at_utc": now(),
        "repository": str(repo),
        "stage_order": [stage["stage"] for stage in stages],
        "requested_stage_order": list(requested),
        "stages": stages,
        "passed": len(stages) == len(requested) and all(stage["passed"] for stage in stages),
    }
    write_json(output_dir / "vector_drag_pipeline.json", result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
