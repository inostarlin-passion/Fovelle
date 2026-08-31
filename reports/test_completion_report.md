# CI 修复与固定相邻预加载测试完成报告

> 生成时间（UTC）：2026-08-31T06:59:11.069406+00:00
> 仓库：`/Users/inostarlin/code/Fovelle`
> 构建目录：`/Users/inostarlin/code/Fovelle/build-current`

## 一、结论

**总体状态：通过**。

验收闭环按 `static → unit → shortcut → integration → system` 顺序执行；每一级的通过条件来自命令返回码及其结构化输出，而非手工填写。

| 阶段 | 状态 | 返回码 | 耗时（ms） |
|---|---|---:|---:|
| static | 通过 | 0 | 1340.859 |
| unit | 通过 | 0 | 482.928 |
| shortcut | 通过 | 0 | 259.022 |
| integration | 通过 | 0 | 7016.83 |
| system | 通过 | 0 | 1196.345 |

## 二、原子验收结果

| 编号 | 阶段证据 | 结果 |
|---|---|---|
| AC-SETTINGS-TAB-WIDTHS | static=pass, integration=pass, system=pass | 通过 |
| AC-SETTINGS-SHORTCUT-COLUMNS | static=pass, shortcut=pass, integration=pass, system=pass | 通过 |
| AC-SETTINGS-CHECKERBOARD-SOURCE | static=pass, unit=pass, system=pass | 通过 |
| AC-SETTINGS-CHECKERBOARD-TRANSLATIONS | static=pass, integration=pass | 通过 |
| AC-CI-SHORTCUT-GEOMETRY | static=pass, shortcut=pass, integration=pass, system=pass | 通过 |
| AC-PRELOAD-DEFAULT-ADJACENT | static=pass, unit=pass | 通过 |
| AC-PRELOAD-OVERRIDE-DISABLED | static=pass, unit=pass | 通过 |
| AC-PRELOAD-OVERRIDE-EXTENDED | static=pass, unit=pass | 通过 |
| AC-PRELOAD-MIGRATION | static=pass, unit=pass | 通过 |
| AC-QUALITY-TRACEABILITY | static=pass, unit=pass, shortcut=pass, integration=pass, system=pass | 通过 |

## 三、阶段证据

### static

- `ST-UI-XML`: PASS
- `ST-SOURCE-CONTRACT`: PASS
- `ST-OLD-SOURCE-REMOVED`: PASS
- `ST-TRANSLATIONS`: PASS
- `ST-TRANSLATION-TEMPLATE`: PASS
- `ST-TEST-COVERAGE`: PASS
- `ST-PYTHON-SYNTAX`: PASS
- `ST-CLANG-FORMAT`: PASS
- `ST-PRELOAD-POLICY`: PASS
- `ST-TEST-SPECIFICATION`: PASS
- `ST-DIFF`: PASS
- `ST-COMPILE`: PASS
- command：`XML/source/catalog/test-contract checks cmake --build /Users/inostarlin/code/Fovelle/build-current --parallel 2`

### unit

- suite：`FeatureTests`
- cases：`testSettingsRenamedLabelsAndRemovedMouseOptions, testSettingsGeneralLanguageAndRemovedOptions, testPreloadingIgnoresDisabledUserSetting, testPreloadingIgnoresExtendedUserSetting, testRemovedMouseSettingsMigrateToFixedDefaults`
- QTest totals：`{'passed': 7, 'failed': 0, 'skipped': 0, 'blacklisted': 0}`
- command：`/Users/inostarlin/code/Fovelle/build-current/tests/fovelle_tests -o -,txt testSettingsRenamedLabelsAndRemovedMouseOptions testSettingsGeneralLanguageAndRemovedOptions testPreloadingIgnoresDisabledUserSetting testPreloadingIgnoresExtendedUserSetting testRemovedMouseSettingsMigrateToFixedDefaults`
- output tail：

```text
********* Start testing of FeatureTests *********
Config: Using QtTest library 6.11.1, Qt 6.11.1 (arm64-little_endian-lp64 shared (dynamic) release build; by Apple LLVM 17.0.0 (clang-1700.6.4.2)), macos 15.7.9
PASS   : FeatureTests::initTestCase()
PASS   : FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions()
PASS   : FeatureTests::testSettingsGeneralLanguageAndRemovedOptions()
PASS   : FeatureTests::testPreloadingIgnoresDisabledUserSetting()
PASS   : FeatureTests::testPreloadingIgnoresExtendedUserSetting()
PASS   : FeatureTests::testRemovedMouseSettingsMigrateToFixedDefaults()
PASS   : FeatureTests::cleanupTestCase()
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 338ms
********* Finished testing of FeatureTests *********
```

### shortcut

