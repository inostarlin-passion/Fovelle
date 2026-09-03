# 图片缩放垂直滚动条跳变：测试用例说明

## 1. 测试对象与验收原则

本文件把“缩放时垂直滚动条发生跳变”拆成可独立判定的原子标准，并为每条标准给出完整结构化用例。动态用例由一个数据驱动 QtTest 执行，但每条标准有独立的断言块和代码 marker；这样既避免重复启动昂贵窗口，又能明确每个断言的责任边界。

总验收标准为：

```text
AC-ZOOM-VBAR-TRANSIENT
  = AC-ZOOM-VBAR-VALUE
  ∧ AC-ZOOM-VBAR-GEOMETRY
  ∧ AC-ZOOM-VBAR-ANCHOR
  ∧ AC-ZOOM-VBAR-THUMB
  ∧ AC-ZOOM-VBAR-ASYNC
  ∧ AC-ZOOM-VBAR-MATRIX
```

测试判定对象不是“最后一帧截图”，而是一次缩放事务内的所有可绘制、已提交和延迟回调边界状态。任何 `A → B → A` 中间状态只要被 paint 或提交，就必须失败。

## 2. 原子验收矩阵

| 原子验收标准 | 结构化用例 | 固化测试代码 | 静态/动态 | 瞬态/稳态 |
| --- | --- | --- | --- | --- |
| `AC-ZOOM-VBAR-VALUE`：V 的实际 value 与独立推导值一致 | `TC-ZOOM-VBAR-VALUE-TRAJECTORY` | `zoomTraceSampleError()`、`validateZoomTrace()` 中的 value oracle | 动态，另由静态 marker 校验 | 逐帧 + terminal |
| `AC-ZOOM-VBAR-GEOMETRY`：bar/container 物理位置无错误移动 | `TC-ZOOM-VBAR-GEOMETRY-TRAJECTORY` | `ZoomTraceProbe::record()`、`validateZoomTrace()` 的 geometry oracle | 动态，另由静态 marker 校验 | paint/committed + terminal |
| `AC-ZOOM-VBAR-ANCHOR`：同一图片内容点保持在目标位置 | `TC-ZOOM-VBAR-ANCHOR-TRAJECTORY` | `ZoomTraceProbe::record()` 的 anchor error oracle | 动态，另由静态 marker 校验 | 每个动画时刻 + terminal |
| `AC-ZOOM-VBAR-THUMB`：thumb 与平台 style 和 range 一致 | `TC-ZOOM-VBAR-THUMB-TRAJECTORY` | `zoomScrollBarThumbRect()`、`zoomTraceSampleError()` | 动态，另由静态 marker 校验 | 每个 paint/提交状态 |
| `AC-ZOOM-VBAR-ASYNC`：延迟 writer 执行后不再回弹 | `TC-ZOOM-VBAR-ASYNC-TERMINAL` | live replay 的 timer phase、quiet turns、terminal tuple | 动态，另由静态 marker 校验 | timeout 边界 + 稳态 |
| `AC-ZOOM-VBAR-MATRIX`：入口、方向、缩放模式、DPR 覆盖完整 | `TC-ZOOM-VBAR-INPUT-MATRIX` | data function、CTest normal/HiDPI 注册 | 静态 + 动态 | 两阶段均覆盖 |

另外，静态合同本身用 `TC-ZOOM-VBAR-STATIC-CONTRACT` 验证测试代码确实存在上述独立观测量、输入路径和文档字段；静态通过不能代替动态通过。

## 3. 公共前置与公共后置

### 公共前置条件

- 构建 `fovelle_tests`，使用 Qt 6 Cocoa；普通进程和 `QT_SCALE_FACTOR=2` 进程分别启动。
- 创建可见的 640×480 `MainWindow`，加载一个 raster image，滚动条策略为 `Qt::ScrollBarAsNeeded`。
- 设置 `calculatedzoommode=OriginalSize`、`cursorzoom=true`、缩放步长为 25%，并通过 scoped settings/shortcuts 保存和恢复原值。
- 动态 fixture 由当前 viewport 尺寸生成：宽度约为初始 viewport 的 0.90 倍，高度约为 2.20 倍；V 始终溢出，H 在 1.00/1.25 间跨越 `AsNeeded` 阈值。
- 在建立轨迹 baseline 前，等待 bar 从平台初始化的 `100×30` 占位 geometry 变为 style extent 且位于标题栏安全区内；占位 geometry 不得作为测试基线。

