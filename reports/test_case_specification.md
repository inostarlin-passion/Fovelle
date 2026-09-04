# 测试用例说明：同步缩放与可行鼠标锚点

日期：2026-09-04
仓库：/Users/inostarlin/code/Fovelle
对应设计：[technical_design_document.md](technical_design_document.md)

## 1. 测试策略

测试从八条原子验收标准开始，分别覆盖：

- 静态测试：扫描生产头文件/实现，确认几何动画、displayed zoom 和延迟锚点
  writer 已删除，并确认报告、测试代码、CTest 注册可追溯。
- 动态测试：用 QtTest 发送真实 wheel、QTest::keySequence、标题栏菜单 action、
  右键菜单 action 和原生手势，检查同步提交、目标几何、scrollbar range/value、
  paint/resize 事件和静默窗口。
- 几何单元测试：不依赖窗口的纯投影函数覆盖图片内外点、图片放大和缩小、
  小图居中以及目标 viewport 边界。
- 交叉回归：现场 JPEG 可读时使用真实文件；不可读时使用同宽高比合成图。
  HiDPI、expensive scaling、RTL/旋转等既有矩阵保持执行。

每个结构化用例都有六个固定字段，并在“固化代码”列中指向实际测试函数或
Python 测试脚本。没有只写在文档中而未执行的用例。

## 2. 原子标准到用例追溯

| 原子标准 | 结构化测试用例 | 固化测试代码 |
| --- | --- | --- |
| AC-ZOOM-NO-ANIMATION-STATIC | TC-ZOOM-SYNC-ALL-ENTRY-POINTS、TC-STATIC-TRACEABILITY | zoom_scrollbar_duration_static.py、toggle_fit_stability_static.py |
| AC-ZOOM-NO-ANIMATION-INPUT | TC-ZOOM-SYNC-ALL-ENTRY-POINTS、TC-HBAR-FOUR-IN-ONE-OUT | testZoomTransitionCoversWheelKeyboardAndMenus、testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump |
| AC-ZOOM-NO-ANIMATION-SHORTCUT | TC-ZOOM-SYNC-ALL-ENTRY-POINTS、TC-KEYBOARD-CURSOR-ANCHOR | testZoomTransitionCoversWheelKeyboardAndMenus、testKeyboardZoomUsesCursorAnchor |
| AC-ZOOM-NO-ANIMATION-MENU | TC-ZOOM-SYNC-ALL-ENTRY-POINTS | testZoomTransitionCoversWheelKeyboardAndMenus |
| AC-ANCHOR-MOUSE-PREFERRED | TC-ANCHOR-FEASIBLE-PROJECTION、TC-KEYBOARD-CURSOR-ANCHOR、TC-TOGGLE-DIRECTIONAL-ANCHOR | testZoomAnchorProjectsInsideAndOutsideImage、testKeyboardZoomUsesCursorAnchor、testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor |
| AC-ANCHOR-PROJECT-FEASIBLE | TC-ANCHOR-FEASIBLE-PROJECTION、TC-HBAR-FOUR-IN-ONE-OUT | testZoomAnchorProjectsInsideAndOutsideImage、testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump |
| AC-ANCHOR-NO-POST-CORRECTION | TC-ZOOM-SYNC-ALL-ENTRY-POINTS、TC-FIT-QUIESCENT-TERMINAL | testZoomTransitionCoversWheelKeyboardAndMenus、testToggleFitReturnHasMonotonicStableTerminalSize |
| AC-ANCHOR-HBAR-TOPOLOGY | TC-HBAR-FOUR-IN-ONE-OUT、TC-HIDPI-ANCHOR-MATRIX | testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump、testZoomKeepsVerticalScrollbarTrajectoryStable |

## 3. 结构化测试用例

### TC-ZOOM-SYNC-ALL-ENTRY-POINTS

覆盖：AC-ZOOM-NO-ANIMATION-STATIC、AC-ZOOM-NO-ANIMATION-INPUT、
AC-ZOOM-NO-ANIMATION-SHORTCUT、AC-ZOOM-NO-ANIMATION-MENU。

