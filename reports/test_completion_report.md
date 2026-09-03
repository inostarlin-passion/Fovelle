# Fovelle 图片缩放、拖拽与滚动条：测试完成报告

> 报告日期：2026-09-03
> 被测工作树：`/Users/inostarlin/code/Fovelle`（本轮实现尚未提交）
> 环境：macOS Cocoa、Qt 6.11.1、普通 DPR；另有 `QT_SCALE_FACTOR=2` 轨迹验证
> 设计文档：[technical_design_document.md](technical_design_document.md)
> 用例说明：[test_case_specification.md](test_case_specification.md)
> 根因分析：[root_cause.md](root_cause.md)

## 1. 完成结论

五个问题已完成生产代码修复、原子标准拆解、结构化用例设计、测试代码固化和本机验证。最终判定以八条原子验收标准的合取为准：

```text
AC-ALL = AC-SB-NO-STALE-RANGE
       ∧ AC-DRAG-CONTINUOUS
       ∧ AC-DRAG-PRESERVES-OVERFLOW-BARS
       ∧ AC-KBD-ZOOM-CURSOR-ANCHOR
       ∧ AC-TOGGLE-DIRECTIONAL-ANCHOR
       ∧ AC-TOGGLE-VISUAL-STATE
       ∧ AC-WHEEL-CONTENT-ANCHOR
       ∧ AC-NO-LATE-REWRITE
```

在当前本机矩阵内，静态门禁、七个新增动态 QtTest、快捷键回归和普通/HiDPI scrollbar trajectory 回归均为 `PASS`。此结论限定于报告第 6 节的环境与输入矩阵，不宣称覆盖任意平台、任意 style 或 WindowServer/GPU 独立故障。

## 2. 实现完成项

### 2.1 P1：缩小后清除残留滚动范围

- `settlePendingZoomAnchor()` 将 pending margin 转换到当前 transform 后，按水平/垂直轴分别判断 displayed image 是否已经小于 usable viewport。
- 已适合的轴清除 left/right 或 top/bottom retained margin；margin 改变时调用 `updateSceneRect()`，让 `ScrollBarAsNeeded` 的 range 回到真实 image item。
- 终态通过 `testZoomOutClearsStaleVerticalScrollRange` 检查两轴 range、图片尺寸和额外 event-loop turn。

### 2.2 P2：拖拽连续且保留真实溢出范围

- 拖拽、slider、wheel pan、keyboard pan 和 native pan 调用 `cancelPendingZoomAnchor(true)`：取消旧内容锚点和延迟恢复，但保留仍需的可达性 margin。
- generation 使旧 delayed callback 不能覆盖新的 viewport-authority 操作；不在 scrollbar 半提交信号中重建 scene rect。
- `testMousePanKeepsOverflowRangeAndContinuity` 分别断言 tracked image point 的 delta 和 H/V range 的持续性。

### 2.3 P3：键盘和 Toggle 锚点

- `getCursorViewportPosition()` 优先使用最近 viewport mouse event，必要时映射可见全局 cursor；无有效 cursor 才回退 usable viewport center。
- `zoomIn()` 和 `zoomOut()` 将该位置传入统一的 `zoomRelative()`/`zoomAbsolute()` 路径。
- Toggle 下沉至 `QVGraphicsView`，因其需要观察 displayed frame；放大方向用 cursor，缩小方向用 usable viewport center。
- `testKeyboardZoomUsesCursorAnchor`、`testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor` 和 `testToggleFitAnd100UsesDisplayedState` 使用真实 shortcut/action 入口验证。

### 2.4 P4：依据实际 displayed fit 状态切换

- `isImageAtFit()` 独立计算 Zoom-to-Fit level，并要求 displayed zoom 等价且 H/V range 为零；不把 `calculatedZoomMode` 当作当前画面事实。
- `finishZoomTransition()` 和含 pending anchor 的 resize 路径在 AsNeeded relayout 后重新计算 fit，避免 viewport 变大后停在旧的 contained-but-under-sized frame。
- Toggle 动画期间的重复触发由 displayed state 判定，不会因为 mode 已提前改成 fit 而误跳 100%。

### 2.5 P5：修复 scene/view 坐标域和右下角锚点

- `zoomAbsolute()`、`zoomAnchorViewportPoint()` 和 settled anchor 恢复统一基于 `scene()->itemsBoundingRect()`；移除 `getDisplayedContentRect()` 二次 `mapFromScene()` 路径。
- 在 scrollbar visibility 改变后的布局回合，用 `settledZoomAnchor` 与 post-layout timer 再恢复一次可达锚点。
- `testMouseWheelKeepsBottomRightAnchor` 向 viewport 发送真实 `QWheelEvent`，检查右下角内容点和 image edge。

## 3. 原子标准与测试追溯

| 原子标准 | 结构化用例 | 固化测试代码 | 判定 |
| --- | --- | --- | --- |
| `AC-SB-NO-STALE-RANGE` | `TC-SB-ZOOMOUT-ATOMIC` | `testZoomOutClearsStaleVerticalScrollRange` | PASS |
| `AC-DRAG-CONTINUOUS` | `TC-DRAG-CONTINUITY-ATOMIC` | `testMousePanKeepsOverflowRangeAndContinuity`（连续性断言区块） | PASS |
| `AC-DRAG-PRESERVES-OVERFLOW-BARS` | `TC-DRAG-OVERFLOW-ATOMIC` | 同一函数（独立 range 断言区块） | PASS |
| `AC-KBD-ZOOM-CURSOR-ANCHOR` | `TC-KBD-ZOOM-ATOMIC` | `testKeyboardZoomUsesCursorAnchor` | PASS |
| `AC-TOGGLE-DIRECTIONAL-ANCHOR` | `TC-TOGGLE-DIRECTIONAL-ATOMIC` | `testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor`（方向断言区块） | PASS |
| `AC-TOGGLE-VISUAL-STATE` | `TC-TOGGLE-VISUAL-ATOMIC` | `testToggleFitAnd100UsesDisplayedState` | PASS |
| `AC-WHEEL-CONTENT-ANCHOR` | `TC-WHEEL-REAL-ATOMIC` | `testMouseWheelKeepsBottomRightAnchor` | PASS |
| `AC-NO-LATE-REWRITE` | `TC-ASYNC-QUIET-ATOMIC` | `testZoomTerminalStateDoesNotRewriteViewport` | PASS |
| 全部静态合同 | `TC-STATIC-TRACEABILITY` | `tests/zoom_issue_acceptance_static.py` | PASS |

