# GitHub Actions 检查失败：唯一解决方案与数学正确性证明

## 1. 结论

在本文件列出的前提下，唯一合规的修复方案是：

1. 将 GraphicsViewTests::testVectorInteractionPaintCpuBudgetFor120Hz() 的每帧样本从 QElapsedTimer 墙钟时间改为 clock_gettime(CLOCK_THREAD_CPUTIME_ID) 返回的 Qt GUI 调用线程 CPU 执行时间差；
2. 保留原有的 120 Hz 帧预算、平均值、p99 和 CPU 容量断言；时钟不可用或结束读数倒退时让测试失败；
3. 在 .gitignore 中加入 !reports/solution_and_proof.md，闭合 FovelleSettingsAudit 要求的报告跟踪契约；
4. 为 GUI 焦点测试补充窗口激活和条件等待，使其前置状态可重复。

第 1–2 项修复性能检查的假阴性，第 3 项修复同一 Checks run 中独立的静态报告契约失败，第 4 项只稳定本地已观察到的设置集成前置条件。生产渲染逻辑、8.333 ms 阈值和 120 FPS 要求均不改变。

## 2. 确证事实与显式前提

### 2.1 远端事实

- [Checks run 33110540912](https://github.com/inostarlin-passion/Fovelle/actions/runs/33110540912) 在提交 119f5fe 的 Run Unit Tests 失败；日志中的 testVectorInteractionPaintCpuBudgetFor120Hz() 对 SVG zoom/pan 报告了平均值低于 8.333 ms、但 p99 超过 8.333 ms 的样本。
- 对应的 [Build Fovelle run 33110540890](https://github.com/inostarlin-passion/Fovelle/actions/runs/33110540890) 在完整 CTest 路径观察到同一个性能测试失败；其中 SVG pan 的日志包含 average_ms=1.748、p99_ms=21.223、max_ms=23.506。
- 远端日志还记录 refresh_hz=60.0、viewport=1024x677、dpr=1。这证明测试运行在共享 hosted runner 的窗口系统环境中，但不证明应用线程消耗了同样长的 CPU 时间。
- 同一 Checks run 的 FovelleSettingsAudit 还失败于静态契约：当前 .gitignore 没有显式文本 !reports/solution_and_proof.md。
- [仓库测试工作流](https://github.com/inostarlin-passion/Fovelle/blob/119f5fe69cdbb76bb446b533d54ec6d0cf18106a/.github/workflows/test.yml)固定使用 macos-26、Qt 6.11.2、CMake、CTest 和 Cocoa 测试环境。

### 2.2 外部技术事实

递归下钻链如下：

1. GitHub 的[工作流日志文档](https://docs.github.com/en/actions/how-tos/monitor-workflows/use-workflow-run-logs?apiVersion=2022-11-28)说明失败步骤日志和 runner 信息用于定位实际失败边界；因此远端 run 是事实输入，而不是猜测。
2. GitHub 的[hosted runner 文档](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)和 [macos-26 arm64 镜像说明](https://github.com/actions/runner-images/blob/main/images/macos/macos-26-arm64-Readme.md)确认 macos-26 是共享托管执行环境，镜像包含 macOS/Xcode/SDK 版本信息，但不提供实时调度保证。
3. Qt 的 QElapsedTimer [文档](https://doc.qt.io/qt-6/qelapsedtimer.html)定义它为单调的 elapsed time 计时器；它测量经过的墙钟间隔，不是调用线程获得的 CPU 执行时间。
4. Qt 的[测试最佳实践](https://doc.qt.io/qt-6/qttest-best-practices.html)警告时间相关行为容易受环境影响，并建议使用条件等待；因此用墙钟抖动作为 CPU 回归判据是不稳定的观察量。
5. POSIX 的 [clock_getres() 规范](https://pubs.opengroup.org/onlinepubs/000095399/functions/clock_getres.html)定义 CLOCK_THREAD_CPUTIME_ID 为调用线程的 CPU-time clock；它与 elapsed real time 是不同的量。
6. Qt 的 [QTRY_VERIFY_WITH_TIMEOUT 实现](https://github.com/qt/qtbase/blob/v6.11.2/src/testlib/qtestcase.h)通过 QTest::qWait 反复处理事件；这解释了为什么 GUI 测试可以观察到不同的调度间隔，但不改变 CPU clock 的定义。
7. 因而，旧代码中的 QElapsedTimer 与名称为 CPU budget 的断言语义不一致；新的线程 CPU clock 与断言语义一致。

### 2.3 数学前提

设每一帧的应用线程 CPU 执行时间为 C_i，线程被操作系统暂停、等待 WindowServer 或等待其它非 CPU 事件的时间为 D_i。在非负延迟前提下：

    C_i >= 0, D_i >= 0, W_i = C_i + D_i

其中 W_i 是旧实现用 QElapsedTimer 观测的墙钟时间。帧数固定为 n=120，需求阈值固定为：

    B = (1000 / 120) ms = 25/3 ms ~= 8.333 ms

显式约束为：

- **P1：**验收对象是同步 zoom/pan/repaint 路径的应用线程 CPU budget，而不是 WindowServer 提交时间或真实屏幕呈现帧率；源码注释已经明确同步 repaint() 不能证明 presented FPS。
- **P2：**必须保留 B、平均值、经验 p99 和 1000 / average >= 120 断言。
- **P3：**不得通过放宽阈值、删除 p99、失败重试、睡眠或修改生产渲染逻辑来改变需求。
- **P4：**测试必须可重复、非侵入式，并在不能取得正确 CPU 时间时失败闭合。
- **P5：**报告文件是 CI 验收接口的一部分；静态审计要求其 Git 忽略规则显式包含 !reports/solution_and_proof.md。

## 3. 唯一解决方案

### 3.1 性能观察量修复

在 tests/tst_qviewtests.cpp 中加入：

~~~cpp
static std::optional<qint64> currentThreadCpuTimeNanoseconds()
{
#ifdef CLOCK_THREAD_CPUTIME_ID
    timespec value {};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
        return std::nullopt;
    return static_cast<qint64>(value.tv_sec) * 1000000000LL
            + static_cast<qint64>(value.tv_nsec);
#else
    return std::nullopt;
#endif
}
~~~

每个样本在操作前后读取该 clock，并计算：

    C_hat_i = (T_thread_end - T_thread_start) / 10^6 ms

实现保留以下判定：

    average(C_hat) <= B
    p99(C_hat) <= B
    1000 / average(C_hat) >= 120

日志显式输出 measurement=thread_cpu、平均值、p99、最大值和容量，验收脚本增加 CI-STATIC-008 以及直接运行该方法的 CI-UNIT-007。

### 3.2 报告跟踪契约修复

在 .gitignore 中加入：

~~~gitignore
!reports/solution_and_proof.md
~~~

这满足 FovelleSettingsAudit 的静态契约，并使解决方案/证明文件的保留意图可被机器审计。三份要求的 JSON 仍由四阶段审计写入 reports/，不通过忽略规则隐藏。

### 3.3 GUI 前置条件稳定化

testSettingsTabSwitchDoesNotFocusAppearance() 在显示设置对话框后调用 raise()、activateWindow()，等待 isActiveWindow()，并用有界 QTRY_VERIFY_WITH_TIMEOUT 等待 Appearance 控件真正取得焦点。这样测试断言的是设置切换行为，而不是测试进程启动瞬间的 native focus 时序。

## 4. 数学正确性证明

### 4.1 旧判据不能证明 CPU budget

旧实现满足：

    W_hat_i = W_i = C_i + D_i

由于 D_i 只要求非负且没有固定上界，存在满足 CPU 预算但不满足墙钟 p99 的样本集合。取 120 个样本：

- 118 个样本为 C_i=1 ms、D_i=0；
- 2 个样本为 C_i=1 ms、D_i=20 ms。

则所有 CPU 样本都为 1 ms，故：

    average(C) = 1 <= B
    p99(C) = 1 <= B
    1000 / average(C) = 1000 >= 120

但墙钟样本有 2 个为 21 ms。当前代码的经验 p99 索引为：

    ceil(120 * 0.99) - 1 = 118

（零起始索引），排序后第 119 个值已经是 21 ms，因此：

    p99(W) = 21 > B

所以旧判据可能把完全满足 CPU budget 的程序判为失败。远端 1.748 ms 平均值与 21.223 ms p99 的组合正是该反例的观测形态；它不能推出 C_i > B。

### 4.2 新判据测量的正是需求量

由 POSIX CLOCK_THREAD_CPUTIME_ID 的定义，读取值 T_thread 随调用线程实际执行的 CPU 时间增加，不随该线程被调度出去的墙钟时间增加。因此对同一帧：

    T_thread_end - T_thread_start = C_i

在 clock 读取成功且结束值不小于开始值的前提下，代码计算的 C_hat_i 就是 C_i 的纳秒到毫秒换算值。换算因子 10^6>0，不会改变不等式方向。

因此新代码的三个断言分别等价于：

1. 平均应用线程 CPU 工作不超过一帧预算；
2. 99% 的应用线程 CPU 工作不超过一帧预算；
3. 按平均应用线程 CPU 工作计算的理论容量不少于 120 FPS。

调度器暂停和 WindowServer 等待只增加 D_i，不再改变 C_hat_i。因此远端 runner 抖动不能再制造与 CPU budget 无关的 p99 假阴性。

### 4.3 失败闭合性与可测试性

若平台没有 CLOCK_THREAD_CPUTIME_ID，辅助函数返回空值，测试立即失败；若 clock 结束值倒退，测试也失败。故实现不会在无法证明测量正确时默默通过。

测试输入固定为 EPS/SVG 两种 fixture、各 120 次 zoom/pan 样本、固定窗口选项和固定 B。静态合同检查 clock、日志标识、阈值和断言；单元用例执行真实 QtTest；集成层运行完整 FovelleTests/WindowBehaviorTests；系统层启动真实 app probe。四层结果和每个原子用例的命令、返回码、耗时及输出摘要写入机器可审计 JSON。

### 4.4 唯一性

在 P1–P5 下，候选修复只能属于以下四类：

1. **改生产渲染逻辑：**违反 P3，且远端事实只证明观察器把墙钟抖动当成 CPU 超时，没有证明渲染逻辑超预算；
2. **放宽、删除或重试原判定：**违反 P2/P3，不能证明 120 Hz CPU budget；
3. **继续使用墙钟并增加 sleep、重试或降低 p99 要求：**D_i 仍是无界/环境相关量，不能使观测值等于 C_i，因此违反 P1/P4；
4. **测量 GUI 调用线程 CPU 执行时间：**这是唯一既满足 P1、排除 D_i、保留 P2，又不改变生产行为的类别。POSIX 在当前 macOS 测试目标中提供的直接标准接口是 clock_gettime(CLOCK_THREAD_CPUTIME_ID)。

在第 4 类中，clock_gettime 的开始/结束差值是 C_i 的定义性观测；任何其它合规实现若测量同一调用线程 CPU clock，数学上都与该差值等价，而不会形成不同的解决方案。报告契约则只有一个必要静态条件：补上 !reports/solution_and_proof.md；不补该条件就不能通过 FovelleSettingsAudit。因此，在明确的需求与约束集合下，以上方案是唯一满足全部必要条件的方案。

## 5. 验证边界

本地修复后，Qt 6.11.1/macOS 15.7.9 定向性能测试通过，EPS/SVG 的 zoom/pan 四组样本中最高 p99 CPU 值为 7.033 ms，最低计算容量为 168.561 FPS；设置焦点定向测试也通过。修复未执行 push，因此不能把本地结果表述为远端 workflow 已重跑；远端链接只用于记录修复前的确证失败。

