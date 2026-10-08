# HDR 局部偏色技术设计与根因分析

日期：2026-10-08。用户问题：HDR 图片显示异常，进一步确认表现为“一部分偏色”。本次任务是在上一轮亮度渐变改动的基础上，先使补充测试失败，再修改生产代码。此前代码及报告快照保存在 `reports/evidence/hdr_color_regression/before/`，不覆盖用户其他工作。

## 问题界定与原子验收标准

| ID | 验收标准 |
|---|---|
| CR-01 | 显示余量足够、渐变结束时，所有 RGB 区域均恢复正确解码 HDR 端点，包括 HDR 值低于 1、白点附近和饱和高光，不能按局部峰值留下 SDR 颜色。 |
| CR-02 | 中间时刻在扩展线性光空间按同一时间进度插值；不得另加空间 RGB 掩码；若 SDR/HDR 某区域本来相同，该区域应保持稳定。 |
| CR-03 | 当前余量不足时由增益图或 Core Image 色调映射适配；余量足够时保持解码 HDR 原样；预热、可见帧与最终缓存共用端点选择。 |
| CR-04 | 对 JPEG、处理后 DNG、普通 DNG、NEF 的真实图像进行全图 RGB 网格比较，参考值来自独立解码 HDR 分支，不能来自待测公式或就绪标志。 |
| CR-05 | 实际交给合成器的持久图像缓存也必须保持端点颜色；参考与实际使用同样的计算/采样顺序；不能把缩小口径差异当成偏色。 |
| CR-06 | 80+520 ms 默认渐变、250 ms 连续切图、减弱动态效果、动态余量、缩放和 SDR 窗口行为继续通过原测试与完整 Qt 回归。 |

## 多跳检索、多源核验

第一跳检索 HDR/增益图、CIColorKernel、线性颜色；第二跳阅读 WWDC24 原文、增益图和 ToneMap API；第三跳针对缓存比较差异检索 Core Image 延迟计算、合并/重排滤镜与中间缓存。以下是各判断的官方依据，工程推导与已测事实另列，不把候选根因当成事实。

