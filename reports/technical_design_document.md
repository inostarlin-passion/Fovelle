# 缩放初值与矢量图拖动呈现：技术设计文档

日期：2026-09-27
状态：已修复定时器切换与矢量 tile 预取边界问题；SVG/EPS 拖动回归覆盖即时屏幕帧与真实鼠标系统测试

## 1. 问题界定与原子化拆解

本工作区先前包含两个彼此独立的用户可见问题；本次追加处理并重点复核第二项：

1. 点击“Set Zoom Level”后，对话框的初值不等于当前视图缩放百分比；报告现象为初值看起来固定为 100。
2. 拖动 SVG 或 EPS 图片时出现明显拖影或闪烁。外部复现样本为 `/Users/inostarlin/Downloads/wanimagazine_logo.svg`。

缩放问题拆为：读取当前视图逻辑缩放值、将比例换算成百分比、设置数值控件精度和允许范围、设置初值、呈现输入控件文本。拖动问题拆为：真实按住鼠标平移是否启动、第一次滚动前是否启用全视口绘制、按住期间跨过空闲定时器后是否仍保持、静止时屏幕帧是否稳定、释放后是否恢复普通更新模式。

## 2. 代码路径与查明的原因

### 2.1 缩放对话框

- `MainWindow::zoomCustom()` 读取 `graphicsView->getZoomLevel() * 100.0`，并传入 `NativeDialogs::getDouble()`。
- `NativeDialogs::getDouble()` 创建 `QInputDialog`，设置 `DoubleInput` 模式后，旧顺序先调用 `setDoubleValue(value)`，再设置自定义最小值、最大值和精度。
- Qt 文档说明 `QDoubleSpinBox` 默认范围为 0.0 到 99.99，且超出范围的值受当前范围约束；Qt 6.11 源码中 `QInputDialog::setDoubleValue()` 会直接对内部 spin box 调用 `setValue()`。因此先写入大于 99.99 的值会被默认上限截断，后续放宽范围不会恢复被截掉的原值。
- 本仓库新增的 UI 回归用例用生产 `MainWindow::zoomCustom()` 路径复现此问题：当前缩放 64 倍时，对话框实际初值为 100，而预期为 6400。这里的 100 是本机测试观察结果；对任意其他值的显示表现不作无证据外推。

修复将设置顺序改为：精度、调用者范围、调用者初值。测试同时读取 `QInputDialog::doubleValue()` 和实际 `QDoubleSpinBox` 文本解析值，防止只检查传入参数而漏掉控件截断。

### 2.2 SVG/EPS 拖动

- `QVGraphicsView` 用 50 ms 单次 `vectorRefineTimer` 处理矢量交互后的细化/静态呈现切换。
- 鼠标平移由 `startDragAction(Pan)` 开始、`resetDragState()` 结束。旧超时回调不区分鼠标仍按住的平移手势，会直接关掉矢量交互呈现。
- Qt 文档将 `FullViewportUpdate` 定义为可用于需要禁用 scroll optimization 的视口；Qt 的 `QGraphicsView::scrollContentsBy()` 实现会在其他更新模式下复用滚动后的已有视口内容并移动 dirty region。因而鼠标平移仍活动时过早切回局部更新与残留/不完整绘制风险相符。

修复在矢量鼠标平移期间设置 `isVectorMousePanActive`，让空闲计时器不结束该状态；`resetDragState()` 在释放或重置时清除状态并恢复静态更新，新文件加载时也清理该状态。非鼠标按住平移仍使用原有定时器行为。

### 2.3 异步 tile 越界时的预览闪变

- 对用户给定的 SVG 做逐步屏幕帧配准时，早期截图曾出现 6.2% 的重叠像素错配；随后 20 ms 的截图恢复到约 0.27%。保存的早期帧显示形状进入低分辨率预览状态，较晚帧恢复为清晰矢量 tile。
- 代码路径与该观测一致：可见区域不再被已有 tile 完整覆盖时，`QVGraphicsImageItem::paint()` 请求异步 tile；若只能部分复用旧 tile，会先画 raster fallback，再将旧矢量 tile 覆盖到已有交集。`VectorTileInteractionOverscanPixels` 原为 16 设备像素，触及边界才请求新 tile，故存在请求完成前显示预览的窗口。
- 仅把 overscan 增大到 128 像素并未消除此行为：延长到 40 步的用例仍在 SVG 第 12 步测得 5.52% 瞬时错配（之后 20 ms 为 0.27%）。这作为反向复核，证伪了“扩大 overscan 已足够”的假设。
- 当前修复将交互 overscan 设为 128 设备像素，并在可见区域距离当前 tile 边界 64 像素时预取下一 tile；当前精确 tile 仍覆盖视口时继续使用它。测试拖动 40 步跨越预取边界，并将最早截图、20 ms 截图、40 ms 候选截图都与前帧按实际位移配准，避免只比较后续稳定帧而漏掉瞬时变化。

