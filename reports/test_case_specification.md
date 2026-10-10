# 2026-10-10 增补：Keep zoom level 持久化回归

先新增静态门禁和五行动态用例，在生产修复前取得失败，再移除错误迁移项。原有同进程对话框测试保留，但不再将它视为重启持久化证明。可执行用例清单位于 `tests/keep_zoom_persistence_cases.json`，代码位于 `tests/zoom_color_acceptance.inc` 与 `tests/keep_zoom_persistence_static.py`。

## KZ-P1：恢复为可配置的保持缩放键不再被启动迁移强制重置

类型：静态；代码：`tests/keep_zoom_persistence_static.py`。

- 测试目的：阻止恢复设置入口后仍残留在 removedPreferenceDefaults 的回归
- 前置条件：可读源码树
- 输入数据：迁移表、默认注册、设置反向绑定、main 启动顺序
- 操作步骤：检查重置表不包含 navresetszoom；默认仍为true、绑定仍取反；确认迁移在生产应用构造前执行
- 预期结果：活动设置不被迁移覆盖，保留原默认及真实启动路径
- 后置条件：不修改文件或设置

## KZ-P2：迁移保留已保存的开/关，并保持缺省及幂等性

类型：动态/三行数据；代码：`FeatureTests::testKeepZoomSurvivesStartupMigration`。

- 测试目的：补上同进程重建对话框未覆盖的启动迁移阶段
- 前置条件：隔离INI测试目录，firstlaunch=true，保存并作用域恢复原值
- 输入数据：navresetszoom=false、true、缺失；退役 navigationregionsenabled=true
- 操作步骤：写入输入并sync；连续运行两次真实迁移；各轮检查磁盘、全新SettingsManager、真实复选框；核对退役策略仍归一化
- 预期结果：false/true原样保留；缺失不强行写入且默认保持关闭；复选框与反向值一致；退役策略仍为false
- 后置条件：恢复原测试设置；不禁用或绕过迁移

## KZ-P3：勾选保持缩放后跨独立进程启动仍保持勾选

类型：动态/INI及macOS NativeFormat；代码：`FeatureTests::testKeepZoomPersistsAcrossProcesses`。

- 测试目的：检出启动前后被迁移覆盖，排除同进程缓存造成的假通过
- 前置条件：临时共享INI目录或UUID专属原生偏好域；每个子进程按生产顺序先迁移再构造应用
- 输入数据：seed→enable→read→read
- 操作步骤：种子进程初始化标记；写进程点击真实复选框并正常退出；两个全新读进程分别执行启动迁移并读取UI、磁盘与manager
- 预期结果：写入及两个重启均checked=true、storedReset=false、managerReset=false、status=NoError
- 后置条件：独立子进程结束；作用域清理测试域键和临时目录，失败路径也执行清理

## KZ-P4：取消勾选同样持久化且不改变新安装默认

类型：动态/双向回归；代码：`FeatureTests::testKeepZoomPersistsAcrossProcesses / FeatureTests::testKeepZoomSurvivesStartupMigration`。

- 测试目的：防止修复以强制开启所有用户代替保存选择
- 前置条件：前面已经成功跨进程保存勾选状态
- 输入数据：disable→read；无键默认输入
- 操作步骤：另起进程取消勾选并退出，再起进程迁移后读取；独立检查无键默认
- 预期结果：取消后及重启均checked=false、storedReset=true；默认仍关闭保持
- 后置条件：保持用户开/关语义及其它迁移策略，不写真实Fovelle偏好域

---

# 2026-10-10 缩放保持与色彩信息：验收及测试用例

用例先于生产代码固化，边界用例在验证中补充。可执行代码位于 `tests/zoom_color_acceptance.inc` 和 `tests/zoom_color_acceptance_static.py`。每个标准均关联实际测试；执行结果见测试完成报告。

## Z1：切图保持已提交数值且不复活适应模式

