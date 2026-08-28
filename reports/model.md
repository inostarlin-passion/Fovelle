# 全屏缩放与垂直平移保持：数学模型

## 1. 范围、对象与显式前提

本模型描述 macOS 上 Fovelle 的同一份位图在普通窗口、原生全屏进入、全屏
平移和原生全屏退出四个阶段中的视口状态。复现输入是
`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/3.jpeg`；该文件必须存在，且
Fovelle 必须使用“滚轮缩放、左键平移、左键双击切换全屏”的当前设置。

原生复现器不是 QtTest、Accessibility action、键盘操作或业务函数调用。它
使用 CoreGraphics 生成并通过 `CGEventPost(kCGHIDEventTap, ...)` 投递真实的
鼠标/滚轮事件；因此测试前提包含 macOS 的 Post Event 与 Accessibility 权限。
CoreGraphics 将 `CGEvent` 定义为低层硬件事件，`CGEventPost` 将事件放入 Quartz
事件流，`kCGHIDEventTap` 是 HID 系统事件进入 WindowServer 的位置：
[CGEvent](https://developer.apple.com/documentation/coregraphics/cgevent)、
[CGEventPost](https://developer.apple.com/documentation/coregraphics/cgeventpost)、
[kCGHIDEventTap](https://developer.apple.com/documentation/coregraphics/cgeventtaplocation/cghideventtap?changes=__3&language=objc)。

窗口坐标由 `CGWindowListCopyWindowInfo` 的当前会话窗口信息和
`kCGWindowBounds` 解码得到；该坐标系的屏幕原点位于主显示器左上角：
[CGWindowListCopyWindowInfo](https://developer.apple.com/documentation/coregraphics/cgwindowlistcopywindowinfo%28_%3A_%3A%29?changes=_9&language=objc)、
[kCGWindowBounds](https://developer.apple.com/documentation/coregraphics/kcgwindowbounds?changes=_6)。

## 2. 几何状态

设原始图像为

\[
I=[0,W]\times[0,H],\qquad W,H>0,
\]

设当前视图变换为可逆仿射变换 `T`，场景矩形为 `S`，视口矩形为 `V`。对
垂直方向定义：

- `R=[r_min,r_max]`：Qt 根据场景矩形、变换和视口计算的滚动范围；
- `v∈R`：`verticalScrollBar()->value()`；
- `b(T,S,v)`：图像底边 `T(0,H)` 映射到视口后的 y 坐标；
- `V_b`：视口底边。

水平量 `R_x,v_x` 同理。Qt 的 `sceneRect` 是导航与滚动范围的几何输入，
所以 `setSceneRect()` 是本缺陷的状态重建边界。

`QGraphicsView` 的滚动条范围是离散整数，而 `ScrollHelper` 依据
`QRect::width/height` 和可用视口计算约束范围；两者在边界处可能相差少量整数。
令本实现使用的端点容差为

\[
\tau=3.
\]

该值覆盖原生轨迹观测到的最大 2 个单位约束尾差，并保留严格的“新范围
`maximum()`”恢复操作。端点意图函数为

\[
E(v,R)=
\begin{cases}
\text{Minimum},&r_{min}<r_{max}\land v\le r_{min}+\tau,\\
\text{Maximum},&r_{min}<r_{max}\land v\ge r_{max}-\tau,\\
\text{None},&\text{otherwise}.
\end{cases}
\]

验收时还要求映射后的图像底边满足

\[
E_y(v,R)=\text{Maximum}\Longrightarrow
|b(T,S,v)-V_b|\le \varepsilon,
\qquad \varepsilon=4\text{ 个设备像素}.
\]

`ε` 只处理浮点变换、设备像素比和 `QRect` 包含式边界取整；它不替代滚动条
端点恢复。

## 3. 全屏保持状态机

保持器状态定义为

\[
P=(a,e_x,e_y,q),
\]

其中 `a∈{false,true}` 是保持器是否活动，`e_x,e_y∈{None,Minimum,Maximum}`
是当前用户端点意图，`q` 是窗口阶段：
`Normal`、`Entering`、`FullScreen`、`Exiting`。

| 事件 | 前置 | 状态转移与动作 |
| --- | --- | --- |
| `begin` | 任意 | 停止延迟约束、取消滚动动画；若已活动则保持原快照，否则以 `E(v_x,R_x),E(v_y,R_y)` 捕获端点并令 `a=true,q=Entering`。 |
| `rebuild(S',V')` | `a=true` 或手动模式 | 调用 `setSceneRect(S')` 后，把 `Minimum` 映射到 `r'_min`、`Maximum` 映射到 `r'_max`，而不是写回旧整数值。 |
| `refresh` | 退出请求边界 | 停止延迟约束、取消滚动动画，重新从当前滚动条计算 `e_x,e_y`，令 `q=Exiting`；这一步覆盖用户在全屏中最后一次原生拖动。 |
| `end` | 全屏完成/失败 | 在当前范围重新应用 `e_x,e_y`，再清除 `a,e_x,e_y` 并令 `q=Normal`。 |

AppKit 的 will/did 全屏通知、Qt 的窗口状态事件、窗口尺寸变化和自定义
动画回调并非同一个同步事件。因此保持器的生命周期绑定到请求边界与完成
边界，而不是绑定到单个 `resizeEvent`。

## 4. 固定原生输入轨迹

对窗口屏幕边界 `B` 定义归一化点

\[
p_i(B)=B_{min}+(0.50,\;0.94-0.90i/32)\odot(B_{size}),
\qquad i=0,1,\ldots,32.
\]

一次平移轨迹是

\[
\mathcal{D}(B)=
[\operatorname{LeftMouseDown}(p_0),
 \operatorname{LeftMouseDragged}(p_1),\ldots,
 \operatorname{LeftMouseDragged}(p_{32}),
 \operatorname{LeftMouseUp}(p_{32})].
\]

普通窗口和全屏窗口只改变 `B`，使用完全相同的 32 段轨迹。缩放使用 3 个
原生滚轮事件；全屏进入和退出各使用两次真实 down/up 组成的双击。每一个
鼠标事件都由 `CGEventCreateMouseEvent` 创建并以
`CGEventPost(kCGHIDEventTap, event)` 投递。窗口发现仅使用当前会话的窗口
边界，不使用 AX action；权限检查使用
`CGPreflightPostEventAccess` 和 `AXIsProcessTrustedWithOptions`，后者只用于
报告/请求权限。

## 5. 输入、输出与验收谓词

原生 helper 的输入是应用 bundle/可执行文件路径、图像路径和权限选项；内部
状态为 `(pid,B_normal,B_fullscreen, P, samples)`。`samples` 来自应用的诊断
状态，每条样本至少包含 `(phase,zoom,viewportHeight,v,r_min,r_max)`。

定义

\[
Bottom(s)\iff s.r_{max}>s.r_{min}\land s.v\ge s.r_{max}-1.
\]

helper 的成功谓词为：

1. 原生滚轮后存在可滚动范围；
2. `\mathcal{D}(B_normal)` 后普通窗口 `Bottom` 稳定成立；
3. 原生双击进入全屏后，窗口几何稳定且全屏最终视口的 `Bottom` 稳定成立；
4. `\mathcal{D}(B_fullscreen)` 后全屏 `Bottom` 稳定成立；
5. 原生双击退出后，窗口回到 `B_normal`，普通视口 `Bottom` 稳定成立；
6. 退出阶段新增样本不存在 `v\le r_{min}+1` 的起点复位；
7. `A = PostEventAccess ∧ AccessibilityTrusted`，否则 helper 返回 77，CTest
   将其标为跳过而不是伪造通过。外部图像缺失同样返回 77。开发机上必须先
   用 `--request-access` 完成授权，再执行真实复现。

在 CTest 中，`FovelleNativeDragReproduction` 直接运行这个 helper；QtTest
只负责已有的单元/回归交叉检查，不是原生拖动验收的替代物。

## 6. 约束、性质与回退点

- 同一图像内容、缩放变换和拖动轨迹必须保持不变；修复只同步滚动端点与
  全屏几何生命周期。
- 任何 `sceneRect`/视口重建都必须使用新范围的端点，不得复用旧的
  `maximum()` 或旧像素坐标。
- 任何退出请求都必须在布局变化前读取全屏中的最新端点。
- helper 必须等待窗口/诊断状态稳定，不能以固定延迟或中间动画帧判定成功。
- 若静态检查或原生 helper 任一最终谓词失败，回退到本模型对应的端点捕获、
  场景重建或退出刷新步骤，不能放宽验收条件来掩盖失败。
