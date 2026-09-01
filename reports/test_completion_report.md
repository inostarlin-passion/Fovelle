# Fovelle 图片视图与快捷键测试完成报告

## 1. 完成结论

本次实现已完成。8 条原子验收标准均有对应测试代码，并在当前 macOS Cocoa/Qt 6.11.1 环境通过：

| 标准 | 结果 | 动态证据 |
| --- | --- | --- |
| AC-SB-01 图片四边可达 | PASS | `GraphicsViewTests::testScrollBarsReachImageEdges` |
| AC-SB-02 handle 运动方向无端隙 | PASS | `testScrollBarHandleTrackEndpoints` |
| AC-ZOOM-01 AsNeeded 阈值中心稳定 | PASS | `testZoomAcrossScrollbarThresholdKeepsViewportCenterStable` |
| AC-ZOOM-02 右下锚点稳定 | PASS | `testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar` |
| AC-SC-01 Shortcuts Action 面替换 | PASS | `testToggleFitAnd100IsTheOnlyFitShortcutAction` |
| AC-SC-02 默认 Z | PASS | `testToggleFitAnd100DefaultsToZ` |
| AC-SC-03 fit↔100% 状态机 | PASS | `testToggleFitAnd100ChangesBetweenFitAnd100Percent` |
| AC-SC-04 四种翻译 | PASS | `testToggleFitAnd100Translations` |

## 2. 实现变更

- `src/qvgraphicsview.cpp`：保留 active item scene geometry；scene rect 更新防重入；自动滚动条触发的 resize 在 pending zoom anchor 有效时跳过普通半差补偿；跨 transform、viewport layout 和延迟阶段恢复 anchor；运动方向 scrollbar handle margin 归零。
- `src/scrollhelper.cpp`：端点采用硬夹紧，避免拖动/触控板平移越过图片有效范围后回弹露白。
- `src/shortcutmanager.cpp`：将 Shortcut 行改为 `Toggle Fit and 100%` / `togglefitand100` / `Z`，并清理已删除 Action 的旧持久化快捷键。
- `src/actionmanager.cpp`：View 菜单、Action library 与 dispatcher 统一使用新 key，移除旧三个 Action。
- `src/mainwindow.cpp/.h`：新增 fit/100% 状态机，fit 状态触发 `zoomAbsolute(1.0, ...)`，绕过遗留 `originalsizeastoggle` 切换语义。
- `i18n/qview_*.ts`：加入 ActionManager、ShortcutManager 两个 context 的四种精确翻译，并更新 `i18n/template.ts`。
- `tests/tst_qviewtests.cpp`：新增 4 个 ShortcutSettingsTests，扩展 View 菜单与 Escape 冲突回归，并保留滚动/缩放回归。
- `tests/shortcut_toggle_acceptance_static.py`：新增快捷键静态合同脚本。

## 3. 静态验证

### 3.1 Python 与源码合同

执行：

```bash
python3 -m py_compile \
  tests/scrollbar_zoom_acceptance_static.py \
  tests/shortcut_toggle_acceptance_static.py
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
python3 tests/shortcut_toggle_acceptance_static.py \
  --repo . --output reports/evidence/shortcut_toggle_static.json
```

结果：两个脚本退出码均为 `0`；滚动脚本 `ST-SB-01` 至 `ST-TEST-02` 全部 PASS；快捷键脚本 `ST-SC-01` 至 `ST-SC-06` 全部 PASS。

另用 Qt Linguist `lupdate src -locations none` 生成当前 source inventory（`ActionManager` 72 项、`ShortcutManager` 45 项等），逐 context 对四个 TS 目录检查完成翻译，四个目录均无缺失项。

### 3.2 Clang analyzer

使用 Homebrew LLVM 的 libc++/resource headers 与 Xcode macOS SDK 运行 `clang-analyzer-core`，检查：

```text
src/qvgraphicsview.cpp
src/actionmanager.cpp
src/mainwindow.cpp
src/shortcutmanager.cpp
```

结果：退出码 `0`，无 analyzer 诊断。最初未显式指定 SDK/C++ include 的探测只产生工具环境头文件错误；补齐 include 后重跑通过，该错误不属于源码缺陷。

### 3.3 构建与格式

执行：

```bash
cmake --build build-current \
  --target Fovelle fovelle_tests Fovelle_lrelease --parallel 2
git diff --check
```

