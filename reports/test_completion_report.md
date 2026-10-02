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

## GitHub Actions修复补充（2026-10-02）

检查拆为总测试进程期限与动画采样覆盖两个独立问题。历史 [Checks失败运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/36894021791) 在180.27秒终止总套件；[Build失败运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/36894021792) 同样在180.10秒终止总套件，另外一次退出动画仅有4个中段样本，两次读取间隔326.84ms，轨迹由0.4115推进至0.9671；日志没有证明该间隔内动画冻结。

多跳核验：先读取两个独立工作流的失败日志、固定提交和runner版本，再核验 [CTest TIMEOUT优先级](https://cmake.org/cmake/help/latest/prop_test/TIMEOUT.html)、[Qt 6.11.2 qWait／qSleep语义](https://doc.qt.io/qt-6.11/qtest.html)、[Qt事件处理期限](https://doc.qt.io/qt-6.11/qcoreapplication.html)与 [Apple presentationLayer](https://developer.apple.com/documentation/quartzcore/calayer/presentation%28%29?language=objc)，最后交叉对照本地采样与显式Core Animation实现。网络机制、两个远端实际失败和本地源码支持不同层面的证据；不能由单次长采样间隔认定GPU掉帧，也不能把180秒总套件超时当作单个函数死锁。

修复：八套顺序执行的总CTest期限由180秒设为360秒，保留每个Qt函数30秒期限；Checks测试job预算由10分钟设为15分钟，覆盖安装、编译和总回归。中段已经开始的轨迹使用不处理事件的5ms qSleep采样，前后仍用qWait驱动原生启动与完成。不得降低8个中段样本、80ms冻结、忙负载0.08推进与5像素图像推进门槛；不得以跳过测试或失败重试代替修复。

修复后本地 `FovelleFullScreenMotion` 与 `FovelleFullScreenMetricsGate` 两项通过，22.70秒。其余远端验收以修复提交的Actions状态为准；日志保存在本地 `reports/evidence/actions_fix/`。本地clang-format脚本返回0但报告大量既有格式诊断，不能据此声称全库格式无诊断；此次未执行全库格式重写，远端format结果单独核验。
