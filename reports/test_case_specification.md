# EPS/SVG 拖动拖影修复测试用例规格

## 1. 范围、事实与显式前提

测试对象是“已打开 EPS 或 SVG 图片后，在画布内平移图片”的拖动路径，不是文件拖放导入路径。

基于 `reports/root_cause.md` 和 Qt 官方文档，采用以下可复核事实：

1. `QGraphicsView::MinimalViewportUpdate` 会尽量只更新暴露区域；`FullViewportUpdate` 会重绘整个 viewport，并可用于关闭滚动优化：[QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)。
2. Qt 对不透明 widget 的滚动说明是：滚动时可能只收到新暴露的条带；如果该条带没有被当前 frame 完整覆盖，平台 backing store 可能保留上一帧像素：[QWidget](https://doc.qt.io/qt-6.8/qwidget.html)。
3. `QStyleOptionGraphicsItem::exposedRect` 给出 item 需要绘制的区域，因此可以在异步 tile/fallback 绘制前清除精确的 item 暴露区：[QStyleOptionGraphicsItem](https://doc.qt.io/qt-6/qstyleoptiongraphicsitem.html)。
4. macOS 14 移除了系统 PostScript/EPS 转换路径，因此 EPS 的权威内容仍由受限 Ghostscript 转为 PDF 后进入现有 vector document pipeline：[macOS 14 Release Notes](https://developer.apple.com/documentation/macos-release-notes/macos-14-release-notes)。

显式前提：本文的像素回归测试将“上一帧像素”建模为洋红色哨兵；在 tile 尚未就绪的时刻使用透明 fallback。若修复有效，哨兵像素必须为零。系统测试需要 macOS Accessibility 与 Post Event 权限；权限不可用时不得把 system gate 伪报为通过。

## 2. 原子化验收标准

| ID | 原子验收标准 | 验证层级 | 固化代码 |
| --- | --- | --- | --- |
| AC-VECTOR-GHOST-EPS | EPS tile 未就绪时透明 fallback 不得带入上一帧像素 | unit | `GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady` 的 EPS 分支 |
| AC-VECTOR-GHOST-SVG | SVG tile 未就绪时透明 fallback 不得带入上一帧像素 | unit | `GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady` 的 SVG 分支 |
| AC-VECTOR-FULL-FIRST-FRAME | 首个矢量滚动 frame 前切换为 FullViewportUpdate | integration | `GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip` |
| AC-VECTOR-IDLE-RESTORE | 50ms 交互静默后恢复 MinimalViewportUpdate 并完成 refine | integration | `GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip` |
| AC-VECTOR-EPS-SVG-DRAG | EPS/PDF 与 SVG 拖动首帧都完整覆盖 viewport，tile 仍异步有界 | integration | `GraphicsViewTests::testVectorDragFrameBudgetForEPSAndSVG` |
| AC-VECTOR-THEME-BACKGROUND | 主题纯色/checkerboard brush 注入 vector item 后再清除暴露区 | static | `vector_drag_ghosting_pipeline.py::run_static` |
| AC-EPS-AUTHORITATIVE-PATH | EPS 使用权威 PostScript→PDF vector document，而非 placement preview | static/unit | `eps_quality_static.py` 与 `ImageLoaderTests::testEPSPostScriptRender` |
| AC-VECTOR-BOUNDED-CPU | 修复不解除 tile/cache/zoom 安全边界，交互线程 CPU p99 仍满足 120Hz预算 | unit/integration | `GraphicsViewTests::testVectorInteractionPaintCpuBudgetFor120Hz` |
| AC-VECTOR-NATIVE-HID | 真实 bundle 收到 native HID 拖动后记录 full interaction、minimal idle 与 vector render | system | `native_drag_helper.mm` 与 `vector_drag_ghosting_pipeline.py::run_system` |
| AC-PIPELINE-ORDER | gate 严格按 static → unit → integration → system 执行，失败即停止 | audit | `vector_drag_ghosting_pipeline.py::main` |

## 3. 详细测试用例

### TC-AC-VECTOR-GHOST-EPS — EPS 异步 tile 前清除残留像素

- 测试目的：验证 EPS/PDF vector item 在 fallback 或异步 tile 未就绪时，先以配置的背景 brush 清除暴露区。
- 前置条件：已构建 `fovelle_tests`；macOS Cocoa 与 Ghostscript 可用；测试可创建临时目录。
- 输入数据：无背景透明的确定性 120x40 EPS、洋红色哨兵画布、白色 vector background brush。
- 操作步骤：运行 `GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady` 的 EPS 分支；等待 EPS Result；在 tile 发布前调用 `QVGraphicsImageItem::paint`；统计哨兵像素。
- 预期结果：日志为 `format=eps stale_pixels=0 total_pixels=4800`，透明区域为白色而非上一帧。
- 后置条件：异步 tile 请求、PDF document 与临时 EPS 资源释放；用户设置不变。
- 测试层级：unit。
- 测试代码：`tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady`。

### TC-AC-VECTOR-GHOST-SVG — SVG 异步 tile 前清除残留像素

- 测试目的：验证相同清除契约覆盖 Qt SVG renderer，而不是只覆盖 EPS/PDF。
- 前置条件：已构建 `fovelle_tests`；Qt SVG renderer 可用；测试可创建临时目录。
- 输入数据：无背景透明的确定性 120x40 SVG、洋红色哨兵画布、白色 vector background brush。
- 操作步骤：运行同一 Qt test 的 SVG 分支；在 tile 发布前调用 `QVGraphicsImageItem::paint`；统计哨兵像素。
- 预期结果：日志为 `format=svg stale_pixels=0 total_pixels=4800`，透明区域为白色而非上一帧。
- 后置条件：SVG renderer、异步请求与临时文件释放；主题设置不变。
- 测试层级：unit。
- 测试代码：`tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPaintClearsStalePixelsBeforeTileReady`。

### TC-AC-VECTOR-FULL-FIRST-FRAME — 首个滚动 frame 禁止 backing-store scroll reuse

- 测试目的：验证修复发生在 `QGraphicsView::scrollContentsBy` 调用 base implementation 之前。
- 前置条件：可见 Cocoa `QGraphicsView` 已打开 SVG 并完成 64x 缩放；水平滚动条有溢出范围。
- 输入数据：一次 6 logical-pixel 水平滚动条移动、`QPaintEvent::region()` 与 viewport 面积。
- 操作步骤：运行 `testVectorPanRepaintsOnlyExposedStrip`；调用 `setValue` 后立即读取 `viewportUpdateMode`；记录首个 paint 区域。
- 预期结果：模式为 `FullViewportUpdate`；首个 `dirty_ratio >= 0.90`，不再是旧实现的 0.009554 暴露条带。
- 后置条件：测试窗口关闭；timer 与 worker 退出。
- 测试层级：integration。
- 测试代码：`tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip`。

### TC-AC-VECTOR-IDLE-RESTORE — 空闲后恢复轻量更新模式

- 测试目的：验证完整重绘仅覆盖交互 burst，静默后仍使用有界异步终端密度 tile。
- 前置条件：同 TC-AC-VECTOR-FULL-FIRST-FRAME；vector worker 可完成 refine。
- 输入数据：同一滚动操作、`vectorRefineTimer` 的 50ms interval、`vectorRenderCount`。
- 操作步骤：等待 `hasPendingVectorRefinement=false` 且 `vectorRenderCount>0`；读取 `viewportUpdateMode`。
- 预期结果：模式恢复为 `MinimalViewportUpdate`；最终 tile 已绘制，不会无限期保持完整重绘。
- 后置条件：timer、tile cache 与窗口按 RAII 释放。
- 测试层级：integration。
- 测试代码：`tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip`。

### TC-AC-VECTOR-EPS-SVG-DRAG — EPS/SVG 拖动首帧覆盖完整视口

- 测试目的：验证 EPS/PDF 与 SVG 生产路径共用无拖影呈现策略。
- 前置条件：可见 Cocoa 窗口已构建；确定性 EPS 与 SVG 可读；tile worker 可用。
- 输入数据：16x12 EPS、SVG sample、64x zoom、6 logical-pixel 水平滚动。
- 操作步骤：运行 `testVectorDragFrameBudgetForEPSAndSVG`；分别记录 EPS/SVG 首个 paint region、viewport area、mode 与 render count。
- 预期结果：EPS、SVG 均为 `update_mode=full` 且 `dirty_ratio >= 0.90`；没有 renderer error，tile 仍异步有界。
- 后置条件：两个文档、worker 与窗口关闭；没有残留进程。
- 测试层级：integration。
- 测试代码：`tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorDragFrameBudgetForEPSAndSVG`。

### TC-AC-VECTOR-THEME-BACKGROUND — 背景 brush 与主题一致

- 测试目的：避免修复硬编码颜色后在浅色、深色或 checkerboard 主题下产生新的闪烁。
- 前置条件：`QVGraphicsView::settingsUpdated`、theme 与 checkerboard 设置源码可读。
- 输入数据：`viewportBackgroundBrush`、`checkerboardBackgroundBrush`、`setVectorBackgroundBrush` 调用链。
- 操作步骤：运行静态 gate；检查生成 checkerboard brush 后是否将纯色/棋盘格 brush 注入 `loadedPixmapItem`；检查 item 使用 `CompositionMode_Source`。
- 预期结果：两条主题路径均到达 vector item；清除是覆盖式绘制而不是 SourceOver 半透明叠加。
- 后置条件：静态检查不启动 GUI，不写用户配置。
- 测试层级：static。
- 测试代码：`tests/vector_drag_ghosting_pipeline.py::run_static`。

### TC-AC-EPS-AUTHORITATIVE-PATH — EPS 权威内容源不被 preview 替换

- 测试目的：排除 EPS placement preview 与正文不一致对拖动结果的干扰。
- 前置条件：Ghostscript 与 macOS PDF bridge 可用；native bridge、loader、QVImageCore 源码可读。
- 输入数据：无 preview deterministic EPS 或用户 EPS sample；`-dSAFER`、`-dEPSCrop`、`pdfwrite` contract。
- 操作步骤：先运行 `eps_quality_static.py`；再运行 `ImageLoaderTests::testEPSPostScriptRender` 与 `testImageLoaderLoadsEPS`。
- 预期结果：`Result.vectorImage.format=Pdf`，PDF bytes 与 BoundingBox 逻辑尺寸有效；权威内容不来自嵌入 preview。
- 后置条件：Ghostscript 子进程、PDF document、loader 与临时文件释放。
- 测试层级：static/unit。
- 测试代码：`tests/eps_quality_static.py`；`tests/tst_qviewtests.cpp::ImageLoaderTests::testEPSPostScriptRender`。

### TC-AC-VECTOR-BOUNDED-CPU — 完整重绘不解除性能/安全边界

- 测试目的：确认正确性修复没有将矢量转换搬回 GUI 线程，也没有解除 tile/cache/zoom 上限。
- 前置条件：Cocoa display 与 EPS/SVG sample 可用；`fovelle_tests` 已构建。
- 输入数据：EPS/SVG 各 120 次 zoom/pan 样本；8.333ms p99 frame budget；64M tile pixel、2-entry/96MiB cache、64.0 zoom 上限。
- 操作步骤：运行 `testVectorInteractionPaintCpuBudgetFor120Hz`；读取 average、p99、max、capacity；并由静态 gate 检查上限字面量。
- 预期结果：各格式/交互 p99≤8.333ms；worker 仍为单线程异步，tile 上限与 6400% contract 不变。
- 后置条件：窗口、worker 与采样资源释放；原始测量进入 evidence。
- 测试层级：unit/integration。
- 测试代码：`tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorInteractionPaintCpuBudgetFor120Hz`。

### TC-AC-VECTOR-NATIVE-HID — 真实 app 的系统级拖动

- 测试目的：把 QtTest 的确定性回归提升到真实 bundle、WindowServer 输入队列和可见窗口。
- 前置条件：`Fovelle.app` 与 `fovelle_native_drag_helper` 已构建；Accessibility/Post Event 权限可用；真实 Cocoa 桌面可见。
- 输入数据：运行时生成的 1600x1200 EPS/SVG；helper 的 32-step CoreGraphics HID trajectory。
- 操作步骤：分别启动真实 bundle；helper 发送 fullscreen、scroll zoom、vertical native drag、退出 fullscreen；读取 `/tmp` helper log 并复制到 evidence。
- 预期结果：`NATIVE_DRAG_RESULT passed=true`；日志包含 `active=true update_mode=full`、`active=false update_mode=minimal`、完整 viewport paint 与对应 vector render。
- 后置条件：app 由 helper 终止；临时 fixture 删除；日志保留为可复核证据。
- 测试层级：system。
- 测试代码：`tests/native_drag_helper.mm`；`tests/vector_drag_ghosting_pipeline.py::run_system`。

### TC-AC-PIPELINE-ORDER — 四级 gate 顺序与失败语义

- 测试目的：保证“通过”只在四级证据按顺序完成后成立。
- 前置条件：源码、构建目录和四级测试入口可读。
- 输入数据：四个 stage command、return code、stdout/stderr tail、evidence JSON。
- 操作步骤：运行 `python3 tests/vector_drag_ghosting_pipeline.py`；检查 `stage_order`、每阶段 `passed`、命令和时间戳。
- 预期结果：顺序恰为 `[static, unit, integration, system]`；全部通过才返回 0；任一失败即停止后续 stage 并返回非零。
- 后置条件：evidence 可按路径和 hash 复核；不修改用户数据。
- 测试层级：audit。
- 测试代码：`tests/vector_drag_ghosting_pipeline.py::main`。

## 4. 修改前/修改后数据对比

数据由同一 QtTest 像素测试和同一 viewport geometry 采样得到；“前”在修复代码加入前执行，“后”在当前修复代码构建后执行。

| 指标 | 修改前 | 修改后 | 结论 |
| --- | ---: | ---: | --- |
| EPS stale 哨兵像素 | 4800/4800（100%） | 0/4800（0%） | 透明 fallback 不再露出上一帧 |
| SVG stale 哨兵像素 | 4800/4800（100%） | 0/4800（0%） | SVG 同样满足清除契约 |
| SVG 首个平移 dirty ratio | 0.009554 | 1.000000 | 首帧从暴露条带改为完整 viewport，消除 scroll reuse 风险 |
| EPS 首个拖动 dirty ratio | 0.009554 | 1.000000 | EPS/PDF 首帧完整覆盖 |
| SVG 首个拖动 dirty ratio | 0.009554 | 1.000000 | SVG 首帧完整覆盖 |
| 修改后 idle update mode | MinimalViewportUpdate | MinimalViewportUpdate | 50ms 静默后恢复轻量模式 |

`dirty ratio` 增大是本修复的有意结果，不是画面变差：它表示拖动首帧主动放弃 QGraphicsView 的 backing-store 条带复用；像素级 stale ratio 同时从 100% 降为 0%。完整原始输出位于 `reports/evidence/vector_drag_ghosting/`（运行 pipeline 后生成）。

## 5. 执行命令

```bash
python3 tests/vector_drag_ghosting_pipeline.py \
  --repo /Users/inostarlin/code/Fovelle \
  --build-dir /Users/inostarlin/code/Fovelle/build
```

pipeline 固定执行 `static`、`unit`、`integration`、`system`，并将四级结果与原始日志写入 `reports/evidence/vector_drag_ghosting/`。
