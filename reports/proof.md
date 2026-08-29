# EPS 首帧—稳定帧一致性的证明

## 1. 命题

设一次 EPS 请求成功，`D` 为输入 EPS，`A=G(D)` 为受限 Ghostscript `pdfwrite`
产生的 PDF bytes，`V` 为 EPS BoundingBox 对应的逻辑页面。需要证明：

1. 第一份成功 `Result` 的图像 `F` 与稳定参考图像来自同一权威源 `A`；
2. 第一份结果不是内嵌 placement preview 的暂态替代物；
3. 矢量文档和后续高密度 tile 语义不被修复破坏。

稳定参考定义为 `Render(A,T,E,S)`。当 `T/E/S` 与第一份 fallback 的整页比较条件
相同，验收要求为 `δ(F)<3.0`。

## 2. 引理一：内嵌 preview 不是权威 EPS 内容

由 EPS 格式的输入分解，`D=(PS,B,P_embedded)`，其中 `P_embedded` 只是可选的
显示辅助内容，而 artwork 由 `PS` 定义。故一般不能推出

\[
 P_{embedded}=Render(G(PS,B)).
\]

尤其当 preview 的尺寸、裁剪、颜色解释或生成器与当前 PostScript renderer
不同时，两者可以不同。于是“先显示 preview、后显示 PDF”必然允许用户观察到
跳变。要消除该类跳变，必要条件是首帧不以 `P_embedded` 作为图像结果。

## 3. 引理二：loader 不再发布 provisional EPS 结果

修复后的 `QVImageLoader::startJob` 只调用：

```text
result = readFile(path, requestedLargestDimension, isPreload)
jobFinished(path, generation, result)
```

源码中已移除 `readPlacementPreview` 和 `jobPreview`；因此对一个 generation，
loader 不存在“先 preview Result、后 final Result”的 EPS 分支。`jobFinished`
完成 identity、generation 和错误状态检查后，`deliverResult` 只交付该一次
`Result`。故第一份成功 `imageReady` 必然是 `readFile` 产生的结果，而不是
`P_embedded`。

## 4. 引理三：EPS fallback 与稳定参考同源

`readEPS` 的成功路径先执行

\[
 A=G(PS,B),
\]

然后用 `imageFromPDFPage(pdfPath, S_F, ...)` 生成 `F`，最后把同一 PDF 文件读入
`result.vectorImage.encodedData=A`。所以存在同一个 `A` 使得

\[
 F=Render(A,I,S_F),\qquad vectorImage=A.
\]

测试 `testEPSInitialFrameMatchesStableRender` 重新从第一份 Result 的 `A` 构造
`PDFVectorDocument`，以完全相同的 `S_F` 重绘整页 `F'`，并断言

