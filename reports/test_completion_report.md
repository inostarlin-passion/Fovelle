# 2026-10-10 持久化回归修正：Keep zoom level

## 结论与根因

已修复：启动迁移把活动偏好 `navresetszoom` 当作已退役选项，每次强制写成 true；UI 勾选保存的 false 因而在重启时被覆盖。生产改动仅从 `removedPreferenceDefaults` 删除这一项，保留原默认、反向绑定与其它旧设置迁移。

此前测试只覆盖同进程重建设置对话框，普通测试启动也没执行生产启动迁移，无法证明重启持久化。之前“持久化通过”的结论超出了测试证据范围；本轮用迁移数据行及共享实际存储的独立进程测试补齐。

## 验收与结果

| 检查 | 修复前 | 修复后 |
|---|---|---|
| KZ-P1 静态：活动键不在退役重置表 | 失败，发现 navresetszoom | 通过 |
| KZ-P2 已开启 false，经两次启动迁移 | 失败，false 被改为 true | 通过，磁盘/全新 manager/UI 一致 |
| KZ-P2 已关闭 true | 通过 | 通过，未强制开启所有用户 |
| KZ-P2 缺省键 | 失败，被迁移强行写入 | 通过，无键仍由默认true解释为不保持 |
| KZ-P2 其它已退役策略 | 仍按既有逻辑重置 | 通过，navigationregionsenabled仍归一化为false |
| KZ-P3 INI 独立进程勾选并重启读回两次 | 失败，写进程勾选成功，读进程取消 | 通过，checked=true/storedReset=false |
| KZ-P3 macOS原生后端独立进程 | 同样失败 | 同样通过 |
| KZ-P4 取消勾选并再次重启，两个后端 | 原流程在更早勾选保持断言已失败 | 完整序列通过，checked=false/storedReset=true |

修复前两个新增 CTest 条目均失败：动态主体五行中四行失败、一行通过。修复后相关六项 CTest **6/6 通过**（17.97 秒）；完整 CTest **41/41 通过**（340.68 秒）。详细动态运行 **7 passed, 0 failed, 0 skipped**，其中包含两项 Qt 初始化/清理，实际数据行五行。最终格式整理后再构建并重跑两项持久化门禁；结果见 `final-tests.log`。

双后端读写序列均为：`seed(取消) → enable(勾选、磁盘false) → read(仍勾选) → read(仍勾选) → disable(取消、磁盘true) → read(仍取消)`。每个阶段为独立进程，写入/读取均检查 NoError。子进程使用生产的 `migrateOldSettings` 和真实 `QVOptionsDialog`，按生产顺序先迁移再构造应用；不是直接写入一个期望值后假装重启，也不是以同进程QSettings新对象代替独立进程。

## 复现、证据与范围

环境同前一轮：arm64、macOS 27.0.1、Qt 6.11.2、Release、部署目标15.0。INI使用临时共享目录；原生后端使用UUID专属偏好域；scope guard在失败时也运行清理，不写真实Fovelle偏好。

本地证据位于 `reports/evidence/keep_zoom_persistence/`：

- `before-settingsmanager.cpp`：修复前生产迁移快照。
- `before-static.log`、`before-build.log`、`before-tests.log`：先编译新增测试，后取得可读的静态/动态失败。
- `after-build.log`、`after-tests.log`：生产修复与六项相关验收。
- `after-dynamic-details.log`：五行数据与两个后端每阶段的实际勾选、磁盘和manager值。
- `full-ctest.log`：完整41项通过。
- `after-static.log`、`related-static.log`：新增持久化门禁及上一轮UI/语言静态门禁。
- `final-build.log`、`final-tests.log`：最终构建与两项持久化重跑。
- `before-technical_design_document.md`、`before-test_case_specification.md`、`before-test_completion_report.md`：本轮报告更新前完整快照。

复现命令：

```sh
cmake --build build --target Fovelle fovelle_tests -j 6
ctest --test-dir build -R FovelleKeepZoomPersistence --output-on-failure
ctest --test-dir build --output-on-failure
```

