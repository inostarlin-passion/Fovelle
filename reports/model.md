# 矢量图片显示与交互性能模型

## 1. 问题边界与可验证目标

本文把任务解释为：在不把 EPS/SVG 的权威源替换为固定尺寸位图的前提下，缩短 EPS 从请求到首次可见内容的延迟，并把拖动期间的可持续渲染能力提高到基线的两倍。这里的“帧率”采用可重复的渲染能力定义：在同一显示器、同一视口、同一轨迹和同一刷新上限下，完成一帧所需的应用线程 CPU 时间越短，可持续帧率越高；最终仍受物理显示刷新率上限约束。对拖动还单独测量实际 `QGraphicsView` 暴露区域，因为滚动时未变化的 backing-store 像素不会再次进入场景绘制。

设基线为当前版本、相同构建类型和相同测试轨迹下的测量值，下标 `0` 表示基线，下标 `1` 表示改动后：

\[
 L_{EPS,1} \le 0.5 L_{EPS,0},
\qquad
 FPS_{EPS,1} \ge 2 FPS_{EPS,0},
\qquad
 FPS_{SVG,1} \ge 2 FPS_{SVG,0}.
\]

其中 `L_EPS` 是从加载请求被接受到第一帧非空预览或权威矢量内容进入视口的墙钟时间。对于含 placement preview 的 EPS，改动前的可见延迟基线 `L_EPS,0` 是单结果链路等待权威转换完成，改动后的 `L_EPS,1` 是先发布 placement preview；没有内嵌 preview 的 EPS 不宣称该条路径达标，而继续使用有界权威转换。`FPS` 是固定交互轨迹中由应用线程完成的有效 viewport paint 次数与轨迹时长之比，另报告 CPU capacity `1000 / p99_frame_ms`，避免把 120 Hz 显示器的硬上限误认为渲染器速度。冷启动和同一进程内再次打开分别记录，不能把缓存命中伪装成冷启动改进。

## 2. 对象、输入、输出

### 2.1 对象

* `D`：文件文档。EPS 的权威内容是 PostScript 程序，SVG 的权威内容是 XML/SVG 绘制命令。
* `K(D)`：文件身份 `(absolutePath, fileSize, lastModified)`；缓存只有在身份和源类型都匹配时才可复用。
* `V=(W,H)`：文档的逻辑尺寸，单位为 SVG user space 或 PDF point。它是场景坐标的完整边界，而不是预览位图的尺寸。
* `T`：从场景坐标到设备坐标的 `QTransform`。令 `s_x=|T(1,0)-T(0,0)|`、`s_y=|T(0,1)-T(0,0)|`。
* `E`：当前 `QGraphicsItem` 的暴露矩形，且 `E ⊆ [0,W]×[0,H]`。
* `C`：有界矢量 tile 集合。每项为 `(I,R,d_x,d_y,g)`，其中 `I` 为 QImage，`R` 是其覆盖的逻辑源矩形，`d_x,d_y` 是生成时的设备密度，`g` 是源代数。
* `G`：交互状态，取 `idle` 或 `dragging`。
* `P`：小尺寸 Qt 兼容预览，只能作为首次显示/异步 tile 到达前的临时 fallback，不能改变 `V`、源类型或权威渲染路径。
* `A_v`：视口逻辑像素面积；`A_e`：一次滚动导致的首个暴露 paint 区域面积；`ρ=A_e/A_v` 是拖动局部更新比例。

### 2.2 输入

一次显示请求输入 `(K(D), D, V, T, E, G)`；一次拖动还输入按时间排序的滚动/变换序列 `Δ_1…Δ_n` 和显示刷新上限 `H`。EPS 还需要可用的、受超时和输出大小限制的 Ghostscript；Ghostscript 生成的 PDF 仅是保留矢量绘制命令的中间表示。SVG 在 GUI 线程保留一个 renderer 用于文档状态，在 worker 线程使用独立 renderer；renderer 不能跨线程共享。

### 2.3 输出

* `firstVisible`: 第一帧中非空的预览或矢量内容；
* `authoritative`: EPS 的 PDF vector document 或 SVG 的 encoded SVG source；
* `frame_i`: 在 `E_i` 上绘制的设备像素；
* `quality(frame_i)`: 与同一 `T_i,E_i` 下直接以目标设备密度从权威源渲染的参考图的采样通道平均误差；
* `L_EPS`、`FPS`、`p99_frame_ms` 和 tile/cache 统计。

