# GitHub Actions 动画时序测试用例

日期：2026-10-17。按 technical_design_document.md 的 A1–A6 验收。所有用例固化到 movie_resume_static.py、remediation_tests.h 和 CTest 注册中；证据目录 reports/evidence/actions_movie_timing_20261017/。

## S1（A1，静态）：拒绝错误时间假设
1. 测试目的：防止严格正余量和 50ms 隐含上界复发。
2. 前置条件：可读取仓库源文件，Python3。
3. 输入数据：testMoviePauseResume 函数体及事件观察器。
4. 操作步骤：运行 movie_resume_static.py；检查旧表达式不存在，派发前边界和帧观察仍存在。
5. 预期结果：原始代码失败，修复后通过；没有通过删除所有时序/帧断言绕过问题。
6. 后置条件：源文件不变。

## S2（A6，静态）：标准 CI 门禁保留
1. 测试目的：证明新用例注册到失败的 CTest，原有真实 HDR 门禁保留。
2. 前置条件：Python3，可读 CMake/workflow。
3. 输入数据：FovelleMovieResumeDynamic、四个 workflow。
4. 操作步骤：运行 movie_resume_static.py 和 actions_regression_tests.py。
5. 预期结果：延迟用例在 CTest 命令中；现有 required HDR、always 上传等契约全部通过。
6. 后置条件：无外部变更。

## D1（A2、A5，动态）：暂停恢复八行矩阵
1. 测试目的：验证实际定时器派发并保留暂停/帧顺序/像素/复用/预算要求。
2. 前置条件：已构建 fovelle_tests，macOS Cocoa，真实 GIF/APNG fixture。
3. 输入数据：async={false,true} × apng={false,true} × boundedCache={false,true}；speed=50，缓存预算=1。
4. 操作步骤：执行 testMoviePauseResume；publication 时暂停，两轮恢复；最后安装 timer 事件观察器，重复恢复并等待一次派发及下一帧暂停。
5. 预期结果：暂停期间像素和帧数不变；前两轮每次恢复仅前进一帧且匹配参考图；decoderCreations=async?1:0；缓存≤1；观察 active 且 0≤remaining≤interval≤nextFrameDelay；恢复先发布一帧，随后经一次 timer 派发再发布一帧并暂停，合计增加两帧；无 error。
6. 后置条件：stop 并销毁电影/临时文件。

## D2（A3，动态）：异步工作队列受控延迟
1. 测试目的：真实复现耗时超过帧长，零间隔仍正常播放。
2. 前置条件：Qt 测试程序，四线程 QvAsync 池。
3. 输入数据：remediation.gif，async=true，speed=50，四个信号量阻塞作业。
4. 操作步骤：首帧暂停；等四个工作线程阻塞；恢复后不处理 GUI 事件地等待超过帧时长；释放工作线程；观察定时器事件；在第二个后续帧暂停。
5. 预期结果：观察 interval=0、remaining=0、active；实际增加两帧且顺序正确，像素匹配参考图；无 error。
6. 后置条件：作用域保护释放线程，stop 并销毁对象。

## D3（A4，动态）：GUI 延后派发
1. 测试目的：验证已到期但尚未派发的 active 定时器。
2. 前置条件：Qt 测试程序，Cocoa GUI 线程。
3. 输入数据：remediation.gif，async=false，speed=50。
4. 操作步骤：首帧暂停；恢复；不泵事件地等待 interval+80ms；读取 remainingTime；之后派发并在下一帧暂停。
5. 预期结果：等待后 active=true、remaining=0；派发前观察合法边界；帧数增加两帧，顺序/像素正确，无 error。
6. 后置条件：stop、对象清理，无遗留定时器。

## D4（A5，动态）：异步生命周期回归
1. 测试目的：防止修复时引入重复读取、失效结果发布及销毁悬挂。
2. 前置条件：Qt 测试程序与既有 semaphore fixture。
3. 输入数据：testMoviePauseQueuedRead 的 completed/inflight/stop/replace/jump/destroy 及四个 pending-* 行。
4. 操作步骤：执行既有十行，并执行完整 FovelleRemediationDynamic。
5. 预期结果：保留所有既有帧/复用/取消断言，全部通过，无错误信号和停播后帧发布。
6. 后置条件：全部工作线程释放、临时资源清理。

完整回归执行标准 ctest --test-dir build --output-on-failure --timeout 90；不得用过滤/重试的成功代替完整结果。新增延迟矩阵应和八行暂停矩阵重复执行，用于检查稳定性。首次与最终执行日志分别保存。
