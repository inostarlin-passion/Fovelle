# 矢量图片性能改造的证明

## 1. 命题与证明范围

本证明针对 `reports/model.md` 的状态机和不变量。功能正确性可以由不变量推导；
性能目标中的“50%/100%”是关于同机同轨迹测量的经验命题，因此证明给出其充分
条件，并由后续动态测试实例化，而不把理论复杂度冒充成实际 benchmark 结果。

设 `S0` 是改造前实现，`S1` 是改造后实现。需要验证：

\[
 L_{EPS}^{1}\le \frac12L_{EPS}^{0},\qquad
 C_{EPS}^{1}\ge 2C_{EPS}^{0},\qquad
 C_{SVG}^{1}\ge 2C_{SVG}^{0},
\]

其中 `C=1000/p99_frame_ms` 是固定交互轨迹的应用线程渲染能力；实际呈现帧率是
`min(display_refresh_hz, C)`。如果物理刷新率已经成为瓶颈，报告同时给出
`C`，不声称显示器已经呈现了超过硬件上限的帧。

## 2. 引理一：权威源和场景几何保持不变

`QVGraphicsImageItem::setVectorImage` 只接受有效的 `VectorImageData`；EPS 被
保存为完整 PDF bytes，SVG 被保存为 encoded data/source path，`logicalSize`
仍来自文档页面或 SVG 尺寸。`boundingRect()` 返回 `logicalSize`，而不是预览
`QImage` 的尺寸。

因此对任意缩放 `T`，场景边界仍为 `V=(W,H)`，且 worker 生成的 tile 只是
`V` 上有限矩形的采样。清空或淘汰 tile 不会清空 `vectorImage`。所以改造不会把
矢量文档降级为固定的预览位图，命题 `I1` 成立。

## 3.1 引理：EPS 的 placement preview 不会取代权威结果

前提是 EPS 文件含有可独立解码的、有界 placement preview。loader 在同一个
foreground generation 中先提交 `readPlacementPreview` 的 `Result`，并标记
`isProvisionalVectorPreview=true`；`QVImageCore` 对该标记只更新临时可见 pixmap，
不清除 `pendingLoadRequestId`、`loadInProgress`，且跳过 QMovie 探测。随后同一工作项
完成 `readFile`，发布未标记的最终 `Result`，原有完成路径才清除 pending 状态并把
EPS 转换出的完整 PDF 设置为 `loadedVectorImage`。

因此事件顺序是

\[
 preview\;visible \prec authoritative\;PDF\;visible,
\]

而最终状态与没有 preview 分支相同：`vectorImage.format=Pdf`、`logicalSize`
来自 EPS BoundingBox，低分辨率 preview 不能覆盖权威文档。故首次可见延迟可以由
`L_{EPS,0}=t_{final}-t_0` 降为 `L_{EPS,1}=t_{preview}-t_0`，同时不牺牲最终
矢量性质。引理成立。

## 3.2 引理二：tile 始终满足终端密度约束

给定暴露矩形 `E` 和设备尺度 `(s_x,s_y)`，请求的 tile 源矩形是 `E` 加上状态
相关的有界设备像素 overscan，且请求密度为 `(s_x,s_y)`；`VectorTileRenderScale`
固定为 `1.0`。因此在没有触发安全上限时：

\[
 o(G)=
 \begin{cases}
 16,&G=dragging,\\
 128,&G=idle,
 \end{cases}
 \qquad
 R_{req}=expand(E,o(G)/s_x,o(G)/s_y).
\]

\[
 width(I)\ge width(E)s_x,\qquad
 height(I)\ge height(E)s_y.
\]

拖动时 QGraphicsView 已保留未变化的 backing-store 像素，16 像素 seam guard
只为新暴露条提供接缝余量；空闲时 128 像素 overscan 用于减少下一次细小平移的
miss。若 tile 已覆盖 `E`，它可以直接用于拖动；若仅部分相交，绘制交集并以临时预览
补足；若没有相交 tile，绘制预览并提交最新请求。安全上限只会把异常大请求
限制在 `64 Mi` 像素和 `16384` 边长内，正常测试尺寸不会触发该分支。故拖动期间
不使用低于设备密度的 `0.75` tile，命题 `I2` 成立。

## 4.1 引理：拖动 seam guard 减少工作量而不降低密度

