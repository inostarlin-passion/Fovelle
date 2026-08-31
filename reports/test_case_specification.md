# CI 修复与固定相邻预加载测试用例说明

> 生成时间（UTC）：2026-08-31T06:59:11.069406+00:00
>
> 任务范围：修复 GitHub Actions 的 Cocoa 几何回归；固定预加载为 Adjacent；移除预加载模式枚举；并保留设置页相关回归覆盖。

## 一、原子化验收标准

| 编号 | 原子验收标准 | 测试代码 | 覆盖阶段 |
|---|---|---|---|
| AC-SETTINGS-TAB-WIDTHS | General、Shortcuts、Mouse 每个 Tab 按自身内容计算宽度，切换 Tab 后不共用最大宽度且无水平溢出。 | `tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights` | static、integration、system |
| AC-SETTINGS-SHORTCUT-COLUMNS | Shortcuts Tab 的 Action 与 Shortcuts 两列在整数像素取整下等宽（差值不超过 1px），且表格不产生水平滚动。 | `tests/tst_qviewtests.cpp::ShortcutSettingsTests::testShortcutsColumnFillsRemainingWidth` | static、shortcut、integration、system |
| AC-SETTINGS-CHECKERBOARD-SOURCE | General Tab 的复选框源文案由 Use checkerboard background after opening image 改为 Use checkerboard background。 | `tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions + tests/settings_ui_quality_pipeline.py::static_stage` | static、unit、system |
| AC-SETTINGS-CHECKERBOARD-TRANSLATIONS | 西班牙语、日语、简体中文、繁体中文均将新 checkerboard source 翻译为对应的短文案，且无 unfinished 或旧 source。 | `tests/settings_ui_quality_pipeline.py::static_stage + tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage` | static、integration |
| AC-CI-SHORTCUT-GEOMETRY | GitHub Actions 在 macOS 26、Qt 6.11.2 的 Cocoa 像素取整差异下不因两列相差一个整数像素而失败，同时保留 header 总宽度和无水平滚动不变量。 | `tests/tst_qviewtests.cpp::ShortcutSettingsTests::testShortcutsColumnFillsRemainingWidth + tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights + tests/preload_policy_quality.py` | static、shortcut、integration、system |
| AC-PRELOAD-DEFAULT-ADJACENT | 预加载策略的默认距离为 Adjacent，即固定距离 1；源码不再声明 PreloadMode 枚举。 | `tests/preload_policy_quality.py + tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralLanguageAndRemovedOptions` | static、unit |
| AC-PRELOAD-OVERRIDE-DISABLED | 用户持久化值 0（旧 Disabled）不能关闭预加载；当前图和直接相邻图必须仍被请求。 | `tests/tst_qviewtests.cpp::FeatureTests::testPreloadingIgnoresDisabledUserSetting` | static、unit |
| AC-PRELOAD-OVERRIDE-EXTENDED | 用户持久化值 2（旧 Extended）不能扩大预加载范围；只允许直接相邻图。 | `tests/tst_qviewtests.cpp::FeatureTests::testPreloadingIgnoresExtendedUserSetting` | static、unit |
| AC-PRELOAD-MIGRATION | 旧配置迁移后不能恢复任何预加载模式；legacy preloadingmode 必须归一化为 Adjacent。 | `tests/tst_qviewtests.cpp::FeatureTests::testRemovedMouseSettingsMigrateToFixedDefaults + src/settingsmanager.cpp` | static、unit |
| AC-QUALITY-TRACEABILITY | 每条原子验收标准都有包含六个必备字段的可执行测试说明，并由静态、动态和报告阶段闭环验证。 | `tests/preload_policy_quality.py + tests/quality_specification.py + reports/test_case_specification.md + reports/test_completion_report.md` | static、unit、shortcut、integration、system |

## 二、逐条测试用例

### AC-SETTINGS-TAB-WIDTHS

验收标准：General、Shortcuts、Mouse 每个 Tab 按自身内容计算宽度，切换 Tab 后不共用最大宽度且无水平溢出。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 验证设置页的页面宽度来自当前 Tab 的自然内容尺寸，并在原生 Cocoa 窗口中完成切换。 |
| 前置条件 | 已构建带 Cocoa 的 fovelle_tests；QVOptionsDialog 可构造；macOS 事件循环可用。 |
| 输入数据 | 三个 Tab、每个 Tab 的 settingsTabWidths、QStackedWidget 宽度、对话框宽度和两个滚动区域。 |
| 操作步骤 | 构造并显示设置页；依次选择 General、Shortcuts、Mouse；等待尺寸动画结束；读取每个 Tab 的宽度和滚动范围。 |
| 预期结果 | 每个 Tab 的内容区宽度等于该 Tab 的自然宽度，窗口宽度随 Tab 变化并与当前宽度记录一致；General/Mouse 无水平滚动。 |
| 后置条件 | 关闭设置页；临时语言、Tab 索引和窗口几何恢复。 |

