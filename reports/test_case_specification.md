# Fovelle 图片缩放、拖拽与滚动条：测试用例说明

> 文档日期：2026-09-03
> 测试代码：[tests/tst_qviewtests.cpp](../tests/tst_qviewtests.cpp)
> 静态门禁：[tests/zoom_issue_acceptance_static.py](../tests/zoom_issue_acceptance_static.py)
> 设计依据：[reports/technical_design_document.md](technical_design_document.md)
> 根因依据：[reports/root_cause.md](root_cause.md)

## 1. 原子验收标准（原子化拆解）

五个用户问题拆成八条原子验收标准。`AC-ALL` 只有在八条均通过时才通过：

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

| 原子标准 | 可证伪判定 | 问题来源 |
| --- | --- | --- |
| `AC-SB-NO-STALE-RANGE` | 在 displayed frame 已回到 fit 且异步 writer quiet 后，图片小于 usable viewport 的轴不再有非零 range；额外 event-loop turn 不得复活。 | P1 |
| `AC-DRAG-CONTINUOUS` | 真实拖拽中，tracked scene point 的每步 viewport 位移与 pointer delta 相等，误差≤2 DIP；边界只允许 scrollbar 合法钳制。 | P2 |
| `AC-DRAG-PRESERVES-OVERFLOW-BARS` | 当前图片在某轴真实溢出时，开始拖拽和取消旧 zoom anchor 不得使该轴 range 变成零或改变到非预期端点。 | P2 |
| `AC-KBD-ZOOM-CURSOR-ANCHOR` | 真实键盘 `Zoom In`、`Zoom Out` 触发后，鼠标下同一图片 scene 点仍位于同一 cursor viewport 点，误差≤2 DIP。 | P3 |
| `AC-TOGGLE-DIRECTIONAL-ANCHOR` | Toggle 放大时使用 cursor anchor；Toggle 缩小时使用 usable viewport center anchor。 | P3/P4 |
| `AC-TOGGLE-VISUAL-STATE` | 当前 displayed frame 未 fit 时 Toggle 选择 fit；只有已 fit 时才选择 100%，不能以逻辑 mode 提前值替代实际状态。 | P4 |
| `AC-WHEEL-CONTENT-ANCHOR` | 真实 wheel 放大后，鼠标右下角对应的归一化图片内容点保持可达并留在目标 viewport 点附近。 | P5 |
| `AC-NO-LATE-REWRITE` | 动画、anchor settle、post-layout、expensive scaling、constraint 和 scrollbar writer 全部完成后，正确终态 tuple 不再改变。 | P1–P5 |

## 2. 测试层级、时态与共同 oracle

### 2.1 覆盖类型

| 层级 | 代码 | 目的 | 时态 |
| --- | --- | --- | --- |
| 静态测试 | `FovelleZoomIssueStatic` → `zoom_issue_acceptance_static.py` | 检查生产源码合同、原子标准映射、真实输入入口、CTest 注册和文档六字段。 | 稳态合同 |
| 动态测试 | `FovelleFiveIssueZoomAcceptance` → 七个 QtTest 函数 | 在 Cocoa QPA 创建窗口、加载生成图片、发送真实 Qt 事件并观测状态。 | 瞬态 + 稳态 |
| 既有轨迹回归 | `FovelleZoomScrollbarTrajectory`、`...HiDpi` | 逐动画毫秒扫描和真实 event-loop 回放，覆盖 keyboard/wheel/native pinch、Disabled/Expensive、普通 DPR/HiDPI。 | 瞬态 + 稳态 |

拖拽和 Toggle 各有两个原子标准，但输入事务相同；同一 QtTest 函数中有彼此独立的断言区块和两个原子 marker，不把一个 oracle 的通过当作另一个 oracle 的通过。

### 2.2 共同前置条件和判定约束

