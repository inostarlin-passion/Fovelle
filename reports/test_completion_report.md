# 双图分隔条与初始等宽测试完成报告

仓库基线为 `796e521`。验证环境为 Apple Silicon、macOS 27.0.1、Qt 6.12.0、Cocoa、Release。实现见 [对比分隔器](../src/qvcomparison.cpp) 和 [Cocoa 指示层](../src/qvcocoafunctions.mm)。设计与用例见 [技术设计](technical_design_document.md) 和 [测试规范](test_case_specification.md)。

## 基线复现

同一版新增测试在修复前得到 6 个动态失败、1 个新增静态失败。三种初始窗口宽度分别得到左右 350 / 603、351 / 603 和 251 / 432 个逻辑像素。静止手柄未按要求绘制中央笔画，拖动时现有橡皮筋未进入 Cocoa 根图层的前景。 [基线动态](../build-thumbnail-release/test-results/comparison-divider/baseline-dynamic.log) · [基线静态](../build-thumbnail-release/test-results/comparison-divider/baseline-static.log)

## 逐条结果

| 编号 | 结果 | 已核验行为 |
| --- | --- | --- |
| DIV01 | 通过 | 偶数、奇数和窄窗口均平分扣除分隔条后的可用空间，两列最多差一个逻辑像素。 |
| DIV02 | 通过 | Qt 手柄覆盖实际间隙，抓取的像素显示笔画沿中线对称，边缘为底色。 |
| DIV03 | 通过 | 偏心按下后，临时线的中心仍正确。临时原生层与 HDR 视口相交时高于重叠的窗口根子层。拖动中列宽保持，释放后指示线撤除，最终手柄中心与最后指示位置一致。 |
| DIV04 | 通过 | 丢失鼠标抓取即撤掉指示线，旧释放不会改变宽度，双击恢复等宽。 |
| DIV05 | 通过 | 静态契约覆盖对称最小宽度、合法拖动位置及 Cocoa 指示层入口。 |

应用和测试构建通过，默认 CTest 9/9 通过，双图专项连续三轮通过，`git diff --check` 及 C++ 格式检查通过。 [构建](../build-thumbnail-release/test-results/comparison-divider/build.log) · [默认回归](../build-thumbnail-release/test-results/comparison-divider/default-ctest.log) · [专项重复](../build-thumbnail-release/test-results/comparison-divider/focused-repeat.log) · [静态逐项](../build-thumbnail-release/test-results/comparison-divider/static.log)

## 验证边界

额外以 `QT_SCALE_FACTOR=2` 运行 6 个新增动态用例。Qt 将测试窗口判定为超出已知屏幕，在开启警告即失败时终止。在保留警告并继续执行时，6 个用例均通过。该验证使用模拟缩放与实际 Cocoa 图层树。macOS 15／26 和外接 EDR 显示器的物理呈现仍待对应硬件验证。 [模拟缩放警告](../build-thumbnail-release/test-results/comparison-divider/scale-2.log) · [模拟缩放继续执行](../build-thumbnail-release/test-results/comparison-divider/scale-2-no-fatal.log)

## 复现命令

```bash
cmake --build build-thumbnail-release --target Fovelle fovelle_tests -j 8
ctest --test-dir build-thumbnail-release --output-on-failure
ctest --test-dir build-thumbnail-release -V -R '^FovelleComparison' --repeat until-fail:3
```

---

# 双图对比模式测试完成报告

本轮以 `31f2918` 的生产代码和 [双图方案](design.md) 为基线。在 Apple Silicon、macOS 27.0.1、Qt 6.12.0、Cocoa、Release 下重新构建 Fovelle 与 QtTest。实现位于 [双图界面与控制器](../src/qvcomparison.cpp)，采用 [技术设计及正确性论证](technical_design_document.md) 和 [逐条测试规范](test_case_specification.md)。

## 结果

