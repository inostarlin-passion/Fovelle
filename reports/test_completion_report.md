# 测试完成报告：放大 AVIF 退出全屏时的跳变/闪烁

日期：2026-09-25
样本：`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif`（1200×1085）
设计：[技术设计文档](technical_design_document.md)
用例：[测试用例说明](test_case_specification.md)

## 1. 结论

已补上 macOS 全屏退出收尾中的 SDR 图层同步提交。`cancelFullScreenLayoutTransition()` 完成最终 viewport 和 pan 状态恢复后，现在会同步更新 native SDR 几何，再返回 AppKit 交接回调。AVIF 放大、全屏进出专项连续 5 次通过；移除该生产调用后，新增计数断言按预期失败。全量 CTest 为 15/15 PASS。

## 2. 修改内容

- [mainwindow.cpp](/Users/inostarlin/code/Fovelle/src/mainwindow.cpp)：在全屏 transition cancel/completion 收尾中完成 pan preservation 后，同步提交 native SDR layer 几何，确保 AppKit 揭示真实窗口前使用最终 viewport transform。
- [tst_qviewtests.cpp](/Users/inostarlin/code/Fovelle/tests/tst_qviewtests.cpp)：将测试路径改为 `1.avif`；加 2 倍缩放；增加 cancel 回调几何计数断言、drawable 对齐、退出画面采样和 usable viewport scene anchor 检查；保留三轮真实 Cocoa 往返。
- [CMakeLists.txt](/Users/inostarlin/code/Fovelle/tests/CMakeLists.txt)：全屏专项默认样本改为给定 AVIF，并更新缓存项说明。

## 3. 结果追溯

| 原子验收 | 结果 | 执行证据 |
| --- | :---: | --- |
| AC-AVIF-LOAD | PASS | 实际样本加载，并通过 `isNativeSDRLoaded` 与 Metal SDR renderer 检查。 |
| AC-AVIF-ZOOM | PASS | 每轮 zoom 保持为 2.0。 |
| AC-FS-END-SYNC | PASS | 退出收尾回调返回前 geometry update count 增加 1，drawable geometry 对齐。 |
| AC-FS-VISIBLE | PASS | 五次专项执行中的每次运行均完成三轮全屏进出屏幕采样，无纹理/彩色内容阈值失败。 |
| AC-FS-CONTINUOUS | PASS | 五次专项执行的进入和退出采样均未超过 0.14 质心单步阈值。 |
| AC-FS-ANCHOR | PASS | 每轮退出后的 usable viewport scene point 与全屏时记录点距离不超过 2。 |
| AC-FS-ROUNDTRIP | PASS | 专项重复 5 次，每次三轮真实全屏往返。 |

## 4. 测试与逆向验证记录

环境：macOS 27.0.0、Qt 6.11.2、Apple clang 17、Cocoa QPA；用户给定的外接卷样本可读。

1. 修改专项前，使用 `1.avif` 执行旧用例通过，但旧用例没有 zoom 输入，也没有退出 cancel 收尾断言，因而不足以覆盖报告路径。
2. 增加退出锚点断言的首次实验误将整个 viewport center 当作可用中心，导致错误失败。核对实现后改用排除标题栏遮挡的 usable viewport center，再次运行通过。
3. `ctest --test-dir build --repeat until-fail:5 -R '^FovelleSDRFullScreenPresentation$' --output-on-failure`：5/5 PASS，单次约 5 秒，每次三轮全屏往返。
4. 逆向变异：暂时移除 `cancelFullScreenLayoutTransition()` 的同步更新，重建并运行同一专项：FAIL，实际几何计数 4、期望 5，失败位置为退出收尾契约断言。恢复生产调用后专项 PASS。
5. `cmake --build build --parallel 4`：PASS。
6. `ctest --test-dir build --output-on-failure`：15/15 PASS，退出码 0，总耗时 93.84 秒。包含 QtTest、AVIF 全屏显示器采样、缩放/滚动条轨迹、HiDPI 和静态合同检查。

## 5. 多源交叉验证与推导复核

Apple 的 [`NSWindowDelegate` 全屏回调说明](https://developer.apple.com/documentation/appkit/nswindowdelegate?changes=_6)及[`退出全屏完成回调`](https://developer.apple.com/documentation/appkit/nswindowdelegate/windowdidexitfullscreen%28_%3A%29?changes=_4)确立 AppKit 的退出生命周期；Qt [`QTimer`](https://doc.qt.io/qt-6/qtimer.html)说明 0ms timer 与其他事件源的顺序没有保证，Qt [Direct Connection 文档](https://doc.qt.io/qt-6/threads-qobject.html)说明 slot 会立即调用；Apple [`CATransaction`](https://developer.apple.com/documentation/quartzcore/catransaction?language=_1)说明 layer-tree 操作的显式提交机制。仓库代码把这些契约连接成具体时序：AppKit `DidExit` 处理器同步调用 `Cancel` handler，handler 收尾 viewport/pan 并返回后才揭示真实窗口。新增同步提交填补了该边界。

平台文档没有直接断言本程序会闪烁；结论另外由本地几何计数对照、实际屏幕采样和反向移除生产修复后的必失败实验验证。测试没有捕获 WindowServer/Core Animation 的每一次显示刷新，故结论仅表示覆盖的三轮过渡采样和图层几何提交契约通过。

## 6. 信息缺口

当前工具链未提供 WindowServer/Core Animation 逐 scanout 捕获，所以两次截图间隔内可能出现的极短闪帧无法排除。质心算法针对明显跳位，不能替代逐像素追踪。测试与结论仅限指定 1.avif 在当前 macOS Cocoa native SDR 路径。
