# 图片缩放技术设计文档：同步提交、可行锚点与滚动条拓扑

日期：2026-09-04
仓库：/Users/inostarlin/code/Fovelle
适用实现：src/qvgraphicsview.{h,cpp}、src/mainwindow.cpp

设计状态：已实现；本文件定义设计、显式前提和可核验的验收合同，执行结果见
[reports/test_completion_report.md](test_completion_report.md)。

## 1. 目标与范围

本设计处理两个相互耦合的问题：

1. 鼠标滚轮/触控板、键盘快捷键、标题栏菜单和右键菜单触发的图片缩放必须
   没有几何过渡效果。
2. 缩放优先保持鼠标位置对应的图片点；如果该点会使目标图片位置落在约束域
   之外，则把锚点修正到距离鼠标最近、且一次缩放后无需再次位置修正的点。
3. 当缩放改变 `ScrollBarAsNeeded` 的纵向拓扑（从无 range 变为有 range）时，
   Qt 后续的 viewport/range 重算不得把已经提交的纵向锚点推到新 range 的端点。

“无过渡”指 Fovelle 图片几何的缩放提交，不包括全屏窗口、HDR 图层、导航
按钮等独立的原生 UI 过渡。结论针对 Qt widget 可观察的 transform、scene
rect、viewport、滚动条 range/value、paint 和 timer 边界；不把未经捕获的
WindowServer/CALayer presentation tree 当作已证明事实。

## 2. 原子化验收标准

每条标准都有唯一 ID。ID 同时出现在本文件、测试说明、测试源码和完成报告
中，由静态门禁检查追溯完整性。

| ID | 原子验收标准 | 可观察证据 |
| --- | --- | --- |
| AC-ZOOM-NO-ANIMATION-STATIC | 生产缩放路径不再包含几何 QPropertyAnimation、独立 displayed zoom 状态、旧式 pending anchor 或缩放结算 timer；拓扑恢复状态不参与插值。 | 源码静态扫描通过；ZoomPlan 与单一 commit 函数存在。 |
| AC-ZOOM-NO-ANIMATION-INPUT | 一次真实鼠标缩放输入只提交一次目标 zoom；输入返回后 displayed/logical zoom 相等，静默窗口内 zoom、scroll value 和 image rect 不变。 | QtTest 的 wheel 事件、signal count、250ms quiet window。 |
| AC-ZOOM-NO-ANIMATION-SHORTCUT | 真实键盘快捷键触发缩放时没有运行中的几何动画，且立即得到目标几何。 | QTest::keySequence、无名为 zoomTransitionAnimation 的对象、即时状态断言。 |
| AC-ZOOM-NO-ANIMATION-MENU | 标题栏 View 菜单和右键 View 菜单的缩放 action 共享同一即时提交路径。 | 菜单 clone 的真实 action、action route 和即时状态断言。 |
| AC-ANCHOR-MOUSE-PREFERRED | 有效鼠标位置是首选锚点；鼠标点在图片外时先裁到当前图片边界；没有有效鼠标时使用一次解析的可用视口中心。 | 纯锚点投影测试、鼠标/键盘输入测试和定向 Toggle 测试。 |
| AC-ANCHOR-PROJECT-FEASIBLE | 锚点修正是目标可行原点集合的逆仿射像上的欧氏最近点；图片不能通过该选择制造可避免的额外空白。 | 纯函数边界断言、目标尺寸动态测试和最终 image/viewport containment。 |
| AC-ANCHOR-NO-POST-CORRECTION | 缩放提交后不存在由缩放引起的延迟平移或重缩放；至少在 250ms 静默窗口和现场序列终态中保持不变。 | 终态/静默窗口的 image rect、scroll value、timer 状态。 |
| AC-ANCHOR-HBAR-TOPOLOGY | 现场“四格放大、一格回退”跨过横向滚动条出现/消失边界时，H range 为 0→非零→0，H 条改变 viewport 高度时纵向锚点不跳变。 | 真实 JPEG 或等比例 fallback、range trace、paint/终态锚点误差。 |
| AC-VBAR-TOPOLOGY-ANCHOR | 缩放使 V 从无 range 变为有 range 时，提交态、range 收敛态和实际 paint 观察到的同一场景点均保持在目标 viewport 点一 DIP 内；H 已有 range 时其 value/锚点不被连带改写。 | 专用 900×400 fixture、V range/value trace、paint probe、稳定态断言。 |

