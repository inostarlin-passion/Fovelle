# HDR信息页跨应用返回后的亮度：完整源端点与单一动态映射

日期：2026-10-17。用户复现图：/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG，内建屏幕，macOS27。保留之前焦点、timer和旧任务身份修复。

## 问题界定与迭代证伪

以前的测试只证明相同余量时缓存像素相同，没有覆盖系统余量在退化期间降低、返回后仍受当前内容需求影响的闭环。持久化CGImage先经应用按瞬时current headroom压缩，再由Automatic CALayer适配，存在双重适配：临时低余量会被烘焙进缓存，缓存又不能声明完整源HDR的需求。

真实1.JPG为8064×6048（方向变换后6048×8064），内容余量4.9473，半浮点缓存约390MiB，在既有512MiB预算内。诊断user-app.log观察到显示余量从5.52降至1.2、potential仍16；因此“potential大于1”和“图层opacity=1”不足以证明亮度恢复。

原子拆解：复核正常/降额像素与CGImage元数据 → 验证用户文件实际路径 → 区分Metal与persistent映射所有权 → 完整HDR源缓存 → 验证跨余量稳定性与真实源端点 → 旧路径回归。

## 多跳联网与多源交叉核验

[Apple当前EDR余量](https://developer.apple.com/documentation/appkit/nsscreen/maximumextendeddynamicrangecolorcomponentvalue)说明值随屏幕上的EDR需求改变；[potential说明](https://developer.apple.com/documentation/appkit/nsscreen/maximumpotentialextendeddynamicrangecolorcomponentvalue)明确它不是当前可用余量。[WWDC EDR](https://developer.apple.com/videos/play/wwdc2022/10113/)核对动态余量与系统映射的关系。

[Apple HDR图像渲染](https://developer.apple.com/documentation/coregraphics/adopting-advancements-in-hdr-image-rendering)说明CGImage内容余量和preferredDynamicRange；[CALayer.contentsHeadroom](https://developer.apple.com/documentation/quartzcore/calayer/contentsheadroom)与SDK CALayer.h互证：图像本身的内容标签可供系统解释，preferredDynamicRange=High针对有HDR内容标签的图像。实际余量不应被误当成源图像的内容上限。

[CIImage.contentHeadroom](https://developer.apple.com/documentation/coreimage/ciimage/contentheadroom)及[CGImage.contentHeadroom](https://developer.apple.com/documentation/coregraphics/cgimage/contentheadroom)核对滤镜标签传播与不可变图像元数据复制。SDK CGImage.h提供macOS15+的CGImageCreateCopyWithContentHeadroom。部分网页正文依赖JS，使用官方搜索正文、SDK注释和实际CALayer.contents读取交叉核验。

反证结果：正常仓库JPEG/RAW及用户1.JPG的HDR标签并非都缺失；layer.contentsHeadroom=0在CGImage已有正确标签时合法，最初该断言已撤回。部分降额端点也会正确标为较低值，例如用户图从4.9473变为2.97365；这恰好证明亮度已经被烘焙成低余量版本。RAW的SDR边界还会丢失明确的1标签，但只补标签仍不能解决映射所有权问题。不使用potential强行增亮、不修改屏幕亮度、不停用系统自适应、不把AX截图当作物理HDR亮度测量。

## 原子验收与方案

| ID | 标准 |
|---|---|
| A1 | macOS15+ persistent缓存保持完整HDR源像素，并明确标记source内容余量 |
| A2 | 信息页失活及返回时current变低，不能将该临时余量烘焙进persistent缓存；同一图的缓存像素/身份保持稳定 |
| A3 | persistent图层采用系统Automatic映射；Metal/旧系统回退仍执行现有current余量策略，不以potential冒充current |
| A4 | 元数据随任务捕获，serial/generation、预算、换图清理保持；无期望外反复重建 |
| A5 | 用户1.JPG及仓库JPEG/RAW通过，既有真实端点保真、焦点/信息页/机型回归通过 |

macOS15+：准备persistent surface时直接物化preparedHDRImage完整源端点，不再物化按瞬时current压缩的hdrBrightnessDisplayImage结果；捕获已解析的source contentHeadroom，CGImageCreateCopyWithContentHeadroom显式标记。macOS26+安装时从该surface自身同步layer.contentsHeadroom。Automatic与preferredDynamicRange High保留，系统依据真实显示条件负责最终映射。

monitor遇到有效persistent缓存/任务时只更新当前余量诊断，不因current波动销毁重建完整源缓存。没有persistent缓存的Metal路径，以及较旧系统，保留应用侧current适配和此前旧任务身份修复。已完成图层安装时诊断target/contents headroom改为实际surface内容标签，避免继续显示旧Metal目标。

## 正确性论证

令S为完整源HDR端点、Hs为解析后的source内容余量。现代persistent任务产生P=materialize(S)，标记H(P)=Hs；P不含当前显示余量C。隐藏/恢复只改变呈现范围和opacity，C变化不会改写P或H(P)，因此不会把临时后台低余量固化为新的图像内容上限，A1/A2成立。系统Automatic映射以完整内容和实际显示条件适配；没有要求输出超出屏幕当前能力，A3成立。

Hs在提交时按值捕获，元数据复制不修改像素；安装仍核对serial及图片generation，因此异步回调不能取其他帧的全局目标来标记像素。预算和CF所有权保留，换图清理不变，A4成立。monitor不为C变化重建同一个S；测试应核对缓存身份稳定，而不是沿用旧“current降低必须改写缓存像素”的错误抽象。实际tone mapping结果由系统负责，应用提供完整HDR信号与准确标签。

A5以原生图层、完整字节对比、源端点保真测试及用户文件回归验证。没有亮度计，不能宣称已测得用户屏幕恢复后的绝对nits或完全排除所有系统电源/亮度策略。
