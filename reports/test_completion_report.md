# 滚动条端点与右下角缩放测试完成报告

## 1. 完成结论

本次修复已完成。针对 `reports/root_cause.md` 中的两条问题，四条原子验收标准全部通过：

| 原子标准 | 结论 | 证据 |
| --- | --- | --- |
| AC-SB-01：两轴滚动条 min/max 可达图片四边 | PASS | `testScrollBarsReachImageEdges` |
| AC-SB-02：handle 运动方向无端部视觉内缩 | PASS | `testScrollBarHandleTrackEndpoints` + `ST-SB-01` |
| AC-ZOOM-01：中心缩放跨 AsNeeded 阈值稳定 | PASS | `testZoomAcrossScrollbarThresholdKeepsViewportCenterStable` |
| AC-ZOOM-02：右下显式锚点在出现/稳定后不跳变 | PASS | `testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar` |

## 2. 实现变更

- [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp#L274)：自动滚动条引起的 viewport resize 在 pending zoom anchor 有效时跳过普通 `-sizeDelta/2` 重定位，先恢复锚点；scene-rect 重入也走同一恢复路径。
- [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp#L1029)：垂直 handle 改为 `margin: 0px 1px`，水平 handle 改为 `margin: 1px 0px`，移除运动方向两端的 2px 视觉间隙。
- [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp#L1292)：加载新文件时使旧 zoom anchor generation 失效，避免旧 scene 点污染新图片。
- [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp#L1610)：缩放前保存 scene anchor、目标 viewport 点及“固定鼠标点/跟随新 viewport 中心”模式；在 transform、scene rect、resize 和延迟窗口后恢复两轴值。
- [`src/qvgraphicsview.cpp`](../src/qvgraphicsview.cpp#L2826)：新增 `restorePendingZoomAnchor()`，使用当前映射偏差、RTL 水平翻转和整数像素取整恢复滚动值，并标记为内部更新。
- [`src/qvgraphicsview.h`](../src/qvgraphicsview.h#L319)：增加锚点恢复接口及 pending 状态。
- [`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp#L5047)：固化图片四边端点、handle 样式和右下锚点回归测试；并为现有滚动条轴策略测试固定 `WindowNoState`，避免持久化窗口状态改变 viewport 前置条件。
- [`tests/scrollbar_zoom_acceptance_static.py`](../tests/scrollbar_zoom_acceptance_static.py)：新增结构静态验收脚本。
- [`reports/technical_design_document.md`](technical_design_document.md)、[`reports/test_case_specification.md`](test_case_specification.md)：写入设计、原子标准和完整测试字段。

## 3. 静态验证

### 3.1 项目专用结构检查

执行：

```bash
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
```

结果：返回码 `0`，JSON `passed=true`，以下 6 项全部为 `true`：

```text
ST-SB-01   PASS   AsNeeded/QSS 端点合同
ST-SB-02   PASS   active item scene geometry 与 scene-rect guard
ST-ZOOM-01 PASS   pending anchor 保存、延迟恢复、中心/RTL 规则
ST-ZOOM-02 PASS   自动 scrollbar resize 不走普通半差补偿
ST-TEST-01 PASS   每条标准都有可执行 QtTest 入口
ST-TEST-02 PASS   每个测试说明均含六个必填字段
```

同时执行 `python3 -m py_compile tests/scrollbar_zoom_acceptance_static.py`，返回码 `0`。

### 3.2 Clang 静态分析器

执行生产 view 文件的核心 analyzer 检查：

```bash
SDK_PATH="$(xcrun --show-sdk-path)"
clang-tidy -p build-current src/qvgraphicsview.cpp \
  --checks='-*,clang-analyzer-core.*' \
  --extra-arg=-isysroot --extra-arg="$SDK_PATH"
```

结果：返回码 `0`，无 analyzer 输出。泛化 `bugprone-*` 检查未作为门禁，因为该仓库已有与本次改动无关的参数可交换、窄化转换等告警；核心 analyzer 结果不受影响。

### 3.3 编译与格式检查

```bash
cmake --build build-current --target Fovelle fovelle_tests --parallel 2
git diff --check
```

结果：两个目标均构建成功，`git diff --check` 返回码 `0`。应用打包阶段出现 Ghostscript 依赖的既有 macOS code-signature invalidation warning，但不影响构建退出码。

## 4. 动态验证

### 4.1 完整 GraphicsViewTests

执行：

```bash
FOVELLE_TEST_SUITE=GraphicsViewTests \
QTEST_FUNCTION_TIMEOUT=30000 \
build-current/tests/fovelle_tests -v1
```

结果：`32 passed, 0 failed, 0 skipped, 0 blacklisted`，退出码 `0`。

关键回归日志：

```text
before        = QPoint(599,407)
after_layout  = QPointF(599,407)
after_settling= QPointF(599,407)
```

在 640×480 Cocoa 测试窗口中，两个滚动条出现后 viewport 为 628×496；锚点没有发生可观测像素位移。

### 4.2 ScrollHelperTests

执行：

```bash
FOVELLE_TEST_SUITE=ScrollHelperTests \
QTEST_FUNCTION_TIMEOUT=30000 \
build-current/tests/fovelle_tests -v1
```

结果：`7 passed, 0 failed, 0 skipped, 0 blacklisted`，退出码 `0`。最小端、最大端硬钳制及无回弹回归均通过。

### 4.3 验收用例与相关回归子集

额外执行的 10 个 GraphicsView 用例（含旧滚动条轴策略、图片端点、handle 样式、中心阈值、右下锚点、fit/旋转和标题栏安全区）结果为 `10 passed, 0 failed`；与 `ScrollHelperTests` 合计定向动态结果为 `17 passed, 0 failed`。

## 5. 根因到修复的闭环

| 已确证机制 | 修复/验证 |
| --- | --- |
| 显式 `sceneRect` 是 Qt 滚动条范围的输入，必须覆盖 active item | `getSceneRectForViewport()` 继续以 active item 几何为非 native 来源；`ST-SB-02` 检查同步更新与 guard；AC-SB-01 动态检查实际边缘 |
| `ScrollBarAsNeeded` 显示时 viewport 缩小且两轴 range/value 联动 | `resizeEvent()` 的 pending 分支跳过普通 resize 半差移动；AC-ZOOM-01/02 在出现后采样 |
| QScrollBar value 为整数并受 min/max 截断 | 只恢复合法 range 内的值；测试目标在右下区域但留有可达余量，并使用明确容差 |
| QSS movement-direction margin 造成视觉端点间隙 | 两条 handle 规则改为零运动方向 margin；AC-SB-02 动态与静态双重检查 |
| 延迟 backing/scene 几何更新可能触发第二次位置变化 | generation 保护的 150ms pending anchor 窗口覆盖后续布局；AC-ZOOM-02 对立即与稳定两个时刻分别断言 |

## 6. 联网检索与溯源记录

本次采用四跳证据链：Qt 类文档确认 scene/range 语义 → Qt 官方源码确认 `AsNeeded` 两轴重算 → 项目源码/历史确认 `zoomAbsolute`、`updateSceneRect`、`resizeEvent` 时序 → QtTest 采集实际 viewport、映射锚点和 scrollbar endpoint。使用的权威资料为：

- [QGraphicsView sceneRect、scrollbar range 与 fitInView](https://doc.qt.io/qt-6/qgraphicsview.html)
- [QAbstractScrollArea viewport 与 AsNeeded 行为](https://doc.qt.io/qt-6/qabstractscrollarea.html)
- [QAbstractSlider value 的整数合法范围](https://doc.qt.io/qt-6/qabstractslider.html)
- [Qt `QGraphicsViewPrivate::recalculateContentSize()` 源码](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)
- [Qt ScrollBarAsNeeded 枚举](https://doc.qt.io/qt-6/qt.html)
- [Qt 样式表 box model 与 margin](https://doc.qt.io/qt-6/stylesheet-customizing.html#box-model)
- [QPixmap device-independent size](https://doc.qt.io/qt-6/qpixmap.html#deviceIndependentSize)

## 7. 遗留限制

- QtTest 使用 Cocoa 真实 viewport，但没有在 CI 中注入跨应用的物理 HID scrollbar 拖拽；端点行为通过真实 `QScrollBar::minimum()/maximum()`、映射图片边缘和 QSS 合同验证。
- 右下用例刻意避开图片边界本身的合法 maximum 截断，以隔离“自动滚动条出现导致的跳变”与“目标不可达”的正常行为。
- `reports/evidence/scrollbar_zoom_static.json` 是静态检查的机器可读证据；报告正文记录了最终命令和退出结果。
