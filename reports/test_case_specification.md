# 全屏启动绘制预算：测试用例说明

日期：2026-10-01。依据：[根因报告](root_cause.md) R1与 [技术设计](technical_design_document.md) 的平台规范、当前源码和受控实验。

## 覆盖目标与原子断言

| 用例 | 阶段／负载 | 判定 |
| --- | --- | --- |
| 新增 `testFullScreenPreparationPaintBudget` | 真正原生进入／退出；四行；真实PNG／SVG；proxy active时每Paint附加40ms | 每方向原生通知完成；Paint观测非空；源宽度±0.1pt期间≤1次Paint且累计≤75ms；往返几何／fit zoom／标题栏一致 |
| `testFullScreenLayoutPaintBudget` | 单次同步端点Update；四行×三次 | 返回前恰好1次Paint，≤75ms，几何和zoom不变 |
| `testFullScreenPresentationKeepsMoving` | 四种展示／图片×idle／busy，两次原生往返 | 130ms GUI暂停中代理轨迹及图像仍推进，原生完成与终点正确 |
| 标题栏和实际SDR AVIF截图 | 原生全屏及交接 | 标题栏正确，图像持续可见及几何正确 |
| GraphicsView／退出菜单 | fit、垂直pan、overflow inset、Escape退出 | 已有邻接行为断言通过 |

## 新用例步骤

1. 使用scoped选项固定ZoomToFit、禁止自动窗口resize、1:1关闭；创建800×1600临时红蓝PNG或SVG，真实加载到640×480窗口。标题栏分显示／隐藏两种。
2. 记录正常geometry和zoom，安装视口Paint过滤器；事件放行，不替代生产绘制。
3. 每方向开始前记录窗口源宽度和原生entries／exits。过滤器读取独立native探针；只有alpha隐藏真实窗口且可见代理匹配时，才计入本次观测并附加40ms sleep。
4. 宽度与源宽度相差<0.1pt的Paint，计入source_paints及source_cost_ms；其余Paint仍记录完整样本，不被删去。
5. 真正toggleFullScreen，等待相应did-enter／did-exit计数增长，最多5s。输出 `FULLSCREEN_PREPARATION` JSON，然后检查完整性及预算。四行×进入／退出共8个方向过程／轮。
6. 完整往返后断言normal geometry、等价fit zoom和原生标题栏状态。scope guard移除过滤器、必要时等退出完成、关闭窗口并恢复设置。

预算允许正常轨迹启动边界一次Paint，75ms可区别一次40ms成本与两次至少80ms。0.1pt是源几何分类容差。计时只累计受控Paint附加成本，不是完整启动延迟，也不是屏幕帧间隔。探针按Apple presentation近似语义使用。

## 稳定检出与防误通过

最终测试固定后在基线生产代码三轮红、修复代码三轮绿。必须保留源阶段成本样本、退出码及失败信息；探索性失败不计入正式红绿次数。

不能将“没有任何Paint观测”判为改善：新用例拒绝空samples；端点预算还要求同步Paint；实际AVIF截图验证画面。完整矩阵解析拒绝缺失、重复、原生未完成、空观测、超次数和超预算。低负载无自然卡顿时的最终状态正确不能代替此阶段检查。

## 运行入口

```bash
cmake --build build --target fovelle_tests Fovelle -j 4
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 FOVELLE_TEST_SUITE=WindowBehaviorTests \
  build/tests/fovelle_tests testFullScreenPreparationPaintBudget -v1
ctest --test-dir build -R 'Fovelle(FullScreenMotion|FullScreenPaintBudget|FullScreenPreparationBudget|HiddenTitlebarFullScreen|SDRFullScreenPresentation)$' --output-on-failure
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests \
  --output reports/evidence/fullscreen_preparation/system.json
```

必须使用Cocoa和支持原生全屏的本机环境；外部AVIF fixture条件记录在完成报告。物理显示器逐帧呈现、用户现场快照像素成本和尾部事件队列负载不属于本用例已证明的范围。
