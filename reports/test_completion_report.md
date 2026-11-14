# 大 TIFF 缩略图测试完成报告

2026-11-14 在 Apple Silicon、macOS 27.0.1、Qt 6.12.0 和 Cocoa 上构建并验证 Release。测试使用应用的真实导航条、后台解码和缓存，从独立的起始图观察邻居槽。

| 验收 | 固化测试 | 结果 |
| --- | --- | --- |
| TT01 | `FovelleThumbnailStatic` | 通过。验证大小、签名、类型、位深和像素分流顺序，以及后台解码和版本复验。 |
| TT02 | `testThumbnailLargeTiff` 三个超限数据行 | 通过。编码大小、像素数、两项同时超限分别得到 sRGB 缩略图；顺时针旋转的灰阶条纹方向正确。 |
| TT03 | `testThumbnailLargeTiffRejected` 六个数据行 | 通过。分别覆盖 512 MiB、1 亿像素、32768 单边、16 位样本、JPEG 扩展名伪装和损坏 TIFF 目录。正常邻居继续显示。 |
| TT04 | `testThumbnailReportedSample:jaguar-tiff` | 通过。原图缩略图为 272 × 208，和独立 sips 参考的亮度相关系数为 0.998636。槽位尺寸、sRGB、主图和源文件版本均满足要求。 |
| TT05 | `testThumbnailGalleryLargePng` 两个数据行 | 通过。编码最大（1,385,269,031 字节）的画作与像素最多（729,669,102 像素）的画作均显示预览。 |
| TT06 | `testThumbnailLargeTiff:ordinary` | 通过。普通 RGB TIFF 色阶与槽位尺寸正确。 |
| TT07 | `testThumbnailCorpus` | 独立 Cocoa 进程中 25 张图集样本全部通过。 [图集日志](../build-thumbnail-release/test-results/large-tiff/corpus.log) |
| TT08 | `FovelleThumbnailDynamic`、完整 Cocoa 缩略图矩阵 | 通过。完整矩阵为 67 个数据行或独立用例，加上初始化和清理，共 69 项通过。 [完整矩阵日志](../build-thumbnail-release/test-results/large-tiff/extended-thumbnail.log) |

修复前，对原始画作和三个超旧预算的合成样本均复现空缩略图。修复后均通过。全目录读取头部并核对尺寸与大小，排除了 AppleDouble 伴生文件和 `.DS_Store`，共识别 153 个图像文件。43 张 PNG 超过 256 MiB，仍在现有流式 PNG 准入范围内。像素级核对覆盖上述两张 PNG，其他 PNG 完成头部和预算检查。 [修复前结果](../build-thumbnail-release/test-results/large-tiff/baseline-final-fixtures.log) · [修复后结果](../build-thumbnail-release/test-results/large-tiff/acceptance.log)

默认 CTest 7/7 通过，耗时 57.00 秒，内部快速门禁 45 个阶段通过。独立的 TIFF 和同目录 PNG 验收共 13 个数据行、15 项（含初始化和清理）通过。CI 固定版本的 clang-format 检查和 `git diff --check` 通过。 [默认 CTest 日志](../build-thumbnail-release/test-results/large-tiff/default-ctest.log)

将新用例与旧图集放在同一 Cocoa 进程的一次组合运行中，`sdr_test/9.png` 等待预览发布超时，`hdr_test/1.JPG` 的导航条未显示。独立图集复跑中两者均通过，另一次组合运行中这两个数据行也通过。异常未稳定复现，未定位成因。 [异常组合运行日志](../build-thumbnail-release/test-results/large-tiff/combined-initial.log) · [较早组合运行日志](../build-thumbnail-release/test-results/large-tiff/combined-earlier.log)

本机 QtSvg 二进制的部署版本高于项目的 macOS 15.0 链接目标，构建产生版本警告。本次运行验证覆盖 macOS 27.0.1。大 TIFF 的 ImageIO 内部内存随文件而异，独立探针测得当前样本的峰值 RSS 约 474 MiB。应用保留串行缩略图解码。修复交付到工作区的 Release 构建 `build-thumbnail-release/Fovelle.app`，本机已安装的副本未替换。