联网检索路径、Qt/Apple依据、可能根因与反证、恒等迁移证明写在技术设计第10节；四条原子标准及六部分用例写在测试说明最新增补与 `tests/keep_zoom_persistence_cases.json`。未修改其它生产功能或关闭完整迁移来让测试通过。

本次验证正常进程退出/重启，不证明断电写盘、磁盘权限异常或外部进程主动改写设置的行为。初始化标记已存在是UI保存后正常重启的前提；探针避免依赖机器上的旧qView导入数据。旧版已经覆盖掉的用户选择无法反推，故不擅自把所有已有true改成false；使用修复构建后用户重新选择的值会保留。前一轮报告中的历史静态工具及元数据识别边界仍保留，不将本轮41项CTest通过扩展成“所有历史工具均通过”。

---

# 2026-10-10 缩放保持与色彩信息：测试完成报告

## 范围与结果

本轮实现“切图保持缩放比例”和“文件信息区显示源色彩空间、内嵌 ICC、当前输出空间”，均提供英语、简体中文、繁体中文、西班牙语、日语。单实例需求不在本轮范围。

已完成先固化用例/代码、再实现与运行验证；新增十条原子标准均有六部分用例、静态或动态代码映射。完整 CTest 39/39 通过（333.51 秒），后续边界修正的 Qt 主套件与功能验收重跑 6/6 通过（189.38 秒），最终布局/缓存修正的五项受影响验收 5/5 通过（15.05 秒）。最后的可见界面截图与宽高断言结果见下文视觉检查记录。

环境：arm64，macOS 27.0.1，Qt 6.11.2，Apple LLVM 17，Release；部署目标 macOS 15.0。实际执行平台不等于已在 macOS 15.0 真机上验证。zlib 链接系统 `/usr/lib/libz.1.dylib`，未新增需要单独分发的 Homebrew 动态库。

## 原子标准与实测映射

| 标准 | 实测代码/输入 | 结果 |
|---|---|---|
| Z1 数值保持 | `testKeepZoomAcrossNavigation` 的 PNG/XPM/SVG × 1%/120%/6400%，不同宽高、resize、缓存返回 | 通过，保持数值且计算模式为空 |
| Z2 设置入口 | `testKeepZoomPreferenceAndColorTranslations` 点击真实复选框，查询 QSettings/SettingsManager 并重建设置对话框 | 通过，原键反向绑定且立即持久化 |
| Z3 状态边界 | `testKeepCalculatedZoomAndFailedNavigation` 适应模式切图、坏图、快速请求及加载中缩放 | 通过，无旧适应模式回滚；坏图后继承；最后输入 150% 生效 |
| Z4 独立窗口/恢复 | `testKeepZoomWindowIsolationAndSession` 两窗口 120%/70%、恢复 120% 会话、关闭保持选项；另以 QT_SCALE_FACTOR=2 重跑整个缩放组 | 通过，不跨窗口污染；恢复优先；关闭后回到旧默认行为 |
| C1 来源与缓存 | `testSourceColorMetadata`，独立 Qt 写入的 P3 ICC PNG、无标记 PNG、异步 loader | 通过，原始 ICC 保留；生成 Qt profile 不会把未内嵌误报为内嵌 |
| C2 容器/错误/限额 | `testColorContainerBoundaries`，PNG sRGB/cICP、损坏/不支持 profile、16 MiB+1 解压数据、JPEG 乱序/缺失/重复分片、TIFF 越界、WebP 截断 | 通过，按证据区分状态并有界退出；小 profile 不保留 16 MiB 缓冲容量 |
| C3 输出分支 | `testColorInformationFollowsPresentation`，Qt 目标为 sRGB，P3 PNG 走实际原生 SDR Metal，然后切到 XPM | 通过，原生为 Extended Linear Display P3，Qt 为实际 sRGB |
| C4 刷新/清空 | 同一集成测试让信息区保持可见，跨分支切换后关闭图片 | 通过，源 ICC 不串图，无图为 No output |
| L1 静态集成 | `zoom_color_acceptance_static.py` 检查 UI XML、四份 TS 的上下文/完成状态/占位符、构建登记及六部分用例的代码映射 | 通过，英语采用源码；CMake/qmake 均登记新模块 |
| L2 实际语言资源 | 编译加载四份 QM，验证 LanguageChange、英语回退、纯文本与值列高度；各语言截图 | 通过，新增标签/状态与设置入口均使用对应译文；截图最终审阅另列 |

