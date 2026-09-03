# Fovelle 图片缩放四项问题：结构化测试用例说明

> 文档日期：2026-09-03
> 文档性质：原子验收合同与可执行测试设计
> 根因依据：[reports/root_cause.md](root_cause.md)
> 生产代码：[`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp)
> 动态测试：[`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp)

## 1. 范围与原子验收标准

测试从 `root_cause.md` 的共同根因假设出发，以本仓库当前实现和逐帧证据缺口为约束。用户给出的现场文件是：

`/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`

自动化使用临时生成的等比例 raster，避免外置卷成为 CI 隐含前提；输入几何和交互顺序保持与现场问题等价。

| 问题 | 原子验收标准 | 结构化用例 | 测试函数 |
| --- | --- | --- | --- |
| P1：wheel 后位置跳变 | `AC-P1-ANCHOR-CONTINUITY`、`AC-P1-NO-LATE-JUMP` | `TC-P1-WHEEL-TRAJECTORY` | `testWheelZoomHasNoPositionJumpTrajectory` |
| P2：缩小后 V 条闪现 | `AC-P2-NO-TRANSIENT-VBAR`、`AC-SB-NO-STALE-RANGE` | `TC-P2-ZOOMOUT-VBAR` | `testZoomOutHasNoTransientVerticalScrollBar` |
| P3：右侧外点 +3 后 H 条闪现 | `AC-P3-NO-TRANSIENT-HBAR`、`AC-P3-CROSS-AXIS-STABILITY` | `TC-P3-RIGHT-OUTSIDE-WHEEL` | `testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar` |
| P4：Toggle→100% 后多余空白 | `AC-P4-NO-AVOIDABLE-BLANK`、`AC-P4-OPTIMAL-CLAMP`、`AC-P4-TOGGLE-DIRECTIONAL-ANCHOR` | `TC-P4-TOGGLE-NO-BLANK` | `testToggleFitTo100HasNoAvoidableBlankSpace` |

四项验收必须全部成立：

```text
AC-4Q = AC-P1-ANCHOR-CONTINUITY
      ∧ AC-P1-NO-LATE-JUMP
      ∧ AC-P2-NO-TRANSIENT-VBAR
      ∧ AC-SB-NO-STALE-RANGE
      ∧ AC-P3-NO-TRANSIENT-HBAR
      ∧ AC-P3-CROSS-AXIS-STABILITY
      ∧ AC-P4-NO-AVOIDABLE-BLANK
      ∧ AC-P4-OPTIMAL-CLAMP
      ∧ AC-P4-TOGGLE-DIRECTIONAL-ANCHOR
```

### 1.1 原子判定定义

| ID | 原子判定 |
| --- | --- |
| `AC-P1-ANCHOR-CONTINUITY` | 真实一格 wheel 的每个可观察缩放/布局帧中，固定 scene 内容点回映到输入 viewport 点的误差不超过 2 DIP。 |
| `AC-P1-NO-LATE-JUMP` | animation、settle、post-layout、constraint 等 writer quiet 后，再等待延迟窗口，固定内容点位置变化不超过 1 DIP。 |
| `AC-P2-NO-TRANSIENT-VBAR` | P2 缩小过程中，只要图片高度 `<= 当前 usable viewport 高度 + 1`，V range 在每个 rendered/Show/Hide 帧都为零；禁止 `0→非零→0` 的可见暂态。 |
| `AC-SB-NO-STALE-RANGE` | P2 终态图片已适配后，H/V range 均为零，且 quiet event loop 后不复活。 |
| `AC-P3-NO-TRANSIENT-HBAR` | P3 三格过程中，只要图片宽度 `<= 当前 usable viewport 宽度 + 1`，H range 在每个可观察帧都为零，且没有由它产生的 H 条显示帧。 |
| `AC-P3-CROSS-AXIS-STABILITY` | V 条改变 viewport 后，H 判定重新读取缩小后的 usable width；不沿用旧宽度制造伪 H range。 |
| `AC-P4-NO-AVOIDABLE-BLANK` | 目标图片溢出时图片四边覆盖 usable viewport；不通过 synthetic scene margin 暴露可避免的背景空白。 |
| `AC-P4-OPTIMAL-CLAMP` | 图片外鼠标点先投影为首选锚点，再将图片 origin 钳制到真实内容覆盖可行区；实际 origin 与最近可行解误差不超过 2 DIP。 |
| `AC-P4-TOGGLE-DIRECTIONAL-ANCHOR` | 真实 Toggle shortcut 的 fit→100% 使用图片右侧鼠标点作为方向偏好，同时服从真实内容边界，不扩大 scene。 |