类型：动态/数据驱动；代码：`GraphicsViewTests::testKeepZoomAcrossNavigation`。

- 测试目的：验证不同图像尺寸和渲染路径保持比例
- 前置条件：临时目录，navresetszoom=false，窗口可显示
- 输入数据：PNG/XPM/SVG；640×400→220×700；1%、120%、6400%
- 操作步骤：打开A，绝对缩放，打开B，改变窗口大小，再打开缓存A
- 预期结果：每次成功显示与resize后数值不变，计算模式为空
- 后置条件：临时图、窗口销毁，选项恢复

## Z2：设置可见、反向绑定及立即持久化

类型：动态；代码：`FeatureTests::testKeepZoomPreferenceAndColorTranslations`。

- 测试目的：检查UI真正连接原设置键
- 前置条件：保存原选项，navresetszoom=true
- 输入数据：保持缩放复选框
- 操作步骤：创建设置，勾选，查询QSettings和SettingsManager，重建设置
- 预期结果：初始未勾选，点击后原键=false，新设置显示勾选
- 后置条件：原选项恢复，无新设置键

## Z3：计算模式、失败和快速请求有确定行为

类型：动态；代码：`GraphicsViewTests::testKeepCalculatedZoomAndFailedNavigation`。

- 测试目的：证伪只关闭重置、错误占位覆盖与旧请求回滚
- 前置条件：临时图片及损坏PNG，保持开启
- 输入数据：适应比例、120%、加载中150%；A/B/C/坏图
- 操作步骤：适应A后切B并resize；120%→坏图→C；快速请求并缩放；关闭再打开
- 预期结果：保持适应所得数值，坏图不覆盖，最新请求为150%，关闭后恢复首张默认
- 后置条件：窗口和所有选项恢复

## C1：源空间与ICC在转换前采集且缓存携带

类型：动态；代码：`ImageCoreAndMovieTests::testSourceColorMetadata`。

- 测试目的：区分文件内嵌ICC与Qt生成profile
- 前置条件：Qt PNG写入器可用，临时目录
- 输入数据：Display P3 ICC PNG、无标记PNG、未知字节
- 操作步骤：读文件源元数据，经过异步loader，再查询无标记文件
- 预期结果：P3原始ICC保留，无标记为Absent，未知为Unknown
- 后置条件：临时文件销毁，无颜色设置变化

## C2：损坏或不支持不能误报未内嵌

类型：动态/恶意输入；代码：`ImageCoreAndMovieTests::testSourceColorMetadata / ImageCoreAndMovieTests::testColorContainerBoundaries`。

- 测试目的：检查完整性与读取边界
- 前置条件：有效ICC PNG字节
- 输入数据：损坏iCCP名称/CRC、截断PNG、结构合法但不支持ICC、受限解压输入
- 操作步骤：分别写入临时文件并解析
- 预期结果：损坏profile为Invalid，不支持为Unsupported，截断/超限为Unknown，无越界或无界分配
- 后置条件：测试输入清理

## C3：输出空间由当前可见渲染路径提供

类型：动态/集成；代码：`ImageCoreAndMovieTests::testColorInformationFollowsPresentation`。

- 测试目的：证伪以Qt目标设置冒充原生Metal输出
- 前置条件：macOS Metal可用，Qt目标sRGB
- 输入数据：P3 PNG→Qt XPM
- 操作步骤：加载P3等待实际呈现，读取输出；打开信息区再切Qt图
- 预期结果：源仍为P3；Metal为Extended Linear Display P3；Qt为实际sRGB，源ICC不串图
- 后置条件：选项与窗口清理

## C4：信息区刷新并在无图时清除旧状态

类型：动态/UI；代码：`ImageCoreAndMovieTests::testColorInformationFollowsPresentation`。

