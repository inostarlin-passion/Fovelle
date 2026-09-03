# Fovelle 图片缩放四项问题：测试完成报告

> 报告日期：2026-09-03
> 被测工作树：`/Users/inostarlin/code/Fovelle`（实现尚未提交）
> 环境：macOS Cocoa、Qt 6.11.1、arm64；另执行普通 DPR 与 HiDPI 轨迹回归
> 根因依据：[root_cause.md](root_cause.md)
> 技术设计：[technical_design_document.md](technical_design_document.md)
> 用例说明：[test_case_specification.md](test_case_specification.md)

## 1. 完成结论

四个用户问题均已完成：

1. wheel 缩放的图片内容锚点连续，且延迟 writer 不再造成终态跳变；
2. 缩小到图片小于视口时，V range 不在可观察帧中短暂复活；
3. 图片右侧外点连续放大三格时，适配宽度不产生伪 H range；
4. Toggle Fit and 100% 到 100% 时，图片覆盖 usable viewport，并按最近可行位置钳制，不保留右侧可避免空白。

四项发布判定为：

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

## 2. 实现完成项

### 2.1 真实内容几何与锚点

- `getScrollContentRect()` 直接使用当前 displayed image content，不再将外部鼠标锚点转换成 `sceneRect` 的虚拟 margin。
- `zoomAbsolute()` 和 `zoomAnchorViewportPoint()` 统一从 `scene()->itemsBoundingRect()` 映射图片；移除了旧版“已缩放矩形再次 `mapFromScene()`”的 double-transform 路径。
- 外部鼠标点只投影到真实图片边界，最后由 native scrollbar range 钳制到内容可达位置。

### 2.2 AsNeeded 布局与迟到 writer

- pending anchor 在 resize、嵌套 scene update 和 `verticalScrollBarGeometryTimer` 回合后重新恢复；post-layout 使用零延迟回合处理 H/V 交叉布局带来的第二次 viewport 改变。
- settled anchor 只在对应图片轴真实溢出且存在 native range 时恢复；不存在 synthetic range 可供旧 margin 复活。
- titlebar safe-area padding 按当前 usable axis cap，不能把已适配图片重新推成需要滚动条的 scene。
- `cancelPendingZoomAnchor()` 不再携带已被移除的 margin-preservation 参数。

### 2.3 测试固化

- `ZoomIssueProbe` 记录真实 image/usable viewport、H/V range/value、Paint/Resize/Show/Hide、animation 和具名 timer。
- 四个主函数使用真实 `QWheelEvent` 或真实 `QTest::keySequence`，分别覆盖初态、暂态和终态。
- P4 使用独立 preferred-origin/feasible-origin oracle，避免测试只断言“看起来没有右侧空白”。
- `zoom_issue_acceptance_static.py` 把九条原子标准、生产 marker、六字段用例、测试函数和 CTest 注册固化为 JSON 门禁。

## 3. 原子标准追溯与结果

| 原子标准 | 结构化用例 | 固化测试代码 | 结果 |
| --- | --- | --- | --- |
| `AC-P1-ANCHOR-CONTINUITY` | `TC-P1-WHEEL-TRAJECTORY` | `testWheelZoomHasNoPositionJumpTrajectory`：rendered-frame anchor error ≤2 DIP | PASS |
| `AC-P1-NO-LATE-JUMP` | `TC-P1-WHEEL-TRAJECTORY` | 同函数：650 ms quiet 前后 anchor 差值 ≤1 DIP | PASS |
| `AC-P2-NO-TRANSIENT-VBAR` | `TC-P2-ZOOMOUT-VBAR` | `testZoomOutHasNoTransientVerticalScrollBar`：fit 帧 V range=0 | PASS |
| `AC-SB-NO-STALE-RANGE` | `TC-P2-ZOOMOUT-VBAR` | 同函数：终态 image fit 且 H/V range=0 | PASS |
| `AC-P3-NO-TRANSIENT-HBAR` | `TC-P3-RIGHT-OUTSIDE-WHEEL` | `testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar`：fit 宽度帧 H range=0 | PASS |
| `AC-P3-CROSS-AXIS-STABILITY` | `TC-P3-RIGHT-OUTSIDE-WHEEL` | 同函数：每条 sample 重新读取 usable viewport | PASS |
| `AC-P4-NO-AVOIDABLE-BLANK` | `TC-P4-TOGGLE-NO-BLANK` | `testToggleFitTo100HasNoAvoidableBlankSpace`：四边覆盖 + probe | PASS |
| `AC-P4-OPTIMAL-CLAMP` | `TC-P4-TOGGLE-NO-BLANK` | 同函数：preferred/feasible/expected origin oracle ≤2 DIP | PASS |
| `AC-P4-TOGGLE-DIRECTIONAL-ANCHOR` | `TC-P4-TOGGLE-NO-BLANK` | 同函数：外侧 cursor + 真实 shortcut + Toggle 到 1.0 | PASS |
| 全部静态追溯合同 | `TC-STATIC-TRACEABILITY` | `zoom_issue_acceptance_static.py` | PASS |

