# 测试完成报告：滚动条拓扑与 Toggle Fit/100% 锚点

日期：2026-09-04
仓库：`/Users/inostarlin/code/Fovelle`
构建目录：`/Users/inostarlin/code/Fovelle/build`
参考：[reports/root_cause.md](root_cause.md)

## 1. 完成结论

代码修复、原子验收拆解、结构化测试用例、测试代码和 CTest 注册均已完成。专项动态测试已覆盖真实 JPEG 的 4 格放大/1 格缩小边界，以及 Toggle 在横条消失过程中的固定中心 anchor；静态 gate 用于阻止报告、生产合同和测试代码脱节。

“没有位置跳变”的结论限定在 Qt/Fovelle 的可见 widget/model/native 提交边界。当前测试没有捕获 WindowServer presentation tree 或逐扫描屏幕像素，因此不把它表述为硬件屏幕级证明。

## 2. 原子验收结果

| ID | 结果 | 固化证据 |
| --- | :---: | --- |
| `AC-HBAR-01-ROUND-TRIP` | PASS | `testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump` 观察 H range `0→非零→0`；真实 JPEG row 被选中。 |
| `AC-HBAR-02-ANCHOR-CONTINUITY` | PASS | 同一 QtTest 记录 range/value、resize、paint、animation、timer；paint/terminal 固定 wheel scene anchor ≤2 DIP，resize 前置样本只使用横条半厚度边界。 |
| `AC-TOGGLE-DIRECTIONAL-ANCHOR` | PASS | `testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor` 的 cursor 与 usable-center 两方向断言通过。 |
| `AC-TOGGLE-FROZEN-CENTER-ANCHOR` | PASS | `testToggleFitAnd100FreezesViewportCenterDuringScrollbarTransition` 在 H 无 range/V 有 range 的中间态检查 y anchor ≤2 DIP。 |
| `AC-TOGGLE-ANCHOR-LIFETIME` | PASS | 同一测试验证 settle interval = 实际 animation duration + 150 ms，且 animation 运行时 timer active。 |
| `AC-TOGGLE-MONOTONIC-TERMINAL` | PASS | `testToggleFitReturnHasMonotonicStableTerminalSize` 的尺寸 reversal count = 0，animation-finished 与 terminal 一致。 |
| `AC-TOGGLE-QUIESCENT-FINAL` | PASS | 同一测试额外等待 650 ms，quiet size 与 terminal size 一致。 |
| `AC-DURATION-01-LOG-DISTANCE` | PASS | 纯函数断言得到 `232/300/400/400 ms`，等倍率距离相等且上限为 400 ms。 |
| `AC-DURATION-02-FIXED-STEP` | PASS | wheel、标题栏、右键菜单单步保持 200 ms；Toggle 语义跳转在 200–400 ms。 |
| `AC-STATIC-01-TRACEABILITY` | PASS | 两个 Python static gate 均返回 `passed=true`，且结构化用例六字段齐全。 |

## 3. 执行记录

### 3.1 配置与构建

```bash
cmake -S . -B build
cmake --build build --parallel 2
```

结果：PASS；`Fovelle`、`fovelle_tests` 和新增 CTest 条目成功生成/链接。

### 3.2 静态测试

```bash
ctest --test-dir build -R \
  '^(FovelleToggleFitStabilityStatic|FovelleZoomScrollbarDurationStatic)$' \
  --output-on-failure
```

结果：PASS。机器可读产物：

- [`toggle-fit-stability-static.json`](/Users/inostarlin/code/Fovelle/build/test-results/toggle-fit-stability-static.json)
- [`zoom-scrollbar-duration-static.json`](/Users/inostarlin/code/Fovelle/build/test-results/zoom-scrollbar-duration-static.json)

### 3.3 H 条专项动态测试

```bash
ctest --test-dir build -R '^FovelleScrollbarZoomDurationAcceptance$' \
  --output-on-failure
```

结果：PASS（包含 duration 纯函数和真实 wheel 边界用例）。本机日志选择了：

`FOVELLE_SCROLLBAR_ZOOM_SAMPLE provided /Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`

### 3.4 Toggle 锚点专项动态测试

```bash
ctest --test-dir build -R '^FovelleToggleFitAnchorAcceptance$' \
  --output-on-failure
```

结果：PASS；包含定向锚点和 H 消失/V 仍溢出的中间态 anchor 生命周期测试。

### 3.5 Toggle 终态稳定性动态测试

```bash
ctest --test-dir build -R '^FovelleToggleFitStabilityAcceptance$' \
  --output-on-failure
```

结果：PASS。合成 `2560×2938` fixture 与现场可读的 `3840×4407` JPEG row 均执行；两行均报告 `reversals=0`、`zoom_writes=1`，且 quiet window 未改变终态。

### 3.6 既有相关缩放回归

```bash
ctest --test-dir build -R \
  '^(FovelleFiveIssueZoomAcceptance|FovelleFourIssueZoomAcceptance)$' \
  --output-on-failure
```

结果：PASS；既有 wheel、垂直/水平滚动条、键盘锚点和 Toggle blank-space 回归保持通过。

### 3.7 全量 QtTest 目标

```bash
ctest --test-dir build -R '^FovelleTests$' --output-on-failure
```

结果：PASS，201 项 QtTest 通过、0 失败，耗时约 `90.71 s`。为匹配完整
套件的实际运行时间，`FovelleTests` 的 CTest 外层超时已从 90 秒调整为
180 秒；该配置变化只影响测试编排，不改变产品运行时行为。

## 4. 实现证据摘要

- `setAnimatedZoomLevel()` 在动画中暂时关闭相关 QWidget updates；H overflow topology 改变时处理当前排队布局，并在恢复显示前再次应用 pending anchor。
- `zoomAbsolute()` 将中心哨兵一次性解析成具体 `pendingZoomAnchorViewport`；`restorePendingZoomAnchor()` 不再读取变化中的 usable center。
- `finishZoomTransition()`、`resizeEvent()` 和 fit 重算复用同一具体 viewport anchor。
- settle timer 随实际 duration 设置，并在 timer 提前到期而 animation 仍运行时按剩余时长重试，不清除 pending anchor。
- 普通单步入口仍为 200 ms；语义跳转按 `abs(log2(target/current))` 映射到 200–400 ms。

## 5. 证据边界与后续建议

Qt 官方文档及 Qt 6.11.1 源码支持“alignment indent ↔ scrollbar range/value”切换和 AsNeeded viewport 重布局：[QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)、[QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html)、[Qt 6.11.1 qgraphicsview.cpp](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)。动画建模参考 [D3 zoom interpolation](https://d3js.org/d3-interpolate/zoom) 和 [Apple Motion HIG](https://developer.apple.com/design/human-interface-guidelines/motion)。这些来源约束推理，不替代本地测试证据。

如需把证据提升到“屏幕上每一帧均无跳变”，应在目标 macOS 机器上增加屏幕捕获或 Core Animation presentation-layer 时间戳，并与本 probe 的 animation/range/viewport 时间线交叉对齐。