- 测试目的：检查可见信息区与当前快照一致
- 前置条件：信息区可见且接入实时值提供器
- 输入数据：原生图、Qt图、关闭图片
- 操作步骤：跨分支切图并关闭
- 预期结果：三行从同一快照更新，关闭显示No output且无旧ICC
- 后置条件：对话框关闭，定时器随对象销毁

## L1：入口、构建与四个翻译目录完整

类型：静态；代码：`tests/zoom_color_acceptance_static.py`。

- 测试目的：防止遗漏UI、上下文、占位符或构建文件
- 前置条件：源码目录可读
- 输入数据：UI XML、四份TS、CMake/qmake
- 操作步骤：解析入口与键集合并检查占位符
- 预期结果：每个键译文非空且完成，英文使用源码，两套构建均登记新文件
- 后置条件：源码不变

## L2：五语言实际QM可用且对话框重翻译

类型：动态/资源集成；代码：`FeatureTests::testKeepZoomPreferenceAndColorTranslations`。

- 测试目的：检查运行时真实翻译资源而非仅TS文字
- 前置条件：已编译四份QM
- 输入数据：zh_Hans、zh_Hant、es、ja、英语回退
- 操作步骤：加载安装各翻译器，处理LanguageChange，检查设置及信息标签，移除翻译器；显示信息区并检查颜色值标签的 heightForWidth，保存各语言截图；等待设置窗口终态，并检查新复选框自然宽度及视口包含关系
- 预期结果：标签与当前QM一致，切回英语无残留；颜色值列高度足够显示全部换行文字
- 后置条件：所有测试翻译器移除

## Z4：窗口隔离、会话恢复与关闭选项保持旧行为

类型：动态/HiDPI；代码：`GraphicsViewTests::testKeepZoomWindowIsolationAndSession`。

- 测试目的：检查比例不跨窗口传播且会话初次恢复有优先级
- 前置条件：两个窗口，保持开启，临时PNG；另以QT_SCALE_FACTOR=2运行
- 输入数据：窗口1=120%，窗口2=70%；会话保存120%；关闭保持后切图
- 操作步骤：两窗口独立切图；窗口2清图并恢复窗口1会话；窗口2关闭保持再切图
- 预期结果：各自比例保持；恢复120%而非70%；关闭后回默认计算模式；窗口1不受影响
- 后置条件：两窗口关闭、选项与退出策略恢复

---

## 既有测试说明（保留）

# HDR 局部偏色测试用例说明

日期：2026-10-08。对应问题：HDR 图片“一部分偏色”。技术设计中定义 CR-01～CR-06。以下每个用例均含六要素，并映射到测试代码。已有亮度测试继续作为回归；修正其错误假设，不删除新颜色测试或提高容差来换取通过。

## 原测试为什么漏检

1. 单像素只给定峰值明显超过 1.1 的 HDR 高光，使旧空间掩码恒为 1；缺少真实 HDR 中间调和近白点颜色。
2. 把“非高光无论原始 HDR 颜色是什么，都必须保持 SDR”写成预期结果，等于把错误实现当作 oracle。
3. 原生测试只看 firstFramePresented、transitionProgress、persistentHDRSurfaceReady 等状态，未读取显示缓存 RGB。
4. 仅以一个 JPEG 验证呈现状态，未对不同 RAW / 增益图格式验证最终颜色。

旧数值测试现在明确输入“已经适配好的 HDR 端点”，检查线性混合；只有 SDR/HDR 本来相同的非高光区域才要求不变。新测试参考来自端点恒等性质、独立解码图像和实际缓存像素，不来自被测公式。

## CR-01-S 静态用例

