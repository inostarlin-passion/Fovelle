# 全屏切换卡顿：技术设计文档

日期：2026-10-01（Asia/Shanghai）。基线：`cdd37565e9f19a43580a7ecf55fc54a797ee53cd`。依据：[root_cause.md](root_cause.md)。环境：macOS 27.0（26A428）、arm64、Qt 6.11.2、Release。

## 1. 问题界定与原子拆解

将进入／退出全屏的卡顿拆为启动准备、中段运动停止、终点交接、反向操作等待、几何跳变五类。旧测试只检查 Qt 状态和最终画面，GUI 定时器在主线程忙时也停止采样，不能证明运动连续。

本次稳定复现并修复的是：**已开始的代理窗口动画依赖主运行循环逐帧更新，因短时同步 GUI 工作停止运动。** 使用相同的 130 ms 受控负载区分旧代码与修复代码。负载模拟运行循环占用；不宣称用户那次卡顿恰好持续 130 ms，也不把未经测量的图片转换或 GPU 问题当成定论。

## 2. 多跳联网检索与多源交叉验证

沿“全屏生命周期 → 调度保证 → 呈现树 → 显式动画终点 → 可动画几何 → 事务提交”继续检索。只采用官方文档和匹配版本源码；搜索摘要、论坛猜测不作为因果证据。Apple 页面仅显示 JavaScript 提示时，继续读取对应官方 DocC JSON 的 abstract／discussion。

