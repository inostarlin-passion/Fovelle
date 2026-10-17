# GitHub Actions 动画时序修复测试完成报告

日期：2026-10-17（Asia/Shanghai）。基线提交 a839a581882ae9eb668c0122316cf22fe5c0979a。

## 根因与修复

已从 GitHub 读取同一提交的 [Checks 39027871692](https://github.com/inostarlin-passion/Fovelle/actions/runs/39027871692) 和 [Build 39027871694](https://github.com/inostarlin-passion/Fovelle/actions/runs/39027871694) 的原始失败日志及作业元数据。两次均仅 FovelleMovieResumeDynamic 失败，其余 56/57 CTest 成功；失败分别是异步 GIF 的剩余时间严格正断言、异步 APNG 的固定 50ms 调度耗时上界断言。

[Qt QTimer 文档](https://doc.qt.io/qt-6/qtimer.html)、[QMovie 文档](https://doc.qt.io/qt-6/qmovie.html)、[Qt 6.11.2 定时器派发源码](https://github.com/qt/qtbase/blob/v6.11.2/src/corelib/kernel/qtimerinfo_unix.cpp) 与仓库的绝对时间线调度互证：过期余量 0 及解码/排队耗时导致间隔缩短都是允许行为。修复测试观察点和合法不变量，不改动生产播放实现或 workflow，不移除已有帧序、像素、解码器复用、缓存预算及生命周期断言。

改动：
- tests/remediation_tests.h：MovieTimerObservation 事件过滤器；八行暂停恢复矩阵继续验证实际定时器派发后的下一帧；新增异步队列延迟和 GUI 过期派发场景。
- tests/movie_resume_static.py：新增 S1/S2，禁止旧断言复发并确保新增动态用例进入标准 CTest。
- tests/CMakeLists.txt：将延迟矩阵加入原失败的 FovelleMovieResumeDynamic。完整 RemediationTests 也自动执行新增 Qt slots。
- 指定的设计、用例和完成报告已更新；旧版本先备份到本地证据目录。

## 环境与执行证据

本地 macOS 27.0.1、arm64、Qt/QtTest 6.11.2、Apple LLVM 17.0.0，CMake Release，部署目标 15.0，BUILD_TESTS=ON，FOVELLE_REQUIRE_PORTABLE_HDR=ON。托管工作流使用 macos-26，因此下列本地结果不等同于新托管运行结果。

证据目录：reports/evidence/actions_movie_timing_20261017/（仓库现有 ignore 规则忽略此目录；日志实际保存在本地，未修改 ignore 规则）。

| 验收项 | 测试及证据 | 结果 |
|---|---|---|
| A1 | movie_resume_static.py，static-red.log → static-green.log | 新增测试在修复前 2 失败、既有生命周期测试通过；修复后 3/3 通过 |
| A2 | testMoviePauseResume，focused.log | 八行矩阵全部通过，事件观察后真实帧序和像素通过 |
| A3 | testMovieResumeDelayedDispatch(async-queue)，focused.log | 真正阻塞四线程工作池后 interval=0、remaining=0，后续帧顺序及像素正确 |
| A4 | testMovieResumeDelayedDispatch(gui-dispatch)，focused.log | 不处理事件的等待后直接断言 active 且 remaining=0；事件过滤器内仍验证边界，后续帧继续 |
| A5 | testMoviePauseQueuedRead，focused.log | 十行全部通过；定向 Qt 总计 22 passed、0 failed、0 skipped（含初始化/清理） |
| A6 | movie_resume_static.py、actions_regression_tests.py | 静态注册契约及既有 Actions 门禁契约全部通过；Actions 回归 10/10 |
| 既有相邻预加载策略 | preload_policy_quality.py，preload.log | 11/11 通过 |
| 稳定性 | ctest -R '^FovelleMovieResume' --repeat until-fail:3，repeated.log | 两个 CTest 各连续三次通过，约 21.26 秒；不是失败后重试取成功 |
| 构建 | cmake --build build --parallel 2，build-final.log | 成功；存在既有 Qt 弃用警告 |
| 完整回归 | QT_QPA_PLATFORM=cocoa ctest --test-dir build --output-on-failure --timeout 90，ctest-full.log | **60/60 全部通过，0 失败，总耗时 559.31 秒**；含真实 HDR、部署包、全量修复套件及专项回归 |
| 补丁完整性 | git diff --check | 通过 |

新增测试首次执行发现两处 fixture 问题，原始记录保留在 fixture-correction.log：随机跳帧参考电影需要 CacheAll；Qt 底层在送达事件前更新截止时间，不能要求过滤器内剩余值总为 0。修正参考 fixture，GUI 场景改为在未处理事件时验证过期 0，派发时验证合法区间；异步零间隔仍必须为 0。此后定向及重复执行通过。

附加格式审查：执行 build.sh --format-check 发现大量既有格式诊断，而且脚本无条件 exit 0，不能据其退出码宣称格式全量通过。单独比较此次修改的头文件：HEAD 基线 19 条、最终也是同样的 19 条既有诊断；新增代码未增加诊断。保留 format-baseline-header.log、format-final-header.log、format.log。本次远端 format 作业本就成功；未扩大任务去格式化整个仓库或修改该脚本。未单独重跑 clang-tidy，生产代码未改、失败提交的该作业已成功。

全量运行中 FovelleMovieResumeDynamic 用时 7.07 秒并通过，FovelleRemediationDynamic 用时 17.07 秒并通过；真实 HDR 门禁 28.61 秒、部署包 49.31 秒。完整日志和最终源文件 SHA-256 清单保存在证据目录。A1–A6 均有对应已执行的通过证据。

## 验证边界

未提交或推送代码，也未触发包含本地修改的新 GitHub Actions run；远端修复后的状态仍待代码发布后确认。静态契约、受控动态反例和三次重复验证已覆盖本次时序误报路径，但有限次数执行不构成所有机器时序永不失败的证明。实际业务正确性仍由保留的帧/像素/状态/生命周期断言约束。
