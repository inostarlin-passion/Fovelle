# 技术设计文档：滚动条阈值位置稳定与自适应缩放时长

日期：2026-09-04
仓库：`/Users/inostarlin/code/Fovelle`

## 1. 目标与范围

本次实现处理两个相关但可独立验收的问题：

1. 图片缩放跨越 `ScrollBarAsNeeded` 的水平溢出边界时，H range 从零变为非零、再从非零变为零，图片不能因 Qt 的定位语义切换而出现可见跳变。
2. “适合窗口 ↔ 100%”等语义缩放跳转不再统一使用 200ms；根据当前缩放比例与目标比例的对数距离计算有界自适应时长。鼠标滚轮/键盘一次操作仍固定 200ms。

不改变滚动条策略、图片解码方式、原生 Metal 呈现路径和用户设置格式。

## 2. 问题分解、显式前提与原子验收标准

### 2.1 显式前提

- P1：离散滚轮步长使用默认倍率 `1.25`；一次非触控板 detent 是一个固定 zoom step。
- P2：图片从 Fit 开始，H/V 使用 `Qt::ScrollBarAsNeeded`，鼠标锚点启用。
- P3：可见位置稳定性以“当前可用 viewport 下的同一 scene 点映射连续”为准；当 viewport 因 H 条改变高度时，允许由条厚度产生的半厚度中心位移，但禁止先显示错误坐标再延迟回弹。
- P4：真实 JPEG 的绝对路径可能只在本机挂载；测试无该文件时使用相同 `3840 × 4407` 比例的合成图，不把不可访问外部状态伪称为已验证。
- P5：自适应时长基线为已验证的普通单步 200ms，上限 400ms 是本产品工程参数，不是 Apple 的硬性规定。

### 2.2 原子验收标准

| ID | 原子标准 | 可核验证据 |
| --- | --- | --- |
| `AC-HBAR-01-ROUND-TRIP` | 3 格预热后第 4 格使 H range 变为非零，反向 1 格使其归零；最终 H/V 状态正确。 | QtTest `testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump` 的 range 轨迹 |
| `AC-HBAR-02-ANCHOR-CONTINUITY` | H topology 切换的可见帧不消费未完成的 indent/range/value 混合状态；锚点只发生可预测的 viewport 半厚度位移。 | 同一 QtTest 的 paint/resize/terminal 样本和 `≤ 8 DIP` 可见锚点断言 |
| `AC-DURATION-01-LOG-DISTANCE` | 语义缩放时长按 `abs(log2(target/current))` 计算，200–400ms 有界，等倍率距离等时长。 | `zoomTransitionDurationMs` 纯函数测试和源码静态检查 |
| `AC-DURATION-02-FIXED-STEP` | 鼠标滚轮、键盘 Zoom In/Out 和菜单单步缩放仍使用 200ms。 | `testZoomTransitionDurationUsesLogDistance`、`testZoomTransitionCoversWheelKeyboardAndMenus` |
| `AC-STATIC-01-TRACEABILITY` | 生产代码、结构化用例、测试代码、CTest 注册和完成报告可互相追溯。 | `tests/zoom_scrollbar_duration_static.py` |

## 3. 经核验事实与链式推理

### 3.1 H 条阈值机制

