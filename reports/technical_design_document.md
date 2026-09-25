# 技术设计文档：macOS SDR 图片全屏交接稳定性

日期：2026-09-25
仓库：`/Users/inostarlin/code/Fovelle`
问题样本：`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/2.png`（PNG，4616×2924）
实现位置：`src/mainwindow.cpp`、`src/qvgraphicsview.{h,cpp}`
测试位置：`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`

## 1. 问题界定与原子化拆解

操作路径限定为：打开给定 SDR PNG → 图片已在显示器上可见 → 进入 macOS 全屏 → 检查代理窗口与原生 SDR Metal 图层交接。症状是图像在切换期间跳位或画面闪烁。测试需观察实际 Cocoa 全屏路径，而不能只验证最终窗口状态。

| 验收 ID | 原子要求 | 可观察结果 |
| --- | --- | --- |
| AC-SDR-FS-SYNC | 全屏布局回调返回前，持久 SDR 图层已提交当前视口的几何。 | 全屏回调前后 `compositorGeometryUpdateCount` 同步增加；`drawableGeometryMatches` 为真。 |
| AC-SDR-FS-VISIBLE | 真实 SDR 样本的过渡帧仍含有屏幕中心图像纹理及可见图像内容。 | 屏幕采样帧的中心纹理方差和颜色内容比例均高于下限。 |
| AC-SDR-FS-CONTINUOUS | 相邻过渡采样帧的可见图像颜色质心没有大幅跳变。 | 归一化质心单步位移不超过 0.14。 |
| AC-SDR-FS-ROUNDTRIP | 连续三轮进入/退出后窗口状态与 SDR 图层仍有效。 | 每轮均完成全屏进入、退出和可见画面检查。 |

## 2. 本地执行链与故障定位

1. `QVCocoaFunctions` 注册 `NSWindowDelegate` 的自定义全屏窗口和动画回调。代理窗口显示全屏过渡图层；真实窗口在动画终点预备并交接。
2. AppKit 动画回调经 `Qt::DirectConnection` 同步调用 `MainWindow::updateFullScreenLayoutTransition()`。该方法更新标题栏补偿、必要时重新 fit，再准备隐藏的真实窗口。
3. SDR 原生图层几何由 `QVGraphicsView::updateHDRRenderer()` 下发。持久 SDR 分支在 `HDRRenderer::render()` 中同步变更 Core Animation 图层变换；但普通视口绘制只调用 `requestHDRRendererUpdate()`，它启动零毫秒 `hdrFrameRequestTimer`。
4. 修复前，全屏回调只做 fit/repaint 并排队图层更新。代理动画在 AppKit 回调中继续推进时，零毫秒 Qt timer 的执行顺序没有保证；真实窗口可能在最新 SDR layer geometry 提交前被显示。
5. 修复在 `MainWindow::updateFullScreenLayoutTransition()` 的真实窗口仍隐藏期间调用 `synchronizeNativeSDRGeometryForFullScreenTransition()`。该方法停止已排队的 timer 并同步调用 `updateHDRRenderer()`，使当前视口几何先提交。

直接反向证伪结果：移除 MainWindow 对同步方法的调用后，指定样本回归在交接边界报告 compositor 几何提交数仍为 `2`，立即期望值 `3`，用例失败；恢复调用后用例通过。它验证的正是“布局回调返回前是否有同步提交”，不是只比较最终全屏状态。

## 3. 多跳联网检索与多源交叉验证

检索链从平台回调契约进入事件调度语义，再与本地渲染提交点交叉核对：

