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

## 6. 当前任务：App 启动/退出性能模型

本节只建模“启动耗时减少 50%”与“退出耗时减少 50%”，不改变前文已经完成的
全屏滚动状态模型。实现必须同时满足两类性质：性能不变量和原有用户可见行为
不变量。

### 6.1 对象、时间轴与观测量

令一次进程运行实例为

\[
 P=(A,E,W,M,B,S),
\]

其中：

- \(A\) 是 `QVApplication` 与其 Qt 基类的对象状态；
- \(E\) 是 Qt 事件循环；\(W\) 是窗口集合；
- \(M\) 是菜单及其 action clone 图；
- \(B\) 是后台任务集合（图像加载、Open With、矢量 tile 等）；
- \(S\) 是 QSettings/会话状态。

定义时间点：\(t_0\) 为进入 `main()` 的启动测量点，\(t_v\) 为首个窗口被
提交到系统并进入可见状态的时间，\(t_f\) 为首个 Qt 事件循环转折点，\(t_q\)
为收到退出请求的时间，\(t_x\) 为进程真正返回操作系统的时间。启动与退出指标
分别定义为

\[
 L_s=t_f-t_0,\qquad L_e=t_x-t_q.
\]

`FOVELLE_STARTUP phase=first-event-loop-turn` 是 \(L_s\) 的可复现代理；
`aboutToQuit` 只标记退出协议开始，不能冒充 \(L_e\)，因为 Qt 文档规定退出信号
发生在事件循环结束前。动态验证必须另外使用进程墙钟时间和退出阶段日志。

改造前的初始探索性基线（当前机器、禁用自动更新检查）为：

| 观测点 | 代表值 |
| --- | ---: |
| `QVApplication` 构造完成 | 中位数约 169 ms |
| 全局菜单栏准备完成 | 约 102 ms（一次观测） |
| `MainWindow` 构造完成 | 中位数约 52 ms（窗口内部） |
| 首个事件循环转折 | 中位数约 330 ms |

这些数值是本机基线，不是跨机器承诺。退出的空闲探针为约 0 ms，故它不能作为
“减半”的分母；退出验证必须选取确实拥有 \(B\neq\varnothing\) 的场景，并记录
完整的 \(t_q\) 到 \(t_x\) 墙钟区间。

### 6.2 状态、行为和约束

每个菜单资源 \(m\in M\) 有状态

\[
 \operatorname{state}(m)\in\{\text{absent},\text{pending},\text{ready}\},
\]

且窗口菜单有一次性状态变量 \(d_w\in\{0,1\}\)。每个后台任务
\(b\in B\) 有

\[
 \operatorname{state}(b)\in\{\text{queued},\text{running},\text{finished},\text{cleared}\}.
\]

允许的行为是：构造阶段创建 \(A\) 和首个窗口的最小显示状态；首个事件循环
之后把非关键菜单从 `pending` 推进到 `ready`；退出时先令应用进入 quitting
状态，再由每个窗口 owner 在 `aboutToQuit` 阶段清除尚未运行的非关键任务，最后仅
等待仍在运行且必须保持 GUI 生命周期的任务完成。禁止的行为是：重复创建菜单、在
GUI 对象销毁后访问其 future 结果、
用固定 `sleep` 或删除断言伪造性能、在退出时丢失已明确要求保存的会话状态。

方案必须满足以下约束：

1. 菜单最终完备：\(d_w=1\Rightarrow M_w\) 包含原 action library 的全部
   clone，且每个窗口只安装一次；
2. 快捷键语义不变：菜单建立后，原来绑定到窗口的 `Qt::WidgetShortcut` 与
   `virtualMenu` action 仍存在；菜单尚未建立时，首次相关输入必须能触发补建；
3. 生命周期安全：若任务仍运行，拥有它的 QObject 或底层 GUI 依赖必须活到任务
   终止；若任务尚未开始，则可清除而不产生 GUI 回调；
4. 最终窗口状态不变：普通新窗口最终仍是最大化状态，文件打开、会话恢复、窗口
   菜单和 Dock 菜单仍可用；
5. 退出保存语义不变：会话保存请求仍写入 `sessionstate`，非会话退出不新增同步
   磁盘写入。

### 6.3 选定方案与性能谓词

选定的实现分解为四个动作：

\[
 T=(T_1,T_2,T_3,T_4),
\]

其中：