| 跳次 | 一手资料与结论 | 交叉验证及设计影响 |
| --- | --- | --- |
| 1：原生边界 | [Apple 全屏动画回调](https://developer.apple.com/documentation/appkit/nswindowdelegate/window(_:startcustomanimationtoenterfullscreenwithduration:)) 提供与系统同步的 duration；[进入完成通知](https://developer.apple.com/documentation/appkit/nswindow/didenterfullscreennotification) 表示完成 | [Qt 6.11.2 Cocoa 源码](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/cocoa/qcocoawindow.mm) 也通过 AppKit 动作及通知处理状态；保留原生 did-enter／did-exit |
| 2：调度 | [NSAnimation.frameRate](https://developer.apple.com/documentation/appkit/nsanimation/framerate) 不保证实际帧率；[nonblocking](https://developer.apple.com/documentation/appkit/nsanimation/blockingmode/nonblocking) 使用运行循环 | [Qt 定时器精度](https://doc.qt.io/qt-6.11/qtimer.html#accuracy-and-timer-resolution) 同样受事件循环繁忙影响；不能把设置 60 fps 或 1 ms 采样当作连续性证明 |
| 3：运动执行层 | [Core Animation 基础](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/CoreAnimationBasics/CoreAnimationBasics.html) 将模型、呈现、渲染状态分离，支持已提交的属性运动 | 用本地同负载红绿实验验证：摆脱应用逐帧回调后，呈现状态是否继续推进 |
| 4：观测对象 | [CALayer presentation](https://developer.apple.com/documentation/quartzcore/calayer/presentation()) 是当前显示的近似状态 | 读取呈现树而非生产 animation progress；报告不将近似状态称为物理屏幕 FPS |
| 5：几何与终点 | [显式动画](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/CreatingBasicAnimations/CreatingBasicAnimations.html) 不自动写模型终点；[frame](https://developer.apple.com/documentation/quartzcore/calayer/frame) 不可直接动画化 | 写模型终点，对 bounds／position 设置 from／to；动画移除后不弹回 |
| 6：提交 | [flush](https://developer.apple.com/documentation/quartzcore/catransaction/flush()) 建议尽量利用运行循环提交；[Qt repaint](https://doc.qt.io/qt-6.11/qwidget.html#repaint) 是同步绘制 | 不再每帧改层并 flush；保留一次启动提交与必要的终点预绘制，不能由文档建议推断所有 flush 必然等待 GPU |

Apple 多页不计为多个独立厂商；Qt 机制、当前源码与同机实验构成进一步交叉证据。

## 3. 演绎链与逆向证伪

旧链：NSAnimationNonblocking → 主运行循环 setCurrentProgress → 手动插值代理窗口、图像、标题栏图层 → 每回调 commit／flush。禁用隐式动画后，没有应用回调就没有新位置。推导：运行循环暂停时，运动应保持原值。

最终测试停止事件处理但持续读取原生呈现状态。旧生产代码连续三轮中，4 个 busy 行失败、4 个 idle 行通过；busy 的窗口和图像均不推进。这排除“最终全屏成功便证明流畅”，也排除“所有切换都会失败”的泛化。

新链：一次提交完整 bounds／position／opacity 轨迹 → Core Animation 推进呈现 → NSAnimation 只做终点交接。同负载下应继续运动，最终原生状态、标题栏、fit／pan 必须正确。

H1／H2／H5 未被擅自升级为根因：没有测得用户自然卡顿时具体是哪项同步工作占用了 GUI。H4 的每帧 flush 随驱动方式一并去除，但没有独立 GPU 等待证据；H6 的原生切换排队规则保留。

## 4. 生产实现

修改：[qvcocoafunctions.mm](../src/qvcocoafunctions.mm)。

- `animateFullScreenLayer()` 根据 anchorPoint 将矩形转换为 position，以 bounds 表示尺寸；对两个属性使用一致的起止矩形、duration 与媒体时间。
- `startAnimation()` 在禁用隐式动作的事务内写模型终点，并提交窗口／图像／标题栏显式轨迹。标题栏 opacity 使用同一 ease-in-out 曲线；媒体时间经各层 convertTime 转成本地时间。
- 开始时一次 flush，让 AppKit 同步准备期间轨迹已经提交；中间回调不再改可见几何或 flush。
- `setCurrentProgress()` 保留一次性终点保护、真实窗口终点、布局同步、预绘制。终点移除代理动画时，模型已经在终点。
- 原生通知、失败恢复、代理清理和进入期间退出排队仍使用既有生命周期；不把 AppKit 对象搬到后台线程。
- 日志用 `clock_callbacks` 取代 `intermediate_frames`，增加 `motion_driver=core-animation`，避免将时钟回调数冒充显示帧数。

保留 NSAnimation 作为终点时钟，以兼容已有通知和失败流程；非正 duration 直接设置几何终点，不建立属性运动。

## 5. 测试改动、校准与限制

修改 WindowBehaviorTests、native_titlebar_probe、CTest 注册和 quality_fullscreen_system.py。探针通过 NSApp.windows 公开属性发现真实窗口上方的可见全屏辅助代理，读取窗口层和图像层 presentation；不访问生产关联键、不调用生产 progress、不回退为模型值。AppKit 访问全部在主线程。

首次探针在同一 CA 事务中反复读取时，显式动画也返回重复值，修复试跑失败，原始记录保留在 `green-probe.txt`，不计为通过。探针每次读取前结束采样事务（flush），获取新的媒体时间，不泵事件循环。校准后重新构建旧代码，用最终测试三轮红结果验证校准没有让旧动画继续运动；测试阈值未放宽。

8 行×2 cycle×2 direction，逐项检查中段观测、80 ms 静止门槛、负载期间窗口至少推进 8% 完整行程、图像宽度变化至少 5 point、原生完成及最终恢复。80 ms 是区分本次 130 ms 受控停顿的门槛，不是通用性能 SLA。

呈现树是平台近似值，不能保证 GPU 每个刷新周期都按时送达。既有 AVIF 屏幕截图回归交叉检查可见图像、位置与退出恢复，也不冒充高精度屏幕帧计时。启动前快照／布局成本、终点以外的系统负载和 GPU 饱和仍需现场测量，不由本次通过一概排除。

用例见 [测试用例说明](test_case_specification.md)，实测、命令及原始证据见 [测试完成报告](test_completion_report.md)。旧版三报告保存在 [prior_reports](evidence/fullscreen_motion/prior_reports/)。
