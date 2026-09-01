# 三项界面问题测试用例说明

- 规格版本：1.0
- 编写日期：2026-09-01
- 被测基线：`e6c73fb6cd9c893804f06e2561ba508b958a2ebd`（实现后以当前工作树重新构建）
- 根因依据：`reports/root_cause.md`
- 日期格式依据：`/Users/inostarlin/Downloads/不同语言的修改时间格式.xlsx`

本文将需求拆成 4 条可独立判定的原子验收标准。每个动态用例均固化在 `tests/tst_qviewtests.cpp`；每个用例明确测试目的、前置条件、输入数据、操作步骤、预期结果和后置条件。

## 原子验收标准与测试映射

| 原子标准 | 测试用例 | 测试层级 |
|---|---|---|
| AC-NAV-PREVIOUS-ABSENT：没有上一张图片时隐藏左侧按钮 | TC-NAV-PREVIOUS-ABSENT | Cocoa QtTest |
| AC-NAV-NEXT-ABSENT：没有下一张图片时隐藏右侧按钮 | TC-NAV-NEXT-ABSENT | Cocoa QtTest |
| AC-FILEINFO-MODIFIED-FORMAT：Modified 按 UI 语言使用参考格式 | TC-FILEINFO-MODIFIED-FORMAT | QtTest 数据表断言 |
| AC-SCROLLBAR-TITLEBAR-INSET：垂直滚动条顶端避开标题栏遮挡区 | TC-SCROLLBAR-TITLEBAR-INSET | Cocoa QtTest |

## TC-NAV-PREVIOUS-ABSENT

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证文件夹首图在关闭循环浏览时不存在上一张图片，左侧 Previous 导航按钮不会被请求显示。 |
| 前置条件 | Cocoa QtTest 应用可启动；窗口宽度为 800；排序为 Name 升序；`loopfoldersenabled=false`；目录含至少三张可解码 PNG。 |
| 输入数据 | `01-boundary.png`、`02-boundary.png`、`03-boundary.png`，当前打开 `01-boundary.png`；鼠标位置为内容区左边缘和右边缘。 |
| 操作步骤 | 1. 构造 `MainWindow` 并打开首图。<br>2. 等待图像加载完成、目录列表数为 3 且索引为 0。<br>3. 将鼠标移到 viewport 左边缘。<br>4. 读取 `previousImageButton` 的 `navigationRequestedVisible`。<br>5. 将鼠标移到右边缘，读取 Next 的同一属性。 |
| 预期结果 | 左边缘时 Previous 的 requested visibility 为 `false`；右边缘时 Next 为 `true`，说明只有“无上一张”的方向被屏蔽，正常可用方向未被误伤。 |
| 后置条件 | 关闭窗口；恢复测试前的 QSettings；释放 `QTemporaryDir` 和异步加载资源。 |

测试代码：`tests/tst_qviewtests.cpp::WindowBehaviorTests::testPreviousNavigationButtonHiddenWithoutPreviousFile`

## TC-NAV-NEXT-ABSENT

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证文件夹末图在关闭循环浏览时不存在下一张图片，右侧 Next 导航按钮不会被请求显示。 |
| 前置条件 | Cocoa QtTest 应用可启动；窗口宽度为 800；排序为 Name 升序；`loopfoldersenabled=false`；目录含至少三张可解码 PNG。 |
| 输入数据 | 与 TC-NAV-PREVIOUS-ABSENT 相同的三张图片，当前打开 `03-boundary.png`；鼠标位置为内容区右边缘和左边缘。 |
| 操作步骤 | 1. 构造 `MainWindow` 并打开末图。<br>2. 等待图像加载完成、目录列表数为 3 且索引为 2。<br>3. 将鼠标移到 viewport 右边缘。<br>4. 读取 `nextImageButton` 的 `navigationRequestedVisible`。<br>5. 将鼠标移到左边缘，读取 Previous 的同一属性。 |
| 预期结果 | 右边缘时 Next 的 requested visibility 为 `false`；左边缘时 Previous 为 `true`，说明只有“无下一张”的方向被屏蔽，正常可用方向未被误伤。 |
| 后置条件 | 关闭窗口；恢复测试前的 QSettings；释放 `QTemporaryDir` 和异步加载资源。 |

