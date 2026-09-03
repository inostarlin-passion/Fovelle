# 图片缩放时垂直滚动条跳变：技术设计文档

> 验证提交：Fovelle `b378540e16a4c90a205320ea05a3653460ebd9a7`（生产实现基于 `79712ffb3298403cae55d81f37ec3bfd23862140`）
>
> 验证环境：macOS Cocoa、Qt 6.11.1、普通 DPR 与 `QT_SCALE_FACTOR=2`

## 1. 目标与结论

目标是修复图片缩放期间垂直滚动条出现可见瞬时震荡的问题，并把“不会跳变”定义成可被逐帧验证的合同，而不是只比较缩放前后的最终位置。

本次修复包含两部分：

1. 生产代码在 Qt 的滚动条布局事件中，同步修正全尺寸标题栏安全区导致的垂直滚动条容器顶边；原有零延迟合并任务改为具名 member timer，保留原有延迟语义并使其可观测。
2. 测试代码同时检查 scrollbar value、range/thumb、图片锚点和滚动条物理 geometry，并覆盖真实键盘、滚轮、native pinch、Disabled/Expensive、普通 DPR/HiDPI、逐毫秒动画和延迟回调。

验收结论定义为以下合取：

```text
AC-ZOOM-VBAR-TRANSIENT
  = VALUE ∧ GEOMETRY ∧ ANCHOR ∧ THUMB ∧ ASYNC ∧ MATRIX
```

本设计不把“终态正确”当作“过程正确”。任意中间 paint、提交状态或已执行的延迟回调违反任一谓词，测试即失败，即使之后又回到原位置。

## 2. 对用户问题的直接解释：为什么肉眼看到跳变而测试仍通过

旧测试通过的原因已经由故障轨迹定位，而不是推测：它记录了 `verticalBarGlobalRect`，但旧的 `validateZoomTrace()` 没有比较这个字段；它只比较了 value、独立期望值、锚点误差和“根据当前 scrollbar geometry 计算出来的”thumb。

在旧失败轨迹中，垂直值可以保持不变：

```text
vertical value: 291 → 291 → 291
bar global rect:  (1169, 291, 15, 480)
                  → (1169, 263, 15, 508)
                  → (1169, 291, 15, 480)
```

因此人眼看到的是控件整体先向上移动 28 DIP、轨道高度改变、随后回到安全位置；但 value 没有跳，旧断言就没有失败。旧 thumb oracle 还有一个结构性盲点：实际 thumb 和期望 thumb 都使用同一个“当前滚动条 geometry”调用 `SC_ScrollBarSlider`。如果整个 bar/container 一起移动，两个矩形会一起移动，二者仍然相等，形成自洽但不完整的绿色结果。

这正是本次新增 `verticalBarGlobalRect`、`verticalBarContainerGlobalRect` 的独立 geometry oracle，以及在 `Paint`/committed sample 上比较基线的原因。现在同一轨迹会在第一个绘制到错误顶边的 sample 直接失败；生产修复则在下一次 paint 前同步把容器顶边恢复到标题栏安全区。

## 3. 问题分解与显式前提

### 3.1 需要分别观测的四类“位置”

“垂直滚动条位置”不是一个单一变量，必须拆成以下可证伪对象：

| 对象 | 观测量 | 可见失败例子 |
| --- | --- | --- |
| 数值位置 | `minimum/maximum/value/pageStep` | value 被 future range 夹住，再被锚点恢复 |
| 比例位置 | 当前 range 与 style thumb | value 相同但 range/pageStep 改变，thumb 比例改变 |
| 控件几何 | bar 与 parent container 的全局 `x/top/width/height` | value 不变，container 先到 viewport 顶部再回安全顶边 |
| 内容锚点 | 同一图片内容点的 viewport 坐标 | thumb 看似稳定，但图片内容发生位移 |

### 3.2 分析前提

- 测试运行于 macOS Cocoa；本地验证环境为 Qt 6.11.1 arm64。
- `QVGraphicsView` 使用 `ScrollBarAsNeeded`，并将 `transformationAnchor` 设为 `NoAnchor`；应用代码自行保存/恢复缩放锚点。
- 缩放事务同时存在最终逻辑 zoom 和动画中的当前 displayed zoom；两者在 200 ms 动画期间可能不同。
- 动态 fixture 只有一个 raster image item；垂直方向始终溢出，水平方向在 1.00 与 1.25 附近穿越 `AsNeeded` 阈值。
- 测试结论只覆盖显式矩阵，不外推到任意图片、平台 style、系统级 HID 驱动或合成器故障。

