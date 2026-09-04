# 测试用例说明：滚动条拓扑与 Toggle Fit/100% 锚点

日期：2026-09-04
被测代码：`src/qvgraphicsview.h`、`src/qvgraphicsview.cpp`
参考：[reports/root_cause.md](root_cause.md)

## 1. 覆盖矩阵

| 原子验收标准 | 结构化测试用例 | 静态 | 动态 |
| --- | --- | :---: | :---: |
| `AC-HBAR-01-ROUND-TRIP` | `TC-HBAR-FOUR-IN-ONE-OUT` | ✓ | ✓ |
| `AC-HBAR-02-ANCHOR-CONTINUITY` | `TC-HBAR-FOUR-IN-ONE-OUT` | ✓ | ✓ |
| `AC-TOGGLE-DIRECTIONAL-ANCHOR` | `TC-TOGGLE-DIRECTIONAL-ANCHOR` | ✓ | ✓ |
| `AC-TOGGLE-FROZEN-CENTER-ANCHOR` | `TC-TOGGLE-FROZEN-CENTER-ANCHOR` | ✓ | ✓ |
| `AC-TOGGLE-ANCHOR-LIFETIME` | `TC-TOGGLE-ANCHOR-LIFETIME` | ✓ | ✓ |
| `AC-TOGGLE-MONOTONIC-TERMINAL` | `TC-TOGGLE-STABILITY-TRAJECTORY` | ✓ | ✓ |
| `AC-TOGGLE-QUIESCENT-FINAL` | `TC-TOGGLE-STABILITY-TRAJECTORY` | ✓ | ✓ |
| `AC-DURATION-01-LOG-DISTANCE` | `TC-DURATION-LOG-DISTANCE` | ✓ | ✓ |
| `AC-DURATION-02-FIXED-STEP` | `TC-DURATION-FIXED-STEP` | ✓ | ✓ |
| `AC-STATIC-01-TRACEABILITY` | `TC-STATIC-TRACEABILITY` | ✓ | — |

动态测试分为初态、动画/布局暂态、终态和 quiet window；静态测试检查生产实现合同、用例字段、代码标记、CTest 注册和三份报告的追溯关系。测试窗口会清除并在退出时恢复持久化 `geometry`，因此名义尺寸就是实际阈值前提。

## 2. 结构化测试用例

### TC-HBAR-FOUR-IN-ONE-OUT

覆盖：`AC-HBAR-01-ROUND-TRIP`、`AC-HBAR-02-ANCHOR-CONTINUITY`

**测试目的**：重现用户给出的“打开图片、鼠标置于图片中心、滚轮前进 4 格、再后退 1 格”路径，验证 H range 出现/消失时图片固定 scene 点没有不可预测跳变。

**前置条件**：Cocoa QtTest 可创建可见窗口；H/V scrollbar 为 `ScrollBarAsNeeded`；默认 wheel 倍率为 `1.25`；图片已从 Fit 稳定；`QSettings/geometry` 已隔离。

**输入数据**：优先使用 `/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`（预期 `3840×4407`）；现场文件不可读时使用同尺寸合成图；窗口请求尺寸 `1000×550`；wheel 序列 `+120,+120,+120,+120,-120`。

**操作步骤**：

1. 打开图片，等待加载、Fit 动画和所有已知延迟 writer 完成。
2. 连续发送 3 个真实离散 wheel detent，每次等待结算；确认 H 无 range、V 有 range。
3. 把鼠标移到 usable viewport 中心，记录固定 viewport 点及其 scene 点，安装 `ZoomIssueProbe`。
4. 发送第 4 个 `+120`，等待 animation、layout、settle 和约束完成，记录 `four-forward-terminal`。
5. 发送 `-120`，等待同样的终态，记录 `one-reverse-terminal`。
6. 遍历 range/value、resize、paint、animation 和 timer 样本，检查 H 的 `0→非零→0` 轨迹与固定 anchor。

**预期结果**：第 4 格终态 H range 非零；反向 1 格终态 H range 归零；样本确实跨过两个 topology 边界；paint 和终态的固定 anchor 误差不超过 2 DIP，只有 resize 预处理样本允许原生横条半厚度加 1 DIP 的边界；不出现依赖延迟回弹才能成立的可见错误位置。

**后置条件**：动画、settle、post-layout、约束和垂直几何 timer 均停止；窗口关闭；合成图由 `QTemporaryDir` 回收；现场 JPEG 不写入。

### TC-TOGGLE-DIRECTIONAL-ANCHOR

覆盖：`AC-TOGGLE-DIRECTIONAL-ANCHOR`

**测试目的**：验证 Toggle 的方向策略不会把 Fit→100% 的鼠标锚点误用到 100%→Fit，也不会因鼠标被移开而丢失中心锚点。