测试代码：`tests/tst_qviewtests.cpp::WindowBehaviorTests::testNextNavigationButtonHiddenWithoutNextFile`

## TC-FILEINFO-MODIFIED-FORMAT

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证 File Info 的 Modified 字段按当前 UI 语言使用参考工作簿中的唯一格式，并验证 live `QVInfoDialog` 接线没有绕过 formatter。 |
| 前置条件 | `QVInfoDialog`、`SettingsManager` 和 Qt `QLocale` 可用；创建一张临时 PNG；使用固定的日期时间，避免依赖测试机器当前时间。 |
| 输入数据 | 固定时间 `2026-09-01 13:55`；语言代码及期望值如下。 |
| 操作步骤 | 1. 对每个语言代码调用 `QVInfoDialog::formatModifiedDateTime()`。<br>2. 比较结果与参考输出。<br>3. 将 `options/language` 写入该语言，创建 `QVInfoDialog` 并调用 `setInfo()`。<br>4. 读取 `modifiedLabel`，与同一 formatter 对实际文件 `lastModified()` 的结果比较。 |
| 预期结果 | 所有数据行均通过：<br><br>`en` → `Sep 1, 2026, 1:55 PM`<br>`zh_Hans` → `2026年9月1日 13:55`<br>`zh_Hant` → `2026年9月1日 下午1:55`<br>`es` → `1 sept 2026, 13:55`<br>`ja` → `2026年9月1日 13:55`。<br><br>live `modifiedLabel` 与 formatter 一致。 |
| 后置条件 | 恢复 language QSettings；销毁对话框；删除临时目录；不改变系统 locale。 |

测试代码：`tests/tst_qviewtests.cpp::FeatureTests::testFileInfoModifiedUsesUiLanguageFormats`

## TC-SCROLLBAR-TITLEBAR-INSET

| 字段 | 内容 |
|---|---|
| 测试目的 | 验证 full-size client area 下可见垂直滚动条的物理顶端不再落入 macOS 标题栏遮挡区域。 |
| 前置条件 | Cocoa QtTest 应用可启动；MainWindow 已显示并启用 full-size content view；运行时 `obscuredHeight > 0`；`ScrollBarAsNeeded` 和 OriginalSize 可用。 |
| 输入数据 | `1600x1600` 深青色 PNG；窗口 `640x480`；`windowresizemode=Never`、`calculatedzoommode=OriginalSize`。 |
| 操作步骤 | 1. 显示窗口并打开临时图片。<br>2. 等待图像加载、标题栏遮挡高度为正、垂直滚动条可见。<br>3. 将 `verticalScrollBar()->mapTo(view, QPoint())` 的 y 坐标映射到 `QVGraphicsView`。<br>4. 与 `window.getViewportPosition().obscuredHeight` 比较，并记录运行时几何证据。 |
| 预期结果 | `barTop >= obscuredHeight`；测试日志记录 `FOVELLE_SCROLLBAR_SAFE_AREA bar_top=... obscured_height=...`；滚动条仍保留原有溢出/底部布局。 |
| 后置条件 | 关闭窗口；恢复 QSettings；释放图片、滚动条和临时目录。 |

测试代码：`tests/tst_qviewtests.cpp::GraphicsViewTests::testVerticalScrollBarAvoidsTitlebarOverlap`

## 静态追踪用例

| 字段 | 内容 |
|---|---|
| 测试目的 | 检查实现 marker、动态测试 marker 和本规格的六个必备字段形成可审计闭环。 |
| 前置条件 | 仓库源文件和本 Markdown 文件存在；Python 3 可用。 |
| 输入数据 | `tests/task_acceptance_static.py`、`src/mainwindow.cpp`、`src/qvimagecore.*`、`src/qvinfodialog.*`、`src/qvgraphicsview.*`、`tests/tst_qviewtests.cpp`。 |
| 操作步骤 | 执行 `python3 tests/task_acceptance_static.py --repo . --output .tmp/task-acceptance-static.json`，检查 JSON 中每个 check 的 `pass` 字段。 |
| 预期结果 | 导航边界、语言格式、safe-area 几何和规格字段检查全部为 `true`，脚本返回码为 0。 |
| 后置条件 | 保留 JSON 作为本次运行的临时证据或在报告中记录其摘要；不修改产品设置和源代码。 |
