#!/usr/bin/env python3
"""Emit and validate the auditable atomic acceptance-test specification."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from quality_unit_runner import EXPECTED_CASES


REQUIRED_FIELDS = (
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
    "evidence_refs",
)


def case(
    identifier: str,
    criterion: str,
    purpose: str,
    preconditions: str,
    input_data: str,
    steps: str,
    expected: str,
    postconditions: str,
    layer: str,
    implementation: str,
    evidence: tuple[str, ...],
) -> dict:
    return {
        "id": identifier,
        "acceptance_criterion": criterion,
        "test_purpose": purpose,
        "preconditions": preconditions,
        "input_data": input_data,
        "steps": steps,
        "expected_result": expected,
        "postconditions": postconditions,
        "test_layer": layer,
        "implementation": implementation,
        "evidence_refs": list(evidence),
    }


FUNCTIONAL_CASES = (
    case("TC-IMG-WEBP", "可加载 WebP 并得到有效图像", "验证 WebP 主解码与 ImageIO fallback 契约。", "Qt Test 应用已启动；存在有效 WebP fixture。", "TINY_WEBP。", "执行 ImageLoaderTests::testImageLoaderLoadsWebpWithImageIOFallback。", "加载成功、图像非空且测试无警告。", "临时图像对象释放；无崩溃。", "unit", "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderLoadsWebpWithImageIOFallback", ("unit.json", "static.json")),
    case("TC-IMG-AVIF", "可加载 AVIF 并得到有效图像", "验证 AVIF 在当前 macOS 解码路径下可读。", "Qt Test 应用已启动；存在有效 AVIF fixture。", "TINY_AVIF。", "执行 ImageLoaderTests::testImageLoaderLoadsAvifWithImageIOFallback。", "加载成功、图像非空且测试无警告。", "解码资源可回收；无崩溃。", "unit", "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderLoadsAvifWithImageIOFallback", ("unit.json", "system_probe.json")),
    case("TC-ORI-WEBP", "WebP EXIF 方向被正确应用", "验证方向元数据不会被忽略或重复变换。", "Qt Test 应用已启动；存在带方向元数据的 WebP fixture。", "ORIENTED_WEBP。", "执行 ImageLoaderTests::testImageLoaderAppliesWebpOrientation。", "像素尺寸/方向与预期一致。", "测试结束后 fixture 和图像对象释放。", "unit", "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderAppliesWebpOrientation", ("unit.json", "system_probe.json")),
    case("TC-ORI-AVIF", "AVIF EXIF 方向被正确应用", "验证 AVIF 方向转换链路。", "Qt Test 应用已启动；存在带方向元数据的 AVIF fixture。", "ORIENTED_AVIF。", "执行 ImageLoaderTests::testImageLoaderAppliesAvifOrientation。", "像素尺寸/方向与预期一致。", "测试结束后无残留窗口或解码任务。", "unit", "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderAppliesAvifOrientation", ("unit.json", "system_probe.json")),
    case("TC-IMG-TIFF", "可加载 TIFF 并得到有效图像", "验证 TIFF 通过 Image I/O 识别、元数据读取和 RGB 输出。", "macOS Image I/O 支持 public.tiff；Qt Test 临时目录可写。", "确定性 4x3 TIFF。", "执行 ImageLoaderTests::testImageLoaderLoadsTiffWithImageIO。", "UTI 为 public.tiff，图像尺寸为 4x3，异步加载无错误。", "临时 TIFF 与 native 资源释放。", "unit", "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderLoadsTiffWithImageIO", ("unit.json", "static.json", "integration.json")),
    case("TC-RAW-TYPE-DETECTION", "RAW 是否成立由 Image I/O 实际 UTI 决定", "验证伪装为 .nef 的 TIFF 不会触发扩展名 RAW 分支。", "Image I/O 支持 TIFF；临时目录可写。", "有效 TIFF 内容复制为 .nef。", "执行 ImageLoaderTests::testImageIOUsesContentTypeInsteadOfFilenameExtension。", "实际 UTI 为 public.tiff、isRaw=false 且仍成功渲染。", "临时文件和图像资源释放。", "unit", "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageIOUsesContentTypeInsteadOfFilenameExtension", ("unit.json", "static.json")),
    case("TC-IMG-FULL-RES", "放大查看时保留原图像素分辨率", "验证 Image I/O 不会把屏幕尺寸缩略图作为后续缩放的源图。", "Image I/O 支持 PNG；临时目录可写；测试图像 2400x1600 大于生产加载器的 1920px 默认提示。", "2400x1600 一像素黑白棋盘格 PNG。", "执行 ImageLoaderTests::testImageLoaderPreservesSourceResolutionForZoom，分别检查 native bridge 和异步 QVImageLoader 输出。", "intrinsicSize 与 decoded image size 均为 2400x1600，交替源像素仍可区分；不存在加载错误。", "临时源文件、native 图像和 loader 资源释放。", "unit", "tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderPreservesSourceResolutionForZoom", ("unit.json", "static.json", "integration.json", "system_probe.json")),
    case("TC-WIN-ICON", "图像窗口不设置应用级窗口图标", "验证窗口图标清理要求。", "MainWindow 可构造；应用资源已加载。", "构造窗口并检查 windowIcon。", "执行 FeatureTests::testWindowIconIsCleared。", "窗口图标为空，bundle 资源仍可用。", "窗口销毁；全局应用图标不被修改。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testWindowIconIsCleared", ("unit.json", "static.json")),
    case("TC-TITLEBAR-DOCUMENT-ICON", "标题栏文档代理图标被清除", "验证 native document path 不携带 Finder 文档图标。", "MainWindow 和 Cocoa bridge 可用。", "加载文件后检查 document proxy。", "执行 FeatureTests::testTitlebarDocumentProxyIsClearedForLoadedFile。", "native file path 被清为空，应用 bundle 身份保持。", "关闭窗口并清除测试文件状态。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testTitlebarDocumentProxyIsClearedForLoadedFile", ("unit.json", "integration.json")),
    case("TC-TITLEBAR-IDEMPOTENCE", "标题栏图标清理可重复调用", "验证清理逻辑幂等。", "窗口已加载文件；Cocoa window handle 可用。", "连续调用清理逻辑两次。", "执行 FeatureTests::testTitlebarIconClearingIsIdempotent。", "两次调用均成功且结果相同。", "无重复 native state 或崩溃。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testTitlebarIconClearingIsIdempotent", ("unit.json", "static.json")),
    case("TC-FMT-SETTINGS", "设置页格式列表包含 native image formats", "验证格式注册表和 Settings UI 一致。", "Settings 对话框可构造；native extensions 已注册。", "读取 getAllFileExtensionList。", "执行 FeatureTests::testSettingsFormatsIncludeNativeImageFormats。", "WebP/AVIF 扩展出现在设置列表且无重复破坏。", "设置对象按测试生命周期销毁。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testSettingsFormatsIncludeNativeImageFormats", ("unit.json", "integration.json")),
    case("TC-FMT-TIFF-RAW", "Settings → Formats 展示 TIFF 和系统当前可用 RAW 类型", "验证设置页直接消费 Image I/O/UTI 动态注册表。", "QVApplication 已初始化；Settings 对话框可构造。", "CGImageSource 支持类型及其 UTI 文件标签。", "执行 FeatureTests::testSettingsFormatsIncludeTiffAndSystemRawFormats。", "至少包含 .tif/.tiff；所有系统动态扩展均同步到应用和表格。", "不写入用户设置；对话框销毁。", "unit/integration", "tests/tst_qviewtests.cpp::FeatureTests::testSettingsFormatsIncludeTiffAndSystemRawFormats", ("unit.json", "integration.json")),
    case("TC-IMG-SMALL-SETTING", "小图 1:1 设置可暴露并持久化", "验证新增设置的 UI/存储契约。", "Settings manager 与 Options dialog 已初始化。", "读取并修改 smallimageoneone。", "执行 FeatureTests::testSmallImageOneToOneSettingIsExposedInImageOptions。", "复选框、默认值和设置键一致。", "恢复测试设置并销毁对话框。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testSmallImageOneToOneSettingIsExposedInImageOptions", ("unit.json", "system_feature.json")),
    case("TC-ISSUE-864-OPENWITH-TEARDOWN", "Open With worker teardown 不产生异步崩溃", "验证 Issue #864 的生命周期不变量。", "Open With worker 可创建；事件循环可运行。", "启动/停止刷新并销毁窗口。", "执行 FeatureTests::testOpenWithWorkerTeardownContract。", "timer 被停止、future 被等待、无 SIGABRT/QPixmap teardown 错误。", "后台 worker 和 watcher 均完成或安全释放。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testOpenWithWorkerTeardownContract", ("unit.json", "system_feature.json", "integration.json")),
    case("TC-APP-VERSION", "应用版本与构建版本一致", "验证发布版本契约。", "CMake/qmake 版本定义可读；应用已初始化。", "读取 VERSION/VERSION_STRING 和运行时版本。", "执行 FeatureTests::testApplicationVersionIsCurrent。", "所有版本来源为 0.1.3 且一致。", "不修改版本配置。", "unit", "tests/tst_qviewtests.cpp::FeatureTests::testApplicationVersionIsCurrent", ("unit.json", "static.json")),
    case("TC-ZOOM-MOUSE", "鼠标滚轮每次使用一个离散缩放步长", "验证传统滚轮输入不会依赖高分辨率 delta。", "GraphicsView 已有图像；zoom settings 已配置。", "发送一个正向和一个反向离散 wheel event。", "执行 GraphicsViewTests::testMouseWheelUsesOneDiscreteStep。", "每个事件恰好改变一个配置缩放级别。", "恢复原缩放和滚动位置。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testMouseWheelUsesOneDiscreteStep", ("unit.json", "static.json")),
    case("TC-ZOOM-TOUCHPAD", "触控板滚轮支持按 pixel delta 的分数缩放", "验证分数 wheel step 计算。", "GraphicsView 已有图像；输入设备类型为 TouchPad。", "发送带 phase/pixelDelta 的 touchpad wheel event。", "执行 GraphicsViewTests::testTouchpadWheelCanUseFractionalSteps。", "缩放因子按 qPow 合同连续变化，未误走离散路径。", "恢复视图状态。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testTouchpadWheelCanUseFractionalSteps", ("unit.json", "static.json")),
    case("TC-IMAGE-CENTER-WITH-SCROLLBARS", "存在溢出时图像保持视口中心", "验证滚动条占用空间后的中心计算。", "GraphicsView 加载大图；滚动条策略为 AsNeeded。", "打开大图并读取 scene/item/view geometry。", "执行 GraphicsViewTests::testImageIsCenteredAfterOpeningWithScrollBars。", "图像中心与可用 viewport 中心误差在断言容差内。", "视图与临时图像释放。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testImageIsCenteredAfterOpeningWithScrollBars", ("unit.json", "system_feature.json")),
    case("TC-WHEEL-ZOOM-SCROLLBAR-REGRESSION", "滚动条存在时 touchpad 缩放仍遵循设置", "锁定 scrollbar/zoom 回归行为。", "大图已加载；滚动条可见；触控板事件可注入。", "发送配置 wheel zoom 的 touchpad delta。", "执行 GraphicsViewTests::testTouchpadWheelRespectsConfiguredZoomWithScrollBars。", "缩放和滚动条范围均符合配置，不发生错误跳变。", "恢复 zoom mode 与 scrollbars。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testTouchpadWheelRespectsConfiguredZoomWithScrollBars", ("unit.json", "system_feature.json")),
    case("TC-LAYOUT-OPEN-FIT", "打开大图 fit 后不会因昂贵缩放重新出现滚动条", "验证 scene rect 更新具有稳定收敛性。", "GraphicsView 已加载大图；macOS unobscured viewport 可测量。", "打开图像并触发 zoom-to-fit/昂贵缩放路径。", "执行 GraphicsViewTests::testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling。", "fit 后两轴无不必要 overflow，视口中心稳定。", "测试结束不留下递归事件或悬挂进程。", "unit/system", "tests/tst_qviewtests.cpp::GraphicsViewTests::testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling", ("unit.json", "system_feature.json", "system_layout.json")),
    case("TC-LAYOUT-ROTATED-FIT", "旋转图像 fit 使用标题栏下方可用视口", "验证旋转场景没有底部空白条。", "Cocoa app 可启动；有效 AVIF 可加载；诊断日志开启。", "旋转/fit 图像并读取 FOVELLE_VIEW telemetry。", "执行 GraphicsViewTests::testRotatedZoomToFitUsesUnobscuredViewport，并运行 quality_layout_system.py。", "图像上下边界落在 usable viewport 内，底部 gap ≤2 px，连续事件几何值稳定。", "进程收到 SIGTERM 后退出；日志和临时 fixture 可回收。", "unit/system", "tests/tst_qviewtests.cpp::GraphicsViewTests::testRotatedZoomToFitUsesUnobscuredViewport; tests/quality_layout_system.py", ("unit.json", "system_feature.json", "system_layout.json")),
    case("TC-LAYOUT-ZOOM-SCROLLBAR-THRESHOLD", "跨越滚动条阈值时视口中心不漂移", "验证自动滚动条切换不会破坏中心。", "大图已加载；zoom 可在阈值两侧变化。", "连续应用接近阈值的 zoom levels。", "执行 GraphicsViewTests::testZoomAcrossScrollbarThresholdKeepsViewportCenterStable。", "滚动条状态可变但 viewport 中心连续且 scene rect 更新收敛。", "恢复原始缩放和滚动位置。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomAcrossScrollbarThresholdKeepsViewportCenterStable", ("unit.json", "system_feature.json")),
    case("TC-GESTURE-TOUCHPAD-PAN", "未修饰 touchpad stream 用像素平移且不改 zoom", "验证 pan/zoom 输入分流。", "GraphicsView 已加载图像；输入事件带 TouchPad 与 NoScrollPhase。", "发送 pixelDelta 触控板滚动。", "执行 GraphicsViewTests::testTouchpadPanUsesPixelsWithoutChangingZoom。", "viewport 位移与 pixel delta 一致；zoom 不变。", "恢复滚动条值。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testTouchpadPanUsesPixelsWithoutChangingZoom", ("unit.json", "system_feature.json")),
    case("TC-ZOOM-FULLSCREEN", "fit zoom 经反向 wheel 和 fullscreen resize 后仍保持 fit 意图", "验证 calculated zoom mode 的 resize continuity。", "窗口与图像已打开；fullscreen shortcut 可用。", "执行反向 wheel、切换 fullscreen、触发 resize。", "执行 GraphicsViewTests::testFitZoomSurvivesInverseWheelStepsAndFullscreenResize。", "fit/manual 状态按设计恢复，图像不出现异常滚动条。", "退出 fullscreen 并恢复设置。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testFitZoomSurvivesInverseWheelStepsAndFullscreenResize", ("unit.json", "system_feature.json")),
    case("TC-ZOOM-MANUAL-RESIZE", "手动 zoom 在 resize 后保持手动模式", "防止 resize 错误覆盖用户明确缩放。", "GraphicsView 已加载图像并进入 manual zoom。", "设置非 fit zoom 后 resize 窗口。", "执行 GraphicsViewTests::testManualZoomRemainsManualAcrossResize。", "zoom level 不被重算为 fit；viewport 仍可用。", "恢复 calculated zoom mode。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testManualZoomRemainsManualAcrossResize", ("unit.json", "system_feature.json")),
    case("TC-IMG-SMALL-POLICY", "小图 1:1 策略受 viewport 和 WindowResizeMode 共同约束", "验证小图显示策略边界。", "设置 manager 可控；窗口 resize mode 可切换。", "输入小图尺寸、可用 viewport 和 Never/其他 window mode。", "执行 GraphicsViewTests::testSmallImageOneToOnePolicyUsesViewportAndWindowMode。", "仅在策略允许且图像适配时使用 1:1。", "恢复设置/窗口模式。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testSmallImageOneToOnePolicyUsesViewportAndWindowMode", ("unit.json", "system_feature.json")),
    case("TC-IMG-SMALL-OPEN-BROWSE", "打开和浏览图片时小图 1:1 设置均生效", "验证设置不只在单次打开路径生效。", "设置启用；文件序列包含小图；导航可用。", "打开首图并前后浏览。", "执行 GraphicsViewTests::testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages。", "每次 load/browse 后策略一致且图像未被非预期缩放。", "清理文件序列和设置。", "unit/system", "tests/tst_qviewtests.cpp::GraphicsViewTests::testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages", ("unit.json", "system_feature.json")),
    case("TC-GESTURE-NATIVE-ZOOM", "原生 pinch zoom 在手势位置改变缩放", "验证 macOS QNativeGesture zoom 路径。", "GraphicsView 已加载图像；native event 可构造。", "发送 Qt::ZoomNativeGesture 与局部坐标。", "执行 GraphicsViewTests::testNativePinchZoomChangesScaleAtGesturePosition。", "缩放值改变且手势位置保持锚定。", "恢复 zoom 和中心。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testNativePinchZoomChangesScaleAtGesturePosition", ("unit.json", "system_feature.json")),
    case("TC-GESTURE-NATIVE-PAN", "原生 pan 改变 viewport", "验证 Qt::PanNativeGesture 路径。", "GraphicsView 已加载可滚动大图。", "发送 native pan delta。", "执行 GraphicsViewTests::testNativePanChangesViewport。", "viewport/scrollbar 值按 delta 改变且不改变 zoom。", "恢复滚动位置。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testNativePanChangesViewport", ("unit.json", "system_feature.json")),
    case("TC-SCROLLBAR-AXES", "滚动条只在对应 overflow 轴出现", "验证两轴 overflow contract。", "GraphicsView 可加载不同宽高比图像。", "分别输入宽溢出、高溢出和双向溢出图像。", "执行 GraphicsViewTests::testScrollBarsFollowImageOverflowAxes。", "horizontal/vertical visibility 与实际 overflow 一一对应。", "隐藏/恢复滚动条状态。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarsFollowImageOverflowAxes", ("unit.json", "system_feature.json")),
    case("TC-SCROLLBAR-THEME", "滚动条样式匹配主题", "验证 light/dark stylesheet contract。", "主题可切换；scrollbars 已创建。", "切换 Light、Dark 并读取 stylesheet。", "执行 GraphicsViewTests::testScrollBarsMatchTheme。", "handle/add-page 等颜色与主题一致。", "恢复原主题。", "unit", "tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarsMatchTheme", ("unit.json", "system_feature.json")),
    case("TC-GESTURE-PERF", "native gesture 响应满足平均/P99/最大/吞吐观测合同", "测量目标工作负载下 gesture 处理时间。", "GraphicsView 已加载图像；高分辨率计时器可用。", "发送 240 个 native zoom/pan gesture events。", "执行 GraphicsViewTests::testNativeGestureResponsePerformance 并解析 GESTURE_PERF。", "输出 average_ms、p99_ms、max_ms、throughput_events_per_second、count 且 count=240；指标不为空并满足本项目阈值。", "事件队列清空；无悬挂测试进程。", "unit/system", "tests/tst_qviewtests.cpp::GraphicsViewTests::testNativeGestureResponsePerformance; tests/quality_feature_system.py", ("unit.json", "system_feature.json")),
    case("TC-APNG-PLAY", "APNG 播放超过首帧并遵循 timing/loop contract", "验证 ImageIO native animation 与 QVMovie 集成。", "有效 APNG fixture；事件循环和 timer 可用。", "播放 APNG 并采样多帧。", "执行 ImageCoreAndMovieTests::testAnimatedPngPlaysBeyondFirstFrame。", "帧内容/时间推进超过第一帧，未报错。", "停止 movie timer 并释放帧缓存。", "unit/system", "tests/tst_qviewtests.cpp::ImageCoreAndMovieTests::testAnimatedPngPlaysBeyondFirstFrame", ("unit.json", "system_feature.json")),
    case("TC-FS-DEFAULT", "全屏默认快捷键为 Enter 且可配置", "验证 QAction 所有默认快捷键。", "ShortcutManager 和 MainWindow 已初始化。", "读取默认 shortcut 并设置配置值。", "执行 WindowBehaviorTests::testFullscreenDefaultShortcutIsEnterAndConfigurable。", "默认值为 Enter，配置后按配置值生效。", "恢复默认 shortcut。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testFullscreenDefaultShortcutIsEnterAndConfigurable", ("unit.json", "static.json")),
    case("TC-FS-NO-BYPASS", "清空 fullscreen shortcut 后 Enter 不绕过配置", "防止 MainWindow 硬编码 Enter bypass。", "fullscreen shortcut 已清空；窗口可接收 key event。", "发送 Enter key event。", "执行 WindowBehaviorTests::testEnterDoesNotBypassClearedFullscreenShortcut。", "窗口不进入 fullscreen。", "恢复 shortcut 和窗口状态。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testEnterDoesNotBypassClearedFullscreenShortcut", ("unit.json", "integration.json")),
    case("TC-FS-CONFIGURED", "配置的 fullscreen shortcut 仍能切换全屏", "验证 QAction 配置路径未被修复破坏。", "fullscreen shortcut 设置为测试按键。", "发送 configured shortcut。", "执行 WindowBehaviorTests::testConfiguredFullscreenShortcutStillWorks。", "窗口状态切换一次且不重复触发。", "退出 fullscreen。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testConfiguredFullscreenShortcutStillWorks", ("unit.json", "integration.json")),
    case("TC-TITLE-PRACTICAL", "实用标题格式包含文件名和序号", "验证标题显示策略。", "窗口已加载多图序列；title format 为 practical。", "读取标题文本。", "执行 WindowBehaviorTests::testPracticalTitlebarTextUsesFilenameAndSequence。", "标题包含 filename 和 sequence，格式无多余尺寸字段。", "关闭窗口并清除 sequence。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testPracticalTitlebarTextUsesFilenameAndSequence", ("unit.json", "integration.json")),
    case("TC-TITLE-DEFAULT", "默认标题格式为 practical", "验证默认设置。", "新建 SettingsManager/Window。", "读取默认 titleFormat。", "执行 WindowBehaviorTests::testDefaultTitlebarTextIsPractical。", "默认值为 practical。", "不修改持久化配置。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testDefaultTitlebarTextIsPractical", ("unit.json", "integration.json")),
    case("TC-TITLE-VERBOSE", "verbose 标题包含请求的字段", "验证可选详细标题格式。", "窗口加载图像；title format 为 verbose。", "读取标题文本及图像尺寸/索引。", "执行 WindowBehaviorTests::testVerboseTitlebarTextUsesAllRequestedFields。", "标题包含文件名、序号、尺寸等约定字段。", "恢复 practical 标题设置。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testVerboseTitlebarTextUsesAllRequestedFields", ("unit.json", "integration.json")),
    case("TC-THEME-SETTINGS", "Theme 设置替代已移除的颜色控件", "验证设置 UI 精简且可持久化。", "Options dialog UI 已生成；Settings manager 可用。", "枚举 theme 控件和旧控件名称。", "执行 WindowBehaviorTests::testThemeSettingsReplaceRemovedColorControls。", "存在 theme combo；旧 bg/dark-titlebar controls 不存在。", "关闭对话框并恢复设置。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testThemeSettingsReplaceRemovedColorControls", ("unit.json", "static.json")),
    case("TC-THEME-COLORS", "主题映射 native appearance 和 viewport 背景色", "验证 light/dark 视觉状态。", "Cocoa application 与窗口已初始化。", "切换 Light/Dark 并读取 appearance/background。", "执行 WindowBehaviorTests::testThemeAppliesNativeAppearanceAndViewportBackground。", "native appearance 与预定义背景色一致。", "恢复原主题。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testThemeAppliesNativeAppearanceAndViewportBackground", ("unit.json", "system_feature.json")),
    case("TC-THEME-CHECKERBOARD", "checkerboard 覆盖主题背景并可恢复", "验证 checkerboard 优先级。", "图像视图和 theme 已初始化。", "开启/关闭 checkerboard，切换主题。", "执行 WindowBehaviorTests::testCheckerboardOverridesThemeAndRestoresBackground。", "checkerboard 开启时覆盖主题；关闭后恢复主题色。", "恢复 theme/checkerboard 设置。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testCheckerboardOverridesThemeAndRestoresBackground", ("unit.json", "system_feature.json")),
    case("TC-NAV-EDGE", "导航按钮边缘激活区域排除标题栏", "验证 pointer hit-test 只覆盖内容区。", "窗口有前后图像；导航按钮已创建。", "在标题栏和内容边缘移动鼠标。", "执行 WindowBehaviorTests::testNavigationEdgeActivationExcludesTitlebar。", "标题栏不触发按钮，内容边缘按设计触发。", "按钮状态恢复隐藏/透明。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationEdgeActivationExcludesTitlebar", ("unit.json", "system_feature.json")),
    case("TC-NAV-SIZE", "导航按钮尺寸和显示无延迟满足 contract", "验证响应式尺寸。", "窗口尺寸可控；按钮已创建。", "在最小窗口和正常窗口尺寸下读取 geometry/opacity。", "执行 WindowBehaviorTests::testNavigationButtonSizingAndNoDelay。", "按钮不小于最小尺寸，显示不依赖延迟 timer。", "停止动画并释放 effect。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationButtonSizingAndNoDelay", ("unit.json", "system_feature.json")),
    case("TC-NAV-CONTRAST", "导航按钮使用实际内容对比度", "验证按钮前景/背景按采样内容选择。", "viewport 可 grab；图像包含明暗区域。", "分别在左右内容区域采样。", "执行 WindowBehaviorTests::testNavigationButtonsUseActualContentContrast。", "每侧按钮样式与底层内容对比度匹配。", "清除采样图像和按钮样式。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationButtonsUseActualContentContrast", ("unit.json", "system_feature.json")),
    case("TC-NAV-TRANSITION", "导航按钮淡入淡出动画可观测且有界", "验证 opacity transition。", "按钮使用 QGraphicsOpacityEffect/QPropertyAnimation。", "触发显示与隐藏并采样 opacity。", "执行 WindowBehaviorTests::testNavigationButtonsFadeTransition。", "opacity 单调趋向目标值，动画时长符合常量且最终完成。", "停止动画并恢复隐藏状态。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationButtonsFadeTransition", ("unit.json", "system_feature.json")),
    case("TC-NAV-CLICK", "导航按钮点击切换文件", "验证端到端导航副作用。", "至少两张可加载图像；前后按钮可见。", "点击 next/previous button。", "执行 WindowBehaviorTests::testNavigationButtonsClickSwitchesFiles。", "当前文件索引按方向改变，标题/图像同步更新。", "关闭窗口并删除临时序列。", "unit", "tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationButtonsClickSwitchesFiles", ("unit.json", "system_feature.json")),
)


QUALITY_CASES = (
    case("AC-LEAN-01", "需求范围内的行为、CI 修复和审计产物均有明确边界", "检查实现没有遗漏用户要求，也没有把无关功能纳入变更。", "仓库工作树可读；当前任务范围为源代码、测试、CI 与 reports。", "git diff --check、静态检查结果、文件清单。", "运行 quality_static.py 与 quality_integration.py，核对变更路径和检查 ID。", "所有任务范围检查通过；未发现越界文件；README 既有改动不被覆盖。", "保留用户已有 README 改动；reports 证据可追溯。", "static/integration", "tests/quality_static.py::ST-03/ST-25; tests/quality_integration.py", ("static.json", "integration.json")),
    case("AC-FUNC-01", "规定输入产生规定输出、副作用并保持不变量", "汇总 47 个功能原子验收点的确定性执行。", "可执行测试二进制已构建；Cocoa 环境变量已设置。", "6 个 Qt Test suite、47 个功能 test case。", "运行 quality_unit_runner.py，按 suite 和 case 解析 Qt Test 文本输出。", "47/47 case passed；6/6 suite passed；failed/skipped/blacklisted 均为 0。", "测试进程返回码为 0；输出被保存到 unit.json。", "unit", "tests/quality_unit_runner.py; tests/tst_qviewtests.cpp", ("unit.json", "static.json")),
    case("AC-IMG-CLARITY-01", "同一缩放比例下高分辨率图片不因解码降采样而失去细节", "验证根因修复覆盖 Image I/O 解码、异步加载和系统实际显示尺寸。", "macOS Image I/O、Metal/Qt 应用已构建；确定性 2400x1600 单像素棋盘格单元测试和 4000x2500 系统 fixture 可生成。", "静态源码契约、2400x1600 checkerboard、系统生成的 4000x2500 checkerboard、FOVELLE_VIEW itemRect。", "依次运行 ST-06/I-03、单元测试 TC-IMG-FULL-RES，并运行 quality_system_probe.py 的默认高分辨率 case。", "native 解码使用 fullResolutionThumbnailOptions；loader 保留 2400x1600；系统 telemetry 观察到至少 4000x2500 的 itemRect；后续 Qt Smooth/Expensive scaling 以完整源图为输入。", "不改变用户设置；证据保存源尺寸、itemRect、平均/P99/最大响应时间、吞吐量和主机观测限制。", "static/unit/integration/system", "tests/quality_static.py::ST-06; tests/quality_integration.py::I-03; tests/tst_qviewtests.cpp::ImageLoaderTests::testImageLoaderPreservesSourceResolutionForZoom; tests/quality_system_probe.py", ("static.json", "unit.json", "integration.json", "system_probe.json")),
    case("AC-RAW-PIPELINE-01", "RAW 使用 Image I/O、Core Image RAW、ColorSync 和 Metal/CIContext 的系统管线", "验证 RAW 文件识别不依赖扩展名，并且解码、色彩管理、GPU 渲染和预览降级均有原生 API 契约。", "macOS SDK 提供 Image I/O、Core Image、ColorSync、Metal；native bridge 和 ImageLoader 源码可读；当前验收环境提供真实 sample1.nef。", "CGImageSource UTI、CIRAWFilter outputImage/previewImage、ColorSync sRGB profile、Metal-backed CIContext，以及 /Users/inostarlin/Downloads/sample1.nef。", "运行 quality_static.py 的 ST-06、quality_integration.py 的 I-03，并使用 quality_system_probe.py 加载 sample1.nef 检查诊断 contentRect。", "静态证据同时包含 CGImageSourceGetType、filterWithImageData、previewImage、ColorSync profile、working/output color space、MTLCreateSystemDefaultDevice/contextWithMTLDevice；RAW 分支不读取文件扩展名；sample1.nef 产生非零 RGB 内容几何，无法解码时保留预览或返回可见错误。", "不改变用户设置；证据文件保留每个 marker、运行指标和样本路径；未覆盖的相机型号仍显式记录为不确定性。", "static/integration/system", "tests/quality_static.py::ST-06; src/qvcocoafunctions.mm; src/qvimageloader.cpp; tests/quality_system_probe.py", ("static.json", "integration.json", "system_raw_probe.json")),
    case("AC-TIME-01", "目标工作负载的平均、P99、最大响应时间和吞吐量满足测试合同", "测量 native gesture 及应用启动/加载工作负载的时间行为。", "macOS Cocoa app 已构建；计时器、ps/lsof 可用；目标阈值来自 quality_system_probe.py。", "240 gesture events；5 类 native/oriented/high-resolution image case，每类 3 次启动。", "运行 quality_feature_system.py 和 quality_system_probe.py，解析 average/p99/max/throughput。", "gesture 计时字段齐全且 count=240；system probe 的 startup average/p99/max、throughput、资源上限全部 pass。", "所有被测进程按协议退出；原始观测和阈值保存在 JSON。", "system/performance", "tests/quality_feature_system.py; tests/quality_system_probe.py", ("system_feature.json", "system_probe.json")),
    case("AC-TIME-02", "异常慢测试不会无限占用 CI 资源", "验证 Qt function、CTest、workflow job 三层时间边界。", "CMakeLists 和 workflow 文件可读。", "QTEST_FUNCTION_TIMEOUT=30000、CTest TIMEOUT/--timeout=90、Actions timeout-minutes。", "运行 static check ST-25，并检查配置中的超时常量。", "所有时间边界存在且值符合 30s/90s/10min 等项目合同。", "未来挂死时产生可诊断失败而非无限等待。", "static/CI", "tests/quality_static.py::ST-25-CI-TEST-BOUNDS", ("static.json", "integration.json")),
    case("AC-TEST-01", "测试用例可系统化生成并覆盖实现中的每个功能 case", "验证规格和可执行测试清单一一对应。", "quality_unit_runner.EXPECTED_CASES 和规格脚本均可导入。", "47 个 case ID、suite、test method 映射。", "运行 quality_specification.py 的映射校验。", "每个 EXPECTED_CASES ID 恰好有一个完整规格条目，方法名和证据引用非空。", "输出 test-specification.json/markdown 可供审计。", "static/specification", "tests/quality_specification.py", ("test_specification.json", "static.json")),
    case("AC-TEST-02", "测试条件可确定性控制且跨主机缺失 fixture 时可重复", "验证环境变量、离线开关和内嵌 fixture fallback。", "构建目录存在；不依赖网络服务。", "QT_QPA_PLATFORM=cocoa、QT_FATAL_WARNINGS=1、QV_DISABLE_ONLINE_VERSION_CHECK=ON、embedded AVIF/WebP/APNG。", "运行 CTest、unit runner、system probe、layout probe。", "测试在本地重复通过；layout probe 在请求 AVIF 缺失时仍使用明确标注的内嵌 fixture。", "临时目录在进程结束后删除；不改变用户数据。", "unit/integration/system", "tests/CMakeLists.txt; tests/quality_layout_system.py; tests/quality_system_probe.py", ("unit.json", "integration.json", "system_layout.json", "system_probe.json")),
    case("AC-TEST-03", "运行时状态可非侵入式观测并可复核外部输出", "验证诊断 telemetry、资源采样和原始 stdout/stderr 均被保留。", "app 可启动；诊断环境变量和系统采样工具可用。", "FOVELLE_DIAGNOSTIC_LOG=1、FOVELLE_VIEW 行、ps/lsof/netstat/iostat。", "运行 layout/system probes 并检查 JSON 中 raw_output、observations、metrics、thresholds。", "输出包含 geometry、资源、网络/磁盘主机观测及其 limitations；无需修改业务状态。", "子进程收到 SIGTERM/退出，原始证据文件可通过 hash 复核。", "system/audit", "tests/quality_layout_system.py; tests/quality_system_probe.py", ("system_layout.json", "system_probe.json")),
    case("AC-TEST-04", "几何回归测试不依赖 Retina 专属显示比例", "验证同一测试在 Retina 与非 Retina Cocoa 条件下使用相同业务不变量。", "Cocoa 测试二进制已构建；可控制 QT_SCALE_FACTOR。", "默认显示比例和 QT_SCALE_FACTOR=1 两个运行条件。", "分别运行 quality_unit_runner.py 默认模式和 --scale-factor 1 模式，比较 GraphicsViewTests 及总套件结果。", "两种条件均为 71 passed、0 failed、0 skipped；断言只要求图像非空且不小于原始 1200px，不要求严格大于 1200px。", "两个测试进程均返回 0；证据保留各自 environment。", "unit/cross-platform", "tests/quality_unit_runner.py; tests/tst_qviewtests.cpp::GraphicsViewTests::testImageIsCenteredAfterOpeningWithScrollBars", ("unit.json", "unit_scale1.json", "static.json")),
    case("AC-CI-01", "根因修复覆盖 re-entrant scene rect 路径而非只延长等待", "验证修复直接针对失败调用链。", "QVGraphicsView 源代码可读；实现和当前集成证据可读取。", "resizeEvent/updateSceneRect/sceneRect 的控制流和 Qt 文档约束。", "运行静态 ST-25，执行所有 Qt tests，并核对 integration.json 中的控制流断言。", "存在 isUpdatingSceneRect guard、QScopedValueRollback、redundant setSceneRect skip；47 个 case 和 CTest 通过。", "未修改业务输入输出契约；实现证据和测试原始输出均保留。", "static/unit/integration", "src/qvgraphicsview.cpp; src/qvgraphicsview.h; tests/quality_static.py", ("static.json", "unit.json", "integration.json")),
    case("AC-CI-02", "修复后的 CI 执行路径具有可观测的有限失败语义", "验证 workflow/CTest 失败不会留下无界 orphan process。", "workflow 和 CTest 配置已更新。", "CTest 90s timeout、QTest 30s function timeout、Actions job timeout。", "运行 CTest 与静态配置检查，核对 return code、elapsed、timed_out 字段。", "正常运行返回 0；超时字段可表达 timed_out；配置层提供上限。", "证据 JSON 保留 elapsed/timeout/return_code。", "integration/CI", "tests/quality_unit_runner.py; tests/CMakeLists.txt; .github/workflows/*.yml", ("unit.json", "integration.json", "static.json")),
)


def git_head(repo: Path) -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, text=True, capture_output=True, check=False
    )
    return result.stdout.strip() if result.returncode == 0 else None


def markdown(record: dict) -> str:
    lines = [
        "# 测试规格说明书",
        "",
        f"- 规格版本：{record['specification_version']}",
        f"- 基线提交：`{record['head_sha']}`",
        f"- 生成时间（UTC）：{record['generated_at_utc']}",
        f"- 原子验收标准数：{len(record['cases'])}",
        "- 机器可读源：[reports/test_specification.json](reports/test_specification.json)",
        "",
        "本规格将用户提出的四项代码质量要求拆解为质量属性验收点，并将实现功能拆解为 47 个可执行 Qt Test case。每个条目均明确测试目的、前置条件、输入数据、操作步骤、预期结果和后置条件；`implementation` 指向实际测试代码或质量门禁。",
        "",
        "## 原子验收测试矩阵",
        "",
    ]
    for item in record["cases"]:
        lines.extend(
            [
                f"### {item['id']} — {item['acceptance_criterion']}",
                "",
                f"- 测试目的：{item['test_purpose']}",
                f"- 前置条件：{item['preconditions']}",
                f"- 输入数据：{item['input_data']}",
                f"- 操作步骤：{item['steps']}",
                f"- 预期结果：{item['expected_result']}",
                f"- 后置条件：{item['postconditions']}",
                f"- 测试层级：{item['test_layer']}",
                f"- 测试代码：`{item['implementation']}`",
                f"- 证据引用：{', '.join(f'`evidence/{ref}`' for ref in item['evidence_refs'])}",
                "",
            ]
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()

    cases = list(QUALITY_CASES + FUNCTIONAL_CASES)
    expected_ids = [identifier for identifier, _, _ in EXPECTED_CASES]
    functional_ids = [item["id"] for item in FUNCTIONAL_CASES]
    all_ids = [item["id"] for item in cases]
    errors: list[str] = []
    if len(all_ids) != len(set(all_ids)):
        errors.append("duplicate specification IDs")
    if set(expected_ids) != set(functional_ids):
        errors.append(f"functional IDs do not match EXPECTED_CASES: expected={expected_ids} actual={functional_ids}")
    for item in cases:
        missing = [field for field in REQUIRED_FIELDS if not item.get(field)]
        if missing:
            errors.append(f"{item['id']} missing fields: {missing}")

    record = {
        "kind": "atomic-test-specification",
        "specification_version": "1.0",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "repo": str(repo),
        "head_sha": git_head(repo),
        "source_contract": "tests/quality_unit_runner.py::EXPECTED_CASES",
        "functional_case_count": len(FUNCTIONAL_CASES),
        "quality_case_count": len(QUALITY_CASES),
        "case_count": len(cases),
        "cases": cases,
        "validation_errors": errors,
        "passed": not errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
    args.markdown_output.write_text(markdown(record), encoding="utf-8")
    print(json.dumps(record, ensure_ascii=False, indent=2))
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
