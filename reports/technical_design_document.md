# Fovelle 图片缩放四项问题：技术设计文档

> 文档日期：2026-09-03
> 工作树：`/Users/inostarlin/code/Fovelle`（实现尚未提交）
> 验证环境：macOS Cocoa、Qt 6.11.1、arm64；动态门禁另有 HiDPI 轨迹回归
> 根因证据：[reports/root_cause.md](root_cause.md)
> 测试合同：[reports/test_case_specification.md](test_case_specification.md)

## 1. 目标与范围

本设计只处理用户提出的四个问题，现场样例为：

`/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`

外置卷不是 CI 前提；动态门禁使用相同比例、可重复生成的 raster fixture，另保留该路径供人工复现。

| 问题 | 原子验收标准 | 主测试代码 |
| --- | --- | --- |
| P1：每次缩放后图片位置跳变 | `AC-P1-ANCHOR-CONTINUITY`、`AC-P1-NO-LATE-JUMP` | `testWheelZoomHasNoPositionJumpTrajectory()` |
| P2：缩小到图片小于视口时 V 条短暂出现 | `AC-P2-NO-TRANSIENT-VBAR`、`AC-SB-NO-STALE-RANGE` | `testZoomOutHasNoTransientVerticalScrollBar()` |
| P3：图片右侧鼠标连续放大三格时 H 条短暂出现 | `AC-P3-NO-TRANSIENT-HBAR`、`AC-P3-CROSS-AXIS-STABILITY` | `testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar()` |
| P4：Toggle Fit and 100% 后出现可避免的空白 | `AC-P4-NO-AVOIDABLE-BLANK`、`AC-P4-OPTIMAL-CLAMP`、`AC-P4-TOGGLE-DIRECTIONAL-ANCHOR` | `testToggleFitTo100HasNoAvoidableBlankSpace()` |

四项主验收是合取关系，而不是“通过其中一个即可接受”：

```text
AC-4Q
  = AC-P1-ANCHOR-CONTINUITY
  ∧ AC-P1-NO-LATE-JUMP
  ∧ AC-P2-NO-TRANSIENT-VBAR
  ∧ AC-SB-NO-STALE-RANGE
  ∧ AC-P3-NO-TRANSIENT-HBAR
  ∧ AC-P3-CROSS-AXIS-STABILITY
  ∧ AC-P4-NO-AVOIDABLE-BLANK
  ∧ AC-P4-OPTIMAL-CLAMP
  ∧ AC-P4-TOGGLE-DIRECTIONAL-ANCHOR
```

## 2. 问题分解、证据缺口与联网多跳检索

### 2.1 起始证据与边界

`reports/root_cause.md` 以代码基线 `9c1d53fe904c39f43bdc258011f78c5086ca3f60`、Qt 6.11.1 Cocoa 和现场 JPEG 为上下文，给出了四项现象的共同候选根因：虚拟 scene margin、`ScrollBarAsNeeded` 的双轴交叉布局、350 ms anchor settle、约 500 ms constraint，以及图片外锚点的“精确保持”与“不得产生空白”之间的冲突。

该报告同时保留了证据边界：现场未提供每一帧的 window/viewport、DPR、range、value 和时间戳；因此不能只凭最终截图把所有暂态归因于同一个 writer。本设计把这些缺口转成可观测量，并以当前 `test_case_specification.md` 的四项原子合同作为验收入口。

### 2.2 多跳路径

```text
用户现象
  → 分解为 image geometry、usable viewport、scene range、bar event、anchor point、timer writer
  → 查 Qt 公开合同：sceneRect、mapping、AsNeeded、QAction、animation
  → 查 Qt 6.11.1 源码：H/V range 的交叉占位与 viewport 重算
  → 回到 Fovelle：margin 生成、动画每帧重算、settle/post-layout/constraint 写入顺序
  → 用独立几何 oracle 和真实 QtTest 输入复现
  → 以 rendered frame、range/visibility 轨迹和 terminal quiet 交叉验证
```

证据缺口与约束如下：