- 16 条原子验收通过，包括 2 条静态契约和 14 条 QtTest 行为。专项 CTest 连续运行三轮，每轮均无失败和跳过。 [专项日志](../build-thumbnail-release/test-results/comparison/focused-repeat.log) · [静态逐项日志](../build-thumbnail-release/test-results/comparison/static.log)
- 默认 CTest 9/9 通过，耗时 78.93 秒。覆盖已有快速检查、便携 HDR 样本、适合窗口和缩略图回归。 [默认回归日志](../build-thumbnail-release/test-results/comparison/default-ctest.log)
- CMake 成功构建应用和测试。源码格式、简繁中翻译 XML 和 `git diff --check` 通过。本机 QtSvg 最低系统版本为 27.0，连接器仍报告与 15.0 部署目标的版本差异。 [构建日志](../build-thumbnail-release/test-results/comparison/build.log)

| 编号 | 执行结果 | 已核验行为 |
| --- | --- | --- |
| CMP01 | 通过 | 入口随图片就绪变化，Esc 恢复单图和进入前倍率。 |
| CMP02 | 通过 | 跨目录 B 提交，A 不变，两视口不重叠。 |
| CMP03 | 通过 | 两列应用共同因子，倍率饱和保留比例，反向立即响应。 |
| CMP04 | 通过 | 关闭缩放联动后 B 改变倍率且 A 的视口变换不变。 |
| CMP05 | 通过 | 源端剩余 20 像素时目标移动 10，回拖立即响应，无滚动范围的源轴不带动另一列，关闭联动后目标不动。 |
| CMP06 | 通过 | 不等宽交换两次恢复位置，焦点和手动倍率随图移动。 |
| CMP07 | 通过 | 坏文件保留原 B，随后最新请求提交到交换后的位置。 |
| CMP08 | 通过 | 退出并重进时旧候选无法回填，普通打开可继续使用。 |
| CMP09 | 通过 | 超大 SVG 被准入检查拒绝，原 B 保留。 |
| CMP10 | 通过 | HDR 增益图交换后原生裁切坐标与 Qt 视口一致，原生内容代次保持。 |
| CMP11 | 通过 | 菜单、同步选项、文件选择、无障碍名称与构建接入存在。 |
| CMP12 | 通过 | 串行工作队列、解码前后版本检查和提交前请求失效检测存在。 |
| CMP13 | 通过 | 合成手势跨栏仍由起始栏处理，交换后旧输入失效，新序列可接管。 |
| CMP14 | 通过 | 联动的 100% 应用于两列，关闭后只有活动 B 回到适合视口。 |
| CMP15 | 通过 | 移除 B 后交换禁用，请求取消并拒绝迟到结果。 |
| CMP16 | 通过 | 9000 × 5000 JPEG 经受限路径显示有标记的 SDR 预览，仍用源尺寸计算适合倍率。 |

## 回溯与未验证项

基线的两条新增静态契约失败，验证了测试能识别缺失的对比路径。第一次完整回归发现，旧缩放源代码契约检查单图菜单直达原视口入口。生产代码保留这条调用，并为对比增加活动视口分支，原有静态与动态回归随后均通过。

已测的是本机 Cocoa、候选提交、原生几何和已有 HDR 样本。macOS 15／26 实机、多屏及双 EDR 的最终亮度、真实触控板惯性、VoiceOver 播报、显示器可见帧 P95 和进程／GPU 峰值未实测。512 MiB 是准入估算阈值。候选需有可确定尺寸，编码文件上限 64 MiB。超预算普通 JPEG 可尝试受限 SDR 预览，缺少受限解码能力的文件保留当前画面并报告原因。

## 复现命令

```bash
cmake --build build-thumbnail-release --target Fovelle fovelle_tests -j 8
ctest --test-dir build-thumbnail-release --output-on-failure
ctest --test-dir build-thumbnail-release -V -R '^FovelleComparison' --repeat until-fail:3
```

---

# 文件操作、缓存、URL 和异步预查测试完成报告

