# HDR全屏跳变与退出底部闪烁：根因分析

日期：2026-10-03。本轮基线见 `evidence/hdr_fullscreen_continuity/baseline.txt`。此前SDR报告归档于 `evidence/hdr_fullscreen_continuity/prior_reports/`，其结果不能证明HDR正常。

## 问题界定与原子化拆解

P1退出底部闪烁；P2 HDR进入尺寸跳变；P3 HDR进入位置跳变；P4 HDR退出尺寸跳变；P5 HDR退出位置跳变。分别检查请求前稳态、原生Will、原生resize、原生Did、完成后稳态。合理随窗口缩放/居中不算缺陷；当前Qt布局与原生图像几何不一致才是本轮可检验目标。P1以背景底边与实际底部内容分别检查，不能由尺寸门禁代替。

## 显式前提

本轮本机Qt Cocoa路径；真实HDR图片解码必须断言 `isNativeHDRLoaded`，持久HDR图层必须准备完毕，不允许SDR静默替代。已存在原生HDR半浮点CGImage持久层与大图CAMetalLayer回退两条路径；分别定位，不将一种结果泛化到另一种。屏幕物理合成时序与模型图层观察区分。

## 多跳联网检索与交叉验证

1. [qView固定源码](https://github.com/jurplel/qView/blob/c5eca1c7176549e0f0718d11201547ddcdb1f8c9/src/qvgraphicsview.cpp)追踪普通Qt resize/fit。参考项目没有本项目独立HDR图层，不能照搬其绘制同步结论。
2. [Qt 6.11.2 Cocoa窗口实现](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/cocoa/qcocoawindow.mm)与[QWidget文档](https://doc.qt.io/qt-6.11/qwidget.html)核对请求状态、resize和原生完成区别；GitHub一次读取失败时继续用官方raw端点核验。
3. [Apple macOS Metal窗口](https://developer.apple.com/documentation/metal/managing-your-game-window-for-metal-in-macos)追踪窗口resize与drawable像素大小。该机制适用Metal回退，不代表已准备的持久CGImage也要重新分配drawable。
4. [Apple presentsWithTransaction](https://developer.apple.com/documentation/quartzcore/cametallayer/presentswithtransaction)说明默认Metal呈现与Core Animation事务异步，不能保证同一帧；它是Metal回退的候选因素，不是持久HDR图层的当然根因。
5. [Apple Core Animation事务](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/AdvancedAnimationTricks/AdvancedAnimationTricks.html)核验属性更新/事务机制。本地 `render` 的持久HDR分支只更新已有CGImage仿射变换，而现有全屏同步方法仅接受SDR。

## 分问题的可能根因、推导与反证

|问题|可能根因与链式推导|逆向证伪|
|---|---|---|
|P1退出底部闪烁|标题栏/视口恢复→容器或背景先变→HDR图像仍为全屏变换→底部显示旧覆盖/旧内容→延后修正形成闪动。另可能为应用背景、Dock/Space合成、HDR准备/后端切换。|比较底边范围与底部实际内容；转换前后正控制验证探针；记录后端和准备状态，不能把系统Dock出现当应用闪烁。|
|P2 HDR进入尺寸跳变|Qt同步fit→HDR原生变换被SDR专用guard排除→旧尺寸保留到零timer刷新。另可能为HDR准备完成、drawable重建、DPR变化。|原生resize即时比较真实HDR图层宽高与Qt源图变换；断言HDR已准备、源图不变。|
|P3 HDR进入位置跳变|Qt居中/标题栏布局改变→HDR图层平移未同步→下一提交中心突变。另可能为坐标翻转、pan重捕获。|中心欧氏距离独立于尺寸指标；前后稳态和两个标题栏模式交叉核验。|
|P4 HDR退出尺寸跳变|普通窗口fit已恢复→原生HDR仍保留全屏比例→延后同步。另可能为SDR代理回切或精化。|退出专用宽高指标，持久层在resize时必须仍可见，原生Did之后检查稳态。|
|P5 HDR退出位置跳变|viewport inset/中心恢复→HDR层平移留在全屏位置→延后同步。另可能为manual pan恢复和延迟约束。|退出专用中心指标，分别观察viewport、image；回归manual pan和padding。|

## 实验状态

先增强现有测试，再执行未修改生产基线。稳态正控制、每项独立指标、非零原生采样用于避免伪检出；下文按实际日志区分探索、稳定红灯、交叉验证和修复结果。

## 已复现机制与逆向排查结论

最终DNG测试连续三轮各十行均因对应视觉指标失败，稳态正控制通过、原生采样非零，见 `red-stable-1.txt` 至 `red-stable-3.txt`。使用真实HDR标志、headroom>1及16位浮点CGImage排除SDR替代。

- P1：容器/背景底边误差为0，但底部模型内容不匹配。故单纯“背景覆盖范围不足”不能解释本夹具；旧全屏图像变换在新普通视口中仍可形成错误底部内容。
- P2：进入resize后Qt期望图像1626×1084，而HDR图层仍720×480，尺寸最大误差906点。
- P3：同一进入时点图像中心偏差约567.112点（可见标题栏）/574.623点（隐藏标题栏）。
- P4：退出resize后Qt已恢复720×480，HDR图层仍1626×1084，尺寸最大误差906点。
- P5：同一退出时点中心偏差同上，后续终态恢复正确，支持提交延迟而非恒定坐标翻转错误。

链式推导：原生resize触发Qt最终布局 → 容器立即采用新视口 → SDR专用同步guard拒绝HDR → HDR几何仍在零timer队列 → 当前图层保留旧尺寸/中心/底部内容 → 下一提交修补并形成跳变机会。真实图层错误矩形对应前一状态，源图不变、持久HDR已准备、前后稳态匹配，故“源图重解码/首次HDR准备/恒定DPR错误”不是本次复现所必需的原因。

探索中附着HDR层与未附着参考层的离屏颜色比较在稳态失败，日志 `control-debug*.txt` 保留，但不计作产品缺陷证据。改用相同颜色转换对真实HDR CGImage和两个模型变换栅格化，正控制通过后才统计。探针证明的是应用几何对应的像素内容，不能替代物理显示帧。

Qt源码通知/布局链、Apple事务/Metal机制、本地调用链和真实窗口实验已相互印证，检索在该机制上收敛。Metal回退、HDR未准备阶段、系统Dock/Space合成和多显示器变化仍是边界候选；未用本夹具将它们全面证伪。

## JPEG交叉验证与修复选择

另一真实Adaptive HDR JPEG完整十行也在生产修改前因对应指标失败，稳态与采样前提通过，见 `red-jpeg.txt`。其宽高误差为584点（visible）/552点（hidden），中心误差与DNG一致，支持后端时序而非DNG格式独有因素。两种源图均为已准备HDR持久层。

修复更名同步方法为 `synchronizeNativeImageGeometryForFullScreenTransition()`，除原SDR外，仅允许已准备的HDR持久图层在现有最终布局点同步。未准备HDR维持保护，不引入GPU等待、重新解码或自定义窗口动画。相同测试修复后的结果见 [测试完成报告](test_completion_report.md)。

DNG相同oracle修复后连续三轮30/30通过，所有模型误差均为0；测试摘要一致。这支持已准备HDR持久层的提交时机修复充分消除了本轮复现机制。

JPEG修复后十行也全部通过。结合DNG三轮，共40/40基线检出、40/40修复后通过；六项CTest和39个系统数据用例通过。此结论限于上述明示的已准备HDR持久层和模型像素证据。