| 证据缺口 | 第一跳事实 | 第二跳交叉验证 | 对实现/测试的约束 |
| --- | --- | --- | --- |
| 滚动条由什么几何产生 | `QGraphicsView::sceneRect` 定义可导航 scene | Qt 6.11.1 `recalculateContentSize()` 以变换后的 scene rect 判断两轴 range | `sceneRect` 只能由真实 displayed image 几何产生；不能把锚点辅助空白当内容 |
| H/V 是否相互影响 | `QAbstractScrollArea` 的 AsNeeded 条会占用 viewport | Qt 源码明确执行“水平条影响垂直条、垂直条影响水平条”的再判断 | 每个样本重新读取 usable viewport；不能复用动作开始时的宽高 |
| 锚点坐标是否重复变换 | `mapToScene`/`mapFromScene` 是 scene↔viewport 映射 | Qt 源码的 `mapRectFromScene()` 会再次应用 matrix | 输入必须来自 `scene()->itemsBoundingRect()`；禁止显示矩形二次 `mapFromScene()` |
| action 能否提供鼠标点 | `QAction::triggered(bool)` 只传 checked | 本地 Toggle 需读取缓存 mouse/cursor，再调用统一 zoom 入口 | P4 必须使用真实 shortcut，并把外部点作为“方向偏好”而非绝对可达命令 |
| 终态是否代表全过程 | animation 提供起止值间的 current value | `QPropertyAnimation` 会把中间值写入目标，Fovelle 还有延迟 timer | 采样 paint/resize/show/hide/range/value；只查最终 zoom 不足 |

### 2.3 显式前提

以下是推理条件，均在测试中显式设置或读取：

1. 两轴 scrollbar policy 为 `Qt::ScrollBarAsNeeded`；`QVGraphicsView` 的自有 anchor 事务负责在 Qt layout 后恢复位置。
2. `I = mapFromScene(scene()->itemsBoundingRect()).boundingRect()` 表示真实图片在 viewport 的 displayed geometry；`scene()->itemsBoundingRect()` 是未重复应用 view transform 的 scene 几何。
3. `U` 是运行时 usable viewport：由 `viewport()->rect()` 取得，并扣除 `MainWindow::getViewportPosition().obscuredHeight` 的安全区；不硬编码窗口尺寸。
4. “不应出现滚动条”指图片没有真实溢出且没有产品允许的可导航空白；最终把条隐藏不能抵消已绘制的错误暂态。
5. 图片外点没有真实图片内容可以精确保持。投影到边界只产生方向偏好，最终位置必须落在真实图片覆盖 viewport 的可行域。
6. 2 DIP 是本机整数 scrollbar、fractional transform 和 QRect 舍入的验收容差；不是对所有平台合成器的普遍保证。
7. 生成 fixture 用于可移植自动化；现场 JPEG 的尺寸/比例和用户步骤是人工复现补充，不将 `/Volumes/CRYSTAL` 作为默认测试依赖。

## 3. 原子验收标准

每个原子标准都有唯一 ID、结构化用例中的预期结果段落、测试代码 marker 和至少一个独立 assertion/oracle。相关标准共享一个 GUI fixture 时，仍按 ID 分开追溯。

| ID | 可核验判定 |
| --- | --- |
| `AC-P1-ANCHOR-CONTINUITY` | 对真实一格 wheel 的每个可观察缩放/布局帧，固定 scene 内容点回映到输入 viewport 点的误差不超过 2 DIP；不得因 range 或 alignment 接管发生离散跳位。 |
| `AC-P1-NO-LATE-JUMP` | animation 和具名延迟 writer 结束后，再处理延迟 quiet 窗口，固定 anchor 的位置变化不超过 1 DIP。 |
| `AC-P2-NO-TRANSIENT-VBAR` | 在缩小轨迹中，只要当前图片高度 `<= U.height + 1`，每个 rendered/scrollbar-visibility 帧的 V range 都为零；不能出现 `0→非零→0` 的用户可见暂态。 |
| `AC-SB-NO-STALE-RANGE` | P2 终态图片适配后，H/V range 都为零，且额外 event-loop/延迟 writer 不会复活旧 range。 |
| `AC-P3-NO-TRANSIENT-HBAR` | P3 三格输入中，只要当前图片宽度 `<= U.width + 1`，每个可观察帧的 H range 都为零，且不得有由该 range 导致的 H 条显示帧。 |
| `AC-P3-CROSS-AXIS-STABILITY` | V 条改变 viewport 后，H 判定使用更新后的 U；不能因为沿用旧宽度而制造一次伪 H range。 |
| `AC-P4-NO-AVOIDABLE-BLANK` | 目标图片溢出时 `U` 必须被 `I` 覆盖；图片适配时不以虚拟 scene extent 产生条。P4 终态四边均覆盖 usable viewport。 |
| `AC-P4-OPTIMAL-CLAMP` | 先按投影鼠标点计算首选图片 origin，再将其钳制到真实内容覆盖可行区；实际 origin 与最近可行解误差不超过 2 DIP。 |
| `AC-P4-TOGGLE-DIRECTIONAL-ANCHOR` | 真实 Toggle shortcut 的 fit→100% 读取鼠标外侧点作为方向偏好，仍服从真实图片边界和最近可行 origin；不得为了精确外点坐标扩大 scene。 |

