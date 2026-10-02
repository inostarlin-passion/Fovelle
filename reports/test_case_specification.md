# 全屏测试用例说明

## 测试目标与夹具

测试函数 `WindowBehaviorTests::testFullScreenVisualContinuity`。PNG 800×1600、纯红色、固定普通窗口几何、ZoomToFit、禁用窗口自动缩放和棋盘背景；显示/隐藏标题栏各执行一组。初始化和清理使用真实窗口，原生 Did 通知计数界定进入/退出完成。退出行先完成原生进入，再开始采样。

预期图像矩形 = `viewportTransform().mapRect(源图矩形)` 加 viewport 到窗口的偏移。原生矩形 = 可见 CGImage 持久图块父层 bounds 经 layer convertRect 转换到根坐标，再换算 Qt 坐标。探针由测试代码扫描图层树获得，不读取生产期望值或主动触发生产刷新。

|编号|数据行前缀|独立断言|对应问题|
|---|---|---|---|
|TC-VC-01|exit-bottom|容器/背景底边误差≤2点，实际底部8行内部像素中红色占用错误≤2像素|退出底部闪烁机制|
|TC-VC-02|enter-size|所有原生resize采样宽高最大绝对误差≤2点|进入尺寸跳变|
|TC-VC-03|enter-position|所有原生resize采样图像中心欧氏距离≤2点|进入位置跳变|
|TC-VC-04|exit-size|所有原生resize采样宽高最大绝对误差≤2点|退出尺寸跳变|
|TC-VC-05|exit-position|所有原生resize采样图像中心欧氏距离≤2点|退出位置跳变|

每行加 `-visible`、`-hidden` 两种后缀，共10行。尺寸与中心分别度量，不能互相代替。边缘1像素排除栅格边界取整；像素阈值 red>150、green<100、blue<100，几何阈值以逻辑点计。

## 顺序与稳定检出要求

1. 显示真实窗口、加载图片，断言图像与原生图层有效。
2. 转换前独立检查原生几何和底部像素匹配 Qt 预期。
3. 注册 NSWindowDidResize 通知回调，转换期间立即采样；回调内不等待、不刷新生产、不抽取 Qt 事件。
4. 执行现有 toggleFullScreen，等待原生 DidEnter/DidExit；移除回调。
5. 检查转换后稳态、有效采样数>0及对应指标。记录 FULLSCREEN_CONTINUITY JSON，不以 PASS 字符串代替指标。
6. 对未改生产基线连续运行三次，十行均须因对应指标失败，不能因夹具/稳态失败。生产修复后相同测试连续三次全部通过。

## 现有测试漏检与修正

旧全屏往返与 zoom/pan 测试重点检查完成后的状态；零timer或Did回调已修复终态，所以无法检出中间旧图像。新增原生resize采样补上时序盲区，纳入独立 CTest 与系统测试入口。底边几何测试初版在缺陷基线上通过；这反证“覆盖缺口”假设，必须加入实际底部内容像素检查。

离屏条带初版错误选取顶部，在恢复可见标题栏的稳态失败；通过转换前后正控制发现并修正为最后八行。这些探索日志保留，不计入最终红绿重复次数。

## 回归与运行

```sh
cmake --build build -j 6
FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 \
  build/tests/fovelle_tests testFullScreenVisualContinuity -v1
ctest --test-dir build -R 'Fovelle(FullScreenVisualContinuity|NativeFullScreenRoundTrip|HiddenTitlebarFullScreen|FullScreenRefinement|FullScreenMetricsGate)' --output-on-failure
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests --output reports/evidence/fullscreen_visual_continuity/system.json
```

需要可操作的 macOS Cocoa 桌面会话；原生全屏测试串行执行。其他回归检查原生动画、普通/最大化窗口、PNG/SVG、manual pan、标题栏 padding、昂贵精化延后与快捷键。

## 测量限制

通知时点采样的是应用模型图层；条带是该真实图层树的离屏栅格化。它稳定检出五个问题的已复现共同机制，并不证明系统合成每一物理帧、Dock动画或所有HDR/多屏场景已被穷尽。