- 测试运行于 macOS Cocoa；默认构造 640×480 可见窗口，使用临时生成 raster，避免依赖外置卷。
- 统一设置 `windowresizemode=Never`、`fitoverscan=0`、关闭 one-to-one 小图策略和不确定的 smooth scaling；需要时显式选择 `Disabled` 或 `Expensive`。
- `waitForZoomTerminal()` 以 animation 和 `zoomAnchorSettleTimer`、`zoomAnchorPostLayoutTimer`、`constrainBoundsTimer`、`expensiveScaleTimer`、`verticalScrollBarGeometryTimer` 全部 inactive 为终态条件。
- 图片内容点用 `mapToScene()` 得到 scene anchor，再用当前 `mapFromScene()` 观察；不从 actual scrollbar value 反推 expected anchor。
- 滚动范围用 `maximum > minimum` 判定；geometry 用 viewport/item 的 DIP 坐标，锚点误差容差为 2 DIP。
- AsNeeded bar 出现/隐藏会改变 viewport 尺寸，因此每次比较同时重新读取 usable viewport，不使用固定 scrollbar 厚度。

## 3. 结构化测试用例

### TC-SB-ZOOMOUT-ATOMIC

- 测试目的：验证问题 P1；清除放大历史留下的虚拟 margin，确保图片缩小后垂直滚动条不滞留。
- 前置条件：可见 640×480 Cocoa 窗口；1600×900 生成图片已加载；两轴为 `ScrollBarAsNeeded`；fit 已稳定；cursor zoom 开启。
- 输入数据：视口右下方图片外点；真实 wheel `+120` 三次、真实 wheel `-120` 三次；测试函数 `testZoomOutClearsStaleVerticalScrollRange`。
- 操作步骤：先设置 fit 并等待 terminal；在图片外点发送三格放大并确认垂直 range 非零；发送三格反向缩小；等待 animation、anchor、constraint、geometry writer 全部 quiet；再处理两个 event-loop turn。
- 预期结果：最终 mapped image 的宽高均不超过 usable viewport 加 1 DIP；H/V range 均为零；额外 event-loop turn 后四个 range 边界 tuple 不改变。
- 后置条件：关闭窗口；临时文件、cursor 状态和 scoped settings 自动释放；无测试 timer 保持 active。

### TC-DRAG-CONTINUITY-ATOMIC

- 测试目的：验证问题 P2 的连续性原子标准，排除拖拽首帧/坐标基切换造成的图片跳变。
- 前置条件：可见窗口中加载 1600×900 图片；先 fit，再发送三格放大使图片两轴真实溢出；viewport drag action 为 `Pan`。
- 输入数据：视口中心 press 点；鼠标从该点移动 `QPoint(32,24)`；真实 `QTest::mousePress/mouseMove/mouseRelease`；测试函数 `testMousePanKeepsOverflowRangeAndContinuity`。
- 操作步骤：记录 press 前 tracked scene point 的 viewport 坐标；发送真实 move；立即比较 tracked point 与 pointer delta；随后等待 terminal 并再次比较。
- 预期结果：释放瞬间 tracked point 的位移与 `(32,24)` 一致，误差≤2 DIP；不存在由 cancel anchor 或 viewport resize 引起的额外跳变；quiet 后位置与释放瞬间误差≤1 DIP。
- 后置条件：释放鼠标按钮并关闭窗口；scoped drag option 恢复；无 pending pan 输入残留。

### TC-DRAG-OVERFLOW-ATOMIC

- 测试目的：验证问题 P2 的溢出范围原子标准，确保取消旧 zoom anchor 不会让仍然真实溢出的水平/垂直滚动条消失。
- 前置条件：与 `TC-DRAG-CONTINUITY-ATOMIC` 相同；放大完成后 H/V range 均严格非零。
- 输入数据：相同的三格真实 wheel-in 和 `(32,24)` 的真实 drag；测试函数中的独立 range 断言。
- 操作步骤：记录 drag 前 H/V minimum、maximum；发送 press/move/release；在 release 后立即读取范围；等待 terminal 后再次读取范围和 maximum。
- 预期结果：拖拽开始、释放和 quiet 三个观察点的 H/V range 都保持非零；maximum 的变化≤2；不得出现“清除 margin→range 归零→Qt alignment 重置”的中间状态。
- 后置条件：关闭窗口，恢复设置和输入状态；不保留 pending anchor timer。

