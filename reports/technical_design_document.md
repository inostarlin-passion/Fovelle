# EPS/SVG缩放与拖动掉帧修复设计

日期：2026-10-18（Asia/Shanghai）。基线：0738409。问题拆解：区分输入派发、矢量解析、终端密度栅格化、显示颜色准备、GUI绘制、异步请求合并与缓存生命周期。先测现状，再以同样样本/分辨率/轨迹比较修复。

## 证据与多跳核验

1. src/qvgraphicsimageitem.cpp的paint在每帧调用getIccProfileForWindow和QColorSpace::fromIccProfile，新tile还在GUI调用convertedToColorSpace；SVG源RGBA64、EPS源RGBA32F，输出保持高位深。这是需要测量的候选根因，不能仅凭源码就宣布实际帧率。
2. [Qt QPainter性能说明](https://doc.qt.io/qt-6/qpainter.html)：ARGB32_Premultiplied/RGB32是栅格引擎优化格式；高位深图片画入窗口可能额外格式转换。
3. [Qt QImage线程说明](https://doc.qt.io/qt-6/qimage.html)、[Qt线程模块](https://doc.qt.io/qt-6/threads-modules.html)：QImage可在线程独立处理，隐式共享不等同于跨线程同时修改同一实例。
4. [Qt线程基础](https://doc.qt.io/qt-6/thread-basics.html)及[Mandelbrot例子](https://doc.qt.io/qt-6/qtcore-threads-mandelbrot-example.html)：昂贵图像工作在worker，GUI发布/绘制；QWidget及QPixmap留在GUI。
5. [QWindow screenChanged](https://doc.qt.io/qt-6/qwindow.html)、[QTimer](https://doc.qt.io/qt-6/qtimer.html)：显示器变更用事件更新；同屏profile变化有低频轮询兜底，不在每次paint执行系统profile解析。

6. [Apple AppKit线程安全说明](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/ThreadSafetySummary/ThreadSafetySummary.html)与上述Qt线程资料交叉核验：窗口/NSView访问保持在GUI线程的非paint事件中，不能把NSView窗口profile查询直接搬到worker；只有独立QImage准备下放。
7. [Qt 6.11.2 QImage源码](https://github.com/qt/qtbase/blob/v6.11.2/src/gui/image/qimage.cpp)核对ARGB32优先优化格式说明，与转换API文档、当前Qt库上的目标空间及像素测试互证。

反向证伪：已经异步栅格化不代表paint没有昂贵工作；不能把16位/浮点源直接降为8位sRGB，因为此前色彩修复必须保留。不能恢复有拖影的局部backing-store滚动，也不能以降低tile设备密度掩盖掉帧。物理60Hz/120Hz呈现与CPU paint耗时不同，报告区分两者。

## 原子验收

| ID | 标准 | 测试 |
|---|---|---|
| P1 | paint路径不读取/解析系统ICC | S1、D1的profile查询计数 |
| P2 | paint路径不执行颜色或像素格式转换 | S2、D1 GUI转换计数 |
| P3 | worker在发布前完成目标ICC转换及ARGB32 premultiplied显示准备 | S2、D1格式与worker计数 |
| P4 | 色彩先转换再量化，高精度源保留 | D2参考像素；既有color rendering suite |
| P5 | 目标显示空间变化使旧结果失效，旧任务不能覆盖新目标 | D2及S3 |
| P6 | 连续请求仍至多一个active和一个最新pending | D3、S3 |
| P7 | EPS、SVG的固定1920×1200及3840×2400像素缩放/拖动GUI paint墙钟耗时p95≤16.67ms；极慢机器以同机ARGB参考的4倍为归一化上界 | D1性能日志；不能将CPU指标当作物理屏幕FPS |
| P8 | 交互和空闲最终tile保持设备密度、透明背景无旧帧残影 | 既有矢量密度/拖影/拖动门禁 |
| P9 | 新测试注册CTest，完整回归保留 | S4、完整CTest |

## 设计

请求携带不可变目标QColorSpace。worker先以原有RGBA64/float源栅格化，再转换到目标显示空间，最后生成ARGB32_Premultiplied显示副本。保留不可变的高精度矢量文档；高位深临时raster在worker完成颜色转换后释放，GUI仅保存已准备的显示副本，避免GUI淘汰巨大浮点缓冲的开销。GUI仅采样/合成已经准备的32位副本；没有QPixmap的worker操作。

窗口profile在paint之外缓存：首次绑定延后到事件循环、screenChanged即时刷新、低频timer处理同屏profile变化。目标变化增加源/呈现generation并清除旧显示tile；请求比较同时包含目标空间。回调同时检查generation和显示空间，不发布过期结果。保留原有一个active、一个pending合并策略，不取消正在访问PDF/SVG的数据，不引入无界队列。

GUI缓存按显示副本真实字节预算淘汰，原始文档另行持有。profile字节未变化时不重复解析ICC。FullViewportUpdate交互策略、终端device density、透明区域清除和精确idle refinement保持。诊断只暴露计数/格式/队列深度，不进入用户界面。

## 正确性证明

源像素p在源空间S，高精度worker计算q=T(S→D)(p)，再量化Q32(q)。GUI只绘制Q32(q)，故没有先量化源或先裁至sRGB。与原路径在相同D下最后绘制到32位backing store等价，误差受最终量化约束；原始矢量文档不被覆盖；目标变更重新从文档高精度栅格化，不对旧8位显示副本再转换，源颜色回归仍验证高位深。

令g为当前generation，任务返回(g',D')。仅g'=g且D'=D时发布，故更换图片/显示器/销毁后，过期任务不能覆盖当前结果。paint读取的tile均已完成转换，GUI颜色转换次数应严格为0；profile解析仅来自非paint事件，因此缓存命中帧不会重复系统查询。异步请求合并保留至多2个槽位，输入频率提高不使等待队列无界增长。

性能证明由结构不变量和实测共同组成：结构消除O(像素数)的GUI颜色准备与高位深隐式转换，实测比较相同轨迹的冷tile首绘和缓存重绘p50/p95/max。有限样本不证明任意复杂SVG或任何机器都达到固定物理帧率。

## 测量覆盖的反向核查

旧的真实窗口120Hz CPU测试不能保证每次采样都覆盖新tile的首绘准备，所以本轮D1显式等待vectorTileGenerationCount增加，单独记录冷tile首绘和缓存命中。测量为QElapsedTimer包围paint的墙钟耗时，不包括worker准备耗时，也不是物理显示FPS；既有真实窗口线程CPU门禁另行保留。

基线在3840×2400下EPS冷首绘p95约82.73ms，SVG约17.46ms；两者均实际触发GUI转换12次、ICC查询119次。性能优化以降低首绘尖峰和整体p95为目标，不把所有warm中位数必须下降作为未经证明的前提。高位深工作缓冲在worker释放，GUI缓存只包含显示副本；目标变更从不变量矢量源重新生成，避免累积量化误差。
