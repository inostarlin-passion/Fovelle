# 隐藏标题栏进入全屏：技术设计文档

日期：2026-09-30（Asia/Shanghai）。问题范围：macOS Cocoa 窗口从隐藏标题栏的普通模式进入原生全屏时，标题栏闪现并造成图像瞬时缩小。

## 问题界定与原子化拆解

| 原子问题 | 可验证要求 | 观测方法 |
| --- | --- | --- |
| 标题栏闪现 | 用户隐藏标题栏后，进入请求及动画期间保持隐藏的原生呈现属性 | 独立读取 NSWindow 的 titleVisibility、titlebarAppearsTransparent 和关闭按钮 hidden |
| 视口受挤压 | 隐藏标题栏时，整个进入过程的有效顶部遮挡为 0 | 在同步布局事件与定时采样中读取 getViewportPosition().obscuredHeight |
| 图像短暂缩小 | 从较小普通窗口进入全屏时，适应高度的竖图不能先缩小 | 记录进入期间最小缩放比例与进入前比例 |
| 退出与持久化 | 退出后保持原来的标题栏状态及偏好，重复进入仍成立 | 原生 did-exit 通知、Qt 状态及 QSettings |
| 对照行为 | 原本可见的标题栏仍能正常进入、退出全屏 | PNG/SVG 可见状态对照用例 |

不把正常的全屏窗口扩大与图像重新适应视口视为缺陷；本次禁止的是人为恢复标题栏造成的额外顶部占用与缩小。

## 多跳检索与多源交叉验证

检索从“Qt macOS fullscreen titlebar full size content view”与“NSWindow fullscreen custom animation”开始，再沿文档中的全屏样式、透明标题栏、Qt 窗口标志和完成通知迭代检索。依据来自 Apple、Qt 官方文档、Qt 项目源代码及本地动态实验；同一厂商文档的重复搜索结果不算独立来源。