### TC-KBD-ZOOM-ATOMIC

- 测试目的：验证问题 P3；确保 QAction 触发的键盘 `Zoom In` 与 `Zoom Out` 不再固定以视口中心缩放。
- 前置条件：可见窗口加载 1600×900 图片并稳定 fit；为 `zoomin`/`zoomout` 配置确定性的 Ctrl+=/Ctrl- shortcut；cursor 位于 viewport 内的非中心图片点。
- 输入数据：非中心 cursor viewport 点；真实 `QTest::keySequence()` 发送 Zoom In；回到 1.25 倍已知溢出帧后，再发送 Zoom Out。
- 操作步骤：发送 mouse-move 记录 cursor 下 scene anchor；发送 Zoom In shortcut 并等待 terminal；检查 anchor 映射；重新定位相同 cursor 点并记录第二个 scene anchor；发送 Zoom Out shortcut 并等待 terminal；再次检查。
- 预期结果：两个方向的同一 scene 内容点都回到各自触发时的 cursor 点，误差≤2 DIP；实现可使用最近 mouse event 或合法全局 cursor，不能无条件替换为中心。
- 后置条件：shortcut scope、临时图片、窗口和 cursor 输入状态恢复；无 active zoom writer。

### TC-TOGGLE-DIRECTIONAL-ATOMIC

- 测试目的：验证问题 P3/P4 的方向锚点原子标准：Toggle 放大用 cursor，缩小用 usable viewport center。
- 前置条件：可见窗口加载 1600×900 图片并稳定 fit；`togglefitand100` action 可取；cursor 先置于非中心图片点。
- 输入数据：fit→100% Toggle 一次；将 cursor 移到远离中心的位置；记录 usable center 下 scene point；100%→fit Toggle 一次。
- 操作步骤：发送第一个 action，等待 terminal 并记录 cursor anchor；调用 `centerImage()` 建立可重复的 content center，再发送 mouse move 到非中心位置；记录 usable center scene anchor；发送第二个 action并等待实际 fit。
- 预期结果：fit→100% 的 cursor anchor 误差≤2 DIP；100%→fit 的 center anchor 误差≤2 DIP，即使 cursor 已移开中心也不改用 cursor；最终 H/V range 为零。
- 后置条件：关闭窗口，恢复 action、cursor 和 settings；不遗留 delayed anchor。

### TC-TOGGLE-VISUAL-ATOMIC

- 测试目的：验证问题 P4 的实际显示状态原子标准，防止逻辑 mode 已写成 fit 但动画帧尚未 fit 时误跳 100%。
- 前置条件：与方向锚点用例相同；先让 fit action 启动 200 ms transition，并在其显示帧尚未达到 fit 时再次触发 Toggle。
- 输入数据：首次 Toggle 后的 `calculatedZoomMode`、`getZoomLevel()`、`animatedZoomLevel()`、`isImageAtFit()`；动画期间的第二次 Toggle；随后稳定的 fit→100→fit 序列。
- 操作步骤：触发首次 Toggle，立即在动画窗口读取 mode 与 displayed state；再次触发 Toggle；等待 terminal；再按已 fit、100%、fit 的顺序触发并验证目标。
- 预期结果：动画中 `isImageAtFit()==false` 时第二次 Toggle 仍选择 fit，不跳到 100%；只有 displayed zoom 等于独立 fit level 且 H/V range 为零时才选择 100%；最终循环可收敛到 fit。
- 后置条件：等待所有 transition 和 timer 结束；关闭窗口；恢复 settings。

### TC-WHEEL-REAL-ATOMIC