### 公共后置条件

- 关闭测试窗口，移除 event filter，释放 fixture、signal spy 和 scoped settings。
- 失败时写入 `build/test-results/zoom-vbar/<case>/<stage>/trace.json` 及 first-bad、worst、terminal frame；成功时不改变外部用户设置。

## TC-ZOOM-VBAR-NO-TRANSIENT-EXCURSION

### 测试目的

验证六条原子标准的合取：真实缩放输入期间，垂直滚动条不能出现 value、thumb、物理 geometry 或图片锚点的错误瞬态；所有延迟写入完成后仍保持稳定。

### 前置条件

使用公共前置条件；输入源为实际 keyboard、wheel 或 native pinch，且每个数据行都同时执行 deterministic scan 和 live replay。

### 输入数据

12 行数据：3 个输入源 × 2 个方向 × 2 个 scaling mode；keyboard 使用中心锚点，wheel/pinch 使用图片内固定非中心点 `(0.40, 0.35)`。同一 12 行再在普通 DPR 与 `QT_SCALE_FACTOR=2` 独立进程执行。

### 操作步骤

1. 建立动态 fixture，确认 V 有 range、H 起始状态正确、bar geometry 已稳定。
2. 发送真实输入，确认只发出一次 `zoomLevelChanged` 且 200 ms 动画正在运行。
3. deterministic 阶段扫描每个 integer animation millisecond；live 阶段不暂停、不停止 timer，直到 animation 和所有相关 timer inactive。
4. 对每个 checkable sample 应用六个原子断言；再检查 terminal 连续两个 quiet turns 不变。

### 预期结果

所有原子标准通过；任一中间 sample 失败都使该用例失败，即使最终 value 恢复到正确位置。

### 后置条件

执行公共后置条件，并保留失败 trace 供复核。

## TC-ZOOM-VBAR-VALUE-TRAJECTORY

### 测试目的

证明 scrollbar 的数值位置没有被 future range、旧 scene 坐标或延迟回调错误写入。

### 前置条件

使用公共前置条件；V 在所有判定状态中有非零 range，anchor 期望值距离两端至少 2 个滚动单位。

### 输入数据

当前 sample 的 `transform`、backing image scene rect、归一化 `anchorUV`、目标 viewport 坐标、`vMin/vMax`、actual `vValue`。

### 操作步骤

1. 以当前 image rect 重建图片内容点 `p_k`。
2. 计算独立期望值 `V*_k = clamp(qRound(y(T_k(p_k))-q_k), vMin, vMax)`。
3. 在 deterministic 的每个 `manual-time-*` 和 live 的 paint/committed/timer sample 调用 `zoomTraceSampleError()`。

### 预期结果

`abs(vValue - V*) <= 1`，且 `V*` 不接近 range 端点；期望值的计算不读取 actual `vValue`。

### 后置条件

保存或丢弃 probe，恢复窗口和设置；若失败，报告首个 sample 和最大偏差。

## TC-ZOOM-VBAR-GEOMETRY-TRAJECTORY

### 测试目的

捕获“value 不变但滚动条控件整体移动”的视觉跳变，特别是 `safeTop → viewportTop → safeTop` 的瞬时震荡。

### 前置条件

使用公共前置条件；固定窗口尺寸，记录垂直 bar 及其 `qt_scrollarea_vcontainer` 的 global rectangle baseline。

### 输入数据

每个 checkable sample 的 `verticalBarGlobalRect`、`verticalBarContainerGlobalRect`、viewport global rect、当前 H 状态和 animation phase。

### 操作步骤

1. 在第一个有效 paint/committed sample 建立 bar/container baseline。
2. 监听 bar/container 的 Move、Resize、Show、LayoutRequest，并把 paint/提交状态送入 geometry oracle。
3. 比较后续 sample 的 global `x/top/width`；允许 H 合法切换造成 height/bottom 改变，但不允许顶边回到 viewport 原点。

### 预期结果

