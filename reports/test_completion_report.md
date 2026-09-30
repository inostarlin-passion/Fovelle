# 全屏切换卡顿：测试完成报告

日期：2026-10-01（Asia/Shanghai）。基线提交：`cdd37565e9f19a43580a7ecf55fc54a797ee53cd`。环境：macOS 27.0（26A428）、arm64、Qt 6.11.2、Release。

## 1. 完成结论与范围

已补充能稳定检出“代理动画依赖 GUI 主运行循环，短时同步工作使中段运动停止”的测试，并将生产运动改为一次提交的 Core Animation 属性轨迹。

**同一最终版测试：旧生产代码连续三轮稳定失败，修复生产代码连续三轮全部通过。** 无负载对照、原生完成、标题栏、窗口几何、fit／pan、菜单退出及实际 AVIF 屏幕内容回归均通过。测试和生产应用均构建成功。

稳定性针对本机可重复的受控负载情形；未将 130 ms 故障注入冒充用户自然卡顿的现场，也未将 presentation 近似值冒充物理屏幕 FPS。其余候选根因没有现场证据，不作“一切卡顿均已排除”的结论。

## 2. 最终红绿结果

| 阶段 | 次数与规模 | 实际结果 | 原始证据 |
| --- | --- | --- | --- |
| 最终测试 + 旧生产代码 | 连续 3 轮；每轮 8 行、32 个方向过程 | 每轮 4 个 busy 行失败、4 个 idle 行通过；每轮进程返回 4，无跳过 | [红 1](evidence/fullscreen_motion/red-final-1.txt)、[红 2](evidence/fullscreen_motion/red-final-2.txt)、[红 3](evidence/fullscreen_motion/red-final-3.txt) |
| 最终测试 + 修复生产代码 | 连续 3 轮；共 96 个方向过程，其中 48 个受控负载 | 每轮 8 行通过；每轮 QtTest 合计 10 passed（含 init／cleanup），0 failed、0 skipped；CTest 返回 0 | [绿重复日志](evidence/fullscreen_motion/green-repeat.txt) |
| 更新后的系统入口 | 1 轮；5 个功能项，含完整 motion 矩阵 | passed=true，两套件返回均为 0；32 个 motion 方向过程通过 | [系统 JSON](evidence/fullscreen_motion/system.json)、[输出](evidence/fullscreen_motion/system-run.txt) |
| 测试与应用构建 | 恢复最终修复源码后 | fovelle_tests 与 Fovelle 均成功 | [最终构建日志](evidence/fullscreen_motion/final-fixed-build.txt) |

红绿重复组各包含 48 次进入与 48 次退出。系统入口额外完成一次完整矩阵，不与主重复组混算。

### 2.1 定量对照

| 指标 | 旧代码，最终红三轮 | 修复代码，绿三轮 |
| --- | --- | --- |
| busy 窗口推进量 | 全部为 0 | 完整宽度行程的 0.4610–0.4792 |
| busy 图像宽度变化 | 全部为 0 | 最小 133.34 point |
| busy 观测静止时间 | 143.41–179.54 ms | 中段采样未观察到超过 0.1 point 门槛的静止序列，max_frozen_ms=0 |
| 原生方向完成 | 每轮完整记录 32 个过程 | 每轮完整记录 32 个过程 |

max_frozen_ms=0 表示按本用例采样尺度观察到的 presentation 轨迹一直推进，不表示物理显示每帧零延迟。判据始终为静止≤80 ms、busy 窗口推进≥0.08、图像变化≥5 point、中段样本≥8；没有为使修复通过而提高容忍值。

旧状态指标仍独立保留：系统入口的 Qt 状态确认均值 303 ms、最大 600 ms，仅 2 个响应样本，不把这个小样本的 p99 外推为真实体验分布，更不以状态指标替代 motion 门槛。

## 3. 邻接回归