验证基线为 `6c26ba4`。最终本机环境为 Apple Silicon、macOS 27.0.1、Qt 6.12.0、Ghostscript 10.08.0 和 Cocoa，使用 Debug、默认 CTest 选择并启用更新安装测试。项目的格式检查使用固定的 clang-format 23.1.3。本机 Homebrew QtSvg 的最低系统版本是 27.0，构建保留对应链接警告。

## 基线复现

四项新源码契约在基线均失败。动态基线确认重命名替换、两层旧缓存、读取中替换、两种 URL 格式误判及两个前台提交时序问题。合法 EPS 基线已加载出矢量图，测试第一次把关闭窗口误作对象销毁而过早检查租约。修正后，清理断言在窗口对象析构、文件句柄关闭之后执行。 [基线静态](../build-about-update/test-results/file-io/baseline-static.log) · [基线缓存](../build-about-update/test-results/file-io/baseline-cache.log) · [基线窗口](../build-about-update/test-results/file-io/baseline-window.log)

## 原子验收

| 编号 | 代码用例 | 核验结果 |
| --- | --- | --- |
| IO01 | `testRenameKeepsConfirmedIdentity:replacement` | 通过。替换图留在原路径，未产生新名，应用显示拒绝提示。 |
| IO02 | `testRenameKeepsConfirmedIdentity:unchanged` | 通过。新路径保存原像素，窗口打开新文件。 |
| IO03 | `test_IO03_rename_checks_original_directory_entry` | 通过。读取目录项身份，改名前验证，成功之后才发出新路径。 |
| IO04 | `testCacheDetectsSameMetadataReplacement:bitmap` | 通过。普通请求交付新版本和蓝色像素。 |
| IO05 | `testCacheRetriesReplacementDuringRead` | 通过。门闩内替换触发第二次读取，只交付一次且为新图。 |
| IO06 | `testCacheDetectsSameMetadataReplacement:eps-reload` | 通过。强制重载返回新的矢量和蓝色像素。 |
| IO07 | `test_IO07_caches_compare_strong_worker_versions` | 通过。验证两层版本、工作线程复验和结果发布前的重试分支。 |
| IO08a | `testDownloadUsesFinalImageFormat:redirect-to-png` | 通过。302 后按最终 PNG 路径交付并打开，源字节保留。 |
| IO08b | `testDownloadUsesFinalImageFormat:png-at-eps-url` | 通过。识别 PNG 内容并修正强制 EPS 的后缀。 |
| IO08c | `testDownloadUsesFinalImageFormat:genuine-eps` | 通过。EPS 文件名、原始字节、矢量渲染和租约清理均符合预期。 |
| IO09 | `test_IO09_redirect_and_file_publication_are_ordered` | 通过。验证最终响应地址和暂存文件的关闭、改名及发布顺序。 |
| IO10 | `testOpenAndNavigationYieldForFileProbe:superseded-open` | 通过。两个前台席位被占用期间界面事件继续，解除阻塞后只交付最后一次打开。 |
| IO11 | `testOpenAndNavigationYieldForFileProbe:missing-neighbor` | 通过。阻塞期间界面可处理事件，恢复后跳过已删除的邻居并打开下一张。 |
| IO12 | `testAsyncDirectoryOpen` | 通过。从目录入口打开排序后的第一张有效图。 |
| IO13 | `test_IO13_metadata_is_worker_owned` | 通过。文件分类、导航候选、缓存复验、物理目录根和标题元数据的获取符合线程约定。 |

[静态结果](../build-about-update/test-results/file-io/final-static.log) · [主图用例 XML](../build-about-update/test-results/fast/ImageLoaderTests.xml) · [窗口用例 XML](../build-about-update/test-results/fast/WindowBehaviorTests.xml)

## 最终回归

| 检查 | 结果 |
| --- | --- |
| 新增验收 | 11 条动态场景、4 项源码契约均通过，零失败、零跳过。 |
| 默认 CTest | 9/9 通过，109.59 秒，含 Sparkle 安装与缩略图矩阵。 |
| 补充回归 | 16 条动态场景通过，含缓存保留与 EPS 缓存复用、8 个首尾导航和别名缩放场景，以及原始下载内容、APNG、清理和超时。零失败、零跳过。 |
| 构建与格式 | 应用及测试目标构建成功。固定版本格式检查、四套翻译目录解析和 `git diff --check` 均通过。 |

