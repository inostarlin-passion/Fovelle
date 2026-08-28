# 全屏退出时连续平移位置保持：数学证明

## 1. 方案与待证命题

方案由四部分组成：

1. `QVGraphicsView` 在全屏进入和退出请求边界停止延迟约束；端点仍映射到
   新范围端点，同时为非端点位置保存视口中心的 scene 锚点；
2. `updateSceneRect()` 完成 `setSceneRect()` 后恢复新范围中的滚动状态；
3. `refreshFullScreenPanPreservation()` 在退出前重新读取全屏中的最后一次
   原生拖动，避免使用进入全屏时的旧快照；
4. `fovelle_native_drag_helper` 通过 CoreGraphics HID 事件链，以固定的真实
   拖动轨迹验收“近底但非端点”和退出后的锚点不变。

待证命题：若退出请求前的全屏样本是近底内部位置，则退出完成后仍为同一
图像内容的连续平移位置，不会被重建复位到起点，也不会被错误吸附到端点。

## 2. 引理一：场景矩形重建可以保持端点或连续锚点

设重建前范围为 `R`，重建后范围为 `R'`。`setSceneRect()` 会按新场景矩形
重新调整滚动条，故旧整数值 `v` 不是稳定的跨布局坐标。

若 `E(v,R)=Minimum/Maximum`，实现分别设置

\[
v'=r'_{min}\quad\text{或}\quad v'=r'_{max}.
\]

因此端点语义在 `R'` 中仍成立。若 `E(v,R)=None`，实现保存

\[
a=mapToScene(c(V)),
\]

并在新布局中令

\[
d=mapFromScene_{new}(a)-c(V'),
\]

再把滚动条值按 `d` 的两个分量反向移动。若 `a` 可表示，则
`mapToScene_{new}(c(V'))=a`；若不可表示，Qt 的滚动条夹紧到 `R'`，仍不会写回
旧窗口坐标。故 `rebuild` 不会丢失端点或非端点的平移语义。

## 3. 引理二：退出请求保存的是最后一次全屏拖动

设全屏进入时保存的状态为 `P_enter`，用户随后执行固定原生轨迹并得到状态
`P_drag`。退出请求先停止约束动画，再执行 `refresh`，所以它读取的正是
当前全屏滚动条和当前可用视口中心：

\[
P_{exit}=(E(v_x^{drag},R_x^{drag}),
         E(v_y^{drag},R_y^{drag}),
         a_x^{drag},a_y^{drag}).
\]

`P_exit` 不依赖 `P_enter`。因此，即便进入全屏时没有滚动条、或者进入时的
位置与退出前不同，退出重建使用的仍是用户最后一次原生拖动的位置。

## 4. 引理三：近底内部位置不会被吸附到端点

测试样本满足

\[
r_{max}-v>\tau=3.
\]

由端点定义，`E(v,R)\ne Maximum`；只要它也满足 `r_max-v≤G`，它就是模型中
定义的 `NearBottom`。因此退出 `refresh` 保存的是连续锚点 `a_y` 而不是
`Maximum` 标记。根据引理一，之后的 `rebuild` 使用锚点映射，不会把“差一点
到底部”的位置改写为 `r'_{max}`。

## 5. 引理四：退出前后不会发生可见位置跳变

退出前锚点为

\[
a_y^{before}=mapToScene_{full}(c(V_{full})).
\]

由引理二，`refresh` 保存 `a_y^{before}`。退出过程中每次场景/视口重建由
引理一恢复该锚点，因此每个稳定布局状态都满足

\[
mapToScene_k(c(V_k))=a_y^{before}
\]

（在整数滚动条和浮点变换的有限取整误差内）。退出完成后仍满足该式，故

\[
|a_y^{after}-a_y^{before}|\le\varepsilon,
\qquad\varepsilon=4.
\]

helper 的 `anchorsStayStableAfter` 对退出请求后的每一条包含锚点的诊断样本
应用同一不等式，而不是只检查最终样本；所以“先跳变再回到原位”的轨迹也不
满足成功谓词。

同时，锚点恢复只改变滚动条位置，不改变图像内容或缩放变换 `T`；因此不会
出现从 `r_min` 起点复位或把旧窗口像素坐标直接带入新窗口的跳变。

## 6. 引理五：原生 helper 的输入不能伪造该命题

对每个全屏窗口边界 `B`，helper 严格发送

\[
LeftMouseDown(p_0)\to
LeftMouseDragged(p_1)\to\cdots\to
LeftMouseDragged(p_{32})\to
LeftMouseUp(p_{32}).
\]

每个事件由 `CGEventCreateMouseEvent` 创建，并通过
`CGEventPost(kCGHIDEventTap,...)` 进入 macOS HID/WindowServer/Qt Cocoa 链路；
相关定义见 [CGEvent](https://developer.apple.com/documentation/coregraphics/cgevent)、
[CGEventPost](https://developer.apple.com/documentation/coregraphics/cgeventpost)
和 [kCGHIDEventTap](https://developer.apple.com/documentation/coregraphics/cgeventtaplocation/cghideventtap?language=objc)。
helper 仅使用 Accessibility API 检查/请求权限，不使用 AX action；也不调用
`executeDragAction`、QtTest、键盘或任何业务函数替代拖动。

它只在 `CGPreflightPostEventAccess` 和
`AXIsProcessTrustedWithOptions` 均为真时执行；图像缺失或权限不足返回 77。
窗口几何和诊断样本必须连续稳定，且退出后的新样本没有起点复位，所以任一
中间动画帧或旧日志样本都不能单独构成成功。

## 7. 结论与回退规则

由引理一至四，在以下前提下，连续锚点保持方案满足需求：事件循环最终交付
所有全屏布局事件；滚动条值表示当前场景范围；用户释放拖动后的状态已提交；
全屏完成事件最终到达；图像内容和缩放变换没有被其他业务事件改变。

引理五说明同一命题由真实 macOS 原生拖动轨迹动态检验。若模型对应的近底
谓词、锚点误差、起点复位检查、编译、静态分析或 CTest 任一失败，必须回退到
锚点捕获、场景重建、退出刷新或测试同步步骤修正；不得改用 QTest、AX、键盘、
直接业务调用或降低验收阈值来掩盖失败。