| 执行入口 | 核查内容 | 实际结果与证据 |
| --- | --- | --- |
| FovelleHiddenTitlebarFullScreen | hidden／visible × PNG／SVG，标题栏与 fit | 4 行通过，0 failed／skipped；[日志](evidence/fullscreen_motion/titlebar-regression.txt) |
| GraphicsViewTests | testFitZoomSurvivesInverseWheelStepsAndFullscreenResize、testFullscreenExitPreservesVerticalPan、testFullscreenAfterOverflowRemovesTitlebarScenePadding | 3 项通过，0 failed／skipped；[日志](evidence/fullscreen_motion/graphics-regression.txt) |
| WindowBehaviorTests | testExitFullscreenActionUsesEscapePath | 1 项通过，0 failed／skipped；[日志](evidence/fullscreen_motion/menu-regression.txt) |
| FovelleSDRFullScreenPresentation | testProvidedRasterFullScreenTransitionKeepsImageVisible；AVIF、2× zoom、屏幕可见内容和视口场景点恢复 | 1 项通过，0 failed／skipped；实际执行了截图检查，不是无测试退出；[日志](evidence/fullscreen_motion/screen-regression.txt) |

AVIF fixture 本机存在于 `/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif`。核心 motion fixture 自行生成，不依赖这个外部文件。邻接命令和返回值在 [regressions.json](evidence/fullscreen_motion/regressions.json) 中逐项记录。

## 4. 校准失败与逆向验证

首次测试构建遇到调用 private exitFullScreen 的编译错误，已改用公开 toggleFullScreen；不计入有效测试轮次。

初版 presentation 探针在同一 CA 事务重复读数，修复首轮试跑仍失败（[green-probe.txt](evidence/fullscreen_motion/green-probe.txt)）。校准为每次读取前结束采样事务后，单行试跑通过（[校准记录](evidence/fullscreen_motion/green-calibration.txt)）。初版红日志也保留，但最终结论仅采用校准后、增加图像运动断言的 red-final 三轮与最终 green-repeat。

随后临时恢复旧生产源码、保留最终测试，重新构建并连续三轮检出原问题。最终恢复修复源码再构建运行，防止“测试只对早期版本敏感”。采样 flush 不处理 UI 事件，旧图层仍静止；没有因采样器自身刷新而让旧代码误通过。

系统判定器另做反向核验：三份最终红输出均被拒绝；完整绿矩阵被接受；空输出、缺一个方向、重复一个方向均被拒绝。7 项核验全部满足，见 [gate-validation.json](evidence/fullscreen_motion/gate-validation.json)。

## 5. 复现命令与交付物

```bash
cmake --build build --target fovelle_tests Fovelle -j 4
ctest --test-dir build -R '^FovelleFullScreenMotion$' --repeat until-fail:3 -V
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests --output reports/evidence/fullscreen_motion/system.json
ctest --test-dir build -R '^FovelleHiddenTitlebarFullScreen$' -V
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 FOVELLE_TEST_SUITE=GraphicsViewTests build/tests/fovelle_tests testFitZoomSurvivesInverseWheelStepsAndFullscreenResize testFullscreenExitPreservesVerticalPan testFullscreenAfterOverflowRemovesTitlebarScenePadding -v1
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 FOVELLE_TEST_SUITE=WindowBehaviorTests build/tests/fovelle_tests testExitFullscreenActionUsesEscapePath -v1
ctest --test-dir build -R '^FovelleSDRFullScreenPresentation$' -V
```

红阶段直接运行同一 motion 函数三次，每次独立保存返回值和输出；没有用 until-fail 在第一个红结果停止后宣称三轮。最终源码、测试／应用二进制 SHA-256、环境、各轮统计见 [summary.json](evidence/fullscreen_motion/summary.json)。源代码差异见 [changes.patch](evidence/fullscreen_motion/changes.patch)。

设计和多源推导见 [technical_design_document.md](technical_design_document.md)，步骤与判据见 [test_case_specification.md](test_case_specification.md)。旧三报告已归档到 [prior_reports](evidence/fullscreen_motion/prior_reports/)。本次完成 targeted 回归，未宣称执行了整个仓库的全部测试。