固定窗口内 bar/container 的 `x/top/width` 始终与 baseline 一致；不能出现旧轨迹中的 `y=291→263→291`。若同轮 Move/Resize 后下一次 paint 仍看见未修正顶边，用例立即失败。

### 后置条件

移除 geometry event filter；失败时保留 event object、geometry 和对应 sample/frame。

## TC-ZOOM-VBAR-ANCHOR-TRAJECTORY

### 测试目的

证明缩放或 Expensive backing pixmap 替换不会让同一图片内容点相对目标 viewport 发生位移。

### 前置条件

使用公共前置条件；keyboard 使用每帧 usable viewport center，wheel/pinch 使用固定非中心图片点。

### 输入数据

旧/当前 image scene rect、归一化 `anchorUV`、当前 transform、固定目标 viewport point、`anchorViewportActual`。

### 操作步骤

1. 用当前 backing image rect 将 `anchorUV` 重建为 `anchorScene`。
2. 通过 `mapFromScene(anchorScene)` 得到实际 viewport 坐标。
3. 在每个动画时间、backing 替换后和延迟 settle 后检查 X/Y 误差。

### 预期结果

`abs(anchorErrorX) <= 2 DIP` 且 `abs(anchorErrorY) <= 2 DIP`；换 backing pixmap 后仍代表同一图片内容点。

### 后置条件

关闭窗口并恢复 setting；失败时把旧/新 image rect 与 UV 一并写入 trace。

## TC-ZOOM-VBAR-THUMB-TRAJECTORY

### 测试目的

证明垂直 thumb 的视觉位置由正确 value、range、pageStep 和当前平台 style 共同决定，而不是仅凭 value 或固定截图判断。

### 前置条件

使用公共前置条件；当前 bar 已达到平台 `QStyle::PM_ScrollBarExtent`，H 的出现/消失会真实改变 viewport 几何。

### 输入数据

`QStyleOptionSlider` 的 orientation、minimum、maximum、pageStep、sliderPosition，以及实际和独立期望的 slider position。

### 操作步骤

1. 通过 `SC_ScrollBarSlider` 计算 actual thumb。
2. 用独立 `V*` 计算 expected thumb。
3. 比较 top、centerY、bottom 的最大差，并检查 H 只向目标方向切换。

### 预期结果

actual/expected thumb 的最大差不超过 1 DIP；H 不反向回弹；V 全程可滚动。

### 后置条件

释放 style option 和 probe；失败时记录实际/期望矩形。

## TC-ZOOM-VBAR-ASYNC-TERMINAL

### 测试目的

验证 0 ms scrollbar geometry writer、anchor settle、constraint 和 Expensive scale 等延迟源不会在动画结束后再次覆盖正确状态。

### 前置条件

使用公共前置条件；live 阶段保留真实 event-loop 调度，不用固定 sleep 作为成功判据。

### 输入数据

`animation-finished`、`verticalScrollBarGeometryTimer-timeout`、`zoomAnchorSettleTimer-timeout`、`constrainBoundsTimer-timeout`、Expensive 行的 `expensiveScaleTimer-timeout`，以及 terminal 的 H/V、viewport、scene、bar、anchor tuple。

### 操作步骤

1. 真实发送输入并记录所有已订阅 phase。
2. 等待 animation 停止、全部相关 timer inactive、连续两个 quiet turns。
3. 记录 `live-terminal`，再次处理事件并比较终态 tuple。

### 预期结果

所有必要 timeout 实际出现；相关 timer 最终 inactive；terminal tuple 在 quiet turns 后完全不变，不发生回弹。

### 后置条件

关闭窗口，销毁 named timer 的 probe 连接，保存 terminal frame。

## TC-ZOOM-VBAR-INPUT-MATRIX

### 测试目的

证明测试没有只覆盖一种输入或只覆盖中心锚点；每条输入等价类都实际进入生产缩放路径。

### 前置条件

使用公共前置条件；CTest 为普通 DPR 和 HiDPI 分别注册独立进程，避免运行时临时修改 DPR。

### 输入数据

