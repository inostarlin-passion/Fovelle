# 全屏缩放与垂直平移修复：数学模型

## 1. 对象与输入输出

设原始图像为矩形

\[
I=[0,W]\times[0,H],\qquad W,H>0,
\]

其中 `W`、`H` 是 `loadedPixmapItem` 的场景尺寸。视图变换为可逆仿射变换
`T`，当前内容在视口坐标中的位置由 `T`、场景矩形 `S`、视口矩形 `V`
和两个滚动条值共同决定。

对垂直方向定义：

- `S_y=[s_min,s_max]`：当前 `sceneRect()` 的垂直范围；
- `R_y=[r_min,r_max]`：Qt 根据 `S`、变换和视口计算出的垂直滚动范围；
- `v_y∈R_y`：`verticalScrollBar()->value()`；
- `b_y`：图像底边 `T(0,H)` 映射到视口后的 y 坐标；
- `V_b`：视口底边坐标。

水平量 `R_x,v_x` 同理。滚动条端点是离散整数，映射后的边界则可能经过
浮点变换、设备像素比和 `QRect` 包含式边界取整，因此显示断言采用有限的
设备像素容差 `ε`，而不是把两个不同坐标系统的浮点值要求为完全相等。

程序输出是新的视图状态
`(T,S,R_x,R_y,v_x,v_y)` 以及全屏切换期间的可见图像位置。正确输出满足：

\[
v_y=r_{max}\Longrightarrow |b_y-V_b|\leq\varepsilon
\]

在图像被用户拖到最下方时，退出全屏后的新滚动范围也必须满足
`v'_y=r'_max`，并且同一图像底边仍满足上述关系。

## 2. 状态机

全屏平移保存器的状态为

\[
P=(a,e_x,e_y),
\]

其中 `a∈{false,true}` 表示保存器是否活动，`e_x,e_y` 属于
`{None,Minimum,Maximum}`，表示进入一次几何重建前用户是否位于对应滚动端点。

状态转移如下：

| 事件 | 前置 | 转移与不变量 |
| --- | --- | --- |
| `begin` | 任意 | 停止延迟约束和滚动动画；若已活动则保持已捕获端点，否则捕获当前端点并令 `a=true`。 |
| 场景/视口重建 | `a=true` 或手动模式 | `setSceneRect()` 后按保存的端点设置新范围的 `minimum/maximum`，而不是恢复旧整数值。 |
| `refresh` | 全屏退出请求 | 停止可能覆盖用户输入的延迟动作，并从当前全屏滚动条重新捕获端点；这一步允许用户在全屏中刚刚拖到最下方。 |
| `end` | 任意 | 在当前范围恢复保存端点，再令 `a=false,e_x=e_y=None`。 |

AppKit 的 `will/did enter/exit` 通知与 Qt 的 `WindowStateChange` 不保证同一
事件循环迭代完成。因此 `begin` 与 `end` 的生命周期不能绑定到一次
`resizeEvent`；`refresh` 必须位于退出请求边界。

## 3. 约束与验收谓词

给定用户手动平移到图像底边，定义端点意图谓词

\[
E_y(v_y,R_y)\iff r_{min}<r_{max}\land v_y\ge r_{max}-1.
\]

实现必须满足以下契约：

1. **端点保持**：只要视图处于手动缩放且场景矩形或视口尺寸发生变化，
   `E_y` 的真假不能因为旧范围被替换而丢失；若为真，重建后设置
   `v_y:=r'_max`。
2. **全屏进入保持**：进入全屏前若 `E_y` 为真，完成全屏布局后仍有
   `E_y(v'_y,R'_y)`。
3. **全屏退出保持**：退出请求时重新读取当前滚动条；若用户在全屏中
   拖到最底部，则完成退出后仍有 `E_y(v''_y,R''_y)`。
4. **无跳变**：在布局变化只改变 `R_y` 而不改变图像内容和用户端点意图
   时，输出位置只能沿新范围的同一端点变化，不能回到范围起点或旧整数
   坐标造成的中间位置。
5. **CI 可重复**：测试等待可观察的窗口/范围条件；映射边界只验证设备像素
   取整容差，且不得把调度器墙钟延迟误判为产品几何错误。

## 4. 显式前提与证据

- Qt 文档说明 `sceneRect` 决定视图导航与滚动条范围；因此场景矩形重建
  是滚动值失效的直接边界：[QGraphicsView::sceneRect](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop)。
- Qt 文档说明 `resizeAnchor` 默认不改变场景位置；本修复保留现有变换，
  只在端点意图已知时重设新范围端点：[QGraphicsView anchors](https://doc.qt.io/qt-6/qgraphicsview.html#resizeAnchor-prop)。
- Apple 将 `NSWindowWillEnterFullScreenNotification` 定义为窗口即将进入
  全屏的通知，并提供 will/did enter/exit 生命周期；所以 AppKit 通知不能
  被假定为同步完成：[Apple notification](https://developer.apple.com/documentation/appkit/nswindow/willenterfullscreennotification?language=objc)、
  [Apple delegate lifecycle](https://developer.apple.com/documentation/appkit/nswindowdelegate/windowwillenterfullscreen%28_%3A%29?changes=_2)。
- 本地与远端日志的已知差异是 Qt 6.11.1/macOS 15 与 Qt 6.11.2/macOS 26
  对整数视口边界的取整不同；因此 `ε` 只用于像素映射观察，不用于放宽
  滚动条必须位于 `maximum()` 的功能契约。