[默认 CTest](../build-about-update/test-results/file-io/final-ctest.log) · [补充缓存](../build-about-update/test-results/file-io/final-extra-cache.log) · [补充导航](../build-about-update/test-results/file-io/final-extra-navigation.log) · [补充下载](../build-about-update/test-results/file-io/final-extra-download.log) · [构建日志](../build-about-update/test-results/file-io/final-build.log)

磁盘验证使用可控工作池席位和实际本地文件。真实 SMB 或离线卷的阻塞时间和文件服务器元数据粒度尚未测量。本地 Debug 构建的结果对应上述系统和依赖组合。

---

# 缩略图六项修复测试完成报告

使用 Apple Silicon、macOS 27.0.1、Qt 6.12.0 和 Cocoa 的 Release 构建。动态测试以缩略图条尚未打开的邻居为观察对象。临时样本由测试销毁，真实图库样本通过临时目录引用并复核源文件版本。

| 验收 | 固化测试 | 核验内容 |
| --- | --- | --- |
| TH01a–TH01c | `testThumbnailLiveListing` 三行 | 新增、删除、向原本无图的递归子目录加入文件。 |
| TH02a–TH02b | `testThumbnailLiveReplacement` 两行 | 原地覆盖与原子替换均更换可见像素和版本。 |
| TH03 | `test_live_subscription_and_file_version` | 事件、重扫、取消旧订阅和读前读后版本核对贯通。 |
| TH04–TH05 | `testThumbnailOffscreenDecode`、`test_decode_uses_two_independent_bounded_slots_and_cancels_orphans` | 滚出 EPS 后终止模拟渲染进程，新可见 PNG 在限时内出现。 |
| TH06–TH08 | `testThumbnailClickWithBusySharedPool`、`testThumbnailValidationRetry`、`testThumbnailLateValidation`、`test_metadata_admission_and_timeout_cancellation` | 共享线程全满时仍能点击。校验接纳满载和实际读阻塞两种超时均可重试。迟到的旧结果无切图。 |
| TH09a–TH09b | `testThumbnailExpandedWebp`、`testThumbnailLargeTiff` 与 `testThumbnailLargeTiffRejected` | 4200 万像素 WebP、编码大小超旧阈值及像素超旧阈值的整数 16 位 TIFF 均得到预览。高位深输入超估算预算时显示占位。 |
| TH10 | `testThumbnailEmbeddedRaw`、`test_over_budget_raw_is_embedded_only` | 6800 万像素 DNG 使用已有的 128 × 64 预览，方向 6 后蓝左红右。超预算分支禁止触发传感器解码。 |
| TH11 | `testThumbnailPreviewStates` | 加载、读取失败、超预算的状态可区分。超缩略图预算的 SVG 能从占位格打开主图。 |
| TH12–TH13 | `testThumbnailLoopContinuity`、`testThumbnailLoopDeduplication` | 跨首尾滚动保持位移和主图，两图目录逆向滚动仍连续且无重复格。 |
| TH14a–TH14b | `testThumbnailIncrementalDrawing` 四行 | 分别核验 Qt 与原生绘制的分数滚动、受影响格回填、上传计数和成品像素。 |
| TH15 | `test_native_content_and_geometry_can_change_independently`、现有 Qt 和原生交接、HDR、切图过渡和全屏用例 | 栅格与布局分离。覆盖层保持裁剪、命中、动画和后端交接。 |

