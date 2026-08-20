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

## 本次瞬态问题的根因与修复

预修复的三次系统记录显示：JPEG 首次 render 均耗时约 300–306 ms，但代码在 `setImage` 时就隐藏 Qt proxy 并显示尚无已呈现内容的 Metal layer，因此出现黑块。DNG 的首次 HDR 求值均阻塞约 1084–1103 ms，随后可观察的过渡进度从约 0.026–0.029 一步跳到 1；从竖幅 JPEG 导航到横幅 DNG 时，Qt 的同步 paint 又能在最终 fit 前提交旧缩放，从而让错误几何在该阻塞窗口内停留。用户截图、旧系统 telemetry 和对应源码状态均已哈希到 `reports/evidence/intermediate/hdr_root_cause_before_fix.json`。

修复采用三阶段展示协议：

1. `hdrLayoutReady` 在 `updateSceneRect`、滚动条调整和 `fitOrConstrainImage` 全部完成前保持 false，任何同步 `paintEvent` 都不能提前提交 Metal；
2. 全尺寸逻辑坐标对齐的 SDR proxy 保持可见，Metal layer 以 opacity 0 工作，只有目标尺寸 drawable 通过 `addPresentedHandler` 确认已上屏后才原子切换；
3. 首个可见 SDR Metal 帧之后，把 SDR 与 HDR 端点离屏渲染为两张 viewport 尺寸的 RGBA16Float texture。昂贵的 48 MP RAW 惰性求值在不可见阶段完成，650 ms 渐亮只在已缓存的浮点端点之间合成。

这不是把 RAW 降级成 SDR proxy：proxy 只负责首帧连续性；正式内容、预热端点和最终 surface 始终是 Core Image/Metal 浮点链。提供的 DNG 通过 `CIAreaMaximum` 的 RGBAf 探针测得 SDR 峰值约 1.000、EDR 峰值约 1.350，证明 EDR 图中存在超过 SDR white 的真实数值。

## 实现数据流

### RAW

`CGImageSource` 先按内容 UTI 判断 `public.camera-raw-image`，而不是按扩展名猜测。主路径随后创建 `CIRAWFilter`，分别在 `extendedDynamicRangeAmount = 0` 和 `1` 时捕获 SDR 与 EDR `CIImage` 图。两者保持惰性、高精度且全分辨率，EDR 图是显示主输入；只有单独的 SDR 回退代理会被限制到 2048 像素。

若 Apple RAW 解码器不支持某相机型号，才尝试 `previewImage` 或 Image I/O 内嵌预览，并显式记录 `usedRawPreview`；两者都不可用时返回可审计错误。因此预览不会冒充传感器 RAW HDR。

### 非 RAW HDR

Image I/O 解析 UTI、方向、Apple HDR gain map、ISO gain map、色彩空间、传输函数和解码 headroom。候选 HDR 使用 `kCGImageSourceDecodeToHDR` 探测，并用 `CIImage` 的 `kCIImageExpandToHDR` 重建完整 HDR 图；SDR companion 图仅供 SDR 回退/过渡。PQ、HLG、extended-range 和 gain-map 来源均有明确元数据。

### 色彩与显示

ColorSync 的 Display P3 profile 用于建立扩展线性 Display P3 工作/输出空间；Core Image 的 Metal context 使用 `kCIFormatRGBAh`。最终 surface 是 `CAMetalLayer` 的 `MTLPixelFormatRGBA16Float`，并设置 `wantsExtendedDynamicRangeContent = YES`。每次绘制读取窗口所在 `NSScreen` 的当前与潜在 EDR headroom。

非 RAW HDR 在 macOS 15+ 使用 `CIToneMapHeadroom` 映射内容 headroom 到当前目标；RAW 在 SDR/EDR 两个原生 RAW 图之间平滑过渡。650 ms smoothstep 内容过渡与约 1.8 s 的 headroom 轮询允许 WindowServer 自身的亮度协商继续完成。若当前显示 headroom 为 1，目标严格为 1，系统得到 SDR tone-mapped 图；测试专用的 `FOVELLE_TEST_DISPLAY_HEADROOM=1` 可确定性验证这条路径。

缩放、滚动、90° 旋转、镜像和翻转仍由 Qt 场景变换决定；四个源图角点映射到 viewport 后，再转换为 Core Image 到 Metal drawable 的仿射矩阵，所以 HDR 层与已有交互模型保持一致。

## LibRaw 评估

当前版本不引入 LibRaw。这是工程推断而不是 Apple 的事实：目标格式在被测 macOS Image I/O 中均已声明支持，Apple RAW 路径还能直接保留系统相机配置、ColorSync/Core Image 图和 EDR 行为；额外引入 LibRaw 会增加相机色彩矩阵、去马赛克、降噪、镜头校正、打包和许可维护面。若未来遇到 Apple 不支持且无可用预览的相机，应把 LibRaw 作为独立可选 decoder backend 评估，而不是静默改变本版本的色彩含义。

## 性能测试合同

目标工作负载是两张约 48 MP 的提供样例，在内建 Liquid Retina XDR/M3 Pro 上各冷/暖启动 3 次。解码窗口从请求开始到 HDR 图与 SDR proxy 就绪；渲染稳态窗口要求 `render_count > 3` 且 `hdr_prepared=true`，明确排除首帧与离屏端点准备。验收阈值为：

- 解码平均 ≤ 2500 ms、P99 ≤ 3500 ms、最大 ≤ 4000 ms、吞吐量 ≥ 0.4 image/s；
- 稳态 render submission 平均 ≤ 30 ms、P99 ≤ 120 ms、最大 ≤ 200 ms、等效吞吐量 ≥ 33 submissions/s；
- 过渡采样吞吐量 ≥ 30 observed frames/s，任意相邻进度步长 ≤ 0.15。

阈值和测量窗口固定在测试代码中，JSON 同时保存原始样本、统计量和判定结果。

## 推断与不确定性

- 推断：观察到 `maximumExtendedDynamicRangeColorComponentValue` 在 EDR layer 出现后逐步上升，同时 layer 请求 EDR、内容目标 headroom 逐步增加，足以证明应用与 WindowServer 完成 EDR 协商；肉眼亮度体验仍是设备、环境亮度和系统策略共同结果。
- 不确定：Apple 不公开 Quick Look 的全部 tone curve、曝光意图和过渡时序，因此“接近”不能解释为完全复刻。
- 不确定：物理 SDR Mac 不在本次硬件矩阵内。SDR 行为由纯策略单元测试与强制 headroom=1 的真实 Cocoa/Metal 系统测试覆盖，但仍建议发布前在一台仅 SDR 的 Mac 上做人工视觉确认。
- 不确定：Apple RAW 支持按 macOS 版本和相机型号变化；格式族被支持不等于未来每个型号都能解码。代码会明确回退或报错，不会把预览声称为 RAW HDR。
- 不确定：屏幕截图会被系统色调映射，不能作为真实 EDR 峰值的可靠证据；系统测试使用系统 headroom 与渲染 surface telemetry，而非对截图亮度作错误推断。
