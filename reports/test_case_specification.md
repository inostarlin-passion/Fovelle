# Fovelle 图片缩放与滚动条测试用例说明

本文将本次任务的 5 条原子验收标准映射为可执行测试。每个新增用例都明确包含：测试目的、前置条件、输入数据、操作步骤、预期结果、后置条件。文档后部保留既有滚动条、缩放和快捷键回归用例，作为交叉验证。

测试层分为：源码静态合同、QtTest 动态回归、CTest 集成清单。Cocoa 测试使用确定性临时 PNG，不依赖用户提供的 `/Volumes/CRYSTAL` 文件；手工复现仍以 `reports/root_cause.md` 中的 `1.avif` 为基准。

## 本次任务原子验收矩阵

| 原子标准 | 结构化测试用例 | 固化测试代码 | 静态/动态阶段 | 瞬态/稳态覆盖 |
| --- | --- | --- | --- | --- |
| AC-ZOOM-TRANSITION-200MS | `TC-ZOOM-ALL-ENTRY-POINTS` | `GraphicsViewTests::testZoomTransitionCoversWheelKeyboardAndMenus` | static + dynamic | 80ms in-flight + finished |
| AC-SORT-FIXED-DEFAULT | `TC-SORT-CONFIG-IGNORED-CONTEXT-REMOVED` | `WindowBehaviorTests::testSortConfigurationIsIgnoredAndContextMenuHasNoSortMenu` | static + dynamic | hostile config + fixed state |
| AC-HELP-WEBSITE | `TC-HELP-WEBSITE-URL-CONTEXT-TRANSLATION` | `WindowBehaviorTests::testWebsiteHelpActionAndContextMenuContract` | static + dynamic | dispatch + menu steady state |
| AC-ZOOM-ANCHOR-PROJECTION | `TC-ZOOM-ANCHOR-INSIDE-OUTSIDE` | `GraphicsViewTests::testZoomAnchorProjectsInsideAndOutsideImage` | static + dynamic | layout/animation + final anchor |
| AC-SCROLLBAR-VERTICAL-STEADY | `TC-ZOOM-VERTICAL-SCROLLBAR-TRANSIENT-STEADY` | `GraphicsViewTests::testZoomTransitionLeavesVerticalScrollbarStable` | static + dynamic + CTest | animation end + settle + steady |

下列五个用例严格使用六个字段；每个用例都同时记录静态合同和动态执行入口，时间相关用例分别采样瞬态与稳态。

## TC-ZOOM-ALL-ENTRY-POINTS

### 测试目的

验证鼠标滚轮、键盘 `Toggle Fit and 100%`、标题栏 `View → Zoom In` 和右键 `View → Zoom In` 共享同一个 200ms 过渡实现，而不是只有某一条菜单路径动画化。

### 前置条件

- Cocoa `QApplication`、`MainWindow` 和 `QVGraphicsView` 已初始化并可见。
- 已加载 1200×900 临时 PNG；计算缩放模式为 `OriginalSize`，平滑缩放关闭。
- 标题栏和右键菜单已经 materialize，翻译资源和 QtTest event loop 可用。

### 输入数据

- 一个鼠标 wheel step。
- canonical `togglefitand100` action。
- 标题栏和右键 View menu 中的 `zoomin` clone。
- `QPropertyAnimation` 对象名 `zoomTransitionAnimation`、duration 200ms、80ms 中间采样点。

### 操作步骤

1. 读取动画对象的 duration 和 easing curve，并确认两个 View menu 都有 `Zoom In` 和 `Toggle Fit and 100%`。
2. 发送一个合成鼠标 wheel 事件，立即确认动画运行，80ms 时读取中间 displayed zoom。
3. 等待动画结束，比较 displayed zoom 与 logical target。
4. 恢复 1.0x 后，通过标题栏 View clone、右键 View clone、canonical keyboard action 依次重复步骤 2–3。

### 预期结果

每个入口都启动 duration 恰为 200ms 的动画；80ms 仍处于过渡且 displayed zoom 不等于终值；结束时 displayed zoom 与 logical target 相等，且没有额外的布局回弹。

