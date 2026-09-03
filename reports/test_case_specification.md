# 测试用例说明：滚动条阈值与自适应缩放时长

日期：2026-09-04
被测实现：`src/qvgraphicsview.{h,cpp}`

## 1. 覆盖矩阵

| 原子验收标准 | 结构化用例 | 静态 | 动态 |
| --- | --- | :---: | :---: |
| `AC-HBAR-01-ROUND-TRIP` | `TC-HBAR-FOUR-IN-ONE-OUT` | ✓ | ✓ |
| `AC-HBAR-02-ANCHOR-CONTINUITY` | `TC-HBAR-FOUR-IN-ONE-OUT` | ✓ | ✓ |
| `AC-DURATION-01-LOG-DISTANCE` | `TC-DURATION-LOG-DISTANCE` | ✓ | ✓ |
| `AC-DURATION-02-FIXED-STEP` | `TC-DURATION-FIXED-STEP` | ✓ | ✓ |
| `AC-STATIC-01-TRACEABILITY` | `TC-STATIC-TRACEABILITY` | ✓ | — |

动态用例既记录 animation/range/value/resize/paint/timer 事件，也在动画结束和静默窗口后复核终态；静态用例验证源码合同、测试代码、CTest 注册和报告追溯。

## 2. 结构化测试用例

### TC-HBAR-FOUR-IN-ONE-OUT

覆盖：`AC-HBAR-01-ROUND-TRIP`、`AC-HBAR-02-ANCHOR-CONTINUITY`

**测试目的**：验证用户报告的“打开竖幅图片，连续放大 4 格，再缩小 1 格”在 H range 出现和消失时没有可见位置跳变。

**前置条件**：Cocoa QtTest 窗口可显示；H/V 策略为 `ScrollBarAsNeeded`；默认缩放倍率为 1.25；图片从 Fit 开始；现场 JPEG 可读时使用现场文件，否则使用 `3840×4407` 同比例合成图。

**输入数据**：`/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg`（若存在）或合成 `3840×4407` raster；窗口 `1000×550`；3 次预热 `+120`、第 4 次 `+120`、反向 `-120`。

**操作步骤**：

1. 打开图片，等待加载、Fit 动画和延迟 writer 完成。
2. 连续发送 3 个真实离散 wheel detent，每格等待结算；确认 V range 非零而 H range 为零。
3. 在 usable viewport 中心记录固定 scene anchor，安装 `ZoomIssueProbe`。
4. 发送第 4 个 `+120` wheel，等待动画/布局/timer 稳定，记录 `four-forward-terminal`。
5. 发送第 1 个 `-120` wheel，等待稳定，记录 `one-reverse-terminal`。
6. 遍历所有 range/layout/paint/animation/timer 样本，识别 H range `0→非零→0` 并检查可见帧 anchor。

**预期结果**：第 4 格结束 H range 非零；反向 1 格结束 H range 归零；轨迹实际包含两个拓扑转换；paint/terminal 可见 anchor 误差不超过 8 DIP，其中允许值仅覆盖 H 条改变 usable 高度造成的可预测半厚度中心变化；不存在未完成状态被当成可见 frame。

**后置条件**：动画和全部已知 timer 停止；窗口关闭；合成文件由 `QTemporaryDir` 回收，现场 JPEG 不被修改。

### TC-DURATION-LOG-DISTANCE

覆盖：`AC-DURATION-01-LOG-DISTANCE`

**测试目的**：验证语义缩放跳转按对数缩放距离而不是百分点差计算自适应时长。

**前置条件**：`QVGraphicsView::zoomTransitionDurationMs` 可被无 GUI 调用。

**输入数据**：`1→1.25`、`0.5→1`、`0.25→1`、`0.1→1`、`0.25→0.5`、`1→2`。

**操作步骤**：调用纯函数，分别读取固定模式和自适应模式结果；比较等倍率距离、单调性、上限和边界。

**预期结果**：自适应时长分别为约 `232ms`、`300ms`、`400ms`、`400ms`；`0.25→0.5` 与 `1→2` 时长相等；任何自适应结果在 200–400ms；固定模式始终为 200ms。

**后置条件**：无 GUI、设置或文件状态改变。

### TC-DURATION-FIXED-STEP

覆盖：`AC-DURATION-02-FIXED-STEP`

**测试目的**：验证鼠标滚轮、键盘和菜单 Zoom In/Out 的单步缩放仍为 200ms，同时 Fit/100% 语义跳转使用自适应时长。

**前置条件**：可见窗口加载 `1200×900` raster；animation 对象存在；菜单 clone 和 Toggle Fit and 100% action 已创建。

**输入数据**：一个真实 wheel detent、标题栏 Zoom In、右键菜单 Zoom In、Toggle Fit and 100% action。

**操作步骤**：逐一触发入口，观察 `QPropertyAnimation::duration()` 和 in-flight/terminal frame；对单步入口要求精确 200ms，对 Toggle 要求大于 200ms 且不超过 400ms。

**预期结果**：前三类单步入口均为 200ms；Toggle Fit/100% 是语义跳转，duration 在 `(200, 400]`；所有入口仍有中间帧并精确到达 logical target。

**后置条件**：动画停止；窗口和临时图片释放；测试设置恢复。

### TC-STATIC-TRACEABILITY

覆盖：`AC-STATIC-01-TRACEABILITY`

**测试目的**：静态验证每条原子标准都有生产实现、结构化用例、测试代码、CTest 注册和完成报告证据。

**前置条件**：Python 3 可用；仓库包含源码、测试、CTest 和三份 Markdown 报告；不要求显示器或现场卷。

**输入数据**：`src/qvgraphicsview.{h,cpp}`、`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`、`tests/zoom_scrollbar_duration_static.py` 和三份报告。

**操作步骤**：执行 `python3 tests/zoom_scrollbar_duration_static.py --repo . --output build/test-results/zoom-scrollbar-duration-static.json`；脚本检查 duration/atomic-frame markers、动态测试 markers、五个 AC ID、六个结构化字段和两个 CTest 名称。

**预期结果**：进程退出码为 0；JSON 的 `passed=true`；所有检查项 PASS；静态结果可作为 CTest 门禁产物。

**后置条件**：仅生成机器可读 JSON；不修改生产源码、设置或测试输入。

## 3. 测试代码固化位置

| 用例 | 测试代码 | CTest |
| --- | --- | --- |
| `TC-HBAR-FOUR-IN-ONE-OUT` | `GraphicsViewTests::testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump` | `FovelleScrollbarZoomDurationAcceptance` |
| `TC-DURATION-LOG-DISTANCE` | `GraphicsViewTests::testZoomTransitionDurationUsesLogDistance` | `FovelleScrollbarZoomDurationAcceptance` |
| `TC-DURATION-FIXED-STEP` | `GraphicsViewTests::testZoomTransitionCoversWheelKeyboardAndMenus` | `FovelleTests` |
| `TC-STATIC-TRACEABILITY` | `tests/zoom_scrollbar_duration_static.py` | `FovelleZoomScrollbarDurationStatic` |
