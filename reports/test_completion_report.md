# EPS/SVG缩放、拖动掉帧修复完成报告

日期：2026-10-18（Asia/Shanghai）。基线提交：073840926224d04394193b803a9dcef597c43c2a。

完成顺序：问题拆解与联网交叉核验 → 设计及正确性证明 → 原子验收与六要素用例 → 测试代码及基线失败 → 生产修复 → 定向与完整执行 → 本报告。

[设计文档](/Users/inostarlin/code/Fovelle/reports/technical_design_document.md)包含Qt文档、当前Qt源码、Apple线程约束与本地实测的多跳核验。[测试规范](/Users/inostarlin/code/Fovelle/reports/test_case_specification.md)逐例记录目的、前置条件、输入、步骤、预期和后置条件。旧三份报告备份在本次证据目录。

## 根因与实现

基线的矢量paint每帧读取并解析窗口ICC；新tile在GUI执行颜色转换，随后用RGBA64或RGBA32F直接绘制到窗口。已有异步栅格化没有消除这些GUI工作。基线四组测试各记录GUI转换12次、ICC查询119次；4K EPS冷首绘p95达82.73ms，SVG达17.46ms。静态4项失败、动态4个数据行失败，红色证据保留。

修复集中在qvgraphicsimageitem：

- worker完成高位深矢量栅格化、目标ICC转换、最终ARGB32_Premultiplied准备；GUI只绘制准备好的显示副本。
- 高位深临时raster在worker释放，原始矢量文档保持；目标变化重新从文档渲染，不转换旧8位缓存，保留此前色彩修复。
- 窗口profile仍在GUI访问，但移出paint：首次异步绑定、screenChanged刷新、500ms轮询兜底；profile字节相同时不重复解析。
- 请求key和完成发布均校验目标色彩空间/generation；保持一个active和一个最新pending；关闭时取消票据、停止timer并断开窗口连接。
- FullViewportUpdate交互策略、透明区域清除、终端设备密度及idle精细化保持。未修改旧性能门禁阈值或删除旧测试。

## 同机前后测量

环境：macOS 27.0.1、arm64、Qt/QtTest 6.11.2、Release、Apple Clang 21.0.0；实际窗口日志显示120Hz、viewport 1728×984、DPR=2。Qt库自身构建说明为Apple LLVM 17。部署目标仍为macOS15，本轮没有macOS15整机测试。

自生成的相同EPS/SVG、相同12步缩放与8帧缓存拖动、相同画布进行对比。下表为QElapsedTimer包围item paint的**墙钟耗时**，包括冷tile首绘，单位ms；不是物理屏幕FPS，也不包含worker总准备时间。最终数据取完整CTest中的最后一轮同组采样。

| 格式/画布 | 冷首绘p95：修复前→后 | 全部paint p95：前→后 | 冷首绘降幅 |
|---|---:|---:|---:|
| SVG 1920×1200 | 5.48 → 1.68 | 4.52 → 1.64 | 69.4% |
| EPS 1920×1200 | 22.55 → 1.43 | 20.10 → 1.40 | 93.7% |
| SVG 3840×2400 | 17.46 → 6.06 | 12.10 → 5.96 | 65.3% |
| EPS 3840×2400 | 82.73 → 5.56 | 65.95 → 5.49 | 93.3% |

各组GUI转换均为0、worker完成转换12次；ICC查询分别降为1、1、2、4次。显示副本均为ARGB32 premultiplied。4K场景GUI瓦片栅格缓存计数约83.1MiB，不包含矢量文档、在途工作集、窗口backing store或进程RSS。

没有隐去反向指标：SVG warm-cache p50在1920/3840画布分别由约0.63/2.04ms升至1.40/3.56ms，仍低于预算。本修复降低新tile尖峰和整体p95，不声称每项中位数都下降。[逐组原始数据与比较](/Users/inostarlin/code/Fovelle/reports/evidence/vector_performance_20261018/comparison-final.json)保留完整字段。