#### 测试目的

验证鼠标 wheel、键盘 Toggle 快捷键、标题栏 View 菜单和右键 View 菜单的缩放
都没有几何过渡，并且共享同一个同步提交点。

#### 前置条件

Qt Cocoa 测试环境可创建可见 MainWindow；窗口加载 1200×900 raster；AsNeeded
滚动条和 smooth scaling 已按测试设置初始化；标题栏和右键 View 菜单均已物化；
Toggle action 绑定 Z。

#### 输入数据

一个真实 QWheelEvent；标题栏 View → Zoom In action；右键 View → Zoom In action；
一个通过 QTest::keySequence 发送的 Z；每个输入后观察 250ms 静默窗口。

#### 操作步骤

1. 打开图片并等待加载。
2. 确认 view 不存在名为 zoomTransitionAnimation 的 QObject，且
   isZoomTransitionRunning() 为 false。
3. 发送 wheel event，记录 zoom、H/V scrollbar value，等待 250ms 后复读。
4. 触发标题栏 clone 和右键 clone，各自记录同样数据。
5. 聚焦 viewport，通过 QTest::keySequence 发送 Toggle shortcut，再记录同样数据。

#### 预期结果

每个入口返回后 displayed/logical zoom 相等且没有运行中的几何 transition；
每次提交只产生目标状态，250ms 内 zoom、image rect 和 scrollbar value 不改变；
标题栏与右键菜单没有独立的第二套缩放行为。

#### 后置条件

窗口关闭；临时图片和 scoped settings/shortcuts 释放；无测试 timer 或 action
状态泄漏。

### TC-ANCHOR-FEASIBLE-PROJECTION

覆盖：AC-ANCHOR-MOUSE-PREFERRED、AC-ANCHOR-PROJECT-FEASIBLE。

#### 测试目的

验证鼠标是首选锚点，图片外点先裁到当前图片边界，并在目标图片尺寸/viewport
约束下投影到距离鼠标最近的可行锚点。

#### 前置条件

纯函数 QVGraphicsView::projectZoomAnchorForTarget 可调用；动态部分可以创建
可见窗口并加载 400×300 raster；小图在 1.0 倍时留有 viewport 空白。

#### 输入数据

纯函数的图片内点、左侧/右上/左下外点；动态部分从 1.0 倍开始，使用一个左侧
空白中的请求点和自适应目标 zoom，使目标图片溢出 viewport。

#### 操作步骤

1. 对图片内点执行投影，确认结果不变。
2. 对三个外点执行投影，确认每个轴独立裁到最近图片边界。
3. 打开动态 fixture，解析当前 image rect、viewport rect 和请求点。
4. 调用 zoomAbsolute(targetZoom, requestedPoint)，读取目标 image rect 与 viewport
   四条边。

#### 预期结果

纯函数结果等于逐轴 clamp；动态目标图片覆盖 usable viewport 的四条边，不因
图片外鼠标点制造可滚动空白；投影是确定性的，不把外点错误地吸到图片中心。

#### 后置条件

窗口、临时 raster 和设置释放；未写入任何持久图片或用户文件。

### TC-HBAR-FOUR-IN-ONE-OUT

覆盖：AC-ZOOM-NO-ANIMATION-INPUT、AC-ANCHOR-PROJECT-FEASIBLE、
AC-ANCHOR-NO-POST-CORRECTION、AC-ANCHOR-HBAR-TOPOLOGY。

#### 测试目的

复现报告中的“鼠标滚轮前进四格放大、回退一格缩小”，验证横向滚动条拓扑交叉
时没有缩放后的位置跳变。

#### 前置条件

Cocoa QtTest 可创建可见窗口；Fit、cursorzoom、AsNeeded scrollbars 和位置约束
开启；优先使用现场 JPEG
/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg，
否则使用 3840×4407 合成 raster。

#### 输入数据

