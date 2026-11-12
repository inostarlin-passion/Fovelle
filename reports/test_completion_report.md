# GitHub Actions 格式检查修复测试完成报告

首轮验证日期为 2026-11-12。失败运行 `43248926333` 的环境校验通过，锁定版本为 clang-format 23.1.3。格式检查的 38 条诊断可在本机复现。 [原始失败运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/43248926333)

| 编号 | 执行入口 | 结果 |
| --- | --- | --- |
| C01 | `.github/scripts/check_format.py` | 通过。用 clang-format 23.1.3 执行全量检查，修复前退出码 1，修复后退出码 0 |
| C02 | `tests/ci_runner_policy_tests.py` | 通过。13 条政策测试全部通过 |
| C03 | CMake 构建及 `FovelleThumbnail` CTest | 通过。应用和测试目标构建成功，缩略图静态和动态作业均通过。本机以 CI 测试档运行全部 9 个 CTest 目标，也均通过 |
| C04 | `Build Fovelle` 手动触发 | 部分验证。GitHub 格式作业通过。截至 2026-11-12 23:44（北京时间），macOS 构建仍排队，API 显示未分配 runner |

本机与验证提交的源码、测试及工作流一致。托管构建和打包结果仍待 GitHub runner 执行。Clang 原始词法标记对比确认五个修复文件的标记内容与顺序一致。验证提交为 `59989bbf5f843b74d946d71c0888544c398565f8`，验证分支为 `codex/fix-thumbnail-ci`。 [验证运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/43249624290)

## 复现命令

```sh
# 当前 PATH 中的 clang-format 应为 23.1.3，Python 环境应已安装 PyYAML
clang-format --version
python .github/scripts/check_format.py
python tests/ci_runner_policy_tests.py
cmake --build build-about-update --target Fovelle fovelle_tests -j 8
ctest --test-dir build-about-update -R 'FovelleThumbnail' --output-on-failure
FOVELLE_TEST_PROFILE=ci ctest --test-dir build-about-update --output-on-failure
gh run view 43249624290 --json headSha,status,conclusion,jobs
```

---

# 长截图与 RAW 缩略图方向测试完成报告

测试日期为 2026-11-12。环境为 Apple Silicon、macOS 27.0.1、Qt 6.12.0 和 Cocoa。应用与 Qt Test 目标构建成功。

| 编号 | 固化测试 | 结果 |
| --- | --- | --- |
| P01 | `testThumbnailLongPng` | 通过，7200 万像素 PNG 的上下色块、纵横比和尺寸均正确 |
| P02 | `testThumbnailRawOrientation:exif6` | 通过，缩略图与主图同向 |
| P03 | `testThumbnailRawOrientation:exif8` | 通过，缩略图与主图同向 |
| P04 | `testThumbnailReportedSample:fireshot-png` | 通过，原截图有实际像素、色调变化和正确比例 |
| P05 | `testThumbnailReportedSample:sony-arw` | 通过，原始 ARW 缩略图为竖向，像素结构与主图同向 |
| P06 | `testThumbnailReportedSample:nikon-nef` | 通过，原始 NEF 缩略图为横向，像素结构与主图同向 |
| P07 | `test_large_png_exception_is_format_and_subsample_bounded` | 通过，扩容路径具有 PNG 类型、8 倍子采样、位深和像素上限约束 |
| P08 | `test_raw_thumbnail_uses_oriented_decode_graph` | 通过，草稿和普通模式均取已配置方向的 RAW 解码图 |

生产代码改动前，P01 因没有预览失败，P04 因没有预览失败，P05/P06 因缩略图纵横比与主图方向相反而失败。P02/P03 在改动前也通过，作为已有方向行为的回归。P07/P08 在改动前失败。动态用例见 [thumbnail_acceptance.inc](../tests/thumbnail_acceptance.inc)，静态用例见 [thumbnail_static.py](../tests/thumbnail_static.py)。

