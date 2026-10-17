# 动图暂停恢复测试规格

所有动态用例位于 tests/remediation_tests.h，由 QtTest 执行；静态用例位于 tests/movie_resume_static.py。每个数据行均独立执行。默认素材 tests/data/remediation.gif（四帧，CC0）以及内嵌两帧 APNG。不修改用户设置。

## S1（A1–A5）暂停生命周期静态审查
1. 测试目的：证明暂停不销毁 decoder、不取消在途任务，破坏性取消仍清除暂存结果。
2. 前置条件：源码可读。
3. 输入数据：src/qvmovie.cpp、tests/CMakeLists.txt。
4. 操作步骤：执行 movie_resume_static.py，检查暂停、completion、cancel、单任务守卫及测试注册。
5. 预期结果：暂停无 cancelNextLoad；timer/clock 复位；Paused completion 暂存；取消清 pending；测试门禁已注册。
6. 后置条件：不修改文件或启动 GUI。

## D1（A1、A2、A3、A4）定时等待中暂停恢复
1. 测试目的：直接检测重复创建 decoder，并验证暂停静止、下一帧与幂等行为。
2. 前置条件：Qt 事件循环；测试构建启用创建次数计数。
3. 输入数据：GIF/APNG；同步/异步；CacheNone/CacheAll（缓存上限1字节），50%速度；两次循环。
4. 操作步骤：收到首帧立即暂停；等待120ms；重复暂停；恢复两次；比较下一帧序号/像素与同步参考；统计 decoderCreations；核对恢复后的 timer。
5. 预期结果：暂停无新增帧；恢复恰为后继；异步创建总数保持1；同步为0；无 error；下一帧在1000ms内到达且 timer 间隔接近正常帧时长，缓存不超过上限。
6. 后置条件：stop，局部对象销毁。

## D2（A2、A3、A5）在途任务的暂停与恢复
1. 测试目的：验证任务排队期间暂停，以及完成后暂存、快速恢复和破坏性操作。
2. 前置条件：四个 semaphore 阻塞工作池线程；scope guard 确保失败也释放。
3. 输入数据：GIF、CacheAll；完成后恢复、完成前恢复，以及任务排队/结果暂存两种时机的 stop、换源、跳帧、销毁，共十行数据。
4. 操作步骤：start 提交排队任务；pause；按数据行操作；释放工作池；在暂存行用测试专用 readCompletions 计数等待任务完成交付；恢复并检查图像/帧序与 decoder 创建次数；停止后等120ms。
5. 预期结果：Paused 行首帧保持-1且不报错；恢复从0开始；跳帧后从2继续到3；换源仅显示新源；销毁无回调；其他行不重复创建 decoder；停止后无帧泄漏。
6. 后置条件：释放阻塞器、停止或销毁 movie，无残留工作池阻塞。

## D3（A6）既有回归
1. 测试目的：确认速度、缓存、APNG及换源隔离未回退。
2. 前置条件：现有 RemediationTests 和 ImageCoreAndMovieTests 可运行。
3. 输入数据：既有 GIF/APNG、缺失文件与替换素材。
4. 操作步骤：运行完整 RemediationTests 与 ImageCoreAndMovieTests。
5. 预期结果：所有测试通过；含 speed 比较、bounded cache、invalid input、queued source replacement 和 APNG 后续帧。
6. 后置条件：测试框架清理临时资源。

执行顺序：设计/规格 → 测试实现 → 旧代码失败证据 → 生产修复 → 复测/回归 → 完成报告。S1提供架构证据，D1–D3提供动态行为证据；不以静态字符串断言替代运行验证。

## 原子验收与代码映射

| 原子标准 | 静态用例 | 动态用例及数据组 |
|---|---|---|
| A1 | S1 pause_lifecycle | D1 async=true 的4行；D2 completed/inflight；decoderCreations必须为1 |
| A2 | S1 completion暂存、单任务守卫 | D1全部8行；D2 completed及4个pending-*行 |
| A3 | S1 pending优先消费、ticket守卫 | D1序号及像素断言；D2 completed/inflight |
| A4 | S1 clock/timer复位 | D1两次暂停恢复、重复resume及timer间隔断言 |
| A5 | S1 cancel清理与generation | D2 stop/replace/jump/destroy及4个pending-*行；D3 queued source replacement |
| A6 | S1 CTest注册 | D1同步GIF/APNG/缓存；D3完整两组现有回归 |

附加证伪实验：在正确实现中仅重新加入暂停时 cancelNextLoad，再执行修正后的 D1；要求同步4行通过、异步4行因 decoderCreations=2 失败。随即恢复源码并重新构建、重跑门禁；此实验验证测试对本次根因敏感。