## 复现

```sh
cmake --build build-thumbnail-release --target Fovelle fovelle_tests
ctest --test-dir build-thumbnail-release --output-on-failure
FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa \
FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 \
FOVELLE_REPORTED_THUMBNAIL_TIFF='/Volumes/CRYSTAL/画作/GALLERY/141 Jaguar Attacking a Horse - Henri Rousseau 1910.tif' \
FOVELLE_THUMBNAIL_GALLERY='/Volumes/CRYSTAL/画作/GALLERY' \
  build-thumbnail-release/tests/fovelle_tests \
    testThumbnailReportedSample:jaguar-tiff testThumbnailGalleryLargePng
FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa \
FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 \
FOVELLE_THUMBNAIL_CORPUS='/Volumes/CRYSTAL/仓库/Fovelle App' \
  build-thumbnail-release/tests/fovelle_tests testThumbnailCorpus
```

---

# 默认完整测试套件提速完成报告

2026-11-14 在 Apple Silicon、macOS 27.0.1 和 Qt 6.12.0 上使用已构建的 Release 测试程序验证。完整默认 CTest 从 122.74 秒降至 52.23 秒，7/7 目标通过。内部默认门禁执行 45 个阶段，其中包含十个 Qt 套件、真实 HDR 样本和独立色彩回归。编译在计时前完成。 [默认 CTest 日志](../build-thumbnail-release/test-results/full-fast-default.log)

| 编号 | 固化测试 | 结果 |
| --- | --- | --- |
| FT01 | `test_long_regressions_stay_available_in_extended_gate`、`review_fixes_static.py`、CTest 注册检查 | 通过。默认 7 个目标，扩展配置 83 个目标，两种配置均保留原执行超时策略 |
| FT02 | 完整默认 CTest，带原始 JPEG | 7/7 通过，墙钟时间 52.64 秒。 [原图 CTest 日志](../build-thumbnail-release/test-results/full-fast-original.log) |
| FT03 | `FovelleEPSRendererTimeout` | 通过，30.15 秒。应用报告真实生产超时，渲染进程已回收。 [扩展 EPS 日志](../build-thumbnail-release/test-results/full-fast-extended-eps.log) |

启动菜单测试合并到默认进程后，曾把最大化几何写入下一用例。补上测试设置恢复后，两条导航按钮绘制路径均通过。缩略图代表用例由独立 Cocoa 进程执行，保留了原有的焦点断言。生命周期慢启动和平台延迟、完整缩略图矩阵及真实 EPS 超时由扩展配置提供。CI 单独启用的 Sparkle 安装门禁沿用原配置。

```sh
FOVELLE_FIT_REGRESSION_JPEG="$HOME/Downloads/Snipaste_2026-11-14_15-57-50.jpg" \
  /usr/bin/time -p ctest --test-dir build-thumbnail-release --output-on-failure
```

---

# 默认适合窗口的双滚动条测试完成报告

2026-11-14 在 Apple Silicon、macOS 27.0.1、Qt 6.12.0 和 Cocoa 上验证。Debug 与 Release 均构建了应用和 Qt Test。本机 QtSvg 的最低部署版本高于项目的 macOS 15.0 链接目标，构建产生警告，以下测试在当前系统完成。

## 本机精简验收

| 输入 | CTest 目标 | 本机实测耗时 |
| --- | --- | --- |
| 用户原图副本 | 3/3 通过 | 1.47 秒 |
| 同尺寸临时 JPEG | 3/3 通过 | 1.42 秒 |

耗时为已构建的 Release 测试程序从 CTest 启动到退出的墙钟时间。两种输入各运行两个临界尺寸，共用一次 100% 切换、调整尺寸及旋转检查，并各运行一次分数 DPR 重采样。项目完整回归仍可通过默认 CTest 命令运行。 [原图日志](../build-thumbnail-release/test-results/default-fit-quick-original.log) · [临时 JPEG 日志](../build-thumbnail-release/test-results/default-fit-quick-generated.log)

## 修复前后对照

