# 动图暂停恢复延迟修复设计

日期：2026-10-17（Asia/Shanghai）。范围：QVMovie 异步播放的暂停/恢复生命周期。

## 问题界定及原子拆解

调用链为 MainWindow::pause → QVGraphicsView::setPaused → QVImageCore::setPaused → QVMovie::setPaused。正常浏览启用 setAsyncPlayback(true)。旧实现暂停调用 cancelNextLoad，销毁 asyncDecoder；恢复创建新 AsyncMovieDecoder。GIF 的 read(target) 从 lastFrame=-1 顺序读取直到 target，因而恢复成本包含已播放前缀的重复解码，缓存关闭或目标帧尚未缓存时尤为明显。

拆解为：确认解码器生命周期；保留暂停中的单个任务与结果；恢复下一帧；重置时钟；验证停止/换源/跳帧仍隔离旧结果；固化静态和动态回归。原有三份同名发布故障报告已备份到 evidence/movie_resume_20261017/previous_*。

## 多跳检索与交叉核验

1. [Qt QMovie 文档](https://doc.qt.io/qt-6/qmovie.html)：暂停保留当前帧，恢复后继续下一帧；与本仓库暂停入口核对。
2. 沿帧读取链核对 [QImageReader 文档](https://doc.qt.io/qt-6/qimagereader.html)：动画逐帧 read，单个 reader 独占设备。与 AsyncMovieDecoder::read 的 lastFrame 和顺序读取循环交叉核验。
3. [Qt GIF 原始实现镜像](https://codebrowser.dev/qt6/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp.html)展示增量解码、前帧 disposal 与 backingstore；与上述文档及本地顺序读取代码相符。官方 code.qt.io 源码入口本次抓取 Internal Error，镜像仅用于阅读 Qt 原始实现，不作为独立发行行为保证。
4. [QTimer 文档](https://doc.qt.io/qt-6/qtimer.html)及 [QElapsedTimer 文档](https://doc.qt.io/qt-6/qelapsedtimer.html)核验事件循环计时与单调时钟；暂停必须停止 timer，恢复使用新的时间基准。
5. [Qt Concurrent Run](https://doc.qt.io/qt-6/qtconcurrentrun.html)核验后台工作与异步结果交付模型；实际取消语义以仓库 asyncwork.h 的 ticket 检查为准。

逆向证伪：旧 cancelNextLoad 已清除 nextLoadTime，因此本次不能归因为“没有清理旧时钟”。恢复入口已经立即请求下一帧，因此不能通过简单缩短 timer 修复。不能只保留 decoder 同时取消任务，因为原生同步读可能仍在运行，会让恢复任务并发访问同一 reader。不能取消后复用已读过但未发布的 decoder，否则同一 target 可能触发重新读取。不得取消内存上限来掩盖问题。资料与源码充分支持上述局部原因，停止扩大检索；尚未对用户原始 GIF 做物理显示测量。

## 设计与原子验收标准

| ID | 原子验收标准 |
|---|---|
| A1 | 暂停期间保留异步解码器，未缓存 GIF 恢复不创建第二个解码器。 |
| A2 | Paused 状态不发布帧；已提交任务可完成，但至多暂存一份结果，不持续解码。 |
| A3 | 任务未完成时快速恢复复用同一个任务；任务已完成时消费暂存结果；无重复或跳帧。 |
| A4 | 恢复清除旧截止时间，按当前速度继续；反复暂停/恢复幂等，不积累 timer。 |
| A5 | stop、换源、销毁、手动跳帧仍取消并隔离旧任务，暂存结果不能污染后续播放。 |
| A6 | 同步 GIF、异步 APNG、缓存上限和播放速度回归通过。 |

暂停只 stop timer、清除 nextLoadTime、进入 Paused，不调用 cancelNextLoad。保留 frameTicket，所以恢复期间 loadAsyncFrame 的单任务守卫仍有效。completion 先核对 generation，清除 ticket；如果 Paused，就保存 pendingFrame（包括终止/错误结果），不发布；恢复用 pendingFrame 作为读帧结果，走同一处理路径。cancelNextLoad 继续取消 ticket、释放 decoder，并额外清空 pendingFrame。重复 setPaused(true) 直接返回。测试构建独有宏统计 decoderCreations，生产构建不暴露遥测接口。

## 正确性论证

不变量 I1：仅 frameTicket 为空才提交任务；暂停不清空 ticket。故每个 decoder 同时至多一个任务，不存在暂停恢复造成的 reader 并发访问。

I2：nextFrameNumber 仅在发布或合法循环时递增/重置。Paused completion 只暂存，不改变序号；恢复消费同一 target 的结果，故恢复帧是暂停帧的后继。pendingFrame 只有一槽，且 Paused completion 不调度后继任务，额外持有内存至多一帧（不计 decoder 原有 backingstore）。

I3：所有破坏性状态变化沿 cancelNextLoad 增加 generation，取消 ticket、清空 decoder/pending。旧任务只持有自身 shared_ptr；异步交付受 ticket 和 generation 双重约束，不接触新源。

I4：暂停及恢复均清空截止时间；下一帧基于新 steady_clock 时间累加正常帧时长，暂停时长不参与追赶。恢复成本由已提交任务剩余时间或下一帧解码决定，不再包含从 0 到当前位置的前缀重放；这是工作量保证，不是所有磁盘/CPU负载下的绝对延迟保证。

验证边界：使用真实 Qt GIF/APNG 解码、工作池排队、帧序号与像素比对、timer 和创建次数断言；有限墙钟阈值仅作为本机回归门槛。异步 GIF 循环回到 0、任意手动跳帧仍可能需要从头读，此修复仅消除普通暂停恢复的重复读取。

补充测试可观测性：测试宏还记录 readCompletions，用于确定后台结果已在 GUI 线程交付后才执行 pending-* 状态变化；避免仅凭固定 sleep 推断暂存分支已被覆盖。该统计与 decoderCreations 一样仅存在于 fovelle_tests 构建。
