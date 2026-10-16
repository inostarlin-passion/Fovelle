# GitHub Actions 修复测试用例规范

日期：2026-10-17。代码：tests/stapler_retry_tests.py。标准 R1–R6 定义于技术设计文档。所有用例无需 Apple 凭证，无网络写入。

## S1 — test_static_release_gates（静态，R5）

1. 测试目的：验证 R5，全部调用包装器，失败退出及发布前验证保留。
2. 前置条件：Python 3；仓库完整。
3. 输入数据：发布脚本、CTest 注册。
4. 操作步骤：解析三个调用点及 gates 顺序；执行对应 unittest 方法。
5. 预期结果：全部调用包装器，失败退出及发布前验证保留。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D1 — test_success_once（动态，R1）

1. 测试目的：验证 R1，一次执行，参数数组原样传递，无等待。
2. 前置条件：Python 3；mock xcrun 返回 0。
3. 输入数据：validate，含空格路径。
4. 操作步骤：调用 run_stapler；执行对应 unittest 方法。
5. 预期结果：一次执行，参数数组原样传递，无等待。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D2 — test_transient_recovery（动态，R2）

1. 测试目的：验证 R2，返回 0，执行三次，等待 10/20 秒。
2. 前置条件：Python 3；mock 调用与等待。
3. 输入数据：68/-1005 两次后 0。
4. 操作步骤：依次注入三次结果；执行对应 unittest 方法。
5. 预期结果：返回 0，执行三次，等待 10/20 秒。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D3 — test_exhaustion（动态，R3）

1. 测试目的：验证 R3，返回 68，五次执行，等待 10/20/40/60 秒。
2. 前置条件：Python 3；mock 调用与等待。
3. 输入数据：连续五次 68/-1005。
4. 操作步骤：运行至耗尽；执行对应 unittest 方法。
5. 预期结果：返回 68，五次执行，等待 10/20/40/60 秒。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D4 — test_permanent_failures（动态，R4）

1. 测试目的：验证 R4，原码失败，一次执行，无等待。
2. 前置条件：Python 3；mock 调用与等待。
3. 输入数据：68 无网络证据、65、66、77、-1009。
4. 操作步骤：逐项独立运行；执行对应 unittest 方法。
5. 预期结果：原码失败，一次执行，无等待。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D5 — test_timeout_and_missing_tool（动态，R3/R4）

1. 测试目的：验证 R3/R4，返回 124/127，不重试，调用 timeout=120。
2. 前置条件：Python 3；mock 抛出异常。
3. 输入数据：120 秒超时、工具缺失。
4. 操作步骤：逐项注入异常；执行对应 unittest 方法。
5. 预期结果：返回 124/127，不重试，调用 timeout=120。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D6 — test_recovery_then_permanent_failure（动态，R4）

1. 测试目的：验证 R4，第二次返回 65，不能沿用首次重试资格。
2. 前置条件：Python 3；mock 调用与等待。
3. 输入数据：网络 68 后无效票据 65。
4. 操作步骤：依次注入结果；执行对应 unittest 方法。
5. 预期结果：第二次返回 65，不能沿用首次重试资格。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D7 — test_cli_logs_paths_and_failure（动态/CLI，R1/R6）

1. 测试目的：验证 R1/R6，六组均原码返回，stdout/stderr 诊断与次数保留。
2. 前置条件：Python 3；临时可执行 xcrun 替身。
3. 输入数据：staple/validate × 0/66/68，空格路径。
4. 操作步骤：真实 Python 子进程执行 CLI；执行对应 unittest 方法。
5. 预期结果：六组均原码返回，stdout/stderr 诊断与次数保留。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## D8 — test_cli_invalid_action（动态/CLI，R4）

1. 测试目的：验证 R4，参数解析拒绝并返回 2。
2. 前置条件：Python 3；真实 Python 子进程。
3. 输入数据：delete action。
4. 操作步骤：执行 CLI；执行对应 unittest 方法。
5. 预期结果：参数解析拒绝并返回 2。
6. 后置条件：mock 自动恢复；临时目录删除；仓库及用户偏好未改变。

## 执行与反证

先固化测试，再运行生产修复前红灯；实现后执行全部九个方法及现有 Actions 回归。静态 S1 防止实际发布漏接包装器；动态故障注入证明退出码与重试边界，CLI 用例证明参数和日志穿过真实进程。不能将替身结果等同真实 Apple 服务验证。修复前后日志保存在 reports/evidence/actions_release_20261017/。


---

<details>
<summary>此前任务记录（完整保留，非本次结论）</summary>

# GitHub Actions 失败修复：原子验收与测试用例

日期：2026-10-16。所有下列新用例先固化为代码，取得旧实现红证据，再修改测试基础设施/门禁实现。静态测试负责结构契约，动态测试负责实际行为；伪子进程仅测试门禁执行器，不冒充原生 HDR 通过。

## CI1：滴流总截止时间语义

1. **测试目的**：滴流总截止时间语义，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：带真实进度的 loopback 流；总时限 600ms、空闲 2000ms；精确比较翻译后的总超时消息。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `testDownloadTotalTimeout`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。

## CI2：进度重置空闲计时

1. **测试目的**：进度重置空闲计时，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：持续流传输超过两倍空闲时限仍不失败；随后取消；原有停滞输入仍触发空闲超时。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `testDownloadProgressKeepsIdleAlive`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。

