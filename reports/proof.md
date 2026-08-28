# GitHub Actions 全屏测试失败：数学证明

## 1. 待证命题

已知远端运行 `33207073862` 在
`testFullscreenAfterOverflowRemovesTitlebarScenePadding` 中得到
`zoomFirstImageTop=-818` 而期望为 `zoomFirstViewportTop=0`。失败链为：

\[
\text{begin 保存旧锚点}
\to \text{全屏 scene/viewport 重建}
\to \text{外部 setValue(minimum)}
\to \text{旧锚点再次 restore}.
\]

要证明的命题是：在保持器活动期间，任意非内部滚动条变化都会成为最新平移
状态；之后的全屏几何重建不能覆盖该状态；因此该测试以及真实用户在过渡期间的
滚动都满足预期。

## 2. 引理一：内部更新不会被误认为用户平移

定义布尔量 \(q\)：进入 scene rect 重建、resize、约束和保持器恢复的代码区间时
设 \(q=1\)，区间结束后恢复原值。滚动条 `valueChanged` 的全屏处理条件为

\[
 active\land q=0\land \neg isUpdatingSceneRect.
\]

因此：

1. `setSceneRect` 触发的范围重算以及 `updateSceneRect` 中的值恢复被
   `isUpdatingSceneRect` 或 \(q=1\) 排除；
2. `resizeEvent` 调用 Qt 基类和自身的内部端点修复时被 \(q=1\) 排除；
3. `fitOrConstrainImage`、延迟约束和 `restoreFullScreenPanPreservation` 的
   滚动条写入被 \(q=1\) 排除。

所以布局改变只会触发既定的 `restore(P,X')`，不会意外重捕获一个过渡中的
中间值。引理成立。

## 3. 引理二：外部滚动立即替换旧快照

设进入全屏时快照为

\[
 P_0=(e_x^0,e_y^0,a^0).
\]

若保持器活动期间发生外部滚动，Qt 的 `QScrollBar` 值改变并发出
`valueChanged`；由模型中的条件，该回调立即执行 `capture`，得到

\[
 P_1=(E(v_x^1,R_x^1),E(v_y^1,R_y^1),a^1).
\]

回调是同一 GUI 线程中的直接状态更新，所以在下一个布局恢复调用前，保存状态
已经是 \(P_1\)，不再是 \(P_0\)。若之后没有新的外部滚动，则归纳可得所有后续
恢复均使用最后一次外部状态。引理成立。

特别地，测试直接执行
\(v_y^1=m_y^1\) 时，`valueChanged` 同样成立；这由
[Qt QAbstractSlider 的 `valueChanged` 语义](https://doc.qt.io/qt-6/qabstractslider.html)
保证，而不要求测试伪造鼠标事件。

## 4. 引理三：恢复只在新范围内表示状态

设 scene rect 从 \(S\) 变为 \(S'\)，滚动范围从 \(R_i\) 变为
\(R_i'=[m_i',M_i']\)。若最新意图为端点，则实现写入

\[
 v_i'=m_i'\quad\text{或}\quad v_i'=M_i'.
\]

若最新意图为内部位置，则实现以
\(a=mapToScene_{T}(c(V))\) 为连续坐标，在新变换/新视口中反求滚动条值；
Qt 只允许

\[
 m_i'\le v_i'\le M_i'.
\]

所以旧窗口的整数值和旧范围端点都不会被直接写入新窗口。引理成立。

## 5. 引理四：失败用例中的新最小值不会再被旧锚点覆盖

测试在全屏几何稳定后写入

\[
 v_y=m_y.
\]

由引理二，该写入使垂直意图变为
\(e_y=\mathsf{min}\)，并使保存锚点基于写入后的当前视口重新捕获。任意后续
`restore` 首先遵守端点优先规则，故写入的新范围最小值：

\[
 v_y'=m_y,
\]

而不会再使用 begin 阶段的旧中心锚点把图像向上平移约 818 像素。由测试的
场景几何，最小值对应图像顶边与视口顶边重合，即

\[
 zoomFirstImageTop=zoomFirstViewportTop=0.
\]

这正排除了远端日志中的反例。引理成立。

## 6. 引理五：连续内部平移和端点语义均保持

若外部值不在 \(\tau=3\) 的端点容差内，则
\(e_i=\mathsf{none}\)，保存的是 scene 锚点。对每一次新布局，恢复量为

\[
 d_i=mapFromScene_{T'}(a)_i-c(V')_i,
\]

并把 \(d_i\) 转换为滚动条增量。因此在取整误差范围内

\[
 mapToScene_{T'}(c(V'))=a.
\]

若外部值位于端点容差内，则端点优先，恢复到新范围对应端点；这不会把“近底但
未到底”的位置吸附到 maximum，因为其定义要求
\(v_i<M_i-\tau\)。故连续平移与端点语义都保持。引理成立。

## 7. 定理：修复满足 CI 失败用例及回归约束

由引理一，内部全屏布局变化不会污染快照；由引理二，任何外部滚动都会替换
旧快照；由引理三，恢复值始终属于新范围；由引理四，失败用例的
`setValue(minimum)` 最终保持为最小值且图像顶边为 0；由引理五，普通连续平移
和真实端点仍符合原有语义。因此全屏测试所断言的几何关系成立。

实现不改变 GitHub Actions 的构建命令、runner 选择、测试阈值或输入来源，只在
`QVGraphicsView` 的滚动条信号与内部几何写入之间增加状态隔离。因此已有的
编译、单元测试、clang-tidy 和格式检查仍是独立的回归谓词。

## 8. 验证与失败回退规则

按程序循环执行：

1. 静态检查确认新增字段、捕获函数和内部保护覆盖所有相关写入路径；
2. 动态执行目标 GraphicsView 测试，再执行完整 CTest；
3. 对源码运行 clang-tidy/格式检查，最后复核 git diff 与 CI workflow。

若任一谓词失败：目标测试失败则回退到引理二/四检查捕获时机；出现跳变则回退
到引理一补齐内部保护；出现越界则回退到引理三修正新范围恢复；静态或格式
失败则回退到代码实现步骤。不得通过延长固定等待、删除断言或降低几何阈值来
掩盖失败。
