# 测试完成报告：滚动条阈值与自适应缩放时长

日期：2026-09-04
仓库：`/Users/inostarlin/code/Fovelle`
构建目录：`/Users/inostarlin/code/Fovelle/build`

## 1. 完成结论

实现已完成，原子验收标准全部有对应测试代码；静态检查、专项动态检查和既有相关回归均已执行。本次执行机可读现场 JPEG，专项测试实际打开并验证了该文件；同一测试仍保留同尺寸同宽高比合成图回退路径。

## 2. 原子验收结果

| ID | 结果 | 证据 |
| --- | :---: | --- |
| `AC-HBAR-01-ROUND-TRIP` | PASS | H range 观察到 `0→非零→0`；专项 QtTest 通过 |
| `AC-HBAR-02-ANCHOR-CONTINUITY` | PASS | 可见 paint/terminal anchor 误差不超过 8 DIP；专项 QtTest 通过 |
| `AC-DURATION-01-LOG-DISTANCE` | PASS | `232/300/400/400ms` 和等倍率相等断言通过 |
| `AC-DURATION-02-FIXED-STEP` | PASS | wheel、标题栏/右键 Zoom In 为 200ms；Toggle 为有界自适应时长 |
| `AC-STATIC-01-TRACEABILITY` | PASS | 静态 JSON `passed=true` |

## 3. 执行命令与结果

### 3.1 构建

```bash
cmake --build build --parallel 2
```

结果：PASS，`Fovelle` 和 `fovelle_tests` 均成功链接。

### 3.2 静态测试

```bash
ctest --test-dir build -R '^FovelleZoomScrollbarDurationStatic$' --output-on-failure
```

结果：PASS；产物为 [`zoom-scrollbar-duration-static.json`](/Users/inostarlin/code/Fovelle/build/test-results/zoom-scrollbar-duration-static.json)。

### 3.3 本次专项动态测试

```bash
ctest --test-dir build -R '^FovelleScrollbarZoomDurationAcceptance$' --output-on-failure
```

结果：PASS；包含：

- `testZoomTransitionDurationUsesLogDistance`
- `testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump`

专项测试使用 Cocoa、真实 wheel 入口、3 格预热 + 第 4 格 + 反向 1 格，并在所有可见事件阶段采样；本次日志记录 `FOVELLE_SCROLLBAR_ZOOM_SAMPLE provided /Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`。

### 3.4 既有相关回归

```bash
ctest --test-dir build \
  -R 'Fovelle(FourIssueZoomAcceptance|ToggleFitStabilityAcceptance)$' \
  --output-on-failure
```

结果：PASS；既有四问题缩放回归和 Fit/100% 稳定性回归均通过。

### 3.5 全量测试状态

```bash
ctest --test-dir build -R '^FovelleTests$' --output-on-failure
```

该全量 CTest 在 90 秒项目超时，已输出的各 suite 中未出现断言失败；GraphicsViewTests 已完成 `63 passed`，之后 WindowBehaviorTests 仍在继续。全量超时属于执行时限结果，不能记为全量 PASS；本次验收以已完成的专项门禁和相关回归结果为准。

## 4. 修改内容摘要

- 在 `QVGraphicsView::setAnimatedZoomLevel()` 中将 H topology 改变期间的 QWidget/native 几何提交合并到一个不可见提交窗口，布局事件完成且 anchor 恢复后才允许下一帧呈现。
- 新增 `QVGraphicsView::zoomTransitionDurationMs()`：自适应模式使用 `abs(log2(target/current))`，200–400ms 封顶。
- `recalculateZoom()` 与 Fit→100% 使用自适应模式；滚轮/键盘/菜单单步入口保留 200ms。
- 新增专项 QtTest、静态 Python 门禁和 CTest 注册。

## 5. 证据与限制

Qt 官方 API 和 Qt 6.11.1 源码支持“适配 indent ↔ scrollbar range/value”切换及 AsNeeded 布局链：[QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)、[Qt 6.11.1 qgraphicsview.cpp](https://raw.githubusercontent.com/qt/qtbase/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)。

动画约束参考 [Apple Motion HIG](https://developer.apple.com/design/human-interface-guidelines/motion) 和 [D3 zoom interpolation](https://d3js.org/d3-interpolate/zoom)；400ms 是本实现的显式产品上限。

本报告证明应用模型与可见 QWidget/native 提交边界的回归条件；未声称完成 WindowServer 逐扫描帧捕获。若要闭合该最后证据缺口，应在具备现场卷、真实窗口和屏幕捕获权限的 macOS 机器上执行相同测试并保存 presentation-layer/frame 对时日志。
