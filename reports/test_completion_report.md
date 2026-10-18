# GitHub Actions 修复测试完成报告

## 已确认的问题与修改

基线 a2a6300 的 [Build Fovelle](https://github.com/inostarlin-passion/Fovelle/actions/runs/39138261320) 唯一失败是 EPS 4K 首绘性能断言：墙钟 p95=65.580416 ms，预算45.400336 ms。同提交 Checks 和同次独立性能 CTest 通过。确认性能测试声明的 CPU 指标实际以墙钟测量；原日志不能分离实际执行与调度/等待耗时。

修复 `tests/vector_performance_acceptance.inc` 的执行计量：paint 与参考 drawImage 统一采用线程 CPU 时钟，同时保存墙钟指标。增加受控暂停/CPU 忙循环反证、静态契约与 CTest 注册。阈值、测试像素、首帧判断、线程/颜色约束继续保留。问题属于 CI 测量代码，App 生产绘制路径没有需要修改的已证实缺陷。

## 环境与可复查证据

本地 macOS 27.0.1 arm64、Qt 6.11.2、Apple Clang17；远端 macOS26.6.2、Qt6.11.2、Apple Clang16。硬件与系统版本不同，不能以本地成功宣称远端已绿。

可提交的[精简证据与源码哈希](evidence/actions-fix/vector-timing-20261018.json)保存远端链接、失败数据和本地测量。完整原始日志在本工作区 `reports/evidence/actions_vector_timing_20261018/`（沿用仓库忽略规则，未作为发布文件）。此前三份报告备份在该目录的 previous_*.md。

## 红灯与专项绿灯

|验收标准|执行结果|
|---|---|
|AC1 / S1|新增静态契约修复前5项中1项失败，修复后5/5通过。|
|AC2 / D1|旧墙钟实现睡眠“CPU”p95=41.03 ms，明确失败；修复后CPU p95=0.019833 ms、墙钟p95=41.0309 ms，正确区分暂停。|
|AC3 / D2|忙循环CPU p95=30.0007 ms，高于原16.67 ms预算，超预算识别断言通过。|
|AC4 / D3|四组真实生产绘制数据均通过，见下表；GUI转色0、worker转色12。|
|AC5 / S2+D4|专项CTest 2/2通过，QtTest 10 passed、0 failed、0 skipped；全量CTest 65/65通过，0失败，用时574.80秒；此前失败的FovelleTests通过（238.72秒），独立性能门禁通过（4.40秒）。|

|格式/密度|首绘CPU p95 ms|整体CPU p95 ms|首绘墙钟p95 ms|CPU预算ms|
|---|---:|---:|---:|---:|
|SVG 1x|1.684291|1.604292|1.685250|16.67|
|EPS 1x|1.442625|1.420459|1.444709|16.67|
|SVG 2x|6.034958|6.021667|6.037167|16.67|
|EPS 2x|5.565083|5.542458|5.570000|16.67|

执行命令：`cmake --build build --parallel 2`；`python3 tests/vector_performance_static.py`；`QT_QPA_PLATFORM=cocoa ctest --test-dir build -R 'FovelleVectorPerformance(Static|Dynamic)' --output-on-failure -V`；全量使用 `QT_QPA_PLATFORM=cocoa ctest --test-dir build --output-on-failure --timeout 90`。

`git diff --check`、修改的 inc 文件 clang-format dry-run、Python编译检查均通过。未提高预算、禁用测试或重试取最佳结果。性能数据是CPU绘制开销，不是物理显示FPS；阻塞、GPU/显示延迟需结合保留的墙钟数据评估。修复尚未推送，远端新提交验证待 GitHub Actions 实际执行。

全量结果包括 Portable HDR 必需执行、发布包验证、颜色/画质/终端密度、原生全屏、动画与异步生命周期；执行一次全量验证即通过，无失败后重跑。原始 QtTest 输出另存 ctest-qt-output.log，其 SHA-256 与全量性能数据已写入精简证据。
