# 缩放初值与 SVG/EPS 拖动呈现：测试完成报告

日期：2026-09-27
结论：实现已修正；针对 SVG/EPS 闪烁的集成与真实鼠标系统门禁通过

## 完成内容

- 修正 [nativedialogs.cpp](/Users/inostarlin/code/Fovelle/src/nativedialogs.cpp)：先配置双精度输入控件的小数位数和上下限，再设置传入的当前缩放值，避免默认最大值 99.99 截断 100% 以上的初值。
- 修正 [tst_qviewtests.cpp](/Users/inostarlin/code/Fovelle/tests/tst_qviewtests.cpp)：新增 64 倍当前缩放的 UI 路径回归测试，并检查内部 double 值与实际 spin box 文本；拖动测试覆盖 40 步跨 tile 边界，比较最早、20 ms、40 ms 屏幕帧及后续静止帧。
- 修正 [qvgraphicsimageitem.cpp](/Users/inostarlin/code/Fovelle/src/qvgraphicsimageitem.cpp)：交互 tile 外扩从 16 调为 128 设备像素，并在可见区域距 tile 边界 64 设备像素时提前请求下一块；旧 tile 仍覆盖视口时继续显示旧矢量内容。
- 更新 [eps_quality_static.py](/Users/inostarlin/code/Fovelle/tests/eps_quality_static.py)：静态契约从硬编码旧 16 像素值改为检查新 overscan、预取阈值和覆盖检查。
- 加固 [native_drag_helper.mm](/Users/inostarlin/code/Fovelle/tests/native_drag_helper.mm) 与 [vector_drag_ghosting_pipeline.py](/Users/inostarlin/code/Fovelle/tests/vector_drag_ghosting_pipeline.py)：真实 HID 路径对宽幅 SVG 自动寻找纵向滚动范围，系统门禁逐条校验按住拖动时每个 paint 均为全视口更新。
- 保留并验证 [qvgraphicsview.cpp](/Users/inostarlin/code/Fovelle/src/qvgraphicsview.cpp) 与 [qvgraphicsview.h](/Users/inostarlin/code/Fovelle/src/qvgraphicsview.h) 中已完成的矢量鼠标平移状态修复：定时器在鼠标仍按住平移时不切回局部更新，释放/重置后恢复静态更新。
- 三份交付文档分别位于 [技术设计文档](technical_design_document.md)、[测试用例说明](test_case_specification.md) 与本报告。

## 复现和验证