## CI3：HDR 测试环境可移植且无泄漏

1. **测试目的**：HDR 测试环境可移植且无泄漏，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：原环境未设置、1、2.75及 current-headroom=1；作用域内固定 HDR 测试能力，退出精确恢复。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `testScopedHDRDisplayRestoresEnvironment`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。

## CI4：SDR runner 执行真实 HDR 连续性

1. **测试目的**：SDR runner 执行真实 HDR 连续性，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：父环境 headroom=1；真实 DNG/gain-map JPEG、原生 float 像素和十个全屏数据行都执行，禁止 skip。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `FovellePortableHDR`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。

## CI5：缺失、重复或错误数据行不通过

1. **测试目的**：缺失、重复或错误数据行不通过，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：十个合法 DataTag 与缺一、重复、额外、fail/skip 变异 XML。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `test_required_rows_are_exact`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。

## CI6：子进程失败、超时可诊断且有限退出

1. **测试目的**：子进程失败、超时可诊断且有限退出，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：伪测试进程退出 7／挂起／部分 XML；保留 stdout+stderr、JSON 状态和原始 XML；抛出错误而非成功。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `test_run_suite_failure_and_timeout`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。

## CI7：成功门禁要求精确执行集合

1. **测试目的**：成功门禁要求精确执行集合，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：伪进程输出指定 suite/函数的合法 XML；真实 gate 同时验证对应 suite 名称、用例和数据行。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `test_run_suite_success`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。

## CI8：两个失败工作流及发布链路保存证据

1. **测试目的**：两个失败工作流及发布链路保存证据，防止仅本机通过而无法在 Actions 判定。
2. **前置条件**：隔离 QtTest/临时目录；真实网络使用 loopback；HDR 使用提交的哈希样本，具有 Cocoa/Metal 桌面；执行器单测使用受控伪进程。
3. **输入数据**：CI 必开 portable HDR；if: always() 上传 XML、日志、JSON；保留编译/格式/clang-tidy 检查与原断言。
4. **操作步骤**：静态检查对应夹具/runner/workflow 契约；动态执行 `test_workflow_gate_contract`，读取真正信号、XML、退出码及保留日志，并执行缺失/错误/超时反例。
5. **预期结果**：成功路径满足全部条件；错误及超时必须失败并保留原因；不降低像素/几何阈值、不改为 skip、不吞错误。
6. **后置条件**：回收子进程与 sockets；精确恢复两个 HDR 环境键；删除单测临时目录；正式门禁保留结果供 Actions 上传。


## 补充反例（CI3、CI6、CI7）

| 目的 | 前置条件 | 输入数据 | 步骤 | 预期 | 后置条件／代码 |
|---|---|---|---|---|---|
| 精确环境恢复 | QtTest 环境独立且有外层恢复 guard | 两个键各未设置、已设置空值、1、2.75，组合 16 种 | 进入/退出 ScopedHDRTestDisplay 并比对存在性及原字节 | 作用域内均为 4，退出精确复原；颜色用例使用原参考值 | 恢复调用者原环境；testScopedHDRDisplayRestoresEnvironment |
| 拒绝旧 XML | 临时目录可写、受控伪进程 | 旧成功 XML；新进程退出 0 但无 XML | 调用正式 run_suite | 失败而非复用旧通过记录 | 清理进程/目录；test_run_suite_rejects_stale_xml |
| 阶段提前失败不得遗留后续成功 | 临时目录预置后续 suite 的旧 XML/log/JSON | LaterSuite 三个旧文件 | 调用正式 prepare_output | 旧 XML/log 删除，JSON 为 not-run | 删除目录；test_prepare_output_rejects_stale_later_suite |
| 预算耗尽不启动子进程 | 对 Popen 设置 mock 观测 | timeout=0 | 调用正式 run_suite | 抛出失败、写 timeout JSON、未调用 Popen | 删除目录；test_expired_budget_does_not_launch |

## CI9：执行器单测包含解释器冷启动

1. **测试目的**：区分正常非零退出和真正超时，避免 200ms 启动假设。
2. **前置条件**：临时伪进程、父进程有可写证据目录。
3. **输入数据**：启动先等待 350ms；随后退出 7 或挂起 10s；分别使用 5s 守卫。
4. **操作步骤**：调用正式 run_suite，核对分类、退出码、部分 XML 与日志；与旧 200ms 守卫对照。
5. **预期结果**：非零退出保持 failed/7；挂起保持 timeout；两者均不得通过；有限退出预算保留。
6. **后置条件**：回收进程/进程组，清理单测目录；代码 test_run_suite_failure_and_timeout。

## CI10：功能性原生解码守卫

1. **测试目的**：原字节/原生解码能力不被未声明的 5s 冷启动 SLA 误判。
2. **前置条件**：真实 loopback 下载、提交的 gain-map JPEG/DNG 与其他原格式控制。
3. **输入数据**：原有五类容器；ImageIO 光栅容器 15s，Qt SVG 5s，组上限 90s。
4. **操作步骤**：静态核对按格式预算；动态实际解码，记录类型/字节/预算/耗时并继续所有像素/元数据/帧数/矢量断言。
5. **预期结果**：按实际解码调用链采用冷启动预算，结果不变；超预算仍失败。
6. **后置条件**：释放 loader/临时容器；代码 test_native_decode_budget_contract 与 testDownloadPreservesSources。