- 测试目的：验证问题 P5；用真实鼠标 wheel 检查图片右下角锚点，捕获重复 transform 或 scrollbar threshold relayout 导致的错位。
- 前置条件：可见窗口加载 1200×900 图片并稳定 fit；viewport vertical action 为 Zoom；cursor zoom 开启；图片右下角在 fit 帧可见。
- 输入数据：图片右下角内缩 5 DIP 的 viewport 点；一个真实 `QWheelEvent`，angle delta 为 `+120`；测试函数 `testMouseWheelKeepsBottomRightAnchor`。
- 操作步骤：记录目标点对应 scene anchor；将 mouse-move/wheel 事件发送到 viewport；等待 animation、settle、post-layout、constraint 和 geometry writer 完成；读取 mapped anchor 与 image edge。
- 预期结果：anchor 映射距原始 wheel 点≤2 DIP；mapped image right/bottom 仍覆盖目标邻域；不得因为 `mapFromScene()` 输入重复变换而把右下角推到 viewport 外。
- 后置条件：关闭窗口；确认 wheel action 没有 active timer；恢复 scoped settings。

### TC-ASYNC-QUIET-ATOMIC

- 测试目的：验证五个问题的共同时序标准，排除“terminal 看似正确、延迟 callback 后又重写”的迟到状态。
- 前置条件：可见窗口加载 1200×900 Qt raster；启用 Expensive scaling；所有 zoom 相关 timer 已由 helper 可观测；窗口保持固定大小。
- 输入数据：右下角真实 wheel 放大；terminal tuple 包含 logical/displayed zoom、transform、mapped image rect、viewport rect、H/V value/max；测试函数 `testZoomTerminalStateDoesNotRewriteViewport`。
- 操作步骤：等待初始 fit；发送真实 wheel；`waitForZoomTerminal()` 返回后记录 tuple；处理两个 event-loop turn，再等待 650 ms 并处理事件；重新读取 tuple 和全部 timer 状态。
- 预期结果：两次 tuple 完全相等；`zoomAnchorSettleTimer`、`zoomAnchorPostLayoutTimer`、`constrainBoundsTimer`、`expensiveScaleTimer`、`verticalScrollBarGeometryTimer` 均 inactive；不得有迟到的 scene/range/anchor rewrite。
- 后置条件：关闭窗口；等待异步 loader/scale 工作退出；临时资源释放。

### TC-STATIC-TRACEABILITY

- 测试目的：验证本需求的静态可追溯性，保证原子标准、生产实现、结构化用例、测试代码和 CTest 不脱节。
- 前置条件：仓库源码、测试源码、三份报告和 Python 3 interpreter 可读；不启动 GUI，不依赖外部图片卷。
- 输入数据：`tests/zoom_issue_acceptance_static.py --repo <repo> --output <json>`；源码 marker、测试函数、CTest 条目、Markdown case 和 Qt 官方链接。
- 操作步骤：读取并匹配八条 `AC-*`；检查 P1–P5 实现标志和真实输入入口；检查九个 case 的六个字段；检查静态/动态、瞬态/稳态词项和 CTest 注册；写出 JSON 证据。
- 预期结果：所有 check 的 `pass` 为 true；静态测试返回码为 0；输出 JSON 可作为完成报告的机器证据。
- 后置条件：只写入指定 JSON 结果文件；不修改产品源码、测试源码、用户 settings 或外部系统状态。

## 4. 输入矩阵与证据保存

| 维度 | 覆盖值 |
| --- | --- |
| 输入入口 | real `QWheelEvent`、`QTest::keySequence`、real mouse press/move/release；既有轨迹另含 `QNativeGestureEvent` |
| 缩放方向 | 放大、缩小；fit→100、100→fit |
| scaling | `SmoothScalingMode::Disabled`、`Expensive` |
| 锚点 | 非中心 cursor、右下角、usable viewport center、图片外投影点 |
| 图像 | 生成 raster 1600×900、1200×900；现场 JPEG 作为人工复现路径，不是 CI 前提 |
| 时间 | 真实 200 ms transition、settle/post-layout/constraint/expensive timer；既有轨迹逐 integer millisecond |
| DPR | 默认 Cocoa；独立 `QT_SCALE_FACTOR=2` CTest |
| 稳态 | terminal helper、两个 quiet event-loop turn、650 ms 延迟窗口；比较 tuple/range 不变 |

失败时，QtTest 断言应保留最早违反原子标准的输入阶段、anchor/range/geometry 数值和 timer 状态；不能只报告最终 zoom。