测试程序使用独立临时 INI 目录，不继承或写入桌面设置；新测试的窗口关闭、菜单注销、翻译器移除与选项恢复均有作用域清理。初始测试清理顺序错误及空文件名警告已修复，未通过关闭 fatal warnings 掩盖。

## 修复前证据与逆向证伪

1. 入口静态测试在旧代码上因缺少 `keepZoomCheckbox` 失败。首次包含完整新接口的测试编译因旧生产代码没有 `colorinformation.h` 失败；这是未实现接口证据，不冒充功能运行失败。
2. 为取得功能层反例，仅恢复原始 `qvgraphicsview.cpp` 的缩放实现，保留同一新测试与其安全清理。原测试期望继承 `0.5`，实际下一张为 `0.9028571428571428`，明确失败；恢复实现后通过。因此“关闭导航重置就足够”的假设已被同输入反证。
3. 最初动态测试结束时出现 QMenu 崩溃。系统堆栈定位至 `ScopedOptionValues::~ScopedOptionValues → SettingsManager::loadSettings → ActionManager::settingsUpdated → QMenu::menuAction`；测试窗口已析构但未执行 closeEvent 注销菜单。调整测试清理顺序后取得上述可读断言失败，未把测试崩溃归因于缩放算法。
4. 完整回归第一次出现空文件名警告，在 `QT_FATAL_WARNINGS=1` 下终止。原因是新信息对话框语言更新时尚无文件；增加空路径处理后警告消失。
5. 首次完整运行亦出现多个旧几何断言失败；改为隔离测试设置后，原有适应、滚动条、标题栏、导航及完整 Qt 套件通过。该对照说明验收不能依赖桌面遗留偏好；没有为让测试通过去放宽几何容差或修改旧缩放公式。
6. 截图发现 macOS 默认表单字段增长策略会裁掉色彩值；改用可用值列宽度，并固化 `heightForWidth` 断言。截图不是仅以标签文字存在代替布局检查。

## 命令与本地证据

所有证据位于 `reports/evidence/zoom_color_20261010/`；该目录按仓库既有规则被 Git 忽略，但本地保留，可供复查。

| 文件 | 含义 |
|---|---|
| `before-static.log`、`before-build.log` | 未实现入口/接口时的失败 |
| `baseline-zoom.log`、`baseline-build.log` | 原缩放实现同输入失败，数值期望与实际完整记录 |
| `before-crash-stack.txt`、`before-lldb.log` | 测试清理崩溃定位 |
| `verified-build.log`、`verified-ctest.log` | Release 构建及完整 39 项 CTest |
| `delivery-build.log`、`delivery-ctest.log` | 边界修正后 Qt 主套件与五个功能条目，6/6 |
| `ui-final-build.log`、`ui-final-ctest.log` | 最终信息布局与缓存容量修正，5/5 |
| `visual-build.log`、`visual-ctest.log` | 可见设置窗口中的截图与字段完整性检查 |
| `static-final.log` | 新增静态验收 |
| `file-info-{en,zh_Hans,zh_Hant,es,ja}.png`、`settings-{en,zh_Hans,zh_Hant,es,ja}.png` | 五语言信息区与设置入口截图 |
| `before-file-info-es.png` | 修正前西语值列截断证据 |
| `quality-static.json`、`baseline-quality-static.json` | 旧静态检查器与原提交对照 |

复现命令：

```sh
cmake --build build --target Fovelle fovelle_tests -j 6
ctest --test-dir build --output-on-failure
python3 tests/zoom_color_acceptance_static.py
ctest --test-dir build -R 'Fovelle(ZoomColor|KeepZoom|ColorInformation)' --output-on-failure
```

