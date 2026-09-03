# Fovelle 图片缩放、菜单与滚动条测试完成报告

## 1. 结论

本次五项功能已实现，且每项都有原子验收标准、结构化测试用例、QtTest 固化代码和静态合同检查。当前本机 macOS Cocoa / arm64 / Qt 6.11.1 环境下，构建、静态检查、聚焦动态测试和默认 CTest 均通过。

## 2. 原子验收结果

| 原子验收标准 | 结果 | 固化测试代码 | 静态合同 |
| --- | --- | --- | --- |
| AC-ZOOM-TRANSITION-200MS：四个指定入口使用 200ms 过渡 | PASS | `GraphicsViewTests::testZoomTransitionCoversWheelKeyboardAndMenus` | `ST-ZOOM-TRANSITION-01` |
| AC-SORT-FIXED-DEFAULT：移除右键排序菜单，固定名称升序且不读旧配置 | PASS | `WindowBehaviorTests::testSortConfigurationIsIgnoredAndContextMenuHasNoSortMenu` | `ST-SORT-CONTRACT-01` |
| AC-HELP-WEBSITE：两个 Help 菜单插入 Website、打开精确 URL、同步翻译 | PASS | `WindowBehaviorTests::testWebsiteHelpActionAndContextMenuContract` | `ST-MENU-CONTRACT-01` |
| AC-ZOOM-ANCHOR-PROJECTION：图片内以鼠标点为锚，图片外逐轴投影到边界 | PASS | `GraphicsViewTests::testZoomAnchorProjectsInsideAndOutsideImage` | `ST-ZOOM-ANCHOR-PROJECTION-01` |
| AC-SCROLLBAR-VERTICAL-STEADY：垂直滚动条在过渡、settle、稳态不跳变 | PASS | `GraphicsViewTests::testZoomTransitionLeavesVerticalScrollbarStable` | `ST-SB-01/02`、`ST-TEST-01` |

完整的六字段结构化用例见 [`reports/test_case_specification.md`](test_case_specification.md)；技术方案、证据链和显式推理前提见 [`reports/technical_design_document.md`](technical_design_document.md)。

## 3. 实现摘要

- `QVGraphicsView` 将逻辑缩放值与当前显示帧分离，通过 `QPropertyAnimation` 统一驱动用户缩放，duration 固定为 200ms，使用 `OutCubic`，结束时归一化到精确终帧。
- 图片外锚点使用逐轴 `qBound` 投影到图片边界；临时 scene margin 和延迟 settle 使边界锚点在滚动条布局变化后仍可达，并以 generation 使过期回调失效。
- scene rect 和 scrollbar range 跟随当前显示帧；移除固定 12px scrollbar 厚度规则，使用 Qt 平台 `PM_ScrollBarExtent`，并以更新守卫、内部写入标记和用户端点保留消除垂直跳变。
- `Sort Files By` 构建路径、旧排序设置项和会话排序字段已移除；`QVFileEnumerator` 始终保持 `Name` / ascending，旧 setter 仅保留兼容接口并忽略传入值。
- 标题栏和右键 Help 菜单共享 canonical `website` action，位置为 `Project Homepage` 与 `Check for Updates` 之间，URL 为 `https://fovelle-viewer.onrender.com/`；四个目录已同步翻译：官方网站、官方網站、Sitio web、公式サイト。

## 4. 验证证据

### 4.1 静态阶段

执行：

```bash
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
python3 tests/shortcut_toggle_acceptance_static.py \
  --repo . --output reports/evidence/shortcut_toggle_static.json
git diff --check
```

结果：滚动/缩放脚本 `passed: true`，13 个静态检查全部通过；快捷键脚本 `passed: true`，6 个检查全部通过；差异空白检查返回 0。静态检查覆盖 200ms 动画、Help action 顺序和 URL、排序配置移除、锚点投影、scene rect 防重入，以及每个用例的六个字段。

### 4.2 构建阶段

```bash
cmake --build build --parallel 2
```

结果：`Fovelle`、`fovelle_tests` 和 `fovelle_native_drag_helper` 均构建成功。

### 4.3 聚焦动态阶段

```bash
FOVELLE_TEST_SUITE=GraphicsViewTests build/tests/fovelle_tests \
  testZoomTransitionCoversWheelKeyboardAndMenus \
  testZoomAnchorProjectsInsideAndOutsideImage \
  testZoomTransitionLeavesVerticalScrollbarStable -v1
```

结果：`5 passed, 0 failed`（含初始化和清理）。

```bash
FOVELLE_TEST_SUITE=WindowBehaviorTests build/tests/fovelle_tests \
  testWebsiteHelpActionAndContextMenuContract \
  testSortConfigurationIsIgnoredAndContextMenuHasNoSortMenu -v1
```

结果：`4 passed, 0 failed`（含初始化和清理）。

### 4.4 全量动态阶段

```bash
ctest --test-dir build --output-on-failure --timeout 120
```

结果：`100% tests passed out of 2`，`FovelleTests` 与 `FovelleShortcutSettingsTests` 均通过，总耗时 44.57 秒。

## 5. 根因—修复—验证

| 已核对根因 | 修复 | 验证 |
| --- | --- | --- |
| QSS 固定 scrollbar 厚度与 Cocoa viewport extent 不一致 | 删除固定宽/高，统一使用平台 metric | `testScrollBarGeometryMatchesViewMetricAndDoesNotRebound` |
| 自动 AsNeeded 重排触发 scene rect/viewport 的二次补偿 | 当前显示帧计算 range，并区分内部更新与用户 pan | `testZoomTransitionLeavesVerticalScrollbarStable`、全量 GraphicsViewTests |
| 延迟 anchor 回调晚于用户 scrollbar 操作 | 用户 slider/pan 取消 pending generation，并保留明确端点 | `testManualScrollCancelsPendingZoomAnchor` |
| 图片外坐标会把锚点带入空白区或回退中心 | 变换前逐轴投影到显示图片矩形 | `testZoomAnchorProjectsInsideAndOutsideImage` |

推理所用的官方 API 事实包括：[`QGraphicsView sceneRect`](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop) 定义可导航场景范围、[`QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html#details) 的滚动条布局会改变 viewport、[`QPropertyAnimation`](https://doc.qt.io/qt-6/qpropertyanimation.html) 提供带 easing 的属性插值、[`QTransform`](https://doc.qt.io/qt-6/qtransform.html) 区分场景和 viewport 坐标，以及 [`QAbstractSlider`](https://doc.qt.io/qt-6/qabstractslider.html) / [`QScrollBar`](https://doc.qt.io/qt-6/qscrollbar.html) 的用户动作信号语义。它们与 [`reports/root_cause.md`](root_cause.md) 和本地源码/测试结果交叉核验后，形成“几何一致性 → 当前帧动画 → 锚点投影 → 用户输入优先”的修复链。

## 6. 证据边界

- 默认 CTest 不依赖 Accessibility 权限或外部桌面 fixture；真实 HID/native drag 专项仍需显式开启和单独环境验证。
- URL 测试验证程序向 `QDesktopServices` 发出精确 URL，不把外部站点当前可达性混入程序行为判定。
- 本次未推送代码，因此没有声称远端 CI 已对本补丁重跑；本报告结论仅基于当前工作区的实际构建和测试输出。