**前置条件**：窗口几何已隔离；`1600×900` raster 已加载；Fit 状态稳定；Toggle action 已注册。

**输入数据**：Fit 状态下偏离中心的 cursor 点；一次 Fit→100% Toggle；把鼠标移到左上角后再执行一次 100%→Fit Toggle。

**操作步骤**：

1. 记录 Fit 状态的非中心 cursor scene 点。
2. 触发 Toggle，等待结算，比较该 scene 点与原 cursor 点的映射。
3. 将鼠标移离中心，显式调用 `centerImage()`，记录 usable viewport center 下的 scene 点。
4. 再触发 Toggle，等待 Fit 与所有 timer 完成，比较记录的 center scene 点与最终 usable center。

**预期结果**：放大端 cursor anchor 误差不超过 2 DIP；缩小端使用 usable center 而不是移动后的 cursor；最终为 Fit 且 H/V 均无 range。

**后置条件**：窗口、cursor、action 状态和临时图片释放；设置恢复。

### TC-TOGGLE-FROZEN-CENTER-ANCHOR

覆盖：`AC-TOGGLE-FROZEN-CENTER-ANCHOR`

**测试目的**：验证 100%→Fit 的中心哨兵在请求起点只解析一次，不随 H 条消失导致的 usable viewport center 变化而漂移。

**前置条件**：窗口几何已隔离；`1600×2200` portrait raster 处于 100%，H/V 均有 range；动画和 native Cocoa 事件循环可运行。

**输入数据**：usable viewport center 的固定 viewport 点与其 scene 点；一次 100%→Fit Toggle；观察 H 已消失但 V 仍有 range 的中间帧。

**操作步骤**：

1. 在 100% settled frame 调用 `centerImage()`，记录 `anchorViewport` 和 `anchorScene`。
2. 触发 Toggle，确认 animation 正在运行且为语义自适应时长。
3. 让事件循环推进，直到 H 无 range 且 V 仍有 range；此时读取 `mapFromScene(anchorScene).y()`。
4. 等待完整终态，并检查 Fit 与 H/V range。

**预期结果**：中间帧的 y 锚点与原始 `anchorViewport.y()` 相差不超过 2 DIP；H 条消失时不会把它改成新的 viewport center；终态为 Fit 且 H/V 无 range。

**后置条件**：所有动画和 timer 停止；窗口关闭；设置和临时资源恢复/回收。

### TC-TOGGLE-ANCHOR-LIFETIME

覆盖：`AC-TOGGLE-ANCHOR-LIFETIME`

**测试目的**：验证 anchor 的生命周期覆盖实际动画和后续布局，而不是沿用固定的 `200+150=350 ms` 截止点。

**前置条件**：同 `TC-TOGGLE-FROZEN-CENTER-ANCHOR`；`QPropertyAnimation` 和 `zoomAnchorSettleTimer` 可通过对象名观察。

**输入数据**：100%→Fit 语义 Toggle；实际动画时长 `D`；产品 settle 延迟 `150 ms`。

**操作步骤**：

1. 触发 Toggle 后读取 animation duration 和 settle timer interval。
2. 在 animation 尚未停止时确认 settle timer 仍 active。
3. 让 H/V 布局变化发生，确认中间 anchor 断言仍成立。
4. 等待 `waitForZoomTerminal()`，确认 settle/post-layout/约束 writer 全部停止。

**预期结果**：`200 ms < D ≤ 400 ms`；settle interval 等于 `D+150 ms`；动画运行期间 timer 到期不会清空 anchor；只有 animation 完成并完成布局结算后才进入终态。

**后置条件**：无活动 animation/timer；窗口和 settings 恢复。

### TC-TOGGLE-STABILITY-TRAJECTORY

覆盖：`AC-TOGGLE-MONOTONIC-TERMINAL`、`AC-TOGGLE-QUIESCENT-FINAL`

**测试目的**：验证真实 `Z` 快捷操作的 Fit↔100% 往返不会在 animation-finished 后再次重缩放或回弹。

**前置条件**：窗口几何已隔离；先使用合成 `2560×2938` raster，若现场 JPEG 可读则追加 `3840×4407` provided row；Z 绑定 Toggle；Fit 已稳定。

**输入数据**：真实 `QTest::keySequence` 的 Fit→100%→Fit；动画/paint/resize/range/timer 样本；终态后额外 650 ms quiet window。

**操作步骤**：

1. 发送 Z 进入 100%，确认两轴有 range。
2. 再发送 Z 进入 Fit，记录每一个可见 image rect 的宽高以及 animation-value、animation-finished、terminal 状态。
3. 检查尺寸序列无反向增大，且最后动画值、finished、terminal 相互一致。
4. 等待 650 ms，再次读取 image rect、viewport、range 和 timer 状态。

