# 全屏启动测量阶段绘制阻塞：技术设计

日期：2026-10-01。基线提交：`2f2136e33acd13d9aef65646f509da8350aff9f6`。依据：[root_cause.md](root_cause.md) 的 R1。历史运动驱动和单次Update重复绘制已经修复，本轮处理仍存在的启动阶段成本。

## 问题界定与原子化拆解

拆分启动准备、中段运动、终点交接与物理屏幕呈现。现有预算测试直接调用一个Update槽，现有motion测试从已提交轨迹中段采样；它们均未约束原生启动回调内三轮临时几何测量的总绘制成本。

待验证命题：代理静止在源几何时，隐藏真实窗口是否多次绘制；仅移除显式repaint是否足够；暂停绘制能否保留布局／pan／fit；恢复绘制是否泄漏状态或继续延迟轨迹提交；交接画面是否正确。

## 多跳联网检索与多源交叉验证

1. 沿原生生命周期查阅 [Apple自定义全屏动画](https://developer.apple.com/documentation/appkit/nswindowdelegate/window(_:startcustomanimationtoenterfullscreenwithduration:))：回调包括Space切换，duration应与系统运动配合。同步准备可延迟自定义轨迹启动，不能通过改变系统时长掩盖。
2. 查询 [Qt 6.11.2 QWidget几何](https://doc.qt.io/qt-6.11/qwidget.html#geometry-prop) 与 [绘制](https://doc.qt.io/qt-6.11/qwidget.html#repaint)：布局／resize和绘制是不同职责，resize也可能触发绘制。对应本地 source→target→source setFrame＋Update＋displayIfNeeded链。
3. 用 [Apple性能指南](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/ImprovingAnimationPerformance/ImprovingAnimationPerformance.html) 交叉确认同步绘制占用主线程；使用图层不自动消除该成本。事件过滤器对真实Paint附加固定成本，原生往返实验检验本地机制。
4. 首次仅分离Measure／Update后仍失败，迭代查询 [updatesEnabled](https://doc.qt.io/qt-6.11/qwidget.html#updatesEnabled-prop)，继续核对 [Qt 6.11.2 QWidget源码](https://github.com/qt/qtbase/blob/v6.11.2/src/widgets/kernel/qwidget.cpp) 的 setUpdatesEnabled_helper：显式禁用与父级继承禁用不同；恢复还会请求update。
5. 本地commitZoomImmediately也临时禁用子控件。仅恢复父窗口会留下显式禁用的子控件，试验中的空Paint样本直接检出了该错误。最终保存并恢复整个Widget树的原始显式禁用标志。
6. 根据 [presentation](https://developer.apple.com/documentation/quartzcore/calayer/presentation()) 的近似状态语义，测试仅判定代理源几何阶段，不将它冒称物理屏幕FPS。Apple正文经官方DocC JSON复核，保存于 [证据目录](evidence/fullscreen_preparation/)。

Apple和Qt为不同框架的一手来源，版本源码及实际Paint／原生通知提供本地交叉证据。平台说明只支持机制，不证明用户某次自然卡顿。

## 推导、对照与逆向证伪

基线在代理源尺寸阶段发生多次真实Paint，受控绘制成本累积；此前单次Update只一次Paint并不排除跨三轮测量的重复工作。源画面已由代理快照承载，临时真实窗口alpha为0，这些中间测量画面不会被用户看到。移除临时测量绘制但保留终点同步绘制，是可验证的消融。

仅删除显式绘制后仍有resize相关Paint，否定“只删repaint就足够”。仅恢复父级造成没有Paint的假快，观测完整性断言拒绝；恢复完整状态后若在提交前重绘，又产生源几何阻塞。最终顺序须先提交轨迹再恢复绘制。测试预算始终保持≤1次源几何Paint／≤75ms，未通过调宽阈值达到绿结果。

## 生产代码设计

`FovelleFullScreenAnimationPhase` 新增Measure、SuspendDrawing、ResumeDrawing。MainWindow新增measureFullScreenLayoutTransition：仅更新inset并在必要时fit；Update调用此测量槽后仍同步native SDR几何、标脏视口、同步绘制父窗口。

原生启动路径：

```text
代理承载源画面、真实窗口alpha=0
→ Begin pan preservation
→ 保存窗口及子控件 WA_ForceUpdatesDisabled，暂停Qt绘制
→ source / target / source setFrame + Measure，读取几何
→ 建立并提交Core Animation轨迹
→ 按原始显式状态恢复整个Widget树
→ 最终progress仍Update + displayIfNeeded
→ 原生完成Cancel、reveal和清理
```

移除三轮临时displayIfNeeded。保留源语义恢复、目标几何测量、NSAnimation终点时钟、最终同步绘制及原生生命周期。scope guard保证退出时恢复；空几何或初始化失败先恢复绘制，再走原有恢复／清理路径。duration≤0时先恢复绘制再执行即时终点，避免跳过必要绘制。

## 测试与风险边界

新增原生进入／退出四行测试：标题栏显示／隐藏×PNG／SVG。Paint过滤器仅在原生探针确认代理active时附加40ms成本，记录源宽度±0.1pt内的累计Paint成本；要求原生通知完成、代理绘制观测非空、源阶段≤1次／≤75ms、往返geometry／zoom／标题栏正确。

保留旧端点绘制预算，防止“永久禁用绘制”假修复；保留motion、标题栏、SDR截图与手动pan等回归。系统脚本要求完整四行×两个方向，缺失／重复／空观测不能通过。

风险集中在状态恢复和终点画面。错误返回及零时长分支按代码审查核对；本轮没有针对这些分支伪称完成故障注入。实际执行结果见 [完成报告](test_completion_report.md)。本补丁减少启动测量绘制，不证明快照成本、GUI尾部等待或所有GPU长帧均已消除。
