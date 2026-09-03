# 图片缩放时垂直滚动条瞬时震荡：结构化测试用例规格

> 文档日期：2026-09-03
>
> 适用基线：Fovelle `79712ffb3298403cae55d81f37ec3bfd23862140`、Qt 6 Cocoa
>
> 问题：缩放图片时，肉眼观察到垂直滚动条位置发生瞬时震荡。

## 1. 结论与验收口径

该现象不能只用“缩放结束后滚动条位置正确”验收。滚动条的“位置”至少可能指四个不同对象：滚动值、滑块在轨道中的比例位置、滚动条控件的屏幕几何位置，以及图片内容相对视口的位置。四者必须分别观测，才能避免假通过。

本问题的总验收条件定义为：

```text
AC-ZOOM-VBAR-TRANSIENT
  = AC-ZOOM-VBAR-VALUE
  ∧ AC-ZOOM-VBAR-GEOMETRY
  ∧ AC-ZOOM-VBAR-ANCHOR
  ∧ AC-ZOOM-VBAR-THUMB
  ∧ AC-ZOOM-VBAR-ASYNC
  ∧ AC-ZOOM-VBAR-MATRIX
```

一次缩放事务从真实输入被接受开始，到动画停止、所有相关延迟写入者失活且连续两个事件循环回合无状态变化为止。事务内任何已绘制或可提交的错误中间态都算失败；即使轨迹最终从 `A → B → A` 回到正确位置，中间的 `B` 仍是用户可见缺陷。

判定不采用“滚动值必须保持不变”或“滑块必须单调移动”这类过强规则。缩放本来就可能合法地改变滚动范围和值；正确规则是：每个时刻的实际状态必须与该时刻显示帧、当前 viewport 和同一图片内容锚点共同推导出的独立几何预言一致。

## 2. 问题分解与证据缺口

### 2.1 可区分的故障形态

| 假设 | 肉眼现象的真实含义 | 最小观测量 | 可证伪条件 |
| --- | --- | --- | --- |
| H1 数值跳变 | `verticalScrollBar()->value()` 被错误范围夹紧或被旧回调重写 | `minimum/maximum/value/pageStep` 的带时序轨迹 | value 始终等于独立推导值 |
| H2 比例位置跳变 | value 未必错，但 range/pageStep 变化使 thumb 位置或长度跳变 | 平台 style 推导的 actual/expected thumb rect | thumb 每帧与当前 range、pageStep 和期望 value 一致 |
| H3 控件几何跳变 | bar 或私有 container 的 `top/x/width` 被布局暂时搬动 | bar/container 的 global rect 与 Move/Resize/LayoutRequest | 固定窗口内物理顶边和横向几何始终不变 |
| H4 内容锚点跳变 | 滚动条看似合理，但同一图片点离开缩放目标 | 归一化图片点 UV、scene rect、`mapFromScene()` | 锚点误差始终在容差内 |
| H5 跨轴阈值震荡 | H bar 出现/消失改变 viewport 高度，继而使 V range/value 重算 | 两轴 range、viewport size、bar extent | H 只按目标方向跨阈值，V 轨迹仍通过独立预言 |
| H6 延迟回弹 | 动画后 0/50/350/500 ms 写入者覆盖正确状态或用户新意图 | animation/timer phase、generation、terminal tuple | 所有写入者运行后终态不变，手动 pan 不被覆盖 |
| H7 backing 坐标基变化 | Expensive scaling 换 pixmap 后仍使用旧 scene 绝对坐标 | 替换前后 image rect、anchor UV、anchor scene | 同一 UV 在新 rect 中重建后仍映射到目标 viewport 点 |

### 2.2 现场仍缺失的信息

用户描述足以设计判别测试，但不足以唯一归因。以下缺口不阻止测试设计，而是转化为数据矩阵或失败证据字段：

| 证据缺口 | 测试中的处理 |
| --- | --- |
| “位置”指 value、thumb、bar 本体还是内容 | 四个 oracle 同步采样，禁止用单一截图替代 |
| 缩放入口未知 | 覆盖键盘、滚轮和原生 pinch |
| 放大/缩小、锚点和起始滚动位置未知 | 覆盖双方向、中心/非中心锚点，并使预期值远离端点 |
| 是否跨越 `AsNeeded` 阈值未知 | 构造 V 始终溢出、H 在 1.00↔1.25 间切换的夹具 |
| 平滑缩放模式及 backing 替换未知 | 覆盖 Disabled 与 Expensive；后者必须观察 pixmap 替换回调 |
| DPR、主题和 macOS overlay scrollbar 设置未知 | 核心门禁独立运行 DPR 1/2；以非零 range 判断可滚动性，不依赖 overlay 的可见性 |
| 抖动发生在动画中还是动画后未知 | 同时记录每个动画毫秒、paint/layout 事件和所有延迟 timeout |