## 2. 统一测试数据、状态和 oracle

### 2.1 统一观测量

测试代码中的 `ZoomIssueProbe` 在 view、viewport、两根 scrollbar、bar 容器上安装事件过滤器，并连接 range/value、animation、具名 timer。每条记录保存：

```text
I = mapFromScene(scene()->itemsBoundingRect()).boundingRect()
U = viewport()->rect()，再扣除运行时 obscuredHeight
H/V minimum、maximum、value
phase、anchor error
```

`I` 是真实图片内容，不能把已经变换的显示矩形再次当 scene 输入；`U` 每次从当前 viewport 读取，以覆盖 AsNeeded 的交叉布局。

### 2.2 可行域 oracle

图片显示宽度为 `w`、高度为 `h`，投影鼠标位置为 `(q_x,q_y)`，鼠标在图片中的归一化位置为 `(u,v)`，首选 origin 为：

```text
x_preferred = q_x - u*w
y_preferred = q_y - v*h
```

当图片溢出时，不产生 viewport 外背景的合法 origin 为：

```text
x ∈ [U.right - w + 1, U.left]
y ∈ [U.bottom - h + 1, U.top]
```

因此 P4 的独立预言机为：

```text
x_expected = clamp(x_preferred, U.right - w + 1, U.left)
y_expected = clamp(y_preferred, U.bottom - h + 1, U.top)
```

这明确了“无多余空白”优先于图片外点的不可达精确屏幕坐标。图片适配某轴时，该轴 range 必须为零并由正常 alignment 居中。

### 2.3 初态、暂态、终态定义

- **初态**：图片已加载，fit 已稳定，animation 与具名 writer inactive；P4 还必须证明鼠标确实在 fit 图片右侧空白中。
- **暂态**：真实 wheel 或真实 shortcut 已发送后，监听 Paint、Resize、Show、Hide、range/value、animation callback 和具名 timer；错误只出现一帧也算失败。
- **终态**：`waitForZoomTerminal()` 确认 transition、anchor settle、post-layout、constraint、expensive-scale、scrollbar-geometry writer inactive；再处理 quiet event loop。P1 额外比较 quiet 前后位置，P2/P3 检查最终 ranges，P4 检查覆盖和最近可行 origin。

## 3. 结构化测试用例

### TC-P1-WHEEL-TRAJECTORY

#### 测试目的

动态测试 P1。覆盖 `AC-P1-ANCHOR-CONTINUITY` 和 `AC-P1-NO-LATE-JUMP`，验证用户滚轮缩放时图片内容连续，且动画完成后的迟到 writer 不再使图片跳位。

#### 前置条件

macOS Cocoa 可见 `MainWindow`；窗口由测试固定为 640×480；加载 1200×1200 临时 raster；设置 fit-to-window、cursor zoom、Disabled smooth scaling、`ScrollBarAsNeeded`，并等待 fit 和所有 writer 稳定。

#### 输入数据

usable viewport 的中心点 `target`、其对应的 scene 点 `anchorScene = mapToScene(target)`，以及一个真实 `QWheelEvent`，viewport position 为 `target`、angle delta 为 `(0,120)`。

#### 操作步骤

