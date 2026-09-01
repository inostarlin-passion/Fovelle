# Fovelle 图片缩放与滚动条测试完成报告

## 1. 结论

代码修复、回归测试代码和三份 Markdown 报告已完成。当前本机 macOS Cocoa/Qt 6.11.1 环境下，手动 pan/延迟 anchor 回归、原始全屏端点回归、完整产品 QtTest 和默认 CTest 均通过。

远端当前基线是 commit `babf065`：GitHub Actions 的 CTest 产品测试步骤失败（[Checks 33499768039](https://github.com/inostarlin-passion/Fovelle/actions/runs/33499768039)，`Run Unit Tests` 退出码 8），而同一提交的 [Build Fovelle 33499767940](https://github.com/inostarlin-passion/Fovelle/actions/runs/33499767940) 构建成功。本地已复现并修复该测试阶段竞态；由于本次未执行 push，远端仍标记为待重跑，不把未触发的远端结果记为 PASS。

## 2. 原子验收结果

| 标准 | 结果 | 证据 |
| --- | --- | --- |
| AC-SB-01 图片四边可达 | PASS | `testScrollBarsReachImageEdges` |
| AC-SB-02 scrollbar 厚度统一 | PASS | `testScrollBarGeometryMatchesViewMetricAndDoesNotRebound` |
| AC-SB-03 handle 运动方向无端隙 | PASS | `testScrollBarHandleTrackEndpoints` |
| AC-ZOOM-01 AsNeeded 阈值中心稳定 | PASS | `testZoomAcrossScrollbarThresholdKeepsViewportCenterStable` |
| AC-ZOOM-02 右下显式 anchor 稳定 | PASS | `testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar` |
| AC-ZOOM-03 端点缩小无空白/回弹 | PASS | `testScrollBarGeometryMatchesViewMetricAndDoesNotRebound` |
| AC-ZOOM-04 手动 pan 覆盖延迟 anchor | PASS | `testManualScrollCancelsPendingZoomAnchor`、`testFullscreenExitPreservesVerticalPan` |
| AC-CI-01 默认 CTest 可重复 | PASS | `ctest --test-dir build -N`、默认 CTest |
| AC-STATIC-01 测试入口与规格可追踪 | PASS | `scrollbar_zoom_acceptance_static.py`、本文件 |

## 3. 实现变更

- `src/qvgraphicsview.cpp`：删除 scrollbar QSS 中固定的 12px 宽/高，让实际控件与 `QGraphicsView` 的 `PM_ScrollBarExtent` 使用同一平台几何；保留主题颜色和运动方向为 0 的 handle margin。
- `src/qvgraphicsview.cpp/.h`：滚动条用户信号以及滚轮、键盘、画布拖拽、原生 pan 路径通过 `cancelPendingZoomAnchor()` 使旧的延迟 generation 失效；自动布局的 `valueChanged` 不再被误判为用户输入，zoom/layout 自己的 `setValue` 仍由 `fullScreenPanInternalUpdate` 守卫。
- `tests/tst_qviewtests.cpp`：新增 `testManualScrollCancelsPendingZoomAnchor`，并保留原始 `testFullscreenExitPreservesVerticalPan` 作为 hosted failure 的直接回归；同时比较两轴 `sizeHint` 与平台 extent，覆盖“最大端点 → 缩小 → 延迟约束”立即/稳定两个阶段。
- `tests/CMakeLists.txt`：增加 `FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION`，默认关闭权限/外部 fixture 依赖的 native drag CTest；helper 仍可单独构建并显式启用。
- `tests/scrollbar_zoom_acceptance_static.py`：增加固定厚度、平台 extent、延迟端点、手动 pan anchor 和默认 CTest 注册合同。
- `tests/ci_quality_pipeline.py`：增加 `CI-UNIT-008` 手动 pan/延迟 anchor 回归，并更新当前 Actions 证据与多跳诊断。

## 4. 静态验证

执行：

```bash
python3 -m py_compile \
  tests/scrollbar_zoom_acceptance_static.py \
  tests/shortcut_toggle_acceptance_static.py \
  tests/ci_quality_pipeline.py
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
python3 tests/shortcut_toggle_acceptance_static.py \
  --repo . --output reports/evidence/shortcut_toggle_static.json
git diff --check
```

结果：Python 语法、滚动/缩放源码合同、现有快捷键合同和差异空白检查均返回 0。静态合同覆盖 QSS 固定厚度缺失、`PM_ScrollBarExtent` 动态断言、scene rect 防重入、pending zoom anchor 取消、CTest opt-in 和六字段规格。

clang-tidy 静态分析也通过：

```bash
clang-tidy --checks=-*,clang-analyzer-* -p build src/qvgraphicsview.cpp --quiet
clang-tidy -p build tests/tst_qviewtests.cpp --quiet
```

两条命令均返回 0 且无诊断输出。

构建过程同时完成 C++ 编译，命令为：

```bash
cmake --build build --parallel 2
```

结果：`Fovelle`、`fovelle_tests` 和 `fovelle_native_drag_helper` 均构建成功。

## 5. 动态验证

关键新增用例：

```bash
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 \
QTEST_FUNCTION_TIMEOUT=30000 FOVELLE_TEST_SUITE=GraphicsViewTests \
build/tests/fovelle_tests \
  testManualScrollCancelsPendingZoomAnchor -v1
```

结果：使用 `-repeat 5 -silent` 重复运行后为 `15 passed, 0 failed, 0 skipped, 0 blacklisted`；该用例使用真实 scrollbar handle 拖动和 250ms 等待覆盖生产代码的 150ms 延迟回调，并确认垂直 maximum 与图像底边稳定。原始失败路径 `testFullscreenExitPreservesVerticalPan` 同样重复 5 次通过（`15 passed`）。

平台 extent/缩小端点用例 `testScrollBarGeometryMatchesViewMetricAndDoesNotRebound` 单次运行 `3 passed, 0 failed, 0 skipped, 0 blacklisted`；使用 `-repeat 3` 重复运行后为 `9 passed, 0 failed, 0 skipped, 0 blacklisted`。

完整产品门禁：

```bash
QT_QPA_PLATFORM=cocoa \
ctest --test-dir build --output-on-failure --timeout 90
```

结果：`100% tests passed out of 2`；`FovelleTests` 和 `FovelleShortcutSettingsTests` 均返回 0。默认 CTest 不再执行需要外部桌面权限的 native drag 驱动。

现有四阶段质量脚本在加入新用例后已通过：`static 8/8`、`unit 8/8`、`integration 4/4`、`system 3/3`，失败用例为空。

额外关键回归组合（同一 Cocoa 进程）已通过：

```text
testScrollBarsReachImageEdges                                  PASS
testZoomAcrossScrollbarThresholdKeepsViewportCenterStable      PASS
testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar       PASS
testFullscreenExitPreservesVerticalPan                          PASS
testManualScrollCancelsPendingZoomAnchor                         PASS
testScrollBarGeometryMatchesViewMetricAndDoesNotRebound         PASS
```

真实 `build/Fovelle.app` 加载用户指定的 `1.avif` 后，三格滚轮放大出现两轴 scrollbar；继续放大一格、将垂直条置底、反向滚轮一格后，立即采样与等待 800ms 后的 scrollbar value 均保持不变，截图中右侧和下方没有空白带。AX 数值受窗口尺寸影响，不作为跨机器固定值。

## 6. 根因—修复—验证闭环

| 根因 | 修复 | 验证 |
| --- | --- | --- |
| Qt 按 15px 计算 viewport，QSS 强制实际 scrollbar 为 12px | 删除 QSS 固定宽/高，回归平台 extent | AC-SB-02、AC-ZOOM-03 |
| 3px surplus 在延迟 bounds constraint 中被纠正，造成先露白后位移 | 让 range 与实际 viewport 使用同一几何源；保留既有 hard clamp | AC-SB-01、AC-ZOOM-03 |
| AsNeeded scrollbar appearance 触发 viewport resize | 复用既有 pending anchor 分支，跳过普通 resize 半差补偿 | AC-ZOOM-01、AC-ZOOM-02 |
| 延迟 zoom anchor 晚于手动 scrollbar pan 执行 | 用户输入信号取消 pending generation，自动布局 `valueChanged` 保留，内部 zoom/layout 写入使用 guard | AC-ZOOM-04 |
| 外部权限/挂载依赖的 native drag 进入默认 CTest | CMake 选项默认 OFF，专项运行显式 ON | AC-CI-01 |

## 7. 联网溯源

采用“GitHub Actions 运行状态 → Qt 官方文档/源码 → 项目根因报告 → 本地 QtTest/CTest”的多跳链路：

- [Qt QGraphicsView sceneRect](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop)：确认 scene rect 是可导航场景范围。
- [Qt QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)：确认滚动条策略会改变 viewport 几何。
- [Qt 官方 QGraphicsView 源码](https://github.com/qt/qtbase/blob/dev/src/widgets/graphicsview/qgraphicsview.cpp)：确认 `recalculateContentSize()` 读取 `PM_ScrollBarExtent`。
- [macOS 26 arm64 Actions runner image](https://github.com/actions/runner-images/blob/main/images/macos/macos-26-arm64-Readme.md)：核对 CI runner/SDK 背景。
- [GitHub Actions 工作流日志说明](https://docs.github.com/en/actions/how-tos/monitor-workflows/use-workflow-run-logs)：说明检查步骤、退出码和日志下载的证据边界。
- [最新 GitHub Checks 运行记录](https://github.com/inostarlin-passion/Fovelle/actions/runs/33499768039)、[最新 Build Fovelle 运行记录](https://github.com/inostarlin-passion/Fovelle/actions/runs/33499767940)：定位当前 CTest 失败步骤与同提交构建成功的对照边界。
- `reports/root_cause.md`：记录 12px/15px 差异、3px range surplus 及复现观察。

## 8. 限制与后续动作

- 本地完整默认 CTest 已通过；权限依赖的 native drag 专项未作为默认门禁执行，若要运行请配置 `-DFOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION=ON`、fixture 和 macOS 权限。
- 本地验证不等于远端 Actions 已重跑；将本次修改推送到目标分支后，应确认新的 workflow run 的 `Run Unit Tests` 变为成功。当前两个链接仍是诊断基线。
