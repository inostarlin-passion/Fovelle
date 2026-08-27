# GitHub Actions 检查失败：唯一修复方案与正确性证明

## 1. 问题定义与确证事实

本文件针对当前仓库最新可观察的失败 run，而不是针对一个假设的 CI 环境。GitHub Actions 的 [run 33106593917](https://github.com/inostarlin-passion/Fovelle/actions/runs/33106593917) 的 `Run Unit Tests` job 中，构建成功，`FovelleTests`、`FovelleShortcutSettingsTests` 和 `FovelleSettingsAudit` 成功，唯一失败目标是 `FovelleTaskAcceptanceAudit`，其唯一失败用例为 `CI-UNIT-002`。GitHub 官方文档说明，失败 run 应沿失败 step 的日志诊断，因此该 run 是本推导的外部观测入口：[Using workflow run logs](https://docs.github.com/en/actions/how-tos/monitor-workflows/use-workflow-run-logs)。

失败用例执行：

```text
tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip
```

失败前的测试代码在滚动条变更后立即执行：

```cpp
bar->setValue(bar->value() + 6);
QVERIFY(view->hasPendingVectorRefinement());
QTRY_VERIFY_WITH_TIMEOUT(!recorder.recordedAreas().isEmpty(), 1000);
```

而 `hasPendingVectorRefinement()` 的实现是：

```cpp
return vectorInteractionActive || activeAsyncRequest.has_value()
        || pendingAsyncRequest.has_value();
```

这三个字段描述的是异步工作的当前瞬间状态，不是滚动产生的可见输出。该测试真正的验收目标是：滚动后的首个 viewport Paint 的面积不超过 viewport 面积的 5%；后续异步 refinement Paint 不得被误算为滚动 Paint。

## 2. 多跳检索与显式前提

以下事实来自可复核的仓库源码和上游文档；每个推导都列出其所依赖的前提。

1. **CI 入口。** [GitHub Actions workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax) 规定 job 的运行环境由 workflow 的 `runs-on` 选择；本仓库失败 job 使用 macOS hosted runner 并安装 Qt 6.11.2。
   前提：本地 macOS 15.7.9/Qt 6.11.1 的通过不能覆盖 hosted macOS 26/Qt 6.11.2 的事件调度差异。
   推导：必须检查测试对事件顺序的假设，而不能只比较一次本地返回码。

2. **QTRY 的事件语义。** Qt 6.11.2 的 [`QTRY_VERIFY_WITH_TIMEOUT`](https://doc.qt.io/qt-6/qtest.html) 会重复检查条件，并在检查之间处理事件；Qt 测试最佳实践也明确建议使用 `QTRY_*` 或 `QSignalSpy` 等待异步完成，并警告避免 timing-dependent behavior：[Qt Test Best Practices](https://doc.qt.io/qt-6/qttest-best-practices.html)。
   前提：本测试的 `QTRY_VERIFY_WITH_TIMEOUT(!recorder.recordedAreas().isEmpty(), 1000)` 会泵送 GUI 事件。
   推导：在“首个 Paint 出现”被观察到之前，异步 refinement 可能已经完成；因此紧邻其前的 `hasPendingVectorRefinement()` 可能为真，也可能为假。

3. **异步完成语义。** Qt 的 [`QFutureWatcher::finished`](https://doc.qt.io/qt-6/qfuturewatcher.html) 在被观察的 future 完成时发出；Fovelle 将该信号连接到 `asyncVectorTileFinished()`，该函数保留 tile 并调用 `update()`。
   前提：vector tile 的完成回调和 scrollbar 产生的 viewport update 都可以进入同一个 Cocoa/Qt 事件循环。
   推导：存在合法事件顺序 `scroll → future finished → refinement Paint → test observes state`，在此顺序中 `hasPendingVectorRefinement()` 已为假，但滚动局部重绘仍然正确。

4. **滚动输出语义。** Qt [`QGraphicsView` 文档](https://doc.qt.io/qt-6/qgraphicsview.html) 明确指出，滚动时只需要部分失效；本测试使用 `PaintRegionRecorder` 直接观察 viewport Paint 区域。
   前提：验收应该观测外部可见 Paint 结果，而不是要求某一个内部异步状态在特定纳秒仍保持未完成。
   推导：`recordedAreas()` 的首个元素是滚动动作后到达的第一个可见 Paint，后续元素可以属于异步 refinement；用首个元素验证滚动，用其余元素做诊断，正好与因果顺序一致。

5. **控制反例。** 同一渲染序列的本地诊断记录了 `dirty_ratio=0.009554`、`max_observed_dirty_ratio=1.000000`、`paint_events=2`：首个 Paint 是局部暴露条带，后一个 Paint 是 refinement。该事实同时说明“取最大 Paint”不等价于“滚动 Paint”，也说明异步 Paint 确实可以在等待窗口中到达。
   前提：不改变生产渲染实现，仅改变测试的归因规则。
   推导：5% 阈值必须应用于首个 Paint；后续最大值只能是诊断数据。

## 3. 唯一解决方案

在以下明确约束下，唯一解决方案是：

> **删除滚动后对 `hasPendingVectorRefinement()` 的立即 `QVERIFY`，保留对首个 Paint 的 `QTRY` 等待，将 5% 断言只应用于 `recordedAreas().constFirst()`，并在移除 event filter 前继续等待 refinement 结束以保证安全清理；不修改生产渲染代码、不放宽 5% 阈值、不增加失败重试。**

约束集合为：

* C1：修复 `CI-UNIT-002` 的 false negative，并同时覆盖通过完整 `FovelleTests` 间接执行同一方法的集成路径。
* C2：保留产品的 vector rendering、scroll reuse 和 5% 性能合同。
* C3：测试必须以非侵入式的可见 Paint 结果为判据，并允许异步完成发生在不同事件顺序。
* C4：等待必须有界、可重复、可诊断；不能用重试隐藏真实失败。
* C5：修复范围最小，只改变错误的测试观察前提。

这里的“唯一”是相对于满足 C1–C5 的最小观察器修复而言：候选修复不能改变被测产品、验收阈值或失败处理，只能删除不必要的瞬时谓词并保留因果输出谓词。方案的完整测试断言为：

```text
N > 0
∧ first(recordedAreas) / viewportArea ≤ 0.05
∧ eventually(!hasPendingVectorRefinement()) before teardown
```

其中 `N` 是滚动后 recorder 捕获的 Paint 数量；最后一项只用于安全 teardown，不参与滚动面积判定。

## 4. 数学正确性证明

### 4.1 符号

设：

* `V > 0` 是 viewport 面积；
* `E = (e₀, e₁, …, eₙ₋₁)` 是安装 recorder 后按到达顺序捕获的 Paint 事件序列；
* `A(eᵢ) ≥ 0` 是事件 `eᵢ` 的 dirty area；
* `S` 是滚动事件；
* `R` 是 vector refinement 完成后由 `update()` 触发的独立 Paint；
* `L` 是产品性能合同 `A(e₀) / V ≤ 0.05`；
* `H(t)` 是时刻 `t` 的 `hasPendingVectorRefinement()`；
* `T` 是 recorder 被移除前的 teardown 时刻。

由测试前置清空事件和“先安装 recorder、后改变 scrollbar”的操作顺序，得到前提 P1：若 `E` 非空，则 `e₀` 是 `S` 产生的第一个可观测 viewport Paint。由 Qt 的滚动局部失效语义，得到前提 P2：正确的滚动实现满足 `L`。由 QFutureWatcher 和 QTRY 的事件处理语义，得到前提 P3：`R` 可以在 `S` 后、测试观察期间到达，且 `H(t)` 可以在 `e₀` 到达前后任一时刻从真变假。

### 4.2 完备性（不再误拒合法执行）

旧谓词为：

```text
O = H(t₀) ∧ (N > 0) ∧ maxᵢ A(eᵢ) / V ≤ 0.05
```

取合法事件序列反例：`E = (e₀, e₁)`，其中 `e₀` 为滚动暴露条带，`A(e₀)/V = 0.009554`；`e₁` 为 refinement，`A(e₁)/V = 1`。若 refinement 在立即断言前完成，则 `H(t₀)=false`；并且 `maxᵢ A(eᵢ)/V=1`。所以 `O=false`，尽管 `L=true`。因此旧谓词不是验收目标的完备判据。

新谓词为：

```text
N > 0 ∧ A(e₀) / V ≤ 0.05 ∧ ∃t ≤ T: ¬H(t)
```

同一反例中，`N=2`、`A(e₀)/V=0.009554≤0.05`，且 refinement 完成后存在 `t≤T` 使 `¬H(t)`。新谓词为真。若 refinement 在首个 Paint 之后才完成，`H(t₀)` 即使为真也不影响新谓词。故所有满足 P1–P3 且满足产品合同 `L` 的合法事件顺序均不会因“任务恰好更快完成”而被拒绝。

### 4.3 健全性（不会接受完整滚动重绘）

若滚动动作造成完整 viewport 重绘，则由 P1，首个捕获事件 `e₀` 的面积满足 `A(e₀)=V`，从而：

```text
A(e₀) / V = 1 > 0.05
```

新谓词必为假。后续 refinement 的面积不参与该不等式，因此既不会把合法局部滚动误判为完整重绘，也不会把后续异步 Paint 错算到滚动原因上。新谓词因此对 C2 的 5% 合同是健全的。

### 4.4 唯一性

在 C1–C5 的候选空间中，立即断言 `H(t₀)` 必须被移除：由 4.2 的反例，它是一个非必要且可为假的附加合取项；保留它必然违反 C1。

`maxᵢ A(eᵢ)` 也不能用于验收：由同一反例它等于 refinement 的完整 viewport 面积；改用 `min`、平均值或放宽阈值则不再验证“首个滚动 Paint”，违反 C2/C3。根据 P1，唯一与滚动原因保持同一因果位置的面积观测是 `A(e₀)`。

因此，在“不改变产品、不改变阈值、不重试、不引入固定 sleep”的候选空间里，必须同时满足：

```text
去掉 H(t₀) 这个瞬时必要条件
且把面积判定绑定到 first(recordedAreas)
且保留 bounded wait 与 teardown wait
```

这三项分别由完备性、健全性和 C4 的资源生命周期要求强制推出；删除/替换任一项都会违反至少一个约束。因此该方案在该约束空间内唯一。

## 5. 实施与验收映射

实施改动集中在 `tests/tst_qviewtests.cpp`：

1. 移除滚动后立即的 `QVERIFY(view->hasPendingVectorRefinement())`；
2. 继续用 `QTRY_VERIFY_WITH_TIMEOUT(!recorder.recordedAreas().isEmpty(), 1000)` 等待可见输出；
3. 继续用 `QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000)` 等待安全 teardown；
4. 继续用 `recordedAreas().constFirst()` 验证 5%，将后续最大面积仅作为诊断输出。

对应的原子测试为 `CI-UNIT-002`，并由完整 `FovelleTests` 的集成路径再次覆盖。静态合同应断言：测试不再含有滚动后的立即 pending 状态断言，且仍保留首个 Paint 与后续 refinement 的因果分离。

验证顺序固定为：

```text
static → unit → integration → system
```

每层记录命令、返回码、耗时、输出摘要和输入 SHA-256；三份要求的 JSON 报告分别写入：

* `reports/test_case_specification.json`
* `reports/test_completion_report.json`
* `reports/code_quality_assessment_report.json`

GitHub hosted runner 的选择和日志/产物保存属于 workflow 合同；产物上传使用 GitHub Actions 的官方 artifact 机制：[Store and share data](https://docs.github.com/en/actions/tutorials/store-and-share-data) 与 [Workflow artifacts](https://docs.github.com/en/actions/concepts/workflows-and-actions/workflow-artifacts)。

