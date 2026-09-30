# 全屏测试用例说明

日期：2026-10-01。目的：补足启动准备／终点交接同步绘制成本盲区，并保留中段运动、几何和真实画面检查。

## 测试依据与原子断言

依据 [根因报告](root_cause.md) 的 R1／R3、[技术设计](technical_design_document.md) 的版本源码核对、Qt 绘制规范及 Apple 提交期限说明。依据源码只能建立候选机制，实际次数和成本由测试观察；SVG 作为分支反例。

| 用例 | 输入／操作 | 原子断言 |
| --- | --- | --- |
| `testFullScreenLayoutPaintBudget` | 640×480 窗口加载生成的 800×1600 PNG／SVG；显示／隐藏标题栏；Begin 后连续三次 Update | 每次返回前视口恰绘制一次、耗时≤75 ms、图像矩形和 zoom 不变 |
| `testFullScreenPresentationKeepsMoving` | 标题栏×PNG／SVG×idle／busy；两次完整原生往返 | 每方向≥8中段样本；静止≤80 ms；busy 期间窗口和图像仍推进；原生完成及终点几何正确 |
| `testTitlebarPresentationDuringFullScreen` | 已有显示／隐藏和原生全屏组合 | 标题栏展示和完成状态符合既有断言 |
| `testProvidedRasterFullScreenTransitionKeepsImageVisible` | 本机现有 SDR AVIF，原生全屏过程截图 | 图像持续可见，交接画面符合已有检查 |
| 邻接 GraphicsView／菜单回归 | fit、退出垂直 pan、overflow inset、Escape菜单 | 几何、平移及退出路径保持正确 |

## 绘制成本用例步骤

1. 使用 `ScopedOptionValues` 设置标题栏和禁止自动 resize；创建临时图片，真实打开，等待加载完成。
2. 找到真实 graphicsView 视口，等待初始窗口稳定。安装局部 Paint 事件过滤器，正常放行事件，不替换绘制实现。
3. 每个 Paint 事件递增计数并 `QThread::msleep(40)`；不注入生产分支或伪造生产指标。
4. Begin 使用 0 inset，记录非空图像矩形与 zoom；逐次计时直接调用真实 Update 槽。测量区间内不调用 processEvents／qWait。
5. 输出 `FULLSCREEN_PAINT_BUDGET` JSON；返回时断言一次 Paint 和≤75 ms。每行三次、四行共12个观测；聚合全部失败细节，避免第一失败掩盖后续观测。
6. 移除过滤器、Cancel、关闭窗口，恢复应用设置。测试失败路径也通过 scope guard 清理。

40 ms 模拟内容绘制开销，一次正常绘制仍有35 ms框架余量，两次成本至少80 ms。绘制次数是稳定机制断言，计时补充用户可感知成本；机器负载导致一次绘制超过75 ms时，应保留失败证据检查环境，不自动重试或调宽阈值。

此用例验证真实桥接槽的阶段成本，不直接切换原生 Space；原生往返使用独立集成用例。PNG和SVG的实际加载／视口绘制均保留，不能用空窗口结果替代图片路径。

## 红绿与防误通过

最终测试固定后，先运行基线生产代码三轮，栅格两行应稳定失败，SVG两行应通过；修复生产代码后同一测试连续三轮全通过。系统脚本还要求四行×三次的完整唯一矩阵和40–75 ms耗时，任何样本缺失或重复不能通过。

必须检查“没有同步绘制”的负向指标仍会失败；脚本解析不能只匹配 PASS 文本。原生回归按真实 did-enter／did-exit 边界检查，不能把 Qt flags 提前变化当原生完成。

## 执行入口

```bash
cmake --build build --target fovelle_tests Fovelle -j 4
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 FOVELLE_TEST_SUITE=WindowBehaviorTests \
  build/tests/fovelle_tests testFullScreenLayoutPaintBudget -v1
ctest --test-dir build -R 'Fovelle(FullScreenMotion|FullScreenPaintBudget|HiddenTitlebarFullScreen|SDRFullScreenPresentation)$' --output-on-failure
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests \
  --output reports/evidence/fullscreen_paint/system.json
```

测试环境、实际红绿次数、截图资源路径、失败样本和局限分别记录于 [完成报告](test_completion_report.md)。不得把受控成本改善改写为所有图片或所有屏幕掉帧已消除。
