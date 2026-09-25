# 测试完成报告：SDR 图片全屏跳变/闪烁

日期：2026-09-25
仓库：`/Users/inostarlin/code/Fovelle`
样本：`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/2.png`
设计：[technical_design_document.md](technical_design_document.md)
用例：[test_case_specification.md](test_case_specification.md)

## 1. 结论

已修复 macOS 全屏布局回调只排队更新 native SDR 图层几何的问题。现在隐藏的真实窗口准备交接时，Fovelle 同步刷新原生 SDR layer 几何，然后才 repaint/显示真实窗口。

新回归使用用户提供的 4616×2924 PNG，检查全屏更新 slot 返回前的 compositor 几何提交、实际显示器画面纹理、相邻采样帧颜色质心和 native renderer geometry，并执行三次全屏往返。去掉生产同步调用时，计数断言稳定失败；恢复后连续重复 5 次通过。全量 CTest 为 15/15 PASS。

## 2. 原子验收结果

| 验收 ID | 结果 | 证据 |
| --- | :---: | --- |
| AC-SDR-FS-SYNC | PASS | 修复前 `compositorGeometryUpdateCount` 为 2，回调返回后预期 3，测试失败；恢复调用后同步增至 3。 |
| AC-SDR-FS-VISIBLE | PASS | 三轮全屏进出期间，每个屏幕采样帧均达到中心纹理方差和可见图像内容下限。 |
| AC-SDR-FS-CONTINUOUS | PASS | 过渡采样中的质心单步位移满足 0.14 阈值，renderer 报告 geometry matches。 |
| AC-SDR-FS-ROUNDTRIP | PASS | 指定 PNG 连续完成三次进入/退出。 |

## 3. 实现变更

- [qvgraphicsview.h](/Users/inostarlin/code/Fovelle/src/qvgraphicsview.h) 增加 `synchronizeNativeSDRGeometryForFullScreenTransition()`。
- [qvgraphicsview.cpp](/Users/inostarlin/code/Fovelle/src/qvgraphicsview.cpp) 在该方法中仅对 native SDR renderer 停止已排队的 0ms timer，并同步调用 `updateHDRRenderer()`。
- [mainwindow.cpp](/Users/inostarlin/code/Fovelle/src/mainwindow.cpp) 在每次全屏布局更新 repaint 隐藏的真实窗口前调用同步方法。
- [tst_qviewtests.cpp](/Users/inostarlin/code/Fovelle/tests/tst_qviewtests.cpp) 新增 Cocoa 全屏真实样本回归；它直接核对同步计数并连续采集屏幕过渡帧。
- [CMakeLists.txt](/Users/inostarlin/code/Fovelle/tests/CMakeLists.txt) 样本存在时注册串行 CTest `FovelleSDRFullScreenPresentation`。

## 4. 执行记录

环境：macOS 27.0.0、Qt 6.11.2、Apple clang 17、Cocoa QPA。外接样本可读，路径与用户问题给出的路径一致。

### 4.1 修复前反向验证

临时移除 `MainWindow::updateFullScreenLayoutTransition()` 中的同步调用，重建并运行同一个 `SDRSampleInteractionTests::testProvidedRasterFullScreenTransitionKeepsImageVisible`：

    Actual compositorGeometryUpdateCount: 2
    Expected: 3
    Result: FAIL

这证明用例会检测到缺少交接前同步提交的实现。

### 4.2 修复后专项重复

    ctest --test-dir build --repeat until-fail:5 -R '^FovelleSDRFullScreenPresentation$' --output-on-failure

结果：5/5 PASS，单次耗时约 5.1 秒。实际屏幕观测每次执行三轮全屏往返。

### 4.3 全量构建与测试

    cmake -S . -B build
    cmake --build build --parallel 4
    ctest --test-dir build --output-on-failure

结果：最终工作树构建 PASS；CTest 15/15 PASS，退出码 0，总耗时 87.48 秒。包含完整 QtTest、SDR 全屏屏幕采样、现有缩放与滚动条回归、HiDPI 和静态验收门禁。

## 5. 多源核验与推导复核

外部资料使用一手平台文档：

1. Apple [`NSWindowDelegate`](https://developer.apple.com/documentation/appkit/nswindowdelegate?changes=_6) 描述自定义全屏代理窗口、动画开始与过渡完成回调。本地代码证实 AppKit 动画期间真实窗口保持隐藏，代理窗口承担可见过渡。
2. Qt [`QTimer`](https://doc.qt.io/qt-6/qtimer.html) 说明 0ms timer 与其他事件源的执行顺序未指定；Qt [`DirectConnection`](https://doc.qt.io/qt-6/threads-qobject.html) 说明 slot 同步执行。本地回调桥正是 DirectConnection，但 renderer 常规请求会排入 0ms timer。
3. Apple [`CATransaction`](https://developer.apple.com/documentation/quartzcore/catransaction?language=_1) 描述 Core Animation layer-tree 事务。本地 SDR layer geometry 更新在事务中提交，并提升 compositor 几何计数。
4. 修复前/后实测对照隔离了遗漏同步提交这一具体路径：没有同步调用时计数断言失败；调用后成功。屏幕帧重复结果对其视觉后果作独立交叉检查。

因此结论限定为：全屏布局回调返回时，SDR 原生 layer geometry 已同步提交，实际采样帧没有触发测试定义的大幅跳变或空白阈值。未声称捕获了 WindowServer/CALayer 每一次刷新扫描，也未以当前样本推断其他文件格式、HDR 路径或多显示器迁移。

## 6. 仍存在的信息边界

- Cocoa 屏幕帧采样需要有桌面显示的 macOS 会话；无桌面 runner 不会注册该外接样本专项。
- 质心上限能稳定检测较大位移，但不是逐像素运动估计；色彩管理也使原始文件 RGB 不适合作为屏幕逐像素基准。
- 若要证明每一次显示器刷新均无闪帧，需要对 WindowServer/Core Animation presentation output 做逐帧系统级捕获；本次没有该类低层捕获。

## 附录：既有缩放门禁仍覆盖的合同

全量 CTest 同时验证下列既有原子合同，相关描述和六字段用例保留在本报告附属文档中：`AC-ZOOM-NO-ANIMATION-STATIC`、`AC-ZOOM-NO-ANIMATION-INPUT`、`AC-ZOOM-NO-ANIMATION-SHORTCUT`、`AC-ZOOM-NO-ANIMATION-MENU`、`AC-ANCHOR-MOUSE-PREFERRED`、`AC-ANCHOR-PROJECT-FEASIBLE`、`AC-ANCHOR-NO-POST-CORRECTION`、`AC-ANCHOR-HBAR-TOPOLOGY`、`AC-VBAR-TOPOLOGY-ANCHOR`。
