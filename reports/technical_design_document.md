# GitHub Actions 绘制性能门禁修复设计

## 问题界定与原子拆解

基线：`a2a63006c37ccbba575a09d8302e3ef91cf790fb`。本次只处理 GitHub Actions 最新失败，不扩展先前功能需求。

1. [失败运行 39138261320](https://github.com/inostarlin-passion/Fovelle/actions/runs/39138261320) 编译成功，62 个 CTest 项仅 FovelleTests 失败。唯一 QtTest 失败为 EPS 2x 首帧性能判断：cold p95=65.580416 ms，reference p95=11.350084 ms，预算=45.400336 ms。
2. 同次独立 FovelleVectorPerformanceDynamic 通过；[同提交 Checks](https://github.com/inostarlin-passion/Fovelle/actions/runs/39138261190) 也通过。这排除了必现编译错误，但不能证明实际绘制从未变慢。
3. 测试注释称 CPU paint time，实际以 QElapsedTimer 计时。确认缺陷是计量指标与契约不符；历史日志没有线程 CPU 数据，不能断言该次 65 ms 全部由调度导致。
4. 分别检验计时正确性、暂停干扰隔离、真实 CPU 超预算检出、四种格式/密度组合以及原画质/异步约束。

## 多跳检索与交叉核验

检索顺序为失败日志→测试实现→Qt 时钟语义→POSIX 线程 CPU 定义→Apple 平台实现→GitHub 运行环境。2026-10-18 检索后已足以确认指标缺陷，不再扩大无关搜索。

|来源|核验事实及边界|
|---|---|
|[Qt QElapsedTimer](https://doc.qt.io/qt-6/qelapsedtimer.html)|Qt 6.6 起使用 steady_clock；测量单调经过时间，不是线程执行时间。|
|[POSIX CPU-time clocks](https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html)|CPU-time clock 测量执行时间；线程 CPU 时钟归属于调用线程。|
|[Apple Libc clock_gettime 实现](https://github.com/apple-oss-distributions/Libc/blob/main/gen/clock_gettime.c)|CLOCK_THREAD_CPUTIME_ID 使用 __thread_selfusage，并转换为纳秒；与 POSIX 定义交叉核验。|
|[GitHub hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)|托管 runner 是虚拟环境；不能从墙钟尖峰直接推定 App 的 CPU 退化。环境信息不是本次尖峰原因的直接证明。|

## 方案与正确性论证

修复对象是实际执行 CI 性能门禁的测量代码，无证据要求修改 App 生产绘制路径。

- 同一个测量函数包围 item.paint 或参考 drawImage，返回当前线程 CPU 毫秒与墙钟毫秒。复用仓库已有 currentThreadCpuTimeNanoseconds；时钟不可用、逆行或空样本必须明确失败，不能跳过或用零伪造成功。
- cold、warm、reference 的预算统计全部使用线程 CPU 时间，仍采用原来的 `max(16.67, 4 × reference p95)`、原采样数量、原 4K 像素、原 cold p95 和整体 p95 判断。墙钟 cold/warm/reference/p95/max 同时输出供诊断，不宣称为物理显示 FPS。
- deadline 继续用墙钟；不会因 CPU 时钟排除睡眠而无限等待异步工作。GUI 转色次数、worker 转色、ICC 查询稳定性及显示格式断言不变。
- 自动化反证：在被测函数内部注入 40 ms 睡眠，旧墙钟门禁必然超出 16.67 ms，正确 CPU 指标不应将暂停计为绘制工作；注入至少 30 ms 线程 CPU 忙循环，正确门禁必须拒绝。这验证排除暂停不等于放弃 CPU 性能约束。

设测量范围内线程实际运行耗时为 C，暂停为 S，则墙钟 W 包含 C 与 S。线程 CPU 差值计 C，不随纯暂停 S 增长；因此 CPU 门禁可检验自身绘制开销。对参考绘制使用同一时钟，避免分子与分母量纲不一致。忙循环反例保证 C 超预算仍可被检出；四行真实渲染测试保证函数接入了生产 paint 路径。此论证不覆盖同步阻塞、GPU 或扫描输出延迟，墙钟数据仍保留，原有真实窗口 120Hz CPU 和画质测试继续执行。

## 逆向证伪与验证范围

不提高阈值、不重试取最佳值、不删数据行、不禁用门禁、不更换 runner。静态检查防止 CPU/墙钟混用；动态睡眠与忙循环反证指标；真实 SVG/EPS 1x/2x 验证路径；全量 CTest 验证集成。远端成功必须由新提交后的 GitHub 实际运行确认，本地测试不能替代远端结论。