- 测试代码：`tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights`
- 证据阶段：static, integration, system

### AC-SETTINGS-SHORTCUT-COLUMNS

验收标准：Shortcuts Tab 的 Action 与 Shortcuts 两列在整数像素取整下等宽（差值不超过 1px），且表格不产生水平滚动。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 验证两列使用同一个自然列宽，并在可视区域内等宽填充。 |
| 前置条件 | 设置页已显示；Shortcuts 表已填充快捷键数据。 |
| 输入数据 | QTableWidget 的两个横向 header section、viewport 宽度、header length 和水平滚动范围。 |
| 操作步骤 | 切换到 Shortcuts；读取两列的 resize mode、section size、header length 和 horizontalScrollBar maximum。 |
| 预期结果 | 两列均为 Stretch，stretchLastSection 为 false，两列 section size 为正且整数取整差不超过 1px，header 覆盖 viewport，水平滚动最大值为 0。 |
| 后置条件 | 关闭设置页；不修改快捷键持久化值。 |

- 测试代码：`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testShortcutsColumnFillsRemainingWidth`
- 证据阶段：static, shortcut, integration, system

### AC-SETTINGS-CHECKERBOARD-SOURCE

验收标准：General Tab 的复选框源文案由 Use checkerboard background after opening image 改为 Use checkerboard background。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 验证 Qt Designer 源文案、运行时英文文案和旧文案清理一致。 |
| 前置条件 | QVApplication 已初始化；英文源翻译测试夹具已安装。 |
| 输入数据 | checkerboardBackgroundCheckbox 的 objectName 和 text。 |
| 操作步骤 | 构造 QVOptionsDialog；查找复选框；读取运行时 text；同时执行静态 UI/XML source 检查。 |
| 预期结果 | 运行时 text 精确等于 Use checkerboard background；生产 UI 与目录中不存在旧 source。 |
| 后置条件 | 销毁对话框；恢复临时设置，不改变棋盘格开关值。 |

- 测试代码：`tests/tst_qviewtests.cpp::FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions + tests/settings_ui_quality_pipeline.py::static_stage`
- 证据阶段：static, unit, system

### AC-SETTINGS-CHECKERBOARD-TRANSLATIONS

验收标准：西班牙语、日语、简体中文、繁体中文均将新 checkerboard source 翻译为对应的短文案，且无 unfinished 或旧 source。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 逐目录核对新 source 的翻译内容、完成状态和旧 source 删除情况。 |
| 前置条件 | 四个 TS 目录和 XML 解析器可用；构建配置启用 Linguist 翻译。 |
| 输入数据 | qview_es.ts、qview_ja.ts、qview_zh_Hans.ts、qview_zh_Hant.ts 及 i18n/template.ts。 |
| 操作步骤 | 逐个解析 TS；按新 source 查找 translation；比较目标译文和 unfinished 属性；查找旧 source。 |
| 预期结果 | 四个目录分别得到精确目标译文，translation 非空且未标记 unfinished；旧 source 在所有目录和模板中不存在。 |
| 后置条件 | 不写入用户设置；解析结果写入完成报告。 |

- 测试代码：`tests/settings_ui_quality_pipeline.py::static_stage + tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage`
- 证据阶段：static, integration

### AC-CI-SHORTCUT-GEOMETRY

验收标准：GitHub Actions 在 macOS 26、Qt 6.11.2 的 Cocoa 像素取整差异下不因两列相差一个整数像素而失败，同时保留 header 总宽度和无水平滚动不变量。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 复现远程 Checks 的 165/164 列宽失败并验证修复覆盖所有 Shortcuts 几何断言。 |
| 前置条件 | Cocoa QtTest 可运行；Shortcuts 表含两个 Stretch section；生产设置探针可启动。 |
| 输入数据 | 奇数可用 viewport、两个实际 sectionSize、header length、viewport width 和 horizontalScrollBar maximum。 |
| 操作步骤 | 运行快捷键专项、WindowBehavior 几何/多语言用例和生产 settings probe；记录 section 差值与总宽度。 |
| 预期结果 | 两个 section 的实际整数宽度差不超过 1；两列之和精确等于 header length；水平滚动最大值为 0；Actions 检查不再因 165/164 失败。 |
| 后置条件 | 关闭测试窗口和探针进程；不改变用户设置或远程 Actions 状态。 |