1. 移动真实鼠标到 `target`，记录初态 image/viewport/range 和 `anchorScene`。
2. 通过 `sendDiscreteZoomWheel()` 将真实 wheel event 发送到 viewport。
3. `ZoomIssueProbe` 记录 animation/range/value、Paint/Resize 和 scrollbar 可见性事件。
4. 等待 `waitForZoomTerminal()`，处理事件循环，记录 terminal anchor。
5. 再等待 650 ms 并处理事件，记录 quiet anchor 与 terminal sample。

#### 预期结果

每一个可观察 rendered frame 的 `mapFromScene(anchorScene)` 距 `target` 不超过 2 DIP；P1 的首选锚点没有被 alignment 或 range 重定位。quiet 前后的 anchor 差值不超过 1 DIP；无延迟位置跳变，且最终 zoom 为正值。

#### 后置条件

停止 animation、anchor、post-layout、constraint、expensive-scale、scrollbar-geometry timer；关闭窗口，释放临时 fixture，并恢复 scoped settings。

### TC-P2-ZOOMOUT-VBAR

#### 测试目的

动态测试 P2。覆盖 `AC-P2-NO-TRANSIENT-VBAR` 与 `AC-SB-NO-STALE-RANGE`，验证“放大后反向缩小”跨越 fit 阈值时 V 条既不闪现，也不在终态残留。

#### 前置条件

macOS Cocoa 可见窗口；加载 1600×900 临时 raster；fit 已稳定；cursor zoom、Disabled smooth scaling、AsNeeded scrollbars 开启；测试点位于 usable viewport 右下角，允许初态 fit blank。

#### 输入数据

同一 viewport point 的三次真实 wheel-in `+120`，随后三次真实 wheel-out `-120`；每次反向输入之间处理一个 event-loop turn 并短暂等待。

#### 操作步骤

1. 记录初态 fit image、当前 usable viewport、H/V range 和 scrollbar 事件。
2. 发送三次真实 `QWheelEvent(+120)`，等待 terminal，并确认轨迹曾出现 V range。
3. 发送三次真实 `QWheelEvent(-120)`；在每步后处理事件，使跨阈值期间的 Paint/Show/Hide/range 都能被 probe 看到。
4. 等待所有 writer inactive，处理 250 ms quiet 窗口，记录终态 image 和 ranges。

#### 预期结果

缩小轨迹中每个当前 `image.height <= usable.height + 1` 的 Paint/Resize/Show/Hide sample 都满足 V range 为零；不得出现 `0→非零→0` 的可见 V 条。终态 image height `<= usable.height + 1`，H/V range 均为零，quiet 后不复活。

#### 后置条件

停止所有 zoom writer；关闭窗口，释放临时 raster、probe 和 scoped settings。

### TC-P3-RIGHT-OUTSIDE-WHEEL

#### 测试目的

动态测试 P3。覆盖 `AC-P3-NO-TRANSIENT-HBAR` 与 `AC-P3-CROSS-AXIS-STABILITY`，复现鼠标在图片右侧、连续放大三格时的水平条闪现路径。

#### 前置条件

macOS Cocoa 可见窗口；加载 1280×1469 portrait 临时 raster；fit 已稳定并在 usable viewport 中留下右侧 blank；启用 cursor zoom、Disabled smooth scaling 和 AsNeeded scrollbars。

#### 输入数据

鼠标点为 fit image 右边 40 DIP（同时限制在 usable viewport 内），三次真实 wheel event，每次 angle delta `(0,120)`。

#### 操作步骤

1. 确认初态 `fitImage.width < usable.width` 且鼠标点严格位于图片右侧。
2. 发送真实 mouse move 和系统 cursor 定位，记录初态。
3. 连续发送三次真实 wheel；每次处理 event-loop turn 和 15 ms，让 animation、range、bar visibility 和 viewport resize 进入 probe。
4. 等待 terminal，再处理 250 ms quiet，读取最终 image、usable viewport 和 H range。