## 3. 行为与状态转移

### 3.1 EPS 加载

当前冷路径是：

\[
 D_{EPS}
 \xrightarrow{Ghostscript/pdfwrite}
 PDF_{bytes}
 \xrightarrow{CGPDFDocument}
 P + V
 \longrightarrow
 QVGraphicsImageItem.
\]

改进后的路径分为两个可观察事件：若 EPS 带有可解码的内嵌 placement preview，先由 `readPlacementPreview` 解码并发布 `P`，随后同一 request id 再发布权威结果；若没有 preview，则只发布权威结果。另增加一个按 `K(D)` 校验的受限转换缓存，并把同一次转换的 PDF bytes、逻辑尺寸和预览作为一个不可变结果发布。缓存命中跳过 Ghostscript 和临时文件往返；缓存未命中仍必须执行原有安全检查、超时、失败闭合和权威 PDF 保存。无论命中与否，`authoritative` 都必须是完整 PDF vector document，`P` 不得成为唯一结果。

### 3.2 SVG/EPS tile 绘制

对 `E` 先扩展一个状态相关但有界的设备像素预算，得到 `R_req`：

\[
 o(G)=
 \begin{cases}
 16,&G=dragging,\\
 128,&G=idle.
 \end{cases}
 \qquad
 R_{req}=expand(E,o(G)/s_x,o(G)/s_y).
\]

拖动时 `QGraphicsView` 已经通过 backing store 保留未变化区域，因此 16 像素 seam guard 足以保护新暴露条；空闲时 128 像素 overscan 用于减少下一次细小平移的 miss。请求尺寸为：

\[
 size(I)=bound\big((width(R_{req})s_x,
                         height(R_{req})s_y)\big),
 \qquad d_x=s_x,\ d_y=s_y.
\]

`bound` 保持当前最大像素数和最大边长限制，因此 tile 代价不随整幅文档尺寸无限增长。`G=dragging` 时只允许同样的终端设备密度 tile；不能以 `0.75s` 或其他低于显示密度的采样代替矢量清晰度。worker 结果按代数 `g` 校验，过时结果丢弃。场景只有一个图片 item，索引策略固定为 `NoIndex`，避免在每个 scroll frame 维护无收益的 BSP。

PDF tile 使用已构造的 `PDFVectorDocument`，不在每个 tile 上重新创建/解析 CGPDFDocument；Core Graphics 绘制由 document 内部的互斥保护。SVG tile 在 worker 线程缓存同一源的 renderer 解析结果，之后只改变 frame/viewBox 并绘制当前 tile；GUI renderer 和 worker renderer 始终是不同实例。

如果 `C` 中有覆盖 `E` 的同密度 tile，则直接绘制；如果只有部分重叠 tile，则只绘制交集并以 `P` 补足；没有覆盖时绘制 `P`，同时只提交最新 tile 请求。请求完成后加入 LRU 有界缓存。`QGraphicsView` 显式使用 `MinimalViewportUpdate`，矢量视口在 macOS 上满足不透明背景条件，故一次滚动的首个 paint 只覆盖暴露条。

### 3.3 清晰的快速采样条件

终端密度 tile 本身已由矢量源进行抗锯齿光栅化。只有同时满足以下条件时，拖动帧才允许关闭 `SmoothPixmapTransform`：

1. tile 密度与当前 `s_x,s_y` 相等（相对误差不超过 `10^{-9}`）；
2. 目标矩形和 tile 源矩形在设备坐标中的左右/上下边界均接近整数像素；
3. tile 源矩形完整覆盖暴露矩形，且没有 zoom/rotate/shear 导致的非一一像素映射。

此时是 1:1 的已抗锯齿像素复制，而不是把矢量边缘改为低分辨率位图；任一条件不满足时继续平滑采样。这样优化的是采样开销，不牺牲交互 tile 的设备密度。

## 4. 约束与不变量

### 4.1 正确性不变量