1. 测试目的：核查最终颜色端点完整性的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR01_endpoint_identity`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-01-D 动态用例

1. 测试目的：验证最终颜色端点完整性，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：HDR 值低于白点、近白点、饱和高光三种区域；进度 1；headroom=8。
4. 操作步骤：执行 `testHDRBrightnessPreservesEndpointColors(colored-midtone-endpoint / near-white-endpoint / saturated-highlight)`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：所有 RGB 通道与输入 HDR 端点误差 <.003；不能残留 SDR 颜色。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-02-S 静态用例

1. 测试目的：核查线性时间插值与相同区域稳定的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `BrightnessSourceContracts.test_BR05_highlights`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-02-D 动态用例

1. 测试目的：验证线性时间插值与相同区域稳定，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：中间调进度 .5；原有五个进度点；相同暗部/中间调端点；alpha=.5。
4. 操作步骤：执行 `testHDRBrightnessPreservesEndpointColors(colored-midtone-midpoint)、testHDRBrightnessLinearLightPixels`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：输出等于端点线性插值；相同区域不变；透明度正确。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-03-S 静态用例

1. 测试目的：核查平台显示适配与共用端点的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR02_platform_adaptation`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-03-D 动态用例

1. 测试目的：验证平台显示适配与共用端点，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：source/target headroom API；当前余量 1、3；原生 3→1→3。
4. 操作步骤：执行 `testHDRBrightnessNativePresentation、testSDRDisplayForcesUnitHeadroom、testDisplayHeadroomBootstrapsFromPotentialCapability`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：纯 SDR 正确回退；当前余量优先；原生缓存随余量重建；源码确认三条生产路径使用同一端点适配。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-04-S 静态用例

1. 测试目的：核查真实图片的区域颜色一致性的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR03_real_image_oracle`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-04-D 动态用例

1. 测试目的：验证真实图片的区域颜色一致性，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：1.JPG、2.DNG、3.dng、4.nef；足够 headroom；64×64 全图 float RGB 网格。
4. 操作步骤：执行 `HDRSampleTests::testHDRColorFidelity（四个数据行）`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：相对独立原生 HDR 参考，最大 RGB 通道误差≤.01；无超过阈值的像素。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-05-S 静态用例

1. 测试目的：核查实际最终缓存颜色一致性的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `ColorRegressionSourceContracts.test_CR04_materialized_cache`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-05-D 动态用例

1. 测试目的：验证实际最终缓存颜色一致性，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：真实 JPEG；320×240 原生视口；足够 headroom；实际缓存与独立参考先全分辨率物化再各采样 64×64。
4. 操作步骤：执行 `testHDRBrightnessNativeColorFidelity`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：实际缓存 CGImage 与独立参考最大通道误差≤.01；数组长度 16384、所有值有限。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## CR-06-S 静态用例

1. 测试目的：核查亮度与既有交互回归的生产接线，防止正确探针与错误显示路径并存。
2. 前置条件：仓库源码可读，Python 3 可运行。
3. 输入数据：`src/qvcocoafunctions.mm`、`src/qvgraphicsview.cpp` 与 `tests/tst_qviewtests.cpp`。
4. 操作步骤：运行 `tests/hdr_brightness_static.py` 中 `BrightnessSourceContracts.test_BR01_timing 至 test_BR08_cache（共八项）`，断言端点、适配 API 或探针调用点。
5. 预期结果：源码契约断言通过；空间门槛/自定义 RGB 削峰不能重新进入时间渐变函数。
6. 后置条件：不修改源码或用户系统设置，保留执行日志。

## CR-06-D 动态用例

1. 测试目的：验证亮度与既有交互回归，覆盖原测试没有检测的结果或必要回归。
2. 前置条件：CMake 构建成功；macOS Cocoa/Metal 可用；涉及真实图像时本地四类样本可读；原生渐变测试要求系统减弱动态效果关闭，true 分支由纯策略独立覆盖。
3. 输入数据：普通/连续/减弱动态效果时序、SDR/HDR、缩放和焦点；完整八套 Qt 测试。
4. 操作步骤：执行 `HDRPolicyTests 其余函数、FovelleTests、FovelleHDRFocusTransitionEvidence`；像素用例实际 GPU 渲染再读 float；原生缓存用例等待真正安装后读取其 CGImage。
5. 预期结果：原有时序与交互行为保持通过；已有就绪检查作为生命周期补充，不能替代颜色测试。
6. 后置条件：释放临时窗口、图像、上下文和定时器；测试余量环境变量通过 scope guard 恢复；不更改用户无障碍或显示器设置。

## 数据、门禁与执行方式

本地样本目录：`/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test`，文件依次为 `1.JPG`（增益图 JPEG）、`2.DNG`（处理预览/增益图）、`3.dng`（普通 DNG）、`4.nef`。采样点覆盖整个图像，不只统计平均亮度或峰值。RGB 通道绝对误差阈值 .01，单像素数学阈值 .003；这不是 ΔE 阈值。

无外部样本也可运行 `FovelleHDRColorPixels` 与静态检查。`FovelleHDRColorSamples` / `FovelleHDRColorNative` 仅在配置时发现四个本地样本后注册，可通过 CMake `FOVELLE_HDR_COLOR_FIXTURE_DIR` 指定其他目录。显式运行样本 suite 时缺文件会失败，不会伪装为通过；完整 policy suite 缺少原生 JPEG 时会明确跳过对应原生用例。本次执行均提供了样本。

```bash
python3 tests/hdr_brightness_static.py
ctest --test-dir build -R 'FovelleHDRColor|FovelleHDRBrightness' --output-on-failure
FOVELLE_HDR_JPEG_SAMPLE='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG' \
ctest --test-dir build \
  -R 'FovelleTests$|FovelleHDRBrightness|FovelleHDRColor|FovelleHDRFocusTransitionEvidence' \
  --output-on-failure