### 后置条件

关闭窗口，释放临时 PNG、菜单和动画对象，恢复应用退出策略与 scoped settings。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomTransitionCoversWheelKeyboardAndMenus`；静态合同为 `tests/scrollbar_zoom_acceptance_static.py::ST-ZOOM-TRANSITION-01`。

## TC-SORT-CONFIG-IGNORED-CONTEXT-REMOVED

### 测试目的

验证右键菜单删除 `Sort Files By`，且历史 `sortmode`/`sortdescending` 配置既不注册、不读取，也不能通过旧 setter 改变文件顺序。

### 前置条件

- `SettingsManager`、`QVFileEnumerator`、`ActionManager` 和 `MainWindow` 已初始化。
- 可 materialize 右键菜单；不需要打开真实文件即可检查 action tree 和默认策略。

### 输入数据

- hostile legacy `sortmode=Random` 与 `sortdescending=true`。
- 右键菜单全部嵌套 action 的 text、canonical key。
- `Qv::SortMode::Name` 和 ascending (`false`) 期望值。

### 操作步骤

1. 通过 scoped `QSettings` 写入两个历史 key，并让 `SettingsManager` reload。
2. 检查 settings library 不含两个 key，检查 enumerator 为 Name/Ascending。
3. 调用旧的 `setSortMode(Random)` 和 `setSortDescending(true)`，再次读取策略。
4. 深度遍历右键菜单 action，检查 `Sort Files By`、`sortmenu`、`sortmode*`、`sortdirection*` 均不存在。

### 预期结果

历史值不会进入产品设置或排序状态；setter 调用被固定策略忽略；右键菜单不显示 Sort Files By 分支或其子项，文件顺序保持名称升序。

### 后置条件

关闭测试窗口，恢复临时 QSettings，释放菜单和 enumerator，不改变用户真实配置。

### 固化代码

`tests/tst_qviewtests.cpp::WindowBehaviorTests::testSortConfigurationIsIgnoredAndContextMenuHasNoSortMenu`；静态合同为 `tests/scrollbar_zoom_acceptance_static.py::ST-SORT-CONTRACT-01`。

## TC-HELP-WEBSITE-URL-CONTEXT-TRANSLATION

### 测试目的

验证标题栏和右键 Help 菜单都插入 Website，位置严格位于 Project Homepage 与 Check for Updates 之间，点击 canonical action 打开指定 URL，且简体中文等目录同步翻译。

### 前置条件

- `ActionManager` 已创建 native menu；`MainWindow` 的右键菜单可 materialize。
- translation-enabled build 已生成 `qview_es.qm`、`qview_ja.qm`、`qview_zh_Hans.qm`、`qview_zh_Hant.qm`。
- URL handler probe 可拦截 `https` scheme。

### 输入数据

- canonical key `website`。
- URL `https://fovelle-viewer.onrender.com/`。
- 目标译文：`Sitio web`、`公式サイト`、`官方网站`、`官方網站`。

### 操作步骤

1. 读取标题栏 Help menu 和右键 Help submenu，比较三项的相对顺序。
2. 安装 `QDesktopServices` URL handler，触发 canonical Website action，读取收到的 URL。
3. 分别加载四个 QM catalog，以 `ActionManager/Website` 查询译文。

### 预期结果

两个菜单结构一致，Website 紧跟 Project Homepage 且位于 Check for Updates 之前；handler 收到精确 URL；简体中文显示“官方网站”，其他三个目录返回非空且与期望一致的译文。

### 后置条件

注销 URL handler，关闭窗口，卸载临时 translator，释放菜单和 probe，不发起不可控浏览器副作用。

### 固化代码

`tests/tst_qviewtests.cpp::WindowBehaviorTests::testWebsiteHelpActionAndContextMenuContract`；静态合同为 `tests/scrollbar_zoom_acceptance_static.py::ST-MENU-CONTRACT-01`。

## TC-ZOOM-ANCHOR-INSIDE-OUTSIDE

