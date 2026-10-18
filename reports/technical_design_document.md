# Actions RAW 准备与动画倍速验收修复设计

## 问题界定

基线 `4a8dac8ca3b7b8cf95fc572f34fbd3b664f98472`。[Checks 39140631482](https://github.com/inostarlin-passion/Fovelle/actions/runs/39140631482) 有两项 CTest 失败：

|原子问题|直接证据|排查结论|
|---|---|---|
|HDR RAW 准备等待|testHDRPersistentSurfaceMetadata(raw) 的5秒QTRY失败，Qt报告5200 ms足以成功|失败发生于加载准备，尚未到HDR像素/标签验收；5秒不是产品加载SLA。|
|动画倍速测试|testMovieAsyncPlaybackSpeed 两次20帧的墙钟比例未小于0.85|原日志没有两次实际耗时，不能直接归因于系统负载或证明生产倍速错误；应验证逐帧调度契约。|

[同提交 Build Fovelle](https://github.com/inostarlin-passion/Fovelle/actions/runs/39140631516) 成功；上一轮矢量绘制门禁也通过。本次不改变矢量计时修复。

## 多跳检索与多源交叉核验

从失败日志定位到测试，再读取 QVMovie::publishFrame、speedAdjustedDelay、异步队列及 Qt 定时/图像契约，最后核对平台与已有解码等待政策。

|来源|核验结果|
|---|---|
|[Qt 6.11 QTimer](https://doc.qt.io/qt-6.11/qtimer.html)|PreciseTimer 不提前触发，但系统忙时任何计时器都可能晚到；remainingTime过期时为0。两次独立墙钟时长不能作为严格倍率证明。|
|[Qt 6.11 QImageReader](https://doc.qt.io/qt-6.11/qimagereader.html#nextImageDelay)|nextImageDelay规定动画下一帧显示前应等待的毫秒数，可独立读取固定GIF作为期望值。|
|[QtTest v6.11.2 实现](https://github.com/qt/qtbase/blob/v6.11.2/src/testlib/qtestcase.h)|QTRY显式超时控制准备等待，稍后成功仍可报告超时失败；与远端5200ms诊断一致。|
|[GitHub 托管runner说明](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)|虚拟环境不提供本用例所假设的固定调度比例；该事实不证明本次比例失败的具体来源。|
|仓库交叉核验|testDownloadPreservesSources已有15秒解码准备预算；生产publishFrame按源delay×100/speed计算nextDelay，使用单调累计deadline并允许落后时立即追帧。|

信息足以确定两处测试契约缺陷；不依据偶发日志修改生产速度或RAW图像算法。

## 修复方案与正确性论证

1. HDR功能用例加载准备采用15秒有界等待，与现有格式保持测试一致；记录实际等待和预算。仅改变加载准备期限，后续HDR像素、CGImage身份、headroom标签与恢复断言全部保留。添加RAW队列阻塞5500ms的数据行，四个真实工作线程以信号量阻塞，GUI计时器释放，确保测试越过旧5秒边界，作用域清理避免失败时死锁。
2. 动画从独立QImageReader读取4帧图像和原始延时，测试50%、100%、200%及200%下GUI阻塞1000ms。逐次发布验证帧序、像素及 `nextFrameDelay=max(1,sourceDelay)*100/speed`；观察真实QTimer事件，确保活动定时器区间合法且不大于当前帧延时；跑满20帧再stop并验证没有额外发布。
3. 保留有界进度（每行10秒）和播放不能过早的单侧下界：从start起累计前19帧延时，允许每帧1ms整数deadline舍入。慢调度只增加墙钟，不会让此下界失败；删除不受Qt保证的两次墙钟比例上界。像素、逐帧数学关系及实际timer同时验证，比单独读取speed属性更强。
4. 静态测试固定以上契约及注册。先用延迟RAW行在旧5秒期限下复现失败，再修复；通过临时禁用生产倍速缩放的反向突变验证新测试确实检出故障，恢复源码后重建并执行全量验证。

正确性依据：RAW等待期限只决定何时判定准备失败，不能改变加载完成后的像素结果；迟到但正确的输入可进入原有验收。动画源延时d在速度s下必须发布100d/s的逻辑延时；独立解码器提供d，所有帧同时验证内容和次序，生产缩放缺失会在s≠100时被检出。timer事件检查实际调度路径，累积最早时间下界防止错误立即循环；GUI阻塞数据行证明合法晚到不再触发比例误报。该证明不承诺在任意负载下达到理想显示FPS，也不消除真正的加载死锁（15秒仍失败）。

## 范围及风险

修复在CI执行的测试代码中实现，App算法没有已证实缺陷，不为满足步骤而添加无依据的生产改动。新增RAW行增加约6秒准备成本；原有CTest超时保持，功能验收不skip。远端成功需新提交运行确认。