## 3. 问题分解、证据缺口与本地事实

### 3.1 入口归并

仓库中的缩放入口先按用户表面归并，再按共同调用点收敛：

| 用户入口 | 本地调用链/汇点 | 设计结论 |
| --- | --- | --- |
| 鼠标滚轮/触控板 | wheelEvent → executeScrollAction → zoomRelative → zoomAbsolute | 不在事件类型层分别做动画策略。 |
| 原生 pinch | handleNativeGestureEvent → zoomAbsolute | 每个手势事件也执行一个同步提交。 |
| 键盘 Zoom In/Out、Toggle | ActionManager → MainWindow → QVGraphicsView::zoomIn/zoomOut/toggleFitAnd100 | 快捷键只负责分派，几何策略在 view 汇点统一执行。 |
| 标题栏 View 菜单、右键 View 菜单 | action clone → MainWindow action handler → view API | 两个菜单不复制缩放算法。 |
| 自定义百分比 | MainWindow::zoomCustom → zoomAbsolute | 同样受同步提交和锚点合同约束。 |

本地代码事实可由 rg -n 复核；本设计只依赖函数和对象名，不依赖会随构建
变化的行号。

### 3.2 现场问题与缺口

现场文件
/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg
是可读的 3840×4407 baseline JPEG。使用等比例合成图可在没有挂载卷的机器上
复现同一几何边界。

旧实现的风险来自两类 writer：几何 zoom animation 每帧写 displayed zoom，
而约束/布局 timer 可能在动画结束后再次写 scroll value。仅断言最终 zoom 正确
不能排除“先缩放、再平移”的可见两段状态。因此新增证据缺口驱动如下：

1. 静态确认几何动画和旧式 pending-anchor 状态已被移除。
2. 直接发送真实 wheel、shortcut、标题栏 action、右键 action，并确认返回
   当前事件后已是终态。
3. 用纯函数覆盖图片内点、图片外点、目标图片大于/小于 viewport 的边界。
4. 用现场的 +4/-1 序列记录 H/V range、value、resize、paint 和终态。
5. 对 expensive backing pixmap 和 HiDPI 路径单独做归一化坐标/矩阵回归。
6. 单独构造“横向已溢出、纵向刚好不溢出”的 900×400 fixture，让 1.25 倍缩放
   首次物化 V range；原有只等待最终状态的测试没有覆盖这个拓扑边界，且其
   延迟 writer 清理会在断言前停止 timer。

修复前的可复核基线是：专用测试在提交返回时得到
`anchor=(185,356), V value=4, range=-28..8`，经过布局/事件循环收敛后变为
`anchor=(185,353), V value=7, range=-28..7`；H 仍有 range。也就是说，失败
不是“图片最终没加载”，而是 V range 改变后的第二次 value 写入覆盖了已经正确
的场景锚点。

## 4. 联网多跳检索与多源互证

检索链路是：Qt 公共 API 语义 → 同版本 Qt 源码实现 → 独立数学资料校验。
外部资料只用于确定框架语义和数学性质，Fovelle 的入口、尺寸和测试结果仍以
本地源码/日志为准。