### 测试目的

验证图片内的请求点严格保持为锚点；图片左侧、右上、左下等外部请求点逐轴投影到最近图片边界，不能回退到图片中心或继续使用空白点。

### 前置条件

- 纯函数投影 helper 可调用。
- Cocoa 窗口已显示 400×300 小图片，1.0x 时四周存在可观测空白且两轴 scrollbar 隐藏。
- 2.0x 缩放动画和 scene anchor restore 可运行。

### 输入数据

- 抽象 image rect `(100,100,300,200)`。
- inside `(250,180)`、left `(20,160)`、upper-right `(460,40)`、lower-left `(20,360)`。
- 运行时左侧空白点及其投影点、投影点对应 scene coordinate。

### 操作步骤

1. 对四组纯函数输入检查 x/y 独立 clamp 结果。
2. 在运行时读取小图 viewport rect，选取图片左侧空白点并记录其投影 scene 点。
3. 以空白点触发 2.0x 缩放，等待 200ms 动画结束和布局事件处理。
4. 比较该 scene 点最终映射位置与投影 viewport 点。

### 预期结果

inside 点不变；外部点按最近边界逐轴投影，角点同时投影两轴；动画和最终稳态都不会把锚点改成图片中心或空白区原始坐标。

### 后置条件

关闭窗口，清理临时图片、pending anchor 和 settings，恢复退出策略。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomAnchorProjectsInsideAndOutsideImage`；静态合同为 `tests/scrollbar_zoom_acceptance_static.py::ST-ZOOM-ANCHOR-PROJECTION-01`。

## TC-ZOOM-VERTICAL-SCROLLBAR-TRANSIENT-STEADY

### 测试目的

验证缩放动画使用当前显示帧计算 scene range，修复垂直滚动条跳变；同时以水平滚动条作为对照，覆盖动画结束、anchor settle 和 steady constraint 三个时间状态。

### 前置条件

- Cocoa 640×480 窗口已加载 2048×1536 图片；两轴均有非零 scrollbar range。
- `OriginalSize`、平滑缩放关闭；两轴先置于内部 value，避免端点约束掩盖跳变。

### 输入数据

- 中心锚定的 1.5x zoom。
- 动画结束采样点 `200ms + 30ms`。
- anchor settle 采样点再加 `150ms + 80ms`，以及最后 300ms steady 窗口。
- 两轴 value 差异容差 1；displayed/logical zoom 等价判定。

### 操作步骤

1. 记录两轴初始内部 value。
2. 触发 1.5x 中心缩放，确认动画正在运行。
3. 在动画结束后记录两轴 value。
4. 等待 anchor settle，再次记录两轴 value。
5. 再等待 300ms，记录 steady value，并比较 displayed zoom 与 logical target。

### 预期结果

动画结束到 settle 的垂直 value 差不超过 1；settle 到稳态也不超过 1。水平对照轴同样稳定，最终 displayed zoom 等于 logical target；不出现旧实现的垂直迟滞跳变。

### 后置条件

关闭窗口，释放滚动条、临时图片和设置，恢复应用退出策略。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomTransitionLeavesVerticalScrollbarStable`；静态合同为 `tests/scrollbar_zoom_acceptance_static.py::ST-TEST-01`，并由默认 CTest 交叉执行。

## 本次任务静态/动态执行协议

### 测试目的

确认源码合同、结构化文档、QtTest 入口和 CTest 清单形成闭环，并让每个原子标准都有可复核证据。

### 前置条件

仓库根目录可读，Python 3、CMake、Qt 6 Cocoa 构建和 translation build 可用。

### 输入数据

`src/qvgraphicsview.*`、`src/actionmanager.*`、`src/mainwindow.*`、`src/qvfileenumerator.*`、`src/settingsmanager.cpp`、`i18n/*.ts`、`tests/tst_qviewtests.cpp`、本文件和 `reports/root_cause.md`。

### 操作步骤