| 跳次 | 信息与来源 | 交叉验证及设计结论 |
| --- | --- | --- |
| 1：全屏定义 | [Apple fullScreen](https://developer.apple.com/documentation/appkit/nswindow/stylemask-swift.struct/fullscreen) 说明原生全屏不绘制标题栏；[Qt QWindow](https://doc.qt.io/qt-6/qwindow.html) 同样定义全屏占据整屏且没有标题栏 | 两方定义一致；没有找到要求应用先恢复隐藏标题栏的依据 |
| 2：内容与标题栏关系 | [Apple titlebarAppearsTransparent](https://developer.apple.com/documentation/appkit/nswindow/titlebarappearstransparent) 与 full-size content view 配合；[Qt WindowType](https://doc.qt.io/qt-6/qt.html#WindowType-enum) 区分 ExpandedClientAreaHint 与 NoTitleBarBackgroundHint | 透明背景、扩展内容区和隐藏标题文字是不同属性；不能只用生产代码的 Qt 标志 getter 作为视觉状态判据 |
| 3：原生进入路径 | [Qt Cocoa 源码（6.8 分支）](https://github.com/qt/qtbase/blob/6.8/src/plugins/platforms/cocoa/qcocoawindow.mm) 的 toggleFullScreen 调用 NSWindow toggleFullScreen，并设置 FullScreenPrimary；will-enter 设置 Resizable | 历史实现用于解释平台责任边界，不冒充本机 6.11.2 的逐行源码。与本机 6.11.2 的成功原生进入实验交叉验证，支持保留隐藏标题栏并让 Qt/AppKit 管理全屏 |
| 4：动画与完成 | [Apple customWindowsToEnterFullScreen](https://developer.apple.com/documentation/appkit/nswindowdelegate/customwindowstoenterfullscreen%28for%3A%29) 允许自定义窗口动画；[Apple didEnterFullScreenNotification](https://developer.apple.com/documentation/AppKit/NSWindow/didEnterFullScreenNotification?changes=_7_1) 表示窗口已经进入全屏 | 项目已有代理窗口动画；测试必须采样原生完成通知之前的过程，不能只检查 Qt 请求状态 |
| 5：反证实验 | 旧实现配合新增测试连续失败；固定实现连续通过，原生完成通知确实到达 | 把文档推断收敛为本机可重复观察的因果证据 |

部分 Apple 页面只返回 JavaScript 提示，部分直接 JSON/raw URL 无法由浏览工具取得；因此迭代到官方搜索结果、相关官方文档与可读取的 Qt 源码，并用真实 Cocoa 执行验证，不将失败抓取当成证据。

## 链式演绎与根因

旧链路：`toggleFullScreen()` → 保存 `storedTitlebarHidden` → `setTitlebarHidden(false, false)` → 恢复原生标题文字、背景和按钮 → `fitOrConstrainImage()` → `showFullScreen()` → 原生代理动画。

由透明标题栏恢复为普通标题栏，`getObscuredHeight()` 从 0 增加到标题栏高度。可用高度下降，使适应视口的竖图缩小；这发生在原生动画抓取起点之前。旧代码虽然用 `storedTitlebarHidden` 强制代理标题栏 inset 为 0，却已经修改了真实窗口和图像起点，无法消除此前的闪现与挤压。

动态证据：800×1600 竖图进入前 zoom=0.32；旧实现进入期间最大顶部占用为 32，最小 zoom=0.30。相同现象同时出现在 PNG 与 SVG，排除单一栅格解码路径；可见标题栏对照正常，排除测试一律禁止标题栏的错误判据。0.32→0.30 对应图像高度少了 32 个逻辑像素，比例下降 6.25%。

## 逆向证伪

| 假设 | 证伪方式 | 结果 |
| --- | --- | --- |
| 必须恢复标题栏才能进入原生全屏 | 移除恢复操作，等待真实 did-enter/did-exit 通知 | 本机 Qt 6.11.2/Cocoa 可正常反复进入退出 |
| 是 PNG 解码/Metal 的专有问题 | 同尺寸 SVG 对照 | 旧代码同样失败；共同窗口路径更符合证据 |
| 只需检查最终全屏状态 | 对比同步事件及动画采样与最终状态 | 最终状态可以正确，而中间占用为 32、zoom 降低 |
| 隐藏 getter 与生产实现同错导致假通过 | 用测试独立 Objective-C++ 探针读取 AppKit 属性 | 旧实现明确暴露原生标题栏，固定实现保持隐藏 |
| 修复破坏可见标题栏或退出偏好 | 可见 PNG/SVG、双周期、偏好检查及邻接全屏测试 | 以测试完成报告中的实测结果为准 |

## 生产代码设计

`src/mainwindow.cpp` 的进入路径保留 pan preservation，直接调用 `showFullScreen()`，不再先改变标题栏。删除退出后的补偿隐藏逻辑；`fullScreenTransitionTitlebarOverlap()` 直接依据当前原生有效遮挡，不再依据临时缓存。`src/mainwindow.h` 删除 `storedTitlebarHidden`。已有 Cocoa 动画、Qt/AppKit 状态通知及用户偏好保存路径继续承担各自职责。

这使普通隐藏窗口、代理动画起点和退出终点使用一致的真实标题栏状态，无需通过延迟恢复、截图遮挡或定时器在生产代码中补偿。

## 测试设计与边界

在现有 `WindowBehaviorTests` 增加数据驱动回归；独立探针仅编入测试目标。CMake 注册串行 Cocoa 动态验收，防止本测试与其他 CTest 项同时操作原生全屏 Space。测试不依赖外部图片、OCR、屏幕录制权限或源码字符串匹配。

本次实测环境为 macOS 27.0 / arm64 / Qt 6.11.2。对项目最低支持的 macOS 15 和 Qt 6.9 之前分支仅能给出代码路径分析，不能声明已实测。采样验证原生属性、有效布局与图像缩放，不声称录制并逐帧审核显示器合成像素。

## 邻接测试修正

扩展验证发现 `testFullscreenAfterOverflowRemovesTitlebarScenePadding` 在原生动画未结束时缩放、退出并创建第二个窗口，首次组合执行发生退出超时，随后崩溃。该测试原先只等 Qt 全屏状态，与上文识别的请求/完成边界不一致。为两个窗口分别增加原生 did-enter/did-exit 计数等待，并增加 scope guard 清理；不改变其场景顶部与图像边缘断言，也不放宽超时。最终重复结果见测试完成报告。
