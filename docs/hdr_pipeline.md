# Fovelle RAW/HDR 浏览管线（v0.1.4）

## 目标与边界

HDR 主图从解码到显示保持为 Core Image 高精度图和 Metal `RGBA16Float` drawable。`QImage`/`QPixmap` 只承担首帧连续性、复制和不支持原生 HDR 时的 SDR 回退，不是 HDR 主路径。最终由支持 EDR 的 `CAMetalLayer` 交给 macOS WindowServer；SDR 显示器上 target headroom 自动收敛为 1。

“接近 Quick Look”表示采用 Apple 已公开的 Image I/O、Core Image、ColorSync、Metal、gain-map 和 EDR 机制，并以 Quick Look 导出结果作结构比较。Quick Look 的私有 RAW 配方、tone curve 和亮度时序未公开，因此不声称逐像素复刻。

## 可验证的外部事实

- Apple 的 [WWDC24 HDR images](https://developer.apple.com/videos/play/wwdc2024/10177/) 说明 Adaptive HDR 可由 SDR base＋gain map 表示，显示时按 headroom 重建；演讲也说明 macOS 15 的 Quick Look/Preview 使用新的 HDR 图像 API。
- [`CIImage.auxiliaryHDRGainMap`](https://developer.apple.com/documentation/coreimage/ciimageoption/auxiliaryhdrgainmap) 可请求辅助 HDR gain map；[`applyingGainMap(_:headroom:)`](https://developer.apple.com/documentation/coreimage/ciimage/applyinggainmap%28_%3Aheadroom%3A%29) 可按目标 headroom 应用它。
- [`CIRAWFilter`](https://developer.apple.com/documentation/coreimage/cirawfilter) 从传感器 RAW 生成 `CIImage`；其 [`previewImage`](https://developer.apple.com/documentation/coreimage/cirawfilter/previewimage) 是原图的可选辅助预览表示。
- Apple 的 [WWDC22 EDR](https://developer.apple.com/videos/play/wwdc2022/10114/) 给出 `RGBA16Float`、扩展线性 Display P3、`CAMetalLayer.wantsExtendedDynamicRangeContent` 与 `NSScreen` headroom 的显示合同。
- [`CAMetalLayer.nextDrawable()`](https://developer.apple.com/documentation/quartzcore/cametallayer/nextdrawable%28%29) 在 drawable pool 忙时会等待，最长可到一秒；[`CAMetalDisplayLink`](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink) 用显示同步回调控制 Metal 帧时序，以改善可变刷新率和视觉伪影。
- Qt 明确说明 [`QWidget::grab`](https://doc.qt.io/qt-6/qwidget.html#grab) 会把 widget 重新渲染到 pixmap；[`QGraphicsEffect`](https://doc.qt.io/qt-6/qgraphicseffect.html) 位于 source 与 destination 之间，并可把整个 source 绨制成 pixmap 后处理。
- Apple 的 [`NSScreen.maximumExtendedDynamicRangeColorComponentValue`](https://developer.apple.com/documentation/appkit/nsscreen/maximumextendeddynamicrangecolorcomponentvalue) 是动态 current headroom；[`maximumPotentialExtendedDynamicRangeColorComponentValue`](https://developer.apple.com/documentation/appkit/nsscreen/maximumpotentialextendeddynamicrangecolorcomponentvalue) 描述潜在能力。没有 EDR 内容时 current 可保持 1。
- [`CIImage.imageByInsertingIntermediate`](https://developer.apple.com/documentation/coreimage/ciimage/insertingintermediate(cache:)) 与 [`CIContext.cacheIntermediates`](https://developer.apple.com/documentation/coreimage/cicontextoption/cacheintermediates) 支持重复交互渲染的中间缓存。Apple 的 [WWDC26 Core Image HDR](https://developer.apple.com/videos/play/wwdc2026/305/) 将交互式复用与一次性导出区分开。

## 本轮四条根因链

### 1. 放大后拖动和悬浮控件卡顿

源码事实：修复前 `paintEvent`、水平 scrollbar、垂直 scrollbar 都同步调用完整 renderer；renderer 又在这条 AppKit 路径调用 `nextDrawable`。一个双轴拖动事件可以引发多次 CI/Metal 提交，而 drawable 获取本身允许等待。悬浮导航在每次进入边缘时还对 viewport 调用 `grab`，导致额外重绘。

根因推断：这些同步工作共同占用主事件循环，因而拖动帧和 hover 事件互相阻塞。

修复：所有 paint/scroll/transition 请求只启动一个 0 ms single-shot dirty timer；每轮事件循环最多把最新几何发布一次。`CAMetalDisplayLink` 提供 drawable，renderer 不再调用 `nextDrawable`；`CIContext.render(toMTLTexture:)` 在专用串行 encode queue 执行，而不是占用 AppKit run loop。GPU 同时最多一帧在途；忙时只覆盖 pending viewport/corners/progress/generation，旧状态不会排队。导航对比度改为 post-load 生成的最长边不超过 384 px SDR sample，并只读取映射点附近 3×3 像素。

### 2. 悬浮按钮出现标准矩形底面

源码事实：修复前两个导航按钮都挂载 `QGraphicsOpacityEffect`，opacity 动画作用于整个 widget source surface。HDR 视口同时包含原生 EDR Metal sibling，而圆角只存在于按钮内部绘制像素。

根因推断：矩形 effect source 与圆角 artwork 的合成生命周期不同，解释了标准矩形先出现、后消失。

修复：按钮使用透明且无系统背景的 backing，不挂 graphics effect；动画目标改为 `paintOpacity` 动态属性，`paintEvent` 在同一个 painter 上对圆角底和箭头统一设置 opacity。透明角从始至终没有可淡入的 backing 像素。

### 3. DNG 有 HDR 但细节低于 Quick Look

源码事实：修复前 DNG 使用通用 `CIRAWFilter.outputImage`，并把相机默认的负 `BaselineExposure` 改为 0。独立探针表明该样例默认 baseline 约为 -0.961，单独改为 0 会显著改变浮点峰值；这不是完整相机处理配方。该 DNG 同时提供 8064×6048 的 `previewImage`、4032×3024 gain map，应用后 content headroom 约 3.9696。

根因推断：只改一个曝光参数既不能复刻相机的局部 tone/detail 配方，也会改变 highlight 分配；与该完整预览配对的 authored gain map 才是公开 API 下更接近 Quick Look 的表示。

修复：有配对 gain map 的 DNG 优先保留全尺寸 `CIRAWFilter.previewImage` 作为 SDR base，保留辅助 gain map，并在每个显示 headroom 上调用 `imageByApplyingGainMap:headroom:`。不再修改任何 `BaselineExposure`。处理后的 gain-map 图不再插入 post-apply source intermediate：样例 gain map 恰为 base 的 1/2，过去在此插缓存会把首次缩减 ROI 固化到全尺寸 extent，曾复现只剩左上象限；现在始终从完整 source graph 计算当前 ROI。

Quick Look 质量验收不是主观描述：integration test 让 `qlmanage` 与生产 processed representation 都导出 1024 px 参考，要求 edge cosine ≥0.994、RGB MAE ≤3，并另外验证全尺寸 base、半尺寸 gain map 和浮点 HDR 峰值。

### 4. NEF 缩放短暂残影

源码事实：修复前每个 transform/双 scrollbar 更新都能独立拿 drawable 并提交，代码没有 frame-in-flight 或最新 generation gate。拖动未出现残影而缩放出现，和一次缩放同步触发 transform＋scene rect＋两个 scrollbar 更新相符。

根因推断：相邻几何的命令在 drawable queue 中依次呈现，UI 已到新 zoom 时旧帧仍可能短暂上屏。

修复：DisplayLink callback 只在无 frame in flight 时提交；其余请求覆盖 pending state。command buffer 在 commit 前按序 `presentDrawable`，完成后再开放下一帧。系统验收在 JPEG/NEF 交互结束时要求 `requestedGeneration == submittedGeneration` 且 `frameInFlight=false`，NEF 定时截图还独立检查结构 tile、黑带与最终帧 edge similarity。

## 完整数据流

### DNG / Adaptive-HDR RAW

`CGImageSourceGetType` 先按内容 UTI 判定 RAW。若 RAW 含 Apple/ISO gain map，则尝试：

`CIRAWFilter.previewImage (full resolution SDR)` → `CIImage auxiliary gain map` → `applyingGainMap(headroom)` → ColorSync extended-linear Display P3 → Core Image/Metal `RGBA16Float` → EDR layer。

只有 base、gain map、extent 和 headroom 合同全部成立时才发布 `camera-raw-processed-gain-map`；否则继续传统 RAW 路径，不把普通 embedded preview 冒充 HDR。

### NEF / CR3 / ARW / RAF 等传统 RAW

建立两个互不变异的 `CIRAWFilter`：SDR endpoint 使用 `extendedDynamicRangeAmount=0`，HDR endpoint 使用 1；两者都保留相机默认参数、full scale、非 draft。`CIContext` 启用 intermediate cache，两个 source endpoint 在 viewport transform 之前由 Core Image 管理。若 `contentHeadroom` 未报告，则用 `CIAreaMaximum` 对浮点 HDR endpoint 作一次约减，并把实际内容范围用于 CI/layer tag；显示潜力不能冒充内容 headroom。

解码器不支持某相机型号时才尝试普通 `previewImage`/内嵌 JPEG 作为明确标记的 SDR fallback；都失败则返回错误。LibRaw 暂不引入：当前样例和格式由 Apple RAW 支持，新增第二套 demosaic、相机色彩矩阵、镜头校正和打包维护不符合精益性；未来遇到 Apple 不支持的具体型号再评估独立可选 backend。

### 非 RAW HDR

Image I/O 解析方向、色彩空间、传输函数、Apple/ISO gain map 与 headroom。HDR candidate 使用 `kCGImageSourceDecodeToHDR` 和 `kCIImageExpandToHDR`，SDR/HDR URL recipe 都设置 `kCIImageCacheImmediately=YES`，避免首次局部 Metal ROI 决定易失源缓存。PQ、HLG、extended-range 与 gain-map 来源均进入 metadata；普通 SDR 文件继续走原有 Qt 图片路径。

### 色彩、首帧和显示适配

ColorSync 创建扩展线性 Display P3 工作/输出空间；CI context 使用 `kCIFormatRGBAh`，Metal layer 使用 `MTLPixelFormatRGBA16Float`、`wantsExtendedDynamicRangeContent=YES` 和自动 tone map。每次实际提交读取窗口所在屏幕 current/potential headroom；current=1、potential>1 时可 bootstrap 首个 EDR 帧，potential=1 时 target 固定为 1。

首个昂贵 endpoint 求值在 SDR proxy 可见时预热；drawable 真正 presented 后才令 Metal opacity=1。650 ms 平滑增亮只属于新图片打开生命周期；zoom/pan/resize 复用同一 prepared presentation，不重置 progress、不回切 SDR、不再次变亮。

## 测试与可观测性

正式顺序固定为 static → unit → integration → system。每条原子标准拥有唯一 ID，并记录目的、前置条件、输入、步骤、预期和后置条件。测试环境变量只启用观察/确定性 driver，不替换生产 decoder 或 renderer。

时间阈值包括：

- 48 MP JPEG/DNG 解码平均 ≤2500 ms、P99 ≤3500 ms、最大 ≤4000 ms、吞吐 ≥0.4 image/s；
- 稳态 Metal submission 平均 ≤30 ms、P99 ≤120 ms、最大 ≤200 ms、等效吞吐 ≥33/s；
- JPEG/NEF zoom 主线程 ≤30 ms；12 步 pan 平均间隔 ≤25 ms、P99 ≤45 ms、最大 ≤60 ms、吞吐 ≥40 steps/s；
- 激活阶段观察帧率 ≥30/s，相邻 progress 步长 ≤0.15。

机器证据位于 `reports/evidence/`，聚合输出为 `reports/test_evidence.json`、`reports/test_case_specification.json`、`reports/test_completion_report.json` 和 `reports/code_quality_assessment_report.json`。

## 事实、推断与不确定性

- 事实：pre-fix/working-tree 源码差异、样例哈希、RAW/gain-map 浮点探针、Quick Look 导出指标、真实 WindowServer telemetry、screen crops 和时延原始样本全部进入 JSON。
- 推断：同步 drawable/重复 paint、effect source surface、DNG 配方不匹配和旧 geometry queue 分别是四个症状的最佳根因解释；对应修复同时移除了各自的可审计必要条件。
- 不确定：Apple 不公开 Quick Look 私有 RAW tone recipe 或 Core Image 内部 tile/ROI scheduler，不能证明内部算法相同。
- 不确定：系统截图会被色调映射，不能单独证明绝对 nits 或捕获每个亚帧 ghost；浮点峰值、headroom、代际不变量和多时点截图互补。
- 不确定：本轮没有物理 SDR-only Mac；纯策略单元测试和 current=potential=1 的真实 renderer override 确定性覆盖 SDR 分支，仍建议发布前补一台 SDR Mac 的人工视觉巡检。