| 跳 | 外部一手资料 | 与本地实现交叉核对后的结论 |
| --- | --- | --- |
| 1：AppKit 全屏生命周期 | Apple 的 [`NSWindowDelegate` 全屏回调](https://developer.apple.com/documentation/appkit/nswindowdelegate?changes=_6)列出自定义代理窗口、开始自定义动画及进入/退出通知；自定义动画方法接收系统过渡时长。 | 本地 `customWindowsToEnterFullScreen` / `customWindowsToExitFullScreen` 创建代理窗口；`startFovelleFullScreenAnimation` 以 AppKit 进度驱动图层，完成通知再显示真实窗口。 |
| 2：Qt 与 AppKit 的调用时序 | Qt [`QTimer`](https://doc.qt.io/qt-6/qtimer.html)说明零间隔 timer 尽快触发，但它与其他事件源的先后顺序未指定。Qt [`DirectConnection`](https://doc.qt.io/qt-6/threads-qobject.html)则同步调用接收方法。 | 源码中 `hdrFrameRequestTimer` 是 0ms timer；AppKit bridge 对布局 slot 使用 `Qt::DirectConnection`。因此仅排队更新不能构成“返回前已提交”的保证。 |
| 3：图层提交边界 | Apple [`CATransaction`](https://developer.apple.com/documentation/quartzcore/catransaction?language=_1)将 layer-tree 操作分组成渲染树更新；显式事务通过 `commit()` 提交。 | 本地 `updatePersistentSDRTileGeometry()` 在事务中写 SDR layer 的 bounds/transform；回归所观测的 `compositorGeometryUpdateCount` 只在此几何更新路径增加。 |
| 4：本地反向验证 | 以上 API 契约没有声称某一具体机器必然丢帧。 | 用同一 PNG、同一 QtTest 路径移除/恢复同步调用，得到 `2 ≠ 3` 的确定失败/成功对照；再以实际屏幕帧采样和五次重复执行检查可见性与重复性。 |

推导边界：外部资料证明 timer 顺序不确定、DirectConnection 同步、Core Animation 有事务提交边界；它们本身不能证明某一次用户画面确实闪烁。故根因依据还包括本地调用链和修复前测试反向对照。测试没有捕获 WindowServer 或 Core Animation presentation tree 的每一次扫描输出。

## 4. 设计与实现

- 只在文件是 native SDR 且 renderer 可用时同步刷新；普通栅格、矢量与 HDR 路径不走该同步方法。
- 同步刷新会停止已排队的零延时 SDR renderer 请求，再使用刚完成布局的 viewport size/corners 更新原生图层。后续排队请求仍可执行，但不会覆盖旧几何。
- 同步调用发生在 `viewport()->repaint()` 和 `MainWindow::repaint()` 前，且每次 AppKit 布局更新都执行，覆盖进入/退出动画中的多次 titlebar/viewport 几何变化。
- 全屏回归读取指定 PNG，触发三次完整往返；在全屏期间采集显示器帧，检查中心纹理、画面内容、逐帧颜色质心变化，并检查 renderer geometry 状态。
- CMake 仅在缓存路径指向的样本存在时注册 `FovelleSDRFullScreenPresentation`，避免缺少外接卷的构建机误报通过或失败。可以用 `-DFOVELLE_FULLSCREEN_SDR_IMAGE=/path/to/2.png` 指定副本。

## 5. 风险与证据限制

- 屏幕帧采样依赖 macOS Cocoa 桌面会话。无活跃显示器的 headless runner 不适用。
- 颜色质心是用于发现大幅画面位移的代理指标，不能代替逐像素配准；阈值有意只声称能检测较大单帧跳变。
- 测试样本是外接卷文件，构建机没有该文件时专项不会被注册。此处本机文件可读且已执行。
- 不推断测试之外所有 PNG、多个显示器切换、HDR 图像或系统降低动画效果时的行为。

执行结果见[测试完成报告](test_completion_report.md)，原子步骤见[测试用例说明](test_case_specification.md)。

## 附录：既有同步缩放验收追溯

本次全屏修复继续运行仓库已有的同步缩放与滚动条拓扑门禁，保留其文档追溯：

| 既有验收 ID | 既有合同 |
| --- | --- |
| AC-ZOOM-NO-ANIMATION-STATIC | 缩放路径不含几何动画 writer。 |
| AC-ZOOM-NO-ANIMATION-INPUT | wheel 缩放即时提交终态。 |
| AC-ZOOM-NO-ANIMATION-SHORTCUT | 键盘缩放即时提交终态。 |
| AC-ZOOM-NO-ANIMATION-MENU | 菜单缩放汇入共同 view API。 |
| AC-ANCHOR-MOUSE-PREFERRED | 有效鼠标位置优先作为缩放锚点。 |
| AC-ANCHOR-PROJECT-FEASIBLE | 锚点投影到目标几何可行域。 |
| AC-ANCHOR-NO-POST-CORRECTION | 缩放之后没有延迟位置修正。 |
| AC-ANCHOR-HBAR-TOPOLOGY | 横向滚动条拓扑变化不移动锚点。 |
| AC-VBAR-TOPOLOGY-ANCHOR | 纵向滚动条拓扑变化不移动锚点。 |