一个 GUI fixture 中共享相关原子标准是执行优化；每个原子标准仍有独立 ID、预期结果、marker、assertion 和报告结果，不以同一条总断言替代。

## 4. 执行证据

### 4.1 构建

```bash
cmake --build build --parallel 2
```

结果：PASS；`Fovelle`、`fovelle_tests` 和 native helper 构建成功。

### 4.2 静态门禁

```bash
ctest --test-dir build -R '^FovelleZoomIssueStatic$' \
  --output-on-failure --timeout 30
```

结果：PASS；检查原子追溯、生产合同、真实输入、初态/暂态/终态字段和 CTest 注册。机器证据：[zoom-issue-acceptance-static.json](../build/test-results/zoom-issue-acceptance-static.json)。

### 4.3 四项主动态验收

```bash
ctest --test-dir build -R '^FovelleFourIssueZoomAcceptance$' \
  --output-on-failure --timeout 120
```

结果：PASS；以下四个函数均通过：

- `testWheelZoomHasNoPositionJumpTrajectory`
- `testZoomOutHasNoTransientVerticalScrollBar`
- `testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar`
- `testToggleFitTo100HasNoAvoidableBlankSpace`

### 4.4 相邻回归

```bash
ctest --test-dir build -R '^FovelleFiveIssueZoomAcceptance$' \
  --output-on-failure --timeout 150
ctest --test-dir build -R '^FovelleZoomScrollbarTrajectory$' \
  --output-on-failure --timeout 120
ctest --test-dir build -R '^FovelleZoomScrollbarTrajectoryHiDpi$' \
  --output-on-failure --timeout 120
```

结果：PASS；既有拖拽、键盘 Zoom In/Out、Toggle displayed-state、真实 wheel anchor、terminal quiet，以及普通 DPR/`QT_SCALE_FACTOR=2` 下的 keyboard/wheel/native-pinch scrollbar trajectory 均通过。它们是交叉回归证据，不替代四项主验收。

### 4.5 全量默认 CTest

```bash
ctest --test-dir build --output-on-failure --timeout 180
```

结果：PASS，7/7 测试通过，耗时 164.09 s；包含完整 `FovelleTests`、静态追溯、既有五项相邻缩放回归、四项主动态验收、快捷键设置以及普通 DPR/HiDPI 轨迹回归。

## 5. 事实依据与链式推理

证据链以问题分解为起点：

```text
root_cause.md 的共同根因候选
  → 识别 scene extent、usable viewport、range/value、bar event、anchor、timer writer 缺口
  → Qt 文档确认 sceneRect/mapping/AsNeeded/action/animation 公开语义
  → Qt 6.11.1 源码确认 H/V 交叉占位和 viewport 重算
  → Fovelle 源码确认 margin、动画、resize、settle、constraint 写入链
  → 独立几何 oracle + 真实输入测试确认修复后的可观察状态
  → terminal quiet 排除迟到 writer 改写
```

显式前提是：scene range 只能代表真实图片内容；usable viewport 必须读取运行时安全区；图片外点只能作为方向偏好；错误暂态不能由正确终态抵消；2 DIP 是本测试矩阵的舍入容差。以上前提、证据缺口和交叉验证约束已在技术设计和用例说明中逐项记录。

联网核验来源：

- [Qt `QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html)
- [Qt `QGraphicsView`](https://doc.qt.io/qt-6/qgraphicsview.html)
- [Qt `QAction`](https://doc.qt.io/qt-6/qaction.html)
- [Qt `QVariantAnimation`](https://doc.qt.io/QT-6/qvariantanimation.html)
- [Qt `QPropertyAnimation`](https://doc.qt.io/qt-6/qpropertyanimation.html)
- [Qt `QTimer`](https://doc.qt.io/qt-6/qtimer.html)
- [Qt 6.11.1 `qgraphicsview.cpp`](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)

## 6. 限制

- 自动化 fixture 不依赖 `/Volumes/CRYSTAL`；现场 JPEG 仍需在该卷可用时按用户步骤人工复现。
- 结果覆盖本机 Qt 6.11.1 Cocoa/arm64、报告列出的窗口和输入矩阵，不外推到其他平台 style。
- 未将 WindowServer/GPU 合成器在应用已提交正确 geometry 后的独立残影归因于本修复。
- 系统级 Accessibility/HID 驱动不是默认 CTest 门禁。
