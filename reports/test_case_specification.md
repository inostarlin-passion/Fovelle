# 全屏偶发掉帧测试用例说明

日期：2026-10-02。追踪：[根因分析](root_cause.md)R5 → `WindowBehaviorTests::testFullScreenDefersExpensiveRefinement` → CTest `FovelleFullScreenRefinement`。

## 问题检出用例 TC-FS-REFINEMENT

前置：macOS Cocoa、可显示窗口、Qt 6.11.2；2401×1799 XPM，ZoomToFit，Expensive平滑缩放，无缩放上限，DPR由运行环境读取。断言native SDR、HDR和vector标志均为false，确保昂贵分支实际可达。临时文件、选项、窗口和退出策略用作用域清理。

数据行：`enter`调用请求边界begin；`exit`调用refresh。每行三轮，共六轮。该用例确定性驱动全屏事务边界与几何变化，不依赖AppKit动画时钟；真实原生切换由既有全屏用例验证。

步骤与预期：

1. 恢复原pixmap，停止精化timer，保存其cacheKey；预先排队timer后进入事务。
2. 再调用MainWindow Begin以覆盖原生回调的重复begin，改变viewport尺寸；用真实timer的1ms到期确定性模拟动画期间timeout。等待信号后pixmap key必须不变。
3. 恢复原pixmap并直接调用 `applyExpensiveScaling()`，key必须不变，覆盖动画帧调用共用入口。
4. 再改变viewport为最终尺寸，取消事务两次；必须在2秒内精化，恢复timer恰好到期一次。
5. 最终pixmap尺寸必须等于loaded尺寸×当前zoom×DPR；再等待100ms，timeout仍仅一次。
6. 累积所有事务内失败，完成三轮后报告，确保进入／退出和两个调用来源都被观测。

旧代码即使恢复最终清晰度成功，也必须因步骤2／3失败。新代码即使简单地永久禁用精化，也必须因步骤4失败；恢复错误尺寸或重复调度分别由步骤5与单次timeout断言检出。性能预算不依赖机器偶发繁忙，而用不可发生的事务内整图重建作为确定性门禁。

## 回归矩阵

| 用例 | 验证内容 |
| --- | --- |
| FullScreenMotion | 隐藏／可见标题栏、raster／vector、idle／busy、两周期、双方向；GUI受控暂停时已提交运动继续推进 |
| FullScreenPaintBudget | 每次终点Update一次必要Paint及受控CPU预算 |
| FullScreenPreparationBudget | 源阶段和终点Paint预算，原生完成、updates恢复、普通窗口几何与zoom复原 |
| FullScreenSnapshotReuse | 不变图像请求共享RGBA缓存，原生双方向调用 |
| FullScreenColdOrientation | 六种方向source-order内容和几何，换图失效 |
| HiddenTitlebarFullScreen | 标题栏状态与全屏行为 |
| ZoomScrollbarExpensiveRefinement | 全屏外既有高质量缩放、scrollbar和语义anchor稳定 |
| FullScreenMetricsGate | 既有遥测缺失、错误、预算超限等负例门禁 |

## 执行与通过标准

```sh
cmake --build build --target fovelle_tests -j 8
ctest --test-dir build -R 'FovelleFullScreen|FovelleHiddenTitlebarFullScreen|FovelleZoomScrollbarExpensiveRefinement' --output-on-failure
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests --output reports/evidence/fullscreen_refinement/system.json
```

GUI命令串行执行。新测试旧代码三次运行必须都在enter／exit失败，修复后同一测试三次运行全部通过；既有回归和系统门禁也须通过。实际日志与统计见 [完成报告](test_completion_report.md)。presentation轨迹、CPU预算及pixmap替换均不等于物理屏幕FPS；多屏、不同刷新率及现场GPU负载没有在本轮穷举。
