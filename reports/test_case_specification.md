# 图片缩放垂直滚动条跳变测试用例说明

## 1. 原子验收矩阵

本次问题只有一个可独立判定的原子验收标准；其内部谓词分别检查输入路径、动画瞬态、延迟回调和稳态，避免只比较最终 value 而漏掉 A→B→A。

| 原子标准 | 结构化用例 | 固化测试代码 | 静态阶段 | 动态阶段 | 瞬态 | 稳态 |
| --- | --- | --- | --- | --- | --- | --- |
| `AC-ZOOM-VBAR-TRANSIENT`：键盘/滚轮缩放全过程垂直滚动条无错误位移，结束后保持稳定 | `TC-ZOOM-VBAR-NO-TRANSIENT-EXCURSION` | `GraphicsViewTests::testZoomShortcutsKeepVerticalScrollbarStable_data/testZoomShortcutsKeepVerticalScrollbarStable` | 源码合同、CTest 清单、测试入口 | Cocoa QtTest 真实输入和轨迹 oracle | 逐毫秒、paint、range/layout、timer timeout | animation finished + timers quiet + terminal |

## TC-ZOOM-VBAR-NO-TRANSIENT-EXCURSION

### 测试目的

验证通过键盘快捷键或鼠标滚轮缩放图片时，垂直滚动条不会从稳定位置跳到错误位置再跳回；同时证明修复覆盖普通平滑缩放、高开销 backing-pixmap 缩放、放大/缩小和 HiDPI。

### 前置条件

- 已构建 `fovelle_tests`，Qt 6 Cocoa 平台可启动 `QApplication`、`MainWindow` 和 `QVGraphicsView`。
- 测试设置使用 `OriginalSize`，窗口固定为 640×480，垂直/水平 scrollbar policy 为 `AsNeeded`。
- `cursorzoom=true`，延迟 constraint 未禁用；测试通过 scoped settings 和 shortcut values 恢复外部配置。
- 每个阶段使用全新窗口；先加载 1×4096 几何探针测量真实 viewport，再创建动态 fixture，使垂直条有非零 range、水平条在 1.00/1.25 附近跨过阈值。
- 普通进程和 `QT_SCALE_FACTOR=2` 进程均可运行；测试不依赖 Accessibility 权限或外部图片卷。

### 输入数据

测试数据行是以下笛卡尔积，共 8 行：

| 输入源 | 方向 | 缩放模式 |
| --- | --- | --- |
| 实际 action shortcut：`QTest::keySequence()` | 1.00→1.25 | `Disabled` |
| 实际 action shortcut：`QTest::keySequence()` | 1.25→1.00 | `Disabled` |
| 实际 `QWheelEvent` 发送到 viewport | 1.00→1.25 | `Disabled` |
| 实际 `QWheelEvent` 发送到 viewport | 1.25→1.00 | `Disabled` |
| 上述两种输入 | 两个方向 | `Expensive` |

每行固定一个图片归一化锚点 `anchorUV`；本轨迹用例让键盘和滚轮都从可用 viewport 中心开始，以便水平条的变化只由缩放阈值决定，图片内外及偏离中心的锚点由独立投影回归覆盖。记录字段包括：monotonic time、animation time、logical/displayed zoom、transform、scene/image/viewport rect、DPR、H/V visible/range/value/page step、垂直条 global rect、实际/期望 thumb rect、anchor scene/target/actual/error。

其中轨迹 JSON 的 `h_visible/v_visible` 按 scrollbar `maximum > minimum` 表示可滚动能力，而不是读取 Cocoa overlay scrollbar widget 的当前显示状态；widget 激活态不会改变本验收的几何判定。

### 操作步骤

