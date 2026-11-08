# GitHub Actions Universal 发布构建测试完成报告

## 原子验收结果

| 标准 | 用例 | 结果 |
|---|---|---|
| A01 两种架构的 `BOOL` 可编译为快照字段 | S01 | 通过。修复前，静态断言因缺少显式转换而失败。修复后，导航静态脚本 4 项全部通过。 |
| A01 两种架构的 `BOOL` 可编译为快照字段 | S02 | 通过。修复前，编译真实翻译单元复现 `signed char` 到 `bool` 的列表初始化窄化错误，诊断与远端一致。修复后，带 `-Werror=c++11-narrowing` 编译成功，`lipo -archs` 确认目标文件包含 x86_64 与 arm64。[原失败 job](https://github.com/inostarlin-passion/Fovelle/actions/runs/42481278343/job/128736201193) |
| A02 原生 `NO` 读取为 `false` | D01 | 通过。临时 Cocoa 图层树中按钮可见，快照读取为 `false`。 |
| A03 原生 `YES` 读取为 `true` | D02 | 通过。临时 Cocoa 图层树中按钮可见，快照读取为 `true`。 |
| A03 原生 `YES` 读取为 `true` | D03 | 通过。两组 PNG 集成用例均通过，每组核验前进和后退按钮的组透明度、可见性、不透明度、箭头方向和颜色。 |

## 本地回归

- macOS 27.0.1 arm64、SDK 27.0、Qt 6.12.0，CMake 4.4.2，部署目标 macOS 15.0。完整本地构建成功。链接器报告本机 Homebrew QtSvg 以较新系统构建，因此本地运行结果用于验证功能。
- `FOVELLE_TEST_PROFILE=ci` 下 CTest 7/7 通过，用时 117.53 秒，包含快速套件的 45 个阶段、生命周期、原生 About 和更新安装测试。
- clang-format 23.1.3、对修改后探针执行的 clang-tidy 23.1.3（警告视为错误）和 `git diff --check` 均通过。CI 策略测试 13/13，发布流水线测试 7/7。

## GitHub Actions 验证

2026-11-09，独立分支 `codex/fix-universal-release-bool` 的提交 `8bfaa21` 通过 macOS 15 Release Compatibility 的 Build 与 Test 步骤。诊断产物确认实际环境为 Xcode 16.4、SDK 15.5 和 Qt 6.11.2，快速套件 45 个阶段通过。原生 QtTest XML 确认 `disabled`、`enabled` 及两组真实原生按钮用例均执行并通过。[兼容工作流](https://github.com/inostarlin-passion/Fovelle/actions/runs/42497158272) · [诊断产物](https://github.com/inostarlin-passion/Fovelle/actions/runs/42497158272/artifacts/13623744616)

兼容包的打包和验收步骤也通过，工作流最终状态为 `success`。`contract.json` 和 `smoke.json` 均报告通过，覆盖双架构产物合同、应用启动、Ghostscript 以及 PNG、SVG、EPS 解码。[兼容包验收产物](https://github.com/inostarlin-passion/Fovelle/actions/runs/42497158272/artifacts/13624417216)

正式标签 `v1.3.7` 仍指向原失败提交 `73b536f`。签名、公证及正式发布未执行。

## 复现命令

```bash
cmake -S . -B build-about-update -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-about-update --parallel 4
python3 tests/navigation_button_style_static.py
python3 tests/native_titlebar_probe_compile_test.py build-about-update
QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 \
  FOVELLE_TEST_SUITE=WindowBehaviorTests ./build-about-update/tests/fovelle_tests \
  testNativeNavigationGroupOpacity \
  testNavigationButtonsUseDarkArtwork:native-white-left \
  testNavigationButtonsUseDarkArtwork:native-white-right
QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 \
  FOVELLE_TEST_PROFILE=ci ctest --test-dir build-about-update --output-on-failure
```

复用包含 `BUILD_TESTS=ON`、`FOVELLE_REQUIRE_PORTABLE_HDR=ON` 和 `FOVELLE_ENABLE_UPDATE_INSTALL_TESTS=ON` 的本地 CMake 配置。快速套件结果保存在 `build-about-update/test-results/fast/`，完整 CTest 日志保存在 `build-about-update/Testing/Temporary/LastTest.log`。