## 最终视觉检查记录

可见设置窗口的最终验证 1/1 通过（`visual-ctest.log`，0.89 秒）。新增断言验证复选框宽度不小于其自然宽度、整行位于 General 视口内，并等待实际布局终态；不能在 LanguageChange 只处理一个事件轮次后立即截取未完成的尺寸。信息区验证三个值标签高度覆盖各自的 heightForWidth。最终审阅英/简/繁/西/日截图：新增入口文字与三项色彩信息均完整显示。曾发现的色彩值列截断由生产布局修正；设置截图的早期裁切通过等待真实布局并显示原生窗口的正确测试流程消除，未缩短译文或放宽可见性断言。

这些为 Qt widget 截图，用于文字和布局检查；并非物理显示色度测量。截图通过安装 QM 切换标签，既有选项列表、日期或字节格式仍可能遵循应用保存语言/系统区域设置，不表示全应用已完成即时语言切换。真实 Qt/Metal 输出描述另由 `testColorInformationFollowsPresentation` 验证。

## 旧静态检查器与验证边界

额外运行旧 `quality_static.py` 得到 24/29 通过，五项失败为 ST-01/06/09/11/16。将 HEAD 导出到独立目录再次运行，同五项仍失败：涉及既有全文件格式差异及过时源码字符串/测试名匹配。导出目录没有 `.git`，其额外 ST-03 失败只是归档测试条件，不是原仓库缺陷；实际工作区 `git diff --check` 通过。本次新增静态门禁通过，编译过程亦包含原有 clang-tidy 设置。没有改写旧检查器来掩盖失败，因此不能声称“所有历史静态工具均已通过”。

未实现提取适配器的格式、BigTIFF、多页/多分辨率选择返回 Unknown；无声明文件不保证恢复真实创作空间。ICC 的 Invalid 是已检测到的结构/容器损坏，Unsupported 表示当前解析库无法解释，不是完整 ICC 规范认证。实际输出指应用表面编码，未进行多台物理显示器的色度测量。整体语言选择沿用应用既有重启约定；运行时重翻译测试不代表全部旧菜单已改为即时切换。


---

## 既有测试完成记录（完整保留）

# HDR 局部偏色测试完成报告

日期：2026-10-08。结论：新增测试在修复前检出了端点颜色错误，修复后的颜色、实际缓存、亮度与选定完整回归门禁均通过。已修复的生产缺陷是按 RGB 峰值混用 SDR/HDR 端点；未获得用户原异常图片，未宣称已复现该文件的全部显示现场。

## 修复前检出证据

| 新用例 / 数据 | 修复前实际结果 | 判定 |
|---|---|---|
| 中间调最终端点 | 红通道 .45，参考 .90 | 失败 |
| 近白点最终端点 | 红通道 .925，参考 1.05 | 失败 |
| 中间调 50% 进度 | 红通道 .45，参考 .675 | 失败 |
| 饱和高光对照 | 正确恢复 HDR | 通过；说明错误与空间门槛相关，并非所有像素统一偏差 |
| 增益图 JPEG | 最大 RGB 误差 .55249023；3725/4096 像素至少一个通道误差>.01 | 失败 |
| 处理后 DNG | 最大误差 .57983398；3794/4096 像素超阈值 | 失败 |
| 普通 DNG | 最大误差 .061035156；106/4096 像素超阈值 | 失败 |
| NEF | 最大误差 .09765625；1120/4096 像素超阈值 | 失败 |

失败日志：[red-pixels.log](evidence/hdr_color_regression/red-pixels.log)、[red-images.log](evidence/hdr_color_regression/red-images.log)。新测试先在旧生产算法上执行失败，再修改算法，没有通过放宽容差来掩盖偏色。

旧测试之所以漏检，是把非高光永久保持 SDR 作为正确结果，且真实原生测试只断言状态标志。修正后的测试以独立 HDR 端点、线性插值恒等性质及实际合成器缓存内容为参考，详见 [测试用例说明](test_case_specification.md)。

