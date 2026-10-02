# 全屏测试完成报告

日期：2026-10-03（Asia/Shanghai）。基线 `d8f5af6699b0909155b3ee6fd19ff95aad62bf65`。本次先增强测试并执行缺陷基线，再修改生产代码；原报告已归档于 `evidence/fullscreen_visual_continuity/prior_reports/`。

## 执行环境与验收范围

macOS 27.0.1、arm64、Qt 6.11.2 Cocoa、Apple LLVM 17、Release构建。新视觉连续性测试覆盖原生SDR PNG、fit、可见/隐藏标题栏、真实进入/退出。使用原生完成计数，采样真实NSWindowDidResize。底部像素是实际模型图层离屏渲染，非屏幕录像。

## 红灯证据

最终测试的三个基线日志：[1](evidence/fullscreen_visual_continuity/red-stable-1.txt)、[2](evidence/fullscreen_visual_continuity/red-stable-2.txt)、[3](evidence/fullscreen_visual_continuity/red-stable-3.txt)。每轮10行均在各自视觉指标失败，转换前后稳态控制通过、采样数非零。共30/30检出。

|问题|visible基线误差|hidden基线误差|稳定检出次数|
|---|---|---|---|
|退出底部闪烁机制|2256错误像素，底边误差0点|2352错误像素，底边误差0点|6/6|
|进入尺寸跳变|584点|552点|6/6|
|进入位置跳变|567.112点|574.623点|6/6|
|退出尺寸跳变|584点|552点|6/6|
|退出位置跳变|567.112点|574.623点|6/6|

原始只检查底边几何的测试没有检出底部问题，见 `red-1.txt`；该反证促使新增真实像素条带。坐标与初始化稳态控制失败的探索版本保留为 `red-3.txt`、`red-4.txt`、`control-debug.txt` 等，不纳入最终检出统计。

## 生产修复

在全屏过渡期间，于最终视口resize和zoom commit布局完成处同步原生SDR图层，停止旧的合并帧请求并复用已有图块。保留原生AppKit动画和Did终态同步。修复不改验收阈值或预期值。

## 结果与证据

最终绿灯三次重复：[1](evidence/fullscreen_visual_continuity/green-stable-1.txt)、[2](evidence/fullscreen_visual_continuity/green-stable-2.txt)、[3](evidence/fullscreen_visual_continuity/green-stable-3.txt)。每轮10/10数据行通过（Qt另计init/cleanup，因此日志为12 passed），共30/30通过；所有resize采样的尺寸、中心、底边和底部像素误差均为0。

[红绿统计](evidence/fullscreen_visual_continuity/red-green-summary.json)校验每轮十行唯一、采样非零、无稳态失败，并核对测试源码摘要一致。CTest、系统入口及相关回归结果附于下方。构建日志为 `build-red.txt`、`build-green.txt`；测试源码SHA-256保存于 `test-oracle-sha256.json`，用于证明稳定红灯与绿灯期间视觉断言保持一致。

## 结论边界

已确定且修复的是五项现象共享的原生SDR图层提交滞后机制。没有物理屏幕逐帧采集，不能将本报告解释为所有系统合成帧都绝无闪烁或所有HDR、多显示器DPR切换均已覆盖。相关SVG、manual pan、标题栏和精化回归用于检查副作用，不代替这些未覆盖场景。

## 相关回归执行结果

- [CTest日志](evidence/fullscreen_visual_continuity/ctest.txt)：5/5通过，包含视觉连续性、原生往返、标题栏、昂贵精化、系统指标门禁，总耗时74.09秒。原生往返覆盖2种标题栏×2种窗口状态×2种图像类型，每行两次循环。
- [图形视图回归](evidence/fullscreen_visual_continuity/graphics-regression.txt)：退出后vertical pan保留、overflow后的标题栏scene padding清理，2/2测试函数通过（日志另计init/cleanup，共4 passed）。
- [指标门禁单元测试](evidence/fullscreen_visual_continuity/metrics-unit.txt)：6/6通过，包括新增的缺失/重复/零样本/非法数值及超限连续性数据拒绝检查。
- 生产应用与测试程序均编译通过；`git diff --check`通过。

- [系统验收结果](evidence/fullscreen_visual_continuity/system.json)：9/9测试函数、29/29数据用例通过，两个测试进程退出码均为0；连续性门禁完整接收10行、均有原生采样、所有指标为0。总耗时约76.27秒。Qt状态确认耗时仅作为诊断，不作为物理动画帧率。
