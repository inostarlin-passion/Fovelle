#!/usr/bin/env python3
"""Generate the one-to-one atomic acceptance/test specification."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path


def case(
    identifier: str,
    criterion: str,
    attribute: str,
    level: str,
    purpose: str,
    preconditions: str,
    input_data: str,
    steps: str,
    expected: str,
    postconditions: str,
    test_code: str,
) -> dict:
    return {
        "id": identifier,
        "atomic_acceptance_criterion": criterion,
        "quality_attribute": attribute,
        "test_level": level,
        "test_purpose": purpose,
        "preconditions": preconditions,
        "input_data": input_data,
        "operation_steps": steps,
        "expected_result": expected,
        "postconditions": postconditions,
        "test_code": test_code,
        "evidence_file": f"reports/evidence/hdr_{level}.json",
    }


CASES = [
    case("ST-HDR-RAW-CONTENT-UTI", "RAW 必须按文件内容 UTI 而非扩展名分类。", "功能正确性", "static", "验证 RAW 分类入口。", "源码可读。", "RAW UTI 判定实现。", "检查 CGImageSourceGetType 与 public.camera-raw-image conformance，确认 RAW 分支不读取 suffix。", "内容 UTI 唯一决定 isRaw。", "不修改源码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-RAW-CIRAWFILTER", "RAW 主路径必须使用 CIRAWFilter 并保留 SDR/EDR 两个图。", "精益完整性", "static", "验证传感器 RAW 原生解码图。", "Objective-C++ 源码可读。", "RAW decode 分支。", "检查 filterWithImageURL、extendedDynamicRangeAmount=0/1、NativeHDRImage 发布。", "两个 CIImage 图均存在于高精度句柄。", "不触发实际解码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-RAW-PREVIEW-FALLBACK", "RAW 预览只能在主解码失败后使用且必须可观测。", "功能正确性", "static", "防止嵌入 JPEG 冒充 RAW HDR。", "源码可读。", "RAW fallback 控制流。", "检查 preview 分支顺序、usedRawPreview 与不支持相机错误。", "预览是显式回退而非主路径。", "不修改源码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-NONRAW-METADATA", "非 RAW 必须解析 gain map、headroom、色彩空间和传输函数。", "功能正确性", "static", "验证 HDR 元数据覆盖。", "macOS SDK 符号可编译。", "Apple/ISO gain map 与 PQ/HLG 代码。", "检查两类 auxiliary data、CG/CI headroom、PQ/HLG 判定。", "所有关键 HDR 元数据均有解析路径。", "不解码文件。", "tests/hdr_quality_static.py"),
    case("ST-HDR-NONRAW-RECONSTRUCTION", "非 RAW HDR 必须通过 Image I/O HDR 请求与 CI expand 重建。", "功能正确性", "static", "验证 gain-map/ISO HDR 重建链。", "源码可读。", "非 RAW decode 分支。", "检查 DecodeToHDR、kCIImageExpandToHDR、方向与候选判定。", "HDR graph 不经普通 SDR 加载路径。", "不修改源码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-FLOAT-INTERMEDIATE", "HDR 主路径必须保持 half-float CI 图，RGBA8 只能用于回退。", "功能正确性", "static", "验证禁止提前量化不变量。", "源码可读。", "CIContext、NativeHDRImage、loader。", "检查 RGBAh context、CIImage retain、跨 loader 句柄和具名 SDR fallback helper。", "主图保持浮点，QImage 仅为 fallback。", "不分配图像。", "tests/hdr_quality_static.py"),
    case("ST-HDR-COLORSYNC", "HDR 工作/输出空间必须由 ColorSync 建立扩展线性 Display P3。", "功能正确性", "static", "验证色彩管理链。", "ColorSync/Core Image 源码可读。", "色彩空间与 CI context 设置。", "检查 Display P3 profile、extended linearization、working/output space。", "源到显示的 CI/ColorSync 变换完整。", "不改变系统 profile。", "tests/hdr_quality_static.py"),
    case("ST-HDR-METAL-EDR-SURFACE", "最终 surface 必须是 Metal RGBA16Float 且请求 EDR。", "功能正确性", "static", "验证 WindowServer 前的输出契约。", "源码与构建文件可读。", "CAMetalLayer/CIContext 配置。", "检查 Metal context、RGBA16Float、wantsEDR 与 QuartzCore 链接。", "输出 layer 符合 EDR 精度契约。", "不创建窗口。", "tests/hdr_quality_static.py"),
    case("ST-HDR-DISPLAY-ADAPTATION", "每次绘制必须读取窗口所在屏幕的当前/潜在 headroom。", "功能正确性", "static", "验证动态显示适配。", "renderer 源码可读。", "NSScreen 查询与测试 override。", "检查 window.screen、current/potential EDR API 和确定性 override。", "显示能力不是启动时常量。", "不改变真实 headroom。", "tests/hdr_quality_static.py"),
    case("ST-HDR-TONE-MAPPING", "非 RAW HDR 必须按内容/显示 headroom 色调映射。", "功能正确性", "static", "验证 HDR 与 SDR 输出兼容。", "macOS 15 SDK 可用。", "CIToneMapHeadroom 和 CALayer 配置。", "检查 source/target headroom 参数及 automatic layer tone map。", "系统 tone mapper 接收正确的两端 headroom。", "不渲染像素。", "tests/hdr_quality_static.py"),
    case("ST-HDR-GEOMETRY", "HDR layer 必须复用缩放、滚动、旋转与镜像几何。", "功能正确性", "static", "验证原生层不脱离 Qt 交互。", "view/renderer 源码可读。", "四角映射和 CI 仿射矩阵。", "检查 viewportTransform、四角与 Metal destination affine。", "HDR 图与逻辑源尺寸使用同一变换。", "不改变视图。", "tests/hdr_quality_static.py"),
    case("ST-HDR-OBSERVABILITY", "运行态必须非侵入式输出解码、渲染、headroom 与过渡遥测。", "可测试性", "static", "验证机器可观测接口。", "源码可读。", "FOVELLE_HDR compact JSON。", "检查 JSON 日志、decode/render timing 与 headroom/transition 字段。", "系统测试可从 stderr 恢复完整状态。", "默认未设置环境变量时不输出。", "tests/hdr_quality_static.py"),
    case("ST-HDR-VERSION-0.1.4", "所有发布版本来源必须一致为 0.1.4。", "精益完整性", "static", "验证版本合同。", "CMake、qmake、plist 和测试源码可读。", "四类版本来源。", "逐一比较 0.1.4。", "静态版本来源一致。", "不修改构建缓存。", "tests/hdr_quality_static.py"),
    case("UT-HDR-TRANSITION", "HDR 内容过渡函数必须有界且单调。", "功能正确性", "unit", "确定性验证 smoothstep。", "无需物理显示器。", "[-1,2] 及 0..1 百分点。", "调用 easedHDRTransition 并比较端点与相邻值。", "输出在 [0,1]、端点正确且不下降。", "无全局状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testTransitionCurveIsBoundedAndMonotonic"),
    case("UT-HDR-HEADROOM-CLAMP", "目标 headroom 不得超过内容与显示可用值。", "功能正确性", "unit", "验证 headroom 不变量。", "纯 helper 可调用。", "content/display 4/6、8/3、unknown/3。", "计算完整与半程过渡。", "完整值分别为 4、3、3，半程在 1 与 4 之间。", "无显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testHDRHeadroomIsClampedToContentAndDisplay"),
    case("UT-HDR-SDR-FALLBACK", "显示 headroom=1 时任何 HDR 内容目标都必须为 1。", "功能正确性", "unit", "确定性验证 SDR 自动兼容。", "纯 helper 可调用。", "content=4.9473、display=1、三种 progress。", "计算 effectiveHDRHeadroom。", "三次均严格等于 1。", "无显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testSDRDisplayForcesUnitHeadroom"),
    case("UT-HDR-RENDERER-CONTRACT", "renderer 必须报告 float、ColorSync、扩展线性 P3 与 EDR。", "可测试性", "unit", "通过诊断结构验证 surface。", "Cocoa 与 Metal 可用。", "320×200 QWidget。", "创建 renderer 并读取 diagnostics。", "所有精度/色彩/EDR flag 为 true，图像尚未 active。", "销毁临时 layer。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testRendererUsesFloatEDRColorManagedSurface"),
    case("UT-HDR-SDR-CLASSIFICATION", "普通 SDR 图不得被错误提升到 HDR 主路径。", "功能正确性", "unit", "验证 HDR 分类负例。", "临时目录可写。", "32×32 sRGB PNG。", "经 Image I/O 解码并检查结果。", "QImage 有效、HDR handle 为空、decodedToHDR=false。", "删除临时文件。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testSDRImageStaysOnSDRPath"),
    case("UT-HDR-FORMAT-COVERAGE", "目标 macOS 必须声明所需 RAW 与非 RAW 格式。", "精益完整性", "unit", "验证格式覆盖。", "使用目标机 Image I/O。", "dng/nef/cr3/arw/raf/jpeg/heif/heic/avif。", "逐个查询 supportsAdditionalImageFormat。", "全部格式返回支持。", "释放类型列表。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testRequiredHDRFormatsAreAdvertised"),
    case("UT-HDR-VERSION", "运行时应用版本必须为 0.1.4。", "功能正确性", "unit", "验证编译定义传到运行时。", "测试应用已初始化。", "applicationVersion。", "读取并比较字符串。", "严格等于 0.1.4。", "不改变应用元数据。", "tests/tst_qviewtests.cpp::FeatureTests::testApplicationVersionIsCurrent"),
    case("IT-HDR-GAINMAP-JPEG", "提供的 JPEG 必须产生全分辨率 adaptive HDR 句柄。", "功能正确性", "integration", "验证真实 gain-map JPEG。", "样例存在且 macOS 14+。", "IMG_1735.JPG，fallback 2048px。", "编译测试调用生产 decoder 并检查 UTI、gain map、headroom、尺寸。", "HDR handle/full resolution 保留，headroom>1，只有 SDR fallback≤2048。", "释放 CI graph。", "tests/tst_qviewtests.cpp::HDRSampleTests::testGainMapJPEGCreatesNativeHDRGraph"),
    case("IT-HDR-RAW-DNG", "提供的 DNG 必须产生传感器 RAW EDR 句柄。", "功能正确性", "integration", "验证真实 DNG。", "样例存在且 Apple RAW 支持该相机。", "IMG_8625.DNG，fallback 2048px。", "编译测试调用生产 decoder 并检查 RAW、16-bit 契约、尺寸与 preview flag。", "CIRAW EDR handle/full resolution 保留且 preview 非主路径。", "释放 RAW graph。", "tests/tst_qviewtests.cpp::HDRSampleTests::testDNGCreatesNativeRawEDRGraph"),
    case("SYS-HDR-GAINMAP-JPEG-EDR", "真实窗口中的 gain-map JPEG 必须达到 EDR target>1。", "功能正确性", "system", "端到端验证非 RAW HDR。", "XDR Mac、Cocoa app 和样例可用。", "JPEG 启动 3 次。", "打开窗口、采集 4.2s FOVELLE_HDR telemetry。", "每次识别 adaptive HDR/gain map 且 target>1.1。", "终止测试进程，保存日志。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-RAW-DNG-EDR", "真实窗口中的 DNG 必须走 RAW EDR 且不使用 preview。", "功能正确性", "system", "端到端验证 RAW HDR。", "XDR Mac、Cocoa app 和样例可用。", "DNG 启动 3 次。", "打开窗口并采集 RAW renderer telemetry。", "isRaw/RAW EDR/16-bit 为真，preview=false，target>1.1。", "终止测试进程，保存日志。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-FLOAT-COLORMANAGED-EDR-SURFACE", "端到端 surface 必须持续报告 RGBA16Float/ColorSync/EDR。", "功能正确性", "system", "验证实际窗口 layer 配置。", "两种 HDR 样例可渲染。", "全部真实运行 telemetry。", "聚合每帧配置 flag。", "所有记录均为 float、扩展 P3、ColorSync、wantsEDR。", "销毁窗口 layer。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-SMOOTH-ACTIVATION", "打开 HDR 图后的内容强度必须平滑单调到 1。", "功能正确性", "system", "验证类似 Quick Look 的渐亮激活。", "定时器和 telemetry 可用。", "每次启动的 transition_progress。", "比较首帧、末帧与所有相邻采样。", "首帧≤0.1、末帧≥0.999、全程单调。", "定时器在约 1.8s 后停止。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-WINDOWSERVER-HEADROOM", "EDR layer 必须与 WindowServer 协商出实际 headroom>1。", "功能正确性", "system", "验证物理 XDR 输出而非 SDR bitmap。", "内建 XDR 显示器可用。", "真实运行的 current/potential/target headroom。", "读取每帧 NSScreen telemetry 并比较上限。", "potential/current 均曾>1，target 从不超过 current。", "系统自行恢复亮度策略。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-FORCED-SDR-COMPATIBILITY", "强制显示 headroom=1 时真实 renderer target 必须保持 1。", "功能正确性", "system", "端到端验证 SDR 自动兼容。", "测试 override 可用。", "JPEG + FOVELLE_TEST_DISPLAY_HEADROOM=1。", "打开真实窗口并采集 telemetry。", "override=true 且所有 current/target 均为 1。", "清除子进程环境变量。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-TIME-BEHAVIOR", "规定 48MP 工作负载的平均/P99/最大/吞吐量必须达标。", "时间行为", "system", "量化解码与稳态提交。", "M3 Pro/XDR、两样例、各 3 次。", "6 个 decode 样本；排除每次前 3 帧后的 render 样本。", "计算 average/P99/max 和两类 throughput，与固定阈值比较。", "所有 10 个时间断言通过，原始样本写入 JSON。", "进程全部终止。", "tests/hdr_quality_system.py"),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    required = {
        "id", "atomic_acceptance_criterion", "quality_attribute", "test_level",
        "test_purpose", "preconditions", "input_data", "operation_steps",
        "expected_result", "postconditions", "test_code", "evidence_file",
    }
    identifiers = [item["id"] for item in CASES]
    validation_errors = []
    if len(identifiers) != len(set(identifiers)):
        validation_errors.append("test case identifiers are not unique")
    for item in CASES:
        missing = sorted(required - item.keys())
        empty = sorted(key for key in required if not item.get(key))
        if missing or empty:
            validation_errors.append({"id": item.get("id"), "missing": missing, "empty": empty})
    phase_counts = {
        phase: sum(item["test_level"] == phase for item in CASES)
        for phase in ("static", "unit", "integration", "system")
    }
    record = {
        "schema_version": "1.0",
        "kind": "atomic-test-case-specification",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "execution_order": ["static", "unit", "integration", "system"],
        "required_case_fields": sorted(required),
        "case_count": len(CASES),
        "phase_counts": phase_counts,
        "cases": CASES,
        "validation_errors": validation_errors,
        "passed": not validation_errors and all(count > 0 for count in phase_counts.values()),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"case_count": len(CASES), "phase_counts": phase_counts, "passed": record["passed"]}, ensure_ascii=False))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
