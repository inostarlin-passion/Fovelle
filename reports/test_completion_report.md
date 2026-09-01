# 三项界面问题测试完成报告

## 1. 结论

结论：通过。

四条原子验收标准均有对应测试用例和测试代码；任务专用静态检查、目标代码 clang-tidy、完整构建和 Cocoa 动态回归均通过。垂直滚动条实测安全区证据为 `bar_top=28`、`obscured_height=28`。

## 2. 被测范围

| 范围 | 结果 |
|---|---|
| Previous 在首图/非循环边界隐藏 | 已实现并通过 `testPreviousNavigationButtonHiddenWithoutPreviousFile` |
| Next 在末图/非循环边界隐藏 | 已实现并通过 `testNextNavigationButtonHiddenWithoutNextFile` |
| File Info Modified 的五种 UI 语言格式 | 已实现并通过 `testFileInfoModifiedUsesUiLanguageFormats` |
| 标题栏遮挡区之外的垂直滚动条 | 已实现并通过 `testVerticalScrollBarAvoidsTitlebarOverlap` |

## 3. 测试环境

- macOS 15.7.9、Cocoa、Apple Silicon
- Qt 6.11.1（arm64，Release 构建）
- CMake 构建目录：`build-current`
- C++ 标准：C++17
- QTest 环境：`QT_QPA_PLATFORM=cocoa`、`QT_FATAL_WARNINGS=1`、`QTEST_FUNCTION_TIMEOUT=30000`
- 根因参考：`reports/root_cause.md`
- 日期格式参考：`/Users/inostarlin/Downloads/不同语言的修改时间格式.xlsx`（只读解析）

## 4. 静态与构建验证

| 验证项 | 命令 | 结果 |
|---|---|---|
| 任务专用静态追踪 | `python3 tests/task_acceptance_static.py --repo . --output .tmp/task-acceptance-static.json` | 4/4 checks PASS，返回码 0 |
| 目标代码 clang-tidy | 使用相同的 macOS SDK 参数分别检查 `src/qvimagecore.cpp src/qvinfodialog.cpp src/qvgraphicsview.cpp src/mainwindow.cpp` 与 `tests/tst_qviewtests.cpp` | 两次调用均返回码 0 |
| 全量编译/链接 | `cmake --build build-current --parallel 2` | 应用与 `fovelle_tests` 均成功构建 |
| 差异空白检查 | `git diff --check HEAD -- src tests reports` | 返回码 0 |

clang-format 的补充说明：仓库现有 C++ 文件在当前 Homebrew LLVM 22 工具上整体触发历史格式差异，基线文件本身也可复现该结果；本次没有对全仓做格式化改写。目标代码已通过带 macOS SDK 参数的 clang-tidy，编译器/链接器和任务专用静态契约均通过。

## 5. 动态测试结果

| 测试批次 | 结果 |
|---|---|
| `FeatureTests::testFileInfoModifiedUsesUiLanguageFormats` | PASS；初始化、测试、清理共 3 passed |
| 两个导航边界测试 | PASS；包含既有 edge/size/contrast/fade/click 回归共 9 passed |
| `GraphicsViewTests::testVerticalScrollBarAvoidsTitlebarOverlap` | PASS；3 passed；记录 `bar_top=28 obscured_height=28` |
| `ctest --test-dir build-current --output-on-failure --timeout 90 -j1` | 3/3 tests passed，全部测试时间 41.73 秒 |

完整 CTest 覆盖原有图像、全屏、主题、手势、设置、快捷键和 native drag 测试；没有失败或跳过导致的红色结果。

## 6. 验收追踪

| 原子标准 | 生产代码证据 | 测试证据 | 判定 |
|---|---|---|---|
| AC-NAV-PREVIOUS-ABSENT | `QVImageCore::hasPreviousFile()`；`MainWindow::updateNavigationButtonVisibility()` | 首图、非循环、左边缘 requested visibility 为 false | PASS |
| AC-NAV-NEXT-ABSENT | `QVImageCore::hasNextFile()`；`MainWindow::updateNavigationButtonVisibility()` | 末图、非循环、右边缘 requested visibility 为 false | PASS |
| AC-FILEINFO-MODIFIED-FORMAT | `QVInfoDialog::formatModifiedDateTime()`；`updateInfo()` 接线 | en、zh_Hans、zh_Hant、es、ja 五个参考输出与 live label | PASS |
| AC-SCROLLBAR-TITLEBAR-INSET | scrollbar parent/container 几何刷新、布局事件重应用和防重入 | `barTop >= obscuredHeight` 动态断言 | PASS |

## 7. 交付物

- [技术设计文档](technical_design_document.md)
- [测试用例说明](test_case_specification.md)
- [测试完成报告](test_completion_report.md)
- [任务专用静态检查](../tests/task_acceptance_static.py)
