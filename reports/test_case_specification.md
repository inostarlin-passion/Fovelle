# Fovelle 图片视图与快捷键测试用例说明

## 1. 规则与环境

本文件对应 `reports/technical_design_document.md` 的 8 条原子验收标准。每个原子标准至少有一个动态 QtTest；源码合同另有一个静态测试用例。每个用例固定记录：测试目的、前置条件、输入数据、操作步骤、预期结果、后置条件。

动态测试在 macOS Cocoa、Qt 6.11.1 构建的 `build-current/tests/fovelle_tests` 上执行。滚动条 value 为整数，且 Cocoa viewport 可能有 titlebar 安全区，因此几何断言允许 1–2px 取整误差；这不允许可见的跳位、越界或延迟二次移动。

## TC-SB-IMAGE-EDGES

### 测试目的

验证水平、垂直滚动条到达最小值和最大值时，图片真实左/上/右/下边缘均可到达，消除滑块到端点但 viewport 仍露背景的情况。

### 前置条件

- `QVApplication` 已初始化，运行在 Cocoa 平台。
- 使用可见的 `MainWindow`，窗口固定为 `WindowNoState`，避免持久化全屏/最大化状态改变前置 viewport。
- 设置使用可产生两轴溢出的原始尺寸/手动缩放组合。

### 输入数据

- 确定性大 PNG（当前测试使用 1600×1000 或同等可溢出尺寸）。
- `horizontalScrollBar()`、`verticalScrollBar()` 的 `minimum()` 与 `maximum()`。
- `scene()` 图片矩形、`getUsableViewportRect()` 和 `mapFromScene()`。

### 操作步骤

1. 打开 PNG，等待 `getIsPixmapLoaded()`。
2. 设置足够大的缩放级别，确认两轴存在非零 range。
3. 分别将水平和垂直 scrollbar 设置为 minimum，记录图片映射矩形。
4. 分别将两轴设置为 maximum，再记录图片映射矩形。
5. 比较图片四边与 usable viewport 对应四边。

### 预期结果

最小值时左/上图片边缘与 usable viewport 左/上边缘重合，最大值时右/下图片边缘与 usable viewport 右/下边缘重合，误差不超过 2px；不会在端点露出细长的图片外背景。

### 后置条件

关闭测试窗口，释放临时 PNG，恢复测试前的窗口状态、缩放设置和退出策略。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarsReachImageEdges`。

## TC-SB-VISUAL-ENDPOINT

### 测试目的

验证 scrollbar handle 的运动方向没有由 QSS margin 造成的视觉端点内缩，同时保留非运动方向的 1px 侧向间距。

### 前置条件

- `QVGraphicsView` 已构造并应用当前主题样式表。
- 水平和垂直 scrollbar 均可访问，且至少有可滚动 range。

### 输入数据

- `QVGraphicsView::scrollBarStyleSheet()` 返回的完整样式表。
- 水平/垂直 handle 的 `min-width`/`min-height` 与 margin 规则。
- 动态 handle track endpoint 采样。

### 操作步骤

1. 读取 view 主题样式表，定位两个 handle 规则。
2. 将垂直 handle 置于 track 上端和下端，记录 handle 几何。
3. 将水平 handle 置于 track 左端和右端，记录 handle 几何。
4. 运行动态样式/端点断言，检查运动方向端点与轨道端点的贴合。

### 预期结果

垂直规则为 `min-height: 24px; margin: 0px 1px`，水平规则为 `min-width: 24px; margin: 1px 0px`；运动方向没有 2px 内缩，侧向 margin 仍为 1px。

### 后置条件

不写入设置；销毁临时 view 和窗口。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testScrollBarHandleTrackEndpoints`，以及 `tests/scrollbar_zoom_acceptance_static.py::ST-SB-01`。

## TC-ZOOM-CENTER-THRESHOLD

### 测试目的

验证以可用 viewport 中心缩放、并跨过 `ScrollBarAsNeeded` 阈值时，中心 scene 点不会因 viewport 变窄/变矮而漂移。

### 前置条件

- 可见 640×480 左右的普通窗口，图片初始 fit 后两轴 scrollbar 隐藏。
- 缩放目标为 `Qv::CalculateViewportCenterPos`。
- 测试图片尺寸位于出现 scrollbar 的阈值附近。

### 输入数据

- 确定性 PNG；初始 zoom level、可用 viewport 中心 scene 点。
- 一个会使水平条出现并可能联动垂直条出现的缩放级别。
- 归一化中心坐标和 0.005 的允许误差。

