# 图片导航边界缩放测试完成报告

## 原子验收结果

| 标准 | 用例 | 修复后结果 |
|---|---|---|
| A01 首张请求上一张保留缩放 | S01、D01 | 通过。非循环边界在候选查找前返回。原文件名和不同大小写路径两行均保留 175%。 |
| A02 末张请求下一张保留缩放 | S01、D02 | 通过。原文件名和不同大小写路径两行均保留 175%。 |
| A03 单图目录请求上一张保留缩放 | S01、S02、D03 | 通过。关闭循环和开启循环两行均保留 175%。静态检查确认同索引保护要求当前路径非空。 |
| A04 单图目录请求下一张保留缩放 | S01、S02、D04 | 通过。关闭循环和开启循环两行均保留 175%。 |
| A05 非循环边界发送对应方向的提示信号 | D01–D04 | 通过。六行非循环请求均发送一次正确方向的提示信号，两行单图循环请求均为零次。 |
| A06 非循环边界发送幻灯片停止信号 | D01–D04 | 通过。六行非循环请求均发送一次停止信号，两行单图循环请求均为零次。 |
| A07 切换到不同文件时应用重置缩放设置 | D05 | 通过。普通下一张和循环上一张均成功打开第二张并恢复适合窗口。 |
| A08 切换到不同文件时应用保留缩放设置 | D06 | 通过。普通上一张和循环下一张均成功打开第一张并保留手动缩放 175%。 |

D01–D04 的全部八行也验证了当前文件路径、手动缩放模式和视图变换保持一致，文件和缩放变更信号均为零次。

## 修复前后对比

- 修复前，S01 和 S02 均失败。动态测试的 12 行中有四行失败，分别是 `first-previous-alias`、`last-next-alias`、`single-loop-previous-alias` 和 `single-loop-next-alias`。四行都在请求后从 175% 重置到约 66.7%。其余八行通过。
- 修复后，静态脚本 7 项通过，新增动态测试 12 行通过，零跳过。现有边界提示测试也通过，覆盖方向、本地化、显示时序和循环切换。

## 回归与验证范围

- 本机使用 macOS 27.0.1 arm64、macOS SDK 27.0、Qt 6.12.0 和 CMake 4.4.5。沿用 `build-about-update` 的本地配置，包含便携 HDR、About 更新和更新安装测试。
- 应用及测试目标构建成功。链接器提示本机 QtSvg 使用了比项目部署目标 macOS 15.0 更新的系统版本。本次运行结果适用于上述本机环境。
- CTest 7/7 通过，总用时 123.29 秒。其中默认快速门禁 45/45 阶段通过，快速门禁的 QtTest XML 确认执行了新增的全部 12 行。
- 仓库 C++ 格式检查、修改区间的 `.inc` 格式检查和 `git diff --check` 均通过。

## 复现命令

```bash
cmake --build build-about-update --parallel 4
python3 tests/navigation_boundary_hint_static.py
export QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1
FOVELLE_TEST_SUITE=GraphicsViewTests ./build-about-update/tests/fovelle_tests \
  testBoundaryNavigationKeepsZoom testSuccessfulNavigationAppliesZoomPreference
FOVELLE_TEST_SUITE=WindowBehaviorTests ./build-about-update/tests/fovelle_tests \
  testNavigationBoundaryHintFeedbackAndAppearance
FOVELLE_TEST_PROFILE=ci ctest --test-dir build-about-update --output-on-failure
```

定向结果保存在 `build-about-update/test-results/navigation-boundary-zoom/`，其中 `baseline.xml` 和 `fixed.xml` 分别记录修复前后的动态结果，`boundary-hint.xml` 记录提示回归。完整日志位于 `build-about-update/Testing/Temporary/LastTest.log`，快速门禁摘要位于 `build-about-update/test-results/fast/summary.json`。
