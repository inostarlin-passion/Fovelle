# Fovelle 图片缩放、拖拽与滚动条问题：技术设计文档

> 文档日期：2026-09-03
> 被测工作树：`/Users/inostarlin/code/Fovelle`（实现尚未提交）
> 本机验证：macOS Cocoa、Qt 6.11.1、普通 DPR；另有独立 `QT_SCALE_FACTOR=2` 轨迹门禁
> 参考调查：[reports/root_cause.md](root_cause.md)

## 1. 目标与范围

本设计修复五类相互关联的视图问题，并把每一类问题拆成可独立判定的原子验收标准：

1. 图片缩小到小于可用视口后，旧的垂直滚动范围不得残留。
2. 放大后拖拽必须连续，且真实溢出轴的滚动范围不得因为取消旧锚点而消失。
3. `Zoom In`、`Zoom Out` 的键盘快捷操作必须使用触发时鼠标位置作为图片内容锚点。
4. `Toggle Fit and 100%` 必须依据当前已显示帧是否 fit 决定目标；fit→100% 放大用鼠标锚点，非 fit→fit 缩小用可用视口中心锚点。
5. 鼠标滚轮放大必须保持鼠标下的图片内容点，右下角不得因坐标域错误而移出视口。

“符合”不是只检查最终 zoom 数值。每个动态检查同时观察图片内容点、scene/view 映射、两轴 range/value、viewport 几何、动画中间帧和延迟 writer 的终态；任一已绘制或已提交的中间状态违反合同即失败。

## 2. 问题分解与证据链

### 2.1 根因到修复的映射

| 问题 | 证据驱动的根因 | 修复策略 |
| --- | --- | --- |
| P1：缩小后垂直条仍存在 | 缩放锚点为保证图片外部点可达而加入的虚拟 scene margin 在结算后被保留；`AsNeeded` 因此看到非零范围。 | `settlePendingZoomAnchor()` 按当前 displayed image 与 usable viewport 分轴裁剪 retained margin；margin 改变时立即 `updateSceneRect()`，使 range 重新由真实内容决定。 |
| P2：拖动跳变、横条消失 | 旧拖动入口清除 pending/retained margin 并重建 scene；伪造的水平范围归零后，Qt 的 alignment 与 viewport 尺寸一起变化，图片发生离散重定位。 | 手动滚动/拖拽以 viewport 为新权威，`cancelPendingZoomAnchor(true)` 只取消旧内容锚点而保留当前可达性 margin；generation 使旧延迟回调失效；拖动后的真实溢出范围不被清除。 |
| P3：键盘快捷键不跟随鼠标 | `QAction::triggered` 只携带 `checked`，原路径没有另外取得 viewport 坐标，`zoomIn/zoomOut` 固定使用中心哨兵。 | 视图缓存最近一次有效 viewport mouse event；动作触发时优先读取缓存，必要时用 `QCursor::pos()` 映射到 viewport；无有效指针才回退中心。 |
| P4：未 fit 时 Toggle 误跳 100% | `calculatedZoomMode` 表示逻辑目标，不等于当前动画 displayed frame；动画开始后模式可能已是 fit，但图片仍溢出。 | `isImageAtFit()` 同时要求 displayed zoom 等于独立计算的 fit level 且 H/V range 为零；Toggle 依据该实际状态选 100% 或 fit；finish/resize 在 AsNeeded 布局稳定后重算 fit。 |
| P5：右下角滚轮放大后不见 | `getDisplayedContentRect()` 已是变换后的显示尺寸，却再次作为 scene rect 传给 `mapFromScene()`，导致 transform 被重复应用。 | 锚点投影统一使用 `scene()->itemsBoundingRect()` 这一未重复变换的 item scene rect；结算后再处理 scrollbar relayout，并按可达范围钳制。 |

### 2.2 显式前提

以下前提是推理条件，不是隐含假设：

- 两轴 scrollbar policy 是 `Qt::ScrollBarAsNeeded`；`QVGraphicsView` 的 Qt transformation anchor 不负责本项目的最终定位，Fovelle 自己恢复锚点。
- fit 的权威状态是当前 displayed frame 达到独立计算的 fit level，并且 H/V range 均为零；`calculatedZoomMode` 只能表示目标意图。
- “可用视口”包含运行时的窗口安全区扣除，不硬编码 macOS titlebar inset。
- 鼠标事件坐标以 viewport 局部 DIP 表示；`mapToScene()` 的输入是 viewport 点，`mapFromScene()` 的输入必须是 scene 坐标。
- 目标点在缩放后不可达时，允许投影到图片边界和 scrollbar 合法 range；测试误差为 2 DIP。
- 动态测试用可生成的 raster fixture，避免把 `/Volumes/CRYSTAL/...` 外置卷作为 CI 前提；该现场路径仍作为人工复现参考：`/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`。
- 结论覆盖显式测试矩阵，不外推到任意平台 style、任意合成器或系统级 Accessibility/HID 事件。

