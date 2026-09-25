# 技术设计文档：放大 AVIF 退出全屏时的画面跳变

日期：2026-09-25
问题样本：`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif`（AVIF，1200×1085）
生产代码：`src/mainwindow.cpp`、`src/qvgraphicsview.cpp`、`src/qvcocoafunctions.mm`
回归代码：`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`

## 1. 问题界定与原子化拆解

复现步骤限定为：打开给定 AVIF → 放大 → 进入 macOS 原生全屏 → 退出全屏。失败表现包括退出交接处短暂闪烁、图像位置跳变，或放大状态未保持。问题窗口是 AppKit 将全屏代理窗口交还真实 Qt 窗口的最后一次布局，而不是单纯检查最终 `isFullScreen()`。

| 验收 ID | 原子要求 | 可观察判据 |
| --- | --- | --- |
| AC-AVIF-LOAD | 指定 AVIF 成功进入应用实际采用的 SDR 图像路径。 | 图像加载成功，`isNativeSDRLoaded` 与 Metal SDR renderer 均为真。 |
| AC-AVIF-ZOOM | 转场前执行实际 2 倍缩放，转场期间保持缩放。 | `getZoomLevel()` 与 2.0 等价。 |
| AC-FS-END-SYNC | 退出全屏最后一次 viewport/pan 修正完成后，在真实窗口显示前同步提交 SDR layer 几何。 | `cancelFullScreenLayoutTransition()` 返回前 `compositorGeometryUpdateCount` 增加 1，且 `drawableGeometryMatches` 为真。 |
| AC-FS-VISIBLE | 转场采样中画面持续含有可辨内容。 | 屏幕采样满足中心纹理方差与彩色像素比例门槛。 |
| AC-FS-CONTINUOUS | 相邻采样帧不存在超过门槛的大幅位置跳变。 | 归一化颜色质心的单步距离不超过 0.14。 |
| AC-FS-ANCHOR | 退出后放大图的可用视口中心仍对应全屏时相同场景点。 | 在考虑标题栏遮挡的 usable viewport 中，场景坐标偏差不超过 2 个图像坐标单位。 |
| AC-FS-ROUNDTRIP | 真实全屏进入/退出路径重复可用。 | 连续三轮全屏往返全部满足上述画面与几何断言。 |

## 2. 多跳联网检索与多源交叉验证

检索由系统全屏回调、Qt 定时器事件顺序、Core Animation 图层提交三个平台契约逐跳进行，并与仓库调用链及本机回归结果交叉验证。