结果：应用、QtTest 和四个 QM catalog 均构建成功；`git diff --check` 通过。Ghostscript staging 的 macOS code-signature invalidation 是既有打包警告，不影响构建退出码。

## 4. 动态验证

### 4.1 快捷键专用套件

```bash
FOVELLE_TEST_SUITE=ShortcutSettingsTests \
QTEST_FUNCTION_TIMEOUT=30000 \
build-current/tests/fovelle_tests -silent
```

结果：`11 passed, 0 failed, 0 skipped, 0 blacklisted`，退出码 `0`。

### 4.2 滚动与缩放专用套件

```bash
FOVELLE_TEST_SUITE=GraphicsViewTests \
QTEST_FUNCTION_TIMEOUT=30000 \
build-current/tests/fovelle_tests -v1
FOVELLE_TEST_SUITE=ScrollHelperTests \
QTEST_FUNCTION_TIMEOUT=30000 \
build-current/tests/fovelle_tests -v1
```

结果：GraphicsViewTests `32 passed, 0 failed`；ScrollHelperTests `7 passed, 0 failed`。右下锚点关键日志为：

```text
before= QPoint(599,407)
after_layout= QPointF(599,407)
after_settling= QPointF(599,407)
```

### 4.3 完整主套件

```bash
QTEST_FUNCTION_TIMEOUT=30000 build-current/tests/fovelle_tests -silent
```

结果：主套件共 `167 passed, 0 failed, 0 skipped, 0 blacklisted`，退出码 `0`；ShortcutSettingsTests 通过专用选择器另行执行并计入 4.1。

## 5. 根因—修复—验证闭环

| 根因 | 修复 | 验证 |
| --- | --- | --- |
| AsNeeded 显示缩小 viewport，旧 resize 路径再次施加半差平移 | pending anchor 分支优先恢复并跳过普通补偿 | AC-ZOOM-01、AC-ZOOM-02 |
| scene rect 与 active item/backing geometry 不一致 | 以 active item geometry guarded 更新 scene rect | AC-SB-01、ST-SB-02 |
| scrollbar/ScrollHelper 允许端点越界 | `qBound` 硬夹紧，删除 overscroll/rebound | AC-SB-01、ScrollHelperTests |
| QSS 运动方向 margin 制造视觉端隙 | 垂直上下、水平左右 margin 置零 | AC-SB-02、ST-SB-01 |
| 旧 Action 的配置与行为分散 | 统一 `togglefitand100` key 与 MainWindow 状态机 | AC-SC-01～03、ST-SC-01～03 |
| 新文案缺少多语言资源 | 两个生产 context 和四个 QM catalog 同步更新 | AC-SC-04、ST-SC-05 |

## 6. 联网溯源

采用“Qt 类文档 → Qt 官方源码 → 项目源码/提交历史 → QtTest 观测”的多跳链路。关键一手资料：

- [QGraphicsView `sceneRect`](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop)：确认 scene rect 是 scrollbar 可导航范围。
- [QAbstractScrollArea](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)：确认 scrollbar 显示会改变 viewport 几何。
- [QAbstractSlider `value`](https://doc.qt.io/qt-6/qabstractslider.html#value-prop)：确认整数 value 的合法范围。
- [Qt 官方 QGraphicsView 源码](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)：追踪 `recalculateContentSize()` 的两轴 `AsNeeded` 重算。
- [QGraphicsView `fitInView()`](https://doc.qt.io/qt-6/qgraphicsview.html#fitInView)：确认 resize 中自动滚动条与 transform 的递归风险。
- [Qt Style Sheet box model](https://doc.qt.io/qt-6/stylesheet-customizing.html#box-model)：确认 margin 会影响 handle 的可绘制端点。

具体症状分流、提交反事实和显式前提见 `reports/root_cause.md`。

## 7. 遗留限制

- 测试使用真实 Cocoa viewport、Qt scrollbar range 和布局事件，但未在 CI 中注入跨应用物理 HID 滑块拖动；物理输入差异由 endpoint geometry 与 ScrollHelper 单元测试覆盖。
- 右下锚点用例刻意避开不可达的真实 maximum，以区分正常整数截断和布局跳变。
- `navresetszoom` 和 `OriginalSize` calculated mode 作为内部兼容设置/鼠标操作仍存在；本需求移除的是 Shortcuts/View Action 面，不是所有内部枚举或遗留配置。