## 回归结果

P01–P08 全部通过。缩略图套件的 6 条静态测试和 42 条动态用例通过，42 条动态用例在强制 2× 缩放下再次通过。三个原样本在默认缩放及强制 2× 下均通过，无跳过。现有快速回归与 4 组生命周期回归共 5 组全部通过。

## 复现命令

```sh
cmake --build build-about-update --target Fovelle fovelle_tests -j 8
ctest --test-dir build-about-update -R 'FovelleThumbnail' --output-on-failure
QT_SCALE_FACTOR=2 ctest --test-dir build-about-update -R '^FovelleThumbnailDynamic$' --output-on-failure
ctest --test-dir build-about-update -R 'Fovelle(FastTests|LifecycleFix)' --output-on-failure
QT_QPA_PLATFORM=cocoa FOVELLE_TEST_SUITE=WindowBehaviorTests FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 \
  FOVELLE_REPORTED_THUMBNAIL_PNG='/Users/inostarlin/Downloads/FireShot Capture 001 - 油车配置选择建议 - [chatgpt.com].png' \
  FOVELLE_REPORTED_THUMBNAIL_ARW='/Users/inostarlin/Downloads/DSC07019.ARW' \
  FOVELLE_REPORTED_THUMBNAIL_NEF='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/4.nef' \
  build-about-update/tests/fovelle_tests testThumbnailReportedSample -v1
```

## 验证边界

原样本测试在缩略图条中读取临时副本，使用原始文件字节。三个原文件未写入，私有样本不进入仓库。普通 CTest 使用可分发的 PNG 和 DNG 测试数据。RAW 若无法获得定向解码图会显示占位。Homebrew QtSvg 仍报告 macOS 部署目标差异，本轮运行验证使用 macOS 27.0.1。

---

# 缩略图切换过渡与横向浏览测试完成报告

测试日期为 2026-11-12。环境为 Apple Silicon、macOS 27.0.1、Qt 6.12.0 和 Cocoa。应用与测试目标均已成功构建。

| 编号 | 固化测试 | 结果 |
| --- | --- | --- |
| S01 | `testThumbnailTransition` | 通过，提交后出现中间透明度，过渡期间禁用命中，完成后居中 |
| S02 | `testThumbnailWheelBrowse` | 通过，离散轮滚动一格，主图、缩放和滚动位置保持一致 |
| S03 | `testThumbnailScrollMomentum` | 通过，半格像素位移正确，停顿并移出面板后仍由条接收更新和惯性 |
| S04 | `testThumbnailWheelOutside` | 通过，面板外保留原有主图导航 |
| S05 | `testThumbnailScrollBounds` | 通过，首末端限制正确，继续滚动不切图 |
| S06 | `testThumbnailClickAfterScroll` | 通过，点击从屏外滚入的格打开正确路径 |
| S07 | `testThumbnailNativeBrowse` | 通过，原生横移和左滑只改变浏览位置 |
| S08 | `testThumbnailReopenRecenters` | 通过，收起并重新呼出后以当前图居中 |
| S09 | `test_browsing_and_native_transition_wiring` | 通过，滚动路径不触发打开，原生逐帧修改透明度 |
| S10 | `testThumbnailTransitionResize` | 通过，窗口变窄、槽位减少时，切图过渡继续 |
| S11 | `testThumbnailTransitionRetarget` | 通过，连续提交后续接过渡，最后一张居中 |

实现前 S09 因滚动入口和原生新旧层尚未接入而失败。生产实现完成后，默认缩放下的 4 条静态测试和 39 条动态用例全部通过，强制 2× 下 39 条动态用例也全部通过，无跳过。测试代码见 [静态接线](../tests/thumbnail_static.py) 与 [Cocoa 动态用例](../tests/thumbnail_acceptance.inc)。

## 回归结果

S01–S11 全部通过。现有快速回归与 4 组生命周期回归共 5 组全部通过。

