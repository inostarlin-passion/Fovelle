# 全屏退出时连续平移位置保持：数学模型

## 1. 范围与显式前提

本模型描述 macOS 上 Fovelle 打开同一份位图、进入原生全屏、缩放、把图像
拖到“接近但尚未到达底部”、再退出全屏时的视口状态。复现输入为
`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/3.jpeg`，且应用使用当前的
滚轮缩放、左键平移和左键双击全屏设置。

原生复现器不使用 CUA、QtTest、Accessibility action、键盘操作或业务函数。
它使用 CoreGraphics 创建真实鼠标/滚轮事件，并用
`CGEventPost(kCGHIDEventTap, ...)` 投递到 Quartz HID 事件流；因此执行前提是
macOS Post Event 与 Accessibility 权限已经授权。相关 API 的定义见
[CGEvent](https://developer.apple.com/documentation/coregraphics/cgevent)、
[CGEventPost](https://developer.apple.com/documentation/coregraphics/cgeventpost)
和
[kCGHIDEventTap](https://developer.apple.com/documentation/coregraphics/cgeventtaplocation/cghideventtap?language=objc)。

窗口屏幕边界由当前会话的 CoreGraphics 窗口信息和 `kCGWindowBounds` 取得：
[CGWindowListCopyWindowInfo](https://developer.apple.com/documentation/coregraphics/cgwindowlistcopywindowinfo%28_%3A_%3A%29?changes=_9&language=objc)、
[kCGWindowBounds](https://developer.apple.com/documentation/coregraphics/kcgwindowbounds?changes=_6)。
Qt 的 `QGraphicsView::sceneRect` 决定可导航区域和滚动条范围，且窗口尺寸变化
会受到 `resizeAnchor` 规则影响：[QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)。
AppKit 的全屏进入/退出由 will/did 生命周期通知异步发布：[NSWindow](https://developer.apple.com/documentation/appkit/nswindow?changes=___1)。

## 2. 几何对象与滚动状态

设原始图像为

\[
I=[0,W]\times[0,H],\qquad W,H>0,
\]

设当前场景到视口的可逆变换为 `T`，场景矩形为 `S`，可用视口矩形为 `V`。
对每个轴定义：

- `R=[r_min,r_max]`：Qt 当前滚动条的离散整数范围；
- `v∈R`：当前滚动条值；
- `c(V)`：可用视口中心；
- `a=mapToScene(c(V))`：视口中心对应的 scene 坐标锚点；
- `b(T,S,v)`：图像底边在视口中的位置。

`a` 是连续平移位置的语义表示；它不依赖旧窗口的滚动条整数值。若重建后
范围变为 `R'`，则目标是不变地满足

\[
mapToScene_{new}(c(V'))=a,
\]

若该锚点因范围裁剪不可表示，则按新的合法范围夹紧。水平轴同理。

端点意图仍需要优先处理，因为端点有比中心锚点更强的用户语义。设端点容差
`τ=3`：

\[
E(v,R)=
\begin{cases}
\text{Minimum},&r_{min}<r_{max}\land v\le r_{min}+\tau,\\
\text{Maximum},&r_{min}<r_{max}\land v\ge r_{max}-\tau,\\
\text{None},&\text{otherwise}.
\end{cases}
\]

“接近底部但不在底部”的测试状态定义为

\[
NearBottom(v,R)\iff
r_{max}-v>\tau\land 0<r_{max}-v\le G,
\qquad G=160.
\]

其中 `G` 是测试轨迹的近底窗口，不把真正的 `Maximum` 伪装成内部位置。

## 3. 全屏保持器状态机

保持器状态为

\[
P=(active,e_x,e_y,a_x,a_y,q),
\]

其中 `active` 表示保持窗口几何的生命周期，`e_x,e_y` 是端点意图，`a_x,a_y`
是可选的 scene 锚点，`q` 属于 `Normal`、`Entering`、`FullScreen`、`Exiting`。

| 事件 | 前置 | 状态转移与动作 |
| --- | --- | --- |
| `begin` | 进入请求边界 | 停止延迟约束和滚动动画；捕获当前端点，并捕获可用视口中心的 scene 锚点；令 `active=true,q=Entering`。重复调用不覆盖已有进入快照。 |
| `rebuild(S',V')` | `active=true` | `setSceneRect()` 后先恢复端点到 `r'_min/r'_max`；没有端点时，按 `a_x/a_y` 将锚点重新置于新视口中心。 |
| `refresh` | 退出请求边界 | 停止延迟约束和滚动动画；丢弃旧快照，重新捕获全屏中最后一次拖动后的端点和 scene 锚点；令 `q=Exiting`。 |
| `end` | 全屏完成或失败 | 在最终范围再次恢复端点或锚点，然后清除 `active,e_x,e_y,a_x,a_y`，令 `q=Normal`。 |

端点恢复优先于锚点恢复；因此“真正到底部”仍严格落在新范围的
`maximum()`，而“差一点到底部”不会被强制吸附到 `maximum()`。

## 4. 固定原生拖动轨迹

对当前全屏窗口的屏幕边界 `B`，定义归一化轨迹

\[
p_i(B)=B_{min}+(0.50,\;0.94-0.34i/32)\odot B_{size},
\qquad i=0,1,\ldots,32.
\]

helper 发出的平移序列严格为

\[
\mathcal D(B)=
[LeftMouseDown(p_0),
 LeftMouseDragged(p_1),\ldots,
 LeftMouseDragged(p_{32}),
 LeftMouseUp(p_{32})].
\]

该序列使用 32 个 `LeftMouseDragged`，终点归一化 y 坐标为 `0.60`，故保留
一个可观测的非端点近底间隔。每次修改 Qt/Cocoa viewer 后，必须用同一
`\mathcal D(B)` 重新执行；普通窗口和全屏若都需要拖动，只改变 `B`，不改变
轨迹参数。缩放和全屏切换也使用真实 CoreGraphics 滚轮及双击事件。

## 5. 输入、输出与验收谓词

helper 输入为应用 bundle/可执行文件、图像路径和权限选项；运行状态包含
`(pid,B_normal,B_fullscreen,P,samples)`。每条诊断样本至少包含
`(phase,zoom,viewportHeight,v,r_min,r_max,anchorSceneY)`。

成功必须同时满足：

1. Post Event 与 Accessibility 权限均为真，且原生滚轮使图像可滚动；
2. 原生双击后全屏窗口几何稳定；
3. 全屏原生拖动后的样本满足 `NearBottom(v,R)`；
4. 退出请求后的每条包含锚点的诊断样本都满足
   `|a_y^{sample}-a_y^{before}|≤4` 个 scene 像素，且最终普通窗口几何稳定；
5. 退出阶段没有出现 `v≤r_min+1` 的起点复位；
6. `CGEventPost` 失败、权限缺失或外部图像不存在时，helper 返回 CTest
   跳过码 77，不输出伪造的成功结果。

## 6. 约束、性质与回退点

- 图像内容、缩放变换和原生轨迹不变；修复只同步连续平移锚点、端点语义和
  全屏几何生命周期。
- `setSceneRect()` 后只能使用新范围恢复，不能写回旧 `maximum()` 或旧窗口
  像素坐标。
- 退出请求必须在任何退出几何变化前读取全屏中最新的锚点。
- 端点状态使用 `τ=3`，内部近底状态必须保留为内部位置，不得扩大到端点。
- helper 必须等待窗口和诊断样本稳定，不能用固定睡眠或替代输入路径判定成功。
- 若模型、证明、静态检查或原生 helper 任一谓词失败，回退到对应的锚点
  捕获、场景重建、退出刷新或测试同步步骤；不能通过放宽验收条件掩盖失败。