```bash
python3 -m py_compile tests/scrollbar_zoom_acceptance_static.py
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

### 预期结果

静态脚本、编译和默认 CTest 均返回 0；每个原子标准的动态用例无 failed/skipped/blacklisted，并保留瞬态与稳态采样结果。

### 后置条件

保留机器可读 JSON 证据和完成报告；不保留测试窗口、URL handler、临时配置或外部浏览器状态。

### 固化代码

`tests/scrollbar_zoom_acceptance_static.py`、`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`。

## TC-SB-IMAGE-EDGES

### 测试目的

验证水平、垂直滚动条到达最小值和最大值时，图片真实左、上、右、下边缘都能到达 viewport 对应边缘。

### 前置条件

- `QVApplication` 与 Cocoa `MainWindow` 已初始化并可见。
- 窗口使用 `WindowNoState`、`windowresizemode=Never`、`calculatedzoommode=OriginalSize`。
- 图片加载完成且两轴均有非零 scrollbar range。

### 输入数据

- `tests/tst_qviewtests.cpp` 生成的 1600×1600 PNG。
- 两个 scrollbar 的 `minimum()`、`maximum()`。
- `scene()->itemsBoundingRect()`、viewport rectangle 和 `mapFromScene()` 的结果。

### 操作步骤

1. 打开临时 PNG，等待 `getIsPixmapLoaded()`。
2. 设为 1.0x，等待两个 AsNeeded scrollbar 可见。
3. 将两轴设为 minimum，记录映射图片矩形。
4. 将两轴设为 maximum，记录映射图片矩形。
5. 比较图片四边与 viewport 四边。

### 预期结果

minimum 时左/上边缘、maximum 时右/下边缘均在 2px 内贴合 viewport；端点没有图片外背景条。

### 后置条件

关闭窗口，临时 PNG 和测试对象释放，测试设置与退出策略恢复。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarsReachImageEdges`。

## TC-SB-NATIVE-EXTENT

### 测试目的

验证主题样式不会把 scrollbar 厚度固定成与 `QGraphicsView` 不同的值，消除 12px/15px 分裂造成的 3px range surplus。

### 前置条件

- Cocoa `MainWindow` 可见，Qt style 可查询 `QStyle::PM_ScrollBarExtent`。
- 1200×1085 图片已加载，两轴 scrollbar 可见。

### 输入数据

- `view->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, view)`。
- 纵向 scrollbar `sizeHint().width()` 与横向 scrollbar `sizeHint().height()`。
- 2.0x、1.6x 缩放级别和 viewport 右下目标点。

### 操作步骤

1. 在 1.0x 下等待两轴 scrollbar 出现。
2. 读取平台 extent，比较纵条宽度和横条高度。
3. 放大到 2.0x，将两轴 value 置于 maximum。
4. 在右下目标点缩小到 1.6x，立即采样 value 与图片映射矩形。
5. 等待 700ms（覆盖 500ms bounds constraint），再次采样。

### 预期结果

两个 scrollbar 厚度都等于 `PM_ScrollBarExtent`；缩小前后图片右/下边缘均贴合 viewport，立即与延迟采样的 value、映射矩形完全相同。

### 后置条件

关闭窗口，释放临时图片、scrollbar 和测试设置。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarGeometryMatchesViewMetricAndDoesNotRebound`。

## TC-SB-VISUAL-ENDPOINT

### 测试目的

验证 handle 的运动方向没有由 QSS margin 造成的视觉端点内缩。

### 前置条件

- `QVGraphicsView::scrollBarStyleSheet()` 可调用并返回当前主题样式。
- handle 规则中保留非运动方向的 1px 侧向 margin。

### 输入数据

- 垂直规则 `min-height: 24px; margin: 0px 1px`。
- 水平规则 `min-width: 24px; margin: 1px 0px`。
- 样式表中不得出现 `margin: 2px 1px` 或 `margin: 1px 2px`。

### 操作步骤

1. 读取 Light theme 样式表。
2. 检查垂直 handle 的上下 margin 和水平 handle 的左右 margin。
3. 检查两轴没有旧的运动方向内缩规则。

### 预期结果

运动方向 margin 为 0，侧向 margin 为 1px；样式合同通过且不改变滚动范围。

### 后置条件

不写入持久化设置，不保留临时 UI 对象。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarHandleTrackEndpoints` 与 `tests/scrollbar_zoom_acceptance_static.py::ST-SB-01`。