#### 预期结果

对每一个当前 `image.width <= usable.width + 1` 的 rendered/Show/Hide sample，H range 必须为零且不得出现 H 条显示帧。若 V 条出现并缩小 viewport，下一次 H 判定必须使用新的 usable width；不得产生交叉轴伪 H range。终态 image width `<= usable.width + 1`，H range 为零。

#### 后置条件

停止所有 writer；关闭窗口，释放 probe、临时 raster 和 settings。若现场 JPEG 可用，可用同样步骤进行人工复现，但不改变默认 CI 输入依赖。

### TC-P4-TOGGLE-NO-BLANK

#### 测试目的

动态测试 P4。覆盖 `AC-P4-NO-AVOIDABLE-BLANK`、`AC-P4-OPTIMAL-CLAMP` 和 `AC-P4-TOGGLE-DIRECTIONAL-ANCHOR`，验证外部鼠标点触发 Toggle Fit and 100% 时，图片覆盖 viewport 且只取最近可行位置。

#### 前置条件

macOS Cocoa 可见窗口；加载 1280×1469 portrait 临时 raster；窗口固定为 1000×550；fit 已稳定；fit image 右侧有可测 blank；将 `togglefitand100` 临时绑定到 `Z`，启用 cursor zoom、Disabled smooth scaling 和 AsNeeded scrollbars。

#### 输入数据

位于 fit image 右侧 40 DIP 的 `outside` viewport 点、其投影点 `projectedFitAnchor`，以及通过 `QTest::keySequence()` 发送的真实 `Z` shortcut。

#### 操作步骤

1. 计算初态 `fitImage`、usable viewport 和投影 anchor；断言 `outside.x() > fitImage.right()`。
2. 移动真实鼠标并设置系统 cursor，创建开启 no-transient/no-blank 检查的 `ZoomIssueProbe`，记录初态。
3. 通过 action 的真实 shortcut 调用 Toggle，等待 `waitForZoomTerminal()`，确认 logical zoom 等价于 1.0。
4. 处理终态事件，读取 mapped image 和当前 usable viewport；按 `x_expected/y_expected` 独立计算最近可行 origin。

#### 预期结果

100% image 在目标 fixture 中横向、纵向均溢出；所有可观察 overflowing frame 的 image 四边覆盖 usable viewport，右侧不能留下可避免空白。最终 image origin 与 `clamp(preferred origin, feasible interval)` 的差值不超过 2 DIP；不通过扩大 sceneRect 维持图片外点的旧屏幕坐标。该结果同时证明 Toggle 使用外部鼠标点作为方向偏好，并服从真实内容边界。

#### 后置条件

停止所有 zoom writer；关闭窗口；清理临时 shortcut、cursor、fixture、probe 和 scoped settings。

### TC-STATIC-TRACEABILITY

#### 测试目的

静态测试。验证九条原子验收标准分别存在于设计文档、当前用例说明、QtTest marker 和可执行测试函数中，并验证 CTest 注册没有把“已设计”误报成“已执行”。

#### 前置条件

Python 3、源码、三份 Markdown、`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt` 均可读取；不启动 GUI，不依赖现场 JPEG。

#### 输入数据

仓库路径和输出 JSON 路径 `build/test-results/zoom-issue-acceptance-static.json`。

#### 操作步骤

运行 `tests/zoom_issue_acceptance_static.py`，检查原子 ID、生产实现 marker、真实 wheel/shortcut、初态/暂态/终态字段、五个结构化 case 和两个四问题 CTest 入口。

#### 预期结果

静态脚本返回 0，JSON 中所有 check 的 `pass` 均为 true；每个结构化 case 均有“测试目的、前置条件、输入数据、操作步骤、预期结果、后置条件”六个字段，同时包含静态/动态与初态/暂态/终态覆盖。

#### 后置条件

