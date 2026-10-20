# 启动与退出性能修复测试完成报告

日期：2026-10-20（Asia/Shanghai）。结论：本次 A1—A7 原子验收全部通过，Release 构建完成，默认快速回归通过。修复了启动菜单延后和 Open With 无期限窗口等待；未将原生渲染队列等未经量化的独立路径宣称为已解决。

## 变更与执行顺序

1. 根据根因报告及 Qt/Apple 官方资料完成[设计与正确性论证](technical_design_document.md)，再编写[用例规格](test_case_specification.md)。旧版三份报告归档在 [baseline-reports](evidence/lifecycle-fix/baseline-reports/)。
2. 先加入静态及动态测试，未改生产代码时验证失败：[静态红灯](evidence/lifecycle-fix/red-static.log)；[启动红灯](evidence/lifecycle-fix/red-window.log)显示返回时菜单动作数为 0；[后台任务红灯](evidence/lifecycle-fix/red-worker.log)显示阻塞提供者使窗口收尾耗时 1014 ms，超过 250 ms 验收阈值。信号量的 1 秒兜底释放让旧实现可结束而不挂死测试。
3. 生产实现将窗口菜单放在构造末尾、原生 show 前；Open With 改为 QvAsync 值捕获工作、取消票据及 GUI 线程弱引用交付，移除窗口成员池与 watcher 的无期限等待，保留应用析构的设置同步及 1500 ms 排空边界。相应更新历史静态审计中的 Open With 合同。
4. 修正新增测试的窗口清理（close 后再 delete），加强旧文件查询结果抑制测试，重编译后串行执行全部默认 CTest。最后在无其他测试进程并发的条件下运行独立性能对照。

## 环境、命令与证据

环境：macOS 27.0.1、arm64、Qt 6.11.2 Release、Apple LLVM 17；Cocoa 原生窗口，测试隔离 INI 设置，关闭自动更新。基线是变更前现有构建的完整 app 副本；其主程序 SHA-256 与根因报告一致：`98925604485b687fde3dee60d88eb3ee67b012eacc692f32b5a3b93fbe97c479`。候选哈希与全部样本保存在 [benchmark.json](evidence/lifecycle-fix/benchmark/benchmark.json)。

```bash
cmake --build build -j 6
ctest --test-dir build --output-on-failure
python3 tests/lifecycle_fix_benchmark.py \
  /tmp/Fovelle-lifecycle-baseline.app/Contents/MacOS/Fovelle \
  build/Fovelle.app/Contents/MacOS/Fovelle \
  --output reports/evidence/lifecycle-fix/benchmark
git diff --check
```

构建成功，保留已有 Qt deprecated API 编译警告。参见[最终构建日志](evidence/lifecycle-fix/build-final.log)。最终 CTest **5/5 通过**，总计 **45.24 秒**；[汇总](evidence/lifecycle-fix/ctest-final.log)、[含全部测试输出的日志](evidence/lifecycle-fix/ctest-details.log)。默认快速回归 **40 个阶段全部通过**、30.114 秒，含静态合同、GUI 行为、图像加载、动画、便携 HDR 与色彩回归，见 [fast-summary.json](evidence/lifecycle-fix/fast-summary.json)。新增 Python 及修改的审计脚本语法解析通过；`git diff --check` 通过。

## 原子验收结果

