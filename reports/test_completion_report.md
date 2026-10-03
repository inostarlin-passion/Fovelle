# HDR全屏测试完成报告

日期：2026-10-03。本轮从上一轮SDR修复后的基线开始，基线hash与原四份报告归档位于 `evidence/hdr_fullscreen_continuity/`。此次明确验证真实HDR，不沿用SDR测试通过作为HDR证据。

## 环境与测量范围

macOS 27.0.1、arm64、Qt 6.11.2 Cocoa、Apple LLVM 17、Release构建。DNG样本3456×2304，补充Adaptive HDR JPEG样本8064×6048；文件SHA-256与字节数见 `fixtures.json`。真实HDR解码、headroom>1和持久HDR准备完成为前提，探针只接受16位浮点CGImage。

模型几何采样来自真实NSWindowDidResize；底部检查将真实HDR像素及观察到的模型变换栅格化，与Qt预期几何的独立参考比较。不是物理屏幕录像或FPS测量。

## 先红灯

DNG最终测试：[第1轮](evidence/hdr_fullscreen_continuity/red-stable-1.txt)、[第2轮](evidence/hdr_fullscreen_continuity/red-stable-2.txt)、[第3轮](evidence/hdr_fullscreen_continuity/red-stable-3.txt)。每轮10/10因视觉指标失败，转换前后稳态通过、采样非零，共30/30稳定检出。各问题在两种标题栏下各6/6检出。

|问题|DNG基线指标|
|---|---|
|退出底部闪烁机制|底边几何误差0点，底部错误像素3974（两种标题栏）|
|HDR进入尺寸|906点宽高最大误差|
|HDR进入位置|约567.112/574.623点中心偏差（visible/hidden）|
|HDR退出尺寸|906点宽高最大误差|
|HDR退出位置|约567.112/574.623点中心偏差|

探索探针的稳态失败与中断日志不计入上述统计；它们用于排除测试自身错误。测试源码摘要记录在 `test-oracle-sha256.json`，用于核对红绿阶段oracle一致。

## 修复与结果

针对已准备HDR持久层，将SDR专用全屏几何同步扩展为原生图像几何同步，复用原有最终布局提交点。保持AppKit动画、HDR准备保护及普通交互合并。DNG修复后连续三轮：[第1轮](evidence/hdr_fullscreen_continuity/green-stable-1.txt)、[第2轮](evidence/hdr_fullscreen_continuity/green-stable-2.txt)、[第3轮](evidence/hdr_fullscreen_continuity/green-stable-3.txt)。每轮10/10通过，共30/30；Qt日志另计init/cleanup，故各为12 passed。所有resize采样的尺寸、中心、底边和底部像素误差均为0。

[红绿统计](evidence/hdr_fullscreen_continuity/red-green-summary.json)同时核对每轮十行唯一、采样非零、稳态正常和测试源码摘要一致。JPEG基线十行均按对应指标失败：尺寸584/552点、中心约567.112/574.623点、退出底部错误像素3273/3353（visible/hidden）；见 [JPEG红灯](evidence/hdr_fullscreen_continuity/red-jpeg.txt)。[JPEG绿灯](evidence/hdr_fullscreen_continuity/green-jpeg.txt)十行全部通过，所有模型误差为0。与DNG三轮合计40/40基线检出、40/40修复后通过；各问题在两种标题栏、两种源图的累计8/8观察通过。详见 [JPEG统计](evidence/hdr_fullscreen_continuity/jpeg-summary.json)。

## 结论边界

结果只支持已验证持久HDR路径的提交时序修复。未准备HDR、超预算大图Metal回退、多屏DPR、系统Dock/Space和全部物理合成帧未被穷尽；原SDR、SVG、manual pan和标题栏回归用于检查副作用，不能代替这些边界验证。

## 回归与最终构建

- [CTest](evidence/hdr_fullscreen_continuity/ctest.txt)：6/6通过，总耗时102.63秒，包括HDR/SDR连续性、原生全屏往返、标题栏、精化和指标门禁。原生往返保持普通/最大化、显示/隐藏标题栏、PNG/SVG矩阵及每行两次循环。
- [图形视图回归](evidence/hdr_fullscreen_continuity/graphics-regression.txt)：退出vertical pan保留、overflow后标题栏scene padding清理，2/2函数通过（另计init/cleanup为4 passed）。
- [指标门禁单元测试](evidence/hdr_fullscreen_continuity/metrics-unit.txt)：7/7通过，包括HDR/SDR前缀隔离与缺失矩阵拒绝。未允许HDR数据替代SDR，或反向替代。
- [最终构建](evidence/hdr_fullscreen_continuity/build-final.txt)：应用与测试程序均编译通过；`git diff --check`通过。

- [系统入口验收](evidence/hdr_fullscreen_continuity/system.json)：10/10函数、39/39数据用例通过，两个进程退出码均为0；HDR和SDR各十行独立门禁完整、采样非零、所有视觉指标为0。耗时约105.95秒，Qt状态确认耗时仅作为诊断。