1. **AppKit 回调边界。** Apple 的 [`NSWindowDelegate` 全屏和自定义动画方法](https://developer.apple.com/documentation/appkit/nswindowdelegate?changes=_6)列出退出自定义动画回调；[`customWindowsToExitFullScreen(for:)`](https://developer.apple.com/documentation/appkit/nswindowdelegate/customwindowstoexitfullscreen%28for%3A%29)说明该回调用于定制退出动画。Apple 的 [`windowDidExitFullScreen(_:)`](https://developer.apple.com/documentation/appkit/nswindowdelegate/windowdidexitfullscreen%28_%3A%29?changes=_4)确认该通知表示窗口已经离开全屏。
2. **仓库中的实际顺序。** `src/qvcocoafunctions.mm` 在 `NSWindowDidExitFullScreenNotification` 处理器中先调用转场 handler 的 `Cancel` 阶段，随后调用 `revealFovelleFullScreenRealWindow(window)`。handler 通过 `Qt::DirectConnection` 同步进入 `MainWindow::cancelFullScreenLayoutTransition()`；Qt [线程与 QObject 文档](https://doc.qt.io/qt-6/threads-qobject.html)说明 Direct Connection 的 slot 在信号发出时立即调用。因此该函数返回前是准备真实窗口最后画面的确定交接点。
3. **异步更新不能满足交接前置条件。** Qt [`QTimer`](https://doc.qt.io/qt-6/qtimer.html) 明确说明 0ms timer 与其他事件源的先后次序未指定。`QVGraphicsView::requestHDRRendererUpdate()` 使用 0ms `hdrFrameRequestTimer` 排队普通 SDR 几何更新；故在最后一次 viewport 修正之后仅请求更新，不能证明它已在真实窗口显示前执行。
4. **可同步观察的提交边界。** Apple [`CATransaction`](https://developer.apple.com/documentation/quartzcore/catransaction?language=_1)说明 layer-tree 变更可通过显式事务提交。仓库 `updatePersistentSDRTileGeometry()` 对 layer bounds/transform 执行 `CATransaction begin/commit`，并增加 `compositorGeometryUpdateCount`。`synchronizeNativeSDRGeometryForFullScreenTransition()` 停止排队 timer 并立即调用 renderer 更新，因此该计数可直接验证同步提交发生在回调返回之前。
5. **交叉验证结论。** 平台资料仅确立回调、timer、layer transaction 的契约，不单独证明用户一定会看到闪烁。实际根因由本地代码时序和逆向变更实验确认：移除退出收尾同步后，同一 AVIF、同一测试路径在退出收尾断言处得到计数 `4`，预期 `5`，稳定失败；恢复同步后该专项连续 5 次通过。真实屏幕采样及退出场景锚点另行检查视觉和位置结果。

## 3. 严格推导与逆向证伪

1. 最后一次动画 `Update` 已同步更新当时 viewport 的 SDR layer 几何。
2. AppKit 完成退出时，仓库仍会先调用 Qt 的 `Cancel` handler，再揭示真实窗口。
3. `MainWindow::cancelFullScreenLayoutTransition()` 在此边界可能清除 titlebar overlap 并调用 `fitOrConstrainImage()`；之后 `endFullScreenPanPreservation()` 还会恢复滚动位置。两步都可能改变 layer 使用的 viewport corners。
4. 修改前该收尾路径未同步刷新 SDR layer；几何更新只会进入普通的 0ms timer 路径。由 Qt 定时器契约可知，不能据此推出它先于 AppKit 的真实窗口揭示完成。
5. 所以回调返回时真实窗口可能携带上一个 viewport 的 layer transform；这是产生退出首帧闪烁/偏移的明确竞态条件。
6. 修复在 pan/anchor 收尾完成后调用 `synchronizeNativeSDRGeometryForFullScreenTransition()`，把最新 geometry 在 `Cancel` 返回前提交。该调用对非原生 SDR 图像不做更新（生产 helper 内有路径守卫）。
7. 逆向证伪移除这一调用：专项断言实际计数 4 而期望 5，证实测试能识别该缺失。恢复后计数断言、真实屏幕采样、场景锚点断言和 15 项 CTest 均通过。

测试第一次使用 `viewport()->rect().center()` 作场景锚点时出现失败；核对 `QVGraphicsView::getUsableViewportRect()` 及 `MainWindow::getViewportPosition()` 后确认实际锚点需排除被标题栏遮挡的上沿。测试 oracle 已修正为 usable viewport center。此修正只校正测量定义；退出提交遗漏由独立计数断言及逆向实验判定。

## 4. 设计与实现

- 在 `MainWindow::cancelFullScreenLayoutTransition()` 中先执行最终 pan preservation 收尾，再同步更新 native SDR layer 几何，最后才返回 AppKit handler。
- 把回归样本改为用户给定的 `1.avif`，并在全屏前显式调用 2 倍缩放。
- 扩充 Cocoa 集成测试：用真实窗口和真实 AppKit 全屏进出三轮；每 8ms 采集屏幕；采样前缩为 360×240；检查中心局部亮度方差、有色内容像素比例、逐帧颜色质心位移与 Metal drawable 几何。
- 对最终交接增加直接契约检查：调用收尾回调前后比较 compositor geometry 计数，要求精确增加一次并确认 `drawableGeometryMatches`。随后检查每轮退出时 zoom 为 2.0，usable viewport center 的 scene point 偏差不大于 2。
- CMake 专项在指定外部样本存在时注册，`FOVELLE_FULLSCREEN_SDR_IMAGE` 可覆盖默认路径；仅 macOS Cocoa 测试执行该显示器采样用例。

## 5. 边界与信息缺口

- 屏幕采样间隔约 8ms，无法捕获两个采样之间的每一次 WindowServer/Core Animation scanout；对“任一刷新绝不闪帧”没有逐刷新级证据。
- 颜色质心阈值用于发现大幅跳位，不是逐像素图像配准，也不证明低于阈值的细小运动不存在。
- 专项依赖挂载卷中的 AVIF 和可见 macOS 桌面会话；无样本或无桌面的构建机不会得到同等显示器采样证据。
- 结论限于该 AVIF 的 native SDR Metal 全屏退出路径；没有从本样本推断 HDR、非 macOS、其他图像解码路径或多显示器行为。