---

# 历史记录：本轮之前

# Fovelle 问题 1–10：原子验收与测试用例

日期：2026-10-16。先建立测试，再实现生产代码；红阶段证据记录在完成报告。所有测试有独立输入与清理，不修改用户偏好。静态测试负责构建/线程边界契约，动态测试负责可观察行为。

## R1 / 原问题 1 / `testDownloadThreadAffinity`

1. **测试目的**：网络对象、计时器、下载文件仅由所属主线程访问。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：下载本地 HTTP PNG；检查回调线程及字节结果。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadThreadAffinity` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：网络对象、计时器、下载文件仅由所属主线程访问；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R2 / 原问题 2 / `testDownloadPreservesSources`

1. **测试目的**：下载原字节不变，后缀仅提示解码，不重编码。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：APNG/SVG/带元数据 PNG 逐字节比对，并走统一加载器。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadPreservesSources` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：下载原字节不变，后缀仅提示解码，不重编码；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R3 / 原问题 3 / `testDownloadCleanup`

1. **测试目的**：成功、失败、取消后 reply 和部分文件全部释放。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：循环成功/404/取消请求；排空 DeferredDelete 后计数与目录基线一致。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadCleanup` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：成功、失败、取消后 reply 和部分文件全部释放；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R4 / 原问题 4 / `testDownloadIdleTimeout`

1. **测试目的**：无数据超过空闲时限终止。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：HTTP 接收请求后不发数据，使用 100ms 测试策略。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadIdleTimeout` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：无数据超过空闲时限终止；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R5 / 原问题 4 / `testDownloadTotalTimeout`

1. **测试目的**：持续滴流也受总时限约束。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：每 30ms 发送字节，空闲 150ms，总时限 220ms。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadTotalTimeout` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：持续滴流也受总时限约束；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R6 / 原问题 5 / `testDownloadLimits`

1. **测试目的**：已知长度超过预算在下载前拒绝。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：预算 1024 字节；Content-Length 为 1025。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadLimits` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：已知长度超过预算在下载前拒绝；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R7 / 原问题 5 / `testDownloadLimits`

1. **测试目的**：无长度/分块响应累计超过预算终止，恰好预算成功。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：1024/1025 字节边界，chunked 和连接关闭定界。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadLimits` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：无长度/分块响应累计超过预算终止，恰好预算成功；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R8 / 原问题 5 / `testDownloadProgressAndCancel`

1. **测试目的**：未知/零长度进度无除零、取消不弹错误框。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：未知长度及取消；只发一次终态。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDownloadProgressAndCancel` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：未知/零长度进度无除零、取消不弹错误框；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R9 / 原问题 6 / `test_delivery_contract / remediation_delivery.py`

1. **测试目的**：普通 CI 打包前部署 Qt，打包后验证库和平台插件。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：静态检查 workflow 顺序；动态执行部署后的 bundle 验证器。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `test_delivery_contract / remediation_delivery.py` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：普通 CI 打包前部署 Qt，打包后验证库和平台插件；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R10 / 原问题 7 / `testFolderScanAsync`

1. **测试目的**：目录扫描采用无 QObject 跨线程访问的值快照。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：创建乱序文件、隐藏文件、递归标志；异步返回自然顺序。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testFolderScanAsync` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：目录扫描采用无 QObject 跨线程访问的值快照；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R11 / 原问题 7 / `testFolderScanGeneration`

1. **测试目的**：旧目录扫描结果不能覆盖新目录，关闭后不能回调。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：A→B 快速切换及销毁；只提交最后请求。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testFolderScanGeneration` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：旧目录扫描结果不能覆盖新目录，关闭后不能回调；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R12 / 原问题 7 / `testDirectoryOpenAndNavigation`

1. **测试目的**：目录作为输入可异步打开首图，首图先于整目录扫描也可呈现。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：目录打开、首图、导航、刷新及空目录。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testDirectoryOpenAndNavigation` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：目录作为输入可异步打开首图，首图先于整目录扫描也可呈现；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R13 / 原问题 8 / `testMovieBoundedCache`

1. **测试目的**：动画缓存按实际 QImage 字节计量且不超过预算。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：有限预算下多帧循环、超预算单帧，不缓存大帧。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testMovieBoundedCache` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：动画缓存按实际 QImage 字节计量且不超过预算；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R14 / 原问题 8 / `testMovieBoundedCache`

1. **测试目的**：被淘汰的帧仍可逐帧回退且像素一致。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：0→1→0；比较独立解码结果。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testMovieBoundedCache` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：被淘汰的帧仍可逐帧回退且像素一致；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R15 / 原问题 8 / `testMovieAsyncPlayback`

1. **测试目的**：生产动画帧在工作线程解码，主线程只呈现。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：异步 start、换源、暂停恢复、stop 后无迟到 frameChanged。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testMovieAsyncPlayback` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：生产动画帧在工作线程解码，主线程只呈现；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R16 / 原问题 9 / `testLoaderNonblockingShutdown`

