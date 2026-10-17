# GitHub Actions 动画时序回归修复设计

日期：2026-10-17（Asia/Shanghai）。基线：a839a581882ae9eb668c0122316cf22fe5c0979a。

## 问题界定与证据

最新 [Checks](https://github.com/inostarlin-passion/Fovelle/actions/runs/39027871692) 和 [Build Fovelle](https://github.com/inostarlin-passion/Fovelle/actions/runs/39027871694) 各有 56/57 个 CTest 通过，唯一失败均为 FovelleMovieResumeDynamic。前者在异步 GIF 无缓存行的 `remainingTime() > 0` 失败；后者在异步 APNG 无缓存行的 `interval() >= nextFrameDelay() - 50` 失败。同次运行的完整 RemediationDynamic 及 HDR 测试通过；Checks 的 format、clang-tidy 作业成功。原始日志和运行元数据保存在 reports/evidence/actions_movie_timing_20261017/。

原子拆解：核对失败提交及作业 → 追踪暂停/恢复、异步队列、发布时间与定时器 → 核验 Qt 时序语义 → 构造受控繁忙情况 → 修复测试观察点和不变量 → 回归与完成报告。

## 多跳检索与交叉核验

1. GitHub 实际运行及失败日志，定位到 tests/remediation_tests.h 的两条断言，排除安装、编译、格式与 HDR 为本次失败原因。
2. [Qt QTimer 官方文档](https://doc.qt.io/qt-6/qtimer.html)：active 定时器可以已到期，remainingTime 为 0；繁忙时允许延后派发，PreciseTimer 不保证绝对准点。
3. [Qt QMovie 官方文档](https://doc.qt.io/qt-6/qmovie.html)：nextFrameDelay 是帧显示时长，恢复继续后续帧；不能将其等同于任意时刻的定时器剩余时间。
4. [Qt QTest 官方文档](https://doc.qt.io/qt-6/qtest.html)：QTRY 通过处理事件和等待反复检查条件；轮询观察点不保证刚好位于定时器启动时。
5. [Qt 6.11.2 定时器源码](https://github.com/qt/qtbase/blob/v6.11.2/src/corelib/kernel/qtimerinfo_unix.cpp)：activateTimers 在 sendEvent 之前计算下一截止时间；事件过滤器内 remaining 也可能重新为正，不应要求派发观察值恒为 0。GUI 场景在不泵事件的等待之后、重新派发之前直接验证过期余量为 0。
6. 仓库 src/qvmovie.cpp 实现交叉核验：异步 loadAsyncFrame 在入队前设定时间基线，publishFrame 以绝对时间线扣除耗时并将非正剩余间隔钳制为 0；暂停清空时间线但保留解码器/在途结果。src/asyncwork.cpp 固定四个工作线程，允许用信号量确定性延迟帧工作。

反向排查：不能由 remainingTime=0 推断定时器未启动；不能由调度间隔比帧长少 50ms 推断恢复错误。旧断言隐含队列/解码延迟上界，本仓库没有此保证。若真正未恢复、重复提交、跳帧、帧内容错误或复用失效，现有语义断言仍应失败。最新 HDR 改动没有修改动画实现；时间相邻不构成因果关系。以实际执行的受控队列/GUI延迟验证上述判断后结束检索，不为绿色构建修改产品行为。

## 原子验收与设计

| ID | 原子标准 | 验证 |
|---|---|---|
| A1 | 回归用例不再要求剩余时间严格正或固定 50ms 耗时上界 | S1 静态 |
| A2 | 恢复后实际派发下一帧定时器；在派发前检查 active、0≤remaining≤interval≤帧长 | D1 八行矩阵 |
| A3 | 异步队列延迟超过帧时长时，零间隔合法且仍按顺序继续 | D2 受控四线程信号量 |
| A4 | GUI 暂停派发使定时器过期后，剩余 0 合法；派发能继续播放 | D3 阻塞线程不泵事件 |
| A5 | 暂停不推进、恢复恰好下一帧/像素正确、解码器复用、缓存有界、重复恢复无副作用及破坏性转换保持 | D1/D4 既有语义断言与 queued-read 十行 |
| A6 | 修复及新增回归随标准 CTest 执行，既有 workflow 门禁保留 | S2 静态、完整 CTest |

设计：使用仅在测试中的 QObject 事件过滤器，在电影子 QTimer 的 QEvent::Timer 派发前捕获状态。此时定时器尚未消耗单次事件，同步/异步路径的 movie.nextFrameDelay 仍属于本次待派发的同一帧。验证 active 与非负边界，不设置外部机器耗时上界。收到该事件后让下一帧发布再次暂停，继续验证实际帧数/顺序，避免仅观察活跃定时器而遗漏真正的恢复错误。

新增延迟矩阵：async-queue 用四个工作线程信号量占满队列，恢复后阻塞超过目标帧长，再释放队列；gui-dispatch 先等待同步路径定时器活跃，再不处理事件地等待超过间隔，然后派发。均要求实际观察到 interval=0 或 remaining=0，并验证下一帧与错误信号。信号量采用作用域清理，断言提前失败也释放线程。

## 正确性论证与范围

令 D 为帧时长，L 为从基线到发布时间的耗时。当前生产调度 I=max(0,D−L)，因此 L>D 或 L>50 时，旧条件 I>0 或 I≥D−50 并非不变量。在尚未处理事件时实际余量 R=max(0,I−E)，其中 E 是等待时间，故 R=0 合法。底层派发器可能在送达 QTimer 事件前先更新截止时间，此时过滤器内的 R 可以再次为正，但不超过 I。正确边界为 active 且 0≤R≤I≤D，事件过滤器在相同调度周期内读取这些值，避免跨帧读值造成伪矛盾。

事件观察后必须发生下一次 publication，且断言帧序/像素/暂停，故不会把未恢复误判为成功；遗漏调度会在观察或后续帧等待时失败。四线程屏障确保队列延迟真实发生，GUI 阻塞确保已到期但未处理事件真实存在。它们构成旧假设的反例，而非放宽产品行为要求。保留既有 1s/2s 完成上界防死锁，不用重试掩盖失败、不跳过用例、不修改工作流。

本次实施步骤 4 的修复对象是已定位的测试基础设施代码；没有证据表明生产播放逻辑有误，因此不修改 src/。本地 SDK/Qt 与托管机可能不同；本地通过不等于新 GitHub run 已通过，报告区分两者。