```

## 缓存 oracle 的校正

第一次缓存测试将“直接缩小的延迟 CI 图”与“全分辨率计算后再缩小的实际 CGImage”比较，产生 .130859 的差异。校正为参考与实际均在全分辨率计算后再采样，独立参考不经过被测渐变/掩码，容差仍为 .01。校正后最大误差为 0。保留两个实验日志，避免将测试口径问题误报为生产缓存缺陷。

修复前的新单像素测试和四类真实图片测试的失败结果，以及修复后结果，详见测试完成报告。新增实际缓存用例是端到端颜色防回归补充，不宣称最初的采样口径失败就是已确认生产缺陷。

## 2026-10-08 Actions 回归测试

| 用例 | 目的 | 前置条件 | 输入 | 步骤 | 预期结果 | 后置条件 |
| --- | --- | --- | --- | --- | --- | --- |
| OpenWith normal-startup | A2：真实关闭任务安全完成 | Cocoa、可加载 PNG | 准备延迟 0 ms | 创建显示窗口、加载 PNG、计时、请求 Open With、关闭及析构 | 无崩溃，关闭耗时 <5000 ms | 窗口与临时目录释放，退出策略恢复 |
| OpenWith slow-startup | A1/A2：排除准备时间对关闭断言的污染 | 同上 | 准备延迟 5100 ms | 就绪后等待输入延迟，再计时及关闭 | 旧计时范围失败；修正后关闭 <5000 ms | 同上 |
| 作用域清理审查 | A3：断言提前返回时设置仍恢复 | 读取测试源码 | qScopeGuard 与局部对象生命周期 | 静态审查 guard 位于首个断言之前、捕获原值且晚于窗口析构释放 | 所有返回路径恢复原退出策略 | 无状态修改 |
| 完整 CI | A4：修正兼容完整工程 | 编译成功、main 可推送 | 当前提交 | 完整 CTest；推送并读取两个工作流结果 | Build Fovelle 和 Checks 均成功 | 工作区干净 |

动态用例固化于 `FeatureTests::testOpenWithWorkerTeardownContract_data/testOpenWithWorkerTeardownContract`，由现有 FovelleTests 自动执行。准备延迟专门覆盖旧五秒边界，不放宽关闭门槛。静态审查和 git diff --check 为补充检查。
