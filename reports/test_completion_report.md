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
