# 全屏视觉连续性技术设计

日期：2026-10-03。根因与资料链见 [根因分析](root_cause.md)。基线为 `d8f5af6699b0909155b3ee6fd19ff95aad62bf65`。

## 范围与前提

五个独立验收目标是退出底部像素连续性、进入尺寸、进入位置、退出尺寸、退出位置。已验证路径是 macOS 27.0.1、Qt 6.11.2 Cocoa、原生 SDR 持久图块、fit 模式；显示/隐藏标题栏分别覆盖。系统原生全屏动画继续由 AppKit 管理，图像随视口合理缩放与居中；“连续性”表示同一个 resize 时刻原生图像与 Qt 当前布局一致，不要求窗口变大时图像尺寸恒定。

## 现状与推导

Qt resize 同步执行 fit/constraint、场景范围与滚动位置调整。额外的原生图像后端通过零延迟 timer 合并刷新。原生容器已得到新视口时，图像仍可能停留旧变换。等待 DidEnter/DidExit 后再同步只能修正终态，不能排除中间的错误内容。关闭 QWidget 绘制和禁止 CALayer 隐式动画也不能代替图层几何同步。

因此在全屏过渡期间完成 Qt 布局的同一调用栈中，同步原生 SDR 几何；普通交互保留异步合并。生产同步复用现有 `synchronizeNativeSDRGeometryForFullScreenTransition()`，停止待处理帧 timer 后更新渲染器。后端继续使用已有无隐式动画的事务和持久 CGImage 图块。

## 修改点与不变量

1. `QVGraphicsView::resizeEvent` 完成 fit、滚动边缘恢复和竖向滚动条布局后，在全屏 pan preservation 活跃时提交原生 SDR 几何。
2. `commitZoomImmediately` 完成最终 anchor/constraint 和更新状态恢复后补同步，覆盖嵌套 resize 因 zoom commit 保护直接返回的路径。
3. 原生 Did 通知的终态同步保留，防止结束时的 pan 恢复留下差异。
4. 方法仅接受已激活的原生 SDR 后端；不引入新解码、全图快照、自定义代理窗口、事件循环抽水或 GPU 等待。普通 resize/zoom 的合并策略保留。

同步必须发生在最终布局之后；在 `QMainWindow::resizeEvent` 或嵌套 resize 中提前提交可能读到尚未完成的场景几何。全屏状态用现有 preservation 生命周期界定，不能只检查 `isFullScreen()`，因为退出阶段该值已改变。

## 独立测试设计

测试专用 Objective-C 通知观察器读取真实 NSWindowDidResize 通知，扫描实际可见图块树，不调用生产同步方法。原生 image bounds 经实际 layer transform 转换到根坐标；预期值由 Qt viewportTransform 与固定源图独立计算。底部测试离屏绘制真实 CGImage 图块/背景，检查最后八行的红色区域与期望图像相交范围。

转换前和转换后均需几何/像素稳态校验，避免坐标或夹具错误伪装为缺陷。必须收到有效原生采样，零样本不能通过。每个问题单独数据行，两个标题栏模式共十行。测试不在 resize 回调内等待或刷新渲染器。

## 风险与验证边界

同步可能增加过渡期间主线程工作；限制为现有 SDR 几何路径，昂贵精化仍由原生 Will/Did 生命周期暂停/恢复。通过精化、全屏往返、manual pan、标题栏和系统快捷键回归检查。离屏模型树证明错误状态存在及被消除，不等同于物理屏幕逐帧录像，也不覆盖系统 Dock/Space 合成闪烁、HDR、多显示器 DPR 切换的全部成因。

## 实验验收

相同测试在未改生产基线30/30失败，修复后30/30通过；采样误差从552–584点尺寸、约567–575点中心偏差和2256/2352底部错误像素降至0。详见 [测试完成报告](test_completion_report.md)。这支持“同步提交时机”是本夹具的充分修复条件。