若现场仍可复现，失败工件必须携带上述字段，随后才能把 H1～H7 中的某一项提升为现场根因。

## 3. 联网检索、多跳证据与交叉验证

### 3.1 检索路径

检索从症状分解而不是宽泛关键词出发：

```text
肉眼看到滚动条震荡
→ 究竟是 value、range/thumb、bar geometry，还是内容移动
→ scene rect、transform、viewport 如何决定 range/value
→ AsNeeded 的一个轴是否会改变另一个轴
→ 动画是否存在中间显示值
→ 0 ms 与其他延迟回调是否有稳定顺序
→ backing pixmap 与 DPR 是否会改变 scene 坐标基
→ 应当在哪些时刻采样、用什么独立 oracle 判定
```

### 3.2 已核验的外部事实

以下仅采用 Qt 官方文档和 Qt 官方源码作为技术事实来源；访问日期均为 2026-09-03。

| 编号 | 经核验事实 | 来源 | 对测试设计的约束 |
| --- | --- | --- | --- |
| F1 | `ScrollBarAsNeeded` 在 range 非零时提供滚动条；滚动条隐藏时 viewport 扩张，重新出现时 viewport 收缩 | [Qt：QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html#details) | 必须覆盖跨阈值，并同步记录两轴与 viewport |
| F2 | `sceneRect` 是 QGraphicsView 可通过滚动条导航的范围；scene transform 会把 scene 单位映射到 view | [Qt：QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop)、[setTransform](https://doc.qt.io/qt-6/qgraphicsview.html#setTransform) | 期望 range/value 必须来自当前 scene rect 与当前显示 transform |
| F3 | Qt 的 `recalculateContentSize()` 先用 `PM_ScrollBarExtent` 判断两轴需求，再显式处理“一轴出现导致另一轴也需要”的交叉影响，最后设置整数 range/pageStep | [Qt 6.11.1 源码：qgraphicsview.cpp](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp#L2714-L2893) | H bar 切换是 V 轨迹变化的合法输入，但不能造成错误中间态 |
| F4 | `QAbstractSlider::value` 是有界整数；修改 range 时，当前 value 会被调整进新范围 | [Qt：QAbstractSlider](https://doc.qt.io/qt-6/qabstractslider.html#details) | 只看最终 value 会漏掉“先被临时 range 夹紧、后恢复”的瞬态 |
| F5 | 滑块长度通常取决于 pageStep；平台实际 thumb rect 应由 style 的 `SC_ScrollBarSlider`/`subControlRect()` 求得 | [Qt：QScrollBar](https://doc.qt.io/qt-6/qscrollbar.html#details)、[Qt：QStyle](https://doc.qt.io/qt-6/qstyle.html#subControlRect) | thumb oracle 不能硬编码像素长度或只比较 value |
| F6 | `PM_ScrollBarExtent` 是 V bar 的宽度和 H bar 的高度 | [Qt：QStyle::PixelMetric](https://doc.qt.io/qt-6/qstyle.html#PixelMetric-enum) | 初始平台占位 geometry 不能作为基线；先等待 style extent 生效 |
| F7 | `QPropertyAnimation` 在运行期间持续把中间值写入目标属性，插值进度受 easing curve 影响 | [Qt：QPropertyAnimation](https://doc.qt.io/qt-6/qpropertyanimation.html)、[Qt：QVariantAnimation](https://doc.qt.io/qt-6/qvariantanimation.html#details) | 不能只测起点和终点；要扫描整个动画时间域 |
| F8 | 0 ms `QTimer` 会尽快触发，但它与其他事件源之间的顺序未规定 | [Qt：QTimer](https://doc.qt.io/qt-6/qtimer.html#details) | 确定性扫描之外，必须另做不暂停 timer 的真实 event-loop 回放 |
| F9 | Qt Widget/event geometry 使用 DIP，而 QImage/QPixmap 持有原始像素；`QT_SCALE_FACTOR=2` 是官方高 DPI 测试方法 | [Qt：High DPI](https://doc.qt.io/qt-6/highdpi.html#conceptual-model)、[Qt：QPixmap::deviceIndependentSize](https://doc.qt.io/qt-6/qpixmap.html#deviceIndependentSize) | oracle 统一以 DIP 判定，并在独立 HiDPI 进程复跑 |
| F10 | macOS 的 ZoomNativeGesture 是增量缩放，合法序列为 Begin→一个或多个 Zoom→End | [Qt：QNativeGestureEvent](https://doc.qt.io/qt-6/qnativegestureevent.html#details) | pinch 用例必须发送完整原生手势序列，而非直接调用 zoom 函数 |

### 3.3 项目内证据

| 编号 | 当前实现事实 | 可核验位置 | 测试含义 |
| --- | --- | --- | --- |
| P1 | 两轴均为 `ScrollBarAsNeeded`，transform anchor 为 `NoAnchor` | [qvgraphicsview.cpp L24-L30](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L24-L30) | Fovelle 自己的 pending anchor 是主要锚点事务 |
| P2 | V bar 的 titlebar-safe geometry 由 0 ms single-shot timer 回写 | [qvgraphicsview.cpp L34-L59](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L34-L59)、[L3047-L3090](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L3047-L3090) | 必须直接测 bar/container global geometry 和 timeout |
| P3 | 缩放动画为 200 ms `OutCubic`，逻辑 zoom 与显示 zoom 分离 | [qvgraphicsview.cpp L93-L101](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L93-L101)、[L1825-L1867](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L1825-L1867) | range oracle 必须使用 displayed frame，不得偷用最终 zoom |
| P4 | Expensive scaling 的初始 timer 为 50 ms；动画中会推迟；换 backing 后用 UV 重建 pending anchor | [qvgraphicsview.cpp L87-L91](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L87-L91)、[L1947-L2001](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L1947-L2001) | Expensive 行必须跨过 backing 替换，并核对同一 UV |
| P5 | anchor settle 为 `200+150=350 ms`，constraint 默认 500 ms | [qvgraphicsview.cpp L111-L131](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L111-L131)、[L3451-L3453](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L3451-L3453) | 稳态判定必须晚于两个回调实际执行，而非固定 sleep 后猜测 |
| P6 | 当前 scroll content rect 使用显示帧，并按 pending anchor margin 扩展 | [qvgraphicsview.cpp L69-L80](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L69-L80)、[L2489-L2570](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L2489-L2570) | 静态测试要阻止“最终帧 range”或虚构 margin 回归 |
| P7 | anchor 恢复最终通过两轴 `setValue()` 写回；用户 pan 会取消 pending anchor | [qvgraphicsview.cpp L3224-L3275](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/src/qvgraphicsview.cpp#L3224-L3275) | 要测旧意图是否在延迟时刻覆盖新 pan |
| P8 | 专项 QtTest 已注册普通 DPR 与 `QT_SCALE_FACTOR=2` 两个 CTest 入口 | [tests/CMakeLists.txt L165-L178](https://github.com/inostarlin-passion/Fovelle/blob/79712ffb3298403cae55d81f37ec3bfd23862140/tests/CMakeLists.txt#L165-L178) | HiDPI 必须是独立进程，不在同一进程临时切 DPR |

### 3.4 交叉验证约束

- Qt 文档用于确认 API 合同，Qt 6.11.1 源码用于确认跨轴重算顺序，项目源码用于确认实际写入者；任何结论不依赖单一注释。
- 内部状态与视觉结果交叉验证：value/range、style thumb、bar/container global rect、图片 anchor 和事件循环后抓帧同时留证。
- actual 与 expected 必须数据独立。特别是 `V*` 不得用 actual value 反推，expected thumb 不得复用 actual sliderPosition。
- 每个主要假设均有可证伪条件。失败报告先指出是哪一个 oracle 在什么 phase 首次失效，不直接把相关性写成根因。
- 确定性逐毫秒扫描用于穷举动画状态；真实回放用于覆盖 Qt 明确声明为无固定顺序的 timer/layout/paint 交错。两者必须都通过。

## 4. 显式前提与链式推理

### 4.1 显式前提

1. 当前目标平台为 README 声明的 macOS 15+，窗口后端为 Qt 6 Cocoa。
2. 核心夹具固定窗口为 640×480 DIP；测试自身不在缩放事务中主动 resize 窗口。
3. V 轴在全部可判定 sample 中保持非零 range，期望值距离最小/最大值至少 2 个滚动单位，避免端点 clamp 掩盖错误。
4. H 轴刻意在 1.00 与 1.25 之间跨越 `AsNeeded` 阈值；这个切换是输入条件，不是自动判错条件。
5. 键盘缩放以 usable viewport center 为锚点；wheel/pinch 以图片内 `(u=0.40,v=0.35)` 的非中心点为锚点。
6. 几何、事件和容差统一使用 DIP；原始截图另记录 DPR。
7. 允许 Qt 整数 scrollbar 取整造成 1 DIP value/thumb 偏差，允许映射与多次取整累计造成 2 DIP anchor 偏差；不允许用更大容差吞掉肉眼可见位移。
8. 测试通过只证明所列矩阵，不外推为所有图片格式、Qt 版本和辅助功能输入都不可能复现。

### 4.2 独立几何预言

在 sample `k`，设当前图片 scene rect 为 `Rk=(Lk,Tk,Wk,Hk)`，固定图片内容点为 `UV=(u,v)`，则同一内容点为：

```text
Pk = (Lk + u·Wk, Tk + v·Hk)
```

设当前显示 transform 为 `Tk`，目标 viewport 点为 `qk`。在 V 持续溢出、`topIndent=0` 的夹具前提下，独立垂直值为：

```text
V*k = clamp(round((Tk(Pk)).y - qk.y), vMin, vMax)
```

同时定义：

```text
anchorError = mapFromScene(Pk) - qk
thumbExpected = style.subControlRect(SC_ScrollBarSlider,
                    option{range, pageStep, sliderPosition=V*k})
```

`V*k` 只读 scene/image/transform/target/range，不读 actual `vValue`；这使 value 与 thumb 检查不是同源自证。

### 4.3 推理链

1. 根据 F7 与 P3，200 ms 内存在多个真实显示 zoom，最终逻辑 zoom 不能代表所有中间帧。
2. 根据 F2、F3 与 P6，每个显示帧会通过 transform、scene rect 和 viewport 决定当时的两轴 range/pageStep。
3. 根据 F1、F3 与前提 4，H bar 跨阈值会改变 viewport 高度并触发 V range 的合法重算。
4. 根据 F4，任何错误的临时 range 都可能立刻夹紧 V value；稍后的正确 range/anchor 又会写回，于是形成肉眼所见 `A→B→A`。
5. 根据 P2、P4、P5、P7，动画以外还有 0/50/350/500 ms 写入边界；只测 200 ms 终点仍会漏检。
6. 根据 F5，thumb 由 value、range、pageStep 和平台 style 共同决定；value 正确不等于肉眼位置正确。
7. 根据 P4 与 F9，backing 替换可能改变图片 scene 尺寸；只有用 UV 重建 `Pk`，才能证明比较的是同一内容点。
8. 因此，充分的回归门禁必须同时覆盖逐帧 value、thumb、控件物理 geometry、内容 anchor，以及所有异步写入者后的稳定终态。

## 5. 覆盖模型

### 5.1 静态/动态与瞬态/稳态映射

| 用例 | 静态/动态 | 瞬态/稳态 | 主要关闭的证据缺口 |
| --- | --- | --- | --- |
| `TC-ZOOM-VBAR-NO-TRANSIENT-EXCURSION` | 动态系统/灰盒 | 瞬态 + 稳态 | 总体用户可见问题 |
| `TC-ZOOM-VBAR-VALUE-TRAJECTORY` | 动态灰盒 | 瞬态 + terminal | H1、H5 |
| `TC-ZOOM-VBAR-GEOMETRY-TRAJECTORY` | 动态灰盒/视觉 | 瞬态 + terminal | H3 |
| `TC-ZOOM-VBAR-ANCHOR-TRAJECTORY` | 动态灰盒 | 瞬态 + terminal | H4、H7 |
| `TC-ZOOM-VBAR-THUMB-TRAJECTORY` | 动态灰盒/视觉 | 瞬态 + terminal | H2 |
| `TC-ZOOM-VBAR-ASYNC-TERMINAL` | 动态集成 | timeout 边界 + 稳态 | H6、H7 |
| `TC-ZOOM-VBAR-INPUT-MATRIX` | 静态参数审计 + 动态数据驱动 | 两阶段 | 输入、方向、模式、DPR 缺口 |
| `TC-ZOOM-VBAR-STATIC-CONTRACT` | 静态 | 不适用运行时；约束长期稳态代码合同 | 防止关键修复与测试被删除 |

### 5.2 核心动态矩阵

核心门禁为 `3 输入源 × 2 方向 × 2 scaling mode = 12` 行；每行分别执行 deterministic scan 与 live replay，再在普通 DPR 和 `QT_SCALE_FACTOR=2` 两个进程运行。

| 维度 | 等价类 |
| --- | --- |
| 输入 | keyboard、wheel、native pinch |
| 方向 | 1.00→1.25、1.25→1.00 |
| 锚点 | keyboard=center；wheel/pinch=`UV(0.40,0.35)` |
| 平滑缩放 | Disabled、Expensive |
| 跨轴状态 | 放大时 H: off→on；缩小时 H: on→off；V 始终 on/range>0 |
| 像素密度 | normal、`QT_SCALE_FACTOR=2` |
| 调度方式 | 逐 1 ms 确定性扫描、真实 timer/event-loop 回放 |

扩展回归建议在核心门禁稳定后加入：浅色/深色主题、窗口化/全屏、RTL、平台 overlay scrollbar 的“滚动时显示/始终显示”、连续反向缩放、缩放后立即手动 pan，以及 JPEG/PNG、SVG/EPS、HDR/RAW 代表格式。扩展矩阵不能替代上述最小因果夹具。

## 6. 公共前置、采样与工件

### 6.1 公共前置条件

- 构建 `fovelle_tests`，使用 Qt 6 Cocoa；normal 与 `QT_SCALE_FACTOR=2` 分别启动进程。
- 创建可见 640×480 `MainWindow`，使用 `OriginalSize`、`cursorzoom=true`、25% 缩放步长、`ScrollBarAsNeeded`。
- 先用窄长探针实测 viewport，再生成宽约 `0.90×viewport`、高约 `2.20×viewport` 的 raster fixture，使 V 始终溢出、H 在 1.00/1.25 间跨阈值。
- baseline 前等待 V bar 摆脱平台初始占位 geometry，宽度与 `PM_ScrollBarExtent` 相差不超过 1 DIP，bar/container 已处于标题栏安全区。
- 所有 settings、shortcuts、quit policy 用 scoped guard 保存并恢复。

### 6.2 采样阶段

**确定性阶段：** 真实输入先启动生产动画；随后暂停 animation，停止 anchor/constraint/expensive 墙钟 timer，但保留并排空 0 ms geometry writer。对 `t=0..199 ms` 逐毫秒设置 animation currentTime，排空布局事件后采样。最后恢复并采 terminal。

**真实阶段：** 新建窗口、重新发送同一真实输入；不暂停 animation、不重排 timer。记录 animation value/state、Paint、Move、Resize、LayoutRequest、Show、两轴 range/value/action 和四个 timeout，直到动画停止、timer 全部 inactive 且连续两个 quiet turns。

### 6.3 失败工件

失败时保存：

- `trace.json`：phase、单调时钟、animation time、logical/displayed zoom、transform、scene/image/viewport/usable viewport、DPR；
- H/V 的 visible-by-range、min/max/value/pageStep；
- V bar 与 container global rect、actual/expected thumb rect；
- anchor UV/scene/target/actual/error；
- timer active 状态、首个失败 sample、最大偏差 sample；
- `first-bad-frame.png`、`worst-frame.png`、`terminal-frame.png`。

抓帧在产生 sample 的事件循环回合返回后执行，避免从 paint/event filter 内调用 `grab()` 导致重入并改变被测时序。

## TC-ZOOM-VBAR-NO-TRANSIENT-EXCURSION

### 测试目的

以一个端到端门禁验证六个原子标准的合取：真实缩放输入期间及异步结算后，不出现错误 value、thumb、bar/container 物理位移或图片锚点偏移。

### 前置条件

满足公共前置条件；当前数据行能够建立指定的初始 H 状态，V 全程具有非零 range，输入锚点位于 usable viewport 与图片内部。

### 输入数据

核心动态矩阵的 12 行；每行携带 `inputSource`、`zoomIn`、`scalingMode`、`anchorPolicy`，并在 normal/HiDPI 两进程各运行 deterministic/live 两阶段。

### 操作步骤

1. 建立夹具并记录稳定的 `pre-input` baseline。
2. 发送真实 keyboard、wheel 或完整 native pinch 输入；验证事件被接受、只产生一次逻辑 zoom change，并启动 200 ms 动画。
3. deterministic 阶段扫描 200 个整数毫秒；live 阶段保留自然调度直到 timer-quiet。
4. 每个 checkable sample 同时执行 value、geometry、anchor、thumb oracle；最后比较 terminal tuple。
5. 一旦出现首个错误中间态立即标记失败，但继续保留 worst 与 terminal 工件以便诊断。

### 预期结果

六个原子标准全部通过；不存在可绘制的 `A→B→A` 错误中间态；最终 displayed zoom 等于 logical zoom，所有相关 timer 失活且 terminal tuple 不再变化。

### 后置条件

关闭窗口、移除 event filter、恢复 settings/shortcuts/quit policy；失败时保留完整 trace 与三张帧图，成功时不改用户配置。

## TC-ZOOM-VBAR-VALUE-TRAJECTORY

### 测试目的

验证 V value 在每个显示帧均与独立几何预言一致，捕获 future range、旧 scene 坐标或延迟 callback 造成的错误写入。

### 前置条件

满足公共前置条件；V 在所有 checkable sample 中可滚动，`V*` 距两端至少 2 单位；不使用 actual value 构造 expected。

### 输入数据

每个 sample 的 current image rect、anchor UV、displayed transform、anchor target、`vMin/vMax`、actual `vValue`、phase 与 animation time。

### 操作步骤

1. 从当前 image rect 与固定 UV 重建 `Pk`。
2. 按第 4.2 节公式计算并 clamp `V*k`。
3. 在每个 `manual-time-*`、paint/layout committed sample、backing 替换和 timer timeout 后比较 actual 与 expected。
4. 分别记录首个偏差和最大偏差，不因后续恢复而清除失败。

### 预期结果

所有 checkable sample 满足 `abs(vValue - V*) <= 1 DIP`；range 只随当前显示帧和合法 H 阈值变化，不曾把 value 夹到未来帧范围。

### 后置条件

释放 probe 并恢复公共环境；失败 trace 指明 phase、actual/expected、range、displayed/logical zoom 和是否接近端点。

## TC-ZOOM-VBAR-GEOMETRY-TRAJECTORY

### 测试目的

捕获 value/range 正确但 V bar 或其私有 container 被暂时搬动的视觉震荡，尤其是 `safeTop→viewportTop→safeTop`。

### 前置条件

满足公共前置条件；窗口位置和尺寸固定；V bar/container 已达到平台 style extent 与 titlebar-safe 位置后才建立 baseline。

### 输入数据

每个 checkable sample 的 V bar/container global rect、viewport global rect、obscuredHeight、H 状态、Move/Resize/LayoutRequest/paint phase。

### 操作步骤

1. 在 `pre-input` 保存 bar 与 container 的 global `x/top/width`。
2. 给 bar、container、view 和 viewport 安装 event filter，记录 Move、Resize、Show、LayoutRequest 与 Paint。
3. 每次事件循环排空后比较当前 global rect 与 baseline。
4. H 合法出现/消失时允许 bar/container 的 height/bottom 改变，但继续约束 `x/top/width`。
5. 若同轮布局后的可绘制状态暴露错误顶边，立即记录 first-bad frame。

### 预期结果

固定窗口内 bar/container 的 `x/top/width` 始终等于 baseline；top 不回到被标题栏遮挡的 viewport 原点；不存在先移动后恢复的可见帧。

### 后置条件

移除 event filter；失败时保存触发对象、事件类型、前后 global rect、obscuredHeight 和对应帧。

## TC-ZOOM-VBAR-ANCHOR-TRAJECTORY

### 测试目的

证明缩放动画、跨轴 viewport resize 与 Expensive backing pixmap 替换期间，同一图片内容点持续停留在约定 viewport 目标附近。

### 前置条件

满足公共前置条件；keyboard 行使用每帧 usable viewport center，wheel/pinch 行使用固定非中心图片点；anchor UV 位于 `[0,1]²`。

### 输入数据

旧/新 image scene rect、anchor UV、当前 transform、目标 viewport point、`mapFromScene(Pk)` 的实际结果、scaling mode 与 callback phase。

### 操作步骤

1. 每个 sample 都从当前 image rect 与 UV 重建相同内容点，不沿用 backing 替换前的绝对 scene 坐标。
2. 计算 `anchorActual=mapFromScene(Pk)` 并与 center 或固定输入点比较 X/Y。
3. 覆盖动画 0..199 ms、H threshold resize、animation finish、Expensive pixmap replacement、anchor settle 与 constraint timeout。
4. Expensive 行另记录替换前后 rect 与 UV，证明坐标重基准确实发生。

### 预期结果

所有 checkable sample 满足 `abs(anchorErrorX) <= 2 DIP` 且 `abs(anchorErrorY) <= 2 DIP`；backing 替换前后代表同一 UV，不出现内容跳帧。

### 后置条件

关闭窗口并恢复设置；失败时同时保存旧/新 rect、UV、scene point、target/actual 和对应 screenshot。

## TC-ZOOM-VBAR-THUMB-TRAJECTORY

### 测试目的

验证肉眼看到的 V thumb 位置和长度始终与正确 value、当前 range/pageStep 及平台 style 一致，关闭“只检查 value”的盲区。

### 前置条件

满足公共前置条件；V bar 已采用当前平台 `PM_ScrollBarExtent`，style 可通过 `QStyleOptionSlider` 计算 `SC_ScrollBarSlider`。

### 输入数据

orientation、minimum、maximum、pageStep、actual sliderPosition、独立 `V*`、bar geometry，以及 actual/expected thumb rect。

### 操作步骤

1. 用 actual sliderPosition 调用当前 style 的 `subControlRect()` 得到 actual thumb。
2. 保持相同 range/pageStep/style，仅把 sliderPosition 替换为独立 `V*`，得到 expected thumb。
3. 比较 top、centerY、bottom 与 height，并核对 H 只按目标方向跨阈值。
4. 在每个 paint/committed sample 与 terminal 重复判定。

### 预期结果

actual/expected thumb 的 top、centerY、bottom 最大差均不超过 1 DIP，height 与当前 pageStep 一致；H 不发生反向回弹，V 全程保持可滚动。

### 后置条件

释放 style option/probe；失败时记录 actual/expected thumb、range/pageStep/value、bar rect 和当前平台 style 名称。

## TC-ZOOM-VBAR-ASYNC-TERMINAL

### 测试目的

验证 0 ms geometry writer、50 ms Expensive scaling、350 ms anchor settle、500 ms constraint 等延迟边界不会在动画后回弹，也不会覆盖缩放后立即发生的用户 pan。

### 前置条件

满足公共前置条件；live 阶段使用真实 event loop，不以固定 sleep 作为成功依据；手动 pan 分支保证新位置不在端点容差内。

### 输入数据

`animation-finished`、`verticalScrollBarGeometryTimer-timeout`、`expensiveScaleTimer-timeout`、`zoomAnchorSettleTimer-timeout`、`constrainBoundsTimer-timeout`，以及 terminal 的 zoom/scene/viewport/H/V/bar/thumb/anchor tuple；另含一次缩放后立即 slider move/pan。

### 操作步骤

1. 正常分支发送真实缩放，记录所有 named timer timeout；等待 animation stopped、timer inactive 和连续两个 quiet turns。
2. 记录 `live-terminal-1`，再排空事件并记录 `live-terminal-2`。
3. 用户覆盖分支在 settle/constraint 到期前执行真实 scrollbar/pan 输入，记录 generation 与新 value。
4. 继续运行至原 350/500 ms 边界之后，核对旧 pending anchor 已取消且新 pan 未被写回。
5. Expensive 行必须实际观察到 backing replacement timeout；Disabled 行不得错误等待该事件。

### 预期结果

正常分支的必要 timeout 均被观察且最终 inactive，两个 terminal tuple 完全一致；用户覆盖分支保持用户新 value/edge，旧 generation 的回调不能重定位视口。

### 后置条件

停止并销毁测试窗口中的 timer/probe 连接，恢复设置；保存 timeout 顺序、generation、terminal tuple 和失败帧。

## TC-ZOOM-VBAR-INPUT-MATRIX

### 测试目的

证明回归测试真正进入各生产输入路径、两个缩放方向、中心/非中心 anchor、两种 scaling path 和两个 DPR，而非只靠直接调用内部函数得到假绿。

### 前置条件

满足公共前置条件；CTest 已为 normal 与 HiDPI 注册独立进程；keyboard shortcut、wheel action 和 native gesture 接收路径可用。

### 输入数据

| 输入源 | 事件流 | 锚点 | 方向 | scaling |
| --- | --- | --- | --- | --- |
| keyboard | `QTest::keySequence()` 使用 action 当前 shortcut | usable viewport center | in/out | Disabled/Expensive |
| wheel | 实际 `QWheelEvent` 发到 viewport | 图片 `UV(0.40,0.35)` | in/out | Disabled/Expensive |
| pinch | Begin→Zoom→End `QNativeGestureEvent` | 图片 `UV(0.40,0.35)` | in/out | Disabled/Expensive |

### 操作步骤

1. 数据函数生成 12 行，逐行建立对应 1.00/1.25 初态。
2. 通过 widget/event dispatch 发送真实事件，不直接调用 `QAction::trigger()` 或只调用 `zoomAbsolute()`。
3. 检查事件 accepted、恰好一次 `zoomLevelChanged`、目标 zoom 正确且动画确有非端点帧。
4. 每行执行 deterministic/live 两阶段，并在 `FovelleZoomScrollbarTrajectory` 与 `FovelleZoomScrollbarTrajectoryHiDpi` 重复。
5. 审计结果清单，禁止因夹具未满足 range/layout 条件而静默 skip。

### 预期结果

12 行在两个进程均实际执行；每行观察到所需 input、range/layout/resize/paint/timer phase，并通过所有原子 oracle；共计 48 个“数据行×调度阶段×DPR”核心执行单元全部 PASS。

### 后置条件

恢复快捷键、settings 和进程环境；输出逐行结果与覆盖清单，任一行未执行或 skip 均使矩阵失败。

## TC-ZOOM-VBAR-STATIC-CONTRACT

### 测试目的

以不启动 GUI 的静态检查防止生产不变量、测试入口、独立 oracle、异步可观测性和本规格的六字段结构被后续修改删除。

### 前置条件

仓库源码、QtTest 源码、CTest 配置和本 Markdown 文件可读；基线 commit 已记录；不要求窗口系统或图片 fixture。

### 输入数据

`src/qvgraphicsview.cpp/.h`、`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`、`tests/scrollbar_zoom_acceptance_static.py` 和本文件。

### 操作步骤

1. 检查两轴 `AsNeeded`、`NoAnchor`、displayed-frame content rect、pending anchor 与 generation/cancel 合同。
2. 检查 200 ms 动画、0/50/350/500 ms named writer、Expensive UV rebase 和平台 scrollbar extent 合同。
3. 检查真实 keyboard/wheel/pinch、非中心 anchor、逐毫秒 scan、live replay、bar/container geometry、独立 value/style-thumb oracle、失败工件和 normal/HiDPI CTest 入口。
4. 检查本文件八个核心用例均含测试目的、前置条件、输入数据、操作步骤、预期结果、后置条件。
5. 运行：

```bash
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . \
  --output build/test-results/scrollbar-zoom-acceptance-static.json
```

### 预期结果

进程返回 0，全部静态 checks 为 `pass=true`；输出 JSON 可定位每个缺失 marker/合同；静态 PASS 只证明门禁存在，不能代替动态轨迹 PASS。

### 后置条件

仅生成 `build/test-results/scrollbar-zoom-acceptance-static.json`，不修改生产源码、用户设置或系统偏好。

## 7. 执行门禁与可核验结论

建议按以下顺序执行：

```bash
cmake --build build --target fovelle_tests --parallel

python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . \
  --output build/test-results/scrollbar-zoom-acceptance-static.json

ctest --test-dir build \
  -R 'FovelleZoomScrollbarTrajectory(HiDpi)?$' \
  --output-on-failure
```

只有同时满足以下条件，才可形成“该矩阵内未观察到垂直滚动条瞬时震荡”的可核验结论：

1. 静态合同 PASS；
2. normal 与 HiDPI 的 12 行、deterministic/live 两阶段全部执行且 PASS；
3. 每个 checkable sample 的四个独立 oracle 均 PASS；
4. 所有适用 timeout 已实际发生，terminal tuple 在 quiet turns 后不变；
5. 没有 skip、fatal warning 或缺失失败工件。

该结论的边界必须随结果一并陈述：它证明的是本文件所列平台、输入、缩放方向、scaling mode、DPR 和阈值夹具，不是对所有 Qt 版本、图片管线和系统输入组合的无限外推。