## 4. 生产实现设计

### 4.1 真实图片几何是唯一 scene extent

`getScrollContentRect()` 现在直接返回 `getDisplayedContentRect()`。`getDisplayedContentRect()` 来自 loaded item 的真实 `boundingRect()`/当前 displayed zoom；它不再叠加锚点专用的临时 scene margin。因此 `ScrollBarAsNeeded` 的 range 来源恢复为真实 image content，而不是“为了让不可行 anchor 可达”临时制造的 physical blank space。

锚点分类只走一条坐标链：

```text
requested viewport DIP
  → mapFromScene(scene()->itemsBoundingRect()).boundingRect()
  → projectZoomAnchor(requested, image edge)
  → mapToScene(projected viewport point)
  → native scrollbar feasible-range clamp
```

已显示矩形不能再次作为 scene 输入传给 `mapFromScene()`；这正是根因报告中记录的旧版 double-transform 分支。

### 4.2 锚点事务与布局回合

图片内锚点在动画每帧仍由 `pendingZoomAnchorScene`/`pendingZoomAnchorViewport` 表示，`restorePendingZoomAnchor()` 用 scrollbar 的真实合法 range 恢复它。图片外点只被投影到 item 边界，不扩展 scene。

为覆盖 AsNeeded 的二次布局：

- `resizeEvent()` 在 base resize 后恢复 pending anchor，并 `zoomAnchorPostLayoutTimer->start(0)`；
- post-layout callback 若仍有 pending anchor，先再恢复一次，确保另一根 scrollbar 改变导致的 viewport 尺寸已经纳入；
- settle 后保存 settled anchor，只在对应图片轴真实溢出且存在 native range 时恢复；
- settle/post-layout 的正常间隔恢复为 50 ms。

这使 anchor 恢复服从“真实 scene + 当前 viewport”两项事实，避免 margin 清理或 scrollbar 出现/消失把位置交给另一套未同步的权威。

### 4.3 titlebar 安全区不制造 phantom range

`getSceneRectForViewport()` 仍将 full-size titlebar 的 obscured height 换算为 scene padding，但先按当前 transform 和轴向 `usableAxisSize` 限制 `paddingPixels`。当图片已足够小，安全区补偿不能把 scene 推大到重新需要 scrollbar；当图片确实溢出，保留必要补偿以匹配 fit 的 usable viewport。

### 4.4 Toggle 的 displayed-state 合同

Toggle 位于 `QVGraphicsView`，因为 `QAction::triggered` 没有鼠标坐标，也没有 displayed frame 状态。`isImageAtFit()` 同时检查 displayed zoom、独立计算的 fit level 和 H/V range；`calculatedZoomMode` 只表示目标意图。

```text
currentlyAtFit = displayed zoom == calculated fit level ∧ H range == 0 ∧ V range == 0
if currentlyAtFit:
    target = 100%; anchor = cursor if valid else usable center
else:
    target = calculated fit; anchor = cursor only when enlargement is requested,
                                     otherwise usable center
```

P4 的外侧 cursor 由 `projectZoomAnchor()` 转成方向偏好，最后由真实 range 和覆盖约束决定位置。

## 5. 统一几何 oracle 与状态模型

对每个动态样本记录：

```text
I = mapFromScene(scene()->itemsBoundingRect()).boundingRect()
U = current usable viewport rect
R_h = [H.minimum, H.maximum], R_v = [V.minimum, V.maximum]
H/V value, image rect, phase, anchor error
```

### 5.1 锚点连续性