Fit 状态下图片内偏右且非中心的鼠标点；三个 warm-up wheel +120；第四个
+120；随后一个 -120；记录 H/V range、value、resize、paint 和终态。

#### 操作步骤

1. 打开 fixture，等待 Fit 终态并把鼠标移到非中心图片点。
2. 发送三个 +120 warm-up，确认 V 有 range、H 无 range。
3. 安装 trace probe，记录 initial-fit。
4. 发送第四个 +120，记录 four-forward-terminal。
5. 发送一个 -120，记录 one-reverse-terminal。
6. 遍历 trace，检查 H range、交叉轴 Y anchor、溢出时 X anchor 和 quiet 状态。

#### 预期结果

H range 经过 0→非零→0；第四格后目标图片产生 H overflow，回退一格后
H range 消失；有 H range 的可行状态中鼠标锚点误差不超过 2 DIP，H bar 改变
viewport 高度的 Y anchor 误差不超过 2 DIP；没有后续 timer 造成平移。

#### 后置条件

所有缩放相关 writer 停止；窗口、probe、临时 fallback 和 scoped settings 释放；
现场 JPEG 只读且未被修改。

### TC-KEYBOARD-CURSOR-ANCHOR

覆盖：AC-ZOOM-NO-ANIMATION-SHORTCUT、AC-ANCHOR-MOUSE-PREFERRED。

#### 测试目的

验证 Zoom In/Out 的真实键盘快捷键沿用最近一次 viewport mouse position，而不是
无条件跳到 viewport center。

#### 前置条件

可见窗口加载 1600×900 raster；zoom to cursor 开启；Zoom In/Out shortcuts 已
注册；当前窗口处于有溢出的稳定 zoom。

#### 输入数据

图片内偏离中心的 cursor point；一次真实 Zoom In shortcut；恢复稳定 1.25 倍
后再发送一次真实 Zoom Out shortcut。

#### 操作步骤

1. 将鼠标移动到非中心图片点并记录其 scene point。
2. 用 QTest::keySequence 发送 Zoom In，立即检查 transition 状态并读取 scene 点
   的 viewport 映射。
3. 恢复 1.25 倍，重复移动鼠标、记录 scene point、发送 Zoom Out。
4. 等待固定的同步提交检查，不依赖动画时长。

#### 预期结果

两个快捷键均无几何动画；两次 scene anchor 映射与输入点的误差不超过 2 DIP；
logical/displayed zoom 一致。

#### 后置条件

窗口和临时图片关闭；快捷键、设置、cursor 和滚动条状态恢复。

### TC-TOGGLE-DIRECTIONAL-ANCHOR

覆盖：AC-ANCHOR-MOUSE-PREFERRED、AC-ANCHOR-PROJECT-FEASIBLE。

#### 测试目的

验证 Toggle Fit and 100% 的放大端保持 cursor anchor，缩小端使用 usable viewport
center，不会把移动后的 cursor 错当成中心。

#### 前置条件

可见窗口加载 1600×900 raster；图片已稳定 Fit；Toggle action 可用；位置约束和
cursor zoom 开启。

#### 输入数据

Fit 图片中的非中心 cursor point；一次 Fit→100% action；把 cursor 移到左上区域；
再执行一次 100%→Fit action。

#### 操作步骤

1. 记录非中心 cursor 对应的 scene point并触发第一次 Toggle。
2. 读取 100% 目标映射，检查 cursor anchor。
3. 执行 centerImage，移动 cursor，记录 usable center 对应的 scene point。
4. 触发第二次 Toggle，读取最终 usable center、zoom 和 scrollbar range。

#### 预期结果

放大端 cursor anchor 误差不超过 2 DIP；缩小端记录的 center scene point 回到
最终 usable center，误差不超过 2 DIP；最终为 Fit 且 H/V range 为零。

#### 后置条件

action、cursor、窗口、临时图片和 settings 恢复。

### TC-TOGGLE-FROZEN-CENTER-ANCHOR

覆盖：AC-ANCHOR-PROJECT-FEASIBLE、AC-ANCHOR-NO-POST-CORRECTION。

