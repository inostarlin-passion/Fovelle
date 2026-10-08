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