1. **测试目的**：加载器销毁/取消不阻塞等待工作线程，不产生迟到回调。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：注入可控慢解码函数，销毁耗时小于 100ms；等工作完成后无回调。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testLoaderNonblockingShutdown` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：加载器销毁/取消不阻塞等待工作线程，不产生迟到回调；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R17 / 原问题 9 / `testShutdownProcess`

1. **测试目的**：应用只有限等待读任务；正常路径排空，卡死路径保存设置后退出。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：独立 probe 进程运行超过截止时间的任务；验证退出码和磁盘标记，父进程硬超时 5s。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testShutdownProcess` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：应用只有限等待读任务；正常路径排空，卡死路径保存设置后退出；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R18 / 原问题 10 / `test_real_fixture_manifest / test_absent_and_corrupt_fixture_fail`

1. **测试目的**：可移植 HDR gain-map 与 RAW DNG 样本可重复生成、登记哈希。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：仓库提供原创生成器/样本说明；缺失和损坏样本均失败。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `test_real_fixture_manifest / test_absent_and_corrupt_fixture_fail` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：可移植 HDR gain-map 与 RAW DNG 样本可重复生成、登记哈希；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R19 / 原问题 10 / `testPortableHDR`

1. **测试目的**：发布门禁真图测试必须执行、禁止缺样本跳过。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：真实 gain-map JPEG、合成 Bayer DNG 解码、HDR 原生像素与全屏；XML 检查 skips=0。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `testPortableHDR` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：发布门禁真图测试必须执行、禁止缺样本跳过；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。

## R20 / 原问题 1–10 / `existingQtSuites`

1. **测试目的**：现有图片加载、动画、窗口及导航行为不回归。
2. **前置条件**：构建 macOS Qt 6.11.2 测试目标；使用临时目录、隔离 QSettings、本地 loopback HTTP；HDR 用例具有本轮可移植样本。
3. **输入数据**：执行已有 QtTest / CTest，区分真实设备与模拟指标。静态检查输入为本轮生产源文件、CMake 和 workflow；不依赖远程服务。
4. **操作步骤**：静态检查对应接口、注册/调用顺序及边界契约；动态调用 `existingQtSuites` 或注明的现有测试组，等待真实信号/子进程结束，读取实际文件、像素、XML 或动态库依赖。失败注入必须改变可观察输出。
5. **预期结果**：现有图片加载、动画、窗口及导航行为不回归；超时测试用缩短策略而非等待生产时长；使用父进程超时避免阻塞整套测试。失败、跳过不得视作通过。
6. **后置条件**：取消剩余请求/票据、排空延迟删除、移除临时文件、恢复测试环境，独立子进程被回收，无用户文件被写入。



## 补充反例用例（对应既有原子验收，均已固化）

| 用例／代码 | 测试目的 | 前置条件 | 输入数据 | 操作步骤 | 预期结果 | 后置条件 |
|---|---|---|---|---|---|---|
| R2：testDownloadPreservesSources | 排除只保留普通 PNG 的假修复 | loopback HTTP 与 portable-hdr 样本可读 | 带 ICC/作者的 PNG、APNG、SVG、gain-map JPEG、Bayer DNG | 下载并逐字节比较；调用真实 loader，检查动画帧数、矢量与 native HDR/RAW 标记 | 原容器不变、标记与格式能力保留 | 销毁下载与加载器，释放临时目录 |
| R2/R3：testWindowUrlAnimationAndArtifactLifetime | 验证真实窗口成功下载链路与容器所有权 | loopback server 与隔离 MainWindow | 两帧 APNG 原容器 | MainWindow.openUrl，等待动画加载，查真实 core 帧数和文件字节，销毁窗口后查临时文件 | 两帧保留、原字节一致、窗口存活时文件可读、销毁后删除 | 窗口、事务和 decoder 取消，临时文件移除 |
| R3：testDownloadHttpFailureCleanup | HTTP 错误不得残留 reply 或部分文件 | FovelleDownload 临时目录基线已记录 | 404 与错误正文 | 请求、等待 failed、排空延迟删除，对比目录列表与 reply 计数 | 一次失败、零 reply、目录回到基线 | 本地 server 停止 |
| R14：testMovieBoundedCache | 不把 APNG 的原生随机读取当作 GIF 也正确的证据 | 四帧原创 GIF 与 APNG | 1024/16/1 字节缓存预算 | 跳至第 0、末尾、回到 0；检查像素与缓存字节 | 被淘汰帧可重建，预算不突破 | 文件与 decoder 释放 |
| R15：testMovieAsyncPlaybackSpeed | 保留异步播放速度，避免解码时间系统性累加 | 原创四帧 GIF 可读，事件循环运行 | 100% 与 200% 播放，分别观察 20 帧 | 记录 frameChanged 时间，等待上限 3s | 两轮均达到 20 帧，200% 耗时小于 100% 的 85% | 停止并销毁 movie |
| R15：testMovieAsyncInvalidInput | 解码失败不得伪装为空白有效帧 | 临时目录，异步 movie | 不存在的 GIF 路径 | start 后观察状态、error、frameChanged 和 cache | NotRunning、一次错误、零有效帧、零缓存字节 | decoder 取消、目录清理 |
| R4：testDownloadTranslatedFailure | 错误语义在翻译后仍正确呈现 | 已编译简体中文 QM；原翻译器有恢复 guard | loopback 停滞响应、50ms 空闲策略 | 安装 translator 后触发正式下载错误 | 收到“下载等待数据超时”；其余四份 TS 有完整同类文本 | 移除临时 translator |
| R15：testMovieQueuedSourceReplacement | 排除工作排队时换源的旧帧泄露 | 四个受控只读任务占满池；有释放 scope guard | 旧 APNG 与青色 PNG | 排队旧帧、立即换源，观察主线程 heartbeat；释放工作池后检查首帧 | 主线程响应；只呈现新源颜色 | 停止 movie、释放所有阻塞任务 |
| R17：testShutdownProcess | 真实应用析构有限等待，而非仅帮助函数通过 | 隔离子进程与 IniFormat 设置 | 30ms 正常任务、10s 慢任务 | 保存设置、启动读任务、直接返回使应用析构；父进程等待最多 5s | 正常退出 0，超时退出 75，已保存设置可读 | 父进程清理目录 |
| R9：remediation_delivery.py | 部署后实际解压产物能加载 Qt 并启动 | macdeployqt、codesign、ditto 可用 | 当前构建 app 的临时副本 | 部署、规范化、签名、压缩解压、验证全部 Mach-O、在隔离设置中 --version；删除 Cocoa 插件再验证 | 解压后真实启动成功；缺插件反例失败 | 删除整个部署临时目录 |
| R20：键盘轨迹／HDR 原生连续性 | 验证输入或 HDR 活动状态的必要前提 | 可激活的 Cocoa 桌面窗口 | 标准 DPI、2× DPI、外置 RAW 与 portable DNG | raise/activate 后等待 qWaitForWindowActive；继续原来的真实输入与全部像素/几何断言 | 必须获得活动窗口；不放宽任何结果阈值，不将失败转 skip | 退出全屏、关闭窗口、恢复设置 |
| R18/R19：remediation_gate_tests.py | 验证门禁无法以缺样本／跳过蒙混通过 | 临时目录与 XML | 缺文件、损坏文件、缺函数、空执行、skip/fail/xfail XML | 分别调用正式验证函数 | 所有反例抛出异常，完整 pass XML 接受 | 删除临时数据 |