| 输入源 | 事件流 | 锚点 | 方向 | scaling |
| --- | --- | --- | --- | --- |
| keyboard | `QTest::keySequence()` 使用 action 当前 shortcut | viewport center | in/out | Disabled/Expensive |
| wheel | 实际 `QWheelEvent` 发到 viewport | 图片 `(0.40,0.35)` | in/out | Disabled/Expensive |
| pinch | Begin → Zoom → End `QNativeGestureEvent` | 图片 `(0.40,0.35)` | in/out | Disabled/Expensive |

### 操作步骤

1. 数据函数生成 12 行；每行执行 deterministic 和 live 两阶段。
2. 检查实际事件被接受、只发出一次 `zoomLevelChanged`、动画有非端点帧。
3. 在 `FovelleZoomScrollbarTrajectory` 和 `FovelleZoomScrollbarTrajectoryHiDpi` 中重复执行。

### 预期结果

12 行在两个进程均执行，不因条件不足而 skip；每行都观察到应有 range/layout/paint/timer phase，并通过总合同。

### 后置条件

恢复环境变量、窗口和设置；CTest 返回 0 才能把矩阵标准标记为 PASS。

## TC-ZOOM-VBAR-STATIC-CONTRACT

### 测试目的

以静态检查验证生产修复、测试入口、独立 oracle、几何观测量、延迟 timer 和本文件的六字段结构没有被删除或回退。

### 前置条件

仓库源码、测试源码、CTest 配置和本 Markdown 文件可读；不需要启动 GUI。

### 输入数据

`tests/scrollbar_zoom_acceptance_static.py` 读取 `src/qvgraphicsview.cpp/.h`、`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt` 和本文件。

### 操作步骤

运行：

```bash
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output build/test-results/scrollbar-zoom-acceptance-static.json
```

### 预期结果

返回码为 0；所有静态 checks 为 `pass=true`，包括 named zero-delay geometry timer、真实 pinch、非中心锚点、`verticalBarContainerGlobalRect`、独立 value oracle、style thumb oracle、失败证据和八个结构化用例的六个字段。

### 后置条件

生成 JSON 机器证据；不修改源码、不修改用户设置。

## 4. 采样与判定细节

### 确定性阶段

真实输入启动动画后暂停 animation，停止 anchor/constraint/expensive 的墙钟 timer，但保留 `verticalScrollBarGeometryTimer`。对 `t=0..D-1` 逐毫秒 `setCurrentTime(t)`，排空事件后记录 `manual-time-t`；恢复 animation，记录 terminal。

### 真实阶段

新建窗口并重新发送同一真实输入，不暂停动画、不停止 timer；记录自然 paint、layout、Move/Resize、range/value 和 timeout，直到动画停止、四个相关 timer inactive、连续两个 quiet turns 后记录 terminal。

### 为什么几何 oracle 能关闭原来的假绿

旧 oracle 的 actual/expected thumb 使用同一个当前 bar geometry；整体 bar 搬家时二者一起搬家。新的 geometry oracle 直接比较 bar/container 的 global `x/top/width`，所以即使 `vValue` 不变、thumb 相对自身轨道也不变，物理位移仍会失败。

### 失败证据

`ZoomTraceProbe` 保存 phase、event object、sample 时间、DPR、viewport、bar/container、range/value、thumb、anchor 和 timer 状态；失败 frame 分别命名为 `first-bad-frame.png`、`worst-frame.png`、`terminal-frame.png`。抓帧在事件循环回合结束后执行，避免从 paint/event filter 回调中递归 `grab()`。

## 5. 参考事实

- [Qt QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)：`AsNeeded` 滚动条与 viewport 尺寸的关系。
- [Qt QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)：scene、transform、viewport 和 anchor 的关系。
- [Qt QPropertyAnimation](https://doc.qt.io/qt-6/qpropertyanimation.html)：属性插值和中间动画值。
- [Qt QStyle](https://doc.qt.io/qt-6/qstyle.html)：平台相关控件和 sub-control geometry。
- [Qt QNativeGestureEvent](https://doc.qt.io/qt-6/qnativegestureevent.html)：native pinch 事件。
- [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html)：设备无关坐标和 DPR。
- [`reports/root_cause.md`](root_cause.md)
- [`reports/technical_implementation_plan.md`](technical_implementation_plan.md)