- 配置与编译：`cmake -S . -B /tmp/fovelle-zoom-drag-build -DBUILD_TESTS=ON -DFOVELLE_BUILD_TRANSLATIONS=OFF -DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_BUILD_TYPE=Debug`；`cmake --build /tmp/fovelle-zoom-drag-build --target fovelle_tests -j4` 成功。
- 缩放红绿验证：生产修复前新增测试报告初值 100、预期 6400 并失败；修复后 `testZoomCustomDialogStartsAtCurrentLevel` 通过，`-repeat 3` 共 9 项（含初始化/清理）通过、0 失败。
- 负向/正向验证：旧 16 像素 overscan 版本的即时帧配准曾报告 6.2% 错配；单独增至 128 像素仍在 40 步拖动的 SVG 第 12 步报告 5.52%，而后续帧降到 0.27%。加入主动预取后，指定 SVG `-repeat 3` 共 9 项通过、0 失败；三次中最高重叠错配分别 EPS 0.017998、SVG 0.015838，最高静止帧变化 0.011097，均低于 0.05 项目阈值。
- 相邻回归组 `testVectorPanRepaintsOnlyExposedStrip` 与 `testVectorPaintClearsStalePixelsBeforeTileReady` 一并执行：该组连同拖动测试共 5 项通过、0 失败；EPS/SVG 哨兵残留像素均为 0。
- `tests/vector_drag_ghosting_pipeline.py --stage integration`：4/4 集成用例通过，包括 exposed-strip、拖动、矢量场景项和 120 Hz paint CPU 预算。
- 2026-09-27 最终全流程复核 `tests/vector_drag_ghosting_pipeline.py --stage all`：static 9/9、unit 2/2、integration 4/4、system 4/4 检查通过，阶段顺序为 static → unit → integration → system。
- 全流程中的真实 HID 系统阶段使用 `FOVELLE_SVG_SAMPLE=/Users/inostarlin/Downloads/wanimagazine_logo.svg`：EPS 与指定 SVG 均通过；native helper 对示例 SVG 发出 32 个按住拖动事件并确认实际纵向滚动、退出锚点稳定及恢复普通窗口几何。最新日志中 EPS/SVG 分别有 46/48 条 `mouse_pan_active=true` paint，逐条均为 `update_mode=full`、`dirty_ratio=1.0`。
- `python3 -m py_compile tests/vector_drag_ghosting_pipeline.py`、`git diff --check HEAD` 通过。
- GitHub Actions 曾因 SVG 集成截图 `nonblank=0` 失败。日志中的原始基线暗像素比例为 0.055684，而断言要求至少 0.10；本次校准为 0.01 并将实测最小比例加进失败遥测。修复后本机 Cocoa 拖动回归 `-repeat 3` 通过（9 passed，0 failed）；提交 `eed5cae` 的 [Checks](https://github.com/inostarlin-passion/Fovelle/actions/runs/36312464801)（含完整单元测试、clang-tidy、clang-format）和 [Build Fovelle](https://github.com/inostarlin-passion/Fovelle/actions/runs/36312464871) 均成功。
- 执行环境：macOS 27，Apple Silicon，Qt 6.11.2，Cocoa。

运行示例：

```bash
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 QTEST_FUNCTION_TIMEOUT=30000 \
  FOVELLE_TEST_SUITE=WindowBehaviorTests \
  /tmp/fovelle-zoom-drag-build/tests/fovelle_tests \
  -repeat 3 testZoomCustomDialogStartsAtCurrentLevel -silent

QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 QTEST_FUNCTION_TIMEOUT=30000 \
  FOVELLE_TEST_SUITE=GraphicsViewTests \
  FOVELLE_SVG_SAMPLE=/Users/inostarlin/Downloads/wanimagazine_logo.svg \
  /tmp/fovelle-zoom-drag-build/tests/fovelle_tests \
  -repeat 3 testVectorDragFrameBudgetForEPSAndSVG -silent
```

## 多源复核、逆向证伪与边界

Qt `QInputDialog` 文档把 `value` 定义为输入控件初值；Qt `QDoubleSpinBox` 文档给出默认最大值 99.99；Qt 6.11 源码显示值设置会立即写入 spin box。三项与修复前观察到的 100/6400 不匹配及修复后的匹配形成相互印证。对拖动更新，Qt `QGraphicsView` 文档、公开 `scrollContentsBy()` 实现、paint event region 文档与本地实际鼠标/截图测试共同支持状态与绘制更新的结论。参考链接见技术设计文档。

Qt `QGraphicsView` 官方文档与公开 `scrollContentsBy()` 源码说明更新模式和滚动优化的关系；Qt `QScreen::grabWindow()` 文档及官方截图示例说明屏幕像素采集 API；这些来源交叉支撑测试选择，但都不是对物理显示器刷新输出的测量。本机系统门禁使用真实鼠标事件，且逐条审计拖动中的 paint 日志；截图集成门禁另行检查重叠区域内容连续性。

逐帧比较发现早期帧会短暂呈现预览图。源代码检查确认在可见范围超出已有 tile 后才启动异步绘制，截图失真与这个 fallback 路径吻合。负向复核显示单独增加 overscan 仍在第 12 步超过 5%；主动预取后最早帧也参与断言，指定 SVG 40 步重复 3 次稳定通过。旧静态 EPS 质量测试也曾硬编码 16 像素，更新为检查 128 像素 overscan、64 像素预取门槛和 retained tile 覆盖检查后，静态阶段 9/9 通过。

当前证据证明：测试能够稳定拒绝已确认的旧计时器状态切换路径（停顿后不再全视口更新），并在指定 SVG、EPS fixture 和本机 Qt/Cocoa 环境通过；sample system run 日志也显示拖动期间每条被记录 paint 均全量覆盖。截图是离散采样，不能排除采样点之间更短的闪帧；paint 日志不能证明 compositor/GPU/显示器每次刷新均正确；5% 是项目门槛而非平台规范。未取得用户现场修复前录像或逐刷新捕获数据，因此无法独立确认该环境下每一次主观闪烁的根因，也不对所有设备、刷新率、驱动和矢量文件保证绝对无闪烁。这些证据缺口明确保留。

## 工作区校验

- 三份 Markdown 已写入指定路径。
- 测试和生产代码编译通过；本报告列出的回归命令执行通过。
- 最终检查：`git diff --check`。

# 图片序列边界提示与失焦图像保真：测试完成报告

日期：2026-09-29
结论：本次原子验收标准全部通过专项静态与动态测试；更广泛的仓库质量扫描有本任务范围外的失败项，详情见下。

## 完成内容

- 到达无上一张/下一张图片的序列边界时显示短暂提示：`No previous image` / `No next image`。正常导航、循环导航和幻灯片自动推进不误报。
- 提示依 Appearance 切换样式：Light 为深底白字，Dark 为浅底深字；底部居中，每次淡入/淡出 180 ms，完整显示 4000 ms。
- 增加简体中文、繁体中文、西班牙语和日语译文，并用同一翻译文本设置 accessible name 和发布 `QAccessible::Alert`。
- native HDR presentation 失焦时以打开时相同的 450 ms ease-in/ease-out opacity 动画渐淡至清晰 SDR 端点；native SDR 图片则保持其原生 CALayer 与不透明底色常驻，避免透明 PNG 露出后方窗口。
- 写入本次技术设计和完整测试用例；测试报告与前次结果一并保留在本文件。

## 专项验收结果

- 配置：`cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_BUILD_TRANSLATIONS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_BUILD_TYPE=Release` 成功。
- 构建：`cmake --build build --target fovelle_tests -j4` 成功；生成四种非英语 QM 翻译文件。
- 动态与静态专项套件：`ctest --test-dir build -R 'FovelleNavigationBoundaryHint' --output-on-failure`，2/2 通过。动态 QtTest 检查失败边界提示、正常/循环导航、Light/Dark 色彩、4 个 QM 运行时译文、计时，并通过 WindowServer screen capture 比较透明 PNG 视口失焦前后的实际像素；静态检查覆盖导航触发条件、完整翻译资源、可访问性事件、动画/位置、HDR overlay 与 native SDR 常驻契约。
- `python3 tests/navigation_boundary_hint_static.py`：5/5 原子标准通过。
- `python3 -m py_compile tests/navigation_boundary_hint_static.py tests/hdr_quality_static.py` 与 `git diff --check` 通过。
- `tests/hdr_quality_static.py` 中 `ST-HDR-FOCUS-PRESENTATION-TRANSITION` 通过，生产构建和 clang-tidy 均通过；此较大 HDR 套件总计 30/37，另有 7 项失败：`ST-HDR-NONRAW-METADATA`、`ST-HDR-NONRAW-NONVOLATILE-DECODE`、`ST-HDR-DNG-GAINMAP-ROI`、`ST-HDR-DISPLAYLINK-LATEST-ONLY`、`ST-HDR-PERSISTENT-COMPOSITOR-FAST-PATH`、`ST-HDR-THEME-BACKGROUND`、`ST-HDR-VERSION-0.1.4`。这些失败项检查本次边界提示/失焦路径之外的 HDR 元数据、解码、增益图、调度、合成器、主题或版本行为。
- 仓库通用 `tests/quality_static.py` 为 24/29；未通过的是 `ST-01`、`ST-06`、`ST-09`、`ST-11`、`ST-16`。`ST-01` 对整个既有 C++ 文件执行格式门禁，报告落在既有导航按钮测试和测试入口行；本次新增测试范围的格式化抽查通过。其余项分别检查全分辨率/预览解码、TIFF/RAW 设置格式、小图 viewport 策略及 Theme 控件，与本次验收标准无关。专项静态契约和动态回归均独立通过。

## 证据与适用范围

方案推导及反向复核依据 Apple HIG 的 [Feedback](https://developer.apple.com/design/human-interface-guidelines/feedback)、[Alerts](https://developer.apple.com/design/human-interface-guidelines/alerts)、[Motion](https://developer.apple.com/design/human-interface-guidelines/motion)，Material [Snackbar 指南](https://m2.material.io/components/snackbars)，以及 Qt 的 [QAccessible](https://doc.qt.io/qt-6/qaccessible.html)、[lupdate](https://doc.qt.io/qt-6/linguist-lupdate.html)、[Localization](https://doc.qt.io/qt-6/localization.html)、[QWindow](https://doc.qt.io/qt-6/qwindow.html) 与 [QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html) 文档。HIG 支持将一般反馈设计成低打扰的上下文提示并保持动效简短；Material 的底部 snackbar 和至少 4 秒时长仅作为明确引用的设计先例，并非 macOS 强制规范。完整来源链、推导和逆向证伪见 [技术设计文档](technical_design_document.md)。

动态像素比较覆盖当前 Cocoa/Qt 配置中的透明 PNG native SDR viewport；HDR 失焦行为由 native gain-map JPEG 的真实窗口系统测试覆盖，包括 presentation-layer 逐帧采样及失焦前后 WindowServer 边缘细节比较。专项测试不能替代所有显示器、GPU、辅助技术或其他系统 Appearance 组合的真实设备验证。

# 透明 PNG 视口失焦背景泄漏：测试完成补充

日期：2026-09-29
结论：原测试确实漏测 Native SDR/CALayer 的真实屏幕合成；屏幕级测试在修复前失败，native SDR 常驻修复后重复 5 次通过。

## 失效原因与修复

- 原焦点测试以不透明 SVG 构造场景，调用 `viewport()->render()` 并比较 QWidget 离屏输出。它没有让操作系统激活另一顶层窗口，也没有捕获 WindowServer 最终合成像素，因此无法看到 CALayer 子树被关闭后透明 PNG 像素透出的桌面背景。
- 提供的 `auctions_r_34_2x.png` 为 476×68 RGBA，SHA-256 `c837791cdead6ff31446de1828580d8c7820cfd8751f4cd1bbe14f81896fc6a3`，alpha 透明像素比例为 74.9%。现固定为 [focus_transparency.png](/Users/inostarlin/code/Fovelle/tests/data/focus_transparency.png)，确保回归测试不依赖 Downloads 路径。
- 旧生产代码在 deactivate 时把整个 `presentationContainerLayer` 隐藏，包括 native SDR 图片及不透明 viewport 背景。该失焦策略对 HDR 所需的 SDR fallback 不应套用到 native SDR：普通 SDR 不需关闭 EDR，并且它的 native layer 同时组合 alpha 图像与应用底色。
- `QVGraphicsView` 现在只在 HDR 图像上使用失焦 SDR fallback；Native SDR 在主窗失焦和从失焦状态加载时均保持 native presentation 请求。等原生首帧 ready 后仍隐藏 Qt proxy、停用无用 viewport 重绘，因此透明区域继续以应用底色合成，图片不与 proxy 重复混合。

## 原子验收与执行结果

- **AC-VIEWPORT-FOCUS-SHARP**：动态测试用不遮挡 MainWindow 的独立顶层窗口真实切换激活状态，并从 `QScreen::grabWindow(mainWindow.winId(), viewportRect...)` 采集前后屏幕帧。测试修复前在 `inactive == focused` 失败，屏幕图像尺寸均为 1900×1244；修复后命令 `QT_QPA_PLATFORM=cocoa FOVELLE_TEST_SUITE=WindowBehaviorTests build/tests/fovelle_tests testViewportImageRemainsSharpAfterFocusLoss -repeat 5 -v1` 通过，5 次用例加 init/cleanup 共 15 项、0 失败。
- **AC-TRANSPARENT-RASTER-BACKGROUND**：确认源 `(470,34)` alpha=0；聚焦时该点匹配 `Qv::viewportBackgroundColor(Dark)`（通道容差 12）；失焦后 native renderer 仍 `presentationActiveRequested=true`，保持 `NoViewportUpdate`，屏幕采样裁去外缘32物理像素后视口内容和透明点像素与聚焦时相同。和上项共用实际屏幕回归，运行时多次通过。
- 修改后执行 `cmake --build build --target fovelle_tests -j4` 成功；`ctest --test-dir build -R 'FovelleNavigationBoundaryHint' --output-on-failure` 的静态+动态两项通过；`python3 tests/navigation_boundary_hint_static.py` 检查的边界提示、本地化、样式和新失焦契约全部通过；`git diff --check` 通过。

## 多源推导与逆向证伪

1. 输入证据：用户截图显示聚焦时透明区域为应用暗底，失焦时透明区域覆盖横向木纹样式底层；给定 PNG 本身具有大量 alpha=0 像素。
2. 源码链：普通 PNG 路径为 Native SDR；CALayer `presentationContainerLayer` 内含 `viewportBackgroundLayer` 与 SDR 内容；deactivate 旧逻辑将整个父 layer 隐藏。与此同时 Qt 在 macOS raster viewport 上使用非 opaque backing-store 合约。此组合可以解释透明区域为何能显示视口下方合成内容。
3. 外部交叉核验：Qt `QScreen::grabWindow()` 官方 API 明确说明捕获屏幕像素、窗口覆盖会进入采样结果；Apple `NSView.isOpaque` 与 `CALayer.isOpaque` 文档说明 opaque 元素必须覆盖其 frame/bounds 内容。这些文档支持屏幕采样选择和 opaque 背景层在合成中的职责，但没有单独声称某个 Qt/Cocoa bug；因果结论由项目 layer 顺序、透明 PNG alpha 和本机 red-green 测试共同建立。
4. 逆向复核：测试在旧 deactivate 行为下实际失败；修复之后实际失焦状态保留原生 SDR container 和底色，重复 5 次通过。如果回退到关闭 native SDR container，presentationActiveRequested 断言/屏幕像素对比会再次失败。普通 HDR 的焦点回退仍走另一路径，不会因保留 SDR native layer 而常亮 HDR。

参考来源：[Qt QScreen](https://doc.qt.io/qt-6/qscreen.html#grabWindow)、[Apple NSView.isOpaque](https://developer.apple.com/documentation/appkit/nsview/isopaque)、[Apple CALayer.isOpaque](https://developer.apple.com/documentation/quartzcore/calayer/isopaque)。屏幕截帧仍为离散采样；本机通过结果限定于 macOS 27 / Qt 6.11.2 和固定透明 PNG，不推及所有显示器/GPU/系统版本。

# HDR 高倍率失焦模糊漏测：测试加固与生产修复补充

日期：2026-09-29
状态：完成；先前的适屏截图清晰度阈值已被分辨率状态断言替代为主要回归门槛。

## 问题界定与原子验收

- **AC-HDR-FOCUS-01**：失焦时 presentation opacity 至少8个中间帧、单调下降。
- **AC-HDR-FOCUS-02**：淡出持续350–550ms，与450ms打开动画一致。
- **AC-HDR-FOCUS-03**：淡出期间 EDR 保持开启，稳定后 opacity=0、EDR关闭、fallback显示。
- **AC-HDR-SDR-FALLBACK-04**：稳定失焦时 SDR fallback 实际宽高与 HDR 图源宽高相等；缺字段或任一维较小均失败。
- **AC-HDR-SCREEN-DETAIL-05**：同一高倍率下实际窗口聚焦/失焦截图 edge cosine similarity≥0.90。
- **AC-HDR-FOCUS-RESTORE-06**：重新激活时 opacity 单调恢复、时长350–550ms、EDR再次启用。
- **AC-HDR-DECODER-STATIC-07**：HDR SDR CIImage 写入 Qt fallback 时不得应用2048px缩放上限；focus telemetry 必须提供实际 fallback 尺寸。

## 先前为何漏测

之前的 HDR focus system 用适屏截图及≥0.90的全局边缘 cosine 判定清晰度。图像在适屏缩放时，2048px Qt fallback 尚未被明显放大，因此用例会在用户反馈的高倍率模糊仍存在时通过。此外首张聚焦截图按进程启动固定1.2秒采样，可能早于 renderer 的 `final-frame-visible` 端点；这样的截图不一定与稳定失焦帧处于相同状态。此次把生产测试驱动设为2×图像缩放；聚焦和失焦截图分别在 `final-frame-visible` / `inactive-sdr-visible` 事件后触发。

## 生产修复

根因是 HDR 解码器保留源分辨率的 SDR `CIImage` 供 native renderer 使用，但把同一 SDR 内容物化为 Qt fallback 时仍受 loader 的2048px `fallbackLargestDimension` 限制。失焦稳定后 renderer 容器关闭，Qt fallback 接管；高倍率显示被缩小的 fallback，丢失的源采样无法由视图放大恢复。

修复让 HDR 路径在生成 SDR fallback 时以 `largestDimension=0` 保留源宽高，并优先使用 source-sized SDR graph，涵盖增益图 JPEG/ISO HDR 与 RAW HDR SDR graph。生产日志新增 `fallback_pixmap_width/height`。2×真实窗口回归中 source=6048×8064；独立失焦稳定遥测验证 Qt fallback 同为6048×8064。注意：该方案将 HDR fallback 从最多2048边长提升为全分辨率，增加加载期间及缓存 pixmap 的内存占用；这是保留高倍率细节的直接成本，超大图像内存影响仍应在目标素材集监测。

## 测试设计与反向证伪

按原子项目分别固化：transition 分析器覆盖连续性、时长、EDR状态及激活恢复；`focus_proxy_matches_source_resolution()` 单独核验失焦端点 source/fallback 宽高；突变单元将6048×8064源替换为1536×2048 fallback，必须拒绝；静态门禁检查解码生产代码完整尺寸参数及遥测；真实窗口系统用例对照状态触发的实际聚焦/失焦截图。相似度截图检查仍保留为图像结构观察，但不能单独证明分辨率；如果日后删除或回退 full-resolution 参数，运行时像素尺寸检查会失败，即使边缘相似度因色调/缩放阈值而通过。

外部证据链以 API 职责为限：[Apple Image I/O `kCGImageSourceThumbnailMaxPixelSize`](https://developer.apple.com/documentation/imageio/kcgimagesourcethumbnailmaxpixelsize)指出该键限制的是缩略图宽高，未设置上限时允许尺寸达到图像本身；[Apple `CIImage`](https://developer.apple.com/documentation/coreimage/ciimage?language=objc)说明 CIImage 是可延迟渲染的图像 recipe；[Apple WWDC24 HDR 工作流](https://developer.apple.com/videos/play/wwdc2024/10177/)说明 Adaptive HDR 可用 SDR base 与 gain map 并保持高动态范围工作流；[Qt `QImage::scaled()`](https://doc.qt.io/qt-6/qimage.html#scaled)定义实际生成缩放副本；[Qt `QScreen::grabWindow()`](https://doc.qt.io/qt-6/qscreen.html#grabWindow)指出抓取的是屏幕像素，高 DPI 下结果可能大于逻辑尺寸。多源资料确认分辨率上限会丢失代理细节、源 CIImage 可供后续渲染、屏幕测试必须考虑物理像素；具体失焦后代理接管这一因果关系由本项目图层状态、源/代理尺寸遥测及红绿测试验证，而非由外部资料臆测。

## 执行结果

- `cmake --build build --target Fovelle fovelle_tests -j4`：成功；Objective-C++ renderer 和 Qt tests 均构建通过。
- `python3 tests/test_hdr_focus_transition_metrics.py`：8/8 通过，包括将 fallback 反向突变为1536×2048时稳定拒绝、缺尺寸字段失败关闭。
- `tests/hdr_focus_transition_system.py` 对真实 Apple gain-map HDR JPEG `1.JPG` 连续两次运行均10/10 checks 通过。每次31个失焦 opacity 中间样本单调下降，动画480ms；31个重聚焦样本单调上升，分别497ms/495ms；EDR终态正确；源/fallback尺寸严格匹配；真实窗口 edge cosine 为0.9509，超过0.90。
- 真实系统 runner 被状态触发截图；旧1.2秒固定截图可能早于稳定聚焦帧，现不再作为聚焦参考。端点聚焦采样绑定 final-frame-visible，失焦采样绑定 inactive-sdr-visible。
- `tests/hdr_quality_static.py` 的 `ST-HDR-FOCUS-PRESENTATION-TRANSITION` 新增的源分辨率fallback、2048px突变测试及2×端点采样契约均通过；整份专项报告为30/37，7项既有无关失败仍是上一节所列元数据/解码/ROI/调度/合成/主题/版本检查。
- `ctest --test-dir build -R 'FovelleHDRFocusTransitionEvidence|FovelleNavigationBoundaryHint' --output-on-failure`：3/3通过；本次Python测试 `py_compile` 与 `git diff --check` 均通过。

结果限定于本机 macOS 27、Qt 6.11.2、当前 EDR 显示器与该 gain-map JPEG。截图是空间细节证据而非光度测量；全分辨率 Qt fallback 带来额外内存占用。

# HDR 图片失焦渐暗与清晰端点：修复与测试补充

日期：2026-09-29
结论：已将 HDR 失焦从即时切换改为 450 ms 反向亮度过渡；突变测试、源码契约和真实窗口系统回归均通过。

## 问题界定与原子化验收

- **AC-HDR-FOCUS-FADE-01**：失焦不得将 HDR layer 瞬间设为透明；真实 `presentationLayer.opacity` 至少有 8 个中间帧且单调下降。
- **AC-HDR-FOCUS-FADE-02**：失焦淡出与图片打开的动画使用同一 `fullTransitionDuration = 0.45s` 和 ease-in/ease-out 曲线；动态采样时长在 350–550ms 区间。
- **AC-HDR-FOCUS-FADE-03**：淡出期间 `wantsExtendedDynamicRangeContent` 保持开启；动画稳定结束后 opacity=0、EDR 关闭，SDR proxy 可见。
- **AC-HDR-FOCUS-SHARP-ENDPOINT**：失焦稳定端点的真实窗口图像边缘 cosine similarity 与聚焦图像≥0.90，避免模糊或结构丢失。
- **AC-HDR-FOCUS-RESTORE**：重新聚焦使用相同曲线和时长，opacity 单调上升回到 1、EDR 再启用。

## 失效原因与生产修复

之前的 `QVGraphicsView::setHDRPresentationActive()` 把 `active` 同时传为 renderer 的 `animated` 参数。聚焦时 `true` 会动画淡入；失焦时 `false` 会即时关闭 EDR layer，因此测试只看稳定态清晰度时无法发现缺少渐暗过程。

现在两个方向都调用 `hdrRenderer->setPresentationActive(active, true)`。renderer 从 CALayer 当前 presentation opacity 启动同一 450ms ease-in/ease-out opacity animation，并在 fade-out 结束后才关闭 EDR。失焦开始前 Qt SDR proxy 已在底层准备好，HDR layer opacity 到 0 后由它作为清晰终态；native SDR 不经过此 HDR 淡出分支。

## 测试设计和逆向证伪

- `tests/hdr_quality_static.py::ST-HDR-FOCUS-PRESENTATION-TRANSITION` 检查 activation/deactivation 均使用动画、0.45s 常量、同一 easing、presented opacity 读取及测试探针。
- `tests/hdr_quality_system.py::focus_transition_metrics` 分析真实 renderer 状态日志；`tests/test_hdr_focus_transition_metrics.py` 添加正常轨迹、旧即时关闭轨迹及非单调轨迹三项突变单测。旧即时关闭逻辑没有中间帧，也没有 350–550ms 动画区间，必定被拒绝。
- `tests/hdr_quality_system.py::SYS-HDR-FOCUS-FADE-AND-SHARP-ENDPOINT` 纳入完整 HDR system suite；可独立执行的窗口回归为 `tests/hdr_focus_transition_system.py`。它先确认真实输入被解为带 Apple/ISO gain map、target headroom>1 的 HDR JPEG，再读 production 日志和实际窗口截图，避免 SDR 样本让测试误通过。
- 一次固定时间截图亮度试探不能作为可靠的光度验收：屏幕截图会把 HDR 高光编码夹到 8-bit 255，且进程启动偏移和解码耗时会令固定进程时刻落在过渡前。基于 Apple `CALayer.presentationLayer` 文档，最终验收直接以呈现树中的当前 opacity 做逐帧时间断言；opacity 是正在显示的层值。HDR 高光层向固定 SDR proxy 的 alpha contribution 单调减少，亮度向 SDR 端点收敛；屏幕截图独立验证终态边缘细节。测试报告不把 8-bit 屏幕截图误称为绝对光度计。
- 逆向证伪覆盖：换回即时关闭会让中间帧/时长断言失败；把 EDR 提前关闭会让淡出中 `wants_edr` 检查失败；隐藏 SDR proxy 会让 inactive endpoint 检查失败；破坏 opacity 单调性会让方向检查失败；引入终态模糊/结构缺失会使边缘 cosine 阈值失败。

## 执行结果

- `cmake --build build --target Fovelle fovelle_tests -j4` 成功。
- `python3 tests/test_hdr_focus_transition_metrics.py`：5/5 通过，包括对旧即时关闭缺陷、非单调轨迹和错误稳定端点截图配对的稳定拒绝。
- `ctest --test-dir build -R 'FovelleHDRFocusTransitionEvidence|FovelleNavigationBoundaryHint' --output-on-failure`：3/3 通过，含 native SDR 真实失焦屏幕像素动态回归。
- `tests/hdr_focus_transition_system.py` 使用 `/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG` 连续执行两次真实 gain-map HDR 窗口回归，2/2 运行均为9/9检查通过。每次失焦/聚焦各采得31个 opacity 样本且单调，淡出 span=496ms/495ms，淡入均为497ms；EDR 仅在稳定失焦端点关闭，SDR proxy 可见。系统截图由 `inactive-sdr-visible` 状态日志触发，两次聚焦/失焦稳定端点 edge cosine 均为0.937（阈值0.90）。
- HDR 源码大套件的 `ST-HDR-FOCUS-PRESENTATION-TRANSITION` 通过；总计仍是 30/37，7 项既有非本任务失败为 `ST-HDR-NONRAW-METADATA`、`ST-HDR-NONRAW-NONVOLATILE-DECODE`、`ST-HDR-DNG-GAINMAP-ROI`、`ST-HDR-DISPLAYLINK-LATEST-ONLY`、`ST-HDR-PERSISTENT-COMPOSITOR-FAST-PATH`、`ST-HDR-THEME-BACKGROUND`、`ST-HDR-VERSION-0.1.4`。
- `python3 -m py_compile` 覆盖本次修改的 Python 测试脚本且 `git diff --check` 通过。

## 多源依据与边界

Apple [`CABasicAnimation`](https://developer.apple.com/documentation/quartzcore/cabasicanimation?language=objc) 文档确认标量 `opacity` 可插值动画；[`CALayer.opacity`](https://developer.apple.com/documentation/quartzcore/calayer/opacity?language=objc) 定义 0 到 1 的可动画透明度；[`presentationLayer`](https://developer.apple.com/documentation/quartzcore/calayer/presentation%28%29?changes=_8&language=objc) 文档说明动画期间它近似表示屏幕当前值；[`CAMediaTiming.duration`](https://developer.apple.com/documentation/quartzcore/camediatiming/duration) 定义以秒表示的时长；[`easeInEaseOut`](https://developer.apple.com/documentation/quartzcore/camediatimingfunctionname/easeineaseout?changes=_7) 说明曲线前后缓、中段加速。Qt [`QScreen::grabWindow()`](https://doc.qt.io/qt-6/qscreen.html#grabWindow) 捕获屏幕合成像素，支持使用真实窗口截图检验最终清晰度。以上资料分别核实动画属性、动画采样和终态采集手段；450ms 来自本项目打开 HDR 图像时既有动画，不是平台规范要求。

真实系统证据限定于本机 macOS 27、Qt 6.11.2、当前 EDR 显示器及固定 Apple gain-map JPEG；对其他 GPU 和显示器不作普遍保证。屏幕截图用于空间细节而非 absolute luminance，过渡亮度依据 Core Animation 当前 opacity 和其固定 SDR 下层的合成路径验证。

# macOS 原生感叹号提示框：测试完成报告

日期：2026-09-29

## 验收标准和结果

| 原子验收标准 | 主要验证 | 结果 |
|---|---|---|
| AC-ALERT-CENTRAL：生产 QMessageBox 统一由 NativeDialogs 创建 | Python 静态构造点扫描 | 通过；唯一构造点为 `src/nativedialogs.cpp` |
| AC-ALERT-NATIVE：原生消息框选项未禁用 | Python 顺序/反向扫描；Qt 6.6+ 动态 `testOption()` | 通过 |
| AC-ALERT-ICON：Information/Warning 感叹号类别图标移除，Critical/Question 保留 | Python factory mapping；四种 icon 的 Cocoa Qt 动态属性断言 | 通过 |
| AC-ALERT-LAYOUT：父级提示采用无类别图标的原生 sheet 紧凑版式 | Cocoa parent/modality/icon 属性；原生选项契约 | 通过 |
| AC-ALERT-SHEET：有父级为 WindowModal，无父级保持 ApplicationModal | 静态 parent 条件；Cocoa Qt 运行时属性 | 通过 |
| AC-ALERT-CONTENT：正文和业务按钮/角色保持 | 正文/OK 属性断言；Cocoa 模态路径自动点击 Remember 与 Cancel 并校验返回角色 | 通过 |
| AC-ALERT-APPEARANCE：Light/Dark/System 遵循 AppKit appearance | Cocoa `effectiveAppearance` 窗口检查及 Theme adapter 源码 | 通过；Aqua/DarkAqua 和受控 System 两分支均通过 |
| AC-ALERT-ALL-ENTRYPOINTS：更新、会话和普通提示复用共享实现 | 全生产源码 QMessageBox 构造点静态扫描 | 通过 |

## 执行记录

- 构建命令：`cmake --build build --target fovelle_tests -j4`；结果：成功。
- 应用构建：`cmake --build build --target Fovelle -j4`；结果：`Fovelle.app/Contents/MacOS/Fovelle` 成功链接，脚本完成 app 构建。Ghostscript staging 的 install_name_tool 输出依赖库签名将失效的警告，但目标构建成功。
- 静态测试：`python3 tests/native_alerts_acceptance.py`；结果：5/5 通过。
- 新增 CTest：`ctest --test-dir build -R 'FovelleNativeAlerts(Static|Dynamic)' --output-on-failure`；结果：2/2 通过。最终运行静态测试 0.07 秒、Cocoa 动态测试 3.81 秒，总耗时 3.89 秒。
- 动态测试覆盖：Information/Warning→NoIcon，Critical/Question 类别图标不变；Light→Aqua、Dark→DarkAqua、System/light→Aqua、System/dark→DarkAqua；有父窗口 WindowModal、无父窗口 ApplicationModal；正文、标准按钮、Remember/End Session 角色、Cancel 结果和 Qt 6.6+ 原生对话框选项均断言通过。
- 建置/执行环境：仓库当前 `build` 目录的 Cocoa Qt 构建，macOS，本机已解析 Qt 6.11.2。

## 证据链、逆向复核与限制

Apple [Alerts HIG](https://developer.apple.com/design/human-interface-guidelines/alerts) 说明 macOS alert 默认显示应用图标；Apple [`NSAlert.icon`](https://developer.apple.com/documentation/appkit/nsalert/icon) 补充默认 app icon 可在上下文明确的 window sheet 中省略。Qt [`QMessageBox`](https://doc.qt.io/qt-6/qmessagebox.html) 文档确认 Information、Warning 等图标由 GUI style 提供且 `NoIcon` 是可用值；截图中的白色感叹号气泡与项目真实蓝色 Fovelle 图标不同，且语言提示代码调用 `QMessageBox::Information`，所以实现将两种感叹号类别映射为 `NoIcon`，不隐藏 Apple 原生 app icon 策略。Apple [Sheets HIG](https://developer.apple.com/design/human-interface-guidelines/sheets) 定义主窗口归属/一次一个 sheet；Qt 文档明确将有父窗口且 `Qt::WindowModal` 映射为 macOS Qt Sheet，亦说明 `DontUseNativeDialog` 用于禁用原生实现且默认关闭。Apple [`NSAppearance`](https://developer.apple.com/documentation/appkit/nsappearance) 与 [`NSApplication.appearance`](https://developer.apple.com/documentation/appkit/nsapplication/appearance) 说明 AppKit 控件继承 appearance，系统模式可继承当前系统外观。落地链为：移除 Qt 感叹号类别图标 → 使用 parent window sheet 的平台版式 → 现有主题 bridge 负责 Aqua/DarkAqua → 在 Cocoa 实际 window handle 验证结果。

反向证伪点也被落实为测试：恢复 ApplicationModal 会破坏 sheet 模态断言；启用 DontUseNativeDialog 会破坏静态契约及 Qt 6.6+ 选项断言；丢失 Theme bridge 会破坏两套 appearance 检查；丢失 parent 条件会破坏动态 parent/modal 检查。当前这轮没有人为修改源码执行突变，因此上述是代码中的明确负向断言路径，不把它报告成实际完成的 mutation-testing 结果。

测试确认的是 Qt/Cocoa 属性和窗口最终 Appearance，而没有做截图像素/无障碍快照的视觉比较。自定义按钮角色的业务连线被迁移保留，但本次运行用例未实际点击 Download、Skip Version、Remember 或 End Session；这些交互仍由既有业务路径负责。Qt 5 不提供该 QMessageBox option 的公开 setter/getter（此选项从 Qt 6.6 引入），所以 Qt 5 只由默认值/静态路径和 Cocoa sheet 规则覆盖；本机实机验证基于 Qt 6.11.2。HIG 是平台设计建议，并非对所有 Qt/Cocoa 版本的像素级外观保证。

## 后续完整回归说明

本次最新完整 `FovelleTests` 回归在加入按钮响应回归之前执行，新增 `testNativeMessageBoxesUseParentSheets` 通过；其余窗口相关项通过，唯一失败为 `WindowBehaviorTests::testViewportImageRemainsSharpAfterFocusLoss` 的失焦前后屏幕像素相等断言（[测试位置](../tests/tst_qviewtests.cpp)）。该用例曾单独重跑一次，同一断言仍失败。这不是消息框生产路径；本次未改动失焦图像行为。最终定向 Alert 静态/动态 CTest（包含新增按钮响应测试）为 2/2 通过。


# macOS QMessageBox 全量委托 AppKit NSAlert：测试完成报告补充

日期：2026-09-29
结论：所有生产 QMessageBox 已通过统一工厂并由 Cocoa `NSAlert` 后端显示；应用层不再控制 alert 的视觉与几何布局。

## 验收结果

| 原子标准 | 结果 | 证据 |
|---|---|---|
| AC-NATIVE-01 单一构造入口 | 通过 | 静态扫描只找到 `src/nativedialogs.cpp` 一个 QMessageBox 构造点，无静态便捷 API |
| AC-NATIVE-02 Cocoa runtime | 通过 | 动态捕获 Qt Cocoa `qt.qpa.dialogs` 中 `Showing <NSAlert`；factory 使用 ApplicationModal 和 `show()`，转换 rich text、无详情/checkbox |
| AC-NATIVE-03 只提供语义 | 通过 | factory 仅设置 severity、messageText、informativeText、plain-text mode、standardButtons；业务已删除 QMessageBox 自定义 buttons/checkbox |
| AC-NATIVE-04 AppKit 负责布局 | 通过 | 静态检查禁止 QMessageBox 自设 style/size/geometry/margin/font/layout/button/icon；动态测试不锁定宽度或位置 |
| AC-NATIVE-05 本地化与外观 | 通过 | 生产调用文案经过 `tr()` 检查；动态运行验证四种 severity 的 Light/Dark/System 颜色继承 |
| AC-NATIVE-06 标准动作结果 | 通过 | Save/Discard/Cancel 均从原生提示返回相同标准按钮枚举 |
| AC-NATIVE-07 回归可证伪 | 通过 | 四种内存突变（WindowModal、固定尺寸、自定义按钮、禁用 native）均稳定被拒绝 |

## 执行记录

- `python3 tests/native_alerts_acceptance.py`：8/8 通过，含六字段测试用例元数据检查和 4 种故障突变。
- `cmake --build build --target Fovelle fovelle_tests -j 6`：两个目标成功。Ghostscript 依赖 `install_name_tool` 在 staging 文件上的签名失效警告是打包脚本的既有提示；构建完成退出码为 0。
- `ctest --test-dir build -R 'FovelleNativeAlerts' --output-on-failure`：最终执行 2/2 CTest 通过（静态 0.18s；动态 3.64s；总计 3.83s）。
- 动态矩阵包含 Light/Dark × Information/Warning/Critical/Question 共 8 个 alert，System-light/System-dark 再 2 个 alert；均检查后端的 `NSAlert` 创建日志和 alert 当前有效 Appearance。另对 Save/Discard/Cancel 各动态点击一次，共 3 个 response case。HTML 样式包裹的多语言长句被转换为 plain text 后仍走 NSAlert。
- 运行平台是本机 macOS Cocoa、当前 build 的 Qt 6.11.2。未运行全套 `FovelleTests`，只运行本任务关联的目标和测试；其他图像/HDR/navigation 行为不在本次改动范围。

## 实施范围及可见行为

会话提示用系统标准 Save / Discard / Cancel 替换 Remember / End Session 自定义标签；更新提示改用 Open / Ignore / Close。更新检查禁用设置继续在 Preferences 提供；删除确认不再内嵌 “Do not ask again” checkbox，是否询问仍由 Preferences 里的 `askdelete` 设置控制。这样 alert 中没有 AppKit 标准 API 以外的按钮文字、suppression checkbox 或应用绘制控件。

为规避已在 Qt 6.11 Cocoa 源码中确认的 Tahoe WindowModal fallback，有 parent 的 QMessageBox 也改为 ApplicationModal。它是原生 `NSAlert.runModal` 标准 alert 而不是附着 window sheet；因此本应用其他窗口也会被模态阻止，native 路径和“保留 sheet”无法在这套 Qt/Cocoa 条件下同时满足。本次按用户当前更高优先级的全量 NSAlert 约束选用应用模态。

## 多源验证与反向证伪记录

Qt [`QMessageBox` 文档](https://doc.qt.io/qt-6/qmessagebox.html)描述 message/informative/severity/standard-buttons 与 native option；Qt 6.11 Cocoa [`QCocoaMessageDialog`](https://github.com/qt/qtbase/blob/6.11/src/plugins/platforms/cocoa/qcocoamessagedialog.mm)直接实例化 NSAlert，并列出 rich/detail/模态 fallback 条件；Apple [`NSAlert`](https://developer.apple.com/documentation/AppKit/NSAlert?language=objc)文档给出这些语义字段和 app-modal 展示 API。Apple [macOS 11 AppKit notes](https://developer.apple.com/documentation/macos-release-notes/appkit-release-notes-for-macos-11)说明 NSAlert 自动采用纵向为主的系统布局、按钮自适应与系统最大高度。多源链共同支持此次实现与“不做 margin/宽度/按钮位置指标”的测试边界。

逆向验证并非仅在报告中列想象的坏例：测试以内存形式把 modality 改成 WindowModal、加入固定尺寸、加入文字自定义按钮及启用 DontUseNativeDialog，实际调用静态验收方法并确认每个突变失败。动态 native-path 测试若出现 QWidget fallback 即缺少 `Showing <NSAlert`，因此本地化长字串和 Appearance 测试能反驳“只关掉禁用选项就等于走 native”的弱假设。

资料结论限定到本机 Qt 6.11.2 backend 和当前 macOS：Qt 未来版本可能修复 Tahoe sheet 的按钮问题，届时可重新评估 parent sheet，但仍应以运行时 `NSAlert` 证据验证。native NSAlert 的实际排版应由系统版本、辅助功能和语言驱动；本次不做固化像素快照。
