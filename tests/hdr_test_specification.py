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
    case("ST-HDR-RAW-CIRAWFILTER", "RAW 主路径必须使用两个独立 CIRAWFilter 图，SDR 保留相机默认，HDR 中和负基线曝光并启用 EDR。", "精益完整性", "static", "验证传感器 RAW 原生解码图与 HDR 亮度策略。", "Objective-C++ 源码可读。", "RAW decode 分支。", "检查两次 filterWithImageURL、extendedDynamicRangeAmount=0/1、仅 HDR filter 的负 baselineExposure→0 与 NativeHDRImage 发布。", "惰性 SDR/EDR 图不共享可变 filter 状态；HDR 不携带面向 SDR 的负曝光压缩。", "不触发实际解码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-RAW-PREVIEW-FALLBACK", "RAW 预览只能在主解码失败后使用且必须可观测。", "功能正确性", "static", "防止嵌入 JPEG 冒充 RAW HDR。", "源码可读。", "RAW fallback 控制流。", "检查 preview 分支顺序、usedRawPreview 与不支持相机错误。", "预览是显式回退而非主路径。", "不修改源码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-NONRAW-METADATA", "非 RAW 必须解析 gain map、headroom、色彩空间和传输函数。", "功能正确性", "static", "验证 HDR 元数据覆盖。", "macOS SDK 符号可编译。", "Apple/ISO gain map 与 PQ/HLG 代码。", "检查两类 auxiliary data、CG/CI headroom、PQ/HLG 判定。", "所有关键 HDR 元数据均有解析路径。", "不解码文件。", "tests/hdr_quality_static.py"),
    case("ST-HDR-NONRAW-RECONSTRUCTION", "非 RAW HDR 必须通过 Image I/O HDR 请求与 CI expand 重建。", "功能正确性", "static", "验证 gain-map/ISO HDR 重建链。", "源码可读。", "非 RAW decode 分支。", "检查 DecodeToHDR、kCIImageExpandToHDR、方向与候选判定。", "HDR graph 不经普通 SDR 加载路径。", "不修改源码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-NONRAW-NONVOLATILE-DECODE", "Gain-map JPEG 的 SDR/HDR 源必须在初始化时进入非易失解码缓存，且只在源图缓存后执行视口裁剪。", "功能正确性", "static", "防止延迟 gain-map 求值在缩放或拖动 ROI 中产生黑带和旧 tile。", "Core Image 非 RAW 解码与 renderer 源码可读。", "HDR/SDR CIImage options、source-space intermediate 与 viewport transform。", "检查两个 recipe 均设置 kCIImageCacheImmediately=YES，并验证 imageByInsertingIntermediate 位于 imageForTexture 之前。", "源图和 gain map 在局部 render 前已稳定解码；视口几何不成为源缓存键。", "不执行实际解码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-FLOAT-INTERMEDIATE", "HDR 主路径必须保持 half-float CI 图，RGBA8 只能用于回退。", "功能正确性", "static", "验证禁止提前量化不变量。", "源码可读。", "CIContext、NativeHDRImage、loader。", "检查 RGBAh context、CIImage retain、跨 loader 句柄和具名 SDR fallback helper。", "主图保持浮点，QImage 仅为 fallback。", "不分配图像。", "tests/hdr_quality_static.py"),
    case("ST-HDR-COLORSYNC", "HDR 工作/输出空间必须由 ColorSync 建立扩展线性 Display P3。", "功能正确性", "static", "验证色彩管理链。", "ColorSync/Core Image 源码可读。", "色彩空间与 CI context 设置。", "检查 Display P3 profile、extended linearization、working/output space。", "源到显示的 CI/ColorSync 变换完整。", "不改变系统 profile。", "tests/hdr_quality_static.py"),
    case("ST-HDR-METAL-EDR-SURFACE", "最终 surface 必须是 Metal RGBA16Float 且请求 EDR。", "功能正确性", "static", "验证 WindowServer 前的输出契约。", "源码与构建文件可读。", "CAMetalLayer/CIContext 配置。", "检查 Metal context、RGBA16Float、wantsEDR 与 QuartzCore 链接。", "输出 layer 符合 EDR 精度契约。", "不创建窗口。", "tests/hdr_quality_static.py"),
    case("ST-HDR-DISPLAY-ADAPTATION", "每次绘制必须读取窗口所在屏幕的当前/潜在 headroom。", "功能正确性", "static", "验证动态显示适配。", "renderer 源码可读。", "NSScreen 查询与测试 override。", "检查 window.screen、current/potential EDR API，以及区分全显示能力与仅 current 的确定性 override。", "显示能力不是启动时常量，且可独立控制 bootstrap 前置条件。", "不改变真实 headroom。", "tests/hdr_quality_static.py"),
    case("ST-HDR-EDR-BOOTSTRAP", "当前 headroom 仍为 1、但潜在 headroom>1 时，renderer 必须用潜在能力启动首个 EDR 帧。", "功能正确性", "static", "防止等待已有 EDR 内容才输出 EDR 的循环依赖。", "NSScreen 与 renderer 源码可读。", "current/potential/content 三种 headroom。", "检查纯策略 helper、rendering headroom、bootstrapping 诊断和 target 计算链。", "潜在 EDR 显示器可从 current=1 启动 HDR；潜在值=1 时仍保持 SDR。", "不改变系统 headroom。", "tests/hdr_quality_static.py"),
    case("ST-HDR-TONE-MAPPING", "非 RAW HDR 必须按内容/显示 headroom 色调映射。", "功能正确性", "static", "验证 HDR 与 SDR 输出兼容。", "macOS 15 SDK 可用。", "CIToneMapHeadroom 和 CALayer 配置。", "检查 source/target headroom 参数及 automatic layer tone map。", "系统 tone mapper 接收正确的两端 headroom。", "不渲染像素。", "tests/hdr_quality_static.py"),
    case("ST-HDR-GEOMETRY", "HDR layer 必须复用缩放、滚动、旋转与镜像几何。", "功能正确性", "static", "验证原生层不脱离 Qt 交互。", "view/renderer 源码可读。", "四角映射和 CI 仿射矩阵。", "检查 viewportTransform、四角与 Metal destination affine。", "HDR 图与逻辑源尺寸使用同一变换。", "不改变视图。", "tests/hdr_quality_static.py"),
    case("ST-HDR-STAGED-FIRST-FRAME", "最终几何和首个 drawable 呈现前不得移除 SDR 占位或显示 Metal layer。", "功能正确性", "static", "防止空 layer、JPEG 黑块与旧几何首帧。", "view/renderer 源码可读。", "几何稳定 gate、SDR proxy、layer opacity 和 presented handler。", "检查稳定回调才 arm、proxy 常驻到 presented、Metal 从 opacity 0 切到 1。", "未呈现阶段始终有正确占位，且不会暴露空 drawable。", "不创建窗口。", "tests/hdr_quality_static.py"),
    case("ST-HDR-OFFSCREEN-PREPARATION", "HDR 昂贵首求值必须在可见渐亮前由 Core Image 管理的可缓存 float intermediate 完成。", "时间行为", "static", "既防止 RAW 首求值吞掉过渡窗口，也防止自有纹理反向导入产生未解析 GPU tile。", "renderer 源码可读。", "SDR/HDR Core Image managed intermediates 与串行预热 command buffer。", "检查 cacheIntermediates=YES、imageByInsertingIntermediate:YES、预热、prepared 合成，并确认 renderer 不调用 imageWithMTLTexture。", "过渡复用 Core Image 管理的浮点源端点；应用自有 render-target 从不作为后续 CI 输入。", "缓存只随图像替换销毁，交互几何变换继续复用。", "tests/hdr_quality_static.py"),
    case("ST-HDR-GEOMETRY-LIFECYCLE", "首帧前几何变化可以失效未呈现 generation；首帧呈现且端点预热后，缩放、拖动或 resize 必须复用源端点并保持 HDR 可见。", "功能正确性", "static", "防止 DNG 局部空白、拖动残影以及交互亮度回落。", "view/renderer 源码可读。", "viewport size、四角、34ms debounce、generation、prepared 状态和 layer opacity。", "检查完整几何比较、首帧前 invalidate、呈现后 reuseVisibleHDR、源空间 cache 与 autoresizingMask。", "未呈现帧由 proxy 隔离；已呈现 HDR 在几何变化期间不清缓存、不降 opacity、不切回 SDR。", "不触发真实输入。", "tests/hdr_quality_static.py"),
    case("ST-HDR-OBSERVABILITY", "运行态必须非侵入式输出解码、渲染、headroom、过渡、背景色与 viewport/图像像素裁剪遥测。", "可测试性", "static", "验证机器可观测接口与桌面无关的截图测量。", "源码可读。", "FOVELLE_HDR compact JSON。", "检查 JSON 日志、decode/render timing、content/layer/display headroom、transition、主题 RGB、image corners 及 viewport global origin/size/DPR 字段。", "系统测试可从 stderr 恢复完整状态并把背景/结构截图指标限制到被测区域。", "默认未设置环境变量时不输出。", "tests/hdr_quality_static.py"),
    case("ST-HDR-RAW-STABLE-ENDPOINTS", "交互 RAW renderer 必须使用两个不可互相变异的图、启用 CIContext intermediate cache，并显式缓存源空间 SDR/HDR 端点。", "功能正确性", "static", "防止定时相关的 RAW tile 丢失或部分空白。", "RAW decoder 与 renderer 源码可读。", "独立 CIRAWFilter、CI context options 与 source intermediates。", "检查两个 filter 变量、kCIContextCacheIntermediates=YES、两次 imageByInsertingIntermediate:YES。", "重复渲染和变换不会依赖上一次可变 filter 状态，源端点可按需稳定复用。", "不执行 RAW 解码。", "tests/hdr_quality_static.py"),
    case("ST-HDR-RAW-CONTENT-HEADROOM", "RAW 未报告 content headroom 时必须从浮点 HDR 峰值解析，并把实际目标而非显示潜力写入 CIImage/CAMetalLayer 内容标签。", "功能正确性", "static", "防止把约 1.8× 内容错误标成 16× 后被系统强烈压暗。", "macOS HDR API 源码可读。", "CIRAWFilter HDR graph、CIAreaMaximum、CIImage/CALayer headroom。", "检查浮点峰值约减、resolvedHDRContentHeadroom、imageBySettingContentHeadroom 和 layer contentsHeadroom=target。", "内容标签表达像素范围；显示能力只作为 target 上限，不冒充内容 metadata。", "不更改显示器 headroom。", "tests/hdr_quality_static.py"),
    case("ST-HDR-THEME-BACKGROUND", "Qt painter 与 Metal drawable 必须使用同一显式主题背景色，并经 sRGB→扩展线性 P3 色彩管理合成。", "功能正确性", "static", "消除约两秒后 Metal reveal 导致的背景变浅和主题切换失效。", "主窗口、视图与 renderer 源码可读。", "Light #969696、Dark #212121。", "检查共享 viewportBackgroundColor、renderer setter、sRGB source tag、opaque composite，并确认未使用 NSColor.windowBackgroundColor。", "Metal reveal 前后背景颜色合同不变；设置更新可立即重绘新主题。", "不修改持久化主题。", "tests/hdr_quality_static.py"),
    case("ST-HDR-INTERACTION-NO-REACTIVATION", "HDR 打开激活完成后，任何缩放、平移或 resize 均不得重置渐亮时钟、进度或切回 SDR proxy。", "功能正确性", "static", "移除交互时先变暗再变亮的全场景特性。", "视图状态机源码可读。", "activationCompleted、presented/prepared reuse policy 与 stageHDRGeometry。", "检查 canReuseHDRPresentation、stage 内无 clock/progress reset、完成标记与复用时 fallback=false。", "平缓变亮仅绑定新图像打开；交互只改变几何。", "不触发真实输入。", "tests/hdr_quality_static.py"),
    case("ST-HDR-VERSION-0.1.4", "所有发布版本来源必须一致为 0.1.4。", "精益完整性", "static", "验证版本合同。", "CMake、qmake、plist 和测试源码可读。", "四类版本来源。", "逐一比较 0.1.4。", "静态版本来源一致。", "不修改构建缓存。", "tests/hdr_quality_static.py"),
    case("UT-HDR-TRANSITION", "HDR 内容过渡函数必须有界且单调。", "功能正确性", "unit", "确定性验证 smoothstep。", "无需物理显示器。", "[-1,2] 及 0..1 百分点。", "调用 easedHDRTransition 并比较端点与相邻值。", "输出在 [0,1]、端点正确且不下降。", "无全局状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testTransitionCurveIsBoundedAndMonotonic"),
    case("UT-HDR-PRESENTATION-PACING", "GPU 延迟后过渡线性进度每个可提交帧最多前进 0.04，且不得倒退或越界。", "功能正确性", "unit", "防止 wall-clock 延迟变成肉眼亮度跳变。", "纯 helper 可调用。", "previous/desired/step 的正常、延迟、倒退、越界和零步长组合。", "调用 pacedHDRTransitionProgress。", "输出单调、有界，desired 大跳时只增加 step。", "无时钟或显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testPresentationPacingBoundsDelayedFrameProgress"),
    case("UT-HDR-HEADROOM-CLAMP", "目标 headroom 不得超过内容与显示可用值。", "功能正确性", "unit", "验证 headroom 不变量。", "纯 helper 可调用。", "content/display 4/6、8/3、unknown/3。", "计算完整与半程过渡。", "完整值分别为 4、3、3，半程在 1 与 4 之间。", "无显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testHDRHeadroomIsClampedToContentAndDisplay"),
    case("UT-HDR-SDR-FALLBACK", "显示 headroom=1 时任何 HDR 内容目标都必须为 1。", "功能正确性", "unit", "确定性验证 SDR 自动兼容。", "纯 helper 可调用。", "content=4.9473、display=1、三种 progress。", "计算 effectiveHDRHeadroom。", "三次均严格等于 1。", "无显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testSDRDisplayForcesUnitHeadroom"),
    case("UT-HDR-EDR-BOOTSTRAP", "rendering headroom 必须在 current=1/potential>1 时启动 EDR，并在 current>1 后跟随动态 current。", "功能正确性", "unit", "确定性验证 EDR bootstrap 策略。", "纯 helper 可调用。", "(1,1,5)、(1,16,4.9473)、(1,4,unknown)、(3.5,16,5)。", "调用 displayHeadroomForRendering 并逐项比较。", "分别得到 1、4.9473、4、3.5。", "无显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testDisplayHeadroomBootstrapsFromPotentialCapability"),
    case("UT-HDR-STAGED-PRESENTATION", "过渡只能在布局完成、首帧已呈现且 HDR 已预热三条件同时满足时开始。", "功能正确性", "unit", "穷举验证展示 gate。", "纯 helper 可调用。", "三个布尔条件的全部 8 种组合。", "逐项调用 shouldStartHDRTransition。", "仅全 true 返回 true，其余 7 种均 false。", "无 UI 或显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testTransitionStartsOnlyAfterStagedPresentation"),
    case("UT-HDR-GEOMETRY-EQUIVALENCE", "Metal 几何稳定判定必须比较 viewport size 与全部四角，并只容忍规定的浮点误差。", "功能正确性", "unit", "验证 debounce 输入合同而不依赖真实窗口。", "纯 helper 可调用。", "相同几何、0.005/0.02 像素差、不同 viewport、不同角点数。", "调用 hdrViewportGeometryEquivalent。", "相同与 0.005 差返回 true；其余返回 false。", "无 UI 状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testHDRViewportGeometryEquivalenceUsesCompleteContract"),
    case("UT-HDR-CONTENT-HEADROOM", "未知或无效的 RAW content headroom 必须优先解析为有效浮点峰值，且结果至少为 1。", "功能正确性", "unit", "确定性验证内容范围解析，不依赖显示器能力。", "纯 helper 可调用。", "reported/measured=(4,1.8)、(0,1.8321)、(0,0.7)、(-1,-2)。", "调用 resolvedHDRContentHeadroom。", "结果依次为 4、1.8321、1、1。", "无图像或显示状态变化。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testRawContentHeadroomUsesMeasuredPeakWhenUnknown"),
    case("UT-HDR-PRESENTATION-REUSE", "仅当 HDR 首帧已呈现且源端点已准备时，几何交互才允许复用可见 HDR。", "功能正确性", "unit", "穷举验证交互不重启的 gate。", "纯 helper 可调用。", "presented/prepared 的四种布尔组合。", "逐项调用 canReuseHDRPresentation。", "只有 true/true 返回 true。", "不改变 renderer generation。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testPreparedHDRPresentationCanBeReusedAcrossGeometry"),
    case("UT-HDR-THEME-BACKGROUND", "共享主题背景解析必须严格返回 Light #969696 和 Dark #212121。", "功能正确性", "unit", "验证 Qt 与 Metal 可共享的确定性色值。", "无需窗口。", "Light、Dark 两枚 Theme 枚举。", "调用 viewportBackgroundColor 并比较 QColor。", "两项与发布颜色合同逐位一致。", "不改变 palette 或设置。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testViewportBackgroundColorsMatchTheme"),
    case("UT-HDR-RENDERER-CONTRACT", "renderer 必须报告 float、ColorSync、扩展线性 P3 与 EDR。", "可测试性", "unit", "通过诊断结构验证 surface。", "Cocoa 与 Metal 可用。", "320×200 QWidget。", "创建 renderer 并读取 diagnostics。", "所有精度/色彩/EDR flag 为 true，图像尚未 active。", "销毁临时 layer。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testRendererUsesFloatEDRColorManagedSurface"),
    case("UT-HDR-SDR-CLASSIFICATION", "普通 SDR 图不得被错误提升到 HDR 主路径。", "功能正确性", "unit", "验证 HDR 分类负例。", "临时目录可写。", "32×32 sRGB PNG。", "经 Image I/O 解码并检查结果。", "QImage 有效、HDR handle 为空、decodedToHDR=false。", "删除临时文件。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testSDRImageStaysOnSDRPath"),
    case("UT-HDR-FORMAT-COVERAGE", "目标 macOS 必须声明所需 RAW 与非 RAW 格式。", "精益完整性", "unit", "验证格式覆盖。", "使用目标机 Image I/O。", "dng/nef/cr3/arw/raf/jpeg/heif/heic/avif。", "逐个查询 supportsAdditionalImageFormat。", "全部格式返回支持。", "释放类型列表。", "tests/tst_qviewtests.cpp::HDRPolicyTests::testRequiredHDRFormatsAreAdvertised"),
    case("UT-HDR-VERSION", "运行时应用版本必须为 0.1.4。", "功能正确性", "unit", "验证编译定义传到运行时。", "测试应用已初始化。", "applicationVersion。", "读取并比较字符串。", "严格等于 0.1.4。", "不改变应用元数据。", "tests/tst_qviewtests.cpp::FeatureTests::testApplicationVersionIsCurrent"),
    case("IT-HDR-GAINMAP-JPEG", "提供的 JPEG 必须产生全分辨率 adaptive HDR 句柄。", "功能正确性", "integration", "验证真实 gain-map JPEG。", "样例存在且 macOS 14+。", "IMG_1735.JPG，fallback 2048px。", "编译测试调用生产 decoder 并检查 UTI、gain map、headroom、尺寸。", "HDR handle/full resolution 保留，headroom>1，只有 SDR fallback≤2048。", "释放 CI graph。", "tests/tst_qviewtests.cpp::HDRSampleTests::testGainMapJPEGCreatesNativeHDRGraph"),
    case("IT-HDR-GAINMAP-JPEG-PEAK", "提供的 gain-map JPEG HDR 图必须包含超过 SDR white 的浮点值。", "功能正确性", "integration", "用像素数据证明非 RAW HDR 不是仅有 metadata flag。", "样例存在且 Image I/O 可展开 gain map。", "同一 JPEG 的 SDR/HDR CI graph。", "以 CIAreaMaximum 约减两图并比较 RGB 峰值。", "HDR 峰值>1.05 且至少比 SDR 高 0.05。", "释放 probe context 和 CI graph。", "tests/tst_qviewtests.cpp::HDRSampleTests::testGainMapJPEGHDRContainsAboveSDRValues"),
    case("IT-HDR-RAW-DNG", "提供的 DNG 必须产生传感器 RAW EDR 句柄。", "功能正确性", "integration", "验证真实 DNG。", "样例存在且 Apple RAW 支持该相机。", "IMG_8625.DNG，fallback 2048px。", "编译测试调用生产 decoder 并检查 RAW、16-bit 契约、尺寸与 preview flag。", "CIRAW EDR handle/full resolution 保留且 preview 非主路径。", "释放 RAW graph。", "tests/tst_qviewtests.cpp::HDRSampleTests::testDNGCreatesNativeRawEDRGraph"),
    case("IT-HDR-RAW-DNG-PEAK", "提供的 DNG EDR 图必须包含超过 SDR white 的真实浮点值。", "功能正确性", "integration", "用像素值而非 flag 证明 RAW HDR。", "样例存在且 CIRAWFilter 可解码。", "同一 DNG 的 EDR amount 0/1 CI graph。", "以 CIAreaMaximum 将两图各约减为一个 RGBAf 像素并比较 RGB 峰值。", "SDR≤1.01、HDR>1.05 且至少高 0.05。", "释放 probe context 和 RAW graph。", "tests/tst_qviewtests.cpp::HDRSampleTests::testDNGRawEDRContainsAboveSDRValues"),
    case("IT-HDR-RAW-DNG-HEADROOM-TAG", "提供的 DNG metadata content headroom 必须等于其 HDR 浮点图实测峰值。", "功能正确性", "integration", "证明 RAW layer 得到真实内容标签而非显示器潜力。", "样例存在且 CIRAWFilter 可解码。", "IMG_8625.DNG metadata 与 CIAreaMaximum 峰值。", "生产 decoder 解码一次，再独立 probe 保留的 HDR graph。", "metadata>1.5 且与 measured maximum 差≤0.02。", "释放 probe context 和 RAW graphs。", "tests/tst_qviewtests.cpp::HDRSampleTests::testDNGRawHeadroomMatchesMeasuredFloatPeak"),
    case("IT-HDR-RAW-DNG-REPEATABILITY", "同一 DNG 的独立 SDR/HDR 图连续两次浮点求值必须得到稳定端点。", "功能正确性", "integration", "捕获可变 filter 或时序依赖导致的 RAW 不完整求值。", "样例已解码为两个独立 CIRAWFilter graph。", "同一 NativeHDRImage 的两次 probe。", "连续调用 probeHDRPixelStatistics，比较 SDR 与 HDR maximum。", "两次均 valid，每个端点差≤0.001。", "probe 之间不修改 filter 属性。", "tests/tst_qviewtests.cpp::HDRSampleTests::testDNGRawRepeatedFloatProbeIsStable"),
    case("SYS-HDR-GAINMAP-JPEG-EDR", "真实窗口中的 gain-map JPEG 必须达到 EDR target>1。", "功能正确性", "system", "端到端验证非 RAW HDR。", "XDR Mac、Cocoa app 和样例可用。", "JPEG 启动 3 次。", "打开窗口、采集 7s FOVELLE_HDR telemetry。", "每次识别 adaptive HDR/gain map 且 target>1.1。", "终止测试进程，保存日志。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-RAW-DNG-EDR", "真实窗口中的 DNG 必须走 RAW EDR 且不使用 preview。", "功能正确性", "system", "端到端验证 RAW HDR。", "XDR Mac、Cocoa app 和样例可用。", "DNG 启动 3 次。", "打开窗口并采集 RAW renderer telemetry。", "isRaw/RAW EDR/16-bit 为真，preview=false，target>1.1。", "终止测试进程，保存日志。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-NO-PREMATURE-BLACK-FRAME", "首个 Metal 帧实际呈现前必须由 SDR proxy 覆盖且 layer opacity=0。", "功能正确性", "system", "端到端验证 JPEG 黑块与 DNG 未完成 RAW 首帧修复。", "真实 Cocoa Window 与 telemetry 可用。", "JPEG/DNG 共 6 次真实启动记录。", "检查所有 presented=false 记录的 fallback/opacity，并检查 opacity>0 的呈现、完整 prepared geometry 与 drawable。", "空或未完成求值的 drawable 永不裸露；Metal 只在完整预热帧呈现后显示。", "关闭测试窗口。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-RAW-CONTENT-HEADROOM", "DNG 完成激活后的渲染目标必须等于实测 RAW content headroom 与显示可用 headroom 的较小值；支持 CALayer 内容标签的系统还必须写入同值。", "功能正确性", "system", "端到端验证内容范围与显示能力没有混淆。", "XDR Mac、DNG 和 content/display telemetry 可用。", "三次 DNG 完成 transition 的 telemetry。", "筛选 progress≥0.999，比较 content、target、display potential/rendering；若 runtime 支持 contentsHeadroom，再比较 layer 实值。", "content>1.5；target≈min(content,rendering)；content 不被潜在 16× 能力替代；可用时 layer≈target。", "关闭测试窗口并保留 API 支持状态和数值。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-FINAL-LAYOUT-BEFORE-METAL", "任何 Metal 提交都必须使用稳定的完整 viewport 几何和匹配的 drawable。", "功能正确性", "system", "端到端验证 DNG 完整→局部空白→完整故障修复。", "真实 Cocoa Window、DNG 与 screencapture 可用。", "三次 DNG telemetry，以及 clean-start 后 3.6～6.2s 的 10 帧截图。", "验证 layout_ready、geometry_pending=false、drawable match；在图像 crop 内计算各帧边缘向量与最终帧余弦相似度。", "提交只发生在稳定 geometry 且 drawable 完整匹配；10 帧结构相似度均≥0.90。", "保留截图、哈希和结构指标并关闭窗口。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-RAW-NO-BLANK-REGION", "DNG 从首个可见 Metal 帧到稳定 HDR 的十个时间点均不得丢失有结构的图像 tile。", "功能正确性", "system", "以屏幕像素捕获间歇性白色/空白区域。", "DNG clean-start、screencapture 和 Pillow 可用。", "3.6～6.2s 十帧的图像区域与最终参考帧。", "把图像分为 8×6 tiles，比较 FIND_EDGES 能量，标记参考有结构但当前低于 35% 的 tile。", "最终参考至少 20 个结构 tile；每一帧 missing tile count=0。", "保留十帧、哈希、crop 和逐 tile 能量。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-FLOAT-COLORMANAGED-EDR-SURFACE", "端到端 surface 必须持续报告 RGBA16Float/ColorSync/EDR。", "功能正确性", "system", "验证实际窗口 layer 配置。", "两种 HDR 样例可渲染。", "全部真实运行 telemetry。", "聚合每帧配置 flag。", "所有记录均为 float、扩展 P3、ColorSync、wantsEDR。", "销毁窗口 layer。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-SMOOTH-ACTIVATION", "打开 HDR 图后的内容强度必须平滑单调到 1。", "功能正确性", "system", "验证类似 Quick Look 的渐亮激活。", "定时器和 telemetry 可用。", "每次启动的 transition_progress。", "比较首帧、末帧与所有相邻采样。", "首帧≤0.1、末帧≥0.999、全程单调。", "定时器在约 1.8s 后停止。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-WINDOWSERVER-HEADROOM", "EDR layer 必须与 WindowServer 协商出可用 headroom>1。", "功能正确性", "system", "验证物理 XDR 输出而非 SDR bitmap。", "内建 XDR 显示器可用。", "真实运行的 current/potential/rendering/target headroom。", "读取每帧 NSScreen telemetry 并比较能力与渲染上限。", "potential>1、rendering>1，target 不超过 rendering；系统可随后提升 current。", "系统自行恢复亮度策略。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-EDR-BOOTSTRAP", "clean-start current headroom=1 时 JPEG 与 DNG 均必须在潜在 EDR 显示器上达到 target>1。", "功能正确性", "system", "端到端验证无其他 HDR 客户端时仍有真实 HDR。", "XDR 显示器 potential>1，current-only override 可用。", "JPEG/DNG + FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM=1。", "分别打开真实窗口并采集 bootstrap、rendering 和 target telemetry。", "两格式 bootstrapping_edr=true、rendering>1、target>1.1 且最终 prepared geometry active。", "清除子进程 override。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-FORCED-SDR-COMPATIBILITY", "强制显示 headroom=1 时真实 renderer target 必须保持 1。", "功能正确性", "system", "端到端验证 SDR 自动兼容。", "测试 override 可用。", "JPEG + FOVELLE_TEST_DISPLAY_HEADROOM=1。", "打开真实窗口并采集 telemetry。", "override=true 且所有 current/target 均为 1。", "清除子进程环境变量。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-JPEG-BAND-FREE", "JPEG 稳态和缩放/拖动序列中不得出现贯穿图像的大块纯黑竖带或拖动后残影。", "功能正确性", "system", "以屏幕像素证据验证非易失源解码和 managed-intermediate 修复。", "允许 screencapture，Pillow 可读 PNG。", "JPEG 稳态及确定性交互期间的屏幕截图。", "采集多帧，计算中间 86% 高度内近黑像素占比≥80%的最大连续列；比较交互稳定后的最后两帧边缘结构。", "每帧最大连续黑列≤10；最后两帧边缘余弦相似度≥0.995，证明没有旧 tile/残影。", "保留截图、哈希、黑列指标和结构相似度。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-INTERACTION-GEOMETRY", "HDR 完成激活后的缩放/拖动必须复用同一 prepared generation，保持 layer 可见，并只以新四角提交完整 drawable。", "功能正确性", "system", "端到端验证拖动残影和交互中错误 proxy 切换修复。", "真实 Cocoa Window 与确定性交互 driver 可用。", "JPEG 的 zoom×4 与 12 步双轴 scrollbar 移动。", "采集 geometry-reused、render、finished telemetry。", "reuse 期间 presented/prepared=true、fallback=false、opacity=1、generation/reset 不变；render 无 pending geometry，交互后 HDR 完整。", "driver 自动停止且窗口关闭。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-INTERACTION-NO-REACTIVATION", "JPEG 完成首次渐亮后，缩放或移动不得让 transition progress 回落或再次显示 SDR fallback。", "功能正确性", "system", "端到端验证亮度只在图片打开时提高。", "JPEG 已在真实 EDR 窗口完成激活。", "zoom×4、12 步 pan 及交互后 render telemetry。", "检查 geometry-reused 和 finished 后记录中的 activation/progress/fallback/opacity。", "所有 reuse 与交互后 progress≥0.999、activation=true、fallback=false、opacity=1。", "关闭测试窗口。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-THEME-BACKGROUND-STABILITY", "Light Theme 打开 HDR 图片并揭示 Metal 后，视口背景必须保持 #969696，不得约两秒后变浅。", "功能正确性", "system", "以真实屏幕像素验证 Qt→Metal reveal 的背景一致性。", "Light Theme、JPEG、真实 Cocoa Window 与 screencapture 可用。", "主题切换 driver 的 2.4s 前置截图与 pre-dark telemetry。", "在 viewport 外露且避开图像的区域采样中位 RGB，并核对 renderer 诊断。", "telemetry 始终为 (150,150,150)，截图每通道与 150 差≤8。", "保留截图及采样坐标；不修改持久化设置。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-THEME-BACKGROUND-SWITCH", "HDR layer 已显示时切换 Dark Theme，背景必须更新为 #212121。", "功能正确性", "system", "验证原生 layer 不会冻结 AppKit 动态色或忽略应用主题。", "同一 JPEG 窗口和确定性非持久化主题 driver 可用。", "postLoad 后 3.0s 切 Dark；按进程启动时间 4.5s 与 5.2s 截图以覆盖启动/解码抖动。", "检查 test-theme-dark 后 renderer RGB，并在同一外露背景区域取样。", "所有 post-dark telemetry=(33,33,33)，两帧截图每通道与 33 差≤8。", "关闭窗口；不改变 QSettings。", "tests/hdr_quality_system.py"),
    case("SYS-HDR-TIME-BEHAVIOR", "规定 48MP 工作负载的平均/P99/最大/吞吐量必须达标。", "时间行为", "system", "量化解码、稳态提交与过渡连续性。", "M3 Pro/XDR、两样例、各 3 次。", "6 个 decode 样本；排除每次前 3 帧后的 render 样本；激活阶段 progress 差分。", "计算 average/P99/max、两类 throughput、最低帧率与最大 progress 步长。", "所有 11 个时间断言通过，原始样本写入 JSON。", "进程全部终止。", "tests/hdr_quality_system.py"),
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