* `I1`：对任意可见缩放，权威源保持可访问；`vectorImage.format` 仍为 PDF/SVG，场景 `boundingRect` 仍为 `V`。
* `I2`：任何交互 tile 都满足 `tile.width >= width(R_visible)*s_x` 且 `tile.height >= height(R_visible)*s_y`，除非 tile 被安全上限裁剪；测试输入必须证明未触发裁剪。
* `I3`：当前代数以外的 worker 结果不进入 `C`。
* `I4`：EPS 缓存键失配、Ghostscript 缺失、超时、非法 PDF 或超限输出均失败闭合，不能回落为 EPS 内嵌的低分辨率 placement preview。
* `I5`：固定矩形和固定密度下，快速采样的参考误差不高于平滑采样/直接矢量参考的既有容差；空白、透明度和边缘不能因 cache 或 tile 交界产生可见缝隙。
* `I6`：所有跨线程对象满足 Qt 的 reentrant 使用边界：worker 不使用 GUI renderer；PDF document 的 Core Graphics 访问被显式串行化。

### 4.2 资源约束

单 tile 不超过当前 `64 MiB` 像素上限和 `16384` 边长；多 tile 保持当前有界 LRU 和内存上限；EPS Ghostscript 仍受 30 秒超时、256 MiB PDF 上限和有限错误诊断限制。EPS 转换缓存也必须有条目数/字节上限，并在 `K(D)` 变化时失效。

## 5. 可观测验证量

静态分析检查：vector source/format、`VectorTileRenderScale=1.0`、异步 worker、PDF document 复用、SVG worker renderer 隔离、快速采样的整数像素守卫和所有安全上限。

动态测试检查：

1. EPS/SVG 在 2048 目标密度下与直接 vector reference 的平均采样差异 `<3.0`；交互 tile 不低于目标设备密度；
2. EPS 第一次加载成功、失败和缓存身份变化均得到正确结果；缓存命中延迟相对同一构建的未命中基线不超过 50%；
3. 固定 120 Hz 轨迹分别测 EPS/SVG 的 zoom 和 pan，报告平均、p99、最大 CPU frame time，并要求改动后的有效 capacity 至少为基线两倍；
4. EPS/SVG 真实滚动测试记录 `ρ=A_e/A_v`，以全视口重绘 `ρ₀=1` 为同轨迹参考；在“paint 成本随绘制像素面积单调近似线性”的显式前提下，`1/ρ≥2` 是两倍 frame-area capacity 的验收下界；
5. 平台刷新率只作为 `min(H, capacity)` 的上限，不把物理上限伪造为软件提升。

## 6. 依据

* Qt `QGraphicsView` 的更新模式、滚动和缓存语义：[QGraphicsView 文档](https://doc.qt.io/qt-6/qgraphicsview.html)。
* Qt item cache 的质量及变换限制：[QGraphicsItem 文档](https://doc.qt.io/qt-6/qgraphicsitem.html)。
* SVG renderer 可向 QPainter/QImage 输出且接口为 reentrant：[QSvgRenderer 文档](https://doc.qt.io/qt-6/qsvgrenderer.html)；Qt 的 reentrant/thread-safe 区分见 [Threads and QObjects](https://doc.qt.io/qt-6/threads-reentrancy.html)。
* QImage 的隐式共享和 reentrant 约束见 [QImage](https://doc.qt.io/qt-6/qimage.html) 与 [Implicit Sharing](https://doc.qt.io/qt-6/implicit-sharing.html)。
* PDF 绘制保持分辨率无关并由当前 CGContext 输出：[CGContextDrawPDFPage](https://developer.apple.com/documentation/coregraphics/cgcontext/drawpdfpage%28_%3A%29?language=objc)。
* Ghostscript 的 vector device 说明：[Vector Devices](https://ghostscript.readthedocs.io/en/latest/VectorDevices.html)。

## 7. 多跳检索链

检索从平台能力下钻到渲染实现：

1. Apple 的 PDF 文档确认 PDF 是可由 `CGContextDrawPDFPage` 绘制的分辨率无关表示；
2. Ghostscript 的 Vector Devices 文档确认 `pdfwrite` 属于高层输出设备，因此 EPS 可先规范化为可保留绘制命令的 PDF；
3. Qt `QGraphicsView` 文档确认 viewport 更新模式与滚动优化影响未变化区域，`QGraphicsItem` 文档确认内建 item cache 在变换时有质量/重建代价；
4. Qt `QSvgRenderer` 与线程 reentrancy 文档确认不同 renderer 实例可在线程间独立使用，但 QObject 实例不能跨线程搬运；
5. 最后将这些框架事实映射到本地代码的 `readPlacementPreview`、EPS LRU、持久 PDF、worker-local SVG renderer、16/128 像素 seam guard 与动态测试日志。

前四步是外部文档事实，最后一步是对本仓库源码的演绎映射；它不是把外部文档当作本地性能测量的替代。
