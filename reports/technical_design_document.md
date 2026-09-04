# 图片缩放滚动条跳变与 Toggle Fit/100% 锚点稳定性技术设计

日期：2026-09-04
仓库：`/Users/inostarlin/code/Fovelle`
参考调查：[reports/root_cause.md](root_cause.md)

## 1. 目标、范围与非目标

本设计处理两个用户可见问题：

1. 图片缩放跨越 `Qt::ScrollBarAsNeeded` 的水平滚动条边界时，横条出现或消失不能使图片发生额外的位置跳变。
2. 触发 “Toggle Fit and 100%” 时，缩放锚点必须在一次事务内稳定，且 Fit/100% 的动画过程和终态不应出现尾部回弹。

本次不改变图片解码、滚动条策略、快捷键名称、用户设置格式或原生渲染后端；只收敛缩放事务中的 viewport/scene anchor、滚动条布局和延迟 writer 的时序。

## 2. 问题结构化分解与显式前提

### 2.1 显式前提

- P1：默认离散滚轮倍率为 `1.25`；一次普通 wheel detent 的输入位置是固定的 `QPoint`。
- P2：横纵滚动条使用 `ScrollBarAsNeeded`；滚动条出现会改变 viewport 几何，而不仅仅是显示一枚控件。
- P3：H 条切换期间，测试关注的是同一 scene 点的屏幕映射。可接受的几何边界是原生横条厚度导致的半厚度位移和整数舍入；不能接受先呈现错误位置、再由 timer 回弹。
- P4：真实 JPEG 只在挂载 `/Volumes/CRYSTAL` 的机器上存在；不可访问时用同尺寸 `3840×4407` 合成图验证算法路径，不把 fallback 当作真实文件证据。
- P5：普通 wheel/keyboard/menu 单步动画基线为 `200 ms`；语义跳转采用 `200–400 ms` 的乘性距离规则。`400 ms` 是产品工程上限，不是外部规范强制值。
- P6：QtTest 能可靠观察 Qt widget/model 的 `range/value/resize/paint` 边界，但不能单凭 QWidget 事件证明 WindowServer/CALayer 的逐扫描呈现；报告对此保留证据边界。

### 2.2 原子验收标准

| ID | 原子标准 | 可核验证据 |
| --- | --- | --- |
| `AC-HBAR-01-ROUND-TRIP` | 3 格预热后第 4 格使 H range 从零变为非零；反向 1 格使其归零；终态 H/V 正确。 | 真实 `QWheelEvent` 的 range 轨迹 |
| `AC-HBAR-02-ANCHOR-CONTINUITY` | H topology 变化期间，固定 wheel scene anchor 的可见映射不发生不可预测跳变；只允许横条几何造成的半厚度/舍入边界。 | `paint`、`resize`、terminal 样本及固定 anchor 断言 |
| `AC-TOGGLE-DIRECTIONAL-ANCHOR` | Fit→100% 使用鼠标内容点；100%→Fit 使用可用 viewport 中心；两端达到正确状态。 | Toggle 定向锚点 QtTest |
| `AC-TOGGLE-FROZEN-CENTER-ANCHOR` | 中心哨兵在请求开始时解析为一个具体 viewport 点，H 条消失时不重新取当前中心。 | 100%→Fit 中间拓扑状态的 y 坐标断言 |
| `AC-TOGGLE-ANCHOR-LIFETIME` | pending anchor 至少存活到实际动画结束，并覆盖后续布局结算；settle deadline = 实际动画时长 + 延迟。 | 动态测试检查 animation/timer 生命周期 |
| `AC-TOGGLE-MONOTONIC-TERMINAL` | 100%→Fit 的显示图像尺寸单调收缩；animation-finished、terminal 与最后动画值一致。 | 轨迹 probe |
| `AC-TOGGLE-QUIESCENT-FINAL` | 所有延迟 writer 停止后，额外静默窗口内图像尺寸与 viewport 状态不变。 | 650 ms quiet-window 样本 |
| `AC-DURATION-01-LOG-DISTANCE` | 语义缩放时长按 `abs(log2(target/current))` 计算，并限制在 200–400 ms；等倍率距离等时长。 | 无 GUI 纯函数测试 |
| `AC-DURATION-02-FIXED-STEP` | wheel、keyboard Zoom In/Out、菜单单步仍为 200 ms；Toggle 语义跳转使用有界自适应时长。 | 多入口动态测试 |
| `AC-STATIC-01-TRACEABILITY` | 生产代码、结构化用例、测试代码、CTest 注册和完成报告相互可追溯。 | Python 静态 gate |

## 3. 联网多跳检索、证据缺口与推理链

本次按“问题分解 → 证据缺口 → 交叉验证 → 链式推理 → 可核验结论”的顺序检索。外部资料只用来确定 Qt 的公开契约和动画建模背景；产品行为以本地源码、root-cause 调查和可重复 QtTest 为主。

### 3.1 多跳证据