## 复现命令

```sh
cmake --build build-about-update --target Fovelle fovelle_tests -j 8
ctest --test-dir build-about-update -R 'FovelleThumbnail' --output-on-failure
QT_SCALE_FACTOR=2 ctest --test-dir build-about-update -R '^FovelleThumbnailDynamic$' --output-on-failure
ctest --test-dir build-about-update -R 'Fovelle(FastTests|LifecycleFix)' --output-on-failure
```

## 验证边界

滚轮和手势使用 Qt 合成事件投递给实际 Cocoa 窗口。物理触控板和不同系统手势配置未进行人工操作验证。强制 2× 属于软件缩放。Homebrew QtSvg 在本机构建中仍产生部署目标版本提示。

---

# 缩略图内容与尺寸修复测试完成报告

测试环境为 Apple Silicon、macOS 27.0.1、Qt 6.12.0、Cocoa 和本机 Ghostscript。测试日期为 2026-11-12。

## 原子验收

| 编号 | 固化测试 | 结果 |
| --- | --- | --- |
| R01 | `testThumbnailCenter` | 通过，标准格 144×104 pt，首末项均居中 |
| R02 | `testThumbnailCompact` | 通过，紧凑格 112×80 pt，392/488 pt 断点符合设计 |
| R03 | `testThumbnailContent:jpeg` | 通过，2400 万像素 JPEG 邻图显示绿色实像 |
| R04 | `testThumbnailContent:png` | 通过，500 万像素 PNG 邻图显示红色实像 |
| R05 | `testThumbnailContent:raw` | 通过，仓库 DNG 样本显示非透明实像 |
| R06 | `testThumbnailContent:eps` | 通过，EPS 缩略图保留黑白图案 |
| R07 | `testThumbnailContent:svg` | 通过，6000×4000 的逻辑画布显示蓝色实像 |
| R08 | `testThumbnailContent:ppm` | 通过，PPM 显示绿色实像 |
| R09 | `testThumbnailContent:corrupt` | 通过，损坏 PNG 保留占位槽 |
| R10 | `test_native_preview_has_independent_admission` | 通过，原生优先、独立预算和回退顺序的静态合同满足 |

所有有效内容用例都断言了输出像素尺寸上限。测试源见 [动态验收](../tests/thumbnail_acceptance.inc) 和 [静态验收](../tests/thumbnail_static.py)。R10 在生产代码变更前因原生和 Qt 回退尚未分离而失败。

## 构建与回归

应用和测试目标构建成功。R01–R10 全部通过。缩略图套件共执行 3 条静态测试和 29 条动态用例，均通过。29 条动态用例在 `QT_SCALE_FACTOR=2` 下再次通过，未跳过用例。现有快速回归和 4 组生命周期回归全部通过。

## 复现命令

```sh
cmake --build build-about-update --target Fovelle fovelle_tests -j 8
ctest --test-dir build-about-update -R 'FovelleThumbnail' --output-on-failure
QT_SCALE_FACTOR=2 ctest --test-dir build-about-update -R '^FovelleThumbnailDynamic$' --output-on-failure
ctest --test-dir build-about-update -R 'Fovelle(FastTests|LifecycleFix)' --output-on-failure
```

## 验证边界

有效格式的测试覆盖上表样本。其他文件仍受系统解码器、文件访问权限和预览预算约束。2× 用例使用 Qt 强制缩放，未测量实际跨屏或不同刷新率下的性能。构建使用的 Homebrew QtSvg 仍有部署目标版本提示。

---

# 视口底部缩略图条测试完成报告

测试日期为 2026-11-12。环境为 Apple Silicon、macOS 27.0.1、Qt 6.12.0 和 Cocoa。使用 CMake 构建了应用和 Qt Test 目标。

## 缩略图验收结果

