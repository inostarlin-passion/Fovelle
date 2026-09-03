# Fovelle 图片缩放、拖拽与滚动条结构化测试规格

> 文档日期：2026-09-03
>
> 被测代码基线：Fovelle `cf8597860cb2b9e6f34af65b0e1186dedb9af829`
>
> 本机核验环境：Qt 6.11.1、macOS 15.7.9、Cocoa QPA
>
> 文档性质：待实现的回归测试设计；不是“问题已经修复”的证明

## 1. 结论先行

五个问题不能由“最终缩放倍率正确”或一张终态截图充分验证。它们共同涉及四类独立状态：图片内容点、view/scene 坐标映射、两轴滚动范围，以及动画或延迟回调对这些状态的后续写入。建议把发布门禁定义为下列原子验收标准的合取：

```text
AC-ALL
  = AC-SB-NO-STALE-RANGE
  ∧ AC-DRAG-CONTINUOUS
  ∧ AC-DRAG-PRESERVES-OVERFLOW-BARS
  ∧ AC-KBD-ZOOM-CURSOR-ANCHOR
  ∧ AC-TOGGLE-DIRECTIONAL-ANCHOR
  ∧ AC-TOGGLE-VISUAL-STATE
  ∧ AC-WHEEL-CONTENT-ANCHOR
  ∧ AC-NO-LATE-REWRITE
```

- `AC-SB-NO-STALE-RANGE`：图片在稳态完全适合可用视口后，两轴滚动范围均为零；放大历史不得使滚动条滞留。
- `AC-DRAG-CONTINUOUS`：拖拽中每一个已提交帧的图片位移等于指针位移（到达合法边界时只允许钳制），不得发生额外跳变。
- `AC-DRAG-PRESERVES-OVERFLOW-BARS`：只要当前图片在某轴仍真实溢出，该轴滚动范围不得因开始拖拽或取消旧缩放锚点而消失。
- `AC-KBD-ZOOM-CURSOR-ANCHOR`：键盘 `Zoom In` 和 `Zoom Out` 均保持触发时鼠标下的同一图片内容点；几何不可达时落在独立求得的最近合法位置。
- `AC-TOGGLE-DIRECTIONAL-ANCHOR`：`Toggle Fit and 100%` 的目标比当前显示倍率大时用鼠标锚点，目标更小时用可用视口中心锚点。
- `AC-TOGGLE-VISUAL-STATE`：触发 Toggle 时，当前图片若尚未适合窗口则目标为 fit；只有当前图片已经适合窗口时目标才是 100%。
- `AC-WHEEL-CONTENT-ANCHOR`：鼠标滚轮缩放后，鼠标下的归一化图片内容点在可达时保持于原 viewport 点、不可达时落在最近合法位置，包括右下角和滚动条阈值切换场景。
- `AC-NO-LATE-REWRITE`：动画结束、缩放锚点结算、昂贵缩放换图、边界约束及布局回调执行后，不得改写正确终态。

本规格建议新增 5 个静态合同用例和 14 个动态用例。每个问题至少被一个静态用例和两个动态用例覆盖；动态用例同时检查瞬态轨迹和稳态终点。

## 2. 问题分解与证据缺口

### 2.1 从症状到可证伪假设

| 问题 | 可竞争假设 | 当前缺失的证据 | 能证伪假设的观测 |
| --- | --- | --- | --- |
| P1：缩小后垂直条仍存在 | 旧 `sceneRect`、旧虚拟锚点 margin、跨轴布局、旧 timer 或仅是 macOS overlay 外观 | 图片矩形、显式 scene rect、H/V range、bar widget 可见性和所有 timer 的同一时序 | 图片已包含于可用 viewport、所有异步写入者失活后，V range 必须为零且不再复活 |
| P2：放大后拖拽跳变且横条消失 | 拖拽首帧 delta 错、局部/全局坐标混用、取消 pending anchor 时缩小 scene、旧 anchor 回调覆盖新 pan | 每个 mouse move 的位置、图片同一点轨迹、H/V range/value、pending generation 与回调时刻 | 内部位置拖拽每帧满足位移恒等式；仍有横向溢出时 H range 始终非零 |
| P3：键盘缩放没有鼠标锚点 | Action 层没有传递触发时鼠标位置，或所有键盘入口被硬编码为中心 | 真实 shortcut 事件、触发时 cursor、action 路由和逐帧内容点轨迹 | `Zoom In/Out` 中同一 UV 点逐帧保持在 cursor；Toggle 按实际方向选择 cursor/center |
| P4：未 fit 却 Toggle 到 100% | 逻辑 mode 被误当作当前实际 fit 状态；动画中逻辑目标已是 fit，但显示帧仍溢出 | 同一时刻的 mode、logical zoom、displayed zoom、独立 fit target、图片四边和 H/V range | 人为构造 `mode=fit, atFit=false` 后仍选择 fit；反向冲突也按 `atFit` 选择 100% |
| P5：滚轮后右下角不在视口 | `QWheelEvent::position()` 坐标空间错误、scene anchor 在 scrollbar resize 后未恢复、换 backing 后复用旧 scene 坐标 | 原始 event position、UV、每帧 image rect、DPR、bar threshold 与 backing 替换时刻 | 每个已绘制帧的重建 UV 点与原鼠标位置误差不超过容差 |

这些假设是测试分支，不是已经确认的根因。失败报告必须给出“第一个违反哪个 oracle 的事件/帧”，不能仅凭最终现象把某个相关回调宣布为根因。

### 2.2 当前仓库证据与现有覆盖缺口

以下事实由当前工作树直接核验：

| 编号 | 当前实现或测试事实 | 可核验位置 | 对新测试的影响 |
| --- | --- | --- | --- |
| P-F1 | 两轴使用 `ScrollBarAsNeeded`，Qt 自带 transform anchor 被设为 `NoAnchor` | [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp) 第 24–30 行 | Fovelle 自己保存/恢复锚点，必须直接测其事务 |
| P-F2 | 键盘 `zoomIn()`、`zoomOut()` 当前显式传入 viewport center | [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp) 第 1633–1642 行 | 现状与本次明确的鼠标锚点要求冲突；新用例预计在修复前失败 |
| P-F3 | 滚轮路径读取 `QWheelEvent::position()` 并传给 `zoomRelative()` | [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp) 第 849–880、1258–1294 行 | 仅检查调用存在不够，必须验证跨 layout 后的实际 UV |
| P-F4 | 拖拽首个 move 会先取消 pending zoom anchor，再通过 `ScrollHelper` 平移 | [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp) 第 1019–1025 行 | 取消动作可能改变虚拟 scene margin，需同步观察 range 与图片位置 |
| P-F5 | Toggle 当前只按 `getCalculatedZoomMode()==ZoomToFit` 分支 | [`src/mainwindow.cpp`](../src/mainwindow.cpp) 第 2202–2216 行 | 一致状态测试会通过，但 mode/视觉状态冲突测试可暴露错误分支 |
| P-F6 | 显示缩放为 200 ms 动画；另有 0/50/350/500 ms 级布局、换图、锚点和约束写入者 | [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp) 第 38–45、87–131、1645–1722、3451–3453 行 | 不得用一次固定 sleep 代替 terminal/quiet 判定 |
| P-F7 | 当前右下角测试直接调用 `zoomAbsolute()`，没有发送真实滚轮事件，且没有等完全部异步写入者 | [`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp) 第 6826–6913 行 | 它通过并不能关闭 P5 的事件路由和稳态证据缺口 |
| P-F8 | 当前 Toggle 测试只覆盖 mode 与视觉状态一致的 `manual→fit→100→fit` | [`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp) 第 9389–9458 行 | 必须增加两种相互冲突状态与动画重入 |
| P-F9 | 指定现场 JPEG 本机存在，探测尺寸为 3840×4407，无 EXIF orientation | `/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg` | 作为本地系统复现证据；CI 使用同宽高比的生成夹具，不能依赖外置卷 |

