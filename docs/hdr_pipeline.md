# Fovelle RAW/HDR 浏览管线（v1.0.0）

## 目标与边界

HDR 主图从解码到显示保持为 Core Image 高精度图。首个可见 HDR 帧和超出缓存预算的图像使用 Metal `RGBA16Float` drawable；预算内图像随后物化为 regular Core Animation layer 中的 half-float `CGImage`，pan/zoom 只更新 layer transform。`QImage`/`QPixmap` 只承担首帧连续性、复制和不支持原生 HDR 时的 SDR 回退，不是 HDR 主路径。两种原生 surface 都请求 EDR；SDR 显示器由系统 tone map，Metal 回退的 target headroom 自动收敛为 1。

“接近 Quick Look”表示采用 Apple 已公开的 Image I/O、Core Image、ColorSync、Metal、gain-map 和 EDR 机制，并以 Quick Look 导出结果作结构比较。Quick Look 的私有 RAW 配方、tone curve 和亮度时序未公开，因此不声称逐像素复刻。

## 可验证的外部事实

- Apple 的 [WWDC24 HDR images](https://developer.apple.com/videos/play/wwdc2024/10177/) 说明 Adaptive HDR 可由 SDR base＋gain map 表示，显示时按 headroom 重建；演讲也说明 macOS 15 的 Quick Look/Preview 使用新的 HDR 图像 API。
- [`CIImage.auxiliaryHDRGainMap`](https://developer.apple.com/documentation/coreimage/ciimageoption/auxiliaryhdrgainmap) 可请求辅助 HDR gain map；[`applyingGainMap(_:headroom:)`](https://developer.apple.com/documentation/coreimage/ciimage/applyinggainmap%28_%3Aheadroom%3A%29) 可按目标 headroom 应用它。
- [`CIRAWFilter`](https://developer.apple.com/documentation/coreimage/cirawfilter) 从传感器 RAW 生成 `CIImage`；其 [`previewImage`](https://developer.apple.com/documentation/coreimage/cirawfilter/previewimage) 是原图的可选辅助预览表示。
- Apple 的 [WWDC22 EDR](https://developer.apple.com/videos/play/wwdc2022/10114/) 给出 `RGBA16Float`、扩展线性 Display P3、`CAMetalLayer.wantsExtendedDynamicRangeContent` 与 `NSScreen` headroom 的显示合同。
- Apple 将 [`CABasicAnimation`](https://developer.apple.com/documentation/quartzcore/cabasicanimation) 定义为 layer 属性的插值动画，并明确以 `opacity` 0→1 为淡入示例；[`CALayer.presentation()`](https://developer.apple.com/documentation/quartzcore/calayer/presentation%28%29) 返回当前屏幕呈现状态，[`CATransaction`](https://developer.apple.com/documentation/quartzcore/catransaction) 则把多项 layer 修改原子提交到 render tree。这三项构成可反转 presentation fade 的公开合同。
- [`CAMetalDisplayLink`](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink) 按显示刷新提供 drawable 和目标 deadline，但 [`preferredFrameRateRange`](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/preferredframeraterange) 是 best-effort 请求。Metal 首帧/回退在 [`targetTimestamp`](https://developer.apple.com/documentation/quartzcore/cametaldisplaylink/update/targettimestamp) 前调用普通 `present()`；实际时刻由 [`MTLDrawable.presentedTime`](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime) 观测，不能用 command-buffer 提交时刻代替。
- Apple 的 [CPU/GPU 同步指南](https://developer.apple.com/documentation/metal/synchronizing-cpu-and-gpu-work) 说明 drawable pool 通常为 3，`maximumDrawableCount` 只允许 2 或 3；[`CAMetalLayer`](https://developer.apple.com/documentation/quartzcore/cametallayer) 也要求 drawable 离屏且无强引用后才可复用。因此窗口合成 present delay 会直接限制“每个输入样本生产一个 drawable”的最高吞吐。
- Apple 说明 [`CIContext`](https://developer.apple.com/documentation/coreimage/cicontext) 是重量级、可跨线程复用的对象。本实现复用交互 Metal context，并用第二个 context 在独立串行队列一次性物化持久 HDR surface；完成后立刻 `clearCaches()`，避免保留一次性求值缓存。
- Apple 的 Core Animation 指南说明 layer 会缓存内容 bitmap，transform 可由硬件操纵；[`CATiledLayer`](https://developer.apple.com/documentation/quartzcore/catiledlayer) 则面向大图异步 tile/LOD。本实现先采用有界完整 surface 快路径，超预算时保留 viewport-sized Metal 回退；后续可在不改变交互模型的前提下替换为 tiled backing。
- Qt 明确区分共享 backing store 的 alien `QWidget` 与拥有窗口句柄的 native widget。HDR 按钮不再让透明 alien widget 跨越独立 EDR surface，而是通过 [`CALayer.addSublayer`](https://developer.apple.com/documentation/quartzcore/calayer/addsublayer%28_%3A%29) 把 `CAShapeLayer` artwork 放在 native view 的固定 sibling layer 中。
- Qt 的 [`QContextMenuEvent`](https://doc.qt.io/qt-6/qcontextmenuevent.html) 文档说明它在非 Windows 平台默认由 mouse press 触发，且即使原始 mouse event 已处理仍会发送；Qt 6.8+ 同时公开 [`ContextMenuTrigger::Release`](https://doc.qt.io/qt-6/qt.html#ContextMenuTrigger-enum)。Apple 的 [`NSMenu.popUp(positioning:at:in:)`](https://developer.apple.com/documentation/appkit/nsmenu/popup%28positioning%3Aat%3Ain%3A%29) 可以不附带第二个鼠标事件而启动菜单 tracking。
- Apple 的 [`NSScreen.maximumExtendedDynamicRangeColorComponentValue`](https://developer.apple.com/documentation/appkit/nsscreen/maximumextendeddynamicrangecolorcomponentvalue) 是动态 current headroom；[`maximumPotentialExtendedDynamicRangeColorComponentValue`](https://developer.apple.com/documentation/appkit/nsscreen/maximumpotentialextendeddynamicrangecolorcomponentvalue) 描述潜在能力。没有 EDR 内容时 current 可保持 1。
- [`CIImage.imageByInsertingIntermediate`](https://developer.apple.com/documentation/coreimage/ciimage/insertingintermediate(cache:)) 与 [`CIContext.cacheIntermediates`](https://developer.apple.com/documentation/coreimage/cicontextoption/cacheintermediates) 支持重复交互渲染的中间缓存。Apple 的 [WWDC26 Core Image HDR](https://developer.apple.com/videos/play/wwdc2026/305/) 将交互式复用与一次性导出区分开。

## 累计七条根因链

### 1. 放大后拖动和悬浮控件卡顿

实测事实（M3 Pro、内建 120 Hz XDR、8064×6048 JPEG）：旧 viewport-drawable 路径 CPU 编码约 0.4 ms、GPU 执行约 2.0 ms，但 `addPresentedHandler` 的 request-to-glass 约 40 ms，稳态只有约 55–63 fps。该组合排除了 shader/主线程算力饱和；三 drawable 若各占用约 40 ms，理论供给上限约为 `3 / 0.040 = 75 fps`，与观测方向一致。单纯把 frame-rate range 强制为 120 并未改变结果。

根因推断：窗口合成后的 drawable 生命周期而不是 CI/GPU 执行时间，限制了“每次拖动都重画整个 viewport”的频率。Apple 未公开 Preview 的内部实现；但其公开 Core Animation 模型支持缓存内容并只变换 layer，因此 Preview 更可能使用持久/分块内容合成，而不是为每个鼠标样本重建一张窗口大小 drawable。后一条是架构推断，不是对 Preview 私有实现的事实声明。

修复分两层：首帧和超预算回退继续走 `CAMetalDisplayLink` latest-only 管线（80–120、preferred 120、最多两帧在途）；最终 HDR endpoint 准备完成后，后台以 `kCIFormatRGBAh`、`deferred:NO` 物化完整 source-space `CGImage`。当原始 half-float surface 不超过 512 MiB 且边长不超过 16384 时，在一个禁用 implicit actions 的 transaction 中显示 persistent layer、隐藏并暂停 Metal layer。此后 zoom/pan 只更新 `persistentImageLayer.affineTransform`，不申请 drawable、不执行 CI render，并把隐藏的 Qt viewport 切到 `NoViewportUpdate`。专用 8 ms 探针在无 verbose logging 时完成 48 次更新用时 384–385 ms（124.7–125 updates/s）、50 次 compositor geometry update、0 次交互 Metal present，最后一次几何提交约 0.08–0.10 ms；Instruments 同期显示 8.33 ms VSync 且 CA commit/render hitch 表为空。面板把有效可见频率封顶为 120 Hz，因此约 125 是应用侧余量，不应表述成 125 个真实显示帧。

### 2. 悬浮按钮出现标准矩形底面

源码事实：移除 `QGraphicsOpacityEffect` 后，按钮仍是 Qt raster/alien `QWidget`，主图则是独立原生 EDR `CAMetalLayer`。用户截图中的有色区域是精确的 120×120 px 轴对齐矩形，等于 60×60 pt 按钮在 DPR 2 下的 backing 几何；问题只在矩形覆盖真实 HDR 像素时出现。

根因推断：透明 Qt backing store 与 EDR sibling 跨 surface 合成时，透明角没有直接解析到 HDR drawable；这是“在 HDR 像素上显色、在视口背景或 SDR 图上正常”的最佳因果解释。WindowServer 的私有混合公式没有公开，因此内部运算仍属不确定项。

修复：HDR 模式隐藏 Qt 按钮，圆角底和 chevron 改为 native view layer tree 内的两个 `CAShapeLayer`，仅 shape path 有像素；overlay 是 Metal/persistent image 的固定 sibling，因此主图平移时控件不移动。两个 shape 先进入启用 `allowsGroupOpacity` 的单一按钮父层，子层固定为 opacity=1，再只动画父层，避免半透明底板与不透明 chevron 在重叠区分别累积 alpha。frame、颜色、hover、press 与 opacity 均关闭隐式动画后同步。Qt 的左上原点 Y 坐标在写入 Core Animation 子层 frame 前显式转换为底部原点，保证 native artwork 与不可见 Qt 命中区重合。鼠标命中仍由 Qt event filter 处理。此前 HDR 同步函数在目标可见性变为 false 的同一刻把 native opacity 强制为 0，截断了 `QPropertyAnimation` 的 1→0 中间值；现在 native layer 始终读取实时 `paintOpacity`，因此出现和消失都复用与 SDR 相同的 180 ms 曲线。SDR 模式不再逐笔设置透明度，而是先在透明 ARGB 图像中以完整强度合成底板和 chevron，再把 `paintOpacity` 一次性应用于合成结果；两条路径由同一“先组合、后淡化”合同约束。

### 3. DNG 有 HDR 但细节低于 Quick Look

源码事实：修复前 DNG 使用通用 `CIRAWFilter.outputImage`，并把相机默认的负 `BaselineExposure` 改为 0。独立探针表明该样例默认 baseline 约为 -0.961，单独改为 0 会显著改变浮点峰值；这不是完整相机处理配方。该 DNG 同时提供 8064×6048 的 `previewImage`、4032×3024 gain map，应用后 content headroom 约 3.9696。

根因推断：只改一个曝光参数既不能复刻相机的局部 tone/detail 配方，也会改变 highlight 分配；与该完整预览配对的 authored gain map 才是公开 API 下更接近 Quick Look 的表示。

修复：有配对 gain map 的 DNG 优先保留全尺寸 `CIRAWFilter.previewImage` 作为 SDR base，保留辅助 gain map，并在每个显示 headroom 上调用 `imageByApplyingGainMap:headroom:`。不再修改任何 `BaselineExposure`。处理后的 gain-map 图不再插入 post-apply source intermediate：样例 gain map 恰为 base 的 1/2，过去在此插缓存会把首次缩减 ROI 固化到全尺寸 extent，曾复现只剩左上象限；现在始终从完整 source graph 计算当前 ROI。

Quick Look 质量验收不是主观描述：integration test 让 `qlmanage` 与生产 processed representation 都导出 1024 px 参考，要求 edge cosine ≥0.994、RGB MAE ≤3，并另外验证全尺寸 base、半尺寸 gain map 和浮点 HDR 峰值。

### 4. NEF 缩放短暂残影

源码事实：修复前每个 transform/双 scrollbar 更新都能独立拿 drawable 并提交，代码没有 frame-in-flight 或最新 generation gate。拖动未出现残影而缩放出现，和一次缩放同步触发 transform＋scene rect＋两个 scrollbar 更新相符。

根因推断：相邻几何的命令在 drawable queue 中依次呈现，UI 已到新 zoom 时旧帧仍可能短暂上屏。

修复：预算内图像的缩放和拖动直接更新 persistent layer transform；每次输入同步收敛到 `requestedGeneration == submittedGeneration`，交互期间没有 drawable 在途。超预算回退仍使用 DisplayLink-paced latest-only 调度，新输入覆盖未提交几何且最多两个 frame 在途。NEF 定时截图另外检查结构 tile、黑带与最终帧 edge similarity。

### 5. HDR 打开时亮度跳变，失焦后也不复位

源码事实：修复前生产渲染固定提交 `progress=1` 的最终 HDR endpoint，并在首帧 presented 后把原生 layer opacity 从 0 一次性切到 1；窗口事件只记录 `WindowActivate` 时间，没有任何 `WindowDeactivate`→HDR presentation 状态。也就是说，像素端已没有 650/1800 ms 的手工 partial-headroom ramp，但可见合成端同样没有过渡或焦点状态机。

根因推断：把最终 EDR surface 在一个 transaction 中从完全不可见切为完全可见，足以解释打开时的单帧亮度阶跃；仅设置 `wantsExtendedDynamicRangeContent` 只是在请求 EDR，Apple 并未承诺它会替应用实现指定时长的渐亮/渐暗。失焦没有对应状态输入，则不可能主动平滑回到 SDR proxy。WindowServer 的私有 tone-mapping/面板亮度时序仍未公开。

唯一修复链：继续只生成最终 headroom 像素，不恢复 partial-HDR ramp；把 background、Metal 和 persistent HDR layer 归入一个 `presentationContainerLayer`，在完整 SDR proxy 上用 450 ms `CABasicAnimation(opacity)` 做合成级交叉淡入/淡出。反向切换从 `presentationLayer.opacity` 读取当前屏幕值并按剩余距离缩短时长，避免快速切换焦点时跳回端点。`WindowDeactivate`/应用非活动先提交 SDR proxy，再淡出；淡出期间保持 EDR，opacity 到 0 后才关闭 EDR。重新激活先开启 EDR再淡入。持久 surface 在首帧后仍无缝接管，zoom/pan/resize 不重启过渡。

### 6. HDR 导航按钮消失时没有过渡

源码事实：按钮的 `QPropertyAnimation` 始终在更新 `paintOpacity`；但修复前 native HDR overlay 收到的是 `requestedVisible ? paintOpacity : 0`。隐藏请求会先把 `requestedVisible` 置 false，因而第一个动画 tick 之前 native layer 已直接变为 0。SDR widget 直接在 paint 中读取 `paintOpacity`，所以仅 HDR 复现。

修复：native overlay 不再用目标可见性门控，出现和消失都逐 tick 接收实际 `paintOpacity`；目标状态只负责启动/反转动画和最终隐藏 Qt 命中 widget。

### 7. 关闭右键菜单后第一次拖动失效

源码事实：macOS 上 Qt 在右键按下时派发 `QContextMenuEvent`；旧桥接层随后又构造一个新的 `NSEventTypeRightMouseDown` 打开菜单，并在菜单关闭后直接向 view 派发合成 `rightMouseUp`。同一个物理手势因此被拆成 Qt/AppKit 两套不配对的 pointer stream。修复前实测菜单关闭后的第一次左拖不移动，额外左击一次才恢复。

根因推断：重复 down 和绕过 Qt 的 synthetic up 使 Qt 拖动状态与 AppKit tracking 状态失配，是“额外点击一次可恢复”的唯一与源码、时序和复测同时一致的解释；Qt 未公开其 Cocoa 插件的全部内部状态字段，所以具体卡住的私有字段不能断言。

修复：Qt 6.8+ 把应用的 context trigger 改为 `Release`，让真实 `rightMouseUp` 先完整经过 Qt；随后用不接收 `NSEvent` 的 `NSMenu popUpMenuPositioningItem:atLocation:inView:` 打开菜单。Qt 5/6.7 fallback 在 `QGuiApplication::mouseButtons()` 仍含 RightButton 时每 8 ms 让出事件循环，观察到真实 release 后才打开同一原生菜单。两条路径都不构造、不重放、不直接派发任何鼠标事件，因此菜单 tracking 不可能再吞掉 Qt 尚未处理的 release。

## 完整数据流

### DNG / Adaptive-HDR RAW

`CGImageSourceGetType` 先按内容 UTI 判定 RAW。若 RAW 含 Apple/ISO gain map，则尝试：

`CIRAWFilter.previewImage (full resolution SDR)` → `CIImage auxiliary gain map` → `applyingGainMap(headroom)` → ColorSync extended-linear Display P3 → 首帧 Metal `RGBA16Float` → persistent half-float Core Animation layer（或超预算 Metal 回退）。

只有 base、gain map、extent 和 headroom 合同全部成立时才发布 `camera-raw-processed-gain-map`；否则继续传统 RAW 路径，不把普通 embedded preview 冒充 HDR。

### NEF / CR3 / ARW / RAF 等传统 RAW

建立两个互不变异的 `CIRAWFilter`：SDR endpoint 使用 `extendedDynamicRangeAmount=0`，HDR endpoint 使用 1；两者都保留相机默认参数、full scale、非 draft。`CIContext` 启用 intermediate cache，两个 source endpoint 在 viewport transform 之前由 Core Image 管理。若 `contentHeadroom` 未报告，则用 `CIAreaMaximum` 对浮点 HDR endpoint 作一次约减，并把实际内容范围用于 CI/layer tag；显示潜力不能冒充内容 headroom。

解码器不支持某相机型号时才尝试普通 `previewImage`/内嵌 JPEG 作为明确标记的 SDR fallback；都失败则返回错误。LibRaw 暂不引入：当前样例和格式由 Apple RAW 支持，新增第二套 demosaic、相机色彩矩阵、镜头校正和打包维护不符合精益性；未来遇到 Apple 不支持的具体型号再评估独立可选 backend。

### 非 RAW HDR

Image I/O 解析方向、色彩空间、传输函数、Apple/ISO gain map 与 headroom。HDR candidate 使用 `kCGImageSourceDecodeToHDR` 和 `kCIImageExpandToHDR`，SDR/HDR URL recipe 都设置 `kCIImageCacheImmediately=YES`，避免首次局部 Metal ROI 决定易失源缓存。PQ、HLG、extended-range 与 gain-map 来源均进入 metadata；普通 SDR 文件继续走原有 Qt 图片路径。

### 色彩、首帧和显示适配

ColorSync 创建扩展线性 Display P3 工作/输出空间；CI context 使用 `kCIFormatRGBAh`。Metal layer 使用 `MTLPixelFormatRGBA16Float`，persistent layer 的 contents 也是 `RGBA16Float` CGImage；两者都设置 `wantsExtendedDynamicRangeContent=YES` 和自动 tone map。Metal 提交读取窗口所在屏幕 current/potential headroom；current=1、potential>1 时可 bootstrap 首个 EDR 帧，potential=1 时 target 固定为 1。持久 surface 保留 source content headroom，由 Core Animation/WindowServer 对当前显示器适配。

首个昂贵 endpoint 求值在 SDR proxy 或上一张 HDR surface 可见时预热；只有最终 headroom、prepared 且 drawable 几何匹配的帧真正 presented 后，presentation container 才从 opacity 0→1 平滑揭示。persistent surface 准备完成后在 container 内接管。应用不生成 SDR→partial-HDR 像素 ramp；Core Animation 负责 450 ms 合成过渡，WindowServer 负责设备相关的 EDR headroom 适配。失焦时 container 1→0 并在终点关闭 EDR，重新激活时反向执行；zoom/pan/resize 复用同一 source-space surface，不回切 SDR、不再次激活亮度。

## 测试与可观测性

正式顺序固定为 static → unit → integration → system。每条原子标准拥有唯一 ID，并记录目的、前置条件、输入、步骤、预期和后置条件。测试环境变量只启用观察/确定性 driver，不替换生产 decoder 或 renderer。

时间阈值包括：

- 48 MP JPEG/DNG 解码平均 ≤2500 ms、P99 ≤3500 ms、最大 ≤4000 ms、吞吐 ≥0.4 image/s；
- 首帧/超预算回退的 Metal submission 平均 ≤30 ms、P99 ≤120 ms、最大 ≤200 ms；其真实 present 和 request-to-presentation 仍只由 `addPresentedHandler` 验证；
- 8064×6048 JPEG/DNG 的 persistent raw surface 为 390,168,576 bytes，NEF 样例为 96,423,936 bytes；本机后台物化分别约 191/331/86 ms，硬上限为 512 MiB 与 16384 px；
- 无 verbose telemetry 的 120 Hz probe：48 个 pan sample ≤400 ms、compositor update ≥48、交互 Metal present=0、最后一次 geometry update <1 ms，并要求最终 requested=submitted；当前 JPEG 两次实测为 384–385 ms、50、0、约 0.08–0.10 ms；
- Instruments 必须同时观察到内建显示 `maximum refresh rate=120`、连续 VSync interval 约 8.33 ms，且交互窗口内 `hitches-ca-commit-interval`/`hitches-render-interval` 无记录。该证据证明应用有足够提交余量且系统以 120 Hz 合成，但不等同于光电仪器逐帧测量。

正式发布管线会把机器证据写入 `reports/evidence/`，并聚合为 `reports/test_evidence.json`、`reports/test_case_specification.json`、`reports/test_completion_report.json` 和 `reports/code_quality_assessment_report.json`。

## 事实、推断与不确定性

- 事实：pre-fix/working-tree 源码差异、样例 metadata、RAW/gain-map 浮点探针、Quick Look 导出指标、CPU/GPU/present telemetry、120 Hz VSync、presentation-layer opacity、真实菜单前后 scrollbar 位移、screen crops 和时延原始样本均可机器审计。当前 persistent probe 的更新计数与 Metal present 计数分开，避免用 input callback 冒充 drawable 帧。
- 推断：约 40 ms 的窗口合成 drawable 占用与最多 3 个 drawable 共同构成旧路径的主要吞吐上限；跨 surface alien-widget backing、一次性 presentation opacity 阶跃、DNG 配方不匹配、旧 geometry queue 和重复右键 pointer stream 分别是其他症状的最佳根因解释。对应修复移除了各自的可审计必要条件。
- 不确定：Apple 不公开 Preview/Quick Look 的滚动 compositor 架构、私有 RAW tone recipe 或 Core Image 内部 tile/ROI scheduler；持久/分块 layer 与 Preview 行为吻合，但不能声称内部算法相同。
- 不确定：系统截图会被色调映射，不能单独证明绝对 nits 或捕获每个 8.33 ms 画面；VSync、无 hitch、几何 transaction、浮点峰值、headroom 和多时点截图互补，但仍不等同高速摄像/光电测量。
- 不确定：opacity 遥测证明应用提交了连续、单调且可逆的合成状态，但没有光电仪器就不能把它等同为面板绝对 nits 的逐帧线性变化；WindowServer 的环境光、tone map 和显示功耗策略仍可能改变感知曲线。
- 不确定与边界：完整 half-float surface 以准备时间和内存换取交互余量；当前进程在 48 MP JPEG 样例稳定后约 1.44 GiB RSS。超过 512 MiB/16384 px 的图像会回退到约 60 fps 的 viewport Metal 路径。若要在任意超大图上同时满足低内存与 120 Hz，应把 persistent backing 迭代为 `CATiledLayer` 或自有 IOSurface/Metal mip-tile cache。
- 不确定：本轮没有物理 SDR-only Mac；纯策略单元测试和 current=potential=1 的真实 renderer override 确定性覆盖 SDR 分支，仍建议发布前补一台 SDR Mac 的人工视觉巡检。
