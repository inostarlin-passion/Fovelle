# 全屏切换卡顿：测试用例说明

日期：2026-10-01（Asia/Shanghai）。依据：[根因分析](root_cause.md)、[技术设计](technical_design_document.md)。

## 1. 目标与前提

检测已开始的全屏代理动画在 GUI 主运行循环短时忙碌时是否停顿；分别断言窗口运动、图像运动、原生完成及恢复。测试函数：`WindowBehaviorTests::testFullScreenPresentationKeepsMoving`。观测函数：`nativeFullScreenPresentation()`，读取呈现树几何近似值，不使用生产 progress。

要求可显示窗口的 macOS Cocoa 会话；不是 Cocoa 会失败，不用 skip 掩盖环境缺失。普通窗口与全屏宽度差须大于 200 point。fixture 在 QTemporaryDir 自动生成，不依赖外部文件：800×1600 PNG／SVG，红色背景加蓝色区域。普通窗口 640×480，fit 模式，关闭 1:1 DPI 调整和加载后自动调整窗口尺寸；退出恢复选项和 quit 策略。

## 2. 数据矩阵

| 行名 | 标题栏 | 格式 | 负载 |
| --- | --- | --- | --- |
| visible-raster-idle | 可见 | PNG | 无额外负载 |
| visible-raster-busy | 可见 | PNG | 中段暂停事件处理 |
| visible-vector-idle | 可见 | SVG | 无额外负载 |
| visible-vector-busy | 可见 | SVG | 中段暂停事件处理 |
| hidden-raster-idle | 隐藏 | PNG | 无额外负载 |
| hidden-raster-busy | 隐藏 | PNG | 中段暂停事件处理 |
| hidden-vector-idle | 隐藏 | SVG | 无额外负载 |
| hidden-vector-busy | 隐藏 | SVG | 中段暂停事件处理 |

每行两次往返，总计 16 次进入、16 次退出、32 个方向观测。PNG 不被擅自等同于 Qt 栅格后端，本机可能采用 native SDR；SVG 提供不同内容路径对照。

## 3. TC-FS-MOTION-IDLE

步骤：加载并稳定窗口 → 记录普通几何及 fit zoom → 调用生产 toggleFullScreen → 约每 5 ms 处理事件并采样呈现层 → 等待 did-enter → 同样采样退出直到 did-exit → 检查恢复 → 重复一次。

断言：

- 每个方向至少 8 个中段样本；探针缺失不会回退为 Qt 状态。
- 归一化进度 `p=(呈现宽度−起始宽度)/(终点宽度−起始宽度)`；退出分母为负，p 仍从 0 向 1 推进。
- 只在 `0.15<p<0.85` 统计静止，排除缓入缓出的端点；相邻宽度变化超过 0.1 point 视为推进，观测到的持续静止不超过 80 ms。
- 收到方向对应的原生通知；Qt 最终状态一致；每次往返恢复窗口几何、fit zoom、原生标题栏状态。

5000 ms 是生命周期失败的保护超时，不是允许中间停顿 5 秒。运动断言汇总到完成往返后，以保留进入和退出证据；无法完成生命周期则立即失败。

## 4. TC-FS-MOTION-BUSY

在同一生产路径，观察到 `0.20<p<0.45` 后只施加一次受控负载：13 次 QThread::msleep(10)，间隙继续读取呈现层，期间不执行 Qt／AppKit 事件处理。名义暂停 130 ms，实际时间记录为单调时钟测量。

追加断言：必须实际进入负载窗口；窗口推进至少为完整宽度行程的 0.08；图像宽度变化至少 5 point；同时满足 idle 的观测、静止门槛、原生完成和恢复检查。只让窗口动而图像冻结也会失败。

预期红：暂停期间 window advance／image delta 为 0，静止超过 80 ms；4 个 busy 行失败，4 个 idle 行通过。预期绿：已提交轨迹持续推进，8 行均通过并正确恢复。

故障注入只在测试代码。生产没有延时开关或绕过断言的测试分支。红绿采用同一最终版测试和同一阈值。这个条件把依赖任务重叠时机的调度故障变成稳定检验，不推断用户现场的具体同步任务；暂停期间不检查输入响应，检查的是已提交运动的独立性。

## 5. 校准与防漏检

同一 CA 事务可能缓存呈现时间；每次读取前 flush 采样事务，刷新时间而不泵事件循环。校准后必须重新构建旧生产代码并稳定失败，证明测试仍有敏感性。没有探针、样本不足、未施加负载、未收到原生完成都失败。

每个方向输出 FULLSCREEN_MOTION JSON：行名、cycle、方向、完成、注入、中段样本数、静止观测、负载推进量、图像变化量和原始 `(ms,width,image_width,progress)` 样本。quality_fullscreen_system.py 强制要求 8×2×2 完整矩阵，缺失、重复或不完整 telemetry 不可通过，并把状态响应与运动指标分开。

max_frozen_ms 是观测到的静止，不是物理显示器帧间隔。呈现树是近似值，不把样本数量或 API 轨迹外推为真实 FPS。

## 6. 邻接回归

标题栏呈现、fit 意图、手动 pan、scene padding、菜单退出路径和 AVIF 屏幕图像／几何分别验证。实际源码函数名及执行结果见 [测试完成报告](test_completion_report.md)。AVIF 使用外部 fixture 和屏幕捕获条件，独立列出执行结果，不用跳过冒充通过；核心 motion 矩阵无该依赖。

## 7. 执行

```bash
cmake --build build --target fovelle_tests Fovelle -j 4
ctest --test-dir build -R '^FovelleFullScreenMotion$' --repeat until-fail:3 -V
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests --output reports/evidence/fullscreen_motion/system.json
ctest --test-dir build -R '^FovelleHiddenTitlebarFullScreen$' -V
ctest --test-dir build -R '^FovelleSDRFullScreenPresentation$' -V
```

红测试用最终测试源码重新构建旧生产代码，直接运行 motion 函数三次，保留每次返回值。until-fail 会在首个失败停止，不能用于获得三轮红证据。所有数据和范围以原始日志为准。