### 2.3 联网多跳检索与交叉验证

检索从证据缺口开始，路径如下：

```text
用户看到的条/图片跳变
  → 区分 range/value、bar geometry、viewport 和图片内容点
  → 查 Qt 的 AsNeeded、sceneRect、坐标映射和 action 事件合同
  → 用 Qt 源码核对两轴 scrollbar 布局的交叉影响
  → 回到 Fovelle 源码追踪 scene margin、动画、锚点和拖拽写入者
  → 用独立内容锚点与物理几何 oracle 交叉验证
  → 用终态 quiet 检查排除延迟回调的迟到重写
```

已核验的公开事实：

- [`QAbstractScrollArea` 官方文档](https://doc.qt.io/qt-6/qabstractscrollarea.html)说明滚动条会占用 viewport 尺寸，`ScrollBarAsNeeded` 依据范围决定是否显示；因此一轴出现会改变另一轴的可用尺寸。
- [`QGraphicsView` 官方文档](https://doc.qt.io/qt-6/qgraphicsview.html)说明 scene、transform、viewport、alignment 和 `mapToScene/mapFromScene` 共同决定内容定位；本设计不把已变换的 content rect 当作 scene 输入。
- [`QAction` 官方文档](https://doc.qt.io/qt-6/qaction.html)说明 `triggered(bool checked = false)` 的信号参数不是鼠标位置；键盘动作必须在视图侧取得最近指针位置或使用明确 fallback。
- [`QVariantAnimation` 官方文档](https://doc.qt.io/QT-6/qvariantanimation.html)说明当前值是在起止值间按时间插值得到；逻辑终值不能代表 200 ms 动画中的每个 displayed frame。
- [`QPropertyAnimation` 官方文档](https://doc.qt.io/QT-6/qpropertyanimation.html)说明属性动画会把中间值写入目标属性；因此 `finishZoomTransition()` 与延迟 writer 必须纳入终态验证。

交叉验证约束：Qt 文档用于确认公开语义，Qt 源码用于确认 AsNeeded 的双轴布局细节，Fovelle 源码和 QtTest trace 用于确认本项目的实际写入顺序。单独的最终截图、最终 zoom、scrollbar value 或逻辑 mode 都不能作为充分证据。

## 3. 原子验收标准

发布门禁定义为以下合取：

```text
AC-ALL
  = AC-SB-NO-STALE-RANGE
  ∧ AC-DRAG-CONTINUOUS
  ∧ AC-DRAG-PRESERVES-OVERFLOW-BARS
  ∧ AC-KBD-ZOOM-CURSOR-ANCHOR
  ∧ AC-TOGGLE-DIRECTIONAL-ANCHOR
  ∧ AC-TOGGLE-VISUAL-STATE
  ∧ AC-WHEEL-CONTENT-ANCHOR
  ∧ AC-NO-LATE-REWRITE
```

| ID | 原子判定 | 主要观测量 | 固化测试代码 |
| --- | --- | --- | --- |
| `AC-SB-NO-STALE-RANGE` | 缩放回 fit 并稳定后，图片小于可用视口的轴 H/V range 均为零，历史 margin 不复活。 | displayed image rect、usable viewport、H/V min/max、quiet range tuple | `GraphicsViewTests::testZoomOutClearsStaleVerticalScrollRange` |
| `AC-DRAG-CONTINUOUS` | 每个拖拽 move 的图片内容位移等于指针 delta，误差不超过 2 DIP；边界只允许合法钳制。 | tracked scene point、press/move/release delta、每步 mapped point | `GraphicsViewTests::testMousePanKeepsOverflowRangeAndContinuity` |
| `AC-DRAG-PRESERVES-OVERFLOW-BARS` | 当前轴仍真实溢出时，开始拖拽/取消旧锚点不得把该轴 range 变为零。 | drag 前后 H/V range 和 maximum | `GraphicsViewTests::testMousePanKeepsOverflowRangeAndContinuity` 的独立 range 断言 |
| `AC-KBD-ZOOM-CURSOR-ANCHOR` | 真实 `Zoom In/Out` shortcut 保持触发时鼠标下同一 scene 内容点。 | cached/global cursor、scene anchor、mapped anchor | `GraphicsViewTests::testKeyboardZoomUsesCursorAnchor` |
| `AC-TOGGLE-DIRECTIONAL-ANCHOR` | Toggle 目标比当前 displayed frame 大时用鼠标锚点，目标更小时用 usable viewport center。 | fit/100 两次方向、cursor anchor、center anchor | `GraphicsViewTests::testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor` |
| `AC-TOGGLE-VISUAL-STATE` | 当前实际未 fit 时 Toggle 目标为 fit；只有实际 fit 时才目标 100%，不被逻辑 mode 提前值误导。 | `isImageAtFit()`、displayed/logical zoom、最终 ranges | 同一测试中的 displayed-state 断言 |
| `AC-WHEEL-CONTENT-ANCHOR` | 真实 wheel 放大后，右下角图片内容点仍在鼠标目标附近，边界可达且不丢失。 | QWheelEvent position、scene UV、mapped image edge | `GraphicsViewTests::testMouseWheelKeepsBottomRightAnchor` |
| `AC-NO-LATE-REWRITE` | animation、settle、post-layout、expensive scale、constraint 完成后不再改写正确终态。 | terminal tuple、timer active 状态、两轮 event loop + 延迟 quiet | `GraphicsViewTests::testZoomTerminalStateDoesNotRewriteViewport` |

## 4. 生产实现设计

### 4.1 坐标域与锚点算法

`zoomAbsolute()` 的输入 `targetPos` 是 viewport DIP。算法先将其投影到当前 image item 的 viewport rect，再用 `mapToScene()` 得到稳定的 scene anchor：

```text
imageScene = scene()->itemsBoundingRect()
imageViewport = mapFromScene(imageScene).boundingRect()
targetViewport = project(targetPos, imageViewport)
anchorScene = mapToScene(targetViewport) - roundingError
```

禁止下列错误路径：把已经由 `getDisplayedContentRect()` 计算过的显示尺寸再传给 `mapFromScene()`。`zoomAnchorViewportPoint()`、`zoomAbsolute()`、`restoreSettledZoomAnchor()` 均基于同一 scene item 几何合同。

锚点的恢复分两阶段：

1. 动画每次 `setAnimatedZoomLevel()` 后恢复 pending scene anchor，保证中间 frame 跟随输入。
2. `settlePendingZoomAnchor()` 清理可失效的虚拟 margin 后保存 settled anchor；`zoomAnchorPostLayoutTimer` 在 AsNeeded 改变 viewport 后再次恢复一次，避免 bar 消失导致的横向/纵向布局回弹。

### 4.2 虚拟 scene margin 与滚动范围

外部图片点需要 margin 才能被放在指定 viewport 位置，但 margin 不是永久内容。结算时按轴执行：

```text
if displayedImage.width  <= usableViewport.width  + 1:
    left/right margin = 0
if displayedImage.height <= usableViewport.height + 1:
    top/bottom margin = 0
```

若旧 scene margin 或 retained margin 发生改变，且当前不是递归 `updateSceneRect()`，立即重建 scene rect。这样 `ScrollBarAsNeeded` 的 range 来源回到真实 image item；P1 的关键不是强行隐藏 scrollbar，而是消除制造 range 的虚拟输入。

### 4.3 拖拽与手动滚动的事务权

拖拽、slider、wheel pan、keyboard pan 和 native pan 都先以 viewport 交互为权威：

- `cancelPendingZoomAnchor(true)` 停止 settle/post-layout timer、递增 generation 并清掉旧 scene anchor；
- preserve 模式保留当前 pending margin，保证仍溢出的图片轴继续有合法 range；
- 不在 `sliderMoved/actionTriggered` 的半提交时机重建 scene rect，避免把旧 value 重放；
- 新的 pan 位移由 `ScrollHelper` 累加，直到边界才交给 scrollbar 合法钳制。

这使 P2 的连续位移和“横条不因取消锚点消失”成为两个独立 oracle，而不是依赖一次终态截图。

### 4.4 键盘缩放与 Toggle 状态机

`getCursorViewportPosition()` 的优先级为：最近一次位于 viewport 内的 mouse event → 可见全局 cursor 映射 → 无有效位置。后者使用 usable viewport center 哨兵。

`zoomIn()` / `zoomOut()` 始终调用该 helper；因此 QAction、菜单和快捷键最终使用相同视图锚点逻辑。`Toggle Fit and 100%` 位于 `QVGraphicsView`，原因是它必须读取 displayed frame：

```text
currentlyAtFit = isImageAtFit()
if currentlyAtFit:
    target = 1.0; anchor = cursor or center
else:
    target = calculateZoomLevelForMode(ZoomToFit)
    anchor = cursor if target > displayedZoom else usableViewport.center()
```

`isImageAtFit()` 不读取单独的 mode 名称，而是比较 displayed zoom 与独立 fit level，并要求 H/V range 为零。`finishZoomTransition()` 和含 pending anchor 的 resize 路径在 scrollbar relayout 后重新计算 fit，防止“图片已包含但倍率仍是旧 fit 值”的假 fit 状态。

### 4.5 异步与终态

缩放动画仍由统一的 `QPropertyAnimation` 驱动，时长 200 ms；pending settle、post-layout reconcile、constraint、expensive scale 和 scrollbar geometry writer 都是可观察的 member timer 或状态。终态处理顺序为：

```text
animation finished
  → setAnimatedZoomLevel(logical zoom) 精确归一化
  → restore pending/settled anchor
  → 依据稳定 viewport 重算 fit（如需要）
  → constrainBounds
  → 等待所有相关 timer inactive
```

`verticalScrollBarGeometryTimer` 使用 named、single-shot、0 ms coalescing；它用于合并 Qt layout 事件并提供可审计的兜底，不作为唯一正确性的来源。

## 5. 测试设计与代码落点

### 5.1 静态测试

`tests/zoom_issue_acceptance_static.py` 检查：

- 八条原子标准是否同时出现在设计、规格和 QtTest marker 中；
- P1–P5 的生产实现合同是否存在；
- 真实 `QWheelEvent`、`QTest::keySequence`、鼠标 drag、独立内容锚点和 range/geometry oracle 是否存在；
- 九个结构化 Markdown case 是否包含六个字段，并显式区分静态/动态、瞬态/稳态；
- 静态脚本和七个动态函数是否注册到 CTest。

CTest 名称：`FovelleZoomIssueStatic`，输出机器证据到 `build/test-results/zoom-issue-acceptance-static.json`。

### 5.2 动态测试

七个 QtTest 函数覆盖八条原子标准；拖拽测试保留两个独立断言块，因为它们共享同一真实输入事务；Toggle 的方向和 displayed-state 分别使用独立函数：

- `testZoomOutClearsStaleVerticalScrollRange`：三格 wheel in、三格 wheel out，检查 fit 图像尺寸、两轴 range 和 quiet 状态。
- `testMousePanKeepsOverflowRangeAndContinuity`：真实 press/move/release，检查 tracked scene point 位移以及 H/V overflow range。
- `testKeyboardZoomUsesCursorAnchor`：真实 shortcut sequence，分别覆盖 Zoom In 与 Zoom Out。
- `testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor`：fit→100 的 cursor anchor、100→fit 的 center anchor，并等待实际 fit。
- `testToggleFitAnd100UsesDisplayedState`：在 fit 动画尚未结束时重复触发 Toggle，确认仍选择 fit；只有稳定 fit 才进入 100%。
- `testMouseWheelKeepsBottomRightAnchor`：真实 `QWheelEvent` 发送到 viewport，检查右下角。
- `testZoomTerminalStateDoesNotRewriteViewport`：Expensive scaling 下检查所有延迟 writer 完成后的 tuple 不变。

固定的 `waitForZoomTerminal()` 等待 animation 和具名 timer 均 inactive；它不以一个固定 sleep 代替状态条件。动态测试还保留既有的 scrollbar trajectory 测试，覆盖键盘、wheel、native pinch、Disabled/Expensive、普通 DPR/HiDPI 的逐时间轨迹。

## 6. 风险与限制

- 默认动态门禁使用 Cocoa QPA；其他平台 style 需要重新核验 platform-specific scrollbar extent 和布局事件顺序。
- 指定 CRYSTAL JPEG 是人工现场复现输入，不作为默认 CTest 依赖；生成 fixture 用于可移植的精确断言。
- 系统级 Accessibility/HID 驱动测试仍是显式 opt-in，不把外部权限状态混入默认门禁。
- 2 DIP 是本测试对 fractional transform、整数 scrollbar value 和 DIP 舍入的容差，不代表任何平台都只允许 2 DIP。
- 本设计验证“应用提交的视图状态不跳变”；不声称能证明 WindowServer/GPU 合成器在应用已提交正确 geometry 后不会产生独立故障。