原图与同尺寸的临时 JPEG 均复现。可用视口为 642 × 365 DIP 时，计入 32 DIP 标题栏后，旧场景高 397.583 DIP，超出 397 DIP，最终横向滚动范围为 [0, 17]，纵向为 [-32, -14]。可用视口为 642 × 366 DIP 时，旧场景宽 642.732 DIP，横向范围为 [0, 18]，纵向为 [-32, -15]。修复后，原图在两个尺寸下的两轴范围均为 [0, 0]。 [修复前日志](../build-about-update/test-results/default-fit/baseline-jpeg.log) · [修复后日志](../build-about-update/test-results/default-fit/fixed.log)

| 编号 | 固化测试 | 结果 |
| --- | --- | --- |
| FW01 | `test_fit_uses_one_calculation` | 通过。适配和切换共用浮点目标计算 |
| FW02 | `test_fit_leaves_scrollbar_policies_to_qt` | 通过。默认策略为 AsNeeded |
| FW03 | `testDefaultFitScrollBars:height-overflow` | 通过。横条隐藏且范围为空 |
| FW04 | `testDefaultFitScrollBars:width-overflow` | 通过。纵条隐藏且范围为空 |
| FW05 | `testDefaultFitScrollBars` | 通过。两种边界几何均满足浮点尺寸约束 |
| FW06 | `testDefaultFitScrollBars` | 通过。100% 时两条滚动条可见且均可滚动 |
| FW07 | `testDefaultFitScrollBars` | 通过。返回后即时及稳定时的比例一致，两轴范围为空 |
| FW08 | `testDefaultFitScrollBars` | 通过。调整窗口和旋转后无滚动范围 |
| FW09 | `testDefaultFitScrollBarsAfterRefinement` | 通过。DPR 2.6 下实际替换背板，按原倍率换算的宽度为 643.077 DIP，修复后场景不超过 643 DIP，两轴范围为空 |
| FW10 | `testDefaultFitScrollBars:height-overflow`、`:width-overflow`（原图副本） | 通过。使用原图验证默认打开、100% 切换、调整窗口及旋转 |

精简后每次运行 2 项静态检查、2 个 JPEG 数据行和 1 个重采样场景。既有缩放、滚动范围、小图 1:1 和导航回归中，Qt Test 汇总为 24 项通过（含初始化和清理）。CI 锁定的 clang-format 23.1.3 和 `git diff --check` 通过。 [既有回归日志](../build-about-update/test-results/default-fit/related-graphics.log)

精简前，Release 配置默认注册的 10 个 CTest 目标全部通过，耗时 122.74 秒，包含原图用例、重采样、快速门禁、生命周期和缩略图回归。修复通过工作区构建交付。 [Release CTest 日志](../build-about-update/test-results/default-fit/release-ctest.log)

## 复现命令

```sh
cmake --build build-thumbnail-release --target fovelle_tests
FOVELLE_FIT_REGRESSION_JPEG=/Users/inostarlin/Downloads/Snipaste_2026-11-14_15-57-50.jpg \
  ctest --test-dir build-thumbnail-release -L '^default-fit$' --output-on-failure
```

---

# 缩略图键盘跨可见边缘导航测试完成报告

2026-11-14 在 Apple Silicon、macOS 27.0.1、Qt 6.12.0 和 Cocoa 上验证。应用和 Qt Test 的 Release 构建均成功。构建时，本机 Homebrew 的 QtSvg 触发了部署目标版本差异的链接警告，测试在当前系统完成。

## 修复前后对照

修复前，TK01 缺少独立的键盘移动入口，静态检查失败。Cocoa 的左右回归都在第一次跨边缘时失败。起始浏览位置为 8，左右目标分别为 5 和 11，实际浏览位置均停在 8。目录首尾和滚轮组合的 5 个回归数据行在基线上已通过。 [基线日志](../build-thumbnail-release/test-results/thumbnail-keyboard/baseline.log)

