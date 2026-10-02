# 全屏过渡闪烁与图像跳变：根因分析

日期：2026-10-03（Asia/Shanghai）。基线见 `evidence/fullscreen_visual_continuity/baseline.txt`。原四份报告已归档于 `evidence/fullscreen_visual_continuity/prior_reports/`。

## 问题界定与原子化拆解

五个问题独立验收：P1退出时底部闪烁；P2进入时尺寸跳变；P3进入时位置跳变；P4退出时尺寸跳变；P5退出时位置跳变。完成后的尺寸/位置正确不足以排除过渡期间错误帧。把观察划分为请求前、原生Will通知、原生resize、原生Did通知、完成后稳定帧；分别检查原生背景覆盖、图像尺寸、图像中心。

## 多跳联网检索与多源交叉验证

1. [qView固定版本图形视图](https://github.com/jurplel/qView/blob/c5eca1c7176549e0f0718d11201547ddcdb1f8c9/src/qvgraphicsview.cpp)：参考普通Qt绘制路径，追踪resize与fit；本项目额外有独立原生图层，不能直接泛化参考项目的呈现时序。
2. [Qt 6.11.2 Cocoa窗口](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/cocoa/qcocoawindow.mm)：继续追踪Will/Did通知及窗口状态同步，区分请求状态和原生完成。
3. [Qt 6.11.2 QNSWindow](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/cocoa/qnswindow.mm)、[QNSView](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/cocoa/qnsview.mm)：交叉核验原生视图、窗口背景和图层承载机制。
4. [Qt QWidget更新策略](https://doc.qt.io/qt-6.11/qwidget.html#updatesEnabled-prop)：绘制暂停及恢复不等于阻止原生尺寸变化，也不等于同步额外图层。
5. [Apple CALayer contentsGravity](https://developer.apple.com/documentation/quartzcore/calayer/contentsgravity)、[CATransaction disableActions](https://developer.apple.com/documentation/quartzcore/catransaction/disableactions())：HTML返回动态文档壳后，继续读取官方Markdown端点并保存到evidence。禁止隐式动画只影响属性变更动画，不能消除旧几何提交晚于视口resize的时间差。

两家官方机制资料、参考项目固定源码、本地调用链和实际原生resize探针交叉验证。现场物理帧输出仍须区别于模型图层；不以网络资料代替现场实验。

## 显式前提

本轮首先验证本机macOS、Qt 6.11.2 Cocoa、PNG原生SDR持久图块后端、fit模式。源图、窗口大小、标题栏偏好、原生完成计数均由测试断言，不把Qt请求状态当完成。可能涉及SVG/Qt栅格/HDR的因素另列，尚未由此夹具证明。

## 每项可能根因、推导与逆向证伪

|问题|可能根因与可检验推导|逆向证伪|
|---|---|---|
|P1退出底部闪烁|原生host resize/标题栏恢复 → viewport/background覆盖或坐标系与Qt新视口不一致 → 暂露旧背景/边条 → 下一事件循环修补形成闪烁。另可能为Dock/Space系统动画、Qt背景重绘、隐式图层动画。|独立读取实际图层底边、背景范围/可见性；必要时记录屏幕条带；区分应用内容区域与系统Dock区域，不能把正常Dock出现当缺陷。|
|P2进入尺寸跳变|Qt resize同步fit → 图像缩放已变 → 原生提交由零timer延后 → 同一host尺寸下显示旧缩放 → 后续提交突然切换。另一候选为滚动条拓扑多次fit、DPR变化。|比较原生图层变换后的宽高与独立计算的Qt源图几何；记录每次原生resize，终态比较不是证伪。|
|P3进入位置跳变|resizeDelta、fit居中和标题栏遮挡计算已改变 → 原生image/container位置仍旧 → 下一提交修正中心。另一候选为manual pan重捕获或不一致的坐标翻转。|分别比较原生viewport与image中心，固定fit与源图、记录标题栏；定位平移误差而非拿尺寸误差代替位置指标。|
|P4退出尺寸跳变|恢复普通窗口尺寸时Qt重算fit → 可见原生图层暂留全屏缩放 → 下一事件循环或Did通知才同步。另一候选为高质量精化改变几何。|退出专用尺寸门禁，持续到原生DidExit之后稳定；源图不更换，监视原生尺寸而非只看zoom变量。|
|P5退出位置跳变|标题栏/viewport高度恢复与原生图层坐标更新分离 → 图像中心短暂偏移；完成时pan恢复或延迟约束也可能二次移动。|退出专用中心门禁，观察viewport和image坐标、完成后尾部；与fit和manual pan回归交叉核验。|

## 当前实验状态

测试先于生产修改加入实际NSWindowDidResize通知采样。下文区分探索、稳定红灯、针对性修复及未覆盖边界，不把候选原因一律视为已证实。

## 实验证伪与已确认机制

- 原始底边范围测试（`red-1.txt`）没有检出P1：容器与背景底边误差为0。排除本夹具中的覆盖缺口；实际底部像素条带却有2256（visible）/2352（hidden）个错误占用像素，支持“旧图像内容处于新视口”解释。
- 可见标题栏：普通图像矩形 `(235,32,250,500)`，全屏 `(593,0,542,1084)`。原生resize时窗口已切换视口，而图像仍是另一状态；宽高最大误差584点，中心偏差约567.112点。隐藏标题栏分别约552点、574.623点。P2/P3/P4/P5分别由进入/退出的尺寸和中心指标检出。
- 控制实验发现离屏初版采样了顶部；修正为最后八行，并在转换前后等待稳态条件（有界3秒），避免固定200毫秒初始化等待不足引起误判。探索日志不计入稳定检出证据。resize回调自身没有等待或修补。
- Qt渲染终态和native模型终态一致，故静态坐标翻转/持续DPR错误不足以解释本次瞬态；错误native几何精确对应旧状态，源图不变，精化不参与，支持排队提交而非解码/精化改变尺寸。

[Apple事务文档](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/AdvancedAnimationTricks/AdvancedAnimationTricks.html)进一步核验事务组织属性变化；事务提交本身不证明显示器已输出对应物理帧。检索在本地时序证据、Qt实现和Apple事务机制相互一致后收敛。物理屏幕合成/系统Dock因素保留为未由本探针验证的候选，不能宣称被全面排除。

## 稳定红灯与针对性修复

生产修改前，最终探针/断言在 `red-stable-1.txt`、`red-stable-2.txt`、`red-stable-3.txt` 连续三次运行：每次十行均因对应视觉指标失败，稳态控制通过，采样非零。五项×两种标题栏×三次，共30次稳定检出；测试源摘要保存在 `test-oracle-sha256.json`。

对应根因分别为：P1新底部视口中仍呈现旧图像覆盖；P2进入resize后原生缩放未同步；P3进入resize后原生中心未同步；P4退出resize后原生缩放未同步；P5退出resize后原生中心未同步。共同调用链是 Qt同步布局 → 原生容器更新 → 原生图像几何留在零timer队列 → 后续提交修正。各问题仍保留上表中的其他可能成因，但它们不是本夹具已经证明的必要原因。

修复在全屏preservation活跃期间，于最终resize布局和最终zoom commit后立即同步原生SDR几何，复用持久图块；保留Did阶段终态同步。修复后的同一测试结果与相关回归见 [测试完成报告](test_completion_report.md)。
