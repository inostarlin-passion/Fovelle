# GitHub Actions 全屏测试失败：数学模型

## 1. 问题边界与确证事实

本文件对应远端构建 `33207073862`（提交 `8edbce0`）。该构建的编译、配置和
其它检查成功，失败发生在 `FovelleTests` 的
`GraphicsViewTests::testFullscreenAfterOverflowRemovesTitlebarScenePadding`：

\[
  zoomFirstImageTop=-818,\qquad zoomFirstViewportTop=0.
\]

所以问题不是 GitHub Actions runner 或编译器错误，而是全屏几何稳定后，测试
将垂直滚动条设置为新范围最小值，随后仍被旧的全屏连续锚点恢复。对应的运行记录
是 [Build Fovelle 33207073862](https://github.com/inostarlin-passion/Fovelle/actions/runs/33207073862)。
同一提交的 [Checks 33207073827](https://github.com/inostarlin-passion/Fovelle/actions/runs/33207073827)
已经通过，说明修复应聚焦于 `QVGraphicsView` 的行为而不是放宽 CI 条件。

显式前提：

1. Qt GUI 事件在单线程事件循环中处理；全屏原生动画可能在多个事件循环回合内
   产生 resize、scene-rect 和滚动条更新。
2. `QAbstractSlider::valueChanged` 在值发生变化时发出；`QScrollBar` 继承该
   机制，故程序设置滚动条和用户拖动最终都可以被同一状态入口观察。参见
   [Qt QAbstractSlider 文档](https://doc.qt.io/qt-6/qabstractslider.html)。
3. `QGraphicsView::setSceneRect` 会影响视图的可滚动范围；跨窗口几何不能直接
   复用旧的整数滚动条值，必须使用新范围重新表示。参见
   [Qt QGraphicsView 文档](https://doc.qt.io/qt-6/qgraphicsview.html)。
4. 修复不得删除或降低现有全屏、端点、原生输入和 CI 测试契约。

## 2. 几何与离散状态

设图像场景为 \(I\)，当前变换为 \(T\)，场景矩形为 \(S\)，可用视口矩形为
\(V\)。两个滚动轴分别有整数范围

\[
  R_i=[m_i,M_i]\cap\mathbb Z,\qquad v_i\in R_i,
  \quad i\in\{x,y\}.
\]

以可用视口中心 \(c(V)\) 表示连续平移状态：

\[
  a_i=\bigl(mapToScene_T(c(V))\bigr)_i.
\]

端点是比连续锚点更强的用户意图。沿每个轴定义端点容差 \(\tau=3\)：

\[
 E(v_i,R_i)=
 \begin{cases}
   \mathsf{min},&m_i<M_i\land v_i\le m_i+\tau,\\
   \mathsf{max},&m_i<M_i\land v_i\ge M_i-\tau,\\
   \mathsf{none},&\text{otherwise}.
 \end{cases}
\]

因此，“接近底部但仍有间隔”的值不是 `max`，不能在全屏后被吸附到新范围
的 `maximum()`。

保持状态定义为

\[
 P=(active,e_x,e_y,a),
\]

其中 `active` 表示全屏过渡保持器生命周期，\(e_i=E(v_i,R_i)\)，\(a\) 为
可选场景中心锚点。实现另有内部更新标志 \(q\)：

\[
 q=1\iff\text{当前滚动条变化由布局/场景重建/保持器恢复产生},
 \quad q=0\iff\text{当前变化是外部平移输入}.
\]

## 3. 状态转移

令 `capture(X)` 读取当前滚动条范围和值，并计算
\(e_x,e_y,a\)。令 `restore(P,X')` 在新几何 \(X'=(T',S',V',R')\) 中执行：

1. 若 \(e_i=\mathsf{min}\)，设置 \(v'_i=m'_i\)；若
   \(e_i=\mathsf{max}\)，设置 \(v'_i=M'_i\)；
2. 对 \(e_i=\mathsf{none}\) 的轴，使
   \(mapToScene_{T'}(c(V'))\) 回到保存的 \(a\)，并由 Qt 将结果夹紧到
   \(R'_i\)；
3. 所有这些写入都在 \(q=1\) 下执行。

事件转移如下：

| 事件 | 前置条件 | 转移 |
| --- | --- | --- |
| `begin` | 进入全屏请求边界 | 停止延迟约束/动画，`active=true`，执行 `capture`。重复 begin 不覆盖请求边界快照。 |
| `rebuild` | `active=true` | 在 `q=1` 下重建 scene rect、视口和滚动范围，再执行 `restore`。 |
| `external-scroll` | `active=true` 且 `q=0` | 滚动条发出 `valueChanged` 后立即执行 `capture`，使新用户状态替换旧锚点。 |
| `refresh` | 退出全屏请求边界 | 停止延迟约束/动画，丢弃旧端点和锚点，按当前全屏值执行 `capture`。 |
| `end` | 全屏回调完成或请求失败 | 在最终几何中 `restore`，然后清除 `active`、端点和锚点。 |

核心优先级是

\[
  \text{external-scroll} \succ \text{previous-anchor-restore}.
\]

也就是说，测试中的 `setValue(minimum)` 与真实用户滚动具有相同的状态语义：
一旦它不是内部写入，就必须成为后续恢复的依据。

## 4. 可执行输入、输出与验收谓词

测试输入包括：普通窗口状态、全屏状态、同一图像、缩放变换、全屏过渡事件，
以及在全屏几何稳定后写入的目标滚动条值。输出包括：最终普通窗口的 scene
矩形、视口映射、两个滚动条值和全屏保持器状态。

对失败用例，成功谓词为：

\[
\begin{aligned}
P_1&:\quad active\text{ 期间新外部滚动被捕获};\\
P_2&:\quad restore\text{ 不覆盖最近一次外部滚动};\\
P_3&:\quad \text{scene rect 重建后只使用 }R'\text{ 的合法值};\\
P_4&:\quad \text{在全屏溢出用例中，设置 }v_y=m_y\text{ 后图像顶边位于视口顶边};\\
P_5&:\quad \text{其它单元、clang-tidy、格式检查和构建步骤不回归}.
\end{aligned}
\]

其中 \(P_4\) 正是远端失败的反例：旧实现把 \(v_y=m_y\) 后的状态重新映射到
旧锚点，产生 `zoomFirstImageTop=-818`；修复后必须保持 `0`。

## 5. 约束与回退点

- 只修改状态同步和内部更新边界，不修改图像内容、缩放比例、全屏事件来源或
  测试的验收阈值。
- `setSceneRect`、resize 和保持器恢复属于内部更新；用户拖动、滚轮、键盘、
  scrollbar 值写入属于外部更新。若两者重叠，必须由显式内部保护标志决定，
  不能靠时序猜测。
- 若静态检查发现存在未保护的内部滚动条写入，回退到“内部更新保护”步骤；若
  动态测试仍出现旧锚点覆盖，回退到“external-scroll 捕获”步骤；若场景范围
  越界，回退到新范围恢复步骤。
- GitHub Actions 的 runner/Qt 版本可变，测试必须验证状态不变量而非依赖固定
  睡眠时间。GitHub 的 macOS runner 标签和版本由
  [GitHub-hosted runners 文档](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
  定义；本次远端失败已证明该环境会暴露本地未复现的事件时序。