另保留既有真实窗口线程CPU预算测试：EPS缩放/拖动p99为0.454/0.503ms，SVG为1.977/1.956ms，四组各120次样本，均通过原8.33ms门禁。它不能替代本轮逐次等待新tile的冷首绘测试，也不能将计算出的CPU容量FPS当成实际显示FPS。

## 原子验收结果

| 标准 | 固化代码与实际验证 | 结果 |
|---|---|---|
| P1 | vector_performance_static.py；D1命中重绘查询计数不变 | 通过，paint无系统ICC访问/解析 |
| P2 | 同脚本；prepareVectorDisplay实际线程检测 | 通过，GUI转换0 |
| P3 | D1四行格式/worker计数断言 | 通过，发布前准备ARGB32显示副本 |
| P4 | testVectorDisplayTargetSwitch两行；既有color rendering与PDF高精度测试 | 通过，先目标变换后量化，源入口未降精度 |
| P5 | D2在途sRGB→P3→AdobeRGB切换，最终像素/目标检查；S3 | 通过，旧结果未覆盖最后目标 |
| P6 | testVectorRequestBurstAndDestruction；S3 | 通过，100次变化队列≤2，最终精确tile就绪，在途销毁安全 |
| P7 | testVectorPresentationFrameBudget四行，两种分辨率×两种格式 | 通过，冷首绘及整体p95均满足门禁 |
| P8 | 原密度、EPS首屏、拖影/首个拖动全覆盖、真实窗口CPU测试 | 通过，EPS首屏稳定对照diff=0 |
| P9 | 新静态/动态注册CTest，完整65项 | 65/65通过 |

测试代码位于vector_performance_acceptance.inc、vector_pipeline_acceptance.inc和vector_performance_static.py；tst_qviewtests.cpp仅添加slots/include；CMake新增两个测试门禁。原有测试断言未放宽，新增销毁测试按原有异步取消机制执行。

## 执行证据

证据目录：[vector_performance_20261018](/Users/inostarlin/code/Fovelle/reports/evidence/vector_performance_20261018)。目录沿用仓库ignore规则，原始文件实际保存在本地。

| 执行 | 结果 | 日志 |
|---|---|---|
| 基线静态 | 4失败，符合新增契约捕获旧路径的预期 | static-red.log |
| 基线动态 | 四组失败，分别记录GUI转换12、ICC查询119及高位深显示格式 | baseline-2x.log、baseline/*.json |
| 新静态 | 4/4通过 | static-green.log |
| 新动态 | 9 passed、0 failed、0 skipped，含初始化/清理；7个实际数据行 | fixed-second.log |
| 性能+色彩定向CTest | 5/5通过 | focused-final.log |
| 原终端密度、EPS初始帧 | 4 passed（含初始化/清理），初始图像diff=0 | vector-quality.log |
| 完整构建 | 成功；有既有Qt弃用警告 | build-final.log |
| 完整回归 | **65/65通过，565.87秒** | ctest-full.log、ctest-qt-output.log |
| 实际窗口CPU/全组性能 | 原始指标保留 | real-view-cpu-budget.log、full-performance-metrics.log |
| 差异及格式 | git diff --check、新inc clang-format检查通过；既有文件只格式化修改范围 | 最终检查 |

完整命令：`QT_QPA_PLATFORM=cocoa ctest --test-dir build --output-on-failure --timeout 120`。包括主套件、窗口、缩放、HDR/RAW信号、部署包、此前色彩测试及本次两个门禁；没有用筛选成功代替完整结果。主套件236.61秒通过，最终新动态门禁4.04秒通过。本轮完整回归无CTest失败。Qt内部原有可选样本跳过逻辑保持，本报告不声称验证了所有厂商相机。

## 验证边界

本轮复现并消除了已测GUI绘制阻塞，覆盖合成EPS/SVG的两种分辨率、实际目标颜色切换、队列与销毁、真实窗口CPU预算。没有采集物理逐帧呈现时间，也不保证任意复杂矢量文档的后台细节准备时间或任何机器都达到固定显示帧率。SVG热缓存中位数的变化已列明。

未提交、推送或发布应用，未修改用户设置。最终源文件SHA-256见source-manifest.json。