保留 JSON 机器证据；不修改产品运行状态。若任何 traceability check 失败，发布门禁失败，即使 GUI 终态测试通过也不能替代。

## 4. 原子标准到代码的追溯矩阵

| 原子标准 | 结构化测试用例 | 固化测试代码/断言 |
| --- | --- | --- |
| `AC-P1-ANCHOR-CONTINUITY` | `TC-P1-WHEEL-TRAJECTORY` | `testWheelZoomHasNoPositionJumpTrajectory`：`ZoomIssueProbe` rendered-frame anchor error ≤2 DIP |
| `AC-P1-NO-LATE-JUMP` | `TC-P1-WHEEL-TRAJECTORY` | 同函数：650 ms quiet 前后 anchor 差值 ≤1 DIP |
| `AC-P2-NO-TRANSIENT-VBAR` | `TC-P2-ZOOMOUT-VBAR` | `testZoomOutHasNoTransientVerticalScrollBar`：可观察 fit 帧 V range=0 |
| `AC-SB-NO-STALE-RANGE` | `TC-P2-ZOOMOUT-VBAR` | 同函数：终态 image fit 且 H/V range=0 |
| `AC-P3-NO-TRANSIENT-HBAR` | `TC-P3-RIGHT-OUTSIDE-WHEEL` | `testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar`：fit 宽度帧 H range=0 |
| `AC-P3-CROSS-AXIS-STABILITY` | `TC-P3-RIGHT-OUTSIDE-WHEEL` | 同函数：每条记录重新读取 usable viewport，覆盖 V bar 交叉反馈 |
| `AC-P4-NO-AVOIDABLE-BLANK` | `TC-P4-TOGGLE-NO-BLANK` | `testToggleFitTo100HasNoAvoidableBlankSpace`：四边覆盖和 probe no-blank |
| `AC-P4-OPTIMAL-CLAMP` | `TC-P4-TOGGLE-NO-BLANK` | 同函数：独立 `preferred/feasible/expected` origin oracle ≤2 DIP |
| `AC-P4-TOGGLE-DIRECTIONAL-ANCHOR` | `TC-P4-TOGGLE-NO-BLANK` | 同函数：真实 `QTest::keySequence` + 外侧 cursor + Toggle 到 1.0 |

## 5. 交叉验证与通过标准

动态测试使用真实 `QWheelEvent` 和 `QTest::keySequence`；静态测试不依赖动态结果。四个主用例通过 `FovelleFourIssueZoomAcceptance` 注册，静态用例通过 `FovelleZoomIssueStatic` 注册。既有 `FovelleFiveIssueZoomAcceptance`、普通 DPR/HiDPI scrollbar trajectory 用于相邻回归，不替代 `AC-4Q`。

联网事实依据和多跳交叉验证：

- [Qt `QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html)：AsNeeded range、scrollbar 占用 viewport 的公开语义。
- [Qt `QGraphicsView`](https://doc.qt.io/qt-6/qgraphicsview.html)：sceneRect、alignment、transform anchor、scene↔viewport mapping。
- [Qt `QAction`](https://doc.qt.io/qt-6/qaction.html)：`triggered(bool)` 不提供鼠标位置。
- [Qt `QVariantAnimation`](https://doc.qt.io/QT-6/qvariantanimation.html)：动画 current value 是时间插值状态。
- [Qt `QPropertyAnimation`](https://doc.qt.io/qt-6/qpropertyanimation.html)：中间属性值会被写入目标对象。
- [Qt `QTimer`](https://doc.qt.io/qt-6/qtimer.html)：零延迟回调的相对顺序不能作为隐含前提。
- [Qt 6.11.1 `qgraphicsview.cpp`](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)：二次验证 H/V AsNeeded 交叉布局。

验收准则：构建成功；静态 JSON 全部 PASS；四个主动态函数全部 PASS；原子追溯无缺口；不能用“最终状态正确”抵消已观测的错误暂态。
