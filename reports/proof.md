# 全屏缩放与垂直平移保持：数学证明

## 1. 方案与待证命题

实现包含三层：

1. `QVGraphicsView::getScrollEdge()` 用 `\tau=3` 识别释放后的视觉端点；
2. `updateSceneRect()` 在场景/视口重建后把端点意图映射到新范围，
   `beginFullScreenPanPreservation()`、`refreshFullScreenPanPreservation()` 和
   `endFullScreenPanPreservation()` 管理原生全屏生命周期；
3. `fovelle_native_drag_helper` 以固定 CoreGraphics HID 轨迹运行真实应用，
   检查普通窗口、全屏进入、全屏拖动、全屏退出四个可观察阶段。

待证命题是：若用户在任一阶段选择了图像底边，经过一次全屏往返后仍选择
底边，且布局变化不会把图像显示复位到起点；原生复现器对该命题的验收不能
被合成事件或未授权状态伪造。

## 2. 引理一：端点快照不会丢失释放后的底部意图

设滚动条范围为 `R=[r_min,r_max]`。原生轨迹的最后一次拖动事件可能先把
`v` 写到 `r_max`，鼠标释放随后启动 `ScrollHelper` 的约束动画。由于
`ScrollHelper` 使用内容/视口 `QRect` 的整数尺寸，观测到释放后可能有

\[
r_{max}-v\le2.
\]

实现使用 `\tau=3`，故

\[
v\ge r_{max}-2\Longrightarrow v\ge r_{max}-\tau
\Longrightarrow E(v,R)=\text{Maximum}.
\]

同理，距最小端点不超过 2 个单位时识别为 `Minimum`。因此约束动画不会在
全屏请求边界前把用户已经选定的视觉端点错误降级为 `None`。显式前提是
端点附近的 3 个滚动单位属于同一个视觉边界；该前提由滚动条整数化和本地
原生轨迹日志确证。

## 3. 引理二：场景矩形重建保持端点语义

设旧范围为 `R`，新场景/视口产生 `R'=[r'_min,r'_max]`。`setSceneRect()`
可能先重置或裁剪滚动条，故旧值 `v` 不是合法的恢复坐标。实现先保存
`e=E(v,R)`，完成 `setSceneRect()` 后执行

\[
v'=
\begin{cases}
r'_{min},&e=\text{Minimum},\\
r'_{max},&e=\text{Maximum},\\
v,&e=\text{None}.
\end{cases}
\]

因为 `r'_{min},r'_{max}\in R'`，所以端点情况下 `E(v',R')` 保持为同一端点。
`None` 情况不制造新的端点意图。水平轴同理。因此每一次场景矩形重建都
保持了用户的端点选择。

## 4. 引理三：进入全屏保持底边

在进入请求时，`begin` 先停止延迟约束和滚动动画，再捕获当前 `e_y`。由引理
一，普通窗口已经拖到底部时 `e_y=Maximum`。保持器是幂等的：自定义 AppKit
动画再次调用 `begin` 时不会用过渡中的中间值覆盖已捕获端点。

进入动画可能产生任意多个 `resizeEvent`、标题栏 inset 变化和
`setSceneRect()`。由引理二，每次重建之后都有

\[
v_y^{(k)}=r_{max}^{(k)}.
\]

因此，在 AppKit/Qt 发布最终全屏视口后，`Bottom` 成立，且图像底边满足
\(|b(T,S,v_y)-V_b|\le\varepsilon\)。若没有该保持器，旧整数值会被新范围
裁剪为中间位置，正是原缺陷。

## 5. 引理四：全屏中的最后一次拖动会覆盖旧快照

用户在全屏中再次执行 `\mathcal{D}(B_{fullscreen})` 后，退出请求首先调用
`refresh`。该函数即使保持器已经活动，也会清空旧端点并读取当前滚动条，故

\[
e_y^{exit}=E(v_y^{fullscreen},R_y^{fullscreen}).
\]

由引理一，这里捕获的是全屏中最后一次拖动的底部意图，而不是进入全屏时的
普通窗口快照。若不执行 `refresh`，旧快照可能覆盖用户新选择；因此这是退出
跳变缺陷所需的必要边界操作。

## 6. 引理五：退出全屏不会跳回起点

退出动画和 Qt 状态事件可能交错触发多次几何重建。由引理四，所有重建都携带
`e_y^{exit}=Maximum`；由引理二，每次重建都设置当前范围的 `r_max`。完成
通知中 `end` 在清理状态前再次恢复端点，因此最终状态满足

\[
v_y^{final}=r_{max}^{final},
\qquad
|b(T,S,v_y^{final})-V_b^{final}|\le\varepsilon.
\]

端点恢复只改变滚动范围中的位置，不改变图像内容或缩放变换 `T`。所以退出
阶段不存在从 `r_min` 开始的起点复位，也不存在把旧窗口坐标直接带入新窗口
的跳变。

## 7. 引理六：原生 helper 的测试证据是有效的

对任一窗口边界 `B`，helper 发出的序列严格为

\[
LeftMouseDown(p_0)\to
LeftMouseDragged(p_1)\to\cdots\to
LeftMouseDragged(p_{32})\to
LeftMouseUp(p_{32}).
\]

每个事件都由 `CGEventCreateMouseEvent` 创建并通过
`CGEventPost(kCGHIDEventTap,...)` 投递，且普通窗口与全屏使用同一归一化轨迹。
因此测试输入经过 macOS HID/WindowServer/Qt Cocoa 事件链，而不是直接调用
`executeDragAction`、AX action、键盘或 QTest。helper 只在
`CGPreflightPostEventAccess` 与 `AXIsProcessTrustedWithOptions` 均为真时执行；
权限或外部 fixture 不满足时返回 CTest 的 `SKIP_RETURN_CODE=77`，不会输出
通过结果。该测试输入模型由 Apple 的 CoreGraphics 事件和窗口信息 API 定义
直接支持：[CGEvent](https://developer.apple.com/documentation/coregraphics/cgevent)、
[CGEventType](https://developer.apple.com/documentation/coregraphics/cgeventtype/mousemoved?language=objc)、
[CGEventSource](https://developer.apple.com/documentation/coregraphics/cgeventsourcestateid)。

helper 还要求窗口几何和诊断样本在连续轮询中稳定，且退出后的新样本没有
起点复位。故一个过渡中间帧、日志旧样本或窗口尚未完成的状态都不能单独使
测试成功。

## 8. 结论与回退规则

由引理一至五，在以下前提下生产实现满足需求：事件循环最终交付所有布局
事件；滚动条 `maximum()` 表示当前范围下端点；用户释放拖动后端点状态已
提交；全屏完成事件最终到达；图像内容和缩放变换不被其他业务事件改变。

引理六说明同一结论由真实 macOS 原生拖动轨迹动态检验，而不是由替代输入
路径推断。若静态分析、编译、CTest 或 native helper 任一阶段失败，必须回退
到相应的端点捕获、场景重建、退出刷新或测试阶段同步步骤修正；不得通过降低
`maximum()` 断言、放宽窗口阶段、改用 QTest/AX/键盘或直接业务调用来掩盖失败。
