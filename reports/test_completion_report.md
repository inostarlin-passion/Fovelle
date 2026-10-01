# 全屏方向快照性能修复：测试完成报告

日期：2026-10-02（Asia/Shanghai）；检索与红绿对照从2026-10-01开始。生产起点`bfd3aa0a7b51a62cf0430262a6c0e7c98b8dccca`；最终结果对应当前工作区修复，不能只用HEAD代表最终代码。

## 1. 结果与问题边界

**最终相同测试在原始生产基线上三轮均六行失败，修复后三轮均六行通过，无跳过。** 新测试检出了旧测试漏掉的“方向变化后首次原生进入／退出全屏，同步重排大图像素”的性能机制。修复改为缓存原序RGBA源图像、由Core Animation固定仿射矩阵表达旋转／翻转；维持方向、色彩、比例及重载正确性。

三轮共36个方向过程：原生源像素重排由30次降为0；定向离屏内容错误、比例错误均为0；18个同路径重载观察全部正确。八项全屏CTest、完整系统门禁和三个fit／pan业务回归通过。

确认的是提交前同步方向像素处理及其消除；没有物理屏幕逐帧呈现／GPU现场记录，不能声称用户自然异常的唯一根因已定位，或所有设备／负载下零掉帧。

## 2. 问题拆解、检索与证伪结论

按启动、中段、终点、反向等待及几何跳动拆解后，从 [根因报告](root_cause.md) 的R1出发，沿Apple提交／渲染机制 → Qt固定版本像素旋转实现 → 当前缓存与旧测试预热顺序 → 原生代理源像素观测 → CALayer方向和坐标语义 → 最终红绿对照进行多跳验证。来源、迭代读取与推导见 [技术设计](technical_design_document.md)，输入与判据见 [用例说明](test_case_specification.md)。

Qt官方`rotated90()`确实分配并旋转整图；native imageProvider处于轨迹提交前。旧热快照复用通过，却不覆盖新方向的第一次处理。红阶段代理视觉方向与比例正确，但源尺寸／源角颜色已被定向重排；绿阶段原序源图与定向输出同时正确，排除了“只是取消方向”“降低图像分辨率”“缺观测通过”。同路径cyan重载排除了陈旧缓存。

Apple网页外壳信息不足时继续读取官方API JSON；Qt继续读取官方仓库v6.11.2 raw源码，快照保存在证据目录。公开资料与代码、受控红绿相互核验；检索收敛于此机制，不把一般平台文档当作自然帧率故障的实测证据。

## 3. 环境、固定测试与版本有效性

本轮环境：macOS 27.0.1（26A434）、arm64、Qt 6.11.2、Release、Apple LLVM 17.0.0。基线测试首先构建并运行，旧`testFullScreenSnapshotReuse`为6 passed／0 failed／0 skipped，见 [旧测试通过记录](evidence/fullscreen_orientation/old-test-baseline.txt)。

最初四行探索证明漏检后，最终测试扩展到六行四分之一圈旋转／镜像／翻转、12个方向过程及六个原生重载观察。为保证最终对照有效，临时恢复全部五份生产文件到原始HEAD，保持最终C++测试及原生探针不变，再构建并跑三轮红；finally恢复修复源码，构建测试及应用，跑三轮绿。探索记录不混入正式三轮统计。

[summary.json](evidence/fullscreen_orientation/summary.json) 保存11项最终源码／测试／门禁SHA-256、五项基线生产指纹、最终测试及应用二进制指纹、全部正式指标和退出码。红二进制未单独留存指纹，复核依据为生产基线commit、相同最终测试／探针和构建日志；不声称保存了红二进制。

生产变更见 [production.patch](evidence/fullscreen_orientation/production.patch)，源码／测试差异见 [changes.patch](evidence/fullscreen_orientation/changes.patch)，新增Python门禁测试见 [gate-unit.patch](evidence/fullscreen_orientation/gate-unit.patch)。之前三份报告保存在同证据目录的prior文件中，不能沿用旧报告的“全部指纹与当前一致”。

## 4. 正式三轮红绿证据