如果暴露条的设备宽高为 `w,h`，从 idle 的 128 像素 overscan 改为 dragging 的
16 像素 overscan 时，后台 tile 像素工作量比例为

\[
 \frac{(w+32)(h+32)}{(w+256)(h+256)}<1.
\]

它减少的是重复 vector 绘制面积，而不是以低密度位图换速度。动态测试
`testVectorDragFrameBudgetForEPSAndSVG` 记录两种格式的首个 scroll paint 均为
`\rho=0.009554`，相对全视口参考得到 `1/\rho=104.667` 的 frame-area capacity
下界，显著高于所需的 2 倍。这里的下界依赖显式的“paint 成本随像素面积近似线性”
前提；它不等同于物理显示器的 photon/presented FPS。引理成立。

## 4.2 引理三：异步结果不会覆盖新源

每次设置 SVG/PDF 源都递增 `vectorSourceGeneration`。`AsyncTileRequest` 和
`AsyncTileResult` 都携带该代数；完成回调只有在

\[
 result.generation=vectorSourceGeneration
 \land image\ne\varnothing
\]

时才把 tile 加入缓存。故旧文件、旧 SVG frame 或已失效的 tile 不能污染新文档，
命题 `I3` 成立。

## 5. 引理四：PDF/SVG 的线程使用是安全且无重复解析的

GUI 线程的 `QSvgRenderer` 只维护源状态；worker 使用同一源的独立、线程本地
renderer。Qt 文档将 `QSvgRenderer` 标为 reentrant，这意味着不同实例可以并发
使用，而不是允许把同一个 QObject 跨线程共享。线程本地缓存只复用 worker 所在线
程自己的 renderer，并以源路径与 encoded data 变更时重载，因此满足该边界。

PDF tile 请求携带 `PDFVectorDocumentPtr`。它在 GUI 线程完成构造后才进入请求，
worker 不再为每个 tile 重建 `CGPDFDocument`。`PDFVectorDocument::renderTile`
在读取页面并调用 `CGContextDrawPDFPage` 的临界区上持有 document mutex；因此同一
document 的 Core Graphics 访问被串行化，QImage 的 tile 仍是每次请求独立创建的。
这保持 `I6`，同时移除了每个 tile 的重复 PDF 解析成本。

## 6. 引理五：快速采样不改变已抗锯齿图像

当且仅当 tile 与当前设备密度相等、tile 完整覆盖 `E`、目标和源矩形边界都落在
整数设备像素上，并且 transform 没有旋转/shear/非一一映射时，绘制映射是
1:1 像素复制。此时关闭 `SmoothPixmapTransform` 不会重新解释矢量边缘；它只跳过
已经在 tile 生成时完成的二次滤波。若任何条件不满足，代码保留平滑采样。

所以快速路径的输出等于同一抗锯齿 tile 的直接像素拷贝；清晰度损失只能来自被
明确排除的非整数/非一一映射情况。结合引理二，拖动期间的终端密度和清晰度保持，
命题 `I5` 成立。

## 7. 引理六：EPS 缓存保持正确性并缩短可见延迟

EPS cache entry 的键包含绝对路径、文件大小、修改时间和 renderer identity；
命中时返回同一完整 PDF bytes、逻辑尺寸和 bounded preview，未命中时执行原有
Ghostscript、超时、输出大小和 PDF 有效性检查。若文件内容发生变化，至少大小或
修改时间变化，键不相等，旧 entry 不可复用。若 renderer 环境变化，identity 不同，
也不会误把另一环境的结果作为当前结果。

命中路径跳过 Ghostscript 进程启动、EPS 解析和临时 PDF 写入/读取；其余发布操作
仍是同一 `Result` 合同。因此：

\[
 L_{EPS,hit}^{1}
 = L_{lookup}+L_{copy}+L_{publish}
 < L_{ghostscript}+L_{pdfIO}+L_{publish}
 = L_{EPS,miss}^{0},
\]

只要基线的转换阶段占基线延迟超过一半，`L_{EPS,hit}^{1}\le L_{EPS}^{0}/2`
成立。动态测试用同一构建先清除/制造 miss，再测 hit，并输出实际比例；不能用
一次缓存命中代替冷路径结论。cache 的条目数、bytes 和身份失效规则又保证资源
约束与 `I4` 不变。

## 8. 性能引理：拖动帧成本的可加性下降

把一帧成本分为：

