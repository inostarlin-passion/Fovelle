# 隐藏标题栏进入全屏：测试用例说明

日期：2026-09-30。实现位置：`tests/tst_qviewtests.cpp` 的 `WindowBehaviorTests::testTitlebarPresentationDuringFullScreen`，及测试专用 `tests/native_titlebar_probe.h/.mm`。

## 测试缺口

已有标题栏测试检查图标清理与隐藏偏好继承；已有全屏测试覆盖快捷键、全屏端点、滚动边缘及图像呈现。它们没有把隐藏标题栏、加载图像和进入前同步布局变化组合起来观察。最终状态断言可能在闪现消失后才运行，因此不能稳定检出本问题。

## 数据矩阵

| 数据行 | 初始标题栏 | 图片 | 进入/退出次数 | 目的 |
| --- | --- | --- | --- | --- |
| hidden-raster | 隐藏 | 自动生成 800×1600 PNG | 2 | 检出原生标题栏闪现、顶部占用、适应高度图像缩小 |
| hidden-vector | 隐藏 | 自动生成同尺寸 SVG | 2 | 排除仅栅格路径问题，并验证共同窗口行为 |
| visible-raster | 可见 | 同尺寸 PNG | 2 | 可见标题栏对照，退出后仍可见 |
| visible-vector | 可见 | 同尺寸 SVG | 2 | 向量对照，退出后仍可见 |

每次完整运行有 4 个数据行、8 次进入和 8 次退出。失败数据行在首个错误断言处结束，不能将失败运行计为完成了全部周期。

## 前置条件与输入

要求真实 Cocoa 平台；非 Cocoa 直接失败，不以 offscreen 或跳过充当通过。使用生产 MainWindow/QVGraphicsView，禁用跟随图片自动调整窗口尺寸，选择 ZoomToFit，并关闭 1:1 像素模式。普通窗口请求 640×480，实际尺寸由 Qt/AppKit 布局决定；测试用初始真实缩放作为基线，不硬编码 zoom=0.32 或标题栏高度=32。

图像在 QTemporaryDir 内生成，不要求挂载外部磁盘。ScopedOptionValues 保存并恢复涉及的偏好；退出与窗口清理由 scope guard 执行，并恢复 quitOnLastWindowClosed。

## 执行步骤

1. 创建普通窗口、打开竖图，等待加载完成，设置指定标题栏状态，验证原生初始状态。可见对照另要求有效顶部遮挡大于 0。
2. 获取本窗口 AppKit 进入、退出通知计数和初始 zoom。
3. 安装窗口与 viewport 的同步事件过滤器，观察 Resize、Paint、WindowStateChange；同时启动 1 ms 请求间隔的定时器。实际间隔由事件循环调度决定。
4. 在 `toggleFullScreen()` 前后立即采样；随后持续采样至该窗口 did-enter 通知计数增加，超时为 5 秒；再观察 200 ms 交接阶段。
5. 停止定时器、移除过滤器，检查原生进入成功、Qt 全屏状态及采样数大于 20；输出累计暴露状态、最大 inset、最小 zoom 与初始 zoom。
6. 对隐藏数据行依次断言原生呈现始终隐藏、最大 inset 为 0、最小 zoom ≥ 初始 zoom − 0.0001。
7. 请求退出，等待该窗口 did-exit 计数增加及 Qt 普通状态；检查原生标题栏、生产 getter 和持久化偏好均等于初始设置。
8. 同窗口重复一次进入退出，检查状态没有残留。

## 判据与证据独立性

原生隐藏判据是 titleVisibility==NSWindowTitleHidden、titlebarAppearsTransparent==true、关闭按钮 hidden==true 同时成立，独立于生产 `getTitlebarHidden()`。通知观察限定为同一 NSWindow，探针随窗口释放取消监听。

布局判据直接观测生产的有效顶部遮挡；图像判据使用实际 zoom。同步事件与调用前后采样保证旧代码同步恢复标题栏时能被记录，而不是依赖恰巧拍到某一帧。定时采样覆盖随后的原生动画阶段，原生完成通知避免过早结束。

故障运行先触发原生可见性断言时，后续 inset/zoom 断言不继续执行，但同次运行的三项累计值均已写入日志。修复运行则执行全部断言。

## 构建与验收命令

在仓库根目录运行：

```bash
cmake --build build --target fovelle_tests Fovelle -j 4
ctest --test-dir build -R '^FovelleHiddenTitlebarFullScreen$' --repeat until-fail:3 --output-on-failure
```

CMake 测试设置 `QT_QPA_PLATFORM=cocoa`、`QT_FATAL_WARNINGS=1`、`FOVELLE_TEST_SUITE=WindowBehaviorTests`、`QTEST_FUNCTION_TIMEOUT=30000`、CTest TIMEOUT=90、RUN_SERIAL=TRUE。

单独输出详细轨迹：

```bash
env QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 FOVELLE_TEST_SUITE=WindowBehaviorTests   build/tests/fovelle_tests testTitlebarPresentationDuringFullScreen -v1
```

邻接回归覆盖隐藏偏好继承、可配置全屏快捷键、菜单图标与 Escape 退出路径，以及 fit zoom、退出 pan 和顶部 scene padding。具体执行与结果见测试完成报告。

## 稳定性与适用边界

先用旧生产代码重复执行，再恢复修复代码重复执行，相同用例必须由红转绿。重复检出证明本机稳定性；不能将其推广为所有 macOS/Qt 组合已经验证。此测试不是显示器逐帧录屏，对系统自动显示菜单栏、用户将鼠标移至屏幕顶端等额外交互不作像素级承诺。

## 既有场景边距测试的时序修正

`GraphicsViewTests::testFullscreenAfterOverflowRemovesTitlebarScenePadding` 的两个窗口现在分别等待 did-enter 后才执行后续缩放/滚动操作，等待 did-exit 后才关闭或继续下一窗口。scope guard 确保断言失败也恢复退出策略与关闭窗口。原有 sceneRect 顶部和图像映射边缘判据保持不变；首次失败、最终重复执行结果均记录在测试完成报告。
