# Fovelle RAW/HDR 浏览管线（v0.1.4）

## 目标与边界

Fovelle 在 macOS 上把 HDR 文件保持为 Core Image 的高精度惰性图像图，直到最终写入 Metal 的 16 位浮点 EDR surface。`QImage`/`QPixmap` 只作为复制、错误回退和无 Metal 环境的 SDR 代理，不是 HDR 主路径。

这里的“接近 Quick Look”指使用 Apple 公布的同类系统机制：Image I/O/Core Image 解码、ColorSync 色彩管理、Metal/Core Image 浮点渲染、`CAMetalLayer.wantsExtendedDynamicRangeContent`，以及 WindowServer 的显示 headroom 协商。Quick Look 的私有调色参数并未公开，因此不声称逐像素或逐时间完全相同。

## 可验证事实

1. Apple 在 [WWDC24: Use HDR for dynamic image experiences in your app](https://developer.apple.com/videos/play/wwdc2024/10177/) 中说明 `kCGImageSourceDecodeToHDR`/`kCIImageExpandToHDR` 可重建 gain-map/adaptive HDR，`contentHeadroom` 描述内容范围，`CIToneMapHeadroom` 可把内容映射到显示 headroom；该演讲也说明 macOS 15 的 Quick Look 和 Preview 已采用新的 HDR 图像支持。
2. Apple 在 [WWDC22: Display EDR content with Core Image, Metal, and SwiftUI](https://developer.apple.com/videos/play/wwdc2022/10114/) 给出的 Core Image/Metal EDR 配置包括 `RGBA16Float`、扩展线性 Display P3、`wantsExtendedDynamicRangeContent = true`，并要求在绘制时读取 `NSScreen.maximumExtendedDynamicRangeColorComponentValue`。
3. Apple 在 [WWDC21: Capture and process ProRAW images](https://developer.apple.com/videos/play/wwdc2021/10160/) 中将 ProRAW 描述为 scene-referred/可线性化 RAW，并展示 `CIRAWFilter.extendedDynamicRangeAmount = 1` 与 RGBA half-float EDR 输出组合。
4. Apple SDK 将 `CIRAWFilter.extendedDynamicRangeAmount` 定义为 0…2；1 表示默认 EDR 处理。`kCGImageSourceDecodeRequest`/`kCGImageSourceDecodeToHDR` 和 `kCIImageExpandToHDR` 从 macOS 14 可用，`CIImage.contentHeadroom` 与 `CIToneMapHeadroom` 从 macOS 15 可用。
5. 本机 Image I/O 实际声明 DNG、NEF、CR3、ARW、RAF、JPEG、HEIF/HEIC 和 AVIF 支持。验收时，提供的 JPEG 被识别为 `public.jpeg`，同时带 Apple 与 ISO gain map，重建后的内容 headroom 约 4.947；提供的 DNG 被识别为 `com.adobe.raw-image`，并由 `CIRAWFilter` 生成全分辨率输出。
6. Apple 将 [`CAMetalLayer.drawableSize`](https://developer.apple.com/documentation/quartzcore/cametallayer/drawablesize) 定义为 drawable texture 的像素尺寸；[`MTLDrawable.addPresentedHandler`](https://developer.apple.com/documentation/metal/mtldrawable/addpresentedhandler(_:)) 则在 drawable 实际呈现后回调。本实现分别用实际 texture 尺寸计算坐标，并以 presented handler 控制首帧交接。
7. Apple 对 [`NSScreen.maximumExtendedDynamicRangeColorComponentValue`](https://developer.apple.com/documentation/appkit/nsscreen/maximumextendeddynamicrangecolorcomponentvalue) 的说明明确指出：没有 EDR 内容在屏幕上时，当前值可能保持为 1；[`maximumPotentialExtendedDynamicRangeColorComponentValue`](https://developer.apple.com/documentation/appkit/nsscreen/maximumpotentialextendeddynamicrangecolorcomponentvalue) 才描述显示器潜在能力。因此首个 EDR 帧不能以 current>1 作为先决条件。
8. Core Image 的 [`imageByInsertingIntermediate`](https://developer.apple.com/documentation/coreimage/ciimage/insertingintermediate(cache:)) 可显式插入由 Core Image 管理的缓存 intermediate。这里使用它缓存两条浮点图，而不再把应用自有 Metal render-target 反向导入为后续 `CIImage` 输入。
9. Apple SDK 将 [`kCIImageCacheImmediately`](https://developer.apple.com/documentation/coreimage/ciimageoption/cacheimmediately) 的 `YES` 语义定义为：在初始化时尽可能解码到非易失缓存；`NO` 则延迟到 render 时的易失缓存。Gain-map JPEG 的 SDR/HDR recipe 均显式使用 `YES`，避免局部 ROI 首次求值决定源缓存内容。
10. Apple 在 [WWDC26: Develop in HDR with Core Image](https://developer.apple.com/videos/play/wwdc2026/305/) 中把交互式 RAW 编辑与一次性导出明确区分：交互视图复用一个 `CIContext` 并启用 `cacheIntermediates=true`；`false` 适合只渲染一次的导出。本实现属于前者。
11. Apple 将 [`CIRAWFilter.baselineExposure`](https://developer.apple.com/documentation/coreimage/cirawfilter/baselineexposure) 定义为随相机变化的 baseline exposure，并说明零表示 linear response。所给 DNG 的系统 RAW filter 默认值实测为 -0.961081；这不是显示器 headroom。
12. macOS 26 SDK 对 `CALayer.contentsHeadroom` 的注释说明它描述 layer 内容/`MTLDrawable` 所使用的 headroom，默认零表示未标记；它不是显示器潜在能力。因此在支持该 API 的系统上，本实现给 layer 写入内容与当前显示可用范围的较小值，而不是内建 XDR 的潜在 16×；较早系统继续依赖扩展线性像素、layer EDR/colorspace 合同，并在 telemetry 中明确标记“无该 tag API”。

## 本次背景变化、RAW 空白、偏暗与交互重激活的根因和修复

背景变化是确定的双重颜色源错误。Qt painter 在 Light/Dark Theme 下分别填充 `#969696`/`#212121`，独立 Metal layer 却在创建和每帧合成时读取 `NSColor.windowBackgroundColor`；约两秒后 Metal 首帧揭示时，后者覆盖前者。`QVGraphicsView::settingsUpdated` 又没有把新主题通知 renderer，所以 Dark Theme 也无法改变已存在的 layer。现在 `Qv::viewportBackgroundColor` 是唯一合同：Qt 和 Metal 都从该函数取色，Metal 把 QColor 明确标为 sRGB，再由 ColorSync 转到扩展线性 Display P3；主题更新通过 `setBackgroundColor` 禁用隐式动画、重绘同一 HDR 图。系统像素证据中，揭示后的 Light 中位值保持 `(150,150,150)`，切换后两帧均为 `(33,33,33)`。

RAW 空白来自三项可直接审计的高风险合同叠加：基线从同一个惰性 `CIRAWFilter` 先读取 amount 0 图、再变异为 amount 1 读取 HDR 图；交互 renderer 又显式设置 `kCIContextCacheIntermediates=@NO`；已准备端点还把 viewport size、texture size 和四角当成有效性条件，缩放/移动会清掉昂贵 RAW 源并重新求值。Apple 不公开内部 tile scheduler，所以“具体哪条 GPU tile 竞争”仍是推断；但可变图、一次性 cache 策略和几何绑定均是源码事实。修复后 SDR/HDR 使用两个独立 filter，交互 context 开启 cache，两个源空间端点各插入 `imageByInsertingIntermediate:YES`，且这些端点不再依赖 viewport 几何。首次昂贵求值仍在 Metal 揭示前完成；揭示后 zoom/pan 只更新仿射与完整 drawable，不清源 cache。3.6～6.2 秒的十帧把图像划为 8×6 tile，所有帧缺失结构 tile 数均为零。

偏暗也有两项独立事实。第一，所给 DNG 的 `CIImage.contentHeadroom` 是零（未知），旧路径直接传递零；显示层若用潜在显示能力代替内容范围，系统会把实际约 1.35× 的像素误解为 16× 内容并压缩。现在先以 `CIAreaMaximum` 在 float 图上实测，再把结果同时标到 macOS 26 的 `CIImage` 与 `CAMetalLayer`；target 始终是 `min(content, displayRendering)`。第二，所给相机默认 `baselineExposure=-0.961081` 会保留接近一档的 SDR highlight protection。独立探针中，默认 baseline + EDR amount 1 的峰值为 1.349609375；仅 HDR filter 改为 linear-response baseline 0 后为 1.83203125，SDR companion 仍为 1。这个公开 API 组合明显接近用户所见的更亮系统 RAW 预览，但 Quick Look 的私有曝光意图未公开，不能声称逐像素一致。

交互时“先暗后亮”是状态机直接造成的：旧 `stageHDRGeometry` 对每一次 zoom/pan 都执行 `hdrTransitionClock.invalidate()`、`hdrTransitionLinearProgress=0`、显示 SDR proxy 并失效 renderer generation，相当于把“打开图片”重放一遍。现在只有 `beforeLoad`/`postLoad` 为新图像重置激活；一旦首帧已呈现且端点已准备，几何变化复用同一 presentation，保持 `opacity=1`、fallback=false 和 progress=1。34 ms debounce 仅合并几何事件，不再控制亮度生命周期。

此前 JPEG 黑带修复仍保留：gain-map 两个 recipe 在初始化时进入非易失缓存，源空间 float intermediate 由 Core Image 管理，应用自有 scratch texture 永不反向导入为 `CIImage`。每帧先以共享主题色不透明写满 drawable，再合成图像。首个 Metal drawable 只有在完整预热并实际 presented 后才取代 Qt SDR proxy。

两轮根因材料分别哈希到 `reports/evidence/intermediate/hdr_root_cause_before_fix.json` 与 `reports/evidence/intermediate/hdr_root_cause_background_raw_reactivation_before_fix.json`。后者含样例/用户空白截图哈希、HEAD 基线源码合同、可重复 Swift RAW 探针命令和事实/推断/不确定性分类。系统截图测试按 telemetry 给出的 viewport 与 image corners 裁剪，避免桌面其他窗口污染。

这不是把 RAW 降级成 SDR proxy：proxy 只负责首帧连续性；正式内容、预热端点和最终 surface 始终是 Core Image/Metal 浮点链。当前 DNG 的生产 decoder 与独立 integration probe 均得到 SDR 峰值 1、HDR 峰值/内容 headroom 1.83203125。

## 实现数据流

### RAW

`CGImageSource` 先按内容 UTI 判断 `public.camera-raw-image`，而不是按扩展名猜测。主路径创建两个独立 `CIRAWFilter`：SDR filter 保留相机默认并设 `extendedDynamicRangeAmount=0`；HDR filter 只在 baseline 为负时设 `baselineExposure=0`，再设 amount 1。两者保持惰性、高精度且全分辨率，EDR 图是显示主输入；只有单独的 SDR 回退代理会被限制到 2048 像素。若 RAW 图不报告 content headroom，生产 decoder 约减 float HDR 图的最大 RGB 分量并附上该标签。

若 Apple RAW 解码器不支持某相机型号，才尝试 `previewImage` 或 Image I/O 内嵌预览，并显式记录 `usedRawPreview`；两者都不可用时返回可审计错误。因此预览不会冒充传感器 RAW HDR。

### 非 RAW HDR

Image I/O 解析 UTI、方向、Apple HDR gain map、ISO gain map、色彩空间、传输函数和解码 headroom。候选 HDR 使用 `kCGImageSourceDecodeToHDR` 探测，并用 `CIImage` 的 `kCIImageExpandToHDR` 重建完整 HDR 图；SDR/HDR 两个 URL recipe 都设置 `kCIImageCacheImmediately=YES`，使主图和 gain map 在局部 Metal ROI 请求前进入非易失解码缓存。SDR companion 图仅供 SDR 回退/过渡。PQ、HLG、extended-range 和 gain-map 来源均有明确元数据。

### 色彩与显示

ColorSync 的 Display P3 profile 用于建立扩展线性 Display P3 工作/输出空间；Core Image 的 Metal context 使用 `kCIFormatRGBAh`。最终 surface 是 `CAMetalLayer` 的 `MTLPixelFormatRGBA16Float`，并设置 `wantsExtendedDynamicRangeContent = YES`。每次绘制读取窗口所在 `NSScreen` 的当前与潜在 EDR headroom。

非 RAW HDR 在 macOS 15+ 使用 `CIToneMapHeadroom` 映射内容 headroom 到渲染目标；RAW 在两个独立原生 RAW 图之间平滑过渡。650 ms smoothstep 只在新图片打开后执行一次；约 1.8 s 的 headroom 轮询允许 WindowServer 自身协商继续完成。当 current 仍为 1 而 potential>1 时先用潜在能力 bootstrap；当 potential=1 时目标严格为 1，系统得到 SDR tone-mapped 图。`FOVELLE_TEST_DISPLAY_HEADROOM=1` 同时固定 current/potential，确定性验证 SDR；`FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM=1` 只固定 current，用来验证 clean-start EDR。

缩放、滚动、90° 旋转、镜像和翻转仍由 Qt 场景变换决定；四个源图角点映射到 viewport 后，再转换为 Core Image 到 Metal drawable 的仿射矩阵。首个 prepared/presented 帧之前，34 ms 稳定 gate 与 SDR proxy 隔离未完成求值；之后 zoom/pan/resize 始终保留 HDR layer 与 source intermediates，只为当前完整 drawable 计算新仿射，不重新执行激活渐亮。

## LibRaw 评估

当前版本不引入 LibRaw。这是工程推断而不是 Apple 的事实：目标格式在被测 macOS Image I/O 中均已声明支持，Apple RAW 路径还能直接保留系统相机配置、ColorSync/Core Image 图和 EDR 行为；额外引入 LibRaw 会增加相机色彩矩阵、去马赛克、降噪、镜头校正、打包和许可维护面。若未来遇到 Apple 不支持且无可用预览的相机，应把 LibRaw 作为独立可选 decoder backend 评估，而不是静默改变本版本的色彩含义。

## 性能测试合同

目标工作负载是两张约 48 MP 的提供样例，在内建 Liquid Retina XDR/M3 Pro 上各冷/暖启动 3 次。解码窗口从请求开始到 HDR 图与 SDR proxy 就绪；渲染稳态窗口要求 `render_count > 3` 且 `hdr_prepared=true`，明确排除首帧与离屏端点准备。验收阈值为：

- 解码平均 ≤ 2500 ms、P99 ≤ 3500 ms、最大 ≤ 4000 ms、吞吐量 ≥ 0.4 image/s；
- 稳态 render submission 平均 ≤ 30 ms、P99 ≤ 120 ms、最大 ≤ 200 ms、等效吞吐量 ≥ 33 submissions/s；
- 过渡采样吞吐量 ≥ 30 observed frames/s，任意相邻进度步长 ≤ 0.15。

阈值和测量窗口固定在测试代码中，JSON 同时保存原始样本、统计量和判定结果。

## 推断与不确定性

- 推断：潜在 headroom>1、layer 请求 EDR、RGBA16Float surface 与内容目标>1 共同证明应用提供了 WindowServer 可接受的 EDR 表示；current-only=1 的两种真实样例仍达到 HDR target，证明 bootstrap 不依赖另一个 EDR 客户端。肉眼亮度体验仍是设备、环境亮度和系统策略共同结果。
- 推断：独立 RAW 图、交互 context/source cache、几何无关端点与十帧零缺失结果共同支持“移除了定时相关 tile 故障”；Apple 未公开的具体调度路径不能直接证明。
- 推断：对负 baseline 只在 HDR 图上请求 linear response，并使用实测 1.832 headroom，是在公开 API 范围内对所给 DNG 更接近 Quick Look 亮度的最小策略。
- 不确定：Apple 不公开 Quick Look 的全部 tone curve、曝光意图和过渡时序，因此“接近”不能解释为完全复刻。
- 不确定：物理 SDR Mac 不在本次硬件矩阵内。SDR 行为由纯策略单元测试与强制 current=potential=1 的真实 Cocoa/Metal 系统测试覆盖，但仍建议发布前在一台仅 SDR 的 Mac 上做人工视觉确认。
- 不确定：Apple RAW 支持按 macOS 版本和相机型号变化；格式族被支持不等于未来每个型号都能解码。代码会明确回退或报错，不会把预览声称为 RAW HDR。
- 不确定：屏幕截图会被系统色调映射，不能作为真实 EDR 峰值证据；它们只用于检测几何错误和近黑带。HDR 由 JPEG/DNG RGBAf 峰值、系统 headroom 与 surface telemetry 共同验证。
