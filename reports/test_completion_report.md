# 全屏偶发掉帧测试完成报告

日期：2026-10-02（Asia/Shanghai）。基线：`895712fb60f70538585db43c9961ac58acfb045c`；环境：macOS 27.0.1（26A434）、arm64、Qt／QtTest 6.11.2、Release、Apple LLVM 17.0.0、Cocoa。开始时工作区干净。

## 结论与修复范围

本轮稳定检出并修复普通 Qt 栅格图像在全屏事务内同步高质量缩放的机制。事务内禁止精化，完成／取消后按最终尺寸恢复一次。新增测试覆盖进入、退出、真实timer回调、直接调用入口、重复begin／Cancel与最终清晰度。原有native SDR、vector及全屏运动／交接路径回归通过。

这属于对用户“偶发掉帧”的一个可达机制修复。没有用户现场的物理屏幕帧时间线，不将本轮结果解释为所有图像、显示器与GPU负载下都不存在掉帧；候选及证伪条件见 [根因报告](root_cause.md)。

## 红绿对照

先只新增测试，保持生产代码为基线，构建并连续运行三轮；随后修复生产代码，保持同一夹具及断言，再连续运行三轮。

| 阶段 | 每轮QtTest结果 | 三轮事务内违规总数 | 解释 |
| --- | --- | --- | --- |
| 红：旧生产代码＋新增测试 | 2 passed、2 failed、0 skipped | 36 | 两个数据行失败；每行三轮、timer及直接入口各一次，整图pixmap发生替换 |
| 绿：修复生产代码＋同一测试 | 4 passed、0 failed、0 skipped | 0 | 两数据行全部通过；最终尺寸正确，每事务恢复timer一次，重复Cancel不重复精化 |

passed数包含init／cleanup，因此红阶段2 passed不表示某个业务数据行通过。新测试无需注入慢速缩放，也不依赖自然掉帧随机出现；它确定性检测事务内不该发生的整图缩放。用真实pixmap内容key、尺寸及timeout次数观测，不能简单禁止所有精化来蒙混通过。

证据：[红1](evidence/fullscreen_refinement/red-1.txt)、[红2](evidence/fullscreen_refinement/red-2.txt)、[红3](evidence/fullscreen_refinement/red-3.txt)、[绿1](evidence/fullscreen_refinement/green-1.txt)、[绿2](evidence/fullscreen_refinement/green-2.txt)、[绿3](evidence/fullscreen_refinement/green-3.txt)。[汇总与源码／最终测试二进制SHA-256](evidence/fullscreen_refinement/summary.json)绑定本轮结果。红阶段二进制被绿色构建替换，未另存红二进制指纹；基线版本与生产源码指纹已记录。

## 回归与构建

- 全部生产与测试目标构建成功：[完整构建日志](evidence/fullscreen_refinement/build-all.txt)。
- 新增测试的红、绿构建成功：[红构建](evidence/fullscreen_refinement/build-red.txt)、[绿构建](evidence/fullscreen_refinement/build-green.txt)。
- 相关九项CTest通过：[相关回归日志](evidence/fullscreen_refinement/ctest-fullscreen.txt)。随后以串行全量运行重新确认，避免多个GUI进程影响最终验收。
- 全量CTest：**28／28通过，0失败**，总时长268.72秒；主Qt测试套件146.72秒：[完整日志](evidence/fullscreen_refinement/ctest-all.txt)。包含新门禁和原有全屏、zoom、native对话框、导航及设置回归。
- 系统门禁：**10／10功能用例通过**，整体`passed=true`、两个进程返回码均为0，72.53秒：[系统记录](evidence/fullscreen_refinement/system.json)。包含新用例enter／exit数据行严格检查；状态确认平均616.5ms、最大1227ms，仅为Qt目标状态确认指标，不能当作动画或屏幕帧耗时。
- `git diff --check`通过；系统门禁脚本Python语法检查通过。

## 测试范围与限制

新回归确定性驱动生产全屏事务入口和布局寿命，并不模拟物理屏幕呈现；真实AppKit切换由原有全屏用例覆盖。现有presentation轨迹门禁、40ms受控Paint成本和pixmap替换各自观测不同机制，均不等于显示器FPS。本轮没有穷举多屏、120Hz、GPU过载及用户实际安装版本。大图同步精化仍可能在完成后的timer中耗时；本次消除它与全屏事务重叠的机制，未将像素处理改为后台任务。

## 交付

生产与测试修改留在工作区；未提交或部署。四份报告分别为 [根因分析](root_cause.md)、[技术设计](technical_design_document.md)、[测试用例说明](test_case_specification.md)及本报告。原报告已保存在 `reports/evidence/fullscreen_refinement/prior_*`，保留历史证据与本轮证据的区别。