### 操作步骤

1. 打开图片，记录 usable viewport 中心及其对应 scene 点。
2. 调用 `zoomAbsolute()` 跨过 AsNeeded 阈值。
3. 等待 scrollbar layout 完成，确认横条/纵条状态变化。
4. 在立即阶段和延迟几何阶段分别重新映射原 scene 点。
5. 比较两个阶段相对 viewport 中心的归一化坐标。

### 预期结果

滚动条出现后和延迟阶段后的中心归一化坐标均与缩放前相差不超过 0.005；不会出现由内部 resize 的半尺寸补偿引起的跳动。

### 后置条件

关闭窗口，停止 pending anchor timer，恢复退出策略和测试设置。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomAcrossScrollbarThresholdKeepsViewportCenterStable`。

## TC-ZOOM-BOTTOM-RIGHT

### 测试目的

验证右下区域以固定 viewport 像素点缩放时，水平条首次出现及其之后的布局/scene 更新不会移动同一个 scene anchor。

### 前置条件

- 可见 640×480 Cocoa 窗口，初始图片已加载。
- 右下目标点在图片有效范围内，并距离真正 maximum 留有余量，避免把正常 Qt 截断误判为缺陷。
- `transformationAnchor=NoAnchor`，项目代码负责保存锚点。

### 输入数据

- 620×420 或等价确定性 PNG。
- 右下显式目标点（当前样例约为 usable viewport 右侧内缩 40px、底部内缩 100px）。
- 缩放级别 1.25；立即/稳定阶段容差 2px；阶段间二次位移容差 1px。

### 操作步骤

1. 打开图片并记录目标点对应的 scenePos。
2. 调用 `zoomAbsolute(1.25, target)`。
3. 在横向 scrollbar 出现后立即记录 `mapFromScene(scenePos)`。
4. 处理延迟事件至少 150ms，再次记录同一映射点。
5. 比较目标点、立即采样和稳定采样。

### 预期结果

横/纵滚动条出现后的立即映射距固定目标不超过 2px；150ms 后仍不超过 2px；两次采样之间不超过 1px。图片和 scrollbar 不发生第二次可见跳位。

### 后置条件

关闭窗口，释放临时图片，pending anchor generation 随 view 清理，恢复退出策略。

### 固化代码

`tests/tst_qviewtests.cpp::GraphicsViewTests::testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar`。

## TC-SC-ACTION-SURFACE

### 测试目的

验证 Settings → Shortcuts 和 View 菜单的 Action 面使用新的 `Toggle Fit and 100%`，并移除 `Original Size`、`Zoom to Fit`、`Navigation Resets Zoom`。

### 前置条件

- ActionManager、ShortcutManager 和 QVOptionsDialog 已初始化。
- Settings 对话框可显示 Shortcuts Tab。

### 输入数据

- ShortcutManager 的 row inventory。
- ActionManager canonical action key 与 View 菜单 clone。
- Shortcuts 表格两列。

### 操作步骤

1. 遍历 ShortcutManager 列表，按 `name` 检查新旧 key。
2. 检查 canonical action library 和 View 菜单 action data。
3. 打开 Settings，切换到 Shortcuts Tab。
4. 按 row 映射检查新 Action 的可见名称及快捷键列。

### 预期结果

存在 `togglefitand100` 与 `Toggle Fit and 100%`；旧三个名称/key 不在可配置列表、canonical action 或 View 菜单中；新行可见且只有一个快捷键字段。

### 后置条件

关闭 Settings 对话框，不修改用户快捷键；临时 UI 对象销毁。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100IsTheOnlyFitShortcutAction`。

## TC-SC-DEFAULT-Z

### 测试目的

验证 `Toggle Fit and 100%` 的默认快捷键、有效设置值和 QAction 映射均为未修饰的 `Z`。

### 前置条件

- ShortcutManager 已初始化。
- `shortcuts/togglefitand100` 可以由 scoped fixture 临时设置。

### 输入数据

- `QKeySequence(Qt::Key_Z)`。
- 持久化值 `shortcuts/togglefitand100 = ["Z"]`。

### 操作步骤

1. 写入 scoped Z 设置并调用 `updateShortcuts()`。
2. 查找 `togglefitand100` row，比较 `defaultShortcuts` 与 `shortcuts`。
3. 读取 canonical QAction 的 `shortcuts()`。

### 预期结果