| 编号 | 固化测试 | 修复后结果 |
| --- | --- | --- |
| TK01 | `test_keyboard_movement_and_versioned_activation_are_separate` | 通过。方向键调用焦点移动，确认保留版本化打开入口 |
| TK02 | `testThumbnailKeyboardScroll:left` | 通过。左侧屏外目标可见并居中 |
| TK03 | `testThumbnailKeyboardScroll:right` | 通过。右侧屏外目标可见并居中 |
| TK04 | `testThumbnailKeyboardScroll:right` | 通过。目标已可见的每一步均保持原浏览位置 |
| TK05 | `testThumbnailKeyboardScroll:left` 和 `:right` | 通过。两方向各 7 步均保持原主图，且未启动导航加载 |
| TK06 | `testThumbnailKeyboardBounds:first-closed` | 通过。首项外侧按键保持位置，确认打开索引 0 |
| TK07 | `testThumbnailKeyboardBounds:last-closed` | 通过。末项外侧按键保持位置，确认打开索引 16 |
| TK08 | `testThumbnailKeyboardBounds:first-wraps` | 通过。首项左移到可见的末项，确认打开索引 16 |
| TK09 | `testThumbnailKeyboardBounds:last-wraps` | 通过。末项右移到可见的首项，确认打开索引 0 |
| TK10 | `testThumbnailKeyboardAfterWheel` | 通过。滚轮移到索引 6 后，右移并确认打开索引 7 |
| TK11 | `testThumbnailKeyboardScroll:left` 和 `:right` | 通过。跨边缘后继续移动并确认，分别打开索引 1 和 15 |

新增的 7 个动态数据行全部通过。默认 CTest 共 7 个目标全部通过，耗时 120.57 秒。缩略图静态目标通过 8 项，Cocoa 动态目标共 59 项通过（含初始化和清理），零失败、零跳过。CI 锁定的 clang-format 23.1.3 检查和 `git diff --check` 均通过。 [修复后日志](../build-thumbnail-release/test-results/thumbnail-keyboard/fixed.log) · [CTest 汇总](../build-thumbnail-release/test-results/thumbnail-keyboard/ctest.log)

## 复现命令

```sh
cmake --build build-thumbnail-release --target Fovelle fovelle_tests --parallel 5
ctest --test-dir build-thumbnail-release --output-on-failure
QT_QPA_PLATFORM=cocoa FOVELLE_TEST_SUITE=WindowBehaviorTests FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 \
  build-thumbnail-release/tests/fovelle_tests \
  testThumbnailKeyboardScroll testThumbnailKeyboardBounds testThumbnailKeyboardAfterWheel -v1
```

---

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

---

# WebP 与超大 PNG 缩略图测试完成报告

本机验证日期为 2026-11-13（北京时间）。环境为 Apple Silicon、macOS 27.0.1、Qt 6.12.0，使用 Cocoa 平台插件。测试从普通 PNG 打开缩略图条，检查邻居槽位中后台提交的真实像素。

## 原子验收

| 编号 | 执行入口 | 结果 |
| --- | --- | --- |
| TW01 | `FovelleThumbnailStatic` | 6 项静态检查通过，包含预算、文件身份、队列和构建接线 |
| TW02 | `testThumbnailLargeWebp` | VP8L 与 VP8X 两行通过，验证位置、透明度、颜色、比例和输出范围 |
| TW03 | `testThumbnailStreamingPng` | RGB8 与带 EXIF 6、Display P3 声明的 Adam7 RGBA16 两行通过 |
| TW04 | `testThumbnailStreamingPngRejected` | 文件、面积、单边和截断四行通过，确认占位及正常邻居仍可显示 |
| TW05 | `testThumbnailCorpus` | 25 张实际图片全部通过，包括 10 张 SDR、5 张 HDR、4 张混合目录图片和 6 张截图样本 |
| TW06 | `FovelleThumbnailStatic`、`FovelleThumbnailDynamic` | 两个 CTest 目标均通过。动态 QtTest 记录 52 项通过，含初始化、清理及 8 个新增数据行 |

修复前的 Cocoa 回归复现 6 个失败，覆盖三张实际问题图片、透明 VP8L，以及两种超大 PNG。测试用独立 ImageIO 参考图的 8 × 8 亮度结构和尺寸比例验证三张实际问题图。实际槽位图像为 `5.webp` 288 × 165、`9.png` 152 × 208、`macOS 2.webp` 208 × 208。样本版本戳在各测试前后相同。

