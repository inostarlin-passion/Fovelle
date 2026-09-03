# 图片缩放垂直滚动条跳变测试完成报告

## 1. 结论

`AC-ZOOM-VBAR-TRANSIENT` 已通过：键盘快捷键和鼠标滚轮触发的放大/缩小，在 Disabled/Expensive、普通 DPR/HiDPI 下均未出现垂直滚动条 A→B→A 瞬态；动画结束、延迟回调完成和稳态 quiet turns 后仍保持通过。

## 2. 原子验收结果

| 原子验收标准 | 结果 | 结构化用例 | 固化测试 |
| --- | --- | --- | --- |
| `AC-ZOOM-VBAR-TRANSIENT`：真实键盘/滚轮缩放全过程中，垂直 value、独立期望映射、thumb 和锚点不发生错误跳变，最终保持稳定 | PASS | `TC-ZOOM-VBAR-NO-TRANSIENT-EXCURSION` | `GraphicsViewTests::testZoomShortcutsKeepVerticalScrollbarStable_data/testZoomShortcutsKeepVerticalScrollbarStable` |

每个数据行分别执行确定性逐毫秒扫描和真实时钟重放；任一 committed/paint/timer sample 失败都会使该原子标准失败，不会被最终位置恢复掩盖。

## 3. 实现结果

- `applyExpensiveScaling()` 在替换高分辨率 backing pixmap 前后按旧/新 image scene rect 重建 pending anchor 的归一化 UV，避免延迟高开销回调把垂直 value 写到旧坐标系。
- 高开销缩放在动画进行时延迟最终 backing 替换；动画 range 始终跟随当前 displayed frame。
- 临时 anchor margin 使用实际 backing item 的 scene 尺寸；自然居中的、仍能由对齐策略表示的 wheel anchor 不制造虚假水平 range。
- anchor settle 改为可观测的成员 timer，并使用 generation 使过期回调失效。
- 新增 `ZoomTraceProbe`：记录 paint、range/value、layout/resize、animation 和三个相关 timer；以独立 transform oracle、Qt style thumb oracle 和固定归一化 anchor 检查每个样本。
- CTest 新增普通和 `QT_SCALE_FACTOR=2` 两个聚焦进程。

## 4. 实际验证证据

### 4.1 静态阶段

```bash
python3 -m py_compile \
  tests/scrollbar_zoom_acceptance_static.py \
  tests/shortcut_toggle_acceptance_static.py
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
python3 tests/shortcut_toggle_acceptance_static.py \
  --repo . --output reports/evidence/shortcut_toggle_static.json
git diff --check
```

结果：两个 Python 脚本均返回 0；滚动条脚本 14 个合同检查全部通过，快捷键兼容脚本 6 个检查全部通过，差异空白检查通过。

### 4.2 构建阶段

```bash
cmake -S . -B build -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
```

结果：`Fovelle`、`fovelle_tests` 和 `fovelle_native_drag_helper` 构建成功。

### 4.3 聚焦轨迹

```bash
ctest --test-dir build \
  -R '^FovelleZoomScrollbarTrajectory(HiDpi)?$' \
  --output-on-failure
```

结果：普通 DPR 与 HiDPI `2/2` 通过；每个进程均覆盖 8 行数据、确定性扫描、live replay、timer quiet 和终态检查。

```bash
ctest --test-dir build \
  -R '^FovelleZoomScrollbarTrajectory$' \
  --repeat until-fail:30 --output-on-failure
```

结果：普通 DPR `30/30` 通过，总耗时约 289.13 秒。

### 4.4 默认全量 CTest

```bash
ctest --test-dir build --output-on-failure --timeout 120
```

结果：`4/4` 通过，总耗时约 100.56 秒：

- `FovelleTests`：通过，51.61 秒；
- `FovelleShortcutSettingsTests`：通过，2.63 秒；
- `FovelleZoomScrollbarTrajectory`：通过，9.69 秒；
- `FovelleZoomScrollbarTrajectoryHiDpi`：通过，36.63 秒。

## 5. 证据链与交叉核验

根因判断基于 [`reports/root_cause.md`](root_cause.md)、源码轨迹和 Qt 官方语义交叉核验：[`QGraphicsView::sceneRect`](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop) 决定 scene 导航范围，[`QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html#details) 的按需滚动条布局会改变 viewport，[`QPropertyAnimation`](https://doc.qt.io/qt-6/qpropertyanimation.html) 提供可扫描的动画属性时间域，[`QTransform`](https://doc.qt.io/qt-6/qtransform.html) 提供 scene/viewport 映射，而 [`QStyle`](https://doc.qt.io/qt-6/qstyle.html) 提供 thumb 几何来源。

链式结论是：scene range 与 displayed frame 不一致会产生布局二次写入；backing pixmap 替换会改变 scene 坐标尺度；旧 anchor 绝对坐标因此会在延迟回调中写入错误垂直位置。以同一归一化图片点重建 anchor，再用独立 transform oracle 验证，能够直接覆盖该证据缺口。

## 6. 输出文件

- 技术设计：[`technical_design_document.md`](/Users/inostarlin/code/Fovelle/reports/technical_design_document.md)
- 结构化用例：[`test_case_specification.md`](/Users/inostarlin/code/Fovelle/reports/test_case_specification.md)
- 静态机器证据：[`scrollbar_zoom_static.json`](/Users/inostarlin/code/Fovelle/reports/evidence/scrollbar_zoom_static.json)
- 动态失败时的轨迹位置：`build/test-results/zoom-vbar/<case>/<stage>/`

## 7. 边界说明

- 测试使用真实 QWidget 键盘/滚轮事件，但不模拟需要 Accessibility 权限的系统级 HID；该能力不属于本问题的默认 CTest 门禁。
- 已执行一次临时故障注入校准：移除 backing-pixmap anchor UV 重基准后，`keyboard-zoom-in-expensive` 按预期在 sample 405 失败（`vertical value 19` 对 `expected 438`，最大偏差 419）；随后恢复实现并重新构建，sanity case 通过。故障代码未保留在最终工作区。
- 测试使用的是 Qt 6.11.1 Cocoa/arm64 本机；外部 Accessibility/HID 和外部网络状态不作为本次通过判定。