| 跳数 | 证据 | 经核验事实 | 显式推理 |
| ---: | --- | --- | --- |
| 1 | [root cause 调查](root_cause.md) | 当前实现同时修改 transform、scene rect、scrollbar value，并有 resize/timer/native renderer 多个 writer。 | 若这些 writer 不共享一个 anchor 事务，H 条 topology 改变就可能暴露中间几何。 |
| 2 | [QGraphicsView 官方文档](https://doc.qt.io/qt-6/qgraphicsview.html) | scene 完全可见时，`alignment` 参与定位；scene 溢出时，scrollbar/viewport 定位取代单纯居中。 | H range 从零到非零是定位模型切换，不能只比较最终 zoom 值。 |
| 3 | [QAbstractScrollArea 官方文档](https://doc.qt.io/qt-6/qabstractscrollarea.html) | 可选滚动条与 viewport 共同布局；滚动条可见性会改变可用 viewport。 | 中心哨兵若每帧重新计算，条出现/消失会改变屏幕目标点。 |
| 4 | [Qt 6.11.1 `qgraphicsview.cpp`](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp) | `recalculateContentSize` 在可容纳时使用 indent，在溢出时建立 scrollbar range；内容 offset 由 indent/value 共同决定。 | H 条切换会改变 range、indent、viewport 和 value 的组合；应用层必须在拓扑完成后再次恢复同一 anchor。 |
| 5 | [D3 zoom interpolation](https://d3js.org/d3-interpolate/zoom) 与 [Apple Motion HIG](https://developer.apple.com/design/human-interface-guidelines/motion) | 缩放路径适合按乘性距离建模；反馈动画应短、明确、可中断。 | 语义 Fit↔100% 用 log-distance duration；普通单步保持既有 200 ms 契约。 |

### 3.2 交叉验证与证据缺口

- 本地交叉验证：真实 JPEG 与 `3840×4407` 合成图走同一 wheel/Toggle 入口；只在真实 JPEG 可读时将它标记为 provided row。
- 几何交叉验证：同时记录 image rect、usable viewport、H/V range/value、animation value、resize、paint 和 timer 事件，不以 scrollbar value 单一字段推断位置。
- 测试环境交叉验证：先移除并在退出时恢复 `QSettings` 的 `geometry`，避免 `MainWindow` 的持久化窗口状态覆盖测试窗口尺寸。
- 证据缺口：没有在本次 QtTest 中捕获 WindowServer presentation tree 或逐扫描屏幕像素，因此结论是“Qt/Fovelle 可见 widget/native 提交边界没有未闭合的模型跳变”；不扩张为硬件屏幕级证明。

## 4. 实现设计

### 4.1 H 条 topology 事务

在 `QVGraphicsView::setAnimatedZoomLevel()` 中：

1. 动画运行时暂时关闭 view、viewport、H/V scrollbar 的 QWidget updates。
2. 记录变换前的 H overflow 状态，设置当前显示 zoom，调用 `updateSceneRect()`。
3. 立即用 pending scene anchor 和固定 viewport anchor 恢复滚动位置。
4. 若 H overflow 状态发生变化，在更新不可见期间处理当前已排队的 Qt layout 事件，再恢复一次 anchor，并请求 native renderer 更新。
5. 恢复原有 updates 状态；非 topology 变化帧不额外 drain event loop。
6. `resizeEvent`、event filter 和 post-layout timer 继续使用同一 pending anchor；它们不得重新把中心哨兵解释为新的可用中心。

这样做的关键不是把滚动条永远隐藏，而是令“transform + range/value + viewport 几何 + anchor”作为同一逻辑 frame 提交。`processEvents` 只限于动画期间、H topology 变化和 updates 已关闭的窄路径，并排除用户输入/socket，避免交互重入。

### 4.2 Toggle 锚点冻结

`zoomAbsolute()` 在请求入口将 `Qv::CalculateViewportCenterPos` 解析为当前 `getUsableViewportRect().center()`，然后保存为具体的 `pendingZoomAnchorViewport`；后续逐帧恢复只读取这个 snapshot。

`finishZoomTransition()`、`resizeEvent()` 和 fit 重新计算都传递同一个具体 anchor。这样即便横条使 usable viewport 改变，动画中的 scene point 也不会跟随新的 viewport center 漂移；当某一轴已经完全 fit、Qt 无法用 scrollbar 强制任意 scene 点时，仍遵循正常 alignment/constrain 约束。

### 4.3 Anchor 生命周期与动画时长

- 基础单步动画保持 `ZoomTransitionDurationMs = 200`。
- Fit/Fill 等语义计算和 Toggle 使用 `zoomTransitionDurationMs()`，按 `abs(log2(target/current))` 映射到 200–400 ms。
- settle timer 在每次动画请求确定实际 duration 后设置为 `duration + ZoomAnchorSettleDelayMs`，而不是固定假定 200 ms。
- timer 到期但 animation 仍运行时，按剩余动画时间重试，不清空 pending anchor。
- 动画结束后做一次明确终态归一化，再允许 post-layout anchor reconciliation；所有 writer 完成后才进入 quiet state。

### 4.4 风险控制

- 整数 scrollbar value 仍可能产生约 1 DIP 舍入误差；测试将其与半条厚度边界分开记录。
- Qt 文档并不把 `processEvents()` 推荐为一般业务控制流，因此该调用被限制在 topology transaction 内；若未来增加异步 geometry writer，必须纳入同一 probe 和提交屏障。
- 本修复不宣称解决所有原生 CALayer 时序问题；需要屏幕捕获时，应在目标 macOS 机器上增加 presentation-layer 证据。

## 5. 代码、测试与报告映射

| 内容 | 路径/入口 |
| --- | --- |
| 生产实现 | `src/qvgraphicsview.h`、`src/qvgraphicsview.cpp` |
| H 条与 duration 动态测试 | `GraphicsViewTests::testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump`、`testZoomTransitionDurationUsesLogDistance` |
| Toggle 锚点动态测试 | `testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor`、`testToggleFitAnd100FreezesViewportCenterDuringScrollbarTransition`、`testToggleFitReturnHasMonotonicStableTerminalSize` |
| 固化静态测试 | `tests/zoom_scrollbar_duration_static.py`、`tests/toggle_fit_stability_static.py` |
| CTest 注册 | `tests/CMakeLists.txt` |
| 结构化用例 | `reports/test_case_specification.md` |
| 执行证据 | `reports/test_completion_report.md` |
