# 全屏启动测量绘制阻塞：测试完成报告

日期：2026-10-01。生产基线：`2f2136e33acd13d9aef65646f509da8350aff9f6`。环境：macOS 27.0（26A428）、arm64、Qt／QtTest 6.11.2、Apple LLVM 17、Release、Cocoa、`QT_FATAL_WARNINGS=1`。

## 结论

已新增能稳定检出原生全屏启动阶段多轮隐藏窗口绘制成本的测试，并分离临时几何测量与终点同步绘制。相同最终测试在基线生产代码上连续三轮失败，在最终修复生产代码上连续三轮全通过。5项CTest、系统检查和邻接回归通过。

结论针对受控绘制负载下的启动机制，未将它指定为用户某次自然卡顿的唯一原因，未将呈现层近似几何当物理屏幕帧时间。

## 问题拆解与证据链

现有单次Update预算已通过，但原生启动仍有source／target／source三轮布局与绘制；中段motion采样也无法约束提交前成本。按Apple自定义全屏生命周期→Qt resize／绘制语义→本地原生调用链→实际Paint观测建立新用例。首次分离显式绘制仍失败，再检索updatesEnabled规范并核对Qt 6.11.2源码及本地嵌套zoom事务，收敛为测量期间暂停绘制、恢复完整显式状态、先提交轨迹后恢复的方案。

网络一手来源、多源核对、演绎和反例见 [技术设计](technical_design_document.md)；原子断言、指标定义和运行入口见 [用例说明](test_case_specification.md)。

## 最终红绿结果

四行：visible-raster、visible-vector、hidden-raster、hidden-vector。每行一次真正原生进入和退出，每轮8个方向过程。视口每个proxy-active Paint附加40ms成本；源宽度±0.1pt内累计≤75ms、至多1次Paint。

| 指标 | 基线生产代码，最终测试三轮 | 最终修复代码，同一测试三轮 |
| --- | --- | --- |
| 方向过程 | 24 | 24 |
| 源几何阶段Paint次数 | 全部5次 | 0–1次 |
| 受控累计Paint成本 | 204.20–205.14ms | 0–41.04ms |
| 业务数据行 | 每轮4行失败 | 每轮4行通过 |
| QtTest总结 | 每轮2 passed／4 failed／0 skipped，退出码4 | 每轮6 passed／0 failed／0 skipped，退出码0 |
| 原生完成、往返geometry／zoom／标题栏 | 仍完成，成本断言失败 | 全部通过 |

passed计数包含init／cleanup。成本只统计过滤器附加的40ms睡眠，不是完整启动延迟或自然绘制耗时；0次源阶段Paint不意味着无交接绘制，新测试要求proxy Paint样本非空，旧端点预算要求同步绘制一次，真实AVIF截图另行验证。

原始：[红1](evidence/fullscreen_preparation/red-1.txt)、[红2](evidence/fullscreen_preparation/red-2.txt)、[红3](evidence/fullscreen_preparation/red-3.txt)、[绿1](evidence/fullscreen_preparation/green-final-1.txt)、[绿2](evidence/fullscreen_preparation/green-final-2.txt)、[绿3](evidence/fullscreen_preparation/green-final-3.txt)。[summary.json](evidence/fullscreen_preparation/summary.json) 保留全样本、版本、6个生产／测试文件及测试／应用二进制SHA-256；[changes.patch](evidence/fullscreen_preparation/changes.patch) 保留代码补丁。

## 构建与回归

- `fovelle_tests`及`Fovelle`最终构建成功，见 [build-green-4.txt](evidence/fullscreen_preparation/build-green-4.txt)。
- [CTest](evidence/fullscreen_preparation/ctest.txt)：5／5通过，总耗时58.65s，包含准备预算、端点预算、原生motion、标题栏与SDR截图。
- [系统检查](evidence/fullscreen_preparation/system.json)：两套测试进程退出码均0；7项功能、32个运动方向过程、12个端点预算样本、8个准备过程完整并通过。
- [GraphicsView回归](evidence/fullscreen_preparation/GraphicsViewTests-regression.txt)：fit、退出垂直pan、overflow inset通过；[菜单回归](evidence/fullscreen_preparation/WindowBehaviorTests-regression.txt)：Escape退出通过。全部任务状态见 [regressions.json](evidence/fullscreen_preparation/regressions.json)。
- SDR测试使用本机既有 `/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif`，完成画面回归；该fixture为本机外部资源，异机运行需配置。准备测试的PNG／SVG为自动生成临时资源。
- [解析器负向验证](evidence/fullscreen_preparation/gate-validation.json)：缺失、重复、原生未完成、空观测、次数超限、成本1000ms均被拒绝，完整绿记录通过。此处为遥测变异检查，未冒称生产故障注入。

## 失败探索与证伪

1. [red-exploratory](evidence/fullscreen_preparation/red-exploratory.txt) 建立实际多次Paint事实；正式red三轮使用最终完整性断言。
2. [green-exploratory](evidence/fullscreen_preparation/green-exploratory.txt)：仅将临时Update分离为Measure、删display仍有2–3次源阶段Paint，测试拒绝，证明resize引发绘制也须处理。
3. [green-1](evidence/fullscreen_preparation/green-1.txt)：只恢复父窗口状态导致子控件仍禁用，空samples被拒绝。[diagnostic](evidence/fullscreen_preparation/diagnostic.txt) 显示原生动画虽完成，不能据此声称正确。
4. [green-stage3](evidence/fullscreen_preparation/green-stage3.txt)：恢复完整状态但在轨迹提交前执行，栅格仍2次源阶段Paint并超预算；继续修正提交顺序。
5. 最终代码先提交轨迹再恢复绘制，同一阈值三轮绿。所有失败日志保留，未计入正式通过次数，未放宽75ms阈值。

## 剩余限制

新测试用受控40ms Paint成本稳定放大启动工作量，确认机制及单变量修复；缺少用户自然卡顿现场性能栈。presentation为近似状态，不能证明物理显示器没有长帧。快照转换、必要布局计算、尾部主线程等待和合成期限仍是独立候选。

空几何／初始化失败和零duration分支的恢复顺序做了代码审查；没有专门故障注入或零duration运行证据，不将这些审查写作动态测试通过。历史报告备份在 [prior_reports](evidence/fullscreen_preparation/prior_reports/)，既有运动和单次重复绘制证据分别保留在fullscreen_motion／fullscreen_paint目录。