## 3. 依据、链式推导与逆向复核

1. Qt 官方 `QInputDialog::getDouble()` 文档明确：`value` 是输入控件的默认浮点值，`min` 与 `max` 是允许范围；Qt 官方 `QDoubleSpinBox` 文档明确其默认最大值为 99.99。[`QInputDialog` API](https://doc.qt.io/qt-6/qinputdialog.html) [`QDoubleSpinBox` API](https://doc.qt.io/qt-6/qdoublespinbox.html)
2. Qt 6.11 公开源码显示，`setDoubleValue()` 将数值交给内部 spin box 的 `setValue()`；spin box 在现有范围内约束数值。项目在自定义范围之前设置值，因此 6400 被约束到默认范围内。独立的 UI 测试观察到 100，而不是 6400，确认生产链路中初值确实丢失。[Qt 6.11 `qinputdialog.cpp`](https://github.com/qt/qtbase/blob/6.11/src/widgets/dialogs/qinputdialog.cpp)
3. 把精度/范围置于初值之前后，同一路径的内部数值和可见文本均为 6400；这验证了修复点改变的正是初值截断链路。
4. Qt 官方 `QGraphicsView` 文档说明全视口模式会更新整个视口，并用于需禁用滚动优化的场景；公开源码展示 `scrollContentsBy()` 的模式分支；Qt `QPaintEvent` 文档说明 paint region 的语义。这支持以实际鼠标事件、更新模式、paint region 和屏幕截图组合验证，而不是只断言内部标志。[`QGraphicsView` API](https://doc.qt.io/qt-6/qgraphicsview.html) [`scrollContentsBy()` 源码](https://codebrowser.dev/qt6/qtbase/src/widgets/graphicsview/qgraphicsview.cpp.html#L3618) [`QPaintEvent` API](https://doc.qt.io/qt-6/qpaintevent.html)
5. Cocoa 测试用真实窗口的 `QScreen::grabWindow()` 截取视口区域。滚动连续性比较先按真实滚动偏移与 DPR 配准两帧，只计算相交像素区域；另比较候选帧与静止帧。通道差值阈值和 5% 占比均为项目回归门槛，不是 Qt 或 Apple 规定的行业标准。外部 SVG 的颜色不可预设，所以不对它套用暗像素阈值，但仍执行帧稳定性、更新模式、滚动和 paint 区域断言。
6. GitHub Actions 的失败日志显示，在之前 10% 暗像素门槛下，SVG 初始截图的暗像素比例为 0.055684；同一运行中 EPS/SVG 的全视口 paint、拖动状态和帧连续性判据均通过。Qt `QScreen::grabWindow()` 文档指出它取屏幕窗口像素，且坐标是设备无关像素、高 DPI 返回图可能更大；因此固定的近黑覆盖率不应被解释为图像是否正确的通用准则。门槛降为 1%，仅作“非空画面信号”检查，原有按滚动位移配准的帧连续性、静止变化、paint 覆盖和交互状态检查仍保留。该调整对截图构图/颜色敏感的断言进行了校准，并未放宽拖影连续性门槛。[`QScreen::grabWindow()` 文档](https://doc.qt.io/qt-6/qscreen.html#grabWindow)
6. 逆向复核包括缩放回归用例在生产修复前失败（100≠6400）、修复后通过；拖动用例覆盖了按住跨越 50 ms 定时器期限和释放恢复，并重复检查指定外部 SVG。已有负向控制曾将计时器恢复为无条件退出交互模式，测试因此失败；修复状态机后通过。

## 4. 本次对拖动回归的加固

原测试虽然检查了更新模式、非空/暗像素占比和静止截图，但静止帧比较无法证明图像在滚动后仍与其内容位置一致；只看“出现过一次全视口 paint”的系统门禁也可能让拖动期间某些 paint 使用局部更新而漏过。为此增加两层观察：

1. Cocoa 集成用例对相邻滚动帧按水平/垂直滚动条变化量和屏幕 DPR 做平移配准，只比较视口重叠区域；颜色通道最大差值大于 32 的像素比例需不超过 5%。最早截图、20 ms 截图和 40 ms 候选截图都参与配准；额外静止帧比较使用通道差值大于 24、占比不超过 5%。这两个比例是项目回归阈值，不是平台标准。
2. 系统级真实鼠标测试对拖动日志中每条 `mouse_pan_active=true` 的 paint 逐条断言 `update_mode=full` 且 `dirty_ratio >= 0.9`。最终真实 HID 运行中，EPS 记录 46 条、指定 SVG 记录 48 条；两组所有记录均为 full，dirty ratio 均为 1.0。指定 SVG 的本机系统测试覆盖 32 个按住拖动事件，并完成退出/恢复检查。

原生辅助器先前对纵向可滚动的宽幅 SVG 仅执行横向路径，不能保证示例文件真实发生了可见滚动。现已根据纵向滚动范围动态调整缩放，随后执行纵向拖动；样本 `/Users/inostarlin/Downloads/wanimagazine_logo.svg` 的实测结果为 `fullscreen_near_bottom=true`、`exit_anchor_stable=true`、`no_origin_reset=true`、`returned_to_normal_geometry=true`。

## 5. 测试和证据边界

- 缩放测试对 64 倍值覆盖默认 99.99 上限之外的边界，检查输入控件内部值及文本。
- 拖动测试逐一加载 EPS 与 SVG，实际注入左键按下、按住移动、120 ms 停顿、继续移动和释放；记录所有 drag paint region，采集窗口帧。
- 2026-09-27 本机环境：macOS 27、Qt 6.11.2、Cocoa、Apple Silicon。给定 SVG 可读；SHA-256 为 `d163fffd16b5002d3bb211253748419b010b685845e8ce7aef723e5f0e6c4212`。
- 将旧实现作为负向控制时，16 像素 overscan 在长拖动中测得约 6.2% 瞬时错配；仅把 overscan 改成 128 后仍测得 5.52%。加入 64 像素预取门槛后，指定 SVG 40 步拖动 `-repeat 3` 共 9 项通过、0 失败。三次运行中 EPS/SVG 最大重叠错配率分别最高为 0.017998/0.015838，最大静止帧变化率为 0.011097，均低于项目门槛 0.05。
- 2026-09-27 全流程复核的静态、单元、集成、系统阶段均通过；集成阶段四项用例全部通过。最新真实 HID 系统阶段中 EPS/指定 SVG 的 held-pan paint 条数为 46/48，所有 paint 都是 full、dirty ratio 1.0。
- 上述窗口图像由 `QScreen::grabWindow()` 离散采样；截图可捕获屏幕上窗口的显示像素，但不能证明显示控制器每个刷新周期的扫描输出。
- 没有从用户现场取得修复前的录屏或逐帧像素数据；所以归因依据是可复现代码路径、Qt 文档/源码和回归负向控制，不声称做过现场视频取证。

## 6. 修改清单

- `src/nativedialogs.cpp`：先设精度和范围，再设输入初值。
- `tests/tst_qviewtests.cpp`：增加缩放生产路径初值测试；增强 EPS/SVG 按住拖动时序、跨 tile 边界的最早/后续屏幕帧、每步滚动 paint 区域、相邻帧重叠像素和静止帧检查。
- `src/qvgraphicsimageitem.cpp`：交互 tile 外扩改为 128 设备像素；视口距 tile 边缘 64 像素时提前请求下一 tile，避免等到现有覆盖边界耗尽才渲染。
- `tests/eps_quality_static.py`：同步校验交互 overscan 和主动预取约束。
- `tests/native_drag_helper.mm`、`tests/vector_drag_ghosting_pipeline.py`：真实鼠标辅助器支持宽幅示例 SVG 的纵向拖动；系统门禁逐条校验按住时的全视口 paint。
- `src/qvgraphicsview.cpp`、`src/qvgraphicsview.h`：保持此前矢量鼠标平移生命周期修复。

## 7. 外部依据与推导边界

- Qt 6 `QInputDialog` 默认值与范围语义：[官方 API 文档](https://doc.qt.io/qt-6/qinputdialog.html)
- Qt 6 `QDoubleSpinBox` 默认最大值、精度和范围：[官方 API 文档](https://doc.qt.io/qt-6/qdoublespinbox.html)
- Qt 6.11 `QInputDialog` 控件赋值实现：[qtbase 源码](https://github.com/qt/qtbase/blob/6.11/src/widgets/dialogs/qinputdialog.cpp)
- Qt 6 `QGraphicsView` 更新模式和 scroll optimization：[官方 API 文档](https://doc.qt.io/qt-6/qgraphicsview.html)
- Qt 6 `QGraphicsView::scrollContentsBy()`：[公开源码](https://codebrowser.dev/qt6/qtbase/src/widgets/graphicsview/qgraphicsview.cpp.html#L3618)
- Qt paint event region：[官方 `QPaintEvent` 文档](https://doc.qt.io/qt-6/qpaintevent.html)
- QWidget backing-store/double-buffering 背景：[官方 QWidget 文档](https://doc.qt.io/qt-6/qwidget.html#transparency-and-double-buffering)
- macOS layer-backed view 背景：[Apple `NSView.wantsLayer`](https://developer.apple.com/documentation/appkit/nsview/wantslayer)
- Qt 屏幕采样 API：[官方 `QScreen::grabWindow()` 文档](https://doc.qt.io/qt-6/qscreen.html#grabWindow)，说明其返回屏幕窗口内容并提示捕获限制；[Qt 桌面截图示例](https://doc.qt.io/qt-6/qtwidgets-desktop-screenshot-example.html)提供另一个官方截图流程参考。
- Qt `QGraphicsItem::paint()` 文档要求除非调用 `update()`，连续 paint 应产生相同输出：[官方 API 文档](https://doc.qt.io/qt-6/qgraphicsitem.html#paint)。该契约支持逐帧输出检查，但不能单独推出本项目的根因。
- Qt backing store 更新机制：[公开 `QBackingStore` 源码](https://codebrowser.dev/qt6/qtbase/src/gui/painting/qbackingstore.cpp.html)用于交叉核验局部更新/flush 区域的概念。它与 `QGraphicsView` 文档/源码解释的是渲染更新路径，并不证明用户报告现场的具体闪烁原因。

证据链的边界：全视口 paint 日志说明 Qt 的 paint 事件覆盖范围；重叠帧比较说明抽样到的内容与滚动位移一致；两者互补但均不直接采集 GPU/显示器每次刷新。没有从用户现场获得修复前逐帧录屏或硬件刷新捕获，故对更短的亚采样闪烁、其他 macOS/Qt/GPU 组合及其他 SVG/EPS 文档保留未验证项。未穷尽所有可能的硬件/驱动/显示刷新证据源；上述现场缺口明确保留，不据此推断根因普遍适用。

# 图片序列边界提示与失焦图像保真：技术设计补充

日期：2026-09-29
状态：实现完成；边界提示、主题适配、多语言和视口失焦保真均加入静态与动态验收

## 1. 问题界定与原子化验收标准

本次只处理图片序列浏览的边界反馈、提示外观/本地化，以及视口失去焦点时图像的可读性：

- **AC-NAV-HINT-BOUNDARY**：当用户确实请求不存在的上一张或下一张图片时，分别显示 `No previous image` 或 `No next image`；正常移动、启用循环时的移动、幻灯片自动到边界不显示该提示。
- **AC-NAV-HINT-LOCALIZED**：提示经 Qt 翻译机制呈现，支持的西班牙语、日语、简体中文和繁体中文目录都包含两条完整译文，辅助技术收到相同文本。
- **AC-NAV-HINT-APPEARANCE-ANIMATION**：提示随应用 Light/Dark/System Appearance 切换前景与背景样式；单条消息置于视口底部居中，淡入/淡出各 180 ms，完全显示 4000 ms，重复触发复用同一提示。
- **AC-VIEWPORT-FOCUS-SHARP**：通过真实屏幕合成像素验证，另一顶层窗口获得焦点后图片视口与聚焦时相同，不接受仅检查控件离屏 render 的结果。
- **AC-TRANSPARENT-RASTER-BACKGROUND**：含 alpha 通道的 native SDR PNG 失焦时仍以相同 viewport 背景合成；不泄漏窗口后方其他内容；native SDR presentation 与底色图层在失焦时保持可见。
- **AC-HDR-FOCUS-FADE-SOURCE-DETAIL**：HDR 图片失焦时，原生 HDR presentation opacity 由当前屏幕值平滑单调降至 0；使用打开图片时同一 450 ms ease-in/ease-out 动画。动画结束后 EDR layer 完全透明、EDR 关闭，SDR fallback 的实际像素宽高必须与 HDR 源图相等；两倍放大后从真实窗口端点截图验证稳定失焦图像与聚焦图像边缘相似度≥0.90。缺少 fallback 尺寸遥测视为不通过。重新聚焦以同一时长反向恢复。

## 2. 链式推导和实现边界

项目的 `QVImageCore::goToFile()` 已在不循环的序列边界返回 `reachedEnd`，而循环启用会将索引回绕。`QVGraphicsView::goToFile()` 据此只为 Previous/Next 到达边界发信号；MainWindow 的幻灯片入口将通知标志关闭。成功的导航不触发提示。

Apple 的 Feedback 指南要求反馈重要性与打断程度相称，并指出重要信息可在相关界面中被动呈现；Alerts 指南建议不要用会打断当前任务的警报传达一般信息。因此这里采用单行、无按钮、不会获取焦点的状态提示。Material 的 snackbar 指南提供了底部短消息、一次仅显示一条及至少 4 秒自动消失的可验证先例；该平台规范不是 macOS 要求，4 秒在本项目中是据此采用的设计值。项目导航按钮已有 180 ms 透明度过渡，提示沿用这一既有时长。Apple Motion 指南同时要求反馈动效简短、克制，且不能只靠动画传递关键信息。

外观样式根据 `theme` 设置并通过 `QVCocoaFunctions::resolvedTheme()` 解析 System appearance：Light 使用深色半透明圆角底和白字，Dark 使用浅色半透明圆角底和深字。英文源字符串放在 `MainWindow::tr()`，四份已发布 TS 目录添加对应译文；触发时发出 `QAccessible::Alert`，文本与视觉提示一致。Qt 官方文档说明 `lupdate` 从 `tr()` 抽取源文案、TS 文件携带译文、lrelease 生成运行时 QM；QAccessible 的 Alert 是状态通知事件。

原生 HDR 图像使用独立 Metal/Core Animation 图层。不能让 Qt 透明 QWidget 直接覆盖 HDR 像素，因此提示先由同一自绘控件栅格化，再作为 CALayer 图像叠加到现有 viewport overlay layer；SDR/Qt 绘制路径继续显示同一控件。HDR 失焦时 `QVGraphicsView::setHDRPresentationActive(false)` 先确保 SDR fallback 位于原生层下方，再调用 renderer 的同一 opacity 动画由 1 降至 0。renderer 继续保持 EDR 直到动画完成，完成回调再关闭 EDR。关键是该 fallback 同时是加载占位和稳定失焦终态：HDR 解码路径必须以 `imageFromCIImage(..., largestDimension=0)` 物化完整 SDR 源尺寸；沿用 loader 的 2048 像素上限会在高缩放下放大采样不足的代理并造成稳定模糊。失焦终态遥测记录代理与源图尺寸供动态验收。聚焦时沿相同 450 ms ease-in/ease-out 反向淡入。

另一个独立的失焦路径影响普通透明 PNG：ImageIO 将其识别为 native SDR，原生 CALayer 子树除了图片还负责提供不透明的主题底色；此前通用失焦处理把这个子树和 HDR 一样隐藏，底层 QWidget 对这类 macOS raster 视口又未声明 opaque，于是 PNG 的透明像素露出窗口后方内容。修复仅让 native SDR 图层保持可见及其 presentationActiveRequested=true；当窗口不活跃时，原生 SDR 依然是一张不变的 SDR 图像，因此不需要焦点淡出或 SDR proxy 双层合成。HDR 使用独立的反向淡出路径：有动画、保留 EDR 至淡出结束，并以清晰 SDR proxy 收敛。首次加载期间仍允许 Qt proxy 可见，等 native SDR 的首帧及几何稳定后才隐藏 proxy。

上一版焦点测试只调用 `viewport()->render()`，并使用不透明 SVG。Qt 文档说明 `QScreen::grabWindow()` 捕获屏幕像素，包括窗口上方实际合成的内容；因此控件 render 不能覆盖 WindowServer/CALayer 的合成结果。本次动态测试改用用户样本的透明 PNG 固定 fixture，真实激活另一个不遮挡视口的顶层窗口，等待主窗失焦后从 `QScreen::grabWindow(mainWindow.winId(), ...)` 读取同一 viewport 像素，裁去包含窗口圆角/外缘的32个物理像素后比较内容区，并检查已知透明源像素仍显示 Dark viewport 底色。裁边是因为 macOS 失焦会改变窗口外框的抗锯齿像素；viewport 图像区和透明点仍精确比较。

## 3. 逆向证伪复核

- 循环开启时索引会回绕，若仍提示会把成功导航错报为失败；边界反馈只由 `reachedEnd` 与 Previous/Next 双重条件触发。
- 幻灯片碰到末尾属于定时播放状态变化，不是用户刚发起的边界请求；自动推进显式抑制提示。
- 弹窗会抢焦点并遮挡图片；普通提示不抢焦点，也不创建按钮。
- 视口中央会直接挡住主要图像内容，顶部靠近 macOS 标题栏/窗口控件；底部居中的单行形式符合 snackbar 的短反馈先例。
- 仅在当前 Appearance 改 QSS 不足以保证 HDR 下正确合成，故对同一原生图层增加 bitmap overlay；静态测试检查两条呈现路径，动态测试在可用的 native overlay 下检查原生提示确实可见。
- 仅比较 QWidget 内部绘制会漏掉 Core Animation 子树被隐藏及透明像素穿透；反向复核通过先让测试对用户 PNG 和真实顶层窗口切换失败，再修正 renderer 生命周期。
- 把 native SDR 简单替换为 Qt proxy 也不满足要求：proxy 可以呈现图片，但测试确认失焦时必须保留与聚焦相同的真实 WindowServer 背景合成像素；只保持 HDR 的 SDR fallback 逻辑，而让 native SDR 的 opaque backdrop 常驻。
- 把 HDR 失焦直接设为 opacity=0 会让测试缺少任何中间采样，也无法满足亮度渐降；如果淡出开始时就关闭 EDR，现存 HDR layer 会被提前 clamp。焦点测试检查原生 presentation opacity 的单调方向、450 ms 量级、EDR 直到淡出完成、失焦 SDR endpoint 及真实屏幕图像边缘相似度。opacity 是实际动画属性在 presentation tree 中的值；在图像高光 HDR 层与保持不变的 SDR proxy 合成时，HDR 层占比持续下降，亮度随之向 SDR 端点收敛。真实截图通过状态日志时间戳与 `final-frame-visible` / `inactive-sdr-visible` 端点关联后用于细节比较；未把 PNG 截图像素值当作绝对 HDR 光度：8-bit 编码会把多个高光样本夹到255，不能可靠代表亮度轨迹。
- 先前测例在视图适屏时用≥0.90边缘 cosine 通过，仍可能漏掉失焦后切至2048px fallback 的高倍放大模糊。逆向复核将 production `zoomAbsolute()` 放大到2×并检查 inactive 端点 fallback 的实际宽高等于6048×8064源图尺寸；突变测试把代理降为1536×2048，尺寸断言必须失败。屏幕帧继续作为独立合成结果验证，来源尺寸断言负责区分图像采样精度而不依赖HDR色调映射造成的像素亮度差。

## 4. 外部资料

- Apple Human Interface Guidelines：[Feedback](https://developer.apple.com/design/human-interface-guidelines/feedback)、[Alerts](https://developer.apple.com/design/human-interface-guidelines/alerts)、[Motion](https://developer.apple.com/design/human-interface-guidelines/motion)
- Material Components：[Snackbars](https://m2.material.io/components/snackbars)
- Qt 6.11：[QAccessible](https://doc.qt.io/qt-6/qaccessible.html)、[Using lupdate](https://doc.qt.io/qt-6/linguist-lupdate.html)、[Localizing Applications](https://doc.qt.io/qt-6/localization.html)、[QWindow focus events](https://doc.qt.io/qt-6/qwindow.html)、[QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html)
- 失焦合成复核：[Qt `QScreen::grabWindow()`](https://doc.qt.io/qt-6/qscreen.html#grabWindow)说明返回屏幕像素且其他覆盖窗口会出现在结果中；[Apple `NSView.isOpaque`](https://developer.apple.com/documentation/appkit/nsview/isopaque)说明 opaque 代表视图填满 frame 的不透明内容；[Apple `CALayer.isOpaque`](https://developer.apple.com/documentation/quartzcore/calayer/isopaque)说明标记 opaque 的 layer 必须填满 bounds。结合项目透明 PNG alpha 数据和失焦前后实际屏幕像素，支持“隐藏携带 opaque viewport 背景的原生 SDR layer 后会露出底层窗口”的因果链。
- HDR 焦点动画复核：[Apple `CABasicAnimation`](https://developer.apple.com/documentation/quartzcore/cabasicanimation?language=objc)确认 layer opacity 可作为标量属性动画；[`CALayer.opacity`](https://developer.apple.com/documentation/quartzcore/calayer/opacity?language=objc)说明 opacity 是 0…1 的可动画透明度；[`CALayer.presentationLayer`](https://developer.apple.com/documentation/quartzcore/calayer/presentation%28%29?changes=_8&language=objc)说明动画期间可读取当前屏幕呈现值；[`CAMediaTiming.duration`](https://developer.apple.com/documentation/quartzcore/camediatiming/duration)以秒定义动画时长；[Apple ease-in/ease-out](https://developer.apple.com/documentation/quartzcore/camediatimingfunctionname/easeineaseout?changes=_7)定义前后缓、中间加速的时序曲线；[Qt `QScreen::grabWindow()`](https://doc.qt.io/qt-6/qscreen.html#grabWindow)确认可按窗口 ID 采集屏幕合成像素。文档支持选用 opacity/presentation-layer/真实屏幕截图作为验证手段；450 ms 的产品时长来自项目已有打开动画，非外部规范强制值。
- 分辨率选择交叉验证：[Apple `kCGImageSourceThumbnailMaxPixelSize`](https://developer.apple.com/documentation/imageio/kcgimagesourcethumbnailmaxpixelsize)说明该值控制缩略图最大宽高，不设上限时可达到原图尺寸；[Apple `CIImage`](https://developer.apple.com/documentation/coreimage/ciimage?language=objc)把 CIImage 定义为延迟计算的图像处理配方；[Apple Gain Map HDR WWDC24](https://developer.apple.com/videos/play/wwdc2024/10177/)说明 SDR base 与 gain map 可独立保留为 CIImage 后再按 headroom 处理；[Qt `QImage::scaled()`](https://doc.qt.io/qt-6/qimage.html#scaled)确认缩放操作返回实际缩放副本；[Qt `QScreen::grabWindow()`](https://doc.qt.io/qt-6/qscreen.html#grabWindow)确认高 DPI 截图可能包含多于逻辑请求尺寸的物理像素。由此可知把 source-sized SDR CIImage 写入 2048px 上限的 Qt pixmap 会不可逆丢弃 fallback 源采样，且适屏截图不足以证明高倍率细节；外部资料支持机制和观测方法，本项目动态日志与 2×真实窗口测试确认具体缺陷与修复。
- 项目既有实现：[导航过渡时长](../src/mainwindow.h)、[焦点/Metal presentation 切换](../src/qvgraphicsview.cpp)、[原生导航叠层](../src/qvcocoafunctions.mm)

证据范围：Material 4 秒时长是跨平台设计先例，非 macOS HIG 强制值；本机 QtTest 验证当前 macOS 27/Qt 6.11.2 配置。失焦清晰度测试使用屏幕级 WindowServer 采样，不只检查 QWidget；焦点过渡通过每16ms的 Core Animation presentation-layer 值采样，不将离散 PNG 截图误作绝对 HDR 光度仪。屏幕截图仍不覆盖每次刷新；对其他 GPU、显示器和 macOS/Qt 版本的实际呈现仍需目标环境运行。