---

# 历史记录（本轮之前）

# 2026-10-10 增补：Keep zoom level 持久化回归

先新增静态门禁和五行动态用例，在生产修复前取得失败，再移除错误迁移项。原有同进程对话框测试保留，但不再将它视为重启持久化证明。可执行用例清单位于 `tests/keep_zoom_persistence_cases.json`，代码位于 `tests/zoom_color_acceptance.inc` 与 `tests/keep_zoom_persistence_static.py`。

## KZ-P1：恢复为可配置的保持缩放键不再被启动迁移强制重置

类型：静态；代码：`tests/keep_zoom_persistence_static.py`。

- 测试目的：阻止恢复设置入口后仍残留在 removedPreferenceDefaults 的回归
- 前置条件：可读源码树
- 输入数据：迁移表、默认注册、设置反向绑定、main 启动顺序
- 操作步骤：检查重置表不包含 navresetszoom；默认仍为true、绑定仍取反；确认迁移在生产应用构造前执行
- 预期结果：活动设置不被迁移覆盖，保留原默认及真实启动路径
- 后置条件：不修改文件或设置

## KZ-P2：迁移保留已保存的开/关，并保持缺省及幂等性

类型：动态/三行数据；代码：`FeatureTests::testKeepZoomSurvivesStartupMigration`。

- 测试目的：补上同进程重建对话框未覆盖的启动迁移阶段
- 前置条件：隔离INI测试目录，firstlaunch=true，保存并作用域恢复原值
- 输入数据：navresetszoom=false、true、缺失；退役 navigationregionsenabled=true
- 操作步骤：写入输入并sync；连续运行两次真实迁移；各轮检查磁盘、全新SettingsManager、真实复选框；核对退役策略仍归一化
- 预期结果：false/true原样保留；缺失不强行写入且默认保持关闭；复选框与反向值一致；退役策略仍为false
- 后置条件：恢复原测试设置；不禁用或绕过迁移

## KZ-P3：勾选保持缩放后跨独立进程启动仍保持勾选

类型：动态/INI及macOS NativeFormat；代码：`FeatureTests::testKeepZoomPersistsAcrossProcesses`。

- 测试目的：检出启动前后被迁移覆盖，排除同进程缓存造成的假通过
- 前置条件：临时共享INI目录或UUID专属原生偏好域；每个子进程按生产顺序先迁移再构造应用
- 输入数据：seed→enable→read→read
- 操作步骤：种子进程初始化标记；写进程点击真实复选框并正常退出；两个全新读进程分别执行启动迁移并读取UI、磁盘与manager
- 预期结果：写入及两个重启均checked=true、storedReset=false、managerReset=false、status=NoError
- 后置条件：独立子进程结束；作用域清理测试域键和临时目录，失败路径也执行清理

## KZ-P4：取消勾选同样持久化且不改变新安装默认

类型：动态/双向回归；代码：`FeatureTests::testKeepZoomPersistsAcrossProcesses / FeatureTests::testKeepZoomSurvivesStartupMigration`。

- 测试目的：防止修复以强制开启所有用户代替保存选择
- 前置条件：前面已经成功跨进程保存勾选状态
- 输入数据：disable→read；无键默认输入
- 操作步骤：另起进程取消勾选并退出，再起进程迁移后读取；独立检查无键默认
- 预期结果：取消后及重启均checked=false、storedReset=true；默认仍关闭保持
- 后置条件：保持用户开/关语义及其它迁移策略，不写真实Fovelle偏好域