### 3.3 风险链

```text
真实缩放输入
  → 动画改变 displayed transform
  → scene rect / viewport / 两轴 range 重算
  → AsNeeded 的跨轴 layout 改变 viewport 高度
  → macOS 全尺寸标题栏安全区触发 bar/container geometry 更新
  → 延迟 0 ms geometry writer 或下一轮 paint 看到未修正顶边
  → 肉眼看到 y=安全顶边 → viewport 顶边 → 安全顶边
```

同时存在另一条数据链：

```text
Expensive backing pixmap 替换
  → image item scene rect 改变
  → 旧的绝对 scene anchor 不再代表同一图片点
  → anchor settle / range restore 写入错误 vertical value
```

两条链都必须覆盖。只测 value 会漏第一条链；只测 geometry 会漏第二条链。

## 4. 联网检索、多跳取证与交叉验证

检索从证据缺口出发，而不是从“滚动条 bug”关键词直接猜结论：

```text
人眼看到的位移
  → value、range/thumb、bar geometry、图片 anchor 哪一个改变？
  → Qt 谁负责计算 viewport 与 range？
  → AsNeeded 是否会让另一条滚动条改变 viewport？
  → 动画使用的是当前帧还是最终目标？
  → backing pixmap 是否改变 scene 坐标基？
  → 延迟 writer 是否在 paint 前后覆盖了状态？
```

### 4.1 已核验的 Qt 事实