## 修复后结果

| 验证 | 结果 | 证据 |
|---|---|---|
| 构建应用与测试程序 | 成功；已移除本次自定义 CIColorKernel 及其 deprecated 源码编译 API | [green-build.log](evidence/hdr_color_regression/green-build.log) |
| 静态检查 | 12 项通过、0 失败 | [final-static.log](evidence/hdr_color_regression/final-static.log) |
| 颜色单像素四个数据行 | 全部通过，容差 .003 | 最终 CTest ColorPixels 详细日志 |
| 四类真实图像 | 全部通过；每图采样 4096 像素，0 像素超过 .01 阈值 | 最终 CTest ColorSamples 详细日志 |
| 实际持久缓存读回 | 通过；全分辨率物化参考对比最大误差 0 | [cache-probe-materialized-detail.log](evidence/hdr_color_regression/cache-probe-materialized-detail.log) 及最终 ColorNative 日志 |
| FovelleTests 八套 Qt suite | 256 通过，0 失败、0 跳过（含 suite init/cleanup） | [final-ctest-detail.log](evidence/hdr_color_regression/final-ctest-detail.log) |
| 选定 CTest 汇总 | 7/7 通过，0 失败，共 178.79 s | [final-ctest.log](evidence/hdr_color_regression/final-ctest.log) |
| 源码空白/差异检查 | `git diff --check` 通过 | 本地执行 |

早期修复后独立图片测试的最大误差：JPEG .00048828125，其余三类为 0；均在 .01 阈值内。最终重跑每一类样本与实际缓存，日志保留每个数据行的实际数值。该误差是线性 RGB 数值误差，不是 ΔE 或物理亮度测量。

## 逆向证伪与执行中发现

缓存读回测试第一次比较得到 .130859 的差异。没有据此修改生产缓存，也没有提高容差。分析发现两边计算顺序不同，控制实验将独立参考先全分辨率物化、再按相同方式采样，误差归零。保留 [cache-probe-initial.log](evidence/hdr_color_regression/cache-probe-initial.log) 和 [cache-probe-materialized-detail.log](evidence/hdr_color_regression/cache-probe-materialized-detail.log)。这是一处测试参考口径修正，不是额外已确认的生产缓存缺陷。

第一次完整回归的所有颜色/HDR/视图测试通过，但 `WindowBehaviorTests::testNavigationButtonsClickSwitchesFiles` 一次失败，并在失败后清理时 SIGSEGV。相同二进制单独重跑该测试通过；不修改代码、再重跑相同完整命令后全部通过。保留 [regression-initial.log](evidence/hdr_color_regression/regression-initial.log) 与 [navigation-isolated.log](evidence/hdr_color_regression/navigation-isolated.log)。这次偶发导航失败的具体根因未定位，不能宣称已修复，也不能仅据隔离通过断言它与本次改动必然无关。

## 验收追踪

| 标准 | 测试与结果 |
|---|---|
| CR-01 端点色彩 | 3 类最终颜色数据行 + 全图四格式端点比较，通过 |
| CR-02 中间插值 | 中间调独立反例、原五个进度点、相同暗部/中间调与 alpha，通过 |
| CR-03 平台余量适配 | 共享适配路径静态检查、SDR 回退、当前余量、原生 3→1→3 重建，通过；未宣称已测量每一物理显示余量下的色差 |
| CR-04 真实图像 | JPEG/处理后 DNG/普通 DNG/NEF，独立 HDR 网格参考，通过 |
| CR-05 实际缓存 | 真实 presented 生命周期后的 CGImage 内容读回，公平采样参考，通过 |
| CR-06 回归 | 时序、连续切图、无障碍策略、焦点、缩放、SDR 与完整八 suite，通过 |

## 复现与证据范围

```bash
cmake --build build -j 6
python3 tests/hdr_brightness_static.py
FOVELLE_HDR_JPEG_SAMPLE='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG' \
ctest --test-dir build \
  -R 'FovelleTests$|FovelleHDRBrightness|FovelleHDRColor|FovelleHDRFocusTransitionEvidence' \
  --output-on-failure
```