\[
 \delta(F,F')<3.0.
\]

由于 `F` 和 `F'` 使用相同 bytes、页面区域和尺寸，若断言通过，首帧与同尺寸
稳定 PDF 绘制不存在内容级差异；允许的误差仅是平台采样舍入。

## 5. 引理四：UI 不会用动画探测再次替换 EPS

`QVImageCore::loadPixmap` 在收到第一份 Result 后保存 `loadedVectorImage=A`。
当 `loadedVectorImage.isValid()` 时，`isVectorDocument=true`，`loadedMovie`
的文件名被清空且不启动 QMovie。于是延迟的静态/动画探测不能把 EPS 当成普通
图片再次读取，也没有第二个低分辨率内容源可以覆盖 `A`。测试
`testEPSRenderSurvivesStaticMovieProbe` 在等待 1100 ms 后检查 movie 状态仍为
`NotRunning` 且 PDF vector document 仍有效。

## 6. 引理五：后续稳定 tile 仍来自同一个矢量文档

`QVGraphicsImageItem::setVectorImage` 由第一份 Result 的 `A` 构造持久
`PDFVectorDocument`。异步 tile request 携带该 shared document 与当前
`vectorSourceGeneration`；结果只有在 generation 仍匹配时才会进入 tile 缓存。
因此每个稳定 tile 都是

\[
 Tile_i=Render(A,T_i,E_i,S_i),
\]

而不是从 `P_embedded` 或第一份 fallback 继续放大。设备密度和已有的边界/采样
守卫保证清晰度约束仍成立；本修复只是删除错误的首帧内容源。

## 7. 引理六：错误路径不会以错误 preview 掩盖失败

Ghostscript 缺失、进程启动失败、超时、诊断输出超限、PDF 输出超限或 PDF 无效
时，`readEPS` 返回错误；`readImageWithImageIO` 对已识别 EPS 保持
`allowsQtFallback=false`。由于 preview 路径已删除，失败结果不能偷偷变成
`P_embedded`，而是沿现有 `errorData` 合同闭合。故修复没有以“看起来先有图”为代价
掩盖依赖错误。

## 8. 定理：打开瞬间与稳定后的 EPS 图像不再发生源级跳变

由引理二，第一份成功结果是 `readFile` 的最终结果；由引理三，该结果的 fallback
和 vector source 都由同一个 `A` 生成；由引理四，延迟 QMovie 探测不会再发布
第二个普通图片结果；由引理五，后续 tile 仍从 `A` 生成。因此首帧到稳定帧的
变化至多是同一权威 PDF 在不同 `T/E/S` 下的正常采样细化，而不再是

\[
 P_{embedded}\longrightarrow Render(A).
\]

在同尺寸验收条件下，测试断言 `δ<3.0`，故任务“打开 EPS 图片后的瞬间与稳定
后的图像不同”所指的内容跳变被修复。

## 9. 缓存与非回归证明

EPS cache 的键包含路径、文件大小、修改时间、fallback 尺寸和 Ghostscript
身份。键相等时返回同一完整 `A` 及其同源 `F`；键不等时不复用旧结果。故缓存
只能减少重复转换，不能引入另一内容源。持久 PDF document、设备密度 vector
tile、SVG worker renderer、暴露区域更新和快速采样路径均未被首帧修复删除。

因此可将一致性修复与已有的性能优化组合：首帧正确性由本证明和 EPS consistency
测试负责，重复打开速度由 cache 测试负责，拖动清晰度和局部更新由 vector
interaction 测试负责；三者的验收谓词彼此独立。

## 10. 反例回退规则

若 `testEPSInitialFrameMatchesStableRender` 失败：

* 若第一份 Result 不是 PDF，回退检查 loader/native Result 合同，不放宽测试；
* 若 PDF 有效但 `δ≥3.0`，回退检查 `imageFromPDFPage` 与 `PDFVectorDocument`
  的页面 transform、BoundingBox 和目标尺寸；
* 若等待后 vector source 改变，回退检查 QVImageCore 的 movie probe 和 loader
  generation，而不是重新引入 preview；
* 若错误样例出现图片，回退检查 `allowsQtFallback=false` 和 failure-closed
  条件。

只有在这些谓词都通过后，才可接受实现。

## 11. 外部依据与本地映射

证明的外部语义链为：

1. [Adobe EPS artwork/preview](https://helpx.adobe.com/uk/illustrator/using/saving-artwork.html)：
   preview 不是 artwork 权威源；
2. [Ghostscript Vector Devices](https://ghostscript.readthedocs.io/en/latest/VectorDevices.html)：
   `pdfwrite` 可作为保留矢量绘图命令的高层输出设备；
3. [Apple CGContextDrawPDFPage](https://developer.apple.com/documentation/coregraphics/cgcontext/drawpdfpage%28_%3A%29?language=objc)：
   PDF 页面可按目标上下文绘制；
4. [Qt QGraphicsItem](https://doc.qt.io/qt-6/qgraphicsitem.html) 与
   [QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)：item 绘制和
   viewport 更新应使用当前文档/暴露区域；
5. 本地实现把上述事实落实为：`readEPS -> PDF -> F` 单源路径、无
   `readPlacementPreview`、PDF vector document 复用及首帧同源测试。

外部资料提供框架/格式语义；首帧差异的具体定位和 `δ<3.0` 结果必须由本地源码
与动态测试确认，不能由网页资料替代。