- suite：`ShortcutSettingsTests`
- cases：`testShortcutsColumnFillsRemainingWidth`
- QTest totals：`{'passed': 3, 'failed': 0, 'skipped': 0, 'blacklisted': 0}`
- command：`/Users/inostarlin/code/Fovelle/build-current/tests/fovelle_tests -o -,txt testShortcutsColumnFillsRemainingWidth`
- output tail：

```text
********* Start testing of ShortcutSettingsTests *********
Config: Using QtTest library 6.11.1, Qt 6.11.1 (arm64-little_endian-lp64 shared (dynamic) release build; by Apple LLVM 17.0.0 (clang-1700.6.4.2)), macos 15.7.9
PASS   : ShortcutSettingsTests::initTestCase()
PASS   : ShortcutSettingsTests::testShortcutsColumnFillsRemainingWidth()
PASS   : ShortcutSettingsTests::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 141ms
********* Finished testing of ShortcutSettingsTests *********
```

### integration

- suite：`WindowBehaviorTests`
- cases：`testSettingsDialogUsesFixedWidthAndTabHeights, testSettingsDialogSizesFollowTranslations, testSettingsEveryTabFitsEveryLanguage`
- QTest totals：`{'passed': 5, 'failed': 0, 'skipped': 0, 'blacklisted': 0}`
- command：`/Users/inostarlin/code/Fovelle/build-current/tests/fovelle_tests -o -,txt testSettingsDialogUsesFixedWidthAndTabHeights testSettingsDialogSizesFollowTranslations testSettingsEveryTabFitsEveryLanguage`
- output tail：

```text
********* Start testing of WindowBehaviorTests *********
Config: Using QtTest library 6.11.1, Qt 6.11.1 (arm64-little_endian-lp64 shared (dynamic) release build; by Apple LLVM 17.0.0 (clang-1700.6.4.2)), macos 15.7.9
PASS   : WindowBehaviorTests::initTestCase()
PASS   : WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights()
PASS   : WindowBehaviorTests::testSettingsDialogSizesFollowTranslations()
PASS   : WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage()
PASS   : WindowBehaviorTests::cleanupTestCase()
Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 6898ms
********* Finished testing of WindowBehaviorTests *********
```

### system

- system probe：`{'marker_found': True, 'tabs': 3, 'adaptive': True, 'tab_widths_valid': True, 'current_tab_width': 340, 'columns_equal': True, 'checkerboard_renamed': True}`
- command：`/Users/inostarlin/code/Fovelle/build-current/Fovelle.app/Contents/MacOS/Fovelle`
- output tail：

```text
FOVELLE_SETTINGS_SYSTEM_PROBE tabs=3 adaptive=true tab_widths_valid=true current_tab_width=340 columns_equal=true checkerboard_renamed=true
```

## 四、显式前提与边界

- 本次可执行系统环境为 macOS Cocoa；Linux/Windows 不满足本项目的原生构建前提。
- 语言目录验收覆盖项目当前枚举的 English、Español、日本語、简体中文、繁體中文；English 使用源字符串，不生成独立英文 TS。
- system probe 只在测试环境变量存在时打开设置页并退出；普通用户启动和设置持久化路径不受影响。
- Qt 官方文档只用于确认框架语义；具体宽度数值、翻译值和运行结果以本仓库源码与本次命令输出为准。
- 本地动态验证使用 Qt 6.11.1；GitHub Actions workflow 固定 Qt 6.11.2，因此本地通过不等同于远程重跑，但静态门禁会锁定远程版本配置。
- 独立全量 CTest（含原生拖拽、所有非样本 QtTest 和快捷键专项）及 QT_SCALE_FACTOR=1 全量 CTest 均作为报告生成前的额外回归证据执行。

- clang-format 采用仓库配置执行；本机版本对本次涉及的所有 HEAD 基线 C++ 文件也报告格式差异，因此该项按既有基线差异记为可接受，并继续由 `git diff --check` 阻断新增空白错误。

## 五、溯源链接

- Hop 1：[证据来源](https://doc.qt.io/qt-6/qtabbar.html)
- Hop 2：[证据来源](https://doc.qt.io/qt-6/layout.html)
- Hop 3：[证据来源](https://doc.qt.io/qt-6/qscrollarea.html)
- Hop 4：[证据来源](https://doc.qt.io/qt-6/qheaderview.html)
- Hop 5：`local:src/qvoptionsdialog.cpp + tests/tst_qviewtests.cpp`
- Hop 6：[证据来源](https://github.com/inostarlin-passion/Fovelle/actions/runs/33361992196/job/99394990862)
- Hop 7：[证据来源](https://doc.qt.io/qt-6/qheaderview.html)
- Hop 8：[证据来源](https://doc.qt.io/qt-6/qsettings.html)
- Hop 9：[证据来源](https://github.com/actions/runner-images/blob/main/images/macos/macos-26-Readme.md)
- Hop 10：[证据来源](https://github.com/jurplel/install-qt-action/blob/master/action/action.yml)

最终通过判定：`true`