| 阶段 | 每轮Qt结果（含init／cleanup） | 方向／重载样本 | 三轮资源结果 |
| --- | --- | --- | --- |
| 红1／红2／红3 | 各2 passed／6 failed／0 skipped，退出码各6 | 每轮12个方向＋6个重载 | 36方向中30次源像素重排；视觉／比例错误0；18次重载正确 |
| 绿1／绿2／绿3 | 各8 passed／0 failed／0 skipped，退出码各0 | 每轮12个方向＋6个重载 | 36方向中重排0；视觉／比例错误0；18次重载正确 |

identity进入以及rotate270后的退出回到identity，红阶段这两类单方向源检查可通过；因此不能写成“36个红过程全部失败”。每个最终数据行至少一个方向失败，连续三轮六行全部检出。

原始日志：[红1](evidence/fullscreen_orientation/red-final-1.txt)、[红2](evidence/fullscreen_orientation/red-final-2.txt)、[红3](evidence/fullscreen_orientation/red-final-3.txt)、[绿1](evidence/fullscreen_orientation/green-final-1.txt)、[绿2](evidence/fullscreen_orientation/green-final-2.txt)、[绿3](evidence/fullscreen_orientation/green-final-3.txt)。

请求GUI CPU的受控量级：全部36个绿过程约1.66–3.83ms；rotate90进入红约67.94–72.24ms，绿约1.66–1.74ms；该行退出前改为180°，红约16.69–17.49ms，绿约1.79–3.75ms。计时覆盖toggle请求同步工作，不能写成屏幕启动时间、完整切换耗时或FPS。正式通过以源内容、方向、比例、完成与重载判据为准，没有用绝对请求耗时门槛制造红绿。

## 5. 构建、回归和系统门禁

- [最终构建](evidence/fullscreen_orientation/build-green-final.txt)：`fovelle_tests`与`Fovelle`成功；[注册门禁构建](evidence/fullscreen_orientation/build-gate-registration.txt)成功。
- [CTest](evidence/fullscreen_orientation/ctest.txt)：8／8通过，85.55s；包括新增冷方向／Python门禁、既有快照／运动／Paint／准备交接／标题栏／SDR画面。
- [系统门禁](evidence/fullscreen_orientation/system.json)：passed=true，两个测试进程退出码均0，69.19s；32个运动、12个直接Paint、8个准备交接、4个热快照、12个冷方向及6个重载记录齐全且通过。
- [GraphicsView业务回归](evidence/fullscreen_orientation/graphics-regression.txt)：fit resize、退出垂直pan、overflow标题栏padding三项通过；5 passed／0 failed／0 skipped，含init／cleanup。
- [Python门禁单元测试](evidence/fullscreen_orientation/gate-unit.txt)：五项测试通过；[额外门禁变异验证](evidence/fullscreen_orientation/gate-validation.json)：25／25预期判定通过，三份正式红拒绝、三份正式绿接受。变异验证是遥测／预算负例，不是生产故障注入。
- `git diff --check`与两个Python文件的语法检查通过。

Python Paint门禁同步修正为与最新C++相同的线程CPU预算，保留Paint次数和必要注入下界；墙钟仍记录但不与CPU上限混用。负例继续拒绝重复Paint、超CPU、缺失／非有限CPU；主机等待样本不能冒充额外CPU工作。

复现命令：

```bash
cmake --build build --target fovelle_tests Fovelle -j 4
env QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 FOVELLE_TEST_SUITE=WindowBehaviorTests build/tests/fovelle_tests testFullScreenColdOrientation -v1
ctest --test-dir build --output-on-failure -R 'FullScreen|TitlebarFullScreen'
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests --output reports/evidence/fullscreen_orientation/system.json
python3 tests/test_fullscreen_system_metrics.py
```

## 6. 剩余限制

离屏栅格化验证的是实际代理模型图像，presentation采样验证的是运动轨迹；两者都不是物理屏幕每帧呈现回执。源尺寸／角颜色约束能检出方向重排，不能排除一切隐藏的相同像素复制或GPU上传。

新源第一次必要转换、图层创建、GPU／WindowServer渲染合成、终点主队列等待及符合条件的高质量栅格缩放仍可能产生其他迟滞。未完整覆盖所有HDR／动画、多屏／刷新率／极大图内存组合，也未注入原生失败／零时长路径。本轮交付为稳定检出并消除一个有源码、平台语义及三轮红绿证据的性能机制；若中段物理掉帧仍出现，按根因报告继续采集现场呈现与调度证据。
