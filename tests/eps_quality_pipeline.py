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
        "dimension": "native decoder capability",
        "query": "Apple Image I/O supported image sources CGImageSourceCopyTypeIdentifiers",
        "url": "https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/ImageIOGuide/imageio_basics/ikpg_basics.html",
        "classification": "fact",
        "finding": "Apple documents runtime enumeration of Image I/O source UTIs instead of a fixed universal format list.",
    },
    {
        "hop": 2,
        "dimension": "system type identity",
        "query": "Apple com.adobe.encapsulated-postscript UTI",
        "url": "https://developer.apple.com/documentation/appkit/nspasteboard/pasteboardtype/postscript?language=o_2%2Co_2",
        "classification": "fact",
        "finding": "Apple names com.adobe.encapsulated-postscript as the modern EPS type identifier.",
    },
    {
        "hop": 3,
        "dimension": "container preview structure",
        "query": "EPS file format DOS EPS TIFF preview",
        "url": "https://www.loc.gov/preservation/digital/formats/fdd/fdd000246.shtml",
        "classification": "fact",
        "finding": "EPS specifications describe device-specific preview data, including TIFF for DOS EPS.",
    },
    {
        "hop": 4,
        "dimension": "alternative renderer tradeoff",
        "query": "Ghostscript EPS interpreter external process",
        "url": "https://ghostscript.readthedocs.io/en/gs10.03.0/Use.html",
        "classification": "fact",
        "finding": "Ghostscript can interpret EPS, but using it would add an external runtime/dependency and a separate security boundary.",
    },
    {
        "hop": 5,
        "dimension": "implementation decision",
        "query": "local macOS ImageIO/NSImage/Quick Look probe for supplied EPS",
        "url": "local://macos-15.7.7-probes",
        "classification": "inference",
        "finding": "On this host CGImageSource returned a source with nil type and zero frames, NSImage returned nil, and qlmanage produced no preview; the supplied DOS EPS contains a TIFF preview at its binary offset.",
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
            "EPS 预览解析必须覆盖 DOS EPS TIFF 与 EPSI，并限制文件区间、扫描大小和图像尺寸；不调用外部渲染器。",
            "精益完整性/可测试性",
            "static",
            "验证安全边界和两种确定性预览分支存在。",
            ["Objective-C++ native bridge 可读。"],
            ["DOS EPS binary header、TIFF offset/length、%%BeginPreview/%%EndPreview、最大字节/像素常量。"],
            ["检查 range 校验和上限。", "检查 TIFF 和 EPSI 分支。", "检查 Ghostscript/ImageMagick/QProcess 调用不存在。"],
            ["越界和过大预览 fail closed；两种内嵌预览均有实现路径。"],
            ["不读取系统外部依赖，不产生临时运行副作用。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-DOCS-SETTINGS",
            "README Supported Formats、Settings 格式表和 macOS Bundle 文档类型声明必须包含 EPS。",
            "精益完整性",
            "static",
            "验证用户可见格式清单和安装后的文件关联契约没有遗漏。",
            ["README、Info.plist.in、Settings 源码可读。"],
            ["README 的 EPS 行、Info.plist 的 eps/epsf/epsi 和 EPS UTI、格式表动态枚举代码。"],
            ["检查文档和 Bundle 扩展名。", "检查 Settings 使用 QVApplication all-file-extension set。"],
            ["所有用户可见和 Bundle 层面均声明 EPS。"],
            ["不修改用户设置。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-LOADER-DELEGATION",
            "QVImageLoader 必须复用 native decoder result，不增加与 EPS 绑定的重复 suffix 分支。",
            "精益完整性/功能正确性",
            "static",
            "验证 EPS 解码结果能沿现有异步 loader 传递。",
            ["QVImageLoader 与 QVCocoaFunctions 源码可读。"],
            ["readImageWithImageIO、nativeResult.image 和 fallback 条件。"],
            ["检查 loader 调用 native bridge。", "检查 loader 未按 eps suffix 复制分支。"],
            ["EPS 与已有 native formats 共用 Result/缓存/请求语义。"],
            ["不执行图像解码。"],
            "tests/eps_quality_static.py",
            "reports/evidence/eps_static.json",
        ),
        case(
            "ST-EPS-TESTABILITY",
            "测试必须提供确定性 EPSI fallback、外部样例覆盖、native 输出观察、异步 loader、设置表和损坏输入用例。",
            "可测试性",
            "static",
            "验证每项实现边界都有可执行测试入口。",
            ["Qt test source 可读。"],
            ["EPS 测试方法声明、FOVELLE_EPS_SAMPLE、EPSI fixture。"],
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
            "UT-EPS-DECODE",
            "DOS EPS 样例或 deterministic EPSI fixture 必须由 native bridge 解码为非空 QImage，类型标识为 com.adobe.encapsulated-postscript。",
            "功能正确性",
            "unit",
            "验证 EPS preview parser 的实际像素和尺寸输出。",
            ["样例文件可读，或临时目录可写。"],
            ["用户提供的 DOS EPS 样例；样例缺失时为 8x4 EPSI fixture。"],
            ["调用 readImageWithImageIO。", "检查 UTI、errorString、intrinsicSize、image size 和像素非空。"],
            ["样例得到 120x40，EPSI fallback 得到 8x4，均无错误。"],
            ["临时 fixture 和 native image 资源释放。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSPreviewDecode",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "UT-EPS-LOADER",
            "EPS native image 必须通过 QVImageLoader 异步请求，以 matching request id 返回非空 image、intrinsicSize 和无 errorData。",
            "功能正确性/可测试性",
            "unit",
            "验证生产异步加载链不需要 EPS 特殊分支。",
            ["EPS 样例或 EPSI fallback 可读。"],
            ["epsSamplePath 返回的文件路径。"],
            ["requestImage 并等待 imageReady。", "比较 absoluteFilePath、image、intrinsicSize 和 errorData。"],
            ["请求完成且输出与 native decode 尺寸一致，无 errorData。"],
            ["loader 和临时文件释放。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderLoadsEPS",
            "reports/evidence/eps_unit.json",
        ),
        case(
            "UT-EPS-MALFORMED",
            "截断 DOS EPS 的 TIFF 区间必须安全失败，不越界、不崩溃、不伪造图像。",
            "功能正确性/可测试性",
            "unit",
            "验证恶意或损坏预览输入的 fail-closed 行为。",
            ["临时目录可写。"],
            ["DOS EPS magic、TIFF offset=32、length=100、实际文件长度=32。"],
            ["写入 malformed.eps。", "调用 readImageWithImageIO 并检查返回。"],
            ["类型仍被识别为 EPS，image 为空，errorString 非空。"],
            ["损坏 fixture 删除随 QTemporaryDir 生命周期结束。"],
            "tests/tst_qviewtests.cpp::ImageLoaderTests::testMalformedEPSFailsSafely",
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
            "真实 Fovelle.app 以 EPS 样例为命令行输入时，必须观测到非零 item geometry 且无 unsupported-format 错误。",
            "功能正确性/可测试性",
            "system",
            "验证从进程启动、命令行打开到可见视口的端到端行为。",
            ["Fovelle.app 已构建。", "用户提供的 EPS 样例可读，或 pipeline 可创建 EPSI fallback。"],
            ["EPS 样例路径，重复运行 3 次。", "FOVELLE_DIAGNOSTIC_LOG=1。"],
            ["启动 app 并传入 EPS。", "读取 FOVELLE_VIEW。", "记录每次 geometry、退出码和错误输出。"],
            ["全部运行观测到非零 geometry，无 unsupported-format 错误。"],
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
            "The specification intentionally makes embedded-preview EPS the tested contract; full rendering of arbitrary PostScript without a preview would require a separate interpreter decision.",
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
                    "Host-specific limitations and preview-only scope are retained in the stage evidence.",
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
            "status": "passed" if static_passed and "external_renderer_invocation_absent" in json.dumps(stages["static"]["data"], ensure_ascii=False) else "failed",
            "criteria": [
                {"id": "CQ-EPS-LEAN-01", "result": static_passed, "evidence": "ST-EPS-REGISTRY/ST-EPS-PARSER-SAFETY"},
                {"id": "CQ-EPS-LEAN-02", "result": "external_renderer_invocation_absent" in json.dumps(stages["static"]["data"], ensure_ascii=False), "evidence": "ST-EPS-PARSER-SAFETY"},
            ],
            "assessment": "复用现有 native bridge、Result、异步 loader 和 Settings registry；新增代码只覆盖 EPS 预览识别/解码与必要注册。",
        },
        {
            "id": "CQ-EPS-FUNCTION",
            "quality_attribute": "功能正确性",
            "status": "passed" if all_cases_passed and unit_passed and integration_passed and system_passed else "failed",
            "criteria": [
                {"id": "CQ-EPS-FUNCTION-01", "result": all_cases_passed, "evidence": "reports/test_evidence.json"},
                {"id": "CQ-EPS-FUNCTION-02", "result": unit_passed and integration_passed and system_passed, "evidence": "eps_unit.json/eps_integration.json/eps_system.json"},
            ],
            "assessment": "以 native decode、异步 loader、Settings UI 和真实 app 打开路径分别验证输入输出和副作用。",
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
            "assessment": "输入可由 FOVELLE_EPS_SAMPLE 覆盖且有确定性 EPSI fallback；运行时通过 native result、Settings table、FOVELLE_VIEW 和原始 stdout/stderr 非侵入式观测。",
        },
    ]
    passed = all(item["status"] == "passed" for item in dimensions) and completion["passed"]
    return {
        "schema_version": "1.0",
        "kind": "code-quality-assessment-report",
        "generated_at_utc": generated_at,
        "repository": str(repo),
        "head_sha": git_head(repo),
        "scope": "EPS/EPSF/EPSI embedded-preview support, format registration, Settings list, README, Bundle declaration, tests, and audit artifacts",
        "dimensions": dimensions,
        "facts": [
            "All four requested quality dimensions have an explicit assessment dimension and evidence references.",
            "Time evidence stores average response, P99, maximum response, and throughput together with thresholds and measurement window.",
            "The assessment is based on stage return codes, atomic case records, and content-addressed evidence files.",
        ],
        "inferences": [
            "Passing all dimensions supports the inference that the implementation is complete for the stated embedded-preview EPS contract.",
        ],
        "uncertainties": [
            "Arbitrary pure PostScript EPS without a decodable TIFF or EPSI preview is not proven by these tests.",
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
            "A finite fixture matrix cannot establish support for every PostScript dialect, font dependency, or EPS preview variant.",
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
            "The report does not claim universal EPS vector rendering beyond the embedded preview contract.",
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