## TC-ZOOM-CENTER-THRESHOLD

### 测试目的

验证以可用 viewport 中心缩放并跨过 `ScrollBarAsNeeded` 阈值时，中心 scene 点不漂移。

### 前置条件

- 可见 640×480 左右的 Cocoa 窗口。
- 初始图片约为当前可用 viewport 的 90%，初始无 scrollbar。
- 缩放目标为 `Qv::CalculateViewportCenterPos`。

### 输入数据

- 确定性阈值 PNG。
- 缩放前中心 scene 坐标及归一化 image coordinate。
- 一个 1.25x 缩放步长和 0.005 的归一化坐标容差。

### 操作步骤

1. 打开图片并确认两轴 scrollbar 隐藏。
2. 记录可用 viewport 中心映射的归一化图片坐标。
3. 执行一次 `zoomIn()`，等待两轴 scrollbar 出现。
4. 在 layout 完成后和 150ms 延迟阶段分别读取中心坐标。

### 预期结果

两次采样与缩放前中心坐标的距离均不超过 0.005；不会执行错误的 resize 半差补偿。

### 后置条件

关闭窗口，pending anchor、临时图片和设置恢复。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomAcrossScrollbarThresholdKeepsViewportCenterStable`。

## TC-ZOOM-BOTTOM-RIGHT

### 测试目的

验证右下显式 viewport 像素点缩放时，水平 scrollbar 首次出现及随后 scene/layout 更新不会移动同一 scene anchor。

### 前置条件

- 640×480 Cocoa 窗口已加载 620×420 图片，初始图片适配。
- 显式目标位于 usable viewport 右下区域但未要求不可达的真实 maximum。
- `transformationAnchor=NoAnchor`，由项目事务保存 anchor。

### 输入数据

- 右侧内缩 40px、底部内缩 100px 的目标点。
- `zoomAbsolute(1.25, target)`。
- 2px anchor 容差和 1px 阶段间位移容差。

### 操作步骤

1. 记录目标点下的 scene position。
2. 以目标点执行 1.25x 缩放。
3. 等待横/纵 scrollbar 出现，立即记录 scene position 的 viewport 映射。
4. 等待 150ms，再次记录映射。

### 预期结果

立即和延迟映射都距目标不超过 2px，两个阶段间不超过 1px；图像和 scrollbar 不发生第二次可见跳位。

### 后置条件

关闭窗口，pending anchor generation 与临时资源清理，测试设置恢复。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar`。

## TC-ZOOM-MANUAL-PAN

### 测试目的

验证缩放事务留下的延迟 viewport anchor 不会覆盖用户随后对垂直滚动条做出的最大端点选择。

### 前置条件

- Cocoa `MainWindow` 已显示，窗口约为 640×480，`windowresizemode=Never` 且 `calculatedzoommode=OriginalSize`。
- 2048×1536 临时 PNG 已加载；2.0x 缩放后垂直 scrollbar 存在非零范围。
- 生产代码的延迟 zoom anchor 回调窗口为 150ms。

### 输入数据

- `view->zoomAbsolute(2.0, Qv::CalculateViewportCenterPos)`。
- 垂直 scrollbar 的当前 `maximum()`。
- `QAbstractSlider::SliderMove` 语义动作，以及 `setSliderPosition(maximum())` 的目标值。
- 250ms 等待窗口、当前 maximum ±1 的端点容差和图像底边 ±2px 的几何容差。

### 操作步骤

1. 打开临时 2048×1536 PNG 并等待加载完成。
2. 执行 2.0x 中心缩放，等待垂直 scrollbar 的范围可用。
3. 在延迟回调尚未处理前设置垂直 scrollbar 的 `sliderPosition` 为 `maximum()`，再触发 `QAbstractSlider::SliderMove`，模拟真实 handle 拖动产生的语义输入事件。
4. 等待 250ms 并处理事件，覆盖 150ms 延迟 anchor 回调。
5. 读取当前 scrollbar maximum/value，并把图像底边中点映射到 viewport。

