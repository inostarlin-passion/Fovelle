# 测试完成报告：同步图片缩放与可行锚点

日期：2026-09-04
仓库：/Users/inostarlin/code/Fovelle
构建目录：/Users/inostarlin/code/Fovelle/build
设计依据：[technical_design_document.md](technical_design_document.md)
用例依据：[test_case_specification.md](test_case_specification.md)

## 1. 完成结论

生产代码、原子验收拆解、结构化测试用例、可执行测试代码、静态门禁和 CTest
编排均已完成。验证结果以 Qt 6.11.1、macOS Cocoa QPA、当前构建目录和本机
可读现场 JPEG 为依据；现场文件只读。

结论范围是 Qt/Fovelle widget 层的可观察提交边界：transform、scene rect、
viewport、scrollbar range/value、paint、resize 和 timer。没有进行
WindowServer/CALayer presentation-layer 逐帧捕获，因此报告不把结论扩大为
macOS 合成器屏幕扫描级证明。

## 2. 原子验收结果

| ID | 结果 | 固化证据 |
| --- | :---: | --- |
| AC-ZOOM-NO-ANIMATION-STATIC | PASS | 两个 Python 静态 gate 检查生产头/实现不存在几何 QPropertyAnimation、displayed zoom、pending anchor 和缩放结算 timer，并检查 ZoomPlan/单一 commit。 |
| AC-ZOOM-NO-ANIMATION-INPUT | PASS | testZoomTransitionCoversWheelKeyboardAndMenus 真实发送 wheel；即时状态和 250ms quiet window 通过；全量 QtTest 通过。 |
| AC-ZOOM-NO-ANIMATION-SHORTCUT | PASS | testZoomTransitionCoversWheelKeyboardAndMenus、testKeyboardZoomUsesCursorAnchor 和 Toggle 专项使用 QTest::keySequence；无运行 transition。 |
| AC-ZOOM-NO-ANIMATION-MENU | PASS | 同一入口测试物化标题栏 View clone 和右键 View clone，并验证两者均落到同步 view API。 |
| AC-ANCHOR-MOUSE-PREFERRED | PASS | testZoomAnchorProjectionIsNearestFeasible、键盘 cursor anchor 和 Toggle 定向锚点断言通过。 |
| AC-ANCHOR-PROJECT-FEASIBLE | PASS | 纯投影函数覆盖内外点；动态测试确认目标 image 覆盖 usable viewport 且不产生可避免空白。 |
| AC-ANCHOR-NO-POST-CORRECTION | PASS | 现场四进一退、终态 quiet window、timer inactive 和 scroll value 稳定性断言通过。 |
| AC-ANCHOR-HBAR-TOPOLOGY | PASS | 现场 JPEG 专项验证 H range 0→非零→0；paint/terminal 锚点误差及 V 交叉轴断言通过；普通/HiDPI 矩阵通过。 |

## 3. 执行记录

### 3.1 构建

命令：

    cmake -S . -B build
    cmake --build build --parallel 4

结果：PASS。Fovelle、fovelle_tests 和全部 CTest 条目成功生成/链接。

### 3.2 静态测试

命令：

    ctest --test-dir build -R '^(FovelleToggleFitStabilityStatic|FovelleZoomScrollbarDurationStatic)$' --output-on-failure

结果：PASS（2/2）。机器可读产物：

- [toggle-fit-stability-static.json](/Users/inostarlin/code/Fovelle/build/test-results/toggle-fit-stability-static.json)
- [zoom-scrollbar-duration-static.json](/Users/inostarlin/code/Fovelle/build/test-results/zoom-scrollbar-duration-static.json)

静态 gate 的检查内容包括：删除的生产 marker、同步 commit 顺序、目标 topology
重规划、归一化 backing anchor、动态测试符号、六字段用例和四层文档追溯。

### 3.3 缩放入口与投影专项

命令：

    ctest --test-dir build -R '^FovelleScrollbarZoomDurationAcceptance$' --output-on-failure

结果：PASS。覆盖纯可行锚点投影、wheel/键盘/标题栏菜单/右键菜单同步入口和
现场 JPEG 的四格放大、一格回退序列。

### 3.4 Toggle 锚点与终态专项

命令：

    ctest --test-dir build -R '^FovelleToggleFitAnchorAcceptance$' --output-on-failure
    ctest --test-dir build -R '^FovelleToggleFitTrajectoryAcceptance$' --output-on-failure
    ctest --test-dir build -R '^FovelleToggleFitStabilityAcceptance$' --output-on-failure

结果：PASS（3/3）。Toggle 的同步状态、定向 cursor/center 锚点、横条布局切换、
Fit→100%→Fit 终态和 650ms quiet window 均通过。稳定性用例输出
reversals=0、zoom_writes=1；合成 2560×2938 row 和现场 3840×4407 row 均执行。

### 3.5 既有缩放回归与 HiDPI

命令：

    ctest --test-dir build -R '^(FovelleFiveIssueZoomAcceptance|FovelleFourIssueZoomAcceptance)$' --output-on-failure
    ctest --test-dir build -R '^FovelleZoomScrollbarTrajectory$' --output-on-failure
    ctest --test-dir build -R '^FovelleZoomScrollbarTrajectoryHiDpi$' --output-on-failure

结果：PASS（5/5）。既有 wheel、键盘、滚动条、blank-space、expensive scaling
和 QT_SCALE_FACTOR=2 矩阵保持通过。

### 3.6 全量 QtTest

命令：

    ctest --test-dir build -R '^FovelleTests$' --output-on-failure

结果：PASS（1/1，退出码 0）。包含 ImageLoader、Feature、HDRPolicy、
GraphicsView、ShortcutSettings、UI 和 native 相关回归。

### 3.7 全量 CTest 编排

命令：

    ctest --test-dir build --output-on-failure

结果：PASS（12/12，退出码 0，总耗时约 82.64s），覆盖 2 个静态 gate、同步
入口、投影、现场边界、Toggle 锚点/轨迹/终态、既有回归和 HiDPI。

## 4. 实现证据摘要

- qvgraphicsview.cpp 已删除缩放用 QPropertyAnimation、displayed zoom writer、
  pending anchor 和 anchor settle/post-layout timer。
- zoomAbsolute() 将所有入口导向 makeZoomPlan() →
  commitZoomImmediately()；提交中暂时关闭 widget updates，写入目标 transform/
  scene rect，固定点收敛 AsNeeded scrollbar topology，最后一次性应用 anchor。
- projectZoomAnchorForTarget() 逐轴求目标图片原点可行区间，再对其逆仿射像做
  clamp；目标 H/V topology 物化后会重新计算。
- expensive backing 替换使用旧/新 scene rect 的归一化图片坐标，不能把像素密度
  变化解释成新的几何缩放。
- QScrollBar 的整数舍入只在同一提交内做有限两次修正，不安排缩放后的延迟位置
  writer；已有 pan constraint timer 在 zoom commit 开始时停止。

## 5. 证据边界与复核入口

外部语义复核使用 Qt 官方 QAction、QPropertyAnimation、QGraphicsView、
QAbstractScrollArea、QScrollBar、QWidget updatesEnabled 文档，以及 Qt 6.11.1
同版本源码；最近点公式使用 Parikh/Boyd 的 box projection 资料。链接和推理
前提集中在 [technical_design_document.md](technical_design_document.md)。

若未来需要证明“屏幕上每一帧”而非 widget 层无跳变，应在目标 macOS 机器上
追加屏幕或 Core Animation presentation-layer 捕获，并与当前 QtTest trace 的
monotonic timestamp 对齐。