修复前的 Cocoa 探针复现了目录和像素过期、屏外 EPS 阻塞、校验超时无法立即重试和循环边缘跳变。新增五项源码检查在原版本均未通过。初次组合运行在淡出反转用例开始失去 Cocoa 窗口焦点，后续用例受影响。最终测试夹具显式激活测试窗口，保留原有的窗口活动、动画和结果断言。 [修复前探针](../build-thumbnail-release/test-results/thumbnail-repair/baseline-probes.log) · [修复前静态检查](../build-thumbnail-release/test-results/thumbnail-repair/baseline-static.log) · [中间焦点异常记录](../build-thumbnail-release/test-results/thumbnail-repair/extended-focus-interrupted.log)

## 最终结果

| 验证 | 结果 | 日志 |
| --- | --- | --- |
| 默认 CTest | 7/7 通过，耗时 70.34 秒。缩略图静态检查 14 项通过，动态代表矩阵 45 项通过。 | [默认回归](../build-thumbnail-release/test-results/thumbnail-repair/final-ctest.log) · [逐项日志](../build-thumbnail-release/test-results/thumbnail-repair/final-ctest-detail.log) |
| 完整 Cocoa 缩略图矩阵 | 89 项通过，耗时 43.21 秒。TH01a 至 TH15 全部通过，另覆盖原有交互与解码回归。 | [扩展矩阵](../build-thumbnail-release/test-results/thumbnail-repair/final-extended.log) |
| 本地真实图库 | 25 张图集样本通过。独立验收的 TIFF 和两张超大 PNG 全部通过。TIFF 输出 272 × 208，与独立参考亮度相关系数 0.998636。 | [真实图集](../build-thumbnail-release/test-results/thumbnail-repair/final-corpus.log) · [大文件样本](../build-thumbnail-release/test-results/thumbnail-repair/final-gallery.log) |

动态矩阵计数包含初始化和清理，均无失败或跳过。一像素滚动在两种路径都产生 0 次背景重栅、0 次单格重栅和 0 次原生上传。覆盖同一邻居时只重栅该格的等待和就绪两帧，Qt 背景重栅为 0 次，原生上传为 2 次。Qt 控件和 WindowServer 原生窗口取样均保留上下色带方向。

Release 应用及测试程序构建完成。CI 固定版 clang-format 与 `git diff --check` 均通过。本机 Homebrew QtSvg 构建目标高于项目声明的 macOS 15.0，链接仍有部署版本提示。运行验证覆盖 macOS 27.0.1 的本地目录。真实 SMB、云占位、跨出监控目录树的符号链接变更、VoiceOver、物理跨屏和 120 Hz 呈现均待实机验证。 [Release 构建](../build-thumbnail-release/test-results/thumbnail-repair/verified-build.log)

```sh
cmake --build build-thumbnail-release --target Fovelle fovelle_tests --parallel 4
ctest --test-dir build-thumbnail-release --output-on-failure
cmake -S . -B build-thumbnail-release -DFOVELLE_EXTENDED_TESTS=ON
ctest --test-dir build-thumbnail-release -R '^FovelleThumbnail' --output-on-failure
```

---

# SVG 拖动性能测试完成报告

2026-11-17 在 Apple M3 Pro、macOS 27.0.1、Qt 6.12.0 和 2× 显示器上验证 Release。使用修改前复制的同一工作区 bundle 与最终 bundle，交替进行三轮原生全屏 32 步拖动。每轮分别计算应用绘制 p50 和 p95，再各取三轮中位数。