### 预期结果

延迟回调不会把 value 重新定位到旧缩放中心；等待结束后 value 距当前 maximum 不超过 1，图像底边距 viewport 底边不超过 2px。

### 后置条件

关闭窗口，释放临时 PNG 和测试对象，确认测试未遗留 Cocoa 原生 mouse grab，并恢复应用退出策略及 scoped 测试设置。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testManualScrollCancelsPendingZoomAnchor`；静态合同为 `tests/scrollbar_zoom_acceptance_static.py::ST-ZOOM-03`。

## TC-ZOOM-ENDPOINT

### 测试目的

直接回归“滚动条到最大端点后缩小，右侧/下方先露白再位移”的缺陷。

### 前置条件

- 640×480 Cocoa 窗口使用手动缩放和关闭平滑缩放。
- 1200×1085 图片已加载，两轴有足够 overflow。
- 延迟 bounds constraint 的生产计时为 500ms。

### 输入数据

- 2.0x 放大级别、1.6x 缩小级别。
- 两轴 scrollbar 的 maximum。
- viewport 右下 4px 处的显式缩放目标。

### 操作步骤

1. 将两轴滚动条分别设置为 maximum，确认右/下边缘贴合。
2. 在右下目标点执行 2.0x → 1.6x 缩小。
3. 在函数返回后立即记录两轴 value、图片矩形和 viewport 矩形。
4. 等待 700ms 并处理事件，再次记录同样数据。

### 预期结果

立即阶段不出现右侧或下方空白；两轴 value 仍在 maximum（允许 1 的整数取整误差）；700ms 后 value、图片矩形和 viewport 矩形均不变，图片四边仍贴合。

### 后置条件

关闭窗口，释放临时图片和测试资源，恢复应用退出策略。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarGeometryMatchesViewMetricAndDoesNotRebound`。

## TC-CI-TEST-GATE

### 测试目的

验证 GitHub Actions 默认 CTest 门禁只包含可重复的产品测试，不被外部桌面权限或 fixture 影响。

### 前置条件

- 已用 `BUILD_TESTS=ON` 配置 CMake。
- CI workflow 的 Cocoa、Qt、CTest 和 90 秒超时配置可读。
- 不假定 `/Volumes/CRYSTAL` 或 Accessibility/Post Event 权限存在。

### 输入数据

- `tests/CMakeLists.txt` 的注册项。
- `.github/workflows/test.yml` 与 `.github/workflows/build.yml` 的 build/CTest 命令。
- 默认 CMake 缓存值 `FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION=OFF`。

### 操作步骤

1. 运行 `ctest --test-dir build -N` 读取默认清单。
2. 检查清单包含 `FovelleTests` 和 `FovelleShortcutSettingsTests`。
3. 检查默认清单不包含 `FovelleNativeDragReproduction`。
4. 检查 native driver 仅位于显式 `if(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION)` 分支。

### 预期结果

默认清单只含两个产品套件；workflow 保留构建和产品 CTest，外部权限专项测试仍可通过显式选项启用。

### 后置条件

不启动 native driver，不请求系统权限，不修改用户数据。

### 固化代码

`tests/CMakeLists.txt`、`tests/ci_quality_pipeline.py::static_test_registration_contract` 与 `run_integration::ctest_list`。

## TC-STATIC-CONTRACT

### 测试目的

通过静态分析确认实现合同、测试入口和本规格六个必填字段完整存在。

### 前置条件

- Python 3 可执行，仓库根目录可读。
- 三份指定 Markdown 已写入 `reports/`。

### 输入数据

- `src/qvgraphicsview.cpp/.h`、`src/scrollhelper.cpp`。
- `tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`。
- `tests/scrollbar_zoom_acceptance_static.py` 与 `tests/shortcut_toggle_acceptance_static.py`。
- `reports/test_case_specification.md`。

