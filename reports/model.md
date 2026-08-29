# EPS 首帧与稳定帧一致性模型

## 1. 问题边界与目标

问题对象是应用打开一个 EPS 文件时的可见图像序列。用户观察到：打开后的第一
个可见结果与异步处理完成后的稳定结果不同。本次修复的目标不是把低分辨率预览
调得更像最终图，而是让两个结果使用同一个权威内容源，从源头消除内容跳变。

本模型把“首帧”定义为该 EPS 请求在 `QVImageLoader::imageReady` 发出的第一份
成功 `Result`；把“稳定帧”定义为同一 EPS、同一页面变换和同一目标像素尺寸下，
由已保存的权威 PDF 文档直接光栅化的参考结果。窗口合成器、显示器刷新和不同
目标尺寸造成的正常抗锯齿差异不属于本问题。

## 2. 对象、输入和输出

设 EPS 输入为

\[
 D=(PS, B, P_{embedded}),
\]

其中 `PS` 是 PostScript 正文，`B` 是 EPS BoundingBox，`P_embedded` 是可选的
内嵌 TIFF/EPSI placement preview。Adobe 的 EPS 说明把 preview 定义为供不能
直接显示 EPS 的程序使用的显示辅助内容，而不是 artwork；因此它不是本模型的
权威源。

转换器 `G` 是受资源限制的 Ghostscript `pdfwrite` 子进程，定义

\[
 A = G(PS,B),
\]

其中 `A` 是完整 PDF bytes，并保留 PDF 页面逻辑尺寸 `V=(W,H)`。Core Graphics
PDF 文档 `R_A` 由 `A` 构造，`Render(R_A,T,E,S)` 表示在页面变换 `T`、暴露区域
`E` 和目标像素尺寸 `S` 下绘制的图像。

一次成功的 EPS `Result` 定义为不可变三元组：

\[
 Result=(F,A,V), \qquad F=Render(R_A,I,S_F),
\]

其中 `I` 为整页逻辑区域，`S_F` 为有界的 fallback 尺寸（当前上限为最大边
2048，生产 loader 还会把请求尺寸限制在该上限内）。关键约束是 `F` 必须从
同一个 `A` 生成，不能从 `P_embedded` 生成。

## 3. 状态和行为

### 3.1 旧的错误状态机

旧路径可以抽象为：

```text
Loading --decode embedded preview--> Visible(P_embedded)
        --Ghostscript -> PDF-------> Visible(Render(A))
```

由于 `P_embedded` 与 `PS` 可能来自不同绘图/解码路径、尺寸和裁剪规则，两个
可见状态没有等价关系，故出现“打开瞬间”和“稳定后”图像不同。

### 3.2 修复后的状态机

修复后的路径为：

```text
Loading --Ghostscript/pdfwrite--> Authoritative(A)
        --render A at bounded S_F--> Visible(F, A)
        --optional async tile from A--> Stable(Render(A,T,E,S_T))
```

`QVImageLoader::startJob` 只运行一次 `readFile` 并提交一次最终 `Result`；不再
调用或发布 `readPlacementPreview`，也不存在 provisional EPS 结果。`QVImageCore`
收到该结果后同时保存 `F` 和 PDF `vectorImage`，并因为它是有效矢量文档而跳过
QMovie 探测。`QVGraphicsImageItem` 后续的 PDF tile 复用同一个 PDF 文档；因此
异步细化改变的是同一 `A` 的采样密度/暴露区域，不改变图形内容。

重复打开时，EPS LRU cache 的键为绝对路径、文件大小、修改时间、fallback 请求
尺寸和 Ghostscript 可执行文件身份。命中返回同一类 `(F,A,V)`；文件或 renderer
身份变化时键失效，重新执行受限转换。

## 4. 约束和性质

定义同源首帧误差

\[
 \delta(F)=mean_{x\in I}\,|F(x)-Render(R_A,I,S_F)(x)|.
\]