| 验收 | 固化测试 | 结果 |
| --- | --- | --- |
| SV01 | `vector_performance_static.py` | 7 项通过，覆盖共享对齐、预算、SVG 视框裁剪、异步色彩转换和全视口重绘。 [静态日志](../build-thumbnail-release/test-results/svg-pan/final-static.log) |
| SV02 | `testSvgPanPixelGrid` | 无显式视框、偏移视框页边、偏移视框旋转三行通过。修复前前两行有密度误差，修复后整数像素平移重用瓦片，页外标记保持不可见。 |
| SV03 | `testSvgPanPartialTile` | 通过。修复前透明重用区显示洋红预览，修复后显示绿色背景，图形及裸露带分别保留白色和洋红。 |
| SV04 | `testVectorDragFrameBudgetForEPSAndSVG`、`testVectorPanRepaintsOnlyExposedStrip`、`testVectorPaintClearsStalePixelsBeforeTileReady` | 通过。使用指定 SVG 和 EPS，鼠标拖动首帧均覆盖整个视口，透明像素检查无残留，释放后恢复最小更新并完成细化。 [窗口日志](../build-thumbnail-release/test-results/svg-pan/final-drag.log) |
| SV05 | `vector_pan_native.py --repeat 3 --require-opaque-copy` | 通过。三轮均通过全屏终点、退出锚点和窗口几何检查。基线 132 次绘制均未对齐；候选 136 次绘制全部对齐并覆盖不透明瓦片。p50 从 6.25 ms 降至 3.22 ms（约 49%），p95 从 7.99 ms 降至 5.31 ms（约 34%）。 [原生对照日志](../build-thumbnail-release/test-results/svg-pan/final-native.log) |
| SV06 | `testSvgPanOpaqueTile` 和静态检查 | 1.0、0.5 不透明度两行均匹配独立合成参考，绘制后合成模式复原。原生日志确认实际样本使用不透明覆盖分支。 |

首次实现仅启用像素对齐，三轮对照虽全数走对齐路径，但 p50 为 5.92 → 6.05 ms，p95 为 7.46 → 7.57 ms。最终实现加入 worker Alpha 检查和省略已被完整覆盖的背景绘制，达到 SV05 的改善标准。记录来自应用绘制事件，显示器实际呈现帧率超出本次测量范围。 [首次实现对照](../build-thumbnail-release/test-results/svg-pan/green-native.log)

修复前的像素回归有 3 项失败。最终图元与颜色回归 28 项通过，窗口回归 6 项通过，两组均含初始化和清理。默认 CTest 7/7 通过，用时 56.92 秒。CI 固定版本 clang-format 和 `git diff --check` 通过。本机 QtSvg 链接仍产生 macOS 15.0 部署目标的版本警告。 [修复前 Qt 日志](../build-thumbnail-release/test-results/svg-pan/red-dynamic.log) · [最终 Qt 日志](../build-thumbnail-release/test-results/svg-pan/final-dynamic.log) · [默认 CTest 日志](../build-thumbnail-release/test-results/svg-pan/final-ctest.log)

## 复现

```sh
cmake --build build-thumbnail-release --target Fovelle fovelle_tests fovelle_native_drag_helper
ctest --test-dir build-thumbnail-release --output-on-failure

sample='/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/7.svg'
QT_QPA_PLATFORM=cocoa FOVELLE_TEST_SUITE=ImageLoaderTests \
  build-thumbnail-release/tests/fovelle_tests \
  testSvgPanPixelGrid testSvgPanPartialTile testSvgPanOpaqueTile
QT_QPA_PLATFORM=cocoa FOVELLE_TEST_SUITE=GraphicsViewTests FOVELLE_SVG_SAMPLE="$sample" \
  build-thumbnail-release/tests/fovelle_tests testVectorDragFrameBudgetForEPSAndSVG
python3 tests/vector_pan_native.py \
  --helper "$PWD/build-thumbnail-release/tests/fovelle_native_drag_helper" \
  --app "$PWD/build-thumbnail-release/Fovelle.app" \
  --baseline-app "$PWD/build-thumbnail-release/test-results/svg-pan/Baseline.app" \
  --image "$sample" --repeat 3 --require-opaque-copy
```

---

# EPS 拖动性能测试完成报告

2026-11-17 在 Apple M3 Pro、macOS 27.0.1、Qt 6.12.0 上验证 Release。原生对照使用相同的 2× 显示器、同一份 `sdr_test/6.eps`、同一套系统输入和相同应用日志开关。