#### 测试目的

验证 100%→Fit 的 center sentinel 在请求开始只解析一次，即使 H bar 消失并改变
viewport 几何，也不会重新读取新的 center。

#### 前置条件

可见窗口加载 1600×2200 portrait raster；100% 状态下 H/V 均有 range；Toggle
action 可用；生产实现没有 zoomTransitionAnimation、zoomAnchorSettleTimer。

#### 输入数据

100% stable frame 的 usable center viewport point 及其 scene point；一次
100%→Fit Toggle；H 无 range、V 仍有 range 的布局边界。

#### 操作步骤

1. 在 100% frame 调用 centerImage 并记录 center scene point。
2. 触发 Toggle，立即检查无 transition、无动画对象、无 anchor settle timer。
3. 等待同步布局完成，读取最终 Fit 的 image/viewport、H/V range 和 center scene
   point 映射。

#### 预期结果

中心锚点只在一次提交中被使用；最终 Fit 的 center scene point 映射误差不超过
2 DIP；H/V range 均为零，且没有延迟位置修正。

#### 后置条件

窗口关闭；timer、action、设置和临时图片释放。

### TC-FIT-QUIESCENT-TERMINAL

覆盖：AC-ZOOM-NO-ANIMATION-SHORTCUT、AC-ANCHOR-NO-POST-CORRECTION。

#### 测试目的

验证 Fit→100%→Fit 往返没有动画完成后的二次 rescale 或平移。

#### 前置条件

可见窗口加载 2560×2938 合成图；如果环境存在现场文件，则追加 3840×4407
provided row；Fit 已稳定；Z 绑定 Toggle shortcut。

#### 输入数据

两次真实 QTest::keySequence Z；100% 起始 image size；Fit 参考 image size；
终态 image size、H/V value；终态后的 650ms quiet window。

#### 操作步骤

1. 从 Fit 发送 Z 到 100%，确认两轴有 range。
2. 再发送 Z 回 Fit，立即检查 no transition、目标 zoom、Fit 和 H/V range。
3. 记录 terminal image size、scroll values，等待 650ms 后重新读取。
4. 比较参考 Fit、terminal 和 quiet 三组 image size 以及 H/V values。

#### 预期结果

图片缩小提交只有一个可见终态；reference/terminal/quiet image size 相等；
650ms 内 H/V values 不变；signal 只记录一次目标 zoom 写入；不存在 position
rebound。

#### 后置条件

窗口和临时 fixture 释放；所有相关 timer 停止；provided JPEG 未修改。

### TC-HIDPI-ANCHOR-MATRIX

覆盖：AC-ANCHOR-HBAR-TOPOLOGY、AC-ANCHOR-NO-POST-CORRECTION。

#### 测试目的

验证 expensive backing、keyboard/wheel/pinch 三类输入以及 QT_SCALE_FACTOR=2
下，归一化图片坐标不会使 V scrollbar 或 anchor 发生二次跳变。

#### 前置条件

QtTest 可创建窗口；测试矩阵依次启用 Disabled/Expensive scaling、keyboard/wheel/
pinch；每个 row 使用稳定的 V-only 或 H+V 初始布局。

#### 输入数据

1×4096 探测图自动生成的 V-only fixture；根据实际 viewport 生成的动态 raster；
固定图片 UV 点；一次 zoom in 或 zoom out；普通和 QT_SCALE_FACTOR=2 两种运行。

#### 操作步骤

1. 对每个 scaling/input/zoom direction row 建立稳定 baseline。
2. 发送真实 input，检查 zoom signal count、logical/displayed equality 和无动画。
3. 停止所有延迟 timer，记录 immediate-terminal trace。
4. 比较 V range/value、bar geometry、image UV anchor 与目标值。

#### 预期结果

12 个普通矩阵和 HiDPI 矩阵均通过；没有 geometric animation；expensive backing
替换不会把旧 scene coordinate 误当成新 coordinate；anchor/scrollbar geometry
误差在测试定义的 2 DIP/整数舍入范围内。

