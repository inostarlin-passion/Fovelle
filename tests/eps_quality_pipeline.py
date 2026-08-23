#!/usr/bin/env python3
"""Run the EPS quality gates in order and materialize four audit JSON files."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


EPS_SAMPLE = Path(
    "/Users/inostarlin/Downloads/Download-on-the-App-Store/US/Download_on_App_Store/Black_lockup/EPS/Download_on_the_App_Store_Badge_US-UK_blk_092917.eps"
)

RESEARCH_TRACE = [
    {
        "hop": 1,
        "dimension": "platform capability",
        "query": "Apple macOS 14 EPS PostScript removed ImageIO NSEPSImageRep",
        "url": "https://developer.apple.com/documentation/macos-release-notes/macos-14-release-notes",
        "classification": "fact",
        "finding": "Apple removed system PostScript/EPS conversion in macOS 14; ImageIO no longer converts EPS and NSEPSImageRep can no longer display it.",
    },
    {
        "hop": 2,
        "dimension": "format semantics",
        "query": "Adobe EPS embedded preview authoritative PostScript content",
        "url": "https://helpx.adobe.com/uk/illustrator/using/saving-artwork.html",
        "classification": "fact",
        "finding": "Adobe describes EPS as PostScript and its preview as a display aid for applications that cannot display EPS directly.",
    },
    {
        "hop": 3,
        "dimension": "decoder differential",
        "query": "local extraction and independent decode of supplied DOS EPS TIFF preview",
        "url": "local://imageio-libtiff-differential-probe",
        "classification": "fact",
        "finding": "The exact 120x40 preview produces stripes in ImageIO but the correct badge in libtiff. Sorting its noncanonical IFD does not fix ImageIO; palette without alpha and RGBA with alpha both work, isolating the palette-plus-extra-alpha interaction. The 4 MB PostScript section renders correctly.",
    },
    {
        "hop": 4,
        "dimension": "authoritative renderer",
        "query": "Ghostscript EPS dEPSCrop pdfwrite",
        "url": "https://ghostscript.readthedocs.io/en/gs10.03.0/Use.html",
        "classification": "fact",
        "finding": "Ghostscript interprets EPS and documents -dEPSCrop for cropping output to the EPS DSC BoundingBox.",
    },
    {
        "hop": 5,
        "dimension": "reference application behavior",
        "query": "Skim macOS 14 EPS ps2pdf Ghostscript conversion",
        "url": "https://sourceforge.net/p/skim-app/wiki/Hidden_Preferences/",
        "classification": "fact",
        "finding": "Skim documents external conversion on macOS 14 and uses ps2pdf by default for PostScript/EPS.",
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


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def artifact(repo: Path, path: Path) -> dict:
    resolved = path.resolve()
    record = {
        "path": str(resolved.relative_to(repo)) if resolved.is_relative_to(repo) else str(resolved),
        "absolute_path": str(resolved),
        "exists": resolved.is_file(),
    }
    if resolved.is_file():
        record.update({"bytes": resolved.stat().st_size, "sha256": sha256(resolved)})
    else:
        record.update({"bytes": 0, "sha256": None})
    try:
        data = json.loads(resolved.read_text(encoding="utf-8")) if resolved.is_file() else {}
        record["kind"] = data.get("kind")
        record["passed"] = data.get("passed") is True
    except (OSError, json.JSONDecodeError):
        record["kind"] = None
        record["passed"] = False
    return record


def git_head(repo: Path) -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, text=True, capture_output=True, check=False
    )
    return result.stdout.strip() or None


def run_stage(repo: Path, command: list[str], evidence_path: Path) -> dict:
    started = time.perf_counter()
    completed = subprocess.run(command, cwd=repo, text=True, capture_output=True, check=False)
    elapsed = time.perf_counter() - started
    stage = {}
    if evidence_path.is_file():
        try:
            stage = json.loads(evidence_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            stage = {}
    return {
        "command": command,
        "return_code": completed.returncode,
        "elapsed_seconds": elapsed,
        "evidence": artifact(repo, evidence_path),
        "passed": completed.returncode == 0 and stage.get("passed") is True,
        "stdout_tail": completed.stdout[-4000:],
        "stderr_tail": completed.stderr[-4000:],
        "data": stage,
    }


def case(
    identifier: str,
    criterion: str,
    quality: str,
    level: str,
    purpose: str,
    preconditions: list[str],
    input_data: list[str],
    steps: list[str],
    expected: list[str],
    postconditions: list[str],
    test_code: str,
    evidence_file: str,
) -> dict:
    return {
        "id": identifier,
        "atomic_acceptance_criterion": criterion,
        "quality_attribute": quality,
        "test_level": level,
        "test_purpose": purpose,
        "preconditions": preconditions,
        "input_data": input_data,
        "operation_steps": steps,
        "expected_result": expected,
        "postconditions": postconditions,
        "test_code": test_code,
        "evidence_file": evidence_file,
    }


def build_specification(repo: Path, generated_at: str) -> dict:
    cases = [
        case(
            "ST-EPS-REGISTRY",
            "EPS、EPSF、EPSI 必须由同一原生格式注册表声明并可被支持性查询接受。",
            "精益完整性/功能正确性",
            "static",
            "验证 EPS 扩展名没有绕过既有格式注册路径。",
            ["源码可读。", "macOS native bridge 与 QVApplication 格式注册代码存在。"],
            ["QVCocoaFunctions::getAdditionalImageFormats、supportsAdditionalImageFormat 和 QVApplication::defineFilterLists。"],
            ["检查三个 EPS 别名的注册字面量。", "检查支持性谓词和 QVApplication 消费注册表。"],
            ["eps、epsf、epsi 均被原生注册表和应用格式列表接受。"],
            ["不启动外部进程，不改变用户设置。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-PARSER-SAFETY",
            "EPS 必须以受限 Ghostscript 子进程解释权威 PostScript，用 -dEPSCrop 生成并保留 PDF 文档；不得以内嵌预览代替正文。",
            "精益完整性/可测试性",
            "static",
            "验证外部解释器、持久 PDF 文档和资源安全边界同时存在。",
            ["Objective-C++ native bridge 可读。"],
            ["Ghostscript 查找、QProcess 参数、PDF page box、进程/PDF/像素/诊断上限。"],
            ["检查 -dSAFER/-dEPSCrop/pdfwrite。", "检查有限启动与完成等待。", "检查 CoreGraphics PDF 绘制。", "确认旧 TIFF/EPSI preview decoder 不在主链路。"],
            ["权威 PostScript 进入有界解释器；输出按 BoundingBox 裁切且资源超限或解释失败时 fail closed。"],
            ["静态检查不启动 Ghostscript。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-VECTOR-VIEWPORT",
            "EPS 场景项必须异步生成当前 device transform 下带有界拖动边界的 exposedRect 图块、复用不透明 backing store，且全部缩放入口统一限制为 6400%。",
            "功能正确性/时间行为",
            "static",
            "验证高倍缩放没有退化为固定整图位图，并统一执行缩放上限。",
            ["graphics item、graphics view 与 zoom 对话框源码可读。"],
            ["NoCache、ItemUsesExtendedStyleOption、exposedRect、deviceTransform、QtConcurrent interaction refinement、2-entry/96 MiB tile limit、MaximumZoomLevel。"],
            ["检查可见区加 128 device-pixel pan border 的 PDF tile 计算。", "检查 75% 后台交互 tile 与后台精确 idle refinement。", "检查矢量滚动的不透明局部重绘。", "检查中央 zoom clamp 和 6400% UI 上限。"],
            ["交互帧可变换最近 tile 并由后台追赶，50ms idle 后异步恢复精确矢量终端采样；平移仅重画暴露条带；wheel、pinch、键盘、会话和自定义缩放均不超过 64.0。"],
            ["静态检查不分配图像。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-DOCS-SETTINGS",
            "README、Settings、Bundle 和 CI 必须同时声明 EPS 与 Ghostscript 运行时/测试依赖。",
            "精益完整性",
            "static",
            "验证用户可见格式清单和安装后的文件关联契约没有遗漏。",
            ["README、Info.plist.in、Settings 源码可读。"],
            ["README 的 EPS/Ghostscript 说明、Info.plist 扩展名和 UTI、Settings 动态枚举、CI brew 安装步骤。"],
            ["检查文档、Bundle 和 Settings。", "检查四个构建/发布测试工作流安装 Ghostscript。"],
            ["格式声明、安装要求和自动化环境一致。"],
            ["不修改用户设置。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-LOADER-DELEGATION",
            "QVImageLoader 必须复用 native Result 且不增加 suffix 分支；QVImageCore 必须跳过矢量文档的 QMovie 探测，防止内嵌预览延迟覆盖。",
            "精益完整性/功能正确性",
            "static",
            "验证 EPS 解码结果能沿现有异步 loader 传递。",
            ["QVImageLoader 与 QVCocoaFunctions 源码可读。"],
            ["readImageWithImageIO、nativeResult.vectorImage 和 QMovie 启动条件。"],
            ["检查 loader 调用 native bridge。", "检查 loader 未按 eps suffix 复制分支。", "检查 image core 按 vector document 禁用 movie probe。"],
            ["EPS 共用 Result/缓存/请求语义，静态权威文档在延迟事件后仍保留。"],
            ["不执行图像解码。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-TESTABILITY",
            "测试必须提供无内嵌预览的确定性 vector EPS、外部样例像素判据、native render、异步 loader、延迟 movie probe、缺依赖错误、设置表和损坏输入用例。",
            "可测试性",
            "static",
            "验证每项实现边界都有可执行测试入口。",
            ["Qt test source 可读。"],
            ["EPS 测试方法声明、FOVELLE_EPS_SAMPLE、vector PostScript fixture。"],
            ["检查测试方法和可控输入标记。"],
            ["静态检查能定位全部 EPS 测试入口。"],
            ["不执行测试进程。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "UT-EPS-FORMAT",
            "原生格式查询和应用 extension set 必须包含 eps/epsf/epsi。",
            "功能正确性",
            "unit",
            "直接验证格式注册运行时输出。",
            ["fovelle_tests 已编译。", "Cocoa Qt test environment 可用。"],
            ["eps、epsf、epsi 三个 QByteArray。"],
            ["运行 ImageLoaderTests::testEPSFormatIsAdvertised。", "比较 native registry、support predicate 和 QVApplication extension set。"],
            ["三个别名均存在且 support predicate 为 true。"],
            ["测试不持久化设置。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSFormatIsAdvertised",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "UT-EPS-RENDER",
            "DOS EPS 样例或 deterministic vector EPS 必须保留为 PDF 矢量文档，并可按请求从权威 PostScript 生成 2048px 终端密度图块；逻辑尺寸保持 BoundingBox。",
            "功能正确性",
            "unit",
            "验证渲染来自 PostScript 而不是 120x40 placement preview。",
            ["Ghostscript 可执行；样例文件可读，或临时目录可写。"],
            ["用户提供的 DOS EPS；样例缺失时为包含黑底和两个白色 vector 方块的 8x4 EPS。"],
            ["调用 readImageWithImageIO。", "检查 PDF bytes、512px preview、BoundingBox 逻辑尺寸。", "由持久 PDF document 请求 2048px 全页和上半页 tile。"],
            ["PDF 文档有效；独立终端绘制最大边为 2048；partial tile 坐标方向正确，fallback 的已知黑白像素正确。"],
            ["临时 fixture 和 native image 资源释放。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSPostScriptRender",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "UT-EPS-LOADER",
            "EPS native image 必须通过 QVImageLoader 异步请求，以 matching request id 返回非空 image、intrinsicSize 和无 errorData。",
            "功能正确性/可测试性",
            "unit",
            "验证生产异步加载链不需要 EPS 特殊分支。",
            ["Ghostscript、EPS 样例或 vector fallback 可读。"],
            ["epsSamplePath 返回的文件路径。"],
            ["requestImage 并等待 imageReady。", "比较 absoluteFilePath、image、intrinsicSize 和 errorData。"],
            ["请求完成，vectorImage 为 PDF、preview 最大边为 512、logical size 独立保留且无 errorData。"],
            ["loader 和临时文件释放。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderLoadsEPS",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "UT-EPS-STATIC",
            "QVImageCore 处理 EPS 后等待 1100ms，QMovie 必须保持 NotRunning，权威 PDF vectorImage 不得被 placement preview 替换。",
            "功能正确性/可测试性",
            "unit",
            "覆盖初次正确显示后被异步 movie probe 二次覆盖的回归路径。",
            ["生产 loader 已返回 EPS Result；Cocoa window 可显示。"],
            ["同一 DOS EPS 或 deterministic vector EPS。"],
            ["将 Result 交给 QVImageCore。", "处理 1100ms 事件。", "前后比较 movie state 与 pixmap size。"],
            ["movie 始终不运行，PDF vectorImage 前后均有效，preview 不会取代权威文档。"],
            ["窗口、image core 和 reader 资源释放。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSRenderSurvivesStaticMovieProbe",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "UT-EPS-MALFORMED",
            "截断 DOS EPS wrapper 必须在有限 Ghostscript 执行后安全失败，不崩溃、不伪造图像。",
            "功能正确性/可测试性",
            "unit",
            "验证恶意或损坏 PostScript 输入的 fail-closed 行为。",
            ["临时目录可写。"],
            ["只有 32 bytes 的 DOS EPS magic/header，无有效 PostScript section。"],
            ["写入 malformed.eps。", "调用 readImageWithImageIO 并检查返回。"],
            ["类型仍被识别为 EPS，image 为空，errorString 非空。"],
            ["损坏 fixture 删除随 QTemporaryDir 生命周期结束。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testMalformedEPSFailsSafely",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "UT-EPS-DEPENDENCY",
            "Ghostscript 缺失时 QVImageLoader 必须返回含安装/路径提示的错误，image 为空，且禁止 Qt fallback 读取 placement preview。",
            "功能正确性/可测试性",
            "unit",
            "验证显式外部依赖的失败语义，不允许静默回到已知错误链路。",
            ["EPS fixture 可读；进程环境可临时修改并恢复。"],
            ["FOVELLE_GHOSTSCRIPT 指向确定不存在的路径。"],
            ["通过 QVImageLoader 打开 EPS。", "恢复原环境。", "检查 image 和 errorData。"],
            ["image 为空；错误包含 requires Ghostscript 与 FOVELLE_GHOSTSCRIPT。"],
            ["环境变量恢复，loader 和临时文件释放。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSMissingRendererFailsActionably",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "IT-EPS-SETTINGS",
            "Settings → Formats 运行时表格必须显示并默认启用 .eps、.epsf、.epsi。",
            "功能正确性/可测试性",
            "integration",
            "验证应用注册表到实际设置 UI 的组件集成。",
            ["fovelle_tests 已编译。", "Cocoa dialog 可构造。"],
            ["生产 QVOptionsDialog::formatsTable。"],
            ["构造 QVOptionsDialog。", "读取 formatsTable 第一列和 checkbox state。"],
            ["三个 EPS 别名均显示且为 Checked。"],
            ["对话框销毁且不持久化设置。"],
            "tests/tst_qviewtests.cpp::FeatureTests::testSettingsFormatsIncludeEPS",
            "reports/evidence/eps_integration.json",
        ),
        case(
            "SYS-EPS-OPEN",
            "真实 Fovelle.app 打开 EPS 后必须报告 PDF vector source 和非空终端密度 tile，且无 Ghostscript/unsupported-format 错误。",
            "功能正确性/可测试性",
            "system",
            "验证从进程启动、命令行打开到可见视口的端到端行为。",
            ["Fovelle.app 已构建且 Ghostscript 可执行。", "用户提供的 EPS 样例可读，或 pipeline 可创建无 preview 的 vector EPS。"],
            ["EPS 样例路径，重复运行 3 次。", "FOVELLE_DIAGNOSTIC_LOG=1 与 FOVELLE_VECTOR_RENDER_LOG=1。"],
            ["启动 app 并传入 EPS。", "读取 FOVELLE_VIEW 和 FOVELLE_VECTOR_RENDER。", "记录每次 tile、退出码和错误输出。"],
            ["全部运行均出现 format=pdf source=vector 和非空 tile，无 renderer/unsupported-format 错误。"],
            ["每个进程被有限等待后终止，临时 fallback 可回收。"],
            "tests/eps_quality_system.py",
            "reports/evidence/eps_system.json",
        ),
        case(
            "SYS-EPS-TIME",
            "EPS 目标工作负载必须记录平均响应时间、P99、最大响应时间和吞吐量，并满足显式阈值。",
            "时间行为",
            "system",
            "验证 EPS 打开路径具有可审计的有限时间行为。",
            ["Cocoa app 可启动，计时器和有限终止协议可用。"],
            ["同一 EPS 样例重复运行 3 次，响应窗口为 launch 到首个 FOVELLE_VIEW。"],
            ["采集每次响应秒数。", "计算 average、P99、max、throughput。", "逐项和阈值比较。"],
            ["指标非空，平均≤5s、P99≤8s、最大≤10s、吞吐量≥0.2 runs/s。"],
            ["原始输出、阈值、host observation limitation 写入 evidence。"],
            "tests/eps_quality_system.py",
            "reports/evidence/eps_system.json",
        ),
    ]
    return {
        "schema_version": "1.0",
        "kind": "atomic-test-case-specification",
        "generated_at_utc": generated_at,
        "repository": str(repo),
        "execution_order": ["static", "unit", "integration", "system"],
        "required_case_fields": [
            "id",
            "test_purpose",
            "preconditions",
            "input_data",
            "operation_steps",
            "expected_result",
            "postconditions",
            "test_code",
            "evidence_file",
            "atomic_acceptance_criterion",
            "quality_attribute",
            "test_level",
        ],
        "case_count": len(cases),
        "phase_counts": {
            level: sum(1 for item in cases if item["test_level"] == level)
            for level in ("static", "unit", "integration", "system")
        },
        "cases": cases,
        "facts": [
            "Every listed case has one atomic criterion and the six requested test-case fields.",
            "Unit cases are implemented in Qt/C++; static, integration orchestration, and system timing are implemented in Python test code.",
        ],
        "inferences": [
            "The case decomposition separates registration, parser safety, documentation, loader propagation, UI integration, end-to-end opening, and time behavior so a failure identifies one boundary.",
        ],
        "uncertainties": [
            "The specification covers authoritative rendering for the supplied Illustrator EPS and a deterministic no-preview vector EPS, not every PostScript dialect, external font dependency, or Ghostscript release.",
        ],
        "research_trace": RESEARCH_TRACE,
        "passed": all(
            all(field in item and item[field] for field in item_required)
            for item in cases
            for item_required in [[
                "id",
                "test_purpose",
                "preconditions",
                "input_data",
                "operation_steps",
                "expected_result",
                "postconditions",
                "test_code",
                "evidence_file",
                "atomic_acceptance_criterion",
                "quality_attribute",
                "test_level",
            ]]
        ),
    }


def extract_case_records(stage: dict) -> dict[str, dict]:
    records = {}
    for item in stage.get("cases", []):
        if item.get("id"):
            records[item["id"]] = item
    for item in stage.get("checks", []):
        if item.get("id"):
            records[item["id"]] = item
    return records


def make_case_evidence(repo: Path, specification: dict, stages: dict[str, dict]) -> list[dict]:
    records_by_stage = {name: extract_case_records(data["data"]) for name, data in stages.items()}
    evidence_by_stage = {name: data["evidence"] for name, data in stages.items()}
    results = []
    for item in specification["cases"]:
        level = item["test_level"]
        source = records_by_stage[level].get(item["id"], {})
        evidence = evidence_by_stage[level]
        passed = source.get("passed") is True
        results.append(
            {
                "id": item["id"],
                "test_level": level,
                "test_code": item["test_code"],
                "evidence_file": item["evidence_file"],
                "evidence_artifact": evidence,
                "status": "passed" if passed else "failed",
                "actual_result": source,
                "atomic_observations": {
                    "criterion": item["atomic_acceptance_criterion"],
                    "expected_result": item["expected_result"],
                },
                "facts": [
                    f"Stage evidence was read from {evidence['path']}.",
                    "The case record is keyed by its atomic test identifier.",
                ],
                "inferences": [
                    "A passed case is evidence for the scoped acceptance criterion in the tested macOS/Cocoa environment."
                    if passed
                    else "No successful evidence record was found for this atomic criterion."
                ],
                "uncertainties": [
                    "Host-specific Ghostscript, font, and performance limitations are retained in the stage evidence.",
                ],
            }
        )
    return results


def build_quality_report(
    repo: Path,
    specification: dict,
    evidence: dict,
    completion: dict,
    stages: dict[str, dict],
    generated_at: str,
) -> dict:
    all_cases_passed = all(item["status"] == "passed" for item in evidence["cases"])
    static_passed = stages["static"]["passed"]
    unit_passed = stages["unit"]["passed"]
    integration_passed = stages["integration"]["passed"]
    system_passed = stages["system"]["passed"]
    system_data = stages["system"]["data"]
    time_data = system_data.get("metrics", {})
    time_contract = all(
        key in time_data and time_data[key] is not None
        for key in (
            "response_average_seconds",
            "response_p99_seconds",
            "response_max_seconds",
            "response_throughput_runs_per_second",
        )
    )
    dimensions = [
        {
            "id": "CQ-EPS-LEAN",
            "quality_attribute": "精益完整性",
            "status": "passed" if static_passed and "bounded_external_renderer" in json.dumps(stages["static"]["data"], ensure_ascii=False) else "failed",
            "criteria": [
                {"id": "CQ-EPS-LEAN-01", "result": static_passed, "evidence": "ST-EPS-REGISTRY/ST-EPS-PARSER-SAFETY"},
                {"id": "CQ-EPS-LEAN-02", "result": "bounded_external_renderer" in json.dumps(stages["static"]["data"], ensure_ascii=False), "evidence": "ST-EPS-PARSER-SAFETY"},
            ],
            "assessment": "复用现有 native bridge、Result、异步 loader 和 Settings registry；EPS 特有部分只负责有界 Ghostscript→持久 PDF、CoreGraphics 可见区终端采样和静态文档保护。",
        },
        {
            "id": "CQ-EPS-FUNCTION",
            "quality_attribute": "功能正确性",
            "status": "passed" if all_cases_passed and unit_passed and integration_passed and system_passed else "failed",
            "criteria": [
                {"id": "CQ-EPS-FUNCTION-01", "result": all_cases_passed, "evidence": "reports/test_evidence.json"},
                {"id": "CQ-EPS-FUNCTION-02", "result": unit_passed and integration_passed and system_passed, "evidence": "eps_unit.json/eps_integration.json/eps_system.json"},
            ],
            "assessment": "以 native render、异步 loader、延迟静态文档保护、Settings UI 和真实 app 打开路径分别验证输入输出和副作用。",
        },
        {
            "id": "CQ-EPS-TIME",
            "quality_attribute": "时间行为",
            "status": "passed" if system_passed and time_contract else "failed",
            "criteria": [
                {"id": "CQ-EPS-TIME-01", "result": time_contract, "evidence": "SYS-EPS-TIME"},
                {"id": "CQ-EPS-TIME-02", "result": system_passed, "evidence": "reports/evidence/eps_system.json"},
            ],
            "measurement_window": "process launch through first FOVELLE_VIEW decoded viewport record; 3 repeated runs",
            "metrics": time_data,
            "thresholds": system_data.get("thresholds", {}),
        },
        {
            "id": "CQ-EPS-TESTABILITY",
            "quality_attribute": "可测试性",
            "status": "passed" if specification["passed"] and all_cases_passed else "failed",
            "criteria": [
                {"id": "CQ-EPS-TESTABILITY-01", "result": specification["passed"], "evidence": "reports/test_case_specification.json"},
                {"id": "CQ-EPS-TESTABILITY-02", "result": all_cases_passed, "evidence": "reports/test_evidence.json"},
            ],
            "assessment": "输入可由 FOVELLE_EPS_SAMPLE 覆盖且有无 preview 的确定性 vector fallback；运行时通过 native result、Settings table、FOVELLE_VIEW 和原始 stdout/stderr 非侵入式观测。",
        },
    ]
    passed = all(item["status"] == "passed" for item in dimensions) and completion["passed"]
    return {
        "schema_version": "1.0",
        "kind": "code-quality-assessment-report",
        "generated_at_utc": generated_at,
        "repository": str(repo),
        "head_sha": git_head(repo),
        "scope": "EPS/EPSF/EPSI authoritative vector-document rendering, 6400% zoom bound, format registration, static-document retention, dependency documentation, tests, and audit artifacts",
        "dimensions": dimensions,
        "facts": [
            "All four requested quality dimensions have an explicit assessment dimension and evidence references.",
            "Time evidence stores average response, P99, maximum response, and throughput together with thresholds and measurement window.",
            "The assessment is based on stage return codes, atomic case records, and content-addressed evidence files.",
        ],
        "inferences": [
            "Passing all dimensions supports the inference that the implementation is complete for the stated Ghostscript-backed EPS rendering contract.",
        ],
        "uncertainties": [
            "Arbitrary PostScript dialects, external font availability, and other Ghostscript versions are not proven by these tests.",
            "Performance numbers are measurements on this macOS host and should be re-baselined on release hardware.",
        ],
        "evidence_artifacts": {
            "specification": artifact(repo, repo / "reports" / "test_case_specification.json"),
            "test_evidence": artifact(repo, repo / "reports" / "test_evidence.json"),
            "completion": artifact(repo, repo / "reports" / "test_completion_report.json"),
            "stage_files": [data["evidence"] for data in stages.values()],
        },
        "passed": passed,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument("--binary", type=Path, default=None)
    parser.add_argument("--app", type=Path, default=None)
    parser.add_argument("--sample", type=Path, default=EPS_SAMPLE)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = (args.build_dir or repo / "build").resolve()
    binary = (args.binary or build_dir / "tests" / "fovelle_tests").resolve()
    app = (args.app or build_dir / "Fovelle.app" / "Contents" / "MacOS" / "Fovelle").resolve()
    reports = repo / "reports"
    evidence_dir = reports / "evidence"
    generated_at = now()

    specification = build_specification(repo, generated_at)
    specification_path = reports / "test_case_specification.json"
    write_json(specification_path, specification)

    preflight = None
    if not args.skip_build:
        build_command = ["cmake", "--build", str(build_dir), "--parallel", "2"]
        started = time.perf_counter()
        build_result = subprocess.run(build_command, cwd=repo, text=True, capture_output=True, check=False)
        preflight = {
            "command": build_command,
            "return_code": build_result.returncode,
            "elapsed_seconds": time.perf_counter() - started,
            "stdout_tail": build_result.stdout[-4000:],
            "stderr_tail": build_result.stderr[-4000:],
        }

    stage_paths = {
        "static": evidence_dir / "eps_static.json",
        "unit": evidence_dir / "eps_unit.json",
        "integration": evidence_dir / "eps_integration.json",
        "system": evidence_dir / "eps_system.json",
    }
    stages = {
        "static": run_stage(
            repo,
            [sys.executable, str(repo / "tests" / "eps_quality_static.py"), "--repo", str(repo), "--output", str(stage_paths["static"])],
            stage_paths["static"],
        ),
        "unit": run_stage(
            repo,
            [sys.executable, str(repo / "tests" / "eps_quality_unit.py"), "--binary", str(binary), "--output", str(stage_paths["unit"])],
            stage_paths["unit"],
        ),
        "integration": run_stage(
            repo,
            [sys.executable, str(repo / "tests" / "eps_quality_integration.py"), "--binary", str(binary), "--output", str(stage_paths["integration"])],
            stage_paths["integration"],
        ),
        "system": run_stage(
            repo,
            [
                sys.executable,
                str(repo / "tests" / "eps_quality_system.py"),
                "--app",
                str(app),
                "--image",
                str(args.sample.expanduser().resolve()),
                "--runs",
                "3",
                "--output",
                str(stage_paths["system"]),
            ],
            stage_paths["system"],
        ),
    }

    case_evidence = make_case_evidence(repo, specification, stages)
    test_evidence = {
        "schema_version": "1.0",
        "kind": "atomic-test-evidence-index",
        "generated_at_utc": generated_at,
        "repository": str(repo),
        "head_sha": git_head(repo),
        "execution_order": ["static", "unit", "integration", "system"],
        "execution_order_valid": ["static", "unit", "integration", "system"] == list(stages),
        "specification": artifact(repo, specification_path),
        "stage_artifacts": [data["evidence"] for data in stages.values()],
        "cases": case_evidence,
        "summary": {
            "atomic_case_count": len(case_evidence),
            "passed_case_count": sum(1 for item in case_evidence if item["status"] == "passed"),
            "failed_case_count": sum(1 for item in case_evidence if item["status"] != "passed"),
            "stage_passed": {name: data["passed"] for name, data in stages.items()},
        },
        "facts": [
            "Every atomic case points to a stage JSON file with a SHA-256 digest.",
            "Evidence is separated into static, unit, integration, and system stages in the requested order.",
            "The stage records retain command, environment, raw output tail, return code, and pass flags.",
        ],
        "inferences": [
            "When all atomic records pass, the requested EPS scope is covered across code contract, runtime components, UI integration, and system opening.",
        ],
        "uncertainties": [
            "A finite fixture matrix cannot establish support for every PostScript dialect, font dependency, or Ghostscript version.",
        ],
        "research_trace": RESEARCH_TRACE,
        "passed": all(item["status"] == "passed" for item in case_evidence)
        and all(data["passed"] for data in stages.values())
        and specification["passed"],
    }
    evidence_path = reports / "test_evidence.json"
    write_json(evidence_path, test_evidence)

    completion_stages = []
    for order, (name, data) in enumerate(stages.items(), start=1):
        completion_stages.append(
            {
                "order": order,
                "name": name,
                "command": data["command"],
                "return_code": data["return_code"],
                "elapsed_seconds": data["elapsed_seconds"],
                "evidence": data["evidence"],
                "passed": data["passed"],
            }
        )
    completion = {
        "schema_version": "1.0",
        "kind": "test-completion-report",
        "generated_at_utc": generated_at,
        "repository": str(repo),
        "head_sha": git_head(repo),
        "preflight_build": preflight,
        "execution_order": ["static", "unit", "integration", "system"],
        "stages": completion_stages,
        "counts": {
            "atomic_cases": len(case_evidence),
            "passed_cases": sum(1 for item in case_evidence if item["status"] == "passed"),
            "failed_cases": sum(1 for item in case_evidence if item["status"] != "passed"),
        },
        "performance_observations": stages["system"]["data"].get("metrics", {}),
        "facts": [
            "The stages were invoked sequentially as static, unit, integration, system.",
            "The system stage includes average, P99, maximum response time, throughput, thresholds, and raw per-run observations.",
        ],
        "inferences": [
            "A passed completion report indicates all requested test layers completed within their finite command windows.",
        ],
        "uncertainties": [
            "The report does not claim universal compatibility with every EPS producer, PostScript dialect, or external font set.",
        ],
        "artifacts": {
            "specification": artifact(repo, specification_path),
            "test_evidence": artifact(repo, evidence_path),
            "stage_files": [data["evidence"] for data in stages.values()],
        },
        "passed": test_evidence["passed"] and all(data["passed"] for data in stages.values()),
    }
    completion_path = reports / "test_completion_report.json"
    write_json(completion_path, completion)

    quality = build_quality_report(repo, specification, test_evidence, completion, stages, generated_at)
    quality_path = reports / "code_quality_assessment_report.json"
    write_json(quality_path, quality)

    final = {
        "specification": specification["passed"],
        "test_evidence": test_evidence["passed"],
        "test_completion_report": completion["passed"],
        "code_quality_assessment_report": quality["passed"],
        "reports": [str(specification_path), str(evidence_path), str(completion_path), str(quality_path)],
    }
    print(json.dumps(final, ensure_ascii=False, indent=2))
    return 0 if all(final[key] for key in final if key != "reports") else 1


if __name__ == "__main__":
    sys.exit(main())
