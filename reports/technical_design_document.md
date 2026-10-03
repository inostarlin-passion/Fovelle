# HDR全屏视觉连续性技术设计

日期：2026-10-03。基线为此前SDR修复后的版本，详见 [根因分析](root_cause.md)。此前报告归档于 `evidence/hdr_fullscreen_continuity/prior_reports/`。

## 范围与设计前提

五个目标：退出底部内容连续、HDR进入尺寸/位置连续、HDR退出尺寸/位置连续。图像合理随窗口缩放不是缺陷；图像层应与同一resize时点的Qt布局一致。已经解码并准备的HDR使用16位半浮点CGImage持久层；大图超预算时另有CAMetalLayer回退，本轮持久层实验不能证明回退的所有物理呈现行为。

基线中的 `synchronizeNativeSDRGeometryForFullScreenTransition()` 在最终resize、zoom commit及Did结束处调用，但方法因SDR专用guard跳过HDR。Qt当前fit/constraint与容器几何已更新，HDR图像仍等零timer，形成旧图像变换与新视口同时存在。

## 修复方案与不变量

将方法更名为 `synchronizeNativeImageGeometryForFullScreenTransition()`，适用已有SDR与已准备的HDR持久层。继续沿用最终resize、zoom commit和Did结束处的同步点，停止待处理帧timer并立即提交最新图像变换。HDR持久层已缓存完整源图；更新仅改变仿射几何和背景覆盖，不重新解码、不新建快照、不阻塞GPU。

使用preservation生命周期覆盖进入与退出，不能只用 `isFullScreen()`。同步发生在fit、pan边缘恢复、场景范围和滚动条布局完成后；嵌套resize在zoom commit内返回，由最终zoom提交补齐。未准备HDR不能强制提前展示，准备/SDR代理/亮度与焦点过渡策略保留。

原生AppKit全屏动画继续由系统管理，不增加代理窗口动画。普通交互维持原有零timer合并。Did结束同步仍保留，以覆盖最后pan恢复。

## 独立测试与证伪

新增 `testHDRFullScreenVisualContinuity`，真实HDR来源、headroom>1、持久HDR准备完毕均为硬断言。原生探针只接受可见16位浮点CGImage，避免误测SDR瓦片。实际bounds通过CALayer坐标转换得到，预期矩形由Qt源图与viewportTransform独立计算。

底部参考保留实际CGImage像素和方向，独立归一化其尺寸/位置到Qt预期；实际与参考使用相同sRGB转换、背景和条带采样。该方法检查模型变换对应的错误底部内容，不比较不同层附着状态的EDR色调映射，也不将其等同物理屏幕录像。resize回调内不等待、不抽事件、不调用生产刷新。前后稳态正控制和非零采样防止探针错误或缺采样伪通过。

两种标题栏×五项独立指标共十行。连续三次基线红灯后，保留相同测试进行三次绿灯验证。补充另一真实HDR格式交叉验证，保持原SDR测试及全屏往返、标题栏、manual pan和精化回归。

## 验收入口和风险

CTest独立注册HDR连续性测试，原生窗口测试串行。系统入口分别检查HDR/SDR完整数据矩阵和指标，避免HDR标记包含SDR标记导致混淆；缺失、重复、零样本、非法或超限指标均失败。

主线程新增工作限于已准备持久层的几何变换。测试文件由 `FOVELLE_HDR_FULLSCREEN_IMAGE` 指定，默认使用现有HDR样本库；样本不可用或未进入真实HDR后端时直接失败。HDR未准备阶段、超缓存大图Metal回退、多屏DPR和物理系统合成的全部闪烁成因作为明确边界，不以本轮模型测试宣称全面覆盖。

## 已完成的核心验收

DNG相同测试在当前生产基线30/30稳定失败，修复后30/30通过，所有采样误差为0；JPEG基线十行全部检出，修复后十行全部通过。精确重复、格式交叉验证和相关回归见 [测试完成报告](test_completion_report.md)。
