# HDR信息页正常输出色彩空间修复设计

日期：2026-10-17（Asia/Shanghai）。保留工作区已有拍摄机型修改。

## 问题界定与检索核验

MainWindow::showFileInfo→show/activateWindow令主图窗口失活；setHDRPresentationActive(false)显示SDR代理；colorInformation()以firstFramePresented且代理不可见为nativeOutput依据。QVInfoDialog的100ms timer读取该实时值，因而正常HDR输出变成Preview: sRGB。像素退化策略本身不在本次变更范围。

原子拆解：区分正常HDR输出与临时可见输出 → 读取当前图片renderer已呈现证据 → 信息页provider接线 → 验证失活及跨多次timer稳定 → 验证换图/关闭清理 → 邻接回归。

多跳联网及多源核验：

1. [Qt QWidget::activateWindow](https://doc.qt.io/qt-6/qwidget.html#activateWindow)及[QDialog](https://doc.qt.io/qt-6/qdialog.html)说明对话框激活为顶层窗口；与showFileInfo及本地激活/失活事件链核对。
2. [Apple CAMetalLayer.colorspace](https://developer.apple.com/documentation/quartzcore/cametallayer/colorspace)及[EDR显示说明](https://developer.apple.com/videos/play/wwdc2022/10114/)核对输出色彩空间与EDR显示能力是不同概念。renderer构造实际设置extendedLinearDisplayP3，diagnostics.outputColorSpaceName由实际metalLayer.colorspace与outputColorSpace比较产生；不从HDR文件后缀推断。
3. [Apple NSScreen EDR能力](https://developer.apple.com/documentation/appkit/nsscreen/maximumextendeddynamicrangecolorcomponentvalue)说明显示EDR能力相关接口；Apple网页正文需JavaScript，本次以官方页面入口、既有实现及本机SDK头文件交叉核验，未声称硬件当前headroom等于色域名称。
4. 本地renderer的setImage/resetForImage/clearImage将firstFramePresented清零；syncPresentationDiagnostics读取当前图片的呈现状态。与已有testColorInformationFollowsPresentation运行测试交叉验证实时API语义。

逆向证伪：暂停信息页timer只会冻结旧图片信息；show前快照会在首次HDR准备未完成、对话框已打开后换图等场景过期；只认isNativeHDRLoaded会将未成功呈现或无renderer的图像误报HDR；硬编码Extended Linear Display P3无法反映实际renderer配置；修改colorInformation默认行为会破坏实时诊断。资料与代码证据充分支持下述局部实现，检索停止。

## 原子验收与方案

| ID | 原子验收标准 |
|---|---|
| A1 | 已成功呈现的当前HDR图，信息页显示正常HDR输出色彩空间，失活不改为Preview |
| A2 | 多次100ms刷新、失活动画完成及反复开关信息页后仍准确 |
| A3 | 首帧未呈现、无HDR原生renderer、非HDR时保留实时值，不能凭源格式虚报 |
| A4 | 换SDR/其他图或关闭当前图后不残留先前HDR输出或源信息 |
| A5 | 实时colorInformation及HDR失活/恢复行为保持原有语义；机型、时间、颜色邻接功能通过 |

新增QVGraphicsView::fileInfoColorInformation()，先读取实时colorInformation；仅当hasOutput、当前FileDetails.isNativeHDRLoaded、hdrRendererActive、renderer存在、diagnostics.imageActive、firstFramePresented且outputColorSpaceName非空时，将输出替换为该renderer实际色彩空间并设置nativeOutput=true、preview=false。其他状态返回实时值。MainWindow信息页provider调用新方法；原timer和实时方法保持不变。不缓存上一张图或show前快照，不新增硬编码色彩空间名。

## 正确性论证

令L为实时信息，H为上述完整HDR就绪谓词，N为当前renderer输出名。定义F=L；若H，令F.outputSpace=N、F.nativeOutput=true、F.preview=false。H不依赖临时代理可见性，因此打开信息页和失活动画不能将正常HDR输出改为SDR预览，A1/A2成立。

若未加载、错误、非HDR、无renderer或未呈现，H为假且F=L，A3成立。换图时既有setImage清理呈现标志，关闭时hasOutput=false；H同时依赖当前FileDetails，因此不会跨图沿用旧名，A4成立。source字段由L复制，没有改变原始ICC和源元数据。

该方法只读取状态，未改变渲染/激活/失活逻辑或原colorInformation；实时API仍可观测SDR代理，A5成立。这里“正常输出”指当前HDR图已成功呈现所使用的输出色彩空间，不代表当前物理屏幕亮度或EDR headroom。正常呈现尚未得到证实时，信息页仍显示现有预览信息，直到真实首帧完成后由timer更新。