- [`QAbstractScrollArea` 官方文档](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)说明 `ScrollBarAsNeeded` 会在滚动范围非零时显示滚动条；滚动条会占用 viewport 的对应尺寸，因此一条滚动条的出现会影响另一轴的布局。
- [`QGraphicsView` 官方文档](https://doc.qt.io/qt-6/qgraphicsview.html)说明 scene、transform、viewport 和 transformation anchor 共同决定场景在视口中的定位；本项目选择 `NoAnchor` 后，pending anchor 是应用层的定位责任。
- [`QPropertyAnimation` 官方文档](https://doc.qt.io/qt-6/qpropertyanimation.html)说明属性值在起止值之间插值；动画 setter 收到的是中间 displayed frame，不能用最终 logical zoom 的 geometry 代表每一帧。
- Qt 6.11.1 的 [`QGraphicsViewPrivate::recalculateContentSize()` 源码](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)实际查询 scrollbar extent 并处理一轴出现后触发另一轴需求的交叉布局；这与本地 trace 中的 viewport/bar Resize、Move 顺序一致。
- [`QStyle` 官方文档](https://doc.qt.io/qt-6/qstyle.html)提供平台相关控件几何；测试用 `QStyleOptionSlider` 与 `SC_ScrollBarSlider` 计算 thumb，不使用固定截图尺寸。
- [`QNativeGestureEvent` 官方文档](https://doc.qt.io/qt-6/qnativegestureevent.html)定义 native gesture 事件；pinch 测试发送 Begin、Zoom 增量和 End 完整事件流，而不是调用内部 zoom 函数。
- [Qt High DPI 官方文档](https://doc.qt.io/qt-6/highdpi.html)说明 widget 几何使用设备无关像素，而图像缓冲区可能以 DPR 表示；因此测试分别运行普通进程和 `QT_SCALE_FACTOR=2` 进程并记录实际 DPR。

### 4.2 项目内证据链

本地源码和 trace 交叉得到：

1. `zoomAbsolute()` 设置逻辑目标并启动 `QPropertyAnimation`；当前绘制帧由 `animatedZoomLevel` 决定。
2. `updateSceneRect()`/`ScrollHelper` 根据当前 displayed frame 的内容几何维护 range，避免用最终目标制造 future range。
3. `applyExpensiveScaling()` 在 backing pixmap 更换前保存 anchor UV，更换后按新 image rect 重建 scene anchor。
4. Qt layout 更新 `qt_scrollarea_vcontainer` 的 geometry。旧 0 ms callback 在布局后才改 top，因而可能在其间发生一次错误 paint。
5. 本次修复在 `QEvent::Move/Resize/Show` 的 event filter 中同轮调用 `refreshVerticalScrollBarGeometry()`；具名 `verticalScrollBarGeometryTimer` 仍负责零延迟合并和最终兜底。
6. 测试 trace 同时记录 event object、bar/container global rect、value、range、thumb、anchor 和 timer phase，因而能把“数值正确但控件移动”与“锚点写值错误”区分开。

## 5. 设计方案

### 5.1 生产修复

#### A. 把匿名零延迟任务变为可观测 member timer

在 `QVGraphicsView` 中增加：

```cpp
QTimer *verticalScrollBarGeometryTimer;
```

配置为 named、single-shot、interval 0。`scheduleVerticalScrollBarGeometry()` 仍只启动一个 pending timer，保持原 coalescing 语义；timeout 清除 pending 标志并调用 `refreshVerticalScrollBarGeometry()`。

这样做的作用是可观测性和终止合同，不是把异步回调误认为同步修复。测试可以确认这个 writer 确实执行且 terminal 时 inactive。

#### B. 在下一次 paint 前修正全局安全顶边

`QVGraphicsView::eventFilter()` 继续监听垂直 bar/container 的 `LayoutRequest`、`Move`、`Resize`、`Show`：

- 所有事件都 schedule 具名 0 ms timer；
- `Move/Resize/Show` 在事件已赋予新 geometry、但下一次 paint 尚未发生的同一事件轮中，直接调用 `refreshVerticalScrollBarGeometry()`；
- `refreshVerticalScrollBarGeometry()` 只改变 bar/container 的 top，保留 Qt 管理的 bottom，避免破坏 horizontal-bar corner handling；
- `isUpdatingVerticalScrollBarGeometry` 防止 `setGeometry()` 产生的嵌套 Move/Resize 递归；timer 作为下一轮最终兜底。

修复的关键不变量是：

```text
任何可绘制状态的 vertical bar global top
  ≥ viewport global top + obscuredHeight
```

并且在固定窗口缩放事务内，bar/container 的 `x/top/width` 不应在两个可绘制状态之间往返。

#### C. 保留并约束已有缩放坐标一致性

- Scroll range 使用 displayed frame 的 content rect，不使用未来 logical frame；
- Expensive backing pixmap 替换前后按归一化 `(u,v)` 重建 anchor；
- pending anchor 使用 generation 防止过期 callback 覆盖新意图；
- 手动 scrollbar drag、wheel/pan/action 会取消旧 pending anchor；
- style extent 由 Qt platform style 提供，禁止方向固定宽高。

### 5.2 测试修复

测试统一使用 `ZoomTraceProbe`：

- 真实输入：`QTest::keySequence()`、`QWheelEvent`、`QNativeGestureEvent`；
- 真实锚点：keyboard 使用 usable viewport center，wheel/pinch 使用图片内 `(0.40, 0.35)` 非中心点；
- 真实动画：确定性阶段逐一设置每个 integer millisecond，live 阶段保留真实 clock/queued layout/timer 交错；
- 真实 style：按当前 `QStyleOptionSlider` 计算 actual/expected thumb；
- 真实 geometry：记录 viewport、bar 和 bar container 全局矩形；
- 失败证据：保存完整 JSON、first-bad/worst/terminal 三类 frame；
- 预条件：等待平台初始化的 `100×30` placeholder 消失并达到 style extent 后，才建立轨迹 baseline。

## 6. 独立预言机

### 6.1 图片锚点与垂直 value

对 sample `k`，当前 backing image scene rect 为：

```text
I_k = (L_k, T_k, W_k, H_k)
```

输入前固定图片内容归一化坐标 `a=(u,v)`，同一内容点在当前 scene 中为：

```text
p_k = (L_k + u·W_k, T_k + v·H_k)
```

设当前 transform 为 `T_k`，目标 viewport 坐标为 `q_k`。fixture 保证 vertical range 非零、anchor 不在端点，因此期望整数 value 为：

```text
r_k  = y(T_k(p_k)) - q_k
V*_k = clamp(qRound(r_k), vMin_k, vMax_k)
```

代码中的 `verticalExpected` 只读 transform、当前 image rect、anchor UV、目标坐标和 range；它不读取 actual `verticalValue`，避免出现“被测值自证”。

### 6.2 thumb 与物理 geometry

当前 style 下：

```text
actualThumb   = styleRect(sliderPosition = V_k)
expectedThumb = styleRect(sliderPosition = V*_k)
```

其中 `styleRect` 是 `subControlRect(CC_ScrollBar, option, SC_ScrollBarSlider, bar)`。该比较检测 value/range/pageStep 引起的 thumb 偏差；独立的 bar/container 比较检测 value 不变但控件整体移动的情况。

在本测试的固定窗口与安全区模型内：

- `bar/container global x、top、width` 必须保持基线不变；
- height/bottom 可因水平 `AsNeeded` 的合法切换而变化，但不得带来 top 回弹；
- H 只能从起始状态向目标状态切换，不能出现反向回弹；
- vertical range 全程非零。

### 6.3 瞬态与稳态

判定 sample 包括：

- 所有 `paint` sample；
- 连续两个 event-loop turn 无新增相关事件后的 committed sample；
- animation finished 与各 delayed writer timeout 边界 sample；
- deterministic `manual-time-*` sample 和 terminal sample。

因此 `A → B → A` 中的 B 只要被 paint 或提交，就会失败；“最后又回来了”不再掩盖中间错误。

## 7. 覆盖矩阵

| 维度 | 覆盖值 |
| --- | --- |
| 输入 | keyboard / wheel / native pinch |
| 方向 | 1.00→1.25 / 1.25→1.00 |
| scaling | Disabled / Expensive |
| anchor | keyboard center / wheel+pinch fixed image point |
| DPR | 默认 / 独立 `QT_SCALE_FACTOR=2` 进程 |
| 时间 | 逐 integer millisecond / real clock replay |
| 异步源 | animation、range/layout、paint、bar geometry、anchor settle、constraint、expensive scale |

12 行数据在两个 DPR 进程中运行；每行又执行 deterministic 与 live 两阶段。

## 8. 原子验收标准

| ID | 原子标准 | 固化位置 |
| --- | --- | --- |
| `AC-ZOOM-VBAR-VALUE` | 每个可判定 sample 的 actual vertical value 满足独立 `V*` oracle，误差不超过 1 个滚动单位 | `zoomTraceSampleError()`、`validateZoomTrace()` |
| `AC-ZOOM-VBAR-GEOMETRY` | 固定窗口内 bar/container 全局 x、top、width 无可见往返移动 | `ZoomTraceProbe::record()`、`validateZoomTrace()` |
| `AC-ZOOM-VBAR-ANCHOR` | 同一归一化图片点在目标 viewport 位置，X/Y 误差不超过 2 DIP | `ZoomTraceProbe::record()`、`zoomTraceSampleError()` |
| `AC-ZOOM-VBAR-THUMB` | actual thumb 与当前 style 对 expected value 的 thumb 一致，并正确处理 H threshold | `zoomTraceSampleError()`、轨迹 H 状态检查 |
| `AC-ZOOM-VBAR-ASYNC` | animation 与全部相关 delayed writer 执行后，terminal 连续静止且 tuple 不再改变 | live replay、timer phase、terminal check |
| `AC-ZOOM-VBAR-MATRIX` | 三种真实入口、两个方向、两种 scaling、两个 DPR 均执行且覆盖充分 | data function、CTest 注册 |

总标准 `AC-ZOOM-VBAR-TRANSIENT` 是上述六条的合取。

## 9. 变异与可证伪性

为了验证测试不是再次假绿，变异只允许在 disposable worktree：

1. 恢复 stale-range：暂时让 displayed frame 使用最终 logical zoom 的 content rect；预期独立 `verticalExpected` 或 anchor Y 失败。
2. 在动画中点把 vertical value 暂时写成 `V+24`，下一帧恢复；预期逐帧/paint oracle 在中点失败，而只比较终态的对照测试可能仍通过。
3. 暂时跳过 `refreshVerticalScrollBarGeometry()` 的同轮调用；预期 geometry oracle 在 `y=viewportTop` 的 paint sample 失败。

变异代码不进入工作区最终实现；正式通过必须来自未注入的生产代码。

## 10. 非目标与边界

- 本方案不宣称证明所有操作系统、style、图片解码器和窗口管理器绝对不存在滚动条问题。
- 系统级 Accessibility/HID 拖拽不纳入默认 CTest；它需要外部权限和外部图片，不能作为可重复的默认门禁。
- GPU 合成器在 QWidget 已提交正确 geometry 后自行显示错误帧，不属于本 geometry oracle 的可观测范围。
- 若现场仍能复现，应首先收集 `trace.json`，区分 value/range/thumb、bar geometry、anchor 和 timer phase，再决定是否增加新的数据行；不能仅凭肉眼把新现象归因于 DPR。