---

# 2026-10-10 缩放保持与色彩信息：验收及测试用例

用例先于生产代码固化，边界用例在验证中补充。可执行代码位于 `tests/zoom_color_acceptance.inc` 和 `tests/zoom_color_acceptance_static.py`。每个标准均关联实际测试；执行结果见测试完成报告。

## Z1：切图保持已提交数值且不复活适应模式

类型：动态/数据驱动；代码：`GraphicsViewTests::testKeepZoomAcrossNavigation`。

- 测试目的：验证不同图像尺寸和渲染路径保持比例
- 前置条件：临时目录，navresetszoom=false，窗口可显示
- 输入数据：PNG/XPM/SVG；640×400→220×700；1%、120%、6400%
- 操作步骤：打开A，绝对缩放，打开B，改变窗口大小，再打开缓存A
- 预期结果：每次成功显示与resize后数值不变，计算模式为空
- 后置条件：临时图、窗口销毁，选项恢复

## Z2：设置可见、反向绑定及立即持久化

类型：动态；代码：`FeatureTests::testKeepZoomPreferenceAndColorTranslations`。

- 测试目的：检查UI真正连接原设置键
- 前置条件：保存原选项，navresetszoom=true
- 输入数据：保持缩放复选框
- 操作步骤：创建设置，勾选，查询QSettings和SettingsManager，重建设置
- 预期结果：初始未勾选，点击后原键=false，新设置显示勾选
- 后置条件：原选项恢复，无新设置键

## Z3：计算模式、失败和快速请求有确定行为

类型：动态；代码：`GraphicsViewTests::testKeepCalculatedZoomAndFailedNavigation`。

- 测试目的：证伪只关闭重置、错误占位覆盖与旧请求回滚
- 前置条件：临时图片及损坏PNG，保持开启
- 输入数据：适应比例、120%、加载中150%；A/B/C/坏图
- 操作步骤：适应A后切B并resize；120%→坏图→C；快速请求并缩放；关闭再打开
- 预期结果：保持适应所得数值，坏图不覆盖，最新请求为150%，关闭后恢复首张默认
- 后置条件：窗口和所有选项恢复

## C1：源空间与ICC在转换前采集且缓存携带

类型：动态；代码：`ImageCoreAndMovieTests::testSourceColorMetadata`。

- 测试目的：区分文件内嵌ICC与Qt生成profile
- 前置条件：Qt PNG写入器可用，临时目录
- 输入数据：Display P3 ICC PNG、无标记PNG、未知字节
- 操作步骤：读文件源元数据，经过异步loader，再查询无标记文件
- 预期结果：P3原始ICC保留，无标记为Absent，未知为Unknown
- 后置条件：临时文件销毁，无颜色设置变化

## C2：损坏或不支持不能误报未内嵌

类型：动态/恶意输入；代码：`ImageCoreAndMovieTests::testSourceColorMetadata / ImageCoreAndMovieTests::testColorContainerBoundaries`。

- 测试目的：检查完整性与读取边界
- 前置条件：有效ICC PNG字节
- 输入数据：损坏iCCP名称/CRC、截断PNG、结构合法但不支持ICC、受限解压输入
- 操作步骤：分别写入临时文件并解析
- 预期结果：损坏profile为Invalid，不支持为Unsupported，截断/超限为Unknown，无越界或无界分配
- 后置条件：测试输入清理

## C3：输出空间由当前可见渲染路径提供

类型：动态/集成；代码：`ImageCoreAndMovieTests::testColorInformationFollowsPresentation`。

- 测试目的：证伪以Qt目标设置冒充原生Metal输出
- 前置条件：macOS Metal可用，Qt目标sRGB
- 输入数据：P3 PNG→Qt XPM
- 操作步骤：加载P3等待实际呈现，读取输出；打开信息区再切Qt图
- 预期结果：源仍为P3；Metal为Extended Linear Display P3；Qt为实际sRGB，源ICC不串图
- 后置条件：选项与窗口清理

## C4：信息区刷新并在无图时清除旧状态

类型：动态/UI；代码：`ImageCoreAndMovieTests::testColorInformationFollowsPresentation`。

- 测试目的：检查可见信息区与当前快照一致
- 前置条件：信息区可见且接入实时值提供器
- 输入数据：原生图、Qt图、关闭图片
- 操作步骤：跨分支切图并关闭
- 预期结果：三行从同一快照更新，关闭显示No output且无旧ICC
- 后置条件：对话框关闭，定时器随对象销毁

## L1：入口、构建与四个翻译目录完整

类型：静态；代码：`tests/zoom_color_acceptance_static.py`。

- 测试目的：防止遗漏UI、上下文、占位符或构建文件
- 前置条件：源码目录可读
- 输入数据：UI XML、四份TS、CMake/qmake
- 操作步骤：解析入口与键集合并检查占位符
- 预期结果：每个键译文非空且完成，英文使用源码，两套构建均登记新文件
- 后置条件：源码不变

## L2：五语言实际QM可用且对话框重翻译

类型：动态/资源集成；代码：`FeatureTests::testKeepZoomPreferenceAndColorTranslations`。