**预期结果**：缩小尺寸单调不增；无 terminal rescale；quiet window 内尺寸和 viewport 状态不变；最终精确 Fit 且无 scrollbar range。

**后置条件**：animation、settle、post-layout、expensive-scale 和垂直几何 timer 停止；现场文件不修改。

### TC-DURATION-LOG-DISTANCE

覆盖：`AC-DURATION-01-LOG-DISTANCE`

**测试目的**：验证语义缩放按照乘性倍率距离而非百分点差决定动画时长。

**前置条件**：`QVGraphicsView::zoomTransitionDurationMs()` 可脱离 GUI 调用。

**输入数据**：`1→1.25`、`0.5→1`、`0.25→1`、`0.1→1`、`0.25→0.5`、`1→2`，分别测试 fixed/adaptive。

**操作步骤**：调用纯函数并比较结果、单调性、等倍率距离、上下限及无效输入边界。

**预期结果**：自适应结果为 `232/300/400/400 ms`（对应前四组）；`0.25→0.5` 与 `1→2` 相等；所有 adaptive 结果在 `200–400 ms`；fixed 模式恒为 `200 ms`。

**后置条件**：无窗口、文件、事件或持久设置变化。

### TC-DURATION-FIXED-STEP

覆盖：`AC-DURATION-02-FIXED-STEP`

**测试目的**：验证 wheel、键盘 Zoom In/Out 和菜单单步入口的时长契约未被语义动画改动。

**前置条件**：可见窗口加载 `1200×900` raster；animation 对象存在；标题栏 View 菜单、右键 View 菜单和 Toggle action 均已物化。

**输入数据**：真实一个 wheel step、标题栏 Zoom In、右键菜单 Zoom In、Toggle Fit and 100%。

**操作步骤**：逐一 dispatch 入口，立即读取 animation duration，观察一个中间值，等待 terminal，再检查 displayed zoom 等于 logical target。

**预期结果**：wheel、标题栏和右键菜单单步均精确 `200 ms` 且存在中间帧；Toggle 语义跳转时长大于 `200 ms` 且不超过 `400 ms`，最终精确到达目标。

**后置条件**：animation 停止；窗口、临时图和 settings 释放。

### TC-STATIC-TRACEABILITY

覆盖：`AC-STATIC-01-TRACEABILITY`

**测试目的**：静态确认每条原子验收标准都能从设计文档追溯到结构化用例、可执行测试、CTest 注册和完成报告。

**前置条件**：Python 3 可用；源码、QtTest、CTest 配置及三份 Markdown 报告存在；不要求现场卷或显示器。

**输入数据**：`src/qvgraphicsview.{h,cpp}`、`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`、两个 static Python gate 和三份报告。

**操作步骤**：执行 `python3 tests/zoom_scrollbar_duration_static.py --repo . --output build/test-results/zoom-scrollbar-duration-static.json`，并执行 `python3 tests/toggle_fit_stability_static.py --repo . --output build/test-results/toggle-fit-stability-static.json`。

**预期结果**：两个进程退出码均为 0；JSON `passed=true`；源码包含 topology/anchor/duration 合同；每个结构化用例均有“测试目的、前置条件、输入数据、操作步骤、预期结果、后置条件”六项。

**后置条件**：只生成机器可读 JSON；不修改生产源码、用户设置或测试输入。

## 3. 测试代码与 CTest 映射

| 用例 | 固化测试代码 | CTest |
| --- | --- | --- |
| `TC-HBAR-FOUR-IN-ONE-OUT` | `GraphicsViewTests::testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump` | `FovelleScrollbarZoomDurationAcceptance` |
| `TC-TOGGLE-DIRECTIONAL-ANCHOR` | `testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor` | `FovelleToggleFitAnchorAcceptance`、`FovelleFiveIssueZoomAcceptance` |
| `TC-TOGGLE-FROZEN-CENTER-ANCHOR`、`TC-TOGGLE-ANCHOR-LIFETIME` | `testToggleFitAnd100FreezesViewportCenterDuringScrollbarTransition` | `FovelleToggleFitAnchorAcceptance` |
| `TC-TOGGLE-STABILITY-TRAJECTORY` | `testToggleFitReturnHasMonotonicStableTerminalSize` | `FovelleToggleFitStabilityAcceptance` |
| `TC-DURATION-LOG-DISTANCE` | `testZoomTransitionDurationUsesLogDistance` | `FovelleScrollbarZoomDurationAcceptance` |
| `TC-DURATION-FIXED-STEP` | `testZoomTransitionCoversWheelKeyboardAndMenus` | `FovelleTests` |
| `TC-STATIC-TRACEABILITY` | `zoom_scrollbar_duration_static.py`、`toggle_fit_stability_static.py` | `FovelleZoomScrollbarDurationStatic`、`FovelleToggleFitStabilityStatic` |
