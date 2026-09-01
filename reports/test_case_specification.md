# 图片拖拽橡皮筋效果测试用例说明

- 规格版本：1.0
- 编写日期：2026-09-01
- 被测范围：`ScrollHelper` 图片平移边界与上层约束调用
- 测试代码：[`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp) 的 `ScrollHelperTests`
- 静态追踪：[`tests/rubber_band_acceptance_static.py`](../tests/rubber_band_acceptance_static.py)

## 原子标准与测试映射

| 原子标准 | 测试用例 | 测试代码 | 类型 |
|---|---|---|---|
| AC-RB-MIN-EDGE | TC-RB-MIN-EDGE | `ScrollHelperTests::testMinimumEdgesAreHardClamped` | QtTest 动态单元测试 |
| AC-RB-MAX-EDGE | TC-RB-MAX-EDGE | `ScrollHelperTests::testMaximumEdgesAreHardClamped` | QtTest 动态单元测试 |
| AC-RB-NO-RETURN-ANIMATION | TC-RB-NO-RETURN-ANIMATION | `ScrollHelperTests::testEdgePositionDoesNotReboundAfterRelease` | QtTest 动态时序测试 |
| AC-RB-INTERIOR-MOTION | TC-RB-INTERIOR-MOTION | `ScrollHelperTests::testInteriorDragPreservesExactMovement` | QtTest 动态单元测试 |
| AC-RB-CONSTRAINT-OPT-OUT | TC-RB-CONSTRAINT-OPT-OUT | `ScrollHelperTests::testUnconstrainedModeRemainsUnbounded` | QtTest 动态非回归测试 |

测试夹具用 `QScrollArea` 提供滚动条，用固定的 `contentRect=1600x900` 和 `usableViewportRect=600x400` 让合法范围确定为横向 `[0,1000]`、纵向 `[0,500]`。滚动条实际范围故意扩大到 `[-2000,2000]`，因此旧实现的越界值不会被 Qt 先行吞掉，能够直接观测 helper 是否产生 overscroll。

## TC-RB-MIN-EDGE

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证图片向左上边缘继续拖动时，横向和纵向都在最小边界同步停止，不产生越界位移。 |
| 前置条件 | `ScrollHelper` 夹具已创建；内容矩形为 `1600x900`；可用 viewport 为 `600x400`；启用位置约束；滚动条实际范围宽于图片合法范围。 |
| 输入数据 | 当前滚动位置 `(0,0)`；平移 delta `(-120,-80)`。 |
| 操作步骤 | 1. 将滚动条设到 `(0,0)`。<br>2. 调用 `helper.move(QPointF(-120,-80))` 模拟继续向左上拖动。<br>3. 调用 `helper.constrain()` 模拟释放后的约束路径。<br>4. 分别读取两个滚动条值。 |
| 预期结果 | `move()` 返回后横/竖值均为 `0`；`constrain()` 后仍为 `0`，不出现负值。 |
| 后置条件 | 测试夹具析构，滚动条和 helper 一并释放。 |

## TC-RB-MAX-EDGE

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证图片向右下边缘继续拖动时，横向和纵向都在最大边界同步停止，不产生越界位移。 |
| 前置条件 | 与 TC-RB-MIN-EDGE 相同；计算出的合法范围为横向 `[0,1000]`、纵向 `[0,500]`。 |
| 输入数据 | 当前滚动位置 `(1000,500)`；平移 delta `(120,80)`。 |
| 操作步骤 | 1. 将滚动条设到 `(1000,500)`。<br>2. 调用 `helper.move(QPointF(120,80))` 模拟继续向右下拖动。<br>3. 调用 `helper.constrain()`。<br>4. 分别读取两个滚动条值。 |
| 预期结果 | `move()` 返回后横/竖值均为 `(1000,500)`；`constrain()` 后仍保持该值，不出现超过最大值的状态。 |
| 后置条件 | 测试夹具析构，滚动条和 helper 一并释放。 |

## TC-RB-NO-RETURN-ANIMATION

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证释放拖动后不再启动延迟回弹，边缘位置不会在后续时间窗口发生二次变化。 |
| 前置条件 | 约束已启用；起点为 `(0,0)`；滚动条范围允许旧实现的非法中间值；已连接两个 `QScrollBar::valueChanged` 的 `QSignalSpy`。 |
| 输入数据 | 平移 delta `(-120,-80)`；调用 `constrain()` 后等待 `350ms`，该时长超过旧实现的 `250ms` 回弹动画。 |
| 操作步骤 | 1. 调用 `move()` 尝试越过左上边缘。<br>2. 调用 `constrain()` 模拟鼠标释放。<br>3. 记录此时两个 signal spy 的计数和值。<br>4. 运行 Qt 事件循环等待 `350ms`。<br>5. 再次读取滚动条值和 signal 计数。 |
| 预期结果 | 等待前后值均为 `(0,0)`；等待期间两个 `valueChanged` 计数均不增加，不存在回弹动画或延迟修正。 |
| 后置条件 | 事件循环结束；无 helper 动画计时器需要清理；夹具析构。 |

## TC-RB-INTERIOR-MOTION

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证移除边缘阻力不会改变合法范围内部的普通图片拖动。 |
| 前置条件 | 约束已启用；当前值为内部位置 `(300,200)`，不接近任一边界。 |
| 输入数据 | 平移 delta `(-75,65)`；等待窗口 `350ms`。 |
| 操作步骤 | 1. 调用 `helper.move(QPointF(-75,65))`。<br>2. 检查即时值。<br>3. 调用 `helper.constrain()` 并等待 `350ms`。<br>4. 再次检查最终值。 |
| 预期结果 | 即时值为 `(225,265)`；等待和约束后仍为 `(225,265)`；delta 未被缩放或修正。 |
| 后置条件 | 测试夹具析构，滚动条和 helper 一并释放。 |

## TC-RB-CONSTRAINT-OPT-OUT

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证关闭图片位置约束时，已有的自由越界浏览行为不被本次修复误伤。 |
| 前置条件 | `parameters.shouldConstrain=false`；当前值为 `(0,0)`；滚动条实际范围可表示负值。 |
| 输入数据 | 平移 delta `(-120,-80)`。 |
| 操作步骤 | 1. 关闭夹具约束开关。<br>2. 调用 `helper.move(QPointF(-120,-80))`。<br>3. 调用 `helper.constrain()`。<br>4. 读取两个滚动条值。 |
| 预期结果 | 最终值为 `(-120,-80)`；关闭约束时仍不强制钳制到图片边缘。 |
| 后置条件 | 测试夹具析构；不会修改持久化用户设置。 |

## 静态追踪验证

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证硬钳制实现、回弹符号删除、上层调用点、五个测试 marker 和规格必备字段形成可审计闭环。 |
| 前置条件 | 源码、测试源码、本规格和 Python 3 均可读取。 |
| 输入数据 | 仓库根目录；输出路径 `.tmp/rubber-band-static.json`。 |
| 操作步骤 | 执行 `python3 tests/rubber_band_acceptance_static.py --repo . --output .tmp/rubber-band-static.json`；读取 JSON 的 `passed` 和每个 check 的 `pass`。 |
| 预期结果 | `ST-RB-01` 至 `ST-RB-05` 全部为 `true`，进程返回码为 `0`。 |
| 后置条件 | JSON 证据保留在 `.tmp`；不修改产品源码、用户设置或测试输入。 |