- 测试目的：检查运行时真实翻译资源而非仅TS文字
- 前置条件：已编译四份QM
- 输入数据：zh_Hans、zh_Hant、es、ja、英语回退
- 操作步骤：加载安装各翻译器，处理LanguageChange，检查设置及信息标签，移除翻译器；显示信息区并检查颜色值标签的 heightForWidth，保存各语言截图；等待设置窗口终态，并检查新复选框自然宽度及视口包含关系
- 预期结果：标签与当前QM一致，切回英语无残留；颜色值列高度足够显示全部换行文字
- 后置条件：所有测试翻译器移除

## Z4：窗口隔离、会话恢复与关闭选项保持旧行为

类型：动态/HiDPI；代码：`GraphicsViewTests::testKeepZoomWindowIsolationAndSession`。

- 测试目的：检查比例不跨窗口传播且会话初次恢复有优先级
- 前置条件：两个窗口，保持开启，临时PNG；另以QT_SCALE_FACTOR=2运行
- 输入数据：窗口1=120%，窗口2=70%；会话保存120%；关闭保持后切图
- 操作步骤：两窗口独立切图；窗口2清图并恢复窗口1会话；窗口2关闭保持再切图
- 预期结果：各自比例保持；恢复120%而非70%；关闭后回默认计算模式；窗口1不受影响
- 后置条件：两窗口关闭、选项与退出策略恢复

---

## 既有测试说明（保留）

# HDR 局部偏色测试用例说明

日期：2026-10-08。对应问题：HDR 图片“一部分偏色”。技术设计中定义 CR-01～CR-06。以下每个用例均含六要素，并映射到测试代码。已有亮度测试继续作为回归；修正其错误假设，不删除新颜色测试或提高容差来换取通过。

## 原测试为什么漏检

1. 单像素只给定峰值明显超过 1.1 的 HDR 高光，使旧空间掩码恒为 1；缺少真实 HDR 中间调和近白点颜色。
2. 把“非高光无论原始 HDR 颜色是什么，都必须保持 SDR”写成预期结果，等于把错误实现当作 oracle。
3. 原生测试只看 firstFramePresented、transitionProgress、persistentHDRSurfaceReady 等状态，未读取显示缓存 RGB。
4. 仅以一个 JPEG 验证呈现状态，未对不同 RAW / 增益图格式验证最终颜色。

旧数值测试现在明确输入“已经适配好的 HDR 端点”，检查线性混合；只有 SDR/HDR 本来相同的非高光区域才要求不变。新测试参考来自端点恒等性质、独立解码图像和实际缓存像素，不来自被测公式。

## CR-01-S 静态用例

1. 测试目的：核查最终颜色端点完整性的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR01_endpoint_identity`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-01-D 动态用例

1. 测试目的：验证最终颜色端点完整性，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：HDR 值低于白点、近白点、饱和高光三种区域；进度 1；headroom=8。
4. 操作步骤：执行 `testHDRBrightnessPreservesEndpointColors(colored-midtone-endpoint / near-white-endpoint / saturated-highlight)`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：所有 RGB 通道与输入 HDR 端点误差 <.003；不能残留 SDR 颜色。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-02-S 静态用例

1. 测试目的：核查线性时间插值与相同区域稳定的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `BrightnessSourceContracts.test_BR05_highlights`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-02-D 动态用例

1. 测试目的：验证线性时间插值与相同区域稳定，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：中间调进度 .5；原有五个进度点；相同暗部/中间调端点；alpha=.5。
4. 操作步骤：执行 `testHDRBrightnessPreservesEndpointColors(colored-midtone-midpoint)、testHDRBrightnessLinearLightPixels`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：输出等于端点线性插值；相同区域不变；透明度正确。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-03-S 静态用例

1. 测试目的：核查平台显示适配与共用端点的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR02_platform_adaptation`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-03-D 动态用例

1. 测试目的：验证平台显示适配与共用端点，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：source/target headroom API；当前余量 1、3；原生 3→1→3。
4. 操作步骤：执行 `testHDRBrightnessNativePresentation、testSDRDisplayForcesUnitHeadroom、testDisplayHeadroomBootstrapsFromPotentialCapability`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：纯 SDR 正确回退；当前余量优先；原生缓存随余量重建；源码确认三条生产路径使用同一端点适配。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-04-S 静态用例

1. 测试目的：核查真实图片的区域颜色一致性的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR03_real_image_oracle`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-04-D 动态用例

1. 测试目的：验证真实图片的区域颜色一致性，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：1.JPG、2.DNG、3.dng、4.nef；足够 headroom；512×512 全图 float RGB 网格。
4. 操作步骤：执行 `HDRSampleTests::testHDRColorFidelity（四个数据行）`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：相对独立原生 HDR 参考，最大 RGB 通道误差≤.01；无超过阈值的像素。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-05-S 静态用例

1. 测试目的：核查实际最终缓存颜色一致性的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR04_materialized_cache`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-05-D 动态用例

1. 测试目的：验证实际最终缓存颜色一致性，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：真实 JPEG；320×240 原生视口；足够 headroom；实际缓存与独立参考先全分辨率物化再各采样 512×512。
4. 操作步骤：执行 `testHDRBrightnessNativeColorFidelity`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：实际缓存 CGImage 与独立参考最大通道误差≤.01；数组长度 16384、所有值有限。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-06-S 静态用例

1. 测试目的：核查亮度与既有交互回归的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `BrightnessSourceContracts.test_BR01_timing 至 test_BR08_cache（共八项）`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-06-D 动态用例