Qt 官方文档说明：`QGraphicsView::alignment` 只在整个 scene 可见、没有可见滚动条时决定 scene 位置；`ScrollBarAsNeeded` 在内容超过 viewport 时启用滚动条。[QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)、[Qt Namespace 的 ScrollBarAsNeeded](https://doc.qt.io/qt-6/qt.html)。

与运行环境相同版本的 Qt 6.11.1 源码进一步显示，`QGraphicsViewPrivate::recalculateContentSize()` 在横向适配分支设置 `leftIndent` 并将 H range 设为 `0..0`，在溢出分支清除 `leftIndent` 并建立 H range；设置 range 还可能改变 value 并触发 `scrollContentsBy()`。[Qt 6.11.1 `qgraphicsview.cpp`](https://raw.githubusercontent.com/qt/qtbase/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp#L313-L429)。

因果链为：

```text
transform tick
  → scene 映射宽度跨过 viewport
  → Qt 在 alignment indent / scrollbar value 间切换
  → rangeChanged 排队触发 AsNeeded layout
  → H 条改变 viewport 高度
  → resize / anchor / native layer 几何被分别提交
```

因此，仅在最终状态恢复锚点不能证明没有位置跳变；必须把 topology 变化期间的可见帧作为测试对象。

### 3.2 自适应时长度量

缩放是倍率变化，不是百分数加法。使用

```text
d = abs(log2(target / current))
duration = 200ms + (400ms - 200ms) * min(d / 2, 1)
```

于是 `50%→100%`、`100%→200%` 和 `25%→50%` 都是一个 octave，得到相同距离；`25%→100%` 达到 400ms 上限。这样不把“百分点差”误当成视觉距离。

外部依据提供约束而非替代产品参数：D3 的 zoom interpolation 将缩放视为专门的 zoom 路径，并暴露基于路径长度的推荐 duration。[D3 zoom interpolation](https://d3js.org/d3-interpolate/zoom)；Apple 要求自定义反馈动画简短、精准且可取消。[Apple Motion HIG](https://developer.apple.com/design/human-interface-guidelines/motion)。据此采用“单步固定、语义跳转按对数距离并封顶”的单一规则。

## 4. 实现设计

### 4.1 缩放帧几何提交

在 `QVGraphicsView::setAnimatedZoomLevel()` 中：

1. 仅在动画运行期间暂时禁用 view、viewport、H/V scrollbar 的 QWidget 更新。
2. 保存 H overflow topology，设置 transform，调用 `updateSceneRect()` 并立即恢复 pending anchor。
3. 若本帧导致 H topology 变化，处理当前事件循环中排队的 Qt layout；排除用户输入和 socket，避免交互重入。
4. 在同一帧再次恢复 anchor，再提交 HDR/SDR native renderer 几何，最后恢复 QWidget 更新。
5. 若本帧没有 H topology 变化，不额外 drain event loop，保留普通动画路径的性能和时序。

这样 Qt 的 range/value 信号仍然存在，但它们发生在不可见的更新窗口内；下一次可见/native frame 只消费完成布局和锚点恢复后的 geometry generation。

### 4.2 自适应时长分发

新增公开、无状态的 `QVGraphicsView::zoomTransitionDurationMs(from, to, adaptive)`，便于确定性测试：

- `adaptive=false` 或输入无效：返回 200ms。
- `adaptive=true`：按上式计算并限制到 400ms。
- `recalculateZoom()`（Fit/Fill 等计算目标）传入 adaptive=true。
- `toggleFitAnd100()` 的 Fit→100% 传入 adaptive=true。
- `zoomRelative()`、`zoomIn()`、`zoomOut()`、键盘/菜单单步入口保留 adaptive=false。

动画对象仍使用 `QEasingCurve::OutCubic`，不改变中断和终态归一化逻辑。

## 5. 风险与边界

- H 条出现/消失必然改变可用 viewport 高度；测试把由 native 条厚度造成的半厚度中心变化与真正的错误位置跳变区分开。
- `processEvents()` 只在 H topology 改变且 QWidget 更新被禁用时调用；若未来引入更多异步 geometry writer，应将其纳入同一提交屏障和测试 probe。
- 本实现证明 Qt/Fovelle 模型状态在可见帧上的连续性；真实 CALayer presentation tree 的逐扫描帧仍需系统级屏幕捕获才能完全闭合，不能由 QWidget 几何单独推断。

## 6. 代码与测试位置

- 生产：`src/qvgraphicsview.h`、`src/qvgraphicsview.cpp`
- 动态测试：`tests/tst_qviewtests.cpp`
- 静态测试：`tests/zoom_scrollbar_duration_static.py`
- CTest：`tests/CMakeLists.txt`
- 完成证据：`reports/test_completion_report.md`