| 验收 | 固化测试 | 结果 |
| --- | --- | --- |
| EP01 | `vector_performance_static.py` | 6 项通过。修复前的静态用例缺少 PDF 对齐契约。 [静态日志](../build-thumbnail-release/test-results/eps-pan/final-static.log) |
| EP02 | `testEpsPanPixelGrid` | 内部、页面边缘、旋转三行通过。修复前前两行因栅格密度误差失败。 |
| EP03 | `testEpsPanPartialTile` | 通过。旧帧处于同一透明区域时，修复前显示洋红预览，修复后显示正确的绿色背景；裸露带继续使用预览。 |
| EP04 | `testEpsPanPaintBudget` | 指定 EPS 的热缓存、跨界 CPU p95 分别为 3.02 ms、3.07 ms，栅格参考为 1.78 ms，验收门槛为 8.33 ms。 |
| EP05 | `testVectorDragFrameBudgetForEPSAndSVG`、`testVectorPanRepaintsOnlyExposedStrip`、`testVectorPaintClearsStalePixelsBeforeTileReady` | 全部通过。EPS/SVG 首帧覆盖率均为 100%，合成检查各有 0 个残留像素，释放后恢复最小更新且细化完成。 [Qt 拖动日志](../build-thumbnail-release/test-results/eps-pan/final-drag.log) |
| EP06 | `vector_pan_native.py --baseline-app …` | 通过。两版均收到 32 步系统拖动输入并通过全屏几何断言。对齐采样帧数从 0/32 增至 34/37。应用绘制耗时中位数从 10.71 ms 降到 5.27 ms，p95 从 25.40 ms 降到 10.97 ms。多个绘制帧可来自同一步鼠标输入。 [配对日志](../build-thumbnail-release/test-results/eps-pan/final-native.log) |
| EP07 | `fovelle_native_drag_helper` 加 `3.jpeg` | 通过。接近底部、退出锚点、恢复窗口几何均正确。 [JPEG 日志](../build-thumbnail-release/test-results/eps-pan/final-native-jpeg.log) |

指定样本的图像加载和矢量相关测试共 18 项通过，Qt 拖动组共 5 项通过，合计含两组初始化和清理。默认 CTest 7/7 通过，用时 55.81 秒。CI 固定版本 clang-format 和 `git diff --check` 通过。 [修复前动态日志](../build-thumbnail-release/test-results/eps-pan/red-dynamic.log) · [最终图像测试日志](../build-thumbnail-release/test-results/eps-pan/final-dynamic.log) · [默认 CTest 日志](../build-thumbnail-release/test-results/eps-pan/final-ctest.log)

指定样本在旧版的两个像素测试中也失败。旧断言直接比较带透明度的 PDF 渲染和已与视口背景合成的图元；现将独立参考转为 sRGB 并与同一背景合成，同时保留原像素容差和裁剪检查。 [旧版像素日志](../build-thumbnail-release/test-results/eps-pan/baseline-existing-pixels.log)

离屏 QImage 微基准中，修复前热缓存、跨界 CPU p95 分别为 0.84 ms、1.30 ms；修复后离屏耗时增加，两组仍在验收预算内。原生窗口耗时按应用绘制日志计算，显示器实际呈现帧率超出本次测量范围。构建时 QtSvg 产生本机系统版本高于 macOS 15.0 部署目标的既有链接警告。

## 复现

```sh
cmake --build build-thumbnail-release --target Fovelle fovelle_tests fovelle_native_drag_helper
ctest --test-dir build-thumbnail-release --output-on-failure

sample='/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/6.eps'
FOVELLE_TEST_SUITE=ImageLoaderTests FOVELLE_EPS_SAMPLE="$sample" QT_QPA_PLATFORM=cocoa \
  build-thumbnail-release/tests/fovelle_tests \
  testEpsPanPixelGrid testEpsPanPartialTile testEpsPanPaintBudget

python3 tests/vector_pan_native.py \
  --helper "$PWD/build-thumbnail-release/tests/fovelle_native_drag_helper" \
  --app "$PWD/build-thumbnail-release/Fovelle.app" \
  --baseline-app "$PWD/build-install-release/Fovelle.app" \
  --image "$sample"
```

---

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