本地颜色门禁依赖 `1.JPG`、`2.DNG`、`3.dng`、`4.nef`，可在 CMake 中设置 `FOVELLE_HDR_COLOR_FIXTURE_DIR`。没有样本的环境不会注册本地样本 CTest；确定性颜色单像素测试仍可运行。此次本地样本齐全，所有最终注册门禁执行成功。

代码指纹：[source-sha256.json](evidence/hdr_color_regression/source-sha256.json)。本轮修改前的源码与报告快照位于 `reports/evidence/hdr_color_regression/before/`。技术依据、候选根因与证伪过程见 [技术设计文档](technical_design_document.md)。

没有改变用户真实显示器设置或无障碍设置，没有进行跨物理显示器观感实验。足够 headroom 的测试使用进程内测试余量输入，不等同于显示器真实可达亮度；验证的是图像管线数值与缓存内容。

## 2026-10-08 Actions 修复验证

原失败：Build Fovelle 37746276287，FovelleTests 内 Open With 关闭时间断言失败；其余 30 项 CTest 通过，同提交 Checks 成功。

红灯：仅添加 slow-startup=5100 ms 数据行、保留旧计时起点，得到 3 passed / 1 failed；失败正是 teardownTimer.elapsed()<5000。绿灯：调整计时起点并加入作用域清理后，4 passed / 0 failed，正常行关闭耗时 820 ms，慢启动行 12 ms。日志位于本次本机 `/tmp/fovelle-ci-red.log` 与 `/tmp/fovelle-ci-green.log`（临时诊断文件，不作为仓库持久证据）。生产后台等待逻辑保持原样，因为未证实它发生超时。

完整本地验证：CMake 编译成功；与 CI 相同命令 `QT_QPA_PLATFORM=cocoa ctest --test-dir build --output-on-failure --timeout 90` 执行 34/34 项通过，用时 321.48 秒（本机额外注册了真实 HDR 样本专项）。`git diff --check` 通过。红灯、绿灯和完整 CTest 日志持久化到 `reports/evidence/ci_teardown/`。

格式检查限制：仓库现有 `build.sh --format-check` 在大量原有格式诊断后仍返回 0，不能据此宣称格式全部符合；本次新增日志/断言已局部格式化，没有扩大修改范围。该历史脚本问题不是原失败原因。

远端验收：本报告随修正推送，实际完成状态以该提交的 [Build Fovelle](https://github.com/inostarlin-passion/Fovelle/actions/workflows/build.yml) 与 [Checks](https://github.com/inostarlin-passion/Fovelle/actions/workflows/test.yml) 运行结论为准；推送后继续监控，最终运行链接在任务完成回复中交付。


## 2026-10-10 Actions 修复与推送验证

基线提交566858058102a7081213de3609391aed158962bd：Checks失败，Build Fovelle成功；clang-tidy/clang-format均为成功状态，唯一Qt失败为Open With正常启动总耗时5080ms超过5000ms。

先复现：5200ms可控provider在旧总时长断言下失败（实测5210ms）。修正后原生正常启动、5100ms慢准备和可控延迟三行均通过。一次本地测量分别为 total/worker/overhead=931/928/3、19/14/5、5211/5203/8ms；dispatch均0ms，close分别23/12/13ms。完成标志在每次窗口析构返回后为真，五秒预算保留给独立收尾阶段，完整函数看门狗保留。

生产改动只增加默认不变的按值provider依赖，保留排空顺序；同步更新两份源码检查的捕获模式。证据位于reports/evidence/ci_openwith_20261010，包括原始失败/成功CI日志、红绿测试、构建与静态检查。Open With源码生命周期门禁ST-12通过；旧质量脚本其它既有问题不扩展为本轮修复。完整回归日志另存于同一证据目录；远端最终结果以对应提交的Actions状态及本次交付消息为准，不以本地通过替代远端验证。
