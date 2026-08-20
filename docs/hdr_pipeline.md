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

## 本次黑带、局部帧、残影与无 HDR 的根因和修复

修复前 JPEG 稳态截图中存在 80～243 个连续近黑列。与此同时 telemetry 已经报告 `hdr_prepared=true`、`transition_progress=1`、drawable geometry 匹配且 layer opacity 为 1，排除了“空 drawable”或“过渡尚未完成”。基线代码把 SDR/HDR 端点写入两张应用自有 `MTLStorageModePrivate` texture，随后通过 `imageWithMTLTexture` 把这些 render-target 再作为后续 Core Image 输入。移除该反向导入后，放大/拖动对照实验又进一步隔离出第二个条件：强制 SDR 的相同几何始终完整，只有延迟解码的 adaptive-HDR recipe 在局部 ROI 求值时丢失区域；加入 `kCIImageCacheImmediately=YES` 后，HDR 对照也连续完整。最终实现让两个源 recipe 在初始化时进入非易失缓存，再在源图空间插入 `imageByInsertingIntermediate:YES`，最后才针对每一代 viewport 做变换、裁剪和不透明整帧合成。私有 RGBA16Float texture 仅用于串行预热，永不被反向导入。RAW/自适应 HDR 的首次全分辨率求值也不再作为可见首帧：完整 SDR/HDR 端点和代表性过渡状态均预热完成后，Metal layer 才从 opacity 0 切到 1。

DNG 的“完整→只剩左上角→完整”有两段原因。定时截图证明：初始完整图是 Qt proxy；随后 drawable 尺寸和仿射几何虽正确，但未预热的全分辨率 `CIRAWFilter` 图只出现已经解析的左上角区域；准备完成后才恢复完整。因此首次 RAW/自适应 HDR 求值现在严格位于可见交接之前。缩放、拖动时的残影则来自另一层生命周期：Qt viewport 已移动，独立 `CAMetalLayer` 仍可能展示/排队旧仿射矩阵。现在稳定合同包括 viewport size 与全部四个映射角点；任一变化立即使 presentation generation 失效、清除该几何的 prepared cache、将 Metal opacity 设为 0，并让 Qt proxy 成为唯一可见内容。几何连续 34 ms 不变且新几何预热完成后才提交可见帧，实际呈现回调后再隐藏 proxy。`CAMetalLayer.autoresizingMask` 同时跟随 native view resize。每帧还会先以当前窗口背景不透明写满整个 drawable，再绘制 HDR 图，避免 drawable 池复用时透明区露出底层 proxy 或旧位置。

“全程无 HDR”还有独立原因：基线以动态 current headroom>1 决定是否准备/输出 HDR，但 Apple 说明 current 可能在没有 EDR 内容时保持 1，形成“先有 HDR 才得到 headroom、先有 headroom 才输出 HDR”的循环。现在 `displayHeadroomForRendering` 在 current=1 且 potential>1 时，以 `min(content,potential)` 启动首个 EDR 帧；一旦 current 升高便重新跟随动态 current。若 potential 也为 1，仍严格走 SDR。

用户两张截图、独立 JPEG 稳态帧、修复前 telemetry 和基线/当前源码合同均哈希到 `reports/evidence/intermediate/hdr_root_cause_before_fix.json`。这些结论分为可直接复核的事实和基于对照实验的推断；Apple GPU 内部 tile 解析细节仍属于不确定性。

系统截图测试不假定应用独占桌面：opt-in telemetry 同时记录 viewport 的全局逻辑坐标、尺寸与 device-pixel ratio，像素断言先换算并裁剪到被测 viewport，再计算近黑连续列和停止交互后的边缘结构相似度。第一次全桌面比较受到后台视频污染的失败记录被保留为中间证据，未被当作最终通过证据。

这不是把 RAW 降级成 SDR proxy：proxy 只负责首帧连续性；正式内容、预热端点和最终 surface 始终是 Core Image/Metal 浮点链。提供的 DNG 通过 `CIAreaMaximum` 的 RGBAf 探针测得 SDR 峰值约 1.000、EDR 峰值约 1.350，证明 EDR 图中存在超过 SDR white 的真实数值。