| 官方来源 | 核验结论及用途 |
|---|---|
| [WWDC24 HDR 图像](https://developer.apple.com/videos/play/wwdc2024/10177/) 与 [增益图 headroom API](https://developer.apple.com/documentation/coreimage/ciimage/applyinggainmap(_:headroom:)) | SDR 与 HDR 是各自正确的呈现；增益图与元数据负责显示适配。不能因为 HDR 某个像素未超过白点，就认定该处必须退回 SDR 颜色。 |
| [CIToneMapHeadroom](https://developer.apple.com/documentation/coreimage/citonemapheadroom) 与 [WWDC22 EDR 示例](https://developer.apple.com/documentation/coreimage/generating-an-animation-with-a-core-image-render-destination) | 区分内容 headroom 和当前显示 headroom；在扩展线性色彩空间绘制，用指定 source/target headroom 进行映射，而非自定义逐像素最大通道硬压缩。 |
| [Core Image 内建滤镜处理](https://developer.apple.com/documentation/coreimage/processing-an-image-using-built-in-filters) 与 [Processing Images](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/CoreImaging/ci_tasks/ci_tasks.html) | CIImage 是延迟计算图，滤镜可能合并/重排；没有明确物化的缩小图不能直接当作全分辨率缓存的等价像素参考。 |
| [缓存中间图](https://developer.apple.com/documentation/coreimage/ciimage/insertingintermediate(cache:)) 与 [cacheIntermediates](https://developer.apple.com/documentation/coreimage/cicontextoption/cacheintermediates) | 缓存影响计算路径和复用；缓存就绪不代表缓存内容正确，须实际读回比较。 |

## 可能根因、证据与逆向证伪

### 1. 已确认：空间亮度门槛把两套颜色端点混为一幅图

上一轮 `hdrBrightnessImage` 执行：`P=max(HDR.r,HDR.g,HDR.b)`，`W=smoothstep(1,1.1,P)`，`output=SDR+(compressedHDR-SDR)*W*E(t)`。

即便 `E(t)=1`：

- P≤1 的区域永远显示 SDR 颜色，而非正确 HDR 颜色。
- 1<P<1.1 的区域永远停留在两种颜色之间。
- P≥1.1 的区域才完全进入 HDR。由此形成区域性的颜色关系差异；不只是时间上的缓动效果。

反例：SDR=(.45,.40,.35)，HDR=(.90,.60,.30)，可用 headroom=8。最终应该为 HDR，旧实现却返回 SDR；中间 50% 应为 (.675,.50,.325)，旧实现仍为 SDR。近白点反例最终应为红通道 1.05，旧实现为 .925。新增测试在修改生产算法前均失败。

真实四类样本也失败，详见完成报告中的修复前后比较。充足 headroom 条件下仍失败，反证“只是显示器亮度不够”；单像素可复现，反证“必须是文件解码损坏或缩放几何问题”。同一批数据仅替换端点处理就通过，为本次代码缺陷建立了因果证据。

### 2. 已发现的设计缺陷：绕过平台适配，并使预热与最终绘制使用不同图像图

旧可见帧与缓存使用最大 RGB 通道硬缩放，预热却使用原先的增益图 / CIToneMapHeadroom 路径。这会忽略文件的显示适配语义，也无法保证预热和实际显示内容一致。逐像素同比例缩放本身不直接改变该像素的 RGB 比例，因此不将“硬缩放本身必然偏色”当作已证明结论；已确认的偏色原因是前述空间掩码混合端点。此次一并统一显示适配路径。

### 3. 未获支持：持久缓存损坏或 ICC 变换错误

新增实际缓存像素比较最初得到最大误差 .130859，但参考图直接缩小 CI 图，实际缓存先全分辨率计算再缩小。控制变量实验仅把独立参考也先物化全分辨率，再以相同方式缩小，误差归零，生产缓存流程未为这个实验改动。结合官方延迟计算机制，直接缩小与全分辨率物化后的结果不能用作无条件相等的参考。因此保留这次失败日志，但不将其归因于缓存损坏或 ICC 错误，也没有放宽 .01 容差来让测试通过。

数学上，含空间增益的运算与重采样一般不交换：`R(S*G)` 与 `R(S)*R(G)` 不必相等。该解释是工程推导；实际实验确认的是两种采样口径产生差异，未声称定位到了 Core Image 内部某条未公开优化指令。

## 生产修复

移除自定义 CIColorKernel、空间白点门槛和逐像素最大通道压缩。`hdrDisplayEndpoint` 先确定正确端点：

1. current headroom≤1 时选择 SDR。
2. current≥content headroom 时直接使用解码 HDR，保留全部颜色。
3. 余量不足且有可用增益图时，通过 `imageByApplyingGainMap:headroom:` 重建。
4. 其他已知内容余量使用 `CIToneMapHeadroom`；无法使用新 API 的旧系统保留原生 EDR 合成器回退。

`hdrBrightnessImage` 只做 `SDR+(adaptedHDR-SDR)*E(t)` 的线性时间插值，进度 0/1 精确返回端点。预热、Metal 可见帧、最终持久缓存和图像测试探针共用 `hdrBrightnessDisplayImage`。正确的 HDR 中间调可能与 SDR 不同；“中间调基本稳定”不能解释为永久覆盖 HDR 中间调，否则与最终色彩正确性矛盾。相同端点的非高光区域仍保持不变。

色彩探针 `probeHDRBrightnessImage(..., sourceEndpoint=true)` 绕开待测显示处理，读取独立原生 HDR 参考；`probePersistentHDRPixels()` 读取实际缓存 CGImage。缓存参考明确采用全分辨率物化后再采样，不依赖就绪状态推断内容正确。

## 风险与验证边界

当前用户未提供发生偏色的具体文件或截图。本次已经复现并修复能导致局部颜色错误的生产缺陷，验证了本地四类真实样本；不能据此宣称已一比一复现用户那张照片的全部现场。数值容差用于 half-float 渲染误差，未宣称为视觉色差 ΔE 或亮度计测量结果。显示器 ICC、物理峰值和主观观感没有在多个物理显示器上验证。

## 2026-10-08 GitHub Actions 关闭测试计时修正

问题界定：提交 `1fffabb` 的 Build Fovelle [运行 37746276287](https://github.com/inostarlin-passion/Fovelle/actions/runs/37746276287) 编译成功，31 项 CTest 中仅 FovelleTests 失败；唯一 QtTest 失败为 Open With 关闭耗时断言。同一提交的 [Checks](https://github.com/inostarlin-passion/Fovelle/actions/runs/37746276317) 全部通过。

原子验收：A1 启动准备时间不占用关闭预算；A2 从请求 Open With 到窗口关闭及析构仍须小于 5000 ms，后台工作必须安全结束；A3 任意断言返回均恢复 quitOnLastWindowClosed；A4 推送后的两个工作流全部成功。

根因：计时器在 MainWindow 构造前启动，实际测量包含菜单初始化、窗口显示和图片加载。源码与两个远端任务交叉验证表明原断言无法区分慢启动与慢关闭。加入 5100 ms 准备延迟后旧代码稳定失败；缩小计时范围后同一输入通过，构成反证检查。未发现需修改生产关闭逻辑的证据。

设计：增加正常启动和慢启动数据行；就绪检查与准备延迟完成后、Open With 请求前启动单调计时器，作用域退出后读取耗时。保留五秒限制及真实 Cocoa 后台任务，用 qScopeGuard 恢复全局设置，并输出阶段耗时。

联网核查路径：失败 Actions 日志 → 同提交成功工作流 → MainWindow 关闭源码 → Qt 官方 API/测试建议。Qt 文档确认 [QElapsedTimer](https://doc.qt.io/qt-6/qelapsedtimer.html) 测量显式起点以来的时间，[QFutureWatcher](https://doc.qt.io/qt-6/qfuturewatcher.html) 指出 QtConcurrent::run 返回的 future 不能取消，因此保留 waitForFinished；[Qt Test Best Practices](https://doc.qt.io/qt-6/qttest-best-practices.html) 支持修复前失败/修复后通过的回归验证及 RAII 设置恢复。证据已充分，无需扩展到无关 HDR 或生产逻辑。