| 跳 | 经核验事实 | 对本设计的约束 |
| ---: | --- | --- |
| 1 | [QAction 官方文档](https://doc.qt.io/qt-6/qaction.html)说明 action 可由菜单、工具按钮或快捷键激活，并通过统一的 triggered 语义分派。 | 缩放行为必须在共同 view API 收敛，而不是给每个菜单单独加补丁。 |
| 2 | [QPropertyAnimation](https://doc.qt.io/qt-6/qpropertyanimation.html)和[QAbstractAnimation](https://doc.qt.io/QT-6/qabstractanimation.html)描述了属性动画的起止值、current time 和事件循环推进的中间帧。 | 要求“无几何过渡”时必须删除该 writer，不能只修改 duration。 |
| 3 | [QGraphicsView transformationAnchor](https://doc.qt.io/qt-6/qgraphicsview.html#transformationAnchor-prop)说明缩放锚点会与滚动条/对齐共同作用；图片完全可见时 alignment 会影响最终位置。 | AnchorUnderMouse 不是所有目标尺度的可行性证明。 |
| 4 | [QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html)和[QScrollBar](https://doc.qt.io/qt-6/qscrollbar.html)说明 AsNeeded range、viewport 和整数 value 的关系。 | 目标 viewport 与整数滚动条 range 必须纳入锚点规划，并给出舍入误差界。 |
| 5 | 同版本 [Qt 6.11.1 qgraphicsview.cpp](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)和[qabstractscrollarea.cpp](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/widgets/qabstractscrollarea.cpp)用于核对内容尺寸重算、scrollbar 交叉影响和布局调用顺序。 | 目标 H/V 拓扑改变后必须重规划；不能把缩放前 viewport 当作最终 viewport。 |
| 6 | [QWidget::updatesEnabled](https://doc.qt.io/qt-6/qwidget.html#updatesEnabled-prop)规定 updates disabled 时不会产生 paint。 | transform、scene rect、range 和 scroll value 在短临界区内批量提交，恢复后只请求最终更新。 |
| 7 | Parikh/Boyd 的[Proximal Algorithms](https://web.stanford.edu/~boyd/papers/pdf/prox_algs.pdf)第 6.2.4 节给出 box/hyper-rectangle 的逐坐标欧氏投影。 | 最近可行锚点可实现为两个一维 clamp，结果唯一且无需迭代优化器。 |

多源互证得到的结论是：action 的入口归并由 Qt 文档和本地 action clone 调用图
互证；动画中间 writer 由 Qt 动画语义和旧 view 实现互证；目标滚动条拓扑由公共
scroll-area 语义和 Qt 6.11.1 源码互证；最近点公式由独立投影定理校验。

针对本次垂直跳变，证据链进一步闭合为：`QGraphicsView` 的 scene/transform 变化
先触发 `QAbstractScrollArea` 的交叉轴布局，`QScrollBar` 再以整数 range/value
暴露最终状态；因此只检查 `zoomAbsolute()` 返回值不足以证明视觉稳定，必须同时
观察 `rangeChanged`、`valueChanged`、`Resize` 和 `Paint` 的顺序。该结论由 Qt
公共 API、Qt 6.11.1 实现和本地修复前 trace 三方互证。

## 5. 显式前提与模型

以下前提是推理条件，不是无条件的产品承诺：

- P1：缩放请求在 GUI 线程串行处理，旧/目标 zoom 为有限正数，并受现有
  boundedZoomLevel() 限制。
- P2：本次缩放只改变均匀尺度；既有旋转、镜像等仿射变换保持不变。
- P3：有效鼠标位置优先；菜单/快捷键没有 event position 时，view 使用最近一次
  有效 viewport mouse position，若没有则使用可用 viewport center，并在请求开始
  时解析一次。
- P4：目标 H/V policy 为 Qt::ScrollBarAsNeeded；bar extent、顶部安全区、
  RTL 和 HiDPI 均从运行时对象/transform 读取，不硬编码平台尺寸。
- P5：目标位置要求大图覆盖可用 viewport；小图不能滚动时，默认
  constrainToCenterWhenSmaller 将其放到 alignment fixed point。
- P6：几何先以 DIP 的 qreal 矩形计算，只有写入 QScrollBar 时才进入整数值；
  因此允许不超过 1 DIP 的整数舍入误差。
- P7：提交开始时旧图片状态已经是上一请求的稳定约束状态；若外部代码破坏此
  前提，先由已有的普通约束路径恢复。
- P8：scene image rect 是图片在 scene 中的包围矩形；如果 expensive backing 改变
  scene 尺寸，则用图片归一化 UV 坐标重建 scene anchor。
- P9：“尽可能接近鼠标”定义为最小化 DIP 平面中的平方欧氏距离；没有这个度量，
  “最近”不能推出唯一实现。
- P10：Qt 可能在同步提交返回后继续派发由 AsNeeded 拓扑引起的 range/value/布局
  事件；这些事件属于同一次 zoom 的几何收敛，不代表新的用户缩放请求。

令旧图片 viewport 矩形为
I0=[o0x,o0x+w0)×[o0y,o0y+h0)，目标图片尺寸为 (w1,h1)，目标可用 viewport
为 U1=[ux,ux+W)×[uy,uy+H)，原始鼠标锚点为 m，实际锚点为 a。

对任一轴，目标图片原点的可行区间为：

    T > V:       [u + V - T, u]       # 图片溢出，必须覆盖 viewport
    T <= V:      [center, center]     # 默认小图居中，只有一个 alignment fixed point

## 6. 实现设计

### 6.1 R1：一次同步几何提交

QVGraphicsView 使用 ZoomPlan 和 commitZoomImmediately()：

1. makeZoomPlan() 捕获旧 transform、旧图片 viewport rect、旧 scene rect、目标
   zoom 和原始 viewport 点。
2. 停止可能残留的缩放相关 timer，并移除 expensive backing；它只能作为像素
   质量 refinement，不能成为第二个 zoom writer。
3. 暂时关闭 view、viewport、H bar、V bar 的 updates。
4. 设置唯一的逻辑 zoomLevel，写入目标 transform，调用 updateSceneRect()。
5. 通过 settleTargetScrollAreaLayout() 反复同步触发 AsNeeded policy 的布局，
   直到 viewport/range 状态稳定。实现不调用通用
   QCoreApplication::processEvents()，避免在临界区重入用户输入。
6. 用实际目标 image rect 和实际 usable viewport 重做锚点投影，并一次设置整数
   scroll values；有 1 DIP 舍入残差时只在同一提交内做第二次校正。
7. 对有固定锚点的提交保存场景点与目标 viewport 点，供同一拓扑收敛过程中由
   Qt 触发的 range/value 更新立即重放；这不是插值动画，也不是无条件的延迟平移。
8. 刷新垂直滚动条安全区几何，恢复 updates，请求一次 viewport update，并发出
   一次 zoomLevelChanged()。

生产代码中不存在 QPropertyAnimation、animatedZoomLevel property、独立的
displayedZoomLevel、旧式 pending anchor 或 zoomAnchorSettleTimer。保留的
animatedZoomLevel() 和 isZoomTransitionRunning() 只是 ABI/测试调用方的兼容
读接口：前者返回唯一的 zoomLevel，后者恒为 false，不代表存在动画状态。
本次新增的 `postLayoutZoomAnchorScene/Viewport` 只保存一次已提交的语义场景点，
用于应对 Qt 同一布局收敛过程中的整数 range/value 重写，不是第二个 zoom 状态。

### 6.2 R2：目标可行域上的最近锚点

对一维尺度比 r=T/S，若旧原点为 o0，旧 viewport 中锚点为 a，保持同一
scene image point 后的目标原点满足：

    o1 = (1-r) a + r o0

先由目标图像尺寸和目标 usable viewport 求 o1 的可行区间 [L1,R1]，再反解
得到锚点可行区间：

    aL = (L1 - r o0) / (1-r)
    aR = (R1 - r o0) / (1-r)
    a  = clamp(m, min(aL,aR), max(aL,aR))

两个轴独立计算，构成轴对齐 box 的欧氏投影。实现还会先把鼠标裁到旧图片
矩形，避免把 viewport 空白当成图片内容点。

若目标 H/V 拓扑与预估不同，提交使用 Qt 实际 materialized 的 targetImage 和
getUsableViewportRect() 重新计算；因此横条出现造成的垂直 viewport 缩短不会
偷偷复用旧的中心。若 expensive backing 只改变 scene 像素密度，则通过旧/新
scene rect 的 UV 映射重建 scene anchor，避免把旧 scene 坐标误当作新 backing
坐标。

### 6.3 R3：纵向 AsNeeded 拓扑的语义锚点恢复

`commitZoomImmediately()` 在固定锚点提交后保存 `(scenePoint, viewportPoint)`。
`rangeChanged` 仍只负责安排既有的零延迟安全区几何刷新，同时尝试在同一信号
调用栈中恢复场景锚点；如果 `QAbstractScrollArea` 随后因新 V range 把整数 value
夹到端点，`valueChanged` 再次调用同一个恢复函数。零延迟 timer 在布局队列的
最后阶段刷新安全区并执行一次幂等恢复，随后清理保存的锚点。

恢复函数使用当前 `viewportTransform()` 重新计算
`map(scenePoint) - viewportPoint`，只修改当前 H/V value，且用内部更新保护避免
自触发递归。用户主动拖动滚动条、wheel/keyboard/drag 平移会取消保存的锚点；
所以该状态只属于尚未完成的同一次 zoom 拓扑收敛，不会夺回用户后续的平移。
若没有固定锚点（例如纯 center/constrain 操作），不创建这条恢复状态。

### 6.4 输入路径与非目标路径

滚轮、键盘、菜单、pinch、fit、Toggle 和自定义百分比最终调用 zoomAbsolute()。
旋转、镜像、翻转和全屏自身的 native transition 不属于本设计的图片 zoom
transition；但 zoom commit 会尊重其既有 transform 和全屏保护状态。

## 7. 正确性论证

### 7.1 无中间几何 frame

生产代码删除了几何动画 writer，因此一次请求不会生成中间 zoom 值。提交期间
updates 被关闭，目标 transform、scene rect、range/value 和首次锚点在同一 GUI
临界区内完成；恢复 updates 后，若 Qt 仍在物化 AsNeeded 拓扑，只允许同一场景
锚点的幂等重放，不允许独立的时间插值或任意平移。静默窗口测试进一步排除
拓扑收敛完成后的额外 zoom writer。

### 7.2 最近性与无位置修正

对每个轴，约束固定点集合是闭区间；缩放保持点映射到原点的关系是仿射函数，
其逆像仍是闭区间。闭区间上的欧氏最近点就是 clamp，两个轴的乘积因此是
唯一的最近 box 投影。按该 a 生成的 o1 已在目标固定点集合中，后续
constrain() 的几何位移为零；剩余差异只来自 QScrollBar 整数舍入，限定在
同一提交的 1 DIP 适配误差内。

### 7.3 横条拓扑交叉

AsNeeded 横条可能改变可用 viewport 的高度并反过来影响 V range。实现先写目标
尺寸、同步收敛布局，再按实际目标矩形重投影；现场动态测试要求 H range
0→非零→0，并在 paint/终态样本检查交叉轴锚点。因此“横条出现/消失后再
平移”的旧类问题不能依赖一个等待终态的弱断言逃过测试。

### 7.4 纵条首次出现

在专用 fixture 中，缩放提交时 V range 先为 `-28..8`、value 为 `4`，随后
Qt 将最终上限收敛为 `7`。修复前 value 被写成 `7`，场景锚点在 paint 前下移
3 DIP；修复后 `valueChanged` 路径立即把 value 恢复为由当前 transform 与场景点
计算出的 `4`，所以提交态、稳定态和 paint probe 都保持相同锚点。H range 已
存在，因而该用例还能区分“V 拓扑导致的跳变”和“横向条本身的正常出现”。

## 8. 文件与验证映射

| 内容 | 文件 |
| --- | --- |
| 生产实现 | [src/qvgraphicsview.h](../src/qvgraphicsview.h)、[src/qvgraphicsview.cpp](../src/qvgraphicsview.cpp) |
| action 汇点 | [src/mainwindow.cpp](../src/mainwindow.cpp)、[src/actionmanager.cpp](../src/actionmanager.cpp) |
| 动态 QtTest | [tests/tst_qviewtests.cpp](../tests/tst_qviewtests.cpp) |
| 静态门禁 | [tests/zoom_scrollbar_duration_static.py](../tests/zoom_scrollbar_duration_static.py)、[tests/toggle_fit_stability_static.py](../tests/toggle_fit_stability_static.py) |
| CTest 编排 | [tests/CMakeLists.txt](../tests/CMakeLists.txt) |
| 纵条拓扑回归 | `FovelleZoomScrollbarVerticalTopology` → `testZoomKeepsVerticalScrollbarPositionWhenVerticalRangeAppears` |
| 延迟替换回归 | `FovelleZoomScrollbarExpensiveRefinement` → `testZoomKeepsVerticalScrollbarPositionDuringExpensiveRefinement` |
| 结构化用例 | [reports/test_case_specification.md](test_case_specification.md) |
| 执行证据 | [reports/test_completion_report.md](test_completion_report.md) |

## 9. 证据边界

本设计和测试证明的是 Fovelle/Qt widget 层的可观察提交合同。没有进行逐帧
WindowServer presentation-layer 捕获，因此不声称覆盖 macOS 合成器在屏幕扫描
级别的所有行为。若未来在本合同全部通过后仍能复现屏幕级跳变，应追加同一
时间线上的屏幕/Core Animation 捕获，而不是把未观测现象倒推为本实现已经
证明的事实。