本轮还重新构建并运行了现有的 scrollbar axes、center threshold、bottom-right anchor 和 Toggle behavior 测试；在上述本机环境中共 8 个 QtTest checkpoint 通过。该结果只证明旧测试合同可重复，不证明五个新问题不存在。

## 3. 联网多跳检索与交叉验证

### 3.1 检索链

```text
用户可见的条/图片异常
→ 区分 bar 外观、range/value、scene rect 与图片内容点
→ 查 QAbstractScrollArea 的 AsNeeded 合同
→ 查 QGraphicsView 的 scene/view 映射、alignment 与 anchor 合同
→ 下钻 Qt 源码确认两轴联动和 range 重算顺序
→ 查 wheel/mouse 事件位置的坐标语义
→ 查动画采样是否有固定帧率
→ 查 QtTest 对异步等待与 signal 取证的建议
→ 回到 Fovelle 源码定位每个状态写入者和现有测试缺口
```

### 3.2 已核验外部事实

访问日期均为 2026-09-03；技术事实优先采用 Qt 官方文档，并用 Qt 6.11.1 官方源码作第二跳验证。

| 编号 | 经核验事实 | 来源 | 测试约束 |
| --- | --- | --- | --- |
| F1 | `ScrollBarAsNeeded` 在滚动范围非零时显示、否则隐藏；条隐藏/出现还会扩大/缩小 viewport | [Qt：QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)、[Qt::ScrollBarPolicy](https://doc.qt.io/qt-6/qt.html#ScrollBarPolicy-enum) | 主语义判据用 `maximum>minimum`；两轴与 viewport 必须同采样 |
| F2 | `sceneRect` 定义 view 可通过滚动条导航的范围；`mapToScene`/`mapFromScene` 用于 view 与 scene 间映射 | [Qt：QGraphicsView sceneRect 与映射](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop) | 不能只比较 zoom 数值；必须观测 scene rect 和同一内容点 |
| F3 | 整个 scene 可见时由 alignment 定位；transform anchor 的效果主要出现在仅部分 scene 可见时 | [Qt：QGraphicsView alignment/anchor](https://doc.qt.io/qt-6/qgraphicsview.html#transformationAnchor-prop) | 穿越“有条/无条”阈值时必须分别验证锚点与居中，不可套一个公式 |
| F4 | Qt 6.11.1 的 `recalculateContentSize()` 先判断直接溢出，再处理“一轴占位导致另一轴也溢出”，fit 时把对应 range 设为 `(0,0)` | [Qt 6.11.1 源码：qgraphicsview.cpp](https://raw.githubusercontent.com/qt/qtbase/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp) 第 313–413 行 | expected bar 状态要按两轴耦合求解；终态 fit 时 range 必须为零 |
| F5 | `QAbstractSlider::value` 是受 range 约束的整数；range 改变会钳制 value；`actionTriggered` 发出时 position 已变但 value 尚未提交 | [Qt：QAbstractSlider](https://doc.qt.io/qt-6/qabstractslider.html#details) | 只看最终 value 会漏掉临时钳制；拖拽需同时记录 position、value、range 和信号顺序 |
| F6 | `QWheelEvent::position()` 是事件发生时的鼠标位置；wheel 同时可带 angle、pixel delta 和 phase | [Qt：QWheelEvent](https://doc.qt.io/qt-6/qwheelevent.html#details) | 必须向 viewport 发送真实事件并保存原 position，不能用直接函数调用替代入口测试 |
| F7 | `QMouseEvent::position()` 相对接收控件；若操作本身移动控件，应使用 global position 避免抖动 | [Qt：QMouseEvent](https://doc.qt.io/qt-6/qmouseevent.html#details) | 拖拽轨迹同时记录 local/global 坐标，以排除坐标基切换 |
| F8 | `QAbstractAnimation` 的更新间隔和调用次数未定义；可以用 `setCurrentTime()` 确定性访问时间轴 | [Qt：QAbstractAnimation](https://doc.qt.io/qt-6/qabstractanimation.html#details)、[Qt：QVariantAnimation](https://doc.qt.io/qt-6/qvariantanimation.html#details) | 同一用例必须有逐毫秒确定性扫描和不干预 timer 的真实回放 |
| F9 | Qt widget、事件与窗口几何采用 DIP；`QT_SCALE_FACTOR` 是官方建议的高 DPI 测试入口 | [Qt：High DPI](https://doc.qt.io/qt-6/highdpi.html#testing) | 所有几何 oracle 用 DIP，DPR 2 必须在独立进程复跑 |
| F10 | 异步测试应优先等待 signal 或用 `QTRY_*` 条件轮询，固定 `qWait()` 容易在不同机器上不稳定 | [Qt Test Best Practices](https://doc.qt.io/qt-6/qttest-best-practices.html#avoid-fixed-timeouts)、[QSignalSpy](https://doc.qt.io/qt-6/qsignalspy.html) | terminal 由状态条件而非单一睡眠时长定义 |

### 3.3 交叉验证约束

1. Qt 文档用于确认公开合同，Qt 源码用于确认双轴重算细节，Fovelle 源码用于确认实际写入者；任何根因结论不得只依赖注释。
2. 内部状态与外部画面双重取证：每个关键帧同时保存 image rect、scene rect、transform、H/V range/value、cursor/anchor 与截图。
3. expected 与 actual 数据独立：expected anchor 从初始 UV 与当前 image scene rect重建，不得由当前 scrollbar value 反推；expected bar 从内容尺寸、最大 viewport 与 style extent 求解，不读取 actual visibility。
4. 现场 JPEG 与可生成夹具交叉验证：前者证明用户路径，后者提供可移植、带角点标记、可精确断言的 CI 证据。
5. 确定性动画扫描和真实 event-loop 回放必须同时通过；前者找出时间轴边界，后者覆盖未规定的 timer/layout/paint 交错。

## 4. 显式前提与判定定义

### 4.1 显式前提

1. 本次需求把“Zoom In/Zoom Out 使用鼠标锚点”和“Toggle 按实际方向选择锚点”视为产品合同，而非从 Qt 默认行为推导。
2. `fit` 的权威判据是当前已显示倍率/几何是否匹配独立计算的 Zoom-to-Fit 目标，而不是 `calculatedZoomMode` 名称；“图片只是能容纳在 viewport 中”不等同于“正处于 fit 倍率”。核心用例设置 `fitoverscan=0`、`smallimageoneone=false`、`fitzoomlimitenabled=false` 和 `constraincentersmallimage=true`，避免其他产品选项改变定义。
3. “可用 viewport”排除 macOS full-size titlebar 的 `obscuredHeight`；测试从运行时读取，不硬编码标题栏高度。
4. 鼠标位于 viewport 且位于图片内时，请求锚点是该精确图片内容点；鼠标在 viewport 内但图片外时，本规格沿用当前产品的最近图片边/角投影合同。若请求位置在目标倍率下不可达，则不是凭感觉放宽容差，而是使用 4.2 节独立计算的最近合法位置。
5. 键盘或滚轮明确提供有效鼠标锚点时，该次缩放（包括 terminal/quiet 终态）的锚点合同优先于一般的“小图居中”设置；只有没有有效鼠标锚点或合同明确选择 center 的操作才按小图约束居中。这样可同时检验鼠标锚定和“图片适合视口后无虚假滚动范围”。
6. 鼠标不在窗口内或 cursor 被隐藏时应回退到可用 viewport 中心。该项是为消除未定义行为而提出的显式建议；若产品另有决定，应只替换对应数据行，不影响其余用例。
7. 几何使用 DIP。滚动条整数取整允许每轴 1 DIP，连续映射累计允许锚点欧氏误差 2 DIP；精确角点像素视觉检查允许 1 个 device pixel。不得调大容差来吞掉可见跳变。
8. 动态 CI 不直接依赖外置卷。若现场 JPEG 不存在，`TC-DRAG-REAL-01` 与 `TC-WHEEL-REAL-01` 明确 `QSKIP`，但对应 hermetic 用例不得跳过。
9. “稳定”不是某个固定毫秒数：必须等到动画停止、相关 single-shot timer 均 inactive、所有 queued layout/paint 已处理，且连续两个事件循环回合的状态 tuple 不变。

### 4.2 统一几何 oracle

对采样 `k` 定义：

- `U_k`：可用 viewport 矩形；
- `R_k`：当前图片 item 的 scene rect；
- `D_k = mapFromScene(R_k)`：当前已显示图片的 viewport 多边形/包围矩形；旋转测试使用四角多边形而非仅包围盒；
- `B_h/B_v := scrollbar.maximum > scrollbar.minimum`：两轴语义可滚动状态；
- `P=(u,v)`：输入发生前，由鼠标下图片点计算的归一化内容坐标；
- `S_k(P)=(R_k.left+u·R_k.width, R_k.top+v·R_k.height)`：在当前 backing 中重建的同一图片内容点；
- `A_k=mapFromScene(S_k(P))`：该内容点当前的 viewport 坐标。

先区分两个不能互换的谓词。`fitsInside` 用于判定是否需要滚动条；`atFit` 用于 Toggle 状态机：

```text
fitsInside(k)
  := D_k 的四个变换后角点均在 U_k 内（每轴容差 1 DIP）
     AND B_h = false
     AND B_v = false

zFit*(k)
  := 不读取 calculatedZoomMode/产品 fit 结果，独立求得的最大可表示倍率 z，
     使图片经过非缩放 transform、DPI 与像素取整后四角仍全部位于 U_k

atFit(k)
  := fitsInside(k)
     AND abs(displayedZoom(k) - zFit*(k)) <= zoomTolerance
```

因此，大图缩得比 fit 更小时虽然 `fitsInside=true`，但 `atFit=false`；小图在 100% 时即使没有滚动条，只要其独立 fit 倍率大于 100%，也仍是 `atFit=false`。这一区分正是 Toggle 真值表不应只看“有没有条”的原因；存在非零 range 则一定 `atFit=false`，range 为零却不充分证明 `atFit=true`。

为避免把“不可达的精确锚定”误报成缺陷，非旋转核心行按每轴独立求目标图片左上角。设请求的 viewport 锚点为 `C=(c_x,c_y)`，目标显示尺寸为 `W×H`：

```text
x* = c_x - u·W
y* = c_y - v·H

legalX = [U.right-W, U.left]   if W > U.width
         [U.left, U.right-W]   if W <= U.width
legalY 同理

xExpected = clamp(x*, legalX)
yExpected = clamp(y*, legalY)
CExpected = (xExpected+u·W, yExpected+v·H)
```

该区间分别表达“大图至少覆盖 viewport”和“小图完整留在 viewport”这两个合法位置约束。`x*/y*` 落在区间内时必须精确锚定；否则只能钳制到最近合法边界，且不得为伪造可达性而在 terminal 保留空白 scene margin 或滚动条。旋转行将同一规则推广为图片四角多边形的最近可行平移，并由独立几何库求解，不复用产品约束代码。

鼠标锚点误差与中心锚点误差：

```text
cursorError(k) = distance(A_k, CExpected_k)
centerError(k) = distance(mapFromScene(scenePointCapturedAtUsableCenter), center(U_k))
```

对已证明请求锚点可达的数据行，`CExpected` 就是原触发点；只有专门的边界行才允许它是钳制结果。

在没有临时外部锚点 margin 的样例中，expected bar 状态采用 Qt 源码所示的最小耦合解：从“无可选 bar”的 `maximumViewportSize` 开始，按内容直接溢出决定初值；若 H 占位使高度不足则加入 V，若 V 占位使宽度不足则加入 H，迭代至不再变化。该 expected 只使用当前 transform 后内容尺寸和 `PM_ScrollBarExtent`，不读取 actual range/visibility。

### 4.3 从事实与前提到用例的链式推理

以下推导只把第 3 节的经核验事实和 4.1 节显式产品合同当作前提；“可能根因”不参与 expected 的计算。

1. **P1**：由 F1，`AsNeeded` 的语义由 range 决定；由 F4，H/V 是否需要必须联立求解；由 P-F6，terminal 前仍可能有晚到写入。因此，“图片变小”只能推出应重算，不能直接推出某个瞬时 widget 外观。测试必须先构造 `非零 range → fitsInside` 的历史，再同时断言耦合 range、layout 边界和 quiet window，才足以排除滞留。
2. **P2**：由 F5，range 改变会钳制 value；由 F7，拖拽事件位置有明确局部坐标；由 P-F4，首个 move 又会取消旧锚点。因此，终态相同不能排除首帧跳变。测试必须逐 move 验证图片位移恒等式，并在取消前后独立验证真实溢出；再让旧 timer 全部到期，才能排除旧事务覆盖新 pan。
3. **P3**：需求前提 1 指定键盘锚点合同；F2 提供内容点跨坐标空间的可观测映射；P-F2 表明当前入口传的是 center。因此，静态路由测试应预计暴露现状冲突，动态测试则必须用真实快捷键、非中心鼠标点和独立 UV 重建，才能区分“鼠标锚定”与“中心锚定后碰巧看似正确”。Toggle 的锚点还必须由 `target-displayed` 的符号决定，而不能由 fit/100 的分支名决定。
4. **P4**：由前提 2，`atFit` 同时需要 containment、零 range 和 displayed zoom 等于独立 `zFit*`；P-F5 显示当前实现只查看 mode。因此必须刻意制造 `mode` 与 `atFit` 的两个方向冲突，并在动画重入时按触发帧取样。只有真值表四象限均服从 `atFit=false→fit、true→100%`，才能证明分支不是偶然正确。
5. **P5**：由 F6，wheel 的位置属于事件接收 viewport；由 F1/F4，bar 切换会改变 viewport；由 P-F3/P-F6，输入后还有动画、layout 和 backing 写入。因此，只看一次 wheel 后的最终 zoom 不充分。测试必须先保存 UV，再在每个已提交 frame 用新 backing 重建同一点，同时验证 4.2 节可达/钳制 oracle、bar 耦合状态和 late callbacks；用户右下角现场行与带色块的 hermetic 行共同通过，才构成可移植且可目视复核的证据。

### 4.4 拖拽 oracle

在 LTR、未触边且第 `i` 个 mouse move 的 local delta 为 `d_i` 时：

```text
mappedImagePoint(i) - mappedImagePoint(i-1) = d_i ± 1 DIP
hValue(i) - hValue(i-1) = -d_i.x ± 1
vValue(i) - vValue(i-1) = -d_i.y ± 1
```

RTL 仅翻转横轴 value 的符号，图片仍应“跟手”。到达边界时只允许将期望值钳制到独立计算的合法范围；不允许先越界再回弹。

### 4.5 瞬态与稳态采样边界

- 瞬态：真实输入被接受后、每次 animation property 更新、H/V `rangeChanged/valueChanged`、viewport `Resize/LayoutRequest/Paint`、昂贵 backing 替换、拖拽首帧和 release。
- 内部函数栈中尚未提交绘制的短暂不一致记录为诊断事件；若该状态到达 `Paint` 或事件循环边界，则按失败处理。
- 稳态：满足前提 9 的 terminal 条件后采一次，再处理一个超过当前最长 single-shot deadline 的观察窗；期间 tuple 仍须不变。观察窗用于发现陈旧回调，不用于替代 terminal 条件。

## 5. 夹具、矩阵和失败工件

### 5.1 公共夹具

- 可见 `MainWindow`，外窗约 640×480 DIP；实际 `viewport()` 和 `U` 在显示后实测。
- 设置通过 RAII guard 保存和恢复：`windowresizemode=Never`、`onetoonepixelsizing=false`、`fitoverscan=0`、`cursorzoom=true`、`constrainimageposition=true`、`constraincentersmallimage=true`、25% 缩放步长。
- `fixture-portrait`：按 3840:4407 比例生成的无损 PNG，含 9×9 坐标网格、四角不同色 12×12 标记和中心十字；尺寸按实测 viewport 选择，使 fit 后连续三次 1.25 倍放大产生明确横向和纵向 range。
- `fixture-threshold`：运行时生成四组尺寸，分别产生 none、H-only、V-only、both，并包含与 `PM_ScrollBarExtent` 相差 `-1/0/+1 DIP` 的边界行。
- `fixture-small`：使 ZoomToFit 比率明确大于 1.25；用于证明 Toggle 的锚点由方向而非“fit/100 分支名”决定。
- 平滑缩放矩阵：Disabled、Bilinear、Expensive；核心门禁至少跑 Disabled 和 Expensive。
- DPR 矩阵：原生 DPR 与独立进程 `QT_SCALE_FACTOR=2`。

### 5.2 输入必须走真实入口

- 键盘用例先 `QTest::mouseMove()` 建立 cursor，再用 `QTest::keyClick()` 或实际 `QKeySequence` 触发 canonical QAction；只直接调用 `zoomAbsolute()` 的测试不能替代入口验收。
- 滚轮用例向 `view->viewport()` 发送带一致 local/global position、`angleDelta=(0,±120)`、真实 pointing device 的 `QWheelEvent`，并断言事件被接受。
- 拖拽用例使用 press → 至少 4 个非共线 move → release；灰盒 `ScrollHelper::move()` 只作算术单元测试，不替代端到端拖拽。

### 5.3 每次失败保留的工件

- `trace.jsonl`：单调时间、事件类型/phase、logical/displayed zoom、transform、DPR、image/scene/viewport/usable rect；
- H/V 的 policy、minimum、maximum、value、sliderPosition、pageStep、widget visibility；
- cursor local/global、anchor UV/scene/expected viewport/actual viewport/error；
- animation state/currentTime、所有相关 timer active 状态和 pending generation；
- `pre.png`、`first-bad-frame.png`、`worst-frame.png`、`terminal.png`；
- 首个失败 oracle、expected、actual 以及前后各 3 个 sample。

截图必须在对应 paint/event 回合返回后抓取，避免在 paint handler 内 `grab()` 造成重入并改变时序。

## 6. 覆盖与追踪矩阵

| 问题 | 静态用例 | 动态用例 | 瞬态覆盖 | 稳态覆盖 |
| --- | --- | --- | --- | --- |
| P1 缩小后 V bar 滞留 | `TC-ST-SCROLL-RANGE` | `TC-SB-ZOOMOUT-01/02/03` | 跨阈值每帧、两轴 range/layout | quiet 后零 range、循环无记忆 |
| P2 拖拽跳变/H bar 消失 | `TC-ST-DRAG-ARBITRATION` | `TC-DRAG-REAL-01`、`TC-DRAG-HERMETIC-01/02` | 首 move、动画中打断、range 信号 | release 后无回弹/旧回调 |
| P3 键盘锚点 | `TC-ST-ANCHOR-ROUTING` | `TC-KBD-ZOOM-01`、`TC-KBD-TOGGLE-ANCHOR-01` | 每个动画时点、bar resize | terminal anchor 不漂移 |
| P4 Toggle 错误状态机 | `TC-ST-TOGGLE-PREDICATE` | `TC-TOGGLE-STATE-01/02/03` | mode/视觉状态错位、重入 | 最终 fit/100 与条状态 |
| P5 滚轮右下角错误 | `TC-ST-ANCHOR-ROUTING` | `TC-WHEEL-REAL-01`、`TC-WHEEL-UV-01/02` | 真实 event、每帧/换 backing | UV、缩放倍率、bar 全稳定 |
| 测试本身可靠性 | `TC-ST-TEST-ORACLES` | 所有动态用例 | deterministic + live | condition-based terminal |

## 7. 静态测试用例

### TC-ST-SCROLL-RANGE

#### 测试目的

静态验证滚动条生命周期的数据流：bar 为 `AsNeeded`，每个显示倍率/scene 改变都重算当前滚动范围，缩放事务结束后不保留无业务意义的 scene margin 或旧 range。

#### 前置条件

仓库源码可读；Clang AST 或等价的 C++ 数据流分析器可用；分析目标使用与产品相同的编译数据库。

#### 输入数据

`src/qvgraphicsview.cpp/.h`、`src/scrollhelper.cpp/.h`，以及负责图像 item backing 替换的源码。

#### 操作步骤

1. 确认两轴 policy 的生产初始化路径。
2. 从 animated zoom setter、普通缩放、昂贵换图、resize、rotate 和 anchor settle 追踪到 scene/range 重算。
3. 枚举所有能扩展 scene rect 的 margin，检查其所有成功、取消和 generation 失效出口。
4. 检查是否存在 `AlwaysOn`、仅隐藏 widget 而不清零 range、或把最终 logical zoom 提前用于当前显示帧的路径。

#### 预期结果

两轴始终遵循 `AsNeeded`；range 由当前显示 frame 的有效 scene 几何派生；所有临时 margin 在事务完成/取消后可达清理；不存在能在 terminal 重新写入旧 scene/range 的无 generation 保护路径。

#### 后置条件

不修改源码；输出带文件、行号和数据流路径的 `static-scroll-range.json`。

### TC-ST-DRAG-ARBITRATION

#### 测试目的

静态验证“新拖拽意图优先于旧缩放事务”，并确保取消锚点、更新 scene 与应用 pan 的顺序不会制造首帧跳变或删除真实溢出 range。

#### 前置条件

生产 mouse press/move/release、scroll helper、pending anchor 与所有 delayed callback 均可由分析器解析。

#### 输入数据

`src/qvgraphicsview.cpp/.h`、`src/scrollhelper.cpp/.h`。

#### 操作步骤

1. 追踪每个 Pan 入口至 pending anchor 取消与 scrollbar 写入。
2. 验证取消会使旧 generation 失效并停止相应 settle timer。
3. 检查取消临时 margin 后的 scene rect 仍至少覆盖真实图片。
4. 检查 release/constraint 不会启动越界回弹或重放旧锚点。

#### 预期结果

首个有效 move 先终止旧意图，再仅应用本次 delta；任何晚到回调均因 generation 或 inactive state 不写入；scene 缩回时只删除虚拟空白，不删除图片溢出；边界约束为同步钳制或等价的无回弹行为。

#### 后置条件

仅生成 `static-drag-arbitration.json`，不改变产品设置或代码。

### TC-ST-ANCHOR-ROUTING

#### 测试目的

静态验证键盘、Toggle 和滚轮 action 从输入事件到 zoom 核心完整携带正确的 anchor policy 与同一坐标空间。

#### 前置条件

ActionManager、MainWindow、QVGraphicsView 与快捷键注册源码均进入同一数据流分析。

#### 输入数据

`src/actionmanager.cpp`、`src/shortcutmanager.cpp`、`src/mainwindow.cpp/.h`、`src/qvgraphicsview.cpp/.h`。

#### 操作步骤

1. 从 `zoomin`、`zoomout`、`togglefitand100` 的真实 action dispatch 追踪参数。
2. 检查触发时 cursor 先从 global 映射到接收 viewport，并验证其处于可用区域；不得用陈旧 mouse-move 坐标冒充触发坐标。
3. 检查 Zoom In/Out 均选择 cursor policy。
4. 检查 Toggle 先计算目标，再由 `target > displayed` 选择 cursor、`target < displayed` 选择 center。
5. 检查 wheel 使用事件的 viewport-local `position()`，没有重复 map 或混入 window-local 坐标。

#### 预期结果

三类键盘 action 和 wheel action 均有唯一、可追踪的 anchor 来源；不存在 Zoom In/Out 硬编码中心；Toggle 不按分支名称硬编码锚点；local/global 转换只发生一次且以 viewport 为接收者。

#### 后置条件

输出 `static-anchor-routing.json`；不触发 GUI action。

### TC-ST-TOGGLE-PREDICATE

#### 测试目的

静态验证 Toggle 的分支条件来自当前 `atFit` predicate，而不是仅来自 calculated mode、目标 logical zoom、简单 containment 或过期的 scrollbar widget visibility。

#### 前置条件

Toggle action、fit 计算和当前显示几何的调用图可解析。

#### 输入数据

`src/mainwindow.cpp/.h`、`src/qvgraphicsview.cpp/.h`、`src/logicalpixelfitter.cpp/.h`。

#### 操作步骤

1. 定位 Toggle 的唯一决策点。
2. 追踪该 predicate 是否读取当前 displayed zoom/transform 下的图片四角、usable viewport、两轴语义 range，并与独立意义上的当前 fit target 比较。
3. 检查它在 animation running、resize、rotation 和 backing replacement 后仍读取当前 frame。
4. 检查 `calculatedZoomMode` 只作为模式管理信息，不能单独决定 fit/100 分支。

#### 预期结果

`atFit=false → targetFit`，`atFit=true → target100` 的数据流成立；mode 与实际 fit state 冲突时 `atFit` 优先；仅仅 `fitsInside` 不足以走 100%，判据也不依赖 overlay bar 是否正在绘制。

#### 后置条件

输出 `static-toggle-predicate.json`；不修改源码。

### TC-ST-TEST-ORACLES

#### 测试目的

静态审计新增测试自身，防止直接调用核心函数、读取 actual 构造 expected、固定 sleep 和只测终态造成假通过。

#### 前置条件

新增 QtTest、系统复现 driver、CTest 注册和本规格均已提交。

#### 输入数据

`tests/`、`tests/CMakeLists.txt`、本文件以及测试夹具生成器。

#### 操作步骤

1. 检查 P1–P5 与用例 ID 的双向映射。
2. 检查 keyboard/wheel/drag 入口测试使用真实事件。
3. 检查 UV、expected bars、`fitsInside` 和 `atFit` oracle 不读取被断言的 actual 字段。
4. 检查每个动态用例均有 transient samples、condition-based terminal 与失败工件。
5. 检查 native DPR/`QT_SCALE_FACTOR=2`、Disabled/Expensive 和 hermetic/现场样例注册。

#### 预期结果

所有合同均至少一正一反数据行；不存在以 direct `zoomAbsolute()` 代替真实入口的唯一用例；不存在仅 `qWait(N)` 后比较一次的用例；外置文件缺失只跳过现场层，不跳过 CI 门禁。

#### 后置条件

输出 `static-test-oracles.json`，不启动 GUI、不写用户设置。

## 8. 动态测试用例：P1 缩小后垂直滚动条仍存在

### TC-SB-ZOOMOUT-01

#### 测试目的

复现“先放大产生 V range，再缩小到图片小于 viewport”的历史路径，验证垂直 range 在适当阈值清零且终态不滞留。

#### 前置条件

公共夹具已显示并稳定；使用 `fixture-portrait`；起始为真正 fit 且 H/V range 均为零；center-constrain 启用。

#### 输入数据

数据行：`input={keyboard Zoom Out, mouse wheel -120}` × `scaling={Disabled,Expensive}` × `DPR={native,2}`。cursor 固定在 `center(U)`，先用真实对应入口放大到 V range 明确非零，再逐步缩小到 `D.width < U.width-8` 且 `D.height < U.height-8`；中心 cursor 使锚点合同与小图居中合同一致。

#### 操作步骤

1. 记录无条 baseline。
2. 放大并等待至少 V range 非零；将 V value 移到中间，避免端点钳制掩盖问题。
3. 连续发出缩小输入；从第一个输入起记录 animation、layout、range、scene、image rect 与 paint。
4. 每个 displayed frame 用耦合 oracle 计算 expected H/V。
5. 等待 terminal/quiet，再越过最长旧回调观察窗。

#### 预期结果

当当前显示内容尚溢出时 V range 合法存在；一旦已提交 frame 满足无垂直溢出且跨轴占位也不再需要 V，V range 在该 layout/paint 边界为零。terminal 时 `fitsInside=true`、H/V range 均为零、图片按小图约束居中；即使此时倍率低于 `zFit*`、因而 `atFit=false`，V range 也不得复活。

#### 后置条件

关闭窗口、恢复 settings、清理生成图片与 trace；失败时保留规定工件。

### TC-SB-ZOOMOUT-02

#### 测试目的

验证两轴联动边界，排除“另一个 bar 的占位”或错误的单轴判断使 V bar 永久保留。

#### 前置条件

`fixture-threshold` 可按运行时 `maximumViewportSize` 和 `PM_ScrollBarExtent` 生成精确尺寸；缩放动画可被确定性暂停。

#### 输入数据

数据行：`both→V-only`、`both→H-only`、`both→none`、`V-only→none`；每行再取内容尺寸相对阈值 `-1/0/+1 DIP`。

#### 操作步骤

1. 为每行建立起始 bar 状态并断言非退化。
2. 发出一个真实 zoom-out 输入并暂停 animation。
3. 对 `currentTime=0..duration` 逐毫秒推进；每次排空同步 layout 后采样。
4. 用不读取 actual range 的最小耦合解计算 expected。
5. 在新窗口做一次不暂停的 live replay 并等 terminal。

#### 预期结果

每个可提交 sample 的 H/V 语义状态均与耦合 oracle 一致；恰好等于 viewport 时对应 range 为零；不会出现 V range 已无几何理由却因 H 的旧状态自维持。deterministic 与 live 的 terminal tuple 相同。

#### 后置条件

恢复 animation/timer 状态并销毁每行窗口；数据行相互隔离。

### TC-SB-ZOOMOUT-03

#### 测试目的

验证滚动条生命周期没有历史依赖、迟滞或累积 rounding：反复放大/缩小后结果只由当前几何决定。

#### 前置条件

`fixture-portrait`、真实 wheel 入口、事件 trace 和 terminal detector 可用；测试在非端点位置运行。

#### 输入数据

30 个循环：每循环 3 格放大、3 格缩小；第 10/20 个循环插入一次短拖拽；Disabled 与 Expensive 各一行。

#### 操作步骤

1. 保存 cycle 0 的 fit tuple。
2. 执行输入序列，每个半循环等 terminal。
3. 记录每次 fit terminal 的 scene rect、transform、两轴 range/value 与图片中心。
4. 比较 cycle 1–30 与 cycle 0；同时检查对象/timer 数量不增长。

#### 预期结果

每次缩回 fit 的 H/V range 都为零，图片中心误差不超过 2 DIP；scene rect/transform 在容差内等于 baseline；不存在某次循环后 V bar 开始永久存在或 timer/pending generation 泄漏。

#### 后置条件

关闭窗口，恢复设置，输出每循环摘要；不保留外置资源句柄。

## 9. 动态测试用例：P2 拖拽跳变且滚动条消失

### TC-DRAG-REAL-01

#### 测试目的

按用户给出的文件和“三格放大后拖动”路径做真实桌面复现，验证图片不跳变且横向溢出时 H range 不消失。

#### 前置条件

指定 JPEG 存在；本地 Cocoa 桌面允许发送真实 mouse/wheel 事件；窗口与用户复现尺寸、鼠标缩放和 Pan 拖拽设置一致。该用例不作为无外置卷 CI 的硬门禁。

#### 输入数据

`/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`（已探测 3840×4407）；鼠标在可用 viewport 右侧；3 个 `angleDelta.y=+120`；拖拽轨迹 `(0,0)→(-18,9)→(-43,21)→(-71,34)` DIP。分别在缩放 terminal 后和第 3 次缩放动画尚在运行时开始拖拽。

#### 操作步骤

1. 打开图片并保存 fit baseline。
2. 将 cursor 移到指定右侧位置，发送三格真实 wheel，断言均被接受。
3. 在两种时机按下配置为 Pan 的鼠标按钮并发送 4 个 move，再 release。
4. 对每个 move 记录 local/global cursor、固定 UV 点、图片矩形与 H/V range/value。
5. 等待所有动画和延迟回调 terminal。

#### 预期结果

每个内部拖拽帧满足拖拽 oracle；首帧没有大于输入 delta+1 DIP 的额外位移。若图片宽度仍大于可用 viewport，H range 全程非零；range 不因 cancel pending anchor 瞬间归零。release 后位置不回跳，旧 zoom anchor 不覆盖最终 pan。

#### 后置条件

关闭图片和窗口，不修改或复制现场 JPEG；缺失文件时报告明确的 `SKIP_EXTERNAL_FIXTURE`。

### TC-DRAG-HERMETIC-01

#### 测试目的

用可移植的带网格 portrait 夹具精确验证“缩放已稳定后拖拽”的连续性、range 不变量和视觉内容一致性。

#### 前置条件

公共 hermetic 夹具已 fit；三次 1.25 倍真实 wheel 后，图片在两轴均比 U 大至少 80 DIP；滚动值位于 range 中间且离端点至少 40 DIP。

#### 输入数据

4 个非共线 move delta：`(-7,5)`、`(-11,-4)`、`(9,-8)`、`(13,6)`；LTR/RTL 两行；native/DPR2 两行。

#### 操作步骤

1. 记录 press 点下 UV、两轴 range/value 和网格截图。
2. 发送真实 press/moves/release；每个 move 返回事件循环后采样。
3. 由 cursor delta 独立计算 expected image point 和 scrollbar value。
4. 对比角点标记像素与几何映射。
5. 等 terminal，并再处理陈旧回调观察窗。

#### 预期结果

图片逐帧跟手，误差每轴不超过 1 DIP；transform 和 scene rect 尺寸在纯拖拽中不变；两轴 range 不变且非零；release 后没有第二次位移，截图标记与几何 oracle 一致。

#### 后置条件

释放鼠标 grab、关闭窗口、恢复 layout direction 和设置；即使用例中途失败也由 scope guard 清理。

### TC-DRAG-HERMETIC-02

#### 测试目的

验证拖拽在 zoom animation/pending anchor 尚未结算时取得控制权，晚到的 anchor、expensive scaling 或 constraint 不会产生跳位或删除 bar。

#### 前置条件

`fixture-portrait` 处于 fit；Expensive scaling 开启；能观察 zoom animation、anchor settle、expensive scale 和 constraint timer。

#### 输入数据

三次快速 wheel-in，间隔 10 ms；在最后一次输入后 animation `currentTime` 落在 20–80 ms 时 press，并拖动 `(30,-20)`；另设在 190–199 ms 开始的边界行。

#### 操作步骤

1. 发出快速 zoom 序列，确认 animation running 且 pending anchor 有效。
2. 在指定时间开始真实拖拽，记录取消前后 generation、scene rect、range 和 UV。
3. 完成至少 4 个 move/release。
4. 允许所有原定 timeout 自然到期，持续采样至 terminal。
5. 将最终 pan 与“只执行拖拽、不执行旧 anchor 恢复”的独立预言比较。

#### 预期结果

首个 move 后旧 anchor 即失效；取消虚拟 margin 可以缩小空白范围，但若图片仍溢出则 H/V range 保留。任何晚到 callback 均不改变拖拽结果；从首 move 到 terminal 没有超过输入/钳制预言的跳变。

#### 后置条件

确保所有 timer inactive、mouse button 状态复位，再销毁 view；失败 trace 标出第一个陈旧 writer。

## 10. 动态测试用例：P3 键盘缩放锚点

### TC-KBD-ZOOM-01

#### 测试目的

验证真实键盘 `Zoom In` 和 `Zoom Out` 均以触发时鼠标位置为锚点，而非 viewport center。

#### 前置条件

快捷键 manager 已加载 canonical action；viewport 获得焦点；cursor 可见并位于图片内部 `UV=(0.73,0.68)`；核心行已用 4.2 节 oracle 证明目标位置可达。

#### 输入数据

核心矩阵：`action={Zoom In,Zoom Out}` × `bar transition={none→both,both→both,both→H-only,both→V-only,both→none}` × `scaling={Disabled,Expensive}` × `DPR={native,2}`，每行调节起点与 cursor 以保证精确锚点可达。另设四角 zoom-out 至小图的不可达边界行，expected 取 4.2 节钳制结果。为避免平台默认组合差异，测试从 ShortcutManager 读取当前有效 `QKeySequence`。

#### 操作步骤

1. 用 `QTest::mouseMove` 放置 cursor，记录触发点和 UV；同时记录 viewport center 下的另一 scene 点作为反证。
2. 用真实 key sequence 触发一次 action，断言只产生一个 logical target。
3. deterministic 阶段逐毫秒扫描；live 阶段自然播放。
4. 每个 committed sample 重建 UV 并计算 cursorError；同时计算 center point 的移动量。
5. 等 terminal 和陈旧回调观察窗。

#### 预期结果

Zoom In 与 Zoom Out 的 cursorError 全程不超过 2 DIP；核心可达行的 `CExpected` 等于原 cursor，边界行则等于最近合法位置且图片仍完整位于/覆盖 U。非中心 cursor 样例中 viewport-center scene 点应按缩放产生可观测位移，证明不是碰巧中心锚定。滚动条切换和 backing 替换前后同一 UV 遵守同一 oracle；terminal 不漂移，也不为不可达锚点保留虚假 bar。

#### 后置条件

恢复快捷键、focus、cursor 和 settings；销毁两个阶段各自的窗口。

### TC-KBD-TOGGLE-ANCHOR-01

#### 测试目的

验证 Toggle 的锚点策略只由实际缩放方向决定：放大用鼠标，缩小用可用 viewport 中心。

#### 前置条件

`fixture-portrait` 与 `fixture-small` 均可建立明确 fit ratio；cursor 位于非中心 `UV=(0.77,0.31)`；fit 状态判断已稳定；各非退化行的期望锚点由 4.2 节证明可达。

#### 输入数据

| 数据行 | 当前状态 → 目标 | 实际方向 | 期望锚点 |
| --- | --- | --- | --- |
| large-fit-to-100 | fit `<1` → 1.0 | 放大 | cursor |
| large-100-to-fit | 1.0 → fit `<1` | 缩小 | usable center |
| small-fit-to-100 | fit `>1` → 1.0 | 缩小 | usable center |
| small-100-to-fit | 1.0 → fit `>1` | 放大 | cursor |
| exact-100-fit | fit `=1` → 1.0 | 无变化 | 不移动、不启动动画 |

#### 操作步骤

1. 对每行建立当前视觉状态，独立计算 fit target。
2. 保存 cursor UV 与 usable-center scene 点。
3. 用真实 Toggle 快捷键触发并记录 chosen target/direction。
4. 扫描全部 committed frame，按数据表选择唯一 oracle。
5. 等 terminal，并验证另一候选 anchor 在非退化行确有位移。

#### 预期结果

五行均按 `target compared with displayed zoom` 选择锚点；不能把“fit→100”永久等同于放大，也不能把“100→fit”永久等同于缩小。无变化行不产生虚假 range、动画或位置变化。

#### 后置条件

逐行关闭窗口并恢复 small-image、fit-limit 与 shortcut 设置；生成行级 trace。

## 11. 动态测试用例：P4 Toggle 在未 fit 时错误到 100%

### TC-TOGGLE-STATE-01

#### 测试目的

用视觉状态与 mode 标志的完整真值表验证 Toggle 决策，直接捕获“有滚动条却到 100%”。

#### 前置条件

可通过公开测试入口或仅测试构建的 friend fixture 独立设置 calculated mode 与 displayed geometry；不得直接篡改 expected predicate 的返回值。

#### 输入数据

| 数据行 | `atFit` | mode 标志 | 溢出轴 | 期望目标 |
| --- | ---: | --- | --- | --- |
| A | false | none/manual | H+V | fit |
| B | false | ZoomToFit | V only | fit |
| C | false | ZoomToFit | H only | fit |
| D | true | none/manual | none | 100% |
| E | true | ZoomToFit | none | 100% |
| F | false | FillWindow | H+V | fit |

B/C 可先进入 ZoomToFit，再以 `isApplyingCalculation=true` 的测试路径建立清晰的 mode/geometry 冲突；D 可在保持 fit zoom 不变时清除 mode。每行都先由独立 `fitsInside/atFit` oracle 证明前置状态。

#### 操作步骤

1. 建立数据行并断言 `atFit`、`fitsInside`、mode 和溢出轴精确匹配表格；D 行还要证明其倍率确实为 `zFit*`，而不是仅仅无条。
2. 发送一次真实 Toggle shortcut。
3. 立即读取 logical target，但用 displayed frame 判断触发时状态。
4. 跟踪动画、bar 和 geometry 至 terminal。
5. 比较目标、最终 `fitsInside/atFit` 状态与表格。

#### 预期结果

A/B/C/F 的目标均为独立 `zFit*`，绝不为 1.0（夹具保证两者相差至少 0.2）；D/E 的目标为精确 1.0。决策只随 `atFit` 列变化，不随 mode 列变化；A–F 最终均没有错误的条/位置副作用。

#### 后置条件

清除人为冲突状态并销毁窗口；测试构建 hook 不进入产品二进制。

### TC-TOGGLE-STATE-02

#### 测试目的

验证第一次“未 fit→fit”的动画尚未让图片真正适合窗口时，快速第二次 Toggle 仍继续/重算 fit，而不是因 mode 已先变为 ZoomToFit 就反向到 100%。

#### 前置条件

大图手动 200%，两轴 range 非零；fit level 小于 0.7；zoom animation 可观察；真实 Toggle shortcut 可重入。

#### 输入数据

第二次按键时点：首动画 `currentTime={0,1,50,199}` ms，且每行触发前都断言 `atFit=false`；另设首个 `atFit=true` 后的对照行。

#### 操作步骤

1. 第一次按 Toggle，确认 logical target=fit、mode 可已变更、displayed image 仍溢出。
2. 在指定时点发送第二次 Toggle。
3. 记录第二次触发瞬间的 visual state、mode 和新 target。
4. 自然运行至 terminal，检查 bar/图片。
5. 对照行在真正 `atFit` 后再按一次。

#### 预期结果

所有 `atFit=false` 行的新 target 仍为 `zFit*`，不出现朝 1.0 的反向帧；最终 `atFit=true` 且 H/V range 为零。只有 `atFit=true` 对照行选择 1.0。快速重复按键不会产生 A→B→A 的可见抖动。

#### 后置条件

等待所有重启动画和 generation 失活，关闭窗口并恢复 shortcut auto-repeat 设置。

### TC-TOGGLE-STATE-03

#### 测试目的

验证 resize、旋转、DPR/backing 切换造成 mode 与当前视觉几何短暂错位时，Toggle 仍按当前已提交画面判断。

#### 前置条件

ZoomToFit mode 已建立；可控制窗口尺寸、90° 旋转和 Expensive backing；event filter 能在每个已提交几何阶段触发测试动作。

#### 输入数据

`mutation={窗口缩小,窗口放大,旋转90°,Expensive backing替换}`；仅在独立 oracle 明确观测到 `atFit=false` 或 `true` 且与 mode 信息不一致的 committed sample 上触发 Toggle。

#### 操作步骤

1. 在稳定 fit 状态应用一种 mutation。
2. 记录每个 resize/layout/paint 的 geometry 与 mode。
3. 捕获明确的冲突 sample 后发送 Toggle；若该平台路径同步修复而从不产生冲突，则记录 `NO_COMMITTED_MISMATCH`，该行作为通过的防护证据而非跳过。
4. 验证 target 与该 sample 的 `atFit` 对应。
5. 等 terminal，检查无递归 layout 或条震荡。

#### 预期结果

若存在 committed mismatch，`false→fit`、`true→100%`；若不存在，trace 证明任何 paint 前已恢复一致。所有 mutation 终态有限收敛，不发生 fit/scrollbar resize 递归。

#### 后置条件

恢复窗口尺寸、旋转、DPR 进程和 scaling mode；销毁临时 backing。

## 12. 动态测试用例：P5 鼠标滚轮右下角缩放位置错误

### TC-WHEEL-REAL-01

#### 测试目的

按指定 JPEG 的“一格放大、鼠标在图片右下角”路径验证真实用户现象，确保放大后仍能在原鼠标位置看到同一个右下内容点。

#### 前置条件

现场 JPEG 存在且可解码；鼠标滚轮动作配置为 Zoom；窗口处于稳定 fit；cursor 可精确移动到映射图片右下角内侧 1 DIP；目标 1.25 倍几何已由 4.2 节证明该角点可达。

#### 输入数据

指定 3840×4407 JPEG；目标点 `mappedImageRect.bottomRight()-(1,1)`；单个 `angleDelta=(0,+120)`、NoScrollPhase；native/Retina 实际 DPR。

#### 操作步骤

1. 打开图片并等待 fit terminal；保存右下内侧点的 UV 和截图。
2. 向 viewport 发送一个真实 wheel event，校验 local/global position 一致且 event accepted。
3. 对 animation、bar layout、backing 替换和 paint 全程采样。
4. 在每个 sample 由当前 image rect 和 UV 重建 scene point。
5. 等 terminal 后再过陈旧回调观察窗。

#### 预期结果

每个 committed frame 的 UV 点都位于原目标 2 DIP 内，目标仍在 U 内；最终倍率等于一个离散步长的目标（默认 1.25 倍），图片右下内容未从视口消失；没有先正确后跳走的延迟帧。

#### 后置条件

关闭窗口且不修改原 JPEG；文件缺失时只跳过本现场层并给出明确原因。

### TC-WHEEL-UV-01

#### 测试目的

用带角点标志的 hermetic 图片对单格右下缩放作精确、可自动化的 UV 与像素双 oracle 验证。

#### 前置条件

`fixture-portrait` 已 fit；右下标志完整可见；cursor 在 `UV=(0.97,0.97)`，且放大后该锚点理论上可达；真实 wheel 构造器可指定 viewport-local position。

#### 输入数据

`anchorUV={(0.97,0.97),(1.0,1.0)}` × `scaling={Disabled,Bilinear,Expensive}` × `DPR={native,2}`；一个 +120 wheel notch。

#### 操作步骤

1. 记录 UV、目标 DIP/device-pixel、scene/image rect。
2. 发出 wheel event；同时验证只改变一次 logical target。
3. deterministic 逐毫秒推进并抓取阈值前、首次 bar 切换、动画终点、backing 替换后帧。
4. 新窗口 live replay，记录所有自然 paint。
5. 比较 UV 几何与角点标志的像素位置。

#### 预期结果

所有 frame 的 cursorError ≤2 DIP；像素标志中心误差 ≤1 device pixel（Bilinear/Expensive 可按色块质心计算）；两种回放的 terminal UV、zoom 和 bars 一致；换 backing 时不复用旧绝对 scene 坐标。

#### 后置条件

清理生成 PNG、截图和两个窗口；失败工件保留。

### TC-WHEEL-UV-02

#### 测试目的

验证滚轮锚点在不同位置和滚动条阈值组合下都正确，防止只修复右下角单一点或仅修复终态。

#### 前置条件

threshold fixtures 可提供无条、单轴条和双轴条起点；图片内外投影规则已由纯函数单元测试独立验证。

#### 输入数据

`position={center,left-middle,right-middle,top-middle,bottom-middle,四个角,viewport内图片外}` × `direction={in,out}` × `bars={none,H-only,V-only,both}`。核心 CI 采用 pairwise，四角与用户右下角行全量保留。

#### 操作步骤

1. 建立每行起点并证明目标点在图片内或得到唯一投影点。
2. 发出真实 wheel event并记录原 UV/投影点。
3. 观察首次 range change、viewport resize、animation endpoint、anchor settle 和 constraint endpoint。
4. 检查 anchor、bar 耦合状态和 scene rect。
5. 在同一位置快速 in→out，验证回到 baseline，无累积 rounding。

#### 预期结果

所有可达的图片内点保持精确 UV；不可达行落在 4.2 节独立求得的最近合法位置，图片外点保持最近边/角投影而不无故跳到中心；所有阈值切换中 cursorError 符合容差，bar 状态符合几何且无虚假 margin；in→out terminal 回到初始 transform/scene/anchor（允许 1 DIP 整数取整）。

#### 后置条件

逐行恢复 cursor、bar 起点和设置；删除生成夹具。

## 13. 测试实现与执行顺序

建议的分层顺序：

1. 纯函数/静态层：`fitsInside/atFit`、耦合 bar、UV 重建、方向到 anchor policy 的数据表，以及第 7 节五个静态用例。
2. QtTest hermetic 层：`TC-SB-*`、`TC-DRAG-HERMETIC-*`、`TC-KBD-*`、`TC-TOGGLE-*`、`TC-WHEEL-UV-*`。
3. 两个独立进程重复核心动态层：原生 DPR 与 `QT_SCALE_FACTOR=2`。
4. Cocoa 真实 event-loop/live replay 层，禁止暂停 animation/timer。
5. 本机现场层：`TC-DRAG-REAL-01`、`TC-WHEEL-REAL-01`。

每个 PR 的最低门禁是所有静态用例、所有 hermetic dynamic 行和两个 DPR 进程均通过。现场层用于发布前复核或缺陷关闭证据；它不能替代 hermetic 门禁。

## 14. 结论可核验性清单

只有同时满足以下条件，才能对五个问题分别写出“已修复”：

- 对应正向复现行在旧基线上至少一次失败，修复基线上通过；若旧基线无法复现，必须说明测试是在验证合同而非证明历史根因。
- 首个失败/修复 sample 可由 `trace.jsonl` 的时间、事件、expected/actual 和截图相互印证。
- 入口测试确实发送了用户所述键盘、wheel 或 drag 事件，而非只调用内部函数。
- transient deterministic scan 与 live replay 都通过，terminal 条件由状态收敛而非固定 sleep 判定。
- `fitsInside/atFit`、expected bars 和 anchor UV 均由独立 oracle 计算。
- 外置 JPEG 结果与同宽高比 hermetic fixture 结果一致；不一致时不得用 CI 结果覆盖现场证据。
- 静态测试能证明修复数据流存在，动态测试能证明行为正确，二者缺一不可。