row 与 QAction 各自恰好有一个 `QKeySequence(Qt::Key_Z)`；不存在 Ctrl、Alt、Shift 或 Qt symbolic fallback。

### 后置条件

恢复原有 shortcut setting，并再次刷新 ShortcutManager action map。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100DefaultsToZ`。

## TC-SC-TOGGLE-BEHAVIOR

### 测试目的

验证 Z 的功能状态机：当前不是 fit 时进入适合窗口，当前是 fit 时进入精确 100%，再次触发回到 fit。

### 前置条件

- 可见普通图片窗口已加载 1600×1000 PNG。
- `windowresizemode=Never`、平滑缩放关闭。
- `originalsizeastoggle=true`，用于验证新 Action 不受遗留偏好影响。

### 输入数据

- 手动 200% zoom。
- canonical `togglefitand100` QAction。
- 三次 Action dispatch。

### 操作步骤

1. 手动设为 200%，确认 calculated zoom mode 为空。
2. 第一次 dispatch Action，读取 mode 和 fit level。
3. 第二次 dispatch Action，读取 mode 和 zoom level。
4. 第三次 dispatch Action，再读取 mode。

### 预期结果

第一次为 `ZoomToFit` 且 fit level 小于 1；第二次 calculated mode 为空且 zoom level 等于 1.0；第三次重新为 `ZoomToFit`。旧 `originalsizeastoggle` 不改变该结果。

### 后置条件

关闭窗口，恢复 calculated zoom、original-size toggle、窗口退出策略及临时文件。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100ChangesBetweenFitAnd100Percent`。

## TC-SC-TRANSLATIONS

### 测试目的

验证新 Action 在 ShortcutManager 和 ActionManager 两个生产 context 中使用精确的四种翻译。

### 前置条件

- CMake 已生成 `qview_es.qm`、`qview_ja.qm`、`qview_zh_Hans.qm`、`qview_zh_Hant.qm`。
- 英文使用 source translator，避免依赖机器 locale。

### 输入数据

| 语言 | 预期文本 |
| --- | --- |
| 简体中文 | `切换适合窗口/100%` |
| 繁体中文 | `切換符合視窗/100%` |
| Español | `Alternar Ajustar/100 %` |
| 日本語 | `合わせる/100%切り替え` |

### 操作步骤

1. 逐个加载并安装语言 translator。
2. 翻译 source `Toggle Fit and 100%`，分别指定 `ShortcutManager` 和 `ActionManager` context。
3. 比较完整字符串，包含斜杠、百分号与西语百分号前空格。
4. 每轮移除 translator。

### 预期结果

四种语言的两个 context 均精确等于输入表，不存在空翻译、unfinished 或标点/空格差异。

### 后置条件

移除所有临时 translator，不改变当前语言设置。

### 固化代码

`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testToggleFitAnd100Translations`。

## TC-STATIC-CONTRACT

### 测试目的

通过源码静态分析验证动态用例依赖的实现合同、每条原子标准的测试入口、翻译目录和本规格六字段均存在。

### 前置条件

- 仓库根目录可读，Python 3 可执行。
- `reports/technical_design_document.md` 与本文件已写入。

### 输入数据

- `src/qvgraphicsview.cpp/.h`、`src/scrollhelper.cpp`。
- `src/shortcutmanager.cpp`、`src/actionmanager.cpp`、`src/mainwindow.cpp`。
- 四个 TS catalog、两个 QtTest 源文件和本 Markdown。

### 操作步骤

执行：

```bash
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
python3 tests/shortcut_toggle_acceptance_static.py \
  --repo . --output reports/evidence/shortcut_toggle_static.json
python3 -m py_compile tests/scrollbar_zoom_acceptance_static.py \
  tests/shortcut_toggle_acceptance_static.py
```

检查两个 JSON 的 `passed` 和每个 `checks[].pass`。

### 预期结果

滚动合同脚本的 `ST-SB-01`、`ST-SB-02`、`ST-ZOOM-01`、`ST-ZOOM-02`、`ST-TEST-01`、`ST-TEST-02` 全部为 true；快捷键脚本的 `ST-SC-01` 至 `ST-SC-06` 全部为 true；编译检查返回码为 0。

### 后置条件

保留 `reports/evidence/*.json` 作为机器可读溯源证据；不改变产品设置或源码运行状态。

### 固化代码

`tests/scrollbar_zoom_acceptance_static.py` 与 `tests/shortcut_toggle_acceptance_static.py`。