1. 运行静态合同 `tests/scrollbar_zoom_acceptance_static.py`，检查生产代码存在 anchor rebase、成员 settle timer，测试代码存在真实键盘/滚轮输入、独立 oracle、样式 thumb 计算、失败产物和普通/HiDPI CTest 注册。
2. 对每个数据行创建确定性阶段窗口，打开动态 fixture，确认垂直 scrollbar 有 range，并记录起始水平可见状态。
3. 通过 `QTest::keySequence()` 或真实 `QWheelEvent` 触发产品路径；不直接调用 QAction `trigger()`，不直接调用 view 的 zoom API 代替输入。确认只发出一次 `zoomLevelChanged` 且 200ms 动画运行。
4. **确定性瞬态扫描**：暂停 `zoomTransitionAnimation`，停止 `zoomAnchorSettleTimer`、`constrainBoundsTimer` 和 `expensiveScaleTimer`；对每个整数 `animationTime=0..duration-1` 调用 `setCurrentTime(animationTime)`，每次等待两个 quiet event-loop turns 后记录 `manual-time-*` sample；恢复动画并记录 terminal。
5. 对同一数据行创建全新动态阶段窗口，重新发送完全相同的真实输入；不停止任何 timer，记录每个 paint、range/value/state/layout 事件和 timer timeout，直到动画停止、相关 timer inactive 且连续两个 quiet turns 后记录 `live-terminal`。
6. 对每条 checkable sample 使用独立 oracle：以当前 image rect 和固定 `anchorUV` 计算 `anchorScene`，再以当前 transform 计算 `verticalExpected`；使用 `QStyleOptionSlider`/`SC_ScrollBarSlider` 计算期望 thumb。
7. 若失败，保存完整 `trace.json` 及 first-bad、worst、terminal 三帧 PNG；输出首个失败 sample、最大偏差和阶段，供多跳定位。
8. 关闭窗口并检查 scoped settings、shortcut 和退出策略已恢复，然后进入下一数据行。

### 预期结果

对普通 DPR 和 HiDPI 的全部 8 行均满足：

- 每个 committed/paint/timer checkable sample 的垂直 scrollbar 可见且有 range；
- `verticalExpected = round(transform.map(anchorScene).y - anchorViewportTarget.y)`，并且 `abs(verticalValue - verticalExpected) <= 1`；该 oracle 不使用实际 `verticalValue` 推导期望值；
- 固定归一化锚点的垂直误差不超过 2 DIP；垂直 thumb 的 top/center/bottom 与 style oracle 的差不超过 1 DIP；期望值不贴近 range 端点；
- live 阶段能观察到 `animation-finished`、`zoomAnchorSettleTimer-timeout`、`constrainBoundsTimer-timeout`；Expensive 行还能观察到 `expensiveScaleTimer-timeout`；
- 动画中水平条只按起始到目标的布局阈值方向切换，不反向迟滞；垂直条不存在 A→B→A 的错误 excursion；
- `live-terminal` 的 logical zoom 与 displayed zoom 等价，所有相关 timer 停止，event loop 连续两个 quiet turns 后仍通过全部检查。

任意一条谓词失败即该原子标准失败，即使最终 scrollbar value 恰好恢复。

### 后置条件

- 测试窗口、临时 fixture、event filter、signal spy 和 translator/settings scope 均释放。
- 失败时保留 `build/test-results/zoom-vbar/<case>/<stage>/trace.json` 和三张帧图；通过时不产生需要人工清理的 UI 或外部浏览器状态。
- CTest 进程结束后不改变用户快捷键、缩放设置或 QSettings 中的原值。

## 3. 静态阶段用例

静态入口为 `tests/scrollbar_zoom_acceptance_static.py`。它不把字符串存在当作动态通过，而是检查以下可执行合同：

- `src/qvgraphicsview.cpp/.h` 有 `zoomAnchorSettleTimer`、generation guard、当前 backing image rect 的 anchor UV 重基准，以及 `updateSceneRect()` 的统一几何路径；
- `tests/tst_qviewtests.cpp` 有 `ZoomTraceProbe`、`QTest::keySequence`、`QWheelEvent`、逐整数时间扫描、timer phase、独立 `verticalExpected` 和 `SC_ScrollBarSlider` oracle；
- `tests/CMakeLists.txt` 注册 `FovelleZoomScrollbarTrajectory` 和 `FovelleZoomScrollbarTrajectoryHiDpi`；
- 本文件的用例包含六个规定字段。