| 标准 | 用例/固化代码 | 最终证据与结果 |
|---|---|---|
| A1 菜单在事件分发前可用 | S1 `lifecycle_fix_static.py`；D1 `lifecycle_fix_acceptance.inc::testStartupMenusReadyBeforeEvents` | 通过；应用工厂返回时已有主菜单/虚拟动作，重新显示不重复构建 |
| A2 菜单状态与隔离 | D2 `menu_lifecycle_acceptance.inc` 的三个测试函数 | 通过；标题栏×构造模式 4 行、全屏、动画暂停；覆盖主菜单/快捷键/上下文菜单及多窗口隔离 |
| A3 查询不阻塞窗口、停止幂等、取消后无交付 | S2；D3 `tst_qviewtests.cpp::testOpenWithWorkerTeardownContract` | 3 行通过；确保 provider 已在运行且阻塞，窗口关闭、两次 shutdown 和析构分别为 21/22/15 ms，均 <250 ms；销毁时 provider 尚未结束；释放后排空并处理迟到事件，无崩溃 |
| A4 单飞、旧结果抑制、最新交付 | D4 `lifecycle_fix_acceptance.inc::testOpenWithLatestRequest` | 通过；A 阻塞期间重复请求只调用一次；A 返回、B 仍阻塞时菜单不显示 A；B 释放后菜单显示 B，总调用两次 |
| A5 实际查询参与进程有限收尾 | D5 `testOpenWithShutdownProcess` 及测试主函数专用子进程入口 | 通过；30 ms 查询：322 ms 完整进程结束、码 0；10000 ms 查询：1830 ms 完整进程结束、码 75；均 <4000 ms；验证 saved.ini 落盘 |
| A6 保留 Qt 生命周期及持久化边界 | S2；D6 `remediation_tests.h::testLoaderNonblockingShutdown/testShutdownProcess` | 通过；loader 快速销毁、取消后无信号；现有短/长任务的 0/75 退出码及持久化合同通过 |
| A7 启动降低、空退出无显著退化 | P1 `lifecycle_fix_benchmark.py` | 通过；每版预热 1 次，交替各测 10 次，22 次进程运行全部退出 0；所有就绪标记齐全且退出探针晚于就绪 |

动态专项共 14 个测试数据行，全部通过、无跳过（Qt 输出的 20 个 passed 另包含三个套件各自的 init/cleanup）。S1/S2 为结构验收，不能代替动态时序验收。

## 性能结果与解释

| 父进程单调时钟指标 | 基线中位数 | 候选中位数 | 变化 |
|---|---:|---:|---:|
| spawn → 首轮事件且菜单就绪 | 348.51 ms | 290.68 ms | −57.83 ms，降低 16.59% |
| 退出请求探针 → 完整进程结束 | 31.76 ms | 32.79 ms | +1.04 ms；满足增幅 ≤20 ms |

[独立测量汇总](evidence/lifecycle-fix/benchmark-summary.log)，[全部原始启动/退出日志与样本](evidence/lifecycle-fix/benchmark/benchmark.json)。没有把先前与桌面测试并发的探索样本用于最终结论；该批次保存在 `benchmark-concurrent-desktop/`。

启动改善与提前菜单的原有干预实验方向一致，消除了两轮菜单排队；不意味着所有构造工作减少。空窗口退出原本较快，本轮没有声称获得退出加速。慢查询场景的改善由“运行中查询不再阻塞窗口”和“进入受管理应用排空期限”的定向测试验证。**码 75 是超时兜底，不能视为正常成功退出**；1.83 秒包含子进程初始化、加载测试图片和完整退出，不只是排空耗时。

## 失败、重测与既有检查限制

- 首轮新增 D1 清理只 delete 未 close，留下应用窗口登记，随后用例崩溃；已修正为禁用 DeleteOnClose、close、delete。探索测试还与另一组 GUI/全屏测试并发，产生全屏退出超时及后继崩溃。保留 `ctest.log` 和 `ctest-concurrent-desktop.log`，最终采用修正后串行完整重测结果，没有忽略失败用例。
- 额外执行了历史 `quality_static.py` / `quality_integration.py`，它们不是当前默认快速门禁，整体未通过。静态失败 ST-01/06/09/11/16 涉及全仓格式、旧图像接口字符串、已改名测试、旧 viewport/theme 合同；对 HEAD 归档执行原始静态审计，确认同类失败在变更前已存在。见[本轮静态审计](evidence/lifecycle-fix/quality-static.json)、[HEAD 静态审计](evidence/lifecycle-fix/quality-static-baseline.json)。归档不含 `.git`，其额外 ST-03 不能用作源码基线结论。
- 历史集成审计 I-03/09/10/14 同样引用上述旧合同；I-06 的 CTest 子执行发生在探索阶段，后续最终串行 CTest 已通过。历史审计原始输出保留在 [quality-integration.json](evidence/lifecycle-fix/quality-integration.json)，没有将其报告为通过，也没有为本任务重写无关产品合同。

测量仅代表热缓存、无图片、单个最大化窗口、直接创建进程及隔离设置；不覆盖 Finder 发起到进程创建、系统冷缓存、光学首帧、真实用户偏好、复杂恢复会话和所有图片负载。原生渲染队列、全局遗留线程池、用户确认框和底层存储阻塞仍是根因报告列出的独立边界，本轮未测出其异常，也未通过移除其安全等待改变行为。