设图片显示宽度为 `w`，投影后的鼠标横坐标为 `q`，其在图片中的归一化位置为 `u`。首选 origin 为：

```text
x_preferred = q - u*w
```

P1 的 target 选在中心且图片在目标帧有足够覆盖区，因此要求回映 scene point 直接落回 target；不把图片左上角当作稳定 oracle。

### 5.2 无空白的可行域

当 `w > U.width` 时，图片左 origin 的合法区间为：

```text
[U.right - w + 1, U.left]
```

高度同理为 `[U.bottom - h + 1, U.top]`。P4 的独立预言机计算：

```text
x_expected = clamp(x_preferred, U.right - w + 1, U.left)
y_expected = clamp(y_preferred, U.bottom - h + 1, U.top)
```

这一步同时表达“覆盖 viewport”和“尽量接近鼠标方向”的优先级，且不允许扩大 `sceneRect`。若图片小于轴向 viewport，则该轴应由 alignment 负责居中并且 range 为零。

### 5.3 初态、暂态、终态

- 初态：图片已加载、fit 已稳定、相关 timer inactive；P4 初态必须验证图片右侧确有 fit blank，才能证明外部锚点路径被执行。
- 暂态：真实 `QWheelEvent`/`QTest::keySequence` 进入后，观察 `QEvent::Paint`、`QEvent::Resize`、scrollbar Show/Hide、range/value、animation callback 和具名 timer；任何已提交或已绘制的错误暂态都失败。
- 终态：`waitForZoomTerminal()` 确认 animation、anchor settle、post-layout、constraint、expensive scaling、scrollbar geometry writer inactive；再处理 quiet event loop 和延迟窗口。P1 额外比较 quiet 前后 anchor，P2/P3 比较最终 ranges，P4 比较覆盖和最近可行 origin。

## 6. 测试固化与执行入口

生产实现和测试代码的职责分离如下：

| 层 | 文件 | 作用 |
| --- | --- | --- |
| 生产代码 | `src/qvgraphicsview.cpp/.h` | 真实 scene extent、单次坐标映射、post-layout anchor 恢复、titlebar cap、Toggle state |
| 动态测试 | `tests/tst_qviewtests.cpp` | 四个主场景、真实 wheel/shortcut、轨迹 probe、几何/range oracle |
| 静态测试 | `tests/zoom_issue_acceptance_static.py` | 原子 ID、源码 marker、六字段 case、CTest traceability |
| CTest | `tests/CMakeLists.txt` | `FovelleZoomIssueStatic` 与 `FovelleFourIssueZoomAcceptance` 可重复注册 |

验证命令：

```bash
cmake --build build --parallel 2
ctest --test-dir build -R '^FovelleZoomIssueStatic$' --output-on-failure --timeout 30
ctest --test-dir build -R '^FovelleFourIssueZoomAcceptance$' --output-on-failure --timeout 120
```

相邻回归仍通过 `FovelleFiveIssueZoomAcceptance`、`FovelleZoomScrollbarTrajectory` 和 `FovelleZoomScrollbarTrajectoryHiDpi` 执行；它们不能替代 `AC-4Q`，但用于防止修复四项主问题破坏既有拖拽、键盘、Toggle 和 DPR 行为。

## 7. 风险与限制

本设计覆盖 Qt 6.11.1 Cocoa/arm64、测试中声明的窗口几何、生成 raster、普通 DPR 与 HiDPI 轨迹。它不声称覆盖 Windows/Linux style、WindowServer/GPU 在应用已提交正确 geometry 后的独立残影，亦不把系统级 Accessibility/HID 作为默认门禁。现场 JPEG 的人工复现结果若与 fixture 不同，必须保留逐帧 tuple 再做归因，不能仅凭肉眼差异修改 oracle。

联网事实依据：

- [Qt `QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html)
- [Qt `QGraphicsView`](https://doc.qt.io/qt-6/qgraphicsview.html)
- [Qt `QAction`](https://doc.qt.io/qt-6/qaction.html)
- [Qt `QVariantAnimation`](https://doc.qt.io/QT-6/qvariantanimation.html)
- [Qt `QPropertyAnimation`](https://doc.qt.io/qt-6/qpropertyanimation.html)
- [Qt `QTimer`](https://doc.qt.io/qt-6/qtimer.html)
- [Qt 6.11.1 `qgraphicsview.cpp`](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)