#### 后置条件

窗口、trace、动态 fixture 和设置释放；不修改用户图片。

### TC-STATIC-TRACEABILITY

覆盖：AC-ZOOM-NO-ANIMATION-STATIC 及全部原子标准的文档追溯。

#### 测试目的

静态确认生产合同、结构化用例、测试源码、CTest 注册和完成报告形成闭环。

#### 前置条件

Python 3、源码、CTest 构建目录以及三份 Markdown 文件存在；不要求现场卷或
真实显示器。

#### 输入数据

src/qvgraphicsview.{h,cpp}、src/mainwindow.cpp、tests/tst_qviewtests.cpp、
tests/CMakeLists.txt、两个 static Python gate 和三份 reports 文件。

#### 操作步骤

1. 执行 zoom_scrollbar_duration_static.py 并写出 JSON 结果。
2. 执行 toggle_fit_stability_static.py 并写出 JSON 结果。
3. 检查每个原子 ID 在技术设计、测试说明、测试源码和完成报告中出现。
4. 检查每个 case 的六个字段和每个动态函数的 CTest 注册。

#### 预期结果

两个 Python 进程退出码为 0，JSON passed=true；不存在生产几何动画 marker；
每个原子标准都有可执行测试或静态证据，且六字段完整。

#### 后置条件

只生成 build/test-results 下的机器可读 JSON，不修改生产源码、用户设置或输入
图片。

## 4. 固化代码与 CTest 编排

| 结构化用例 | 测试代码 | CTest |
| --- | --- | --- |
| TC-ZOOM-SYNC-ALL-ENTRY-POINTS | GraphicsViewTests::testZoomTransitionCoversWheelKeyboardAndMenus | FovelleScrollbarZoomDurationAcceptance、FovelleTests |
| TC-ANCHOR-FEASIBLE-PROJECTION | GraphicsViewTests::testZoomAnchorProjectsInsideAndOutsideImage | FovelleScrollbarZoomDurationAcceptance、FovelleTests |
| TC-HBAR-FOUR-IN-ONE-OUT | GraphicsViewTests::testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump | FovelleScrollbarZoomDurationAcceptance |
| TC-KEYBOARD-CURSOR-ANCHOR | GraphicsViewTests::testKeyboardZoomUsesCursorAnchor | FovelleFiveIssueZoomAcceptance、FovelleTests |
| TC-TOGGLE-DIRECTIONAL-ANCHOR | GraphicsViewTests::testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor | FovelleToggleFitAnchorAcceptance |
| TC-TOGGLE-FROZEN-CENTER-ANCHOR | GraphicsViewTests::testToggleFitAnd100FreezesViewportCenterDuringScrollbarTransition | FovelleToggleFitAnchorAcceptance |
| TC-FIT-QUIESCENT-TERMINAL | GraphicsViewTests::testToggleFitReturnHasMonotonicStableTerminalSize | FovelleToggleFitStabilityAcceptance |
| TC-HIDPI-ANCHOR-MATRIX | GraphicsViewTests::testZoomKeepsVerticalScrollbarTrajectoryStable | FovelleZoomScrollbarTrajectory、FovelleZoomScrollbarTrajectoryHiDpi |
| TC-STATIC-TRACEABILITY | 两个 static Python gate | FovelleZoomScrollbarDurationStatic、FovelleToggleFitStabilityStatic |

## 5. 测试执行命令

构建：

    cmake -S . -B build
    cmake --build build --parallel 4

静态：

    ctest --test-dir build -R 'Fovelle(ToggleFitStability|ZoomScrollbarDuration)Static' --output-on-failure

专项动态：

    ctest --test-dir build -R 'Fovelle(ScrollbarZoomDuration|ToggleFitAnchor|ToggleFitTrajectory|ToggleFitStability)Acceptance' --output-on-failure

全量：

    ctest --test-dir build --output-on-failure

所有命令的实际结果和环境说明写入
[reports/test_completion_report.md](test_completion_report.md)。