验收阈值为 `δ(F) < 3.0` 个采样通道单位；这是同尺寸比较的像素等价判据，允许
平台光栅器的极小舍入差异。对于稳定 tile，保持其原有设备密度，而不是以较低
分辨率位图替代矢量源。

必须保持以下不变量：

1. `vectorImage.format == Pdf`，且 `vectorImage.encodedData == A`；场景逻辑
   几何仍来自 `B`，不来自 fallback 的像素尺寸。
2. 首个成功 EPS `Result` 已同时含有 `F` 和 `A`；不存在先发布
   `P_embedded`、再以 `A` 覆盖的可见事件。
3. 任意 PDF tile 都由同一个 `A` 的 `PDFVectorDocument` 产生；过时代数的
   异步结果不能进入当前 tile 缓存。
4. Ghostscript 缺失、超时、非法 PDF、输出超限或缓存键失配均失败闭合，不能
   回落到 `P_embedded`。
5. EPS 已有的缓存、持久 PDF 文档、设备密度 tile、拖动局部更新和 SVG worker
   renderer 优化继续保留；这些优化只能改变成本，不能改变权威内容源。

## 5. 显式前提

* “稳定图像”按同一 EPS/PDF、同一变换、同一目标尺寸的直接 PDF 绘制定义；若
  稳定阶段请求了更高设备密度，边缘抗锯齿可能不同，但几何、颜色语义和内容
  必须相同。
* Ghostscript `pdfwrite` 对给定稳定输入产生确定的 PDF 结果；若 renderer
  版本或文件身份改变，缓存键必须变化。
* `δ<3.0` 是本工程的可重复像素判据，不把操作系统窗口合成误归因给图像解码。
* 任务只要求修复 EPS 首帧跳变；不以一次 cache hit 的时间数据推断冷启动延迟。

## 6. 验证映射

| 性质 | 静态/动态证据 |
| --- | --- |
| 首帧不使用内嵌 preview | native 源不再包含 `readPlacementPreview`；loader/UI 只有最终 Result 路径 |
| 首帧包含权威 PDF | `testEPSInitialFrameMatchesStableRender` 检查第一份 Result 的 PDF bytes 和格式 |
| 首帧与稳定绘制同源 | 测试按第一份 fallback 尺寸从其 PDF 重绘，并断言 `δ<3.0` |
| 矢量性质保持 | EPS render/loader 测试断言 PDF vector document、BoundingBox 几何和 tile 结果 |
| 失败闭合 | 损坏 EPS、Ghostscript 缺失和超限检查保持通过 |
| 非回归 | EPS cache、静态 movie probe、SVG/EPS vector interaction 和完整 CTest 通过 |

## 7. 多跳检索依据

1. [Adobe artwork/EPS 文档](https://helpx.adobe.com/uk/illustrator/using/saving-artwork.html)
   区分 EPS artwork 与 preview，建立“内嵌 preview 非权威”的格式前提。
2. [Ghostscript Vector Devices](https://ghostscript.readthedocs.io/en/latest/VectorDevices.html)
   说明高层 vector device；因此选择 `pdfwrite` 作为保留绘图命令的中间表示。
3. [Apple CGContextDrawPDFPage](https://developer.apple.com/documentation/coregraphics/cgcontext/drawpdfpage%28_%3A%29?language=objc)
   确认 PDF 页面可在 CGContext 中按目标输出绘制，适合作为同源 fallback 与 tile
   的权威 renderer。
4. [Qt QGraphicsItem](https://doc.qt.io/qt-6/qgraphicsitem.html) 和
   [Qt QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html) 说明 item
   `paint()`/`boundingRect()` 与 viewport 更新的职责边界：后续 tile 只应重绘
   暴露区域，不应替换文档源。
5. [Qt QSvgRenderer](https://doc.qt.io/qt-6/qsvgrenderer.html) 及
   [Qt threads/reentrancy](https://doc.qt.io/qt-6/threads-reentrancy.html)
   支撑 SVG 线程隔离作为非回归约束，而不是 EPS 首帧的另一内容源。
