# 图片色彩路径修复设计

日期：2026-10-18（Asia/Shanghai）。基线：658ce8c，Fovelle 1.3.2。修复上一轮 C1–C7；区分已复现缺陷、实现精度限制和信息不可恢复边界。旧三份报告备份在 reports/evidence/color_rendering_20261018/。

## 问题界定与原子拆解

按源文件字节/颜色声明→解码→颜色表示→分块/矢量缓存→显示空间转换→动画生命周期追踪。C1 是 Qt SVG 不理解 CSS color(display-p3)，C2 是标准 PGM/PPM 被赋予 sRGB，C3 是多图像原生 SDR 被 imageCount==1 排除，C4 是 EPS 固定 8 位 sRGB，C5 是 QImage 分块只接受 8 位，C6 是电影启动仅释放 SDR，C7 是无声明数据缺少恢复原始色彩的证据。不能由文件扩展名或输出空间标签推出像素正确。

## 多跳检索与交叉核验

| 问题 | 外部一手依据 | 仓库/实测互证 |
|---|---|---|
| C1 | [W3C CSS Color](https://www.w3.org/TR/css-color-4/#predefined) 、 [Qt SVG支持范围](https://doc.qt.io/qt-6.11/svgextensions.html) 、 [Qt QColorSpace](https://doc.qt.io/qt-6.11/qcolorspace.html) | 同版本 QSvgRenderer 把 P3 红画黑；loader、GUI renderer、worker renderer 必须消费同一规范化源 |
| C2 | [PPM规范](https://netpbm.sourceforge.net/doc/ppm.html) 、 [PGM规范](https://netpbm.sourceforge.net/doc/pgm.html) 、 [Netpbm转换工具](https://netpbm.sourceforge.net/doc/pnmgamma.html) | 输入128被原样解释为sRGB128，正确应约140；PFM不属于本修正 |
| C3 | [ImageIO图像计数](https://developer.apple.com/documentation/imageio/cgimagesourcegetcount(_:)) 、 [ImageIO类型与解码](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/ImageIOGuide/imageio_basics/ikpg_basics.html) | 双页TIFF、系统ICNS的系统首帧解码成功而App失败 |
| C4/C5 | [Qt QImage](https://doc.qt.io/qt-6.11/qimage.html) 、 [Apple CGImage](https://developer.apple.com/documentation/coregraphics/cgimage) 、 [Ghostscript色彩管理](https://ghostscript.com/features/color-management/) | CG支持显式像素精度、颜色空间；不能用8位转换保留16位源；PDF自带颜色可经ColorSync转到扩展线性P3 |
| C6/C7 | [Apple HDR工作流](https://developer.apple.com/videos/play/wwdc2023/10181/) 、 [Qt QImage](https://doc.qt.io/qt-6.11/qimage.html) | 静态HDR与播放帧不能并存；赋标签不创造缺失的源空间证据 |

反向证伪：P3作为工作空间本身不等于限制到sRGB；原生HDR已有half-float路径且JPEG端点像素测试通过，保留它。仅改变文件信息标签不能修复PNM像素。仅去掉图像数量条件可能把多尺寸图标当电影，必须同时记录多图像语义。仅提高EPS预览精度不修复最终tile，两个入口必须共同改变。逆向检查发现整数P3仍会裁切AdobeRGB绿，新增超P3反例，Qt6改为扩展线性浮点中间表示。测试用小尺寸真实provider构造验证高精度机制，不分配数GB内存。RAW依机型、EXR显示变换、无声明图片原始空间属于未验证/不可推断，不凭空宣称全格式完美。

## 验收标准与实现

| ID | 原子标准 | 测试 |
|---|---|---|
| A1 | SVG/SVGZ的display-p3颜色不再变黑；P3红保留P3颜色语义 | D1/S1 |
| A2 | SVG的普通sRGB颜色、CSS颜色、渐变端点、alpha与P3混用时语义一致；所有renderer消费规范化字节 | D2/S1 |
| A3 | 标准P2/P3/P5/P6按BT.709解释；灰阶128转sRGB约140，黑白不变，16位不被无故截断 | D3/S2 |
| A4 | 双页TIFF至少显示首图；ICNS选择最大有效表示，静态多图像不启动动画 | D4/S2 |
| A5 | EPS预览与最终PDF tile在Qt6使用32位浮点扩展线性P3，不在内部先裁到sRGB或P3整数域 | D5/S3 |
| A6 | QImage分块保留16位/浮点数据及ICC，provider按实际stride保持有界 | D6/S3 |
| A7 | 动画启动后静态SDR/HDR引用与状态均清除，帧继续播放 | D7/S4 |
| A8 | 无声明保持明确假设，未知/无效ICC不被报告为已知源空间；有效ICC不被覆盖 | D8/S4 |
| A9 | 新测试进入CTest；现有SDR/HDR、矢量缩放、电影回归通过 | S5/D9 |

SVG设计：在载入边界把支持的CSS color(display-p3/srgb/srgb-linear)表达式规范化到带标签的Display P3。为避免把普通sRGB红误当P3红，同时转换同文档中的传统paint颜色；保留none/currentColor/继承/url；fill/stroke的内在alpha使用SVG Tiny solidColor服务器，与外部opacity相乘，避免Qt将rgba()画黑。其他paint属性的现代颜色alpha暂明确报错，不伪造渲染成功。样式、属性和嵌入光栅图统一处理。常规不含现代颜色且没有需要解析的相对图像引用的SVG保持原始字节；gzip源在有界解压后交给Qt。无法解析的现代颜色明确拒绝而不静默变黑。SVGZ有界解压；规范化数据成为预览和worker唯一源。Qt SVG既有颜色输入量化仍受其实现精度约束，渐变验收覆盖端点而不宣称实现完整CSS4插值引擎。原有SVG图像相对引用在规范化时按源目录解析；SVGZ有界解压后不交给Qt再次解压。

PNM设计：按文件magic识别，分离PFM/PBM；通过BT.709逆传递函数构造源色彩空间，独立读取ASCII/二进制整数像素到RGBX64，避开Qt PNM对16位输入的降8位；先做尺寸/预算/截断/样本范围校验，不先按sRGB转换。E(v)=v/4.5（v<0.081），否则((v+0.099)/1.099)^(1/0.45)。正向sRGB转换由色彩管理完成。源元数据标记为容器规定BT.709。

多图像：多页文档默认首图，图标按像素面积选最大表示；保留GIF/APNG等真正动画的电影路径。HDR/SDR解码与方向均须针对选中表示。

EPS与分块：Qt6的EPS/PDF使用RGBA32FPx4 premultiplied + Extended Linear DisplayP3，CoreGraphics使用32位float/host字节序，保留负分量和大于1的值，再根据窗口ICC转换。Qt5兼容分支受QImage能力限制使用RGBA64/P3，不宣称等同浮点广色域；当前验收环境是Qt6。原生分块支持64位整数及Qt6浮点格式，无法直接支持时依据源深度选择保精度中间格式，Gray/CMYK ICC必须与源组件模型匹配：直接发布Gray16/CMYK有色彩声明的provider，让ColorSync转换；不能先扩展RGBA再保留灰阶ICC。Qt的跨模型转换实测会合并相邻16位值，因此不用该中间路径；bitsPerComponent按真实格式计算。是否进入有界provider路径依据源组件位深估算4通道实际字节数，而非固定每像素4字节，防止16位图在尚未达到旧像素阈值前就越过2GiB。

## 正确性论证

对于本次支持的sRGB、Display P3及线性sRGB的SDR单个paint像素p（均落在P3可表示域内），规范化产生p'=T(S→P3)(p)，并标记P3，显示时再做T(P3→D)，组合T(P3→D)∘T(S→P3)与直接源到显示转换在Qt SVG颜色输入量化误差内一致；直接将p标记P3不满足此性质，测试以P3红和sRGB红对照捕获它。缓存保存源空间数据，输出转换按当前窗口profile选择，避免跨屏复用旧目标。

PNM的错误原因为E_sRGB(v)≠E_709(v)；修复保留v并赋予正确E_709，再转换，独立数学参考可证伪错误标签。16位provider采用同一bytesPerRow与真实bitsPerComponent，故不丢弃低8位；bounded provider按stride计算带高，避免位深增大突破范围。原生多图像选取有效index后无需Qt插件，图标最大有效表示不再被movie误判。播放时释放两个静态表示，使唯一可见内容随电影帧更新。

## 验证边界

最终色差比较以独立公式、ColorSync参考及实际输出像素为依据；不把标签测试当成色差测试。不承诺未知相机、所有纹理变体或无法恢复的未标注原始空间。完整CTest及失败记录均保留，不能用筛选成功取代完整回归。

## 迭代反例与验证约束

第一轮测试捕获Qt SVG不支持rgba()及Qt PNM丢弃16位样本，分别改用solidColor内在透明度与独立RGBX64读取。继续增加UTF-16 XML（含gzip）反例，避免UTF-8字节扫描漏掉现代颜色。AdobeRGB绿色在浮点扩展线性P3的独立CoreGraphics探针中得到约(-0.150082, 0.953588, 0.0264872)，证明整数P3中间表示仍然不足。SVG普通红的参考使用W3C独立矩阵/传递函数结果(0.91748756, 0.20028681, 0.13856059)，而非仅用生产同一转换调用互比。

完整回归包含真实桌面截图及全屏系统动画；出现失败时保存原始日志，并区分像素/几何实际不满足与取样窗口状态。不得修改断言阈值、屏蔽用例或重试取绿来声称通过。最终报告分别列出首次与最终结果。

## 编译兼容性补核

[Qt5 QColorSpace文档](https://doc.qt.io/archives/qt-5.15/qcolorspace.html)与Qt6文档互证：采样曲线构造器和setDescription不能无条件用于旧Qt。旧分支保留Qt生成的sRGB ICC原色，只按[ICC curveType规范](https://www.color.org/ICC1-V41.pdf)替换rTRC/gTRC/bTRC为共享4096项16位BT.709曲线，并更新长度、清除过期profile ID；新Qt路径保持原实现。兼容守卫前后Qt6预处理token SHA-256完全一致，记录在qt6-compatibility-equivalence.json，因此不改变本轮完整回归覆盖的Qt6生产逻辑。

独立的FovelleLegacyBt709Backend CTest固化旧后端测试，使用当前Qt模拟该构造分支的数学语义；不能据此声称已经完成真实Qt5工具链或macOS15整机验收。该门禁在完整62项回归完成后补充注册并单独执行，不将两次结果伪称为同一次完整63项运行。