1. 测试目的：验证亮度与既有交互回归，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：普通/连续/减弱动态效果时序、SDR/HDR、缩放和焦点；完整八套 Qt 测试。
4. 操作步骤：执行 `HDRPolicyTests 其余函数、FovelleTests、FovelleHDRFocusTransitionEvidence`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：原有时序与交互行为保持通过；已有就绪检查作为生命周期补充，不能替代颜色测试。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## 数据、门禁与执行方式

本地样本目录：`/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test`，文件依次为 `1.JPG`（增益图 JPEG）、`2.DNG`（处理预览/增益图）、`3.dng`（普通 DNG）、`4.nef`。采样点覆盖整个图像，不只统计平均亮度或峰值。RGB 通道绝对误差阈值 .01，单像素数学阈值 .003；这不是 ΔE 阈值。

无外部样本也可运行 `FovelleHDRColorPixels` 与静态检查。`FovelleHDRColorSamples` / `FovelleHDRColorNative` 仅在配置时发现四个本地样本后注册，可通过 CMake `FOVELLE_HDR_COLOR_FIXTURE_DIR` 指定其他目录。显式运行样本 suite 时缺文件会失败，不会伪装为通过；完整 policy suite 缺少原生 JPEG 时会明确跳过对应原生用例。本次执行均提供了样本。

```bash
python3 tests/hdr_brightness_static.py
ctest --test-dir build -R 'FovelleHDRColor|FovelleHDRBrightness' --output-on-failure
FOVELLE_HDR_JPEG_SAMPLE='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG' \
ctest --test-dir build \
  -R 'FovelleTests$|FovelleHDRBrightness|FovelleHDRColor|FovelleHDRFocusTransitionEvidence' \
  --output-on-failure
```

## 缓存 oracle 的校正

第一次缓存测试将“直接缩小的延迟 CI 图”与“全分辨率计算后再缩小的实际 CGImage”比较，产生 .130859 的差异。校正为参考与实际均在全分辨率计算后再采样，独立参考不经过被测渐变/掩码，容差仍为 .01。校正后最大误差为 0。保留两个实验日志，避免将测试口径问题误报为生产缓存缺陷。

修复前的新单像素测试和四类真实图片测试的失败结果，以及修复后结果，详见测试完成报告。新增实际缓存用例是端到端颜色防回归补充，不宣称最初的采样口径失败就是已确认生产缺陷。

## 2026-10-08 Actions 回归测试

| 用例 | 目的 | 前置条件 | 输入 | 步骤 | 预期结果 | 后置条件 |
| --- | --- | --- | --- | --- | --- | --- |
| OpenWith normal-startup | A2：真实关闭任务安全完成 | Cocoa、可加载 PNG | 准备延迟 0 ms | 创建显示窗口、加载 PNG、计时、请求 Open With、关闭及析构 | 无崩溃，关闭耗时 <5000 ms | 窗口与临时目录释放，退出策略恢复 |
| OpenWith slow-startup | A1/A2：排除准备时间对关闭断言的污染 | 同上 | 准备延迟 5100 ms | 就绪后等待输入延迟，再计时及关闭 | 旧计时范围失败；修正后关闭 <5000 ms | 同上 |
| 作用域清理审查 | A3：断言提前返回时设置仍恢复 | 读取测试源码 | qScopeGuard 与局部对象生命周期 | 静态审查 guard 位于首个断言之前、捕获原值且晚于窗口析构释放 | 所有返回路径恢复原退出策略 | 无状态修改 |
| 完整 CI | A4：修正兼容完整工程 | 编译成功、main 可推送 | 当前提交 | 完整 CTest；推送并读取两个工作流结果 | Build Fovelle 和 Checks 均成功 | 工作区干净 |

动态用例固化于 `FeatureTests::testOpenWithWorkerTeardownContract_data/testOpenWithWorkerTeardownContract`，由现有 FovelleTests 自动执行。准备延迟专门覆盖旧五秒边界，不放宽关闭门槛。静态审查和 git diff --check 为补充检查。


## 2026-10-10 CI Open With 回归用例修正

代码：FeatureTests::testOpenWithWorkerTeardownContract_data/testOpenWithWorkerTeardownContract。

- 测试目的：区分原生枚举耗时与窗口收尾开销，并确认析构完成前任务已经结束。
- 前置条件：Cocoa QPA、临时目录可写、QApplication存活；作用域保存退出策略。
- 输入数据：真实PNG与原生provider，准备延迟0/5100ms；另有不依赖系统冷热状态的5200ms可控provider。
- 操作步骤：创建并加载窗口；准备完成后开始计时；分派Open With、close、析构；provider内部独立计时并设置原子完成标志；读取各阶段与总时长。
- 预期结果：任务完成标志为真，provider区间包含于总区间；dispatch、close及增量收尾开销各低于5000ms；完整工作仍受QtTest看门狗约束。5200ms平台工作本身不应被误报为生命周期缺陷。
- 后置条件：窗口、返回列表、线程池与临时图像释放，quitOnLastWindowClosed原值恢复；原生默认provider与其它功能不变。

先在旧总耗时断言下运行可控5200ms数据行，观察5210ms失败，再修正阶段断言。同一个三行测试进入现有FeatureTests/CTest，无额外CI豁免或跳过条件。

</details>