24 条通过，0 条失败，0 条跳过。包含 2 条静态测试与 22 条动态测试。动态测试在默认缩放与 `QT_SCALE_FACTOR=2` 下均通过。第二轮的强制缩放产生了 Qt 窗口定位警告，Qt 改用主屏继续执行。

| 编号 | 测试代码 | 结果 |
| --- | --- | --- |
| T01 | `testThumbnailCenter` | 通过 |
| T02 | `testThumbnailCompact` | 通过 |
| T03 | `testThumbnailLoop` | 通过 |
| T04 | `testThumbnailHover` | 通过 |
| T05 | `testThumbnailHide` | 通过 |
| T06 | `testThumbnailClick` | 通过 |
| T07 | `testThumbnailInvalidPress` | 通过 |
| T08 | `testThumbnailVersion` | 通过 |
| T09 | `testThumbnailKeyboard` | 通过 |
| T10 | `testThumbnailPreview` | 通过 |
| T11 | `test_build_and_native_overlay` | 通过 |
| T12 | `test_background_budget_and_versioned_foreground` | 通过 |
| T13 | `testThumbnailSingle` | 通过 |
| T14 | `testThumbnailPreference` | 通过 |
| T15 | `testThumbnailBackendHandoff` | 通过 |
| T16 | `testThumbnailSupersededValidation` | 通过 |
| T17 | `testThumbnailVersionChangesDuringRead` | 通过 |
| T18 | `testThumbnailPreferenceDoesNotRescan` | 通过 |
| T19 | `testThumbnailFadeReversal` | 通过 |
| T20 | `testThumbnailFullscreenTransition` | 通过 |
| T21 | `testThumbnailVersionedCacheReuse` | 通过 |
| T22 | `testThumbnailFocusRefreshesDirtyListing` | 通过 |
| T23 | `testThumbnailNativeHDR` | 通过 |
| T24 | `testThumbnailDragOwnership` | 通过 |

静态用例见 [thumbnail_static.py](../tests/thumbnail_static.py)，动态用例见 [thumbnail_acceptance.inc](../tests/thumbnail_acceptance.inc)。T23 使用仓库内的 gain-map JPEG，覆盖原生 HDR 合成。T15 覆盖原生 SDR 与 Qt 绘制移交。

## 回归结果

现有 `FovelleFastTests` 和 4 组 `FovelleLifecycleFix` CTest 全部通过。快速回归包括目录扫描代次、导航、缩放保留、加载器取消、原生菜单、可移植 HDR 和源文件生命周期用例。未运行需要更新安装包的 Sparkle 安装测试。

## 复现命令

```sh
cmake --build build-about-update --target Fovelle fovelle_tests -j 8
ctest --test-dir build-about-update -R 'FovelleThumbnail' --output-on-failure
QT_SCALE_FACTOR=2 ctest --test-dir build-about-update -R '^FovelleThumbnailDynamic$' --output-on-failure
ctest --test-dir build-about-update -R 'Fovelle(FastTests|LifecycleFix)' --output-on-failure
```

在实现前，接线静态测试按预期失败。动态验证中，几何测试最初在主图的延后适配完成前取基线，随后改为在窗口激活和视口稳定后驱动悬停。缓存用例显式保留目标在原有预读需求列表中，使其满足已缓存的前置条件。

## 验证边界

- 本轮对缩放使用单一物理显示器和 Qt 强制缩放。实际跨屏、VoiceOver 实机操作、真实 SMB 与云占位环境，以及 100／10,000 张目录和 60／120 Hz 下的 P95 性能目标仍需实测。
- 预览无法在预算内生成时显示格式占位，部分 RAW 和 EPS 会走此路径。原生层按面板图像更新内容，淡入淡出仅更新透明度。
- 本机构建使用的 Homebrew QtSvg 以 macOS 27.0 构建，链接器提示部署目标为 15.0。当前环境通过构建和运行，macOS 15 的运行结果需要在相应系统上验证。

---

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