## 解码与构建验证

- 对 `9.png`，独立逐行解码探针在最长边 288 像素时输出 211 × 288，耗时 8.47 秒，峰值 RSS 为 28,884,992 字节。直接强制 ImageIO 以 8 倍子采样生成预览的独立探针峰值 RSS 为 1,424,228,352 字节。计时包含本机文件读取，结果受缓存和磁盘影响。该测量针对解码器进程，整套 Cocoa 测试的内存还包含应用渲染、预读和其他样本。
- Debug 与 Release 的应用和测试目标均构建成功，qmake 构建完成。CMake 应用包含 libpng 与 libwebp 许可文件，两项依赖静态链接。两套静态库均生成 arm64 和 x86_64 切片，通过双架构链接探针。
- 项目格式检查、变更 C++ 源码的 clang-tidy 和 `git diff --check` 均通过。Homebrew 的 QtSvg 在链接时提示其最低系统版本为 macOS 27.0。本机运行验收使用 macOS 27.0.1。
- 使用含 PyYAML 与 Pillow 的项目虚拟环境重跑 Release 默认 CTest，7 个目标全部通过，包含快速套件、生命周期检查和缩略图验收。本次环境未启用历史扩展矩阵或更新安装测试。
- 专用 PNG 解码上限为 2 GiB、10 亿像素和 65,536 单边像素。超过上限、损坏或专用路径无法转换的颜色声明保留占位。逐行解码仍须读取完整压缩数据。

## 复现命令

```sh
cmake --build build-thumbnail-release --target Fovelle fovelle_tests --parallel 8
ctest --test-dir build-thumbnail-release -R '^FovelleThumbnail' --output-on-failure

FOVELLE_THUMBNAIL_CORPUS='/Volumes/CRYSTAL/仓库/Fovelle App' \
QT_QPA_PLATFORM=cocoa FOVELLE_TEST_SUITE=WindowBehaviorTests \
FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 \
build-thumbnail-release/tests/fovelle_tests testThumbnailCorpus
```

---

# GitHub Actions 的 libwebp 头文件测试完成报告

验证日期为 2026-11-13（北京时间）。运行 `43430398089` 在 macOS Build 步骤报告 `webp/decode.h` 缺失。保留 runner 的编译和检查设置，修复点为 vendored `webpdecoder` 的 CMake 构建接口。 [原始失败作业](https://github.com/inostarlin-passion/Fovelle/actions/runs/43430398089/job/131902344346)

| 编号 | 执行入口 | 验收证据 |
| --- | --- | --- |
| CIW01 | `FovelleThumbnailStatic` | 修复前两目标解析到 Homebrew 的头文件，两条断言均失败。修复后两目标均解析到 vendored `src/webp/decode.h`，静态套件通过 |
| CIW02 | `tests/thumbnail_webp_compile_test.py`、Release 构建、CTest | 修复前移除 Homebrew 回退后复现 `file not found`。修复后探针成功编译应用和测试目标的 Cocoa 对象。Release 构建和 CI 测试档 7/7 个 CTest 目标通过，含 VP8L、VP8X 两个用例和完整缩略图套件的 52 项记录 |

锁定版本 clang-format 23.1.3 的格式检查、13 项 CI 配置测试及 `git diff --check` 均通过。本机 clang-tidy 23.1.3 显式指定与 Qt 本地依赖一致的 Command Line Tools SDK 后，通过两条 Cocoa 编译命令的检查。

验证分支为 `codex/fix-webp-ci-headers`，远端运行对应代码提交 `71c3a1c`。首轮格式检查通过，macOS 作业因托管 runner 容量不足被取消。重试后，Build、Test、Qt 应用包校验、解包校验及产物上传均通过，工作流最终结论为 success。 [GitHub Actions 验证运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/43434733863)

```sh
python3 tests/thumbnail_webp_compile_test.py build-thumbnail-release
ctest --test-dir build-thumbnail-release -R '^FovelleThumbnail' --output-on-failure
FOVELLE_TEST_PROFILE=ci ctest --test-dir build-thumbnail-release --output-on-failure
```