### 操作步骤

执行：

```bash
python3 -m py_compile tests/scrollbar_zoom_acceptance_static.py \
  tests/shortcut_toggle_acceptance_static.py tests/ci_quality_pipeline.py
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
python3 tests/shortcut_toggle_acceptance_static.py \
  --repo . --output reports/evidence/shortcut_toggle_static.json
```

检查 JSON 的 `passed` 以及每个静态检查项。

### 预期结果

脚本退出码均为 0；滚动条厚度、scene rect、pending anchor、端点回归、CTest opt-in 和六字段检查全部通过。

### 后置条件

保留 `reports/evidence/*.json` 作为机器可读证据；不改变产品设置或运行中的窗口状态。

### 固化代码

`tests/scrollbar_zoom_acceptance_static.py`、`tests/shortcut_toggle_acceptance_static.py` 与本文件。

## TC-SC-ACTION-SURFACE

### 测试目的

保留仓库已有的快捷键回归合同，确认本次滚动条修复没有改变 Shortcuts 与 View 菜单的 Action 面。

### 前置条件

- `ShortcutManager`、`ActionManager` 和 Shortcuts 页面已初始化。

### 输入数据

- `togglefitand100` 新 Action。
- 旧 `originalsize`、`zoomtofit`、`navresetszoom` Action key。

### 操作步骤

1. 遍历快捷键 inventory。
2. 检查 canonical action、View 菜单 clone 和 Shortcuts 表格中的 key。
3. 搜索旧 Action 是否仍被注册。

### 预期结果

新 Action 存在，旧三个可配置 Action 不存在，菜单与设置页使用相同 key。

### 后置条件

不修改持久化快捷键，关闭测试 UI。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100IsTheOnlyFitShortcutAction`。

## TC-SC-DEFAULT-Z

### 测试目的

确认既有 `Toggle Fit and 100%` Action 的默认快捷键仍为未修饰的 `Z`。

### 前置条件

`ShortcutManager` 已初始化且允许 scoped shortcut setting。

### 输入数据

`QKeySequence(Qt::Key_Z)` 及 `shortcuts/togglefitand100` 的临时设置。

### 操作步骤

1. 应用 scoped Z 设置并刷新 action map。
2. 比较 row 默认值、有效值和 canonical QAction shortcuts。

### 预期结果

三处均恰好只有一个未修饰 `Z`。

### 后置条件

恢复原快捷键设置并刷新 manager。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100DefaultsToZ`。

## TC-SC-TOGGLE-BEHAVIOR

### 测试目的

确认既有 Z 状态机仍能在 fit 与 100% 间切换。

### 前置条件

图片窗口已加载，窗口 resize mode 为 Never，Action 可 dispatch。

### 输入数据

手动 200% zoom、canonical `togglefitand100` QAction 和三次 dispatch。

### 操作步骤

1. 从手动 zoom dispatch 一次。
2. 从 fit dispatch 第二次。
3. 再 dispatch 第三次并读取 calculated mode。

### 预期结果

状态依次为 fit、精确 100%、fit；遗留 `originalsizeastoggle` 不改变新 Action 语义。

### 后置条件

关闭窗口并恢复原设置。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100ChangesBetweenFitAnd100Percent`。

## TC-SC-TRANSLATIONS

### 测试目的

确认既有新 Action 的四种翻译仍完整，避免滚动条代码变更影响资源构建。

### 前置条件

四个 QM catalog 已生成，两个生产 translation context 可加载。

### 输入数据

简体中文、繁体中文、西班牙语和日语的 `Toggle Fit and 100%` source translation。

### 操作步骤

1. 逐个安装语言 translator。
2. 在 `ActionManager`、`ShortcutManager` context 中翻译 source。
3. 比较完整字符串后移除 translator。

### 预期结果

四种语言、两个 context 的翻译均为完成状态，标点、斜杠、空格和百分号精确一致。

### 后置条件

移除临时 translator，不改变当前语言设置。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100Translations`。