- 测试代码：`tests/tst_qviewtests.cpp::ShortcutSettingsTests::testShortcutsColumnFillsRemainingWidth + tests/tst_qviewtests.cpp::WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights + tests/preload_policy_quality.py`
- 证据阶段：static, shortcut, integration, system

### AC-PRELOAD-DEFAULT-ADJACENT

验收标准：预加载策略的默认距离为 Adjacent，即固定距离 1；源码不再声明 PreloadMode 枚举。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 验证默认值语义与类型层面的枚举移除，防止旧模式分支重新出现。 |
| 前置条件 | SettingsManager 与 QVApplication 已初始化；源码静态检查和 FeatureTests 可用。 |
| 输入数据 | AdjacentPreloadDistance、兼容键 preloadingmode 的默认值、qvnamespace.h/qvimagecore.*。 |
| 操作步骤 | 执行 PRELOAD-STATIC-001..004；运行 FeatureTests::testSettingsGeneralLanguageAndRemovedOptions 读取默认值。 |
| 预期结果 | AdjacentPreloadDistance==1；兼容默认值为 1；QVImageCore 没有 PreloadMode/preloadingMode 或 getEnum 读取。 |
| 后置条件 | 释放设置对话框；默认值检查不写入新的用户设置。 |

- 测试代码：`tests/preload_policy_quality.py + tests/tst_qviewtests.cpp::FeatureTests::testSettingsGeneralLanguageAndRemovedOptions`
- 证据阶段：static, unit

### AC-PRELOAD-OVERRIDE-DISABLED

验收标准：用户持久化值 0（旧 Disabled）不能关闭预加载；当前图和直接相邻图必须仍被请求。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 验证强制 Adjacent 策略覆盖最小/禁用旧设置。 |
| 前置条件 | macOS Cocoa 事件循环可用；QVImageLoader 的 loadStarted 信号可观测；临时目录可写。 |
| 输入数据 | 四张有序 PNG，preloadingmode=0，当前索引 0，期望 priority 0/1。 |
| 操作步骤 | 运行 testPreloadingIgnoresDisabledUserSetting；打开当前图；等待前景加载和邻图请求；枚举全部 loadStarted 记录。 |
| 预期结果 | 当前图 priority=0；第二张图 priority=1；没有第三/第四张图请求；不存在 priority>1。 |
| 后置条件 | 关闭窗口；ScopedOptionValues 恢复原设置；后台任务收敛。 |

- 测试代码：`tests/tst_qviewtests.cpp::FeatureTests::testPreloadingIgnoresDisabledUserSetting`
- 证据阶段：static, unit

### AC-PRELOAD-OVERRIDE-EXTENDED

验收标准：用户持久化值 2（旧 Extended）不能扩大预加载范围；只允许直接相邻图。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 验证强制 Adjacent 策略覆盖扩展旧设置，并保持 priority 取值边界。 |
| 前置条件 | macOS Cocoa 事件循环可用；QVImageLoader 的 loadStarted 信号可观测；临时目录可写。 |
| 输入数据 | 四张有序 PNG，preloadingmode=2，当前索引 1，左右邻图和距离 2 图。 |
| 操作步骤 | 运行 testPreloadingIgnoresExtendedUserSetting；打开第二张图；等待两侧邻图；枚举全部 loadStarted 记录。 |
| 预期结果 | 第一/第三张图均 priority=1；第四张距离 2 图不请求；不存在 priority>1。 |
| 后置条件 | 关闭窗口；ScopedOptionValues 恢复原设置；后台任务收敛。 |

- 测试代码：`tests/tst_qviewtests.cpp::FeatureTests::testPreloadingIgnoresExtendedUserSetting`
- 证据阶段：static, unit

### AC-PRELOAD-MIGRATION