## 实现数据流

### RAW

`CGImageSource` 先按内容 UTI 判断 `public.camera-raw-image`，而不是按扩展名猜测。主路径随后创建 `CIRAWFilter`，分别在 `extendedDynamicRangeAmount = 0` 和 `1` 时捕获 SDR 与 EDR `CIImage` 图。两者保持惰性、高精度且全分辨率，EDR 图是显示主输入；只有单独的 SDR 回退代理会被限制到 2048 像素。

若 Apple RAW 解码器不支持某相机型号，才尝试 `previewImage` 或 Image I/O 内嵌预览，并显式记录 `usedRawPreview`；两者都不可用时返回可审计错误。因此预览不会冒充传感器 RAW HDR。

### 非 RAW HDR

Image I/O 解析 UTI、方向、Apple HDR gain map、ISO gain map、色彩空间、传输函数和解码 headroom。候选 HDR 使用 `kCGImageSourceDecodeToHDR` 探测，并用 `CIImage` 的 `kCIImageExpandToHDR` 重建完整 HDR 图；SDR/HDR 两个 URL recipe 都设置 `kCIImageCacheImmediately=YES`，使主图和 gain map 在局部 Metal ROI 请求前进入非易失解码缓存。SDR companion 图仅供 SDR 回退/过渡。PQ、HLG、extended-range 和 gain-map 来源均有明确元数据。

### 色彩与显示

ColorSync 的 Display P3 profile 用于建立扩展线性 Display P3 工作/输出空间；Core Image 的 Metal context 使用 `kCIFormatRGBAh`。最终 surface 是 `CAMetalLayer` 的 `MTLPixelFormatRGBA16Float`，并设置 `wantsExtendedDynamicRangeContent = YES`。每次绘制读取窗口所在 `NSScreen` 的当前与潜在 EDR headroom。

非 RAW HDR 在 macOS 15+ 使用 `CIToneMapHeadroom` 映射内容 headroom 到渲染目标；RAW 在 SDR/EDR 两个原生 RAW 图之间平滑过渡。650 ms smoothstep 内容过渡与约 1.8 s 的 headroom 轮询允许 WindowServer 自身的亮度协商继续完成。当 current 仍为 1 而 potential>1 时先用潜在能力 bootstrap；当 potential=1 时目标严格为 1，系统得到 SDR tone-mapped 图。`FOVELLE_TEST_DISPLAY_HEADROOM=1` 同时固定 current/potential，确定性验证 SDR；`FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM=1` 只固定 current，用来验证 clean-start EDR。

缩放、滚动、90° 旋转、镜像和翻转仍由 Qt 场景变换决定；四个源图角点映射到 viewport 后，再转换为 Core Image 到 Metal drawable 的仿射矩阵。缩放、滚动或 resize 期间暂时只显示随 Qt 同步更新的 SDR proxy，34 ms 稳定后重新生成 Metal 缓存和仿射矩阵，避免独立 layer 的旧帧残留。

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
- 不确定：Apple 不公开 Quick Look 的全部 tone curve、曝光意图和过渡时序，因此“接近”不能解释为完全复刻。
- 不确定：物理 SDR Mac 不在本次硬件矩阵内。SDR 行为由纯策略单元测试与强制 current=potential=1 的真实 Cocoa/Metal 系统测试覆盖，但仍建议发布前在一台仅 SDR 的 Mac 上做人工视觉确认。
- 不确定：Apple RAW 支持按 macOS 版本和相机型号变化；格式族被支持不等于未来每个型号都能解码。代码会明确回退或报错，不会把预览声称为 RAW HDR。
- 不确定：屏幕截图会被系统色调映射，不能作为真实 EDR 峰值证据；它们只用于检测几何错误和近黑带。HDR 由 JPEG/DNG RGBAf 峰值、系统 headroom 与 surface telemetry 共同验证。