- \(T_1\)：全局菜单栏改为惰性构建；getter 是唯一建造入口；
- \(T_2\)：窗口菜单和虚拟菜单在窗口首帧之后排队建立，并以 \(d_w\) 防重入；
- \(T_3\)：把由窗口/图像项拥有的 QtConcurrent 任务隔离到拥有者线程池，退出
  时清除 queued 任务，避免应用级全局池为别的对象承担等待；
- \(T_4\)：保持活跃 future 的明确等待边界，同时把退出测量扩展到真实进程返回。

给定同一硬件、同一构建、同一输入序列的改造前后中位数 \(L_s^0,L_s^1\) 与
\(L_e^0,L_e^1\)，验收谓词为

\[
 P_s: L_s^1\le \frac12L_s^0,
 \qquad
 P_e: L_e^1\le \frac12L_e^0.
\]

若系统/Qt 基类耗时本身超过 \(L_s^0/2\)，则只能证明“App 自有关键路径”达到
减半，不能把端到端进程指标谎报为达标；报告必须明确区分这两个量。若退出基线
为 0 或低于计时器分辨率，则 \(P_e\) 未定义，必须补充有后台任务的基准后再判定。

### 6.4 证据、测量与回退

方案依据的框架语义是：Qt 建议尽早创建 `QApplication`，`QCoreApplication::aboutToQuit`
发生在事件循环退出前；`QThreadPool::clear()` 只移除尚未开始的 runnable，
`waitForDone()` 等待线程结束；`QtConcurrent::run()` 的运行计算不能被可靠取消；
`QSettings` 通常由事件循环/析构阶段完成同步。对应的官方资料为
[QCoreApplication](https://doc.qt.io/qt-6/qcoreapplication.html)、
[QThreadPool](https://doc.qt.io/qt-6/qthreadpool.html)、
[QFutureWatcher](https://doc.qt.io/QT-6/qfuturewatcher.html) 和
[QSettings](https://doc.qt.io/qt-6/qsettings.html)。macOS 的关键路径原则由
[Apple 的启动耗时指南](https://developer.apple.com/documentation/xcode/reducing-your-app-s-launch-time)
给出：主线程启动阶段应尽量短，并延后非必要工作。

验证顺序固定为：源码静态契约 → 可重复构建 → 现有单元/集成测试 → 冷启动中位数
→ 有后台任务的退出中位数 → 行为回归。任一不变量失败时，回退到对应的
\(T_i\)；不得以增大固定等待、删除测试或降低验收阈值作为修复。

### 6.5 实测反馈（2026-08-29）

验证环境为 macOS 15.7.9 arm64、Qt 6.11.1、Release 构建；旧二进制为改造前
`build/Fovelle.app`，新二进制为 `build-fovelle-perf/Fovelle.app`。在相同环境变量
下任务专用静态契约 16/16 通过，并交错运行 7 对，取中位数：

| 指标 | 改造前 | 改造后 | 降幅 | 谓词 |
| --- | ---: | ---: | ---: | --- |
| \(L_s\)：首个事件循环转折 | 269 ms | 243 ms | 9.7% | \(P_s\)：失败 |
| \(L_e^{idle}\)：空闲退出请求到进程返回 | 69 ms | 64 ms | 7.2% | 非 \(P_e\) 验收样本 |

退出值由 `FOVELLE_SYSTEM_PROBE` 标记后的进程墙钟区间计算；探针前的固定 300 ms
等待没有计入 \(L_e\)。另外以 2.8 MiB 图片启动并立即退出运行 5 次，均输出
`windows=1 maximized=true`，返回时间为 252–267 ms，未出现悬空 future 或崩溃；
这组是生命周期 smoke test，因旧二进制没有零延迟探针，不能单独作为 \(P_e\) 的
对照分母。

因此本次实现通过了功能与生命周期验证，但严格的端到端 50% 目标尚未达到：启动
谓词明确失败，退出谓词因这组对照没有证明退出瞬间存在非空后台任务集合而不能
成立；空闲退出的 7.2% 只能作为 smoke observation。剩余关键路径主要由
Qt/macOS 基类初始化、原生窗口提交和进程退出协议构成；这些成本不能靠把首个标记
提前或跳过真实清理来伪造。按反馈规则，性能谓词失败应回到模型的关键路径分解和
系统级 profiling，而不是降低阈值；本报告不宣称两个 50% 目标已完成。
