# EPS/SVG交互性能测试规范

日期：2026-10-18。每条标准与设计文档P1–P9对应；代码固化在vector_performance_static.py与vector_performance_acceptance.inc。证据目录reports/evidence/vector_performance_20261018/。

## S1（P1）：paint没有ICC系统查询
1. 测试目的：拒绝每帧解析窗口profile的回归。
2. 前置条件：Python3、可读源文件。
3. 输入数据：QVGraphicsImageItem::paint函数体。
4. 操作步骤：运行vector_performance_static.py的对应unittest，检查系统查询和ICC解析不在paint中。
5. 预期结果：基线失败，修复后通过；非paint刷新逻辑仍存在。
6. 后置条件：文件不变。

## S2（P2/P3）：显示准备属于worker
1. 测试目的：防止把颜色/格式转换重新放进GUI，或直接绘制高位深源。
2. 前置条件：Python3、可读源文件。
3. 输入数据：paint、startAsyncVectorTile和prepared显示数据。
4. 操作步骤：静态检查paint无转换，worker准备ARGB32 premultiplied、目标空间和输出副本；动态D1再次验证实际计数及格式。
5. 预期结果：静态契约成立，保留高精度源。
6. 后置条件：无外部变更。

## S3（P5/P6）：目标/generation与有界队列
1. 测试目的：拒绝过期显示结果和无界排队。
2. 前置条件：Python3。
3. 输入数据：请求比较、显示空间更新和回调条件。
4. 操作步骤：检查目标空间进入请求key，目标变化使generation失效，结果发布检查目标；保留active/pending槽位。
5. 预期结果：旧结果不进入新目标cache；队列仍有界。
6. 后置条件：代码不变。

## S4（P9）：测试进入标准CTest
1. 测试目的：让性能与颜色契约持续回归。
2. 前置条件：Python3、CMake源可读。
3. 输入数据：新静态/动态CTest和既有矢量门禁。
4. 操作步骤：检查新用例注册且旧测试未删除。
5. 预期结果：新门禁注册，旧密度/拖影/色彩门禁保留。
6. 后置条件：不修改构建。

## D1（P1/P2/P3/P7）：EPS/SVG固定像素交互与基线对照
1. 测试目的：测量缩放/拖动的冷tile首绘与缓存复用耗时，并验证GUI不承担颜色准备。
2. 前置条件：Release、Qt Cocoa、Ghostscript、可见测试widget；临时设置隔离。
3. 输入数据：自生成EPS和SVG，1920×1200及3840×2400像素画布，固定缩放序列与128px overscan内拖动序列；同机ARGB32绘制参考。
4. 操作步骤：生产loader读取文档；等待异步tile真实发布；记录request paint、首个完成tile paint和连续缓存paint耗时；输出JSON p50/p95/max、格式、颜色准备/ICC查询计数。修复前后执行同一测试。
5. 预期结果：两格式p95≤max(16.67ms,同机ARGB参考p95×4)；GUI转换=0，命中重绘不增加profile查询，显示格式ARGB32 premultiplied，worker转换>0。实际时间与环境如实报告。
6. 后置条件：关闭widget，释放worker和临时文件，恢复quit策略，不保留用户设置。

## D2（P4/P5）：色彩与目标切换
1. 测试目的：验证优化没有退回8位sRGB源，也没有发布旧显示空间的结果。
2. 前置条件：同D1；可控制item的目标色彩空间。
3. 输入数据：含P3颜色的PDF（EPS归一化后的共用绘制层）及SVG；sRGB→DisplayP3→AdobeRGB目标，含任务在途切换。
4. 操作步骤：请求tile，切换目标，等待最终发布；采样显示像素与高精度源到目标转换参考比较；检查呈现空间和任务generation。
5. 预期结果：最终副本属于最后目标且像素误差≤1个8位量化级加渲染容差；旧目标不覆盖新目标，已有高精度源测试继续通过。
6. 后置条件：销毁item，取消上下文回调，恢复测试状态。

## D3（P6）：连续输入和销毁
1. 测试目的：验证交互burst不创建无界任务或悬挂回调。
2. 前置条件：Qt事件循环和生产异步池。
3. 输入数据：连续100次位置/缩放变化，最后一个目标；在途销毁。
4. 操作步骤：不断调用生产paint并读取active+pending深度，随后停止输入等待最后tile；另一次请求立即销毁item。
5. 预期结果：深度始终≤2，最终精确tile就绪，销毁后无崩溃/失效对象访问。
6. 后置条件：线程作业完成并释放临时资源。

## D4（P8/P9）：现有完整回归
1. 测试目的：防止用降密度、部分重绘或去掉色彩转换获得虚假性能。
2. 前置条件：完整测试构建、portable HDR信号。
3. 输入数据：现有矢量密度、拖影、EPS/SVG拖动、色彩、窗口及完整CTest。
4. 操作步骤：定向运行相关用例，再执行完整ctest --test-dir build --output-on-failure。
5. 预期结果：原断言保持并通过；失败记录保留，不通过提高阈值或跳过取得绿色。
6. 后置条件：保存日志及完成报告。
