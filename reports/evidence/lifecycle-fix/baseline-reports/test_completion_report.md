# 测试完成报告（2026-10-20；整体未达标）

## 结果

菜单缺陷及同类状态同步问题已经修复，静态和动态功能回归通过。启动和退出减少 50% 两项验收均失败，因此本次交付不能认定全部任务完成。

| 原子验收 | 结果 | 证据 |
| --- | --- | --- |
| M1a/M1b 主菜单及右键标题栏文案 | 通过 | 四种 hidden/deferred 组合；fast-menu-tests.xml |
| M1c 新窗口隐藏偏好 | 通过 | native-regressions.log |
| M2a 全屏文案 | 通过 | fast-menu-tests.xml；原有全屏图标回归 |
| M2b 暂停/继续文案与启用 | 通过 | APNG 真实加载与暂停/恢复 |
| M2c 幻灯片文案 | 通过 | 创建菜单前启动及后续停止 |
| M2d 置顶勾选 | 通过 | 创建菜单前置顶及反向切换 |
| M2e 填充勾选 | 通过 | 创建菜单前填充及反向切换 |
| M2f 固定排序策略 | 通过 | 已有排序配置回归及动作库静态检查 |
| M3a 逐项动作核查 | 通过 | action-audit.json 的 50 项；空窗口静态文案和禁用规则断言 |
| M3b 图标策略 | 通过 | 主/虚拟菜单无图标，右键动态图标保留 |
| M4 窗口隔离 | 通过 | 两窗口相反标题栏状态不互相覆盖 |
| P1 启动减少 50% | **失败** | performance-final/benchmark.json |
| P2 完整退出减少 50% | **失败** | performance-final/benchmark.json |
| R1a 生命周期安全约束 | 通过 | static-tests.log，取消/排空/设置同步边界保留 |
| R1b 默认及聚焦回归 | 通过 | ctest.log；native-regressions.log |

## 修改及因果依据

根因：菜单延迟创建时从全局动作模板复制默认状态，状态变化发生时尚不存在的副本无法接收更新。统一窗口状态投影在两种菜单创建完毕后执行，保留窗口隔离；状态切换后仍使用同一组投影。额外修复同类的全屏、暂停/继续、幻灯片、置顶及动态图标策略。

优化：生产窗口避免重复构建无窗口 fallback 菜单；Qt 6.9+ 在创建原生窗口前设置扩展内容区标志；关闭时避免无用样式/尺寸恢复事务。未缩短 1500 ms 异步安全边界，未使用强制成功退出跳过析构。

## 实际执行

- Release 构建通过，编译日志 `evidence/menu-lifecycle/build.log`。
- 修改前新测试复现文案/勾选/图标失败，保留 `red-tests.log`。该首次红测在全屏断言提前返回后继续执行发生 SIGSEGV；后续调整测试使退出全屏完成后再报告文案失败。该日志不能单独证明一个生产崩溃根因。
- 菜单聚焦检查 11 passed、0 failed、0 skipped，见 `menu-tests.log`。
- 最终默认 CTest：40 个计划阶段完成，29.84 秒，零失败；新增菜单检查已接入 fast manifest，包含三个真实 HDR 原生全屏连续性场景。见 `ctest.log`、`fast-summary.json`、`fast-menu-tests.xml`、`fast-hdr-tests.xml`。
- 样式优化后的额外最大化/原生全屏/标题栏呈现/固定排序/偏好继承回归：18 passed、0 failed、0 skipped，38.734 秒。包含八行 native round-trip 与四行标题栏呈现测试，每行多次切换，见 `native-regressions.log`。
- 静态脚本、Python 语法检查通过；diff 空白检查最终通过。

## 性能测量及未完成项

基线提交 `9aeac7c7ba3ff3b6e108ae42e5e196e6a6a3f311`；二进制摘要、构建环境见 `environment.json`。macOS arm64、Qt 6.11.2、Release，同一 Cocoa 桌面，自动更新关闭，每次使用临时 INI 设置。启动迁移仍可能读取旧 qView 偏好，两个版本采用相同环境。各预热一次后交替测量 10 次，各进程全部返回 0，且退出前确认窗口最大化和菜单就绪。

| 指标（中位数） | 修改前 | 修改后 | 减少 | 50% 验收 |
| --- | ---: | ---: | ---: | --- |
| 启动 | 391.27 ms | 370.98 ms | 5.19% | 失败 |
| 完整退出 | 36.82 ms | 32.53 ms | 11.64% | 失败 |

启动从父进程发起进程创建前计时，至首轮事件循环与窗口菜单均就绪；这是操作就绪标记，不等同于屏幕首帧光学测量。退出从退出前 system-probe 标记至进程完全终止，包含清理和析构；退出请求之前固定 300 ms 的等待不计入退出。父进程接收标记存在调度误差，绝不把其精度描述为硬实时。

初轮结果另保存在 `performance-initial/benchmark.json`：启动减少 5.79%，退出减少 22.51%。最终结果不能择优替换成初轮。样本规模及系统调度噪声不足以证明统计显著收益，也不能外推冷缓存、图片解码或会话恢复负载。现有分段日志显示 Qt/Cocoa 初始化与首次窗口显示仍占主要时间；尚无达到两项 50% 目标的实现和证据。加载器统计环境变量未输出独立加载器计时，不能据此把未分解时间归因给某一个框架。

性能测试进程以返回码 1 如实失败，不能因功能门禁通过而把整体写成通过。

## 复现

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests testLateMenuState testLateFullscreenMenu testLatePauseMenu -v1
python3 tests/menu_lifecycle_static.py
python3 tests/lifecycle_benchmark.py build/Fovelle.app/Contents/MacOS/Fovelle-baseline build/Fovelle.app/Contents/MacOS/Fovelle --output reports/evidence/menu-lifecycle/recheck
```

基线可执行文件在修改生产源码前从当时完成构建的 Release 应用复制到同一 bundle 的 MacOS/Fovelle-baseline，保持相同资源和框架路径；其二进制不纳入 Git。清理构建目录后需从记录的基线提交重建再测，不能拿候选副本作为基线。原始逐次计时和两轮日志纳入证据目录。

旧设计、规格和报告已备份为 `evidence/menu-lifecycle/previous_*`。
