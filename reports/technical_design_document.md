# 全屏切换高质量缩放调度设计

日期：2026-10-02。依据：[根因分析](root_cause.md)的R5及Qt／Apple官方资料交叉核验。

## 目标与边界

将普通 Qt 栅格的同步整图缩放移出全屏请求、几何测量和原生交接事务；保留最终清晰度、fit几何、滚动位置及native后端行为。验收是事务内不生成缩放pixmap，结束后只执行一次针对当前最终尺寸的精化。物理屏幕全场景零掉帧不是该机制门禁所能证明的结论。

## 调用与状态

现有 `fullScreenPanPreservationActive` 从 MainWindow 请求入口开始，到原生 completion／failure 的 Cancel路径结束，比Qt窗口状态位更适合作为守卫。无需新线程、额外快照或新的状态机。

```text
begin／refresh → 停止已排队expensiveScaleTimer → active=true
  → native Begin（幂等）／Measure／resize／timer或动画帧回调
  → applyExpensiveScaling：原有资格检查 → active则返回
  → native完成／失败 → Cancel → endFullScreenPanPreservation
  → 恢复pan → active=false → 有资格且原事务有效则启动50ms单次timer
  → 用当前source、zoom、DPR计算最终像素尺寸 → 一次精化
```

`beginFullScreenPanPreservation()`与`refreshFullScreenPanPreservation()`停掉请求边界前已排队工作。`applyExpensiveScaling()`的统一守卫覆盖以后resize重启的timer，以及`animatedFrameChanged()`调用该方法的路径。停止timer本身不足以覆盖后两者。

`endFullScreenPanPreservation()`先保存`wasActive`，恢复原有pan并清除事务状态，再按现有资格判断启动单次timer。重复Cancel不会重启延迟。多个中间尺寸无需保存，回调执行时读取最新图像和zoom，避免恢复旧尺寸。50ms沿用已有精化延迟；临时画面继续使用当前pixmap与transform，最终清晰度随后恢复。

## 修改范围

- `src/qvgraphicsview.cpp`：统一执行守卫、请求边界停止timer、完成时幂等恢复。
- `tests/tst_qviewtests.cpp`：XPM普通栅格夹具；进入／退出事务、timer／直接调用、三次循环、重复取消及最终尺寸断言。
- `tests/CMakeLists.txt`：注册 `FovelleFullScreenRefinement`，Cocoa、fatal warnings、串行GUI执行。
- `tests/quality_fullscreen_system.py`：将该用例纳入现有系统门禁，要求enter、exit两行均通过，避免数据驱动用例用无参数PASS正则而误判。

## 风险与选择

同步高质量精化仍会在事务结束后运行，其成本没有被变成后台工作；图像较大时仍可能影响随后输入。此次解决的是时间重叠机制。若精化期间换图／改zoom，回调按现有当前状态计算。native SDR／HDR／vector没有资格执行该精化，因此不新增timer。完成与失败复用同一个清理入口；重复begin和Cancel由测试验证。

更改原生通知顺序、GPU管线或快照策略缺少新证据，因此未采用。现有原生运动、方向、快照、准备绘制、终点交接、标题栏及普通zoom精化门禁作为回归保护。测试结果及限制见 [测试完成报告](test_completion_report.md)。