静态阶段还保留已有滚动条端点、手动 pan、scene resize 和 Qt style extent 检查，作为交叉验证，不替代本用例的全过程轨迹。

## 4. 失败注入校准方案

为了证明测试对原缺陷敏感，可在隔离副本中临时将 `applyExpensiveScaling()` 的 anchor UV 重基准删除，或在 `updateSceneRect()` 中恢复旧的最终 range 写入，然后只运行 `wheel-zoom-out-expensive`。预期测试在 `verticalValue`、`verticalExpected` 或 anchor Y 处失败，并产生 trace 与三帧证据；校准完成后丢弃隔离副本，不把故障代码带回工作区。

该步骤是测试有效性校准，不是产品通过条件；正式通过只能来自未注入故障的源码。

## 5. 交叉验证入口

| 入口 | 作用 |
| --- | --- |
| `tests/scrollbar_zoom_acceptance_static.py` | 静态生产/测试合同与文档字段 |
| `GraphicsViewTests::testZoomShortcutsKeepVerticalScrollbarStable` | 8 行 × 两阶段的动态轨迹验收 |
| `FovelleZoomScrollbarTrajectory` | 普通 DPR CTest 进程 |
| `FovelleZoomScrollbarTrajectoryHiDpi` | `QT_SCALE_FACTOR=2` CTest 进程 |
| 默认 `FovelleTests` | 既有 GraphicsView/Window 行为回归交叉验证 |

## 6. 参考资料

- [Qt QGraphicsView sceneRect](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop)
- [Qt QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)
- [Qt QPropertyAnimation](https://doc.qt.io/qt-6/qpropertyanimation.html)
- [Qt QTransform](https://doc.qt.io/qt-6/qtransform.html)
- [Qt QAbstractSlider](https://doc.qt.io/qt-6/qabstractslider.html)
- [Qt QStyle](https://doc.qt.io/qt-6/qstyle.html)
- [`reports/root_cause.md`](root_cause.md)

## 历史快捷键回归附录

以下四个用例服务于仓库中已有的快捷键静态合同，不属于本次垂直滚动条原子标准。

## TC-SC-ACTION-SURFACE

### 测试目的

确认 View 菜单和 action library 使用唯一的 `togglefitand100` action。

### 前置条件

已初始化 ActionManager 和 View 菜单。

### 输入数据

`togglefitand100` key、View 菜单 action tree 和测试 marker。

### 操作步骤

运行快捷键 action-surface QtTest 与对应静态合同。

### 预期结果

新 action 存在，旧 fit/original/navigation action 不再注册或 clone。

### 后置条件

菜单和测试对象释放。

## TC-SC-DEFAULT-Z

### 测试目的

确认 combined fit/100% shortcut 的默认键为 Z。

### 前置条件

ShortcutManager 已初始化且使用 scoped QSettings。

### 输入数据

默认 shortcut inventory 和 `Qt::Key_Z`。

### 操作步骤

运行默认快捷键 QtTest，并检查静态 inventory。

### 预期结果

默认值为 Z，旧快捷键行不存在。

### 后置条件

恢复 QSettings 原值。

## TC-SC-TOGGLE-BEHAVIOR

### 测试目的

确认 combined action 在 ZoomToFit 与 100% 之间切换。

### 前置条件

可见窗口已加载图片，MainWindow 状态机可用。

### 输入数据

一次 action 触发和 fit/100% 两个 calculated zoom state。

### 操作步骤

连续触发 action，分别读取 calculated mode、zoom level 并等待布局。

### 预期结果

非 fit 状态进入 ZoomToFit，fit 状态进入精确 1.0，并完成约束。

### 后置条件

关闭窗口并恢复测试设置。

## TC-SC-TRANSLATIONS

### 测试目的

确认四个支持语言目录包含已完成的 combined shortcut 翻译。

### 前置条件

四个 TS/QM catalog 可读取。

### 输入数据

简体中文、繁体中文、西班牙语和日语 catalog。

### 操作步骤

运行 translation QtTest，分别查询 ActionManager 与 ShortcutManager context。

### 预期结果

两个 production context 均返回非空且非 unfinished 的目标译文。

### 后置条件

卸载临时 translator。