验收标准：旧配置迁移后不能恢复任何预加载模式；legacy preloadingmode 必须归一化为 Adjacent。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 验证启动迁移和运行时覆盖两条边界均不会让旧配置复活。 |
| 前置条件 | firstlaunch 标记和 QSettings 可写；SettingsManager 迁移函数可调用。 |
| 输入数据 | options/preloadingmode=0 与既有 Mouse 旧配置。 |
| 操作步骤 | 运行 testRemovedMouseSettingsMigrateToFixedDefaults；调用 migrateOldSettings；读取持久化值并重新加载 manager。 |
| 预期结果 | options/preloadingmode 和 manager getInteger 均为 AdjacentPreloadDistance；Mouse 旧值也按既有固定策略归一化。 |
| 后置条件 | ScopedSettingPreserver/ScopedOptionValues 恢复测试前的配置。 |

- 测试代码：`tests/tst_qviewtests.cpp::FeatureTests::testRemovedMouseSettingsMigrateToFixedDefaults + src/settingsmanager.cpp`
- 证据阶段：static, unit

### AC-QUALITY-TRACEABILITY

验收标准：每条原子验收标准都有包含六个必备字段的可执行测试说明，并由静态、动态和报告阶段闭环验证。

| 测试字段 | 内容 |
|---|---|
| 测试目的 | 检查验收标准、测试代码、执行证据和报告字段一一对应。 |
| 前置条件 | Python 3、仓库源码、Cocoa 构建产物和报告目录可用。 |
| 输入数据 | tests/preload_policy_quality.py、tests/quality_specification.py、两份 Markdown 报告及 CTest 输出。 |
| 操作步骤 | 执行静态策略门禁、规格映射校验、全量 CTest、QT_SCALE_FACTOR=1 CTest、设置质量流水线和生产探针；核对报告中的命令与结果。 |
| 预期结果 | 静态门禁 11/11；规格映射 67 条且无校验错误；动态 CTest 和报告阶段返回码均为 0；每个用例均有测试目的、前置条件、输入数据、操作步骤、预期结果、后置条件。 |
| 后置条件 | 报告保留本次主机、工具链、命令和边界说明；不产生未声明的远程副作用。 |

- 测试代码：`tests/preload_policy_quality.py + tests/quality_specification.py + reports/test_case_specification.md + reports/test_completion_report.md`
- 证据阶段：static, unit, shortcut, integration, system

## 三、联网检索与多跳推理溯源

检索只采用 Qt 官方文档作为框架行为事实来源；仓库源码和本地 Cocoa 执行是实现事实来源。推理链明确区分事实、前提和结论：