拖拽的两个 case 共享一个不可拆分的真实输入 fixture，但在测试代码中保留独立 assertion block、marker、报告 case 和结果行；Toggle 的方向锚点与 displayed-state 使用独立测试函数。一个 block 失败不会被另一个 block 的结果掩盖。

## 4. 执行记录

### 4.1 构建

```bash
cmake -S . -B build
cmake --build build --parallel 2
```

结果：PASS；`Fovelle`、`fovelle_tests` 和 native helper 构建成功。

### 4.2 静态测试

```bash
python3 tests/zoom_issue_acceptance_static.py \
  --repo . \
  --output build/test-results/zoom-issue-acceptance-static.json
```

结果：PASS；源码实现、输入 oracle、CTest 注册、九个结构化 case 六字段、静态/动态和瞬态/稳态合同均通过。机器结果：[zoom-issue-acceptance-static.json](../build/test-results/zoom-issue-acceptance-static.json)。

### 4.3 新增动态验收

```bash
ctest --test-dir build -R '^FovelleFiveIssueZoomAcceptance$' --output-on-failure
```

结果：PASS；七个新增函数全部通过，CTest 用时 14.61 秒：

- `testZoomOutClearsStaleVerticalScrollRange`
- `testMousePanKeepsOverflowRangeAndContinuity`
- `testKeyboardZoomUsesCursorAnchor`
- `testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor`
- `testMouseWheelKeepsBottomRightAnchor`
- `testZoomTerminalStateDoesNotRewriteViewport`

### 4.4 既有回归

```bash
ctest --test-dir build -R '^FovelleShortcutSettingsTests$' --output-on-failure
ctest --test-dir build -R '^FovelleZoomScrollbarTrajectory$' --output-on-failure
ctest --test-dir build -R '^FovelleZoomScrollbarTrajectoryHiDpi$' --output-on-failure
```

结果：PASS。快捷键 action surface/默认 Z/Toggle 行为/翻译用时 4.84 秒；普通 DPR trajectory 用时 13.92 秒，`QT_SCALE_FACTOR=2` trajectory 用时 42.74 秒，均包含真实 keyboard、wheel、native pinch、逐动画时间和延迟 writer 检查。

### 4.5 全量默认 CTest

```bash
ctest --test-dir build --output-on-failure --timeout 150
```

结果：PASS；共 6 个注册测试全部通过，总用时 143.13 秒：`FovelleTests` 68.43 秒、静态门禁 0.05 秒、五问题动态门禁 14.61 秒、快捷键 4.81 秒、普通 DPR trajectory 14.10 秒、HiDPI trajectory 41.13 秒。native Accessibility driver 仍为 opt-in。

## 5. 证据来源与链式推理

证据链从问题分解开始：

```text
可见跳变
  → value/range、bar geometry、viewport、图片内容点分别取证
  → Qt AsNeeded 语义解释 scrollbar 与 viewport 的双轴耦合
  → Qt scene/view 映射与 QAction 合同解释坐标入口和键盘缺口
  → Qt animation 合同解释 logical/displayed 的时间窗口
  → Fovelle 源码定位 margin、drag cancellation、double transform、迟到 writer
  → 独立 oracle 与真实事件回放交叉验证
  → terminal + quiet 检查证明无迟到重写
```

联网核验使用官方文档和官方源码：

- [Qt `QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html)：`ScrollBarAsNeeded` 的范围/viewport 语义。
- [Qt `QGraphicsView`](https://doc.qt.io/qt-6/qgraphicsview.html)：scene、transform、alignment、viewport 映射语义。
- [Qt `QAction`](https://doc.qt.io/qt-6/qaction.html)：`triggered` 不携带鼠标位置。
- [Qt `QVariantAnimation`](https://doc.qt.io/QT-6/qvariantanimation.html)：当前值是时间插值的 displayed frame。
- [Qt `QPropertyAnimation`](https://doc.qt.io/QT-6/qpropertyanimation.html)：属性动画把中间值写入目标对象。
- [Qt 6.11.1 `qgraphicsview.cpp`](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)：交叉 scrollbar layout 的第二跳验证。

推理前提已在技术设计和用例说明中显式记录：fit 是 displayed level + 零 range；可用视口扣除运行时安全区；外部图片路径不作为 CI 依赖；2 DIP 是本矩阵的几何容差。结论可由源码、机器 JSON、QtTest 输出和官方事实分别复核。

## 6. 限制与未覆盖

- 证据覆盖本机 Qt 6.11.1 Cocoa/arm64、生成 raster、指定窗口尺寸、Disabled/Expensive、普通 DPR/HiDPI 和报告列出的输入序列。
- 指定现场 JPEG `/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg` 用于人工复现；默认动态测试不依赖该外部卷。
- 系统级 CoreGraphics HID/Accessibility 复现仍需显式打开 `FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION`，并不进入默认 CTest。
- 未证明 WindowServer/GPU 合成器在应用已经提交正确 geometry 后产生的独立显示错误。