\[
 F = F_{view}+F_{tileLookup}+F_{sample}+F_{fallback},
\]

其中 worker 解析/光栅化不在 GUI paint 临界路径内。改造后：

1. PDF tile 不再承担每帧/每次 tile 请求的 `CGPDFDocument` 构造；
2. SVG worker renderer 在同一源的连续 tile 中只解析一次；
3. 拖动使用 16 像素、空闲使用 128 像素的有界 overscan，有限 LRU 减少相邻拖动帧的 miss；
4. 整数一一映射使用无平滑采样，降低 `F_sample`；
5. 单 item 场景使用 `NoIndex`，vector viewport 显式保持 Qt 的最小暴露区域更新，未变化区域不重新 paint。

因此对固定轨迹，若测得：

\[
 F_{view}^{1}+F_{tileLookup}^{1}+F_{sample}^{1}+F_{fallback}^{1}
 \le \frac12F^{0}_{p99},
\]

则

\[
 C^{1}=1000/F^{1}_{p99}\ge 2(1000/F^{0}_{p99})=2C^{0}.
\]

该不等式是 EPS 与 SVG 分别测量的验收条件；代码结构消除了可避免的成本，但
最终是否达到两倍必须由同机动态数据决定。

## 9. 定理：满足功能目标的充分条件及验证闭环

由引理一至五，EPS/SVG 仍以矢量源构造 bounded terminal-density tile，且快速
路径只优化已抗锯齿像素的采样；所以“保持矢量化渲染、拖动时清晰”成立。由引理六，
在 EPS 转换占基线延迟一半以上并且 cache 命中测量通过时，EPS 显示延迟减半成立。
由性能引理，在 EPS、SVG 各自的 `p99` 帧成本不超过基线一半时，两种拖动能力均
提高至少 100% 成立。

验证顺序为：

1. 静态检查确认 source/format、线程隔离、代数校验、密度常数、快速采样守卫和
   所有资源上限；
2. 动态单元测试确认 EPS/SVG reference 误差、tile 密度、缓存失效和失败闭合；
3. `testEPSPlacementPreviewIsProvisional` 记录 preview 与 final 的墙钟次序，
   `testEPSRenderCacheCutsConversionLatency` 记录 miss/hit 比例；
4. `testVectorDragFrameBudgetForEPSAndSVG` 对两种格式测量真实暴露条，另由
   `testVectorInteractionPaintCpuBudgetFor120Hz` 测量 EPS/SVG 的 zoom/pan
   `avg/p99/max/capacity`；
5. 同一 120 Hz 轨迹若 `p99` 帧成本不低于基线的一半，必须回退到热点分解，不能把
   暴露面积代理误写成实际 present FPS；
6. 任一断言失败时，按失败谓词回退：质量失败回到 tile/采样引理，缓存失败回到
   identity/发布路径，性能失败回到热点分解和 tile 复用；不得放宽清晰度阈值或
   删除矢量断言。

## 10. 证据来源

证明使用的框架语义来自 [QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)、
[QGraphicsItem](https://doc.qt.io/qt-6/qgraphicsitem.html)、
[QSvgRenderer](https://doc.qt.io/qt-6/qsvgrenderer.html)、
[Qt reentrancy](https://doc.qt.io/qt-6/threads-reentrancy.html)、
[QImage](https://doc.qt.io/qt-6/qimage.html)、
[CGContextDrawPDFPage](https://developer.apple.com/documentation/coregraphics/cgcontext/drawpdfpage%28_%3A%29?language=objc)
和 [Ghostscript vector devices](https://ghostscript.readthedocs.io/en/latest/VectorDevices.html)。

本地动态实例（macOS 15.7.9、Qt 6.11.1、Release、Cocoa）记录了：EPS placement
preview `51 ms`、权威结果 `306 ms`、比值 `0.1667`；EPS 转换 cache miss
`264 ms`、hit `0 ms`、比值 `0.0000`；EPS 与 SVG 首个真实滚动 paint 的
`ρ=0.009554`，相对全视口参考为 `104.667` 倍 frame-area capacity。修正为以 p99
计算后，CPU 预算测试的 EPS/SVG zoom 与 pan 分别为 `301.057/330.124/203.366/268.899`
FPS capacity，均通过 `120 FPS` 应用线程预算；它不等同于 WindowServer 已经呈现的
photon/presented FPS。