1. 来源：[官方文档/执行证据](https://doc.qt.io/qt-6/qtabbar.html)。
   - 已证事实：QTabBar 的 expanding 属性为 true 时会把 Tab 扩展到空白区域；关闭 expanding 后，Tab 可按自身 size hint 排布。
   - 显式前提：设置页使用 QTabBar 作为原生 toolbar 的页面模型，并已设置 expanding=false。
   - 下钻结论：页面内容区应单独测量；不能用一个最大宽度反向固定所有 Tab。
2. 来源：[官方文档/执行证据](https://doc.qt.io/qt-6/layout.html)。
   - 已证事实：Qt 布局依据 QWidget 的 sizePolicy、sizeHint 和 minimum size 分配空间；重新计算尺寸时应更新几何。
   - 显式前提：General 和 Mouse 是包含控件布局的 QWidget，Shortcuts 是包含 QTableWidget 的页面。
   - 下钻结论：为每个页面保存内容派生宽度，再在 currentChanged 时应用当前页面宽度，是可验证的自适应边界。
3. 来源：[官方文档/执行证据](https://doc.qt.io/qt-6/qscrollarea.html)。
   - 已证事实：QScrollArea 的 widgetResizable 与子 widget 的 minimumSize/sizeHint 共同决定是否需要滚动条。
   - 显式前提：需求要求每个 Tab 内容完整可见，不能靠裁剪本地化文本。
   - 下钻结论：自然宽度必须包含滚动区域 frame、布局 margin 和可见表格 chrome，并用水平滚动 maximum=0 验收。
4. 来源：[官方文档/执行证据](https://doc.qt.io/qt-6/qheaderview.html)。
   - 已证事实：QHeaderView::Stretch 会填满可用区域；stretchLastSection 会覆盖最后一节的 resize mode；sectionSize 可读取实际宽度。
   - 显式前提：需求要求 Action 与 Shortcuts 两列相等，而不仅是 Shortcuts 占用剩余空间。
   - 下钻结论：两列都设为 Stretch、关闭 stretchLastSection，并以两列相同自然宽度计算 Shortcuts 页面宽度。
5. 来源：`local:src/qvoptionsdialog.cpp + tests/tst_qviewtests.cpp`。
   - 已证事实：实现保存三项 settingsTabWidths；测试在三个 Tab 和五种应用语言下检查实际几何与无水平滚动，并检查两列 section size。
   - 显式前提：本地 macOS 15 / Qt 6.11.1 是当前可执行验证环境，Cocoa 原生 toolbar 的显示通过生产路径调用。
   - 下钻结论：静态、QtTest 集成和真实 app system probe 共同覆盖源文案、译文、页面宽度及原生窗口边界。
6. 来源：[官方文档/执行证据](https://github.com/inostarlin-passion/Fovelle/actions/runs/33361992196/job/99394990862)。
   - 已证事实：远程 Checks 的 configure/build、Qt 安装、clang-tidy 和 clang-format 均完成；失败集中在 FovelleTests 的 Shortcuts 断言，实际 sectionSize 为 165 与 164。
   - 显式前提：远程日志是对 HEAD 的只读执行证据，不把日志中的结论直接当作修复方案。
   - 下钻结论：失败是整数像素余数被严格相等断言放大的测试契约问题，而非 Qt 安装或编译失败；修复应保留总宽度/无滚动不变量并接受最多 1px 取整差。
7. 来源：[官方文档/执行证据](https://doc.qt.io/qt-6/qheaderview.html)。
   - 已证事实：QHeaderView 的 Stretch section 会填充可用 header 空间，实际宽度由整数 sectionSize 体现；可用空间为奇数时，两个 section 不必获得相同整数。
   - 显式前提：Shortcuts 表的业务不变量是完整填充且无水平滚动，而不是依赖浮点宽度。
   - 下钻结论：将断言拆为 header 总宽精确相等、两列正数、差值≤1px，才能对 Cocoa/Retina 尺寸取整稳定。
8. 来源：[官方文档/执行证据](https://doc.qt.io/qt-6/qsettings.html)。
   - 已证事实：QSettings 会按键持久化 QVariant 值，旧配置可在启动时被读取并改写。
   - 显式前提：需求要求覆盖用户设置，同时不能让旧 profile 在迁移后重新启用已删除模式。
   - 下钻结论：保留 legacy key 仅用于兼容迁移，将其归一化为 1；QVImageCore 不读取该 key，而是固定使用 AdjacentPreloadDistance。
9. 来源：[官方文档/执行证据](https://github.com/actions/runner-images/blob/main/images/macos/macos-26-Readme.md)。
   - 已证事实：仓库 CI 使用 macOS 26 runner，install-qt-action 提供 version 输入，当前 workflow 固定 Qt 6.11.2。
   - 显式前提：修复必须在触发失败的 runner/toolchain 组合上可复现，不能只在本地 Qt 版本上成立。
   - 下钻结论：将固定策略静态门禁放入 Checks/build workflow，并保留 bounded CTest timeout，使源代码防回归检查与动态回归检查在同一 CI 入口执行。
10. 来源：[官方文档/执行证据](https://github.com/jurplel/install-qt-action/blob/master/action/action.yml)。
   - 已证事实：install-qt-action 的 action metadata 支持 version 输入，workflow 可把 Qt 版本作为可审计的固定构建前提。
   - 显式前提：远程失败环境使用 Qt 6.11.2，修复验证需要区分本地 Qt 6.11.1 与 CI 固定版本。
   - 下钻结论：报告同时记录本地 Qt 6.11.1 的动态结果和 CI workflow 的 Qt 6.11.2 固定项，不把本地运行冒充远程重跑。

## 四、测试设计约束

1. 页面宽度验收以当前 Tab 的自然内容宽度为输入，不用单一最大宽度掩盖某个页面的尺寸契约。
2. Shortcuts 的等宽验收同时读取两个 section 的实际 `sectionSize`、header/viewport 几何和水平滚动范围，并接受最多 1px 的整数取整差。
3. 预加载验收同时覆盖默认常量、旧配置迁移、Disabled/Extended 两个用户设置覆盖值和距离/优先级边界。
4. 静态门禁检查旧 `PreloadMode` 类型和 runtime setting read 不存在；动态用例检查实际 loader 请求。
5. 系统阶段使用生产应用的显式 `FOVELLE_SETTINGS_SYSTEM_PROBE` 环境入口，不改变普通启动路径或用户设置。
