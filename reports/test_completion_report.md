# 全屏重复绘制：测试完成报告

日期：2026-10-01。基线：`1b57b8509d05b57e83f09125eb754bfbf00621d5`。环境：macOS 27.0（26A428）、arm64、Qt／QtTest 6.11.2、Apple LLVM 17、Release、Cocoa、`QT_FATAL_WARNINGS=1`。

## 结论与适用范围

已补足现有中段运动测试漏掉的同步布局绘制成本检查，并修复非不透明视口在一次全屏布局更新中重复绘制的路径。相同最终测试在基线生产代码上连续三轮稳定检出，在修复后连续三轮全通过；原生全屏、标题栏、实际 AVIF 画面和系统检查通过。

该结果确认受控绘制成本下的重复工作及修复效果。没有用户自然卡顿现场或物理显示逐帧时间线，不宣称所有全屏卡顿原因都已消除。

## 问题拆解、检索与推导结果

启动准备／终点交接的同步工作与中段属性动画分开验证。按 Apple 提交期限 → Qt 立即绘制语义 → 版本匹配绘制管理器源码 → 本地视口不透明分支 → 真实 Paint 事件的顺序检索和交叉验证，发现 `viewport.repaint(); window.repaint();` 对 macOS 栅格视口重复绘制。

SVG 对照否定了“所有图片都重复”的推断：基线 SVG 一次绘制并通过。修复仅将第一次请求换为 update，保留同步父窗口 repaint；同一测试验证既消除第二次绘制，又没有跳过必须完成的绘制。来源及完整设计见 [技术设计](technical_design_document.md)，测试步骤见 [用例说明](test_case_specification.md)。

## 红绿证据

四行：visible-raster、visible-vector、hidden-raster、hidden-vector；每行三次同步 Update，每轮12观测。每次真实视口 Paint 添加40 ms成本，预算75 ms，且必须在函数返回前恰好绘制一次。

| 结果 | 基线生产代码，最终测试三轮 | 修复生产代码，同一测试三轮 |
| --- | --- | --- |
| 栅格行 | 每轮2行失败；每次2个 Paint | 每轮2行通过；每次1个 Paint |
| 栅格耗时范围 | 83.14–97.02 ms | 41.37–50.65 ms |
| SVG行 | 每轮2行通过；每次1个 Paint | 每轮2行通过；每次1个 Paint |
| SVG耗时范围 | 48.80–57.04 ms | 49.23–59.26 ms |
| 总观测 | 36，其中18栅格重复绘制 | 36，全部合格 |
| QtTest 总结 | 每轮4 passed／2 failed／0 skipped，退出码2 | 每轮6 passed／0 failed／0 skipped，退出码0 |

QtTest 的 passed 数包含 init／cleanup，业务数据行只有4行。耗时为受控条件绝对范围，不能当自然绘制性能或整个全屏往返时间。

原始日志：[红1](evidence/fullscreen_paint/red-final-1.txt)、[红2](evidence/fullscreen_paint/red-final-2.txt)、[红3](evidence/fullscreen_paint/red-final-3.txt)、[绿1](evidence/fullscreen_paint/green-1.txt)、[绿2](evidence/fullscreen_paint/green-2.txt)、[绿3](evidence/fullscreen_paint/green-3.txt)。[summary.json](evidence/fullscreen_paint/summary.json) 包含样本、环境和生产／测试源码 SHA-256；[changes.patch](evidence/fullscreen_paint/changes.patch) 保留代码变更。

## 集成和邻接验证

- 构建 `fovelle_tests` 与 `Fovelle` 成功，见 [构建日志](evidence/fullscreen_paint/build-green.txt)。
- [CTest日志](evidence/fullscreen_paint/ctest.txt)：4／4通过，耗时48.69 s。包含 FullScreenMotion、FullScreenPaintBudget、HiddenTitlebarFullScreen、SDRFullScreenPresentation。
- AVIF 使用本机既有 `/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif`，截图回归通过，非跳过。该资源是本机外部 fixture，异机运行须配置可用资源。
- [系统记录](evidence/fullscreen_paint/system.json)：两个子进程退出码均0，6项功能检查通过，32个原生运动方向过程和12个绘制预算样本均通过。旧响应指标与新绘制指标分别记录。
- [GraphicsView回归](evidence/fullscreen_paint/GraphicsViewTests-regression.txt)：fit、退出垂直 pan、overflow inset；[菜单回归](evidence/fullscreen_paint/WindowBehaviorTests-regression.txt)：Escape退出。结果见 [regressions.json](evidence/fullscreen_paint/regressions.json)。
- 系统绘制解析器负向验证：缺失样本、重复样本、0次绘制、2次绘制、1000 ms超预算都被拒绝；完整绿记录通过，见 [gate-validation.json](evidence/fullscreen_paint/gate-validation.json)。这是解析器指标变异验证，不冒称生产代码变异测试。

## 实验校准与反证记录

初始空窗口实验发现两次 Paint；随后加入真实 SVG fixture，却得到基线通过，日志保留。继续核查本地不透明分支后加入真实 PNG对照，并冻结四行最终测试。最终红绿结果使用相同测试，未把探索日志算作正式红绿次数。

此反例使归因从“所有视口”收敛为“非不透明视口”，防止用空窗口或源码调用次数替代真实图片绘制。探索日志 `red-1.txt`、`green-loaded-exploratory.txt` 与正式 `red-final-*` 分开保留。

## 剩余限制

绘制成本用例直接调用真实桥接槽，测量阶段成本，不直接测量输入至首个物理屏幕变化。原生运动用例读取 Core Animation presentation 近似几何；不能证明所有合成期限满足。快照转换、三次布局准备、终点队列等待、高质量缩放和系统合成候选仍须自然现场证据。

本次确认的信息足以完成上述小范围修复及回归；继续网页搜索不能代替缺失的现场性能轨迹。历史报告备份在 [prior_reports](evidence/fullscreen_paint/prior_reports/)，旧运动驱动修复证据仍在 `evidence/fullscreen_motion`。
