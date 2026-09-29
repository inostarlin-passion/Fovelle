# 缩放初值与矢量图拖动呈现：测试用例说明

日期：2026-09-27

## TC-ZOOM-CUSTOM-CURRENT-VALUE — 自定义缩放对话框带入当前值

**目的**
防止“Set Zoom Level”输入控件展示固定或被默认范围截断的值。

**测试位置**
`tests/tst_qviewtests.cpp::WindowBehaviorTests::testZoomCustomDialogStartsAtCurrentLevel`

**前置条件**

- Cocoa 测试进程可创建 MainWindow 和 Qt 输入对话框。
- 使用临时 SVG 文档并以矢量方式加载。

**步骤**

1. 将视图逻辑缩放设为 64 倍，并读取当前缩放百分比（预期 6400）。
2. 调用生产入口 `MainWindow::zoomCustom()`。
3. 在模态对话框事件循环中读取 `QInputDialog::doubleValue()` 和内部 `QDoubleSpinBox` 的显示文本；关闭对话框。

**预期**

- 模态输入框成功打开。
- 对话框数值与当前视图缩放百分比相同；输入控件格式化文本解析后也相同。

**缺陷复现和修复验证**

- 修复前：视图为 64 倍，对话框数值实测为 100，预期为 6400；用例失败。
- 修复后：数值和实际输入文本都为 6400；单次及 `-repeat 3` 均通过。
- 用例故意使用高于 Qt `QDoubleSpinBox` 默认最大值 99.99 的值，使“先赋值、后放宽范围”这一顺序错误在测试中暴露。依据：[Qt QDoubleSpinBox 文档](https://doc.qt.io/qt-6/qdoublespinbox.html)、[Qt QInputDialog 文档](https://doc.qt.io/qt-6/qinputdialog.html)。

## TC-VECTOR-GHOSTING-DRAG — 按住拖动期间全视口更新且滚动内容连续

**目的**
通过真实拖动时序、每次 paint 的区域、静止采样帧以及按滚动位移配准后的相邻帧重叠区域，检查 SVG/EPS 的滚动缓存残留与内容不连续风险。配套系统门禁还检查真实 HID 按住拖动期间的每条 paint 记录。

**测试位置**
`tests/tst_qviewtests.cpp::GraphicsViewTests::testVectorDragFrameBudgetForEPSAndSVG`

**前置条件**

- macOS Cocoa Qt 测试进程可显示/激活窗口并通过 `QScreen::grabWindow()` 截图；其采样是离散的，不等同于显示器逐刷新扫描捕获。
- EPS 渲染依赖可用；SVG 使用确定性 fixture。外部 SVG 可由 `FOVELLE_SVG_SAMPLE` 指定。
- 图片成功以矢量方式加载，视口有滚动范围，至少一个矢量 tile 已绘制。

**步骤**

1. 分别打开 EPS 与 SVG，以 64 倍缩放并将水平滚动条置于范围中间。
2. 在视口中心发送鼠标左键按下，随后发送带 `LeftButton` 按住标记的鼠标移动。
3. 验证第一帧已进入 `FullViewportUpdate`，滚动值发生变化，且首个 paint region 覆盖至少 90% 视口面积。
4. 鼠标继续按住并静止 120 ms（生产定时器间隔为 50 ms），验证仍为 `FullViewportUpdate`。
5. 持续按住移动 40 步，跨过有界交互 tile 外扩范围以覆盖 tile 替换过程。记录每个拖动 paint region；每步截取视口区域并计算深色像素占比。对确定性 fixture，最低占比须不低于 1%；此门槛只用于排除无图像信号的空白截图，不代替下一步的位移配准连续性判据。
6. 每步移动后立即截取最早可得屏幕帧，再于约 20 ms 和 40 ms 截取后续帧。按实际水平/垂直滚动条位移乘以设备像素比，对每一帧与前一位置帧做平移配准，只比较共同覆盖的视口区域；任一帧通道差值大于 32 的像素占比不得超过重叠区域的 5%。
7. 比较约 40 ms 候选帧与后续静止帧；通道差值大于 24 的变化像素不得超过全帧 5%。最早帧仍参与连续性门槛，不会因之后变清晰而被忽略。以上阈值是项目门槛，不是 Qt/macOS 规范。
8. 释放鼠标，验证恢复 `MinimalViewportUpdate` 并等待矢量细化完成。

**预期**

- EPS/SVG 在首次滚动、超时停顿后、继续拖动期间都保持完整更新。
- 每个鼠标滚动步骤的首个 drag paint region 至少覆盖 90% 视口面积；异步 tile 发布产生的后续局部 paint 不代替滚动帧判据。
- 确定性 fixture 不出现低于 1% 暗像素的抽样空白帧；连续静止截图不出现超过 5% 区域的明显变化。
- 相邻滚动帧配准后重叠区域的 mismatch ratio 不超过 5%；无足够重叠区域时用例失败，不把缺少比较数据视为通过。
- 鼠标释放后恢复 `MinimalViewportUpdate`。

**外部样本执行**

令 `FOVELLE_SVG_SAMPLE=/Users/inostarlin/Downloads/wanimagazine_logo.svg` 后运行同一用例。该文件本机可读，SHA-256 为 `d163fffd16b5002d3bb211253748419b010b685845e8ce7aef723e5f0e6c4212`。样本图颜色未知，故不对该样本应用颜色相关的暗像素占比断言；它仍执行实际矢量加载、拖动状态、滚动、paint region、重叠区域像素连续性和静止截图稳定性检查。

真实 HID 系统门禁设置 `FOVELLE_SVG_SAMPLE` 时，也用该路径启动生产 app。native helper 按纵向滚动范围动态放大，并验证视图确实接近纵向滚动末端后返回原几何。系统门禁对日志中的**每一条** `mouse_pan_active=true` paint 要求 `update_mode=full` 且 `dirty_ratio >= 0.9`，不以“至少一条 full paint”代替逐帧检查。

**负向控制**

- 缩放：生产代码修复前用例观察到 100 而非 6400，确认能检出旧行为。
- 拖动：将 `vectorRefineTimer` 回调临时恢复为无条件结束交互呈现时，已记录的旧版负向控制在停顿后更新模式断言失败；移除保护即可复现。
- tile 预取：旧 16 像素 overscan 曾测得 6.2% 瞬时错配；单独提高到 128 像素仍在 40 步用例中测得 5.52%。加入 64 像素提前请求下一 tile 后，40 步跨边界样本回归重复 3 次通过，确认测试覆盖的是预览切换窗口。

**稳定性与边界**

- 鼠标移动事件显式包含按下按钮，且使用 120 ms 越过生产的 50 ms 期限。
- 使用比例而非固定物理像素进行视口绘制区断言；scope guard 确保退出时释放鼠标和事件过滤器。
- 截图是离散抽样。即使所有截图都稳定，也不能排除采样间隔内更短的闪帧；颜色阈值也不能证明任意局部残影不存在。
- 重叠比较有意容忍最多 5% 的高差异像素，并允许一次延迟候选帧；它可以检出显著内容错位/缺块，但不能证明像素完全相同，也不覆盖未采样刷新周期。
- 使用真实外部样本的成功运行限于此本机文件和该 Qt/Cocoa 环境，CI 使用仓库 fixture。

## 关联回归用例

- `testVectorPanRepaintsOnlyExposedStrip`：检查矢量滚动时首帧整视口更新。
- `testVectorPaintClearsStalePixelsBeforeTileReady`：分别验证 SVG/EPS tile 尚未就绪时 paint 不残留哨兵像素。

# 图片序列边界提示与失焦图像保真：测试补充

日期：2026-09-29

本次每条原子验收标准均有独立静态契约和动态 QtTest 覆盖；静态入口为 `tests/navigation_boundary_hint_static.py`（同时注册为 CTest），动态入口为 `tests/tst_qviewtests.cpp::WindowBehaviorTests`。

## TC-NAV-HINT-BOUNDARY — 只对真实的 Previous/Next 边界失败提示

**测试目的**
验证到达序列头尾时提示方向正确，正常导航、循环导航及幻灯片自动推进不会被错误提示。

**前置条件**
- `loopfoldersenabled=false`；按文件名排序的三张有效 PNG 已加载。
- MainWindow 和 QVGraphicsView 已显示。

**输入数据**
`01-hint.png`、`02-hint.png`、`03-hint.png`，以及 Previous/Next 导航请求。

**操作步骤**
1. 从第一张向右移动，再返回第一张；确认成功导航未设置 hint message id。
2. 在第一张请求上一张；检查 `previous` 消息 id、英文源文案可翻译、当前索引仍为 0。
3. 打开第三张并请求下一张；检查 `next` 消息 id、当前索引仍为 2。
4. 静态检查 `reachedEnd`、Previous/Next mode 和 `reportNavigationBoundary` 条件，同时检查幻灯片传入 `false`。

**预期结果**
仅失败边界请求生成对应方向提示，不更改当前图片；成功导航/自动幻灯片不发出提示信号。

**后置条件**
关闭窗口、恢复 QSettings、删除临时文件。

**测试代码**
`tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationBoundaryHintFeedbackAndAppearance`；`tests/navigation_boundary_hint_static.py::AC-NAV-HINT-BOUNDARY`。

## TC-NAV-HINT-LOCALIZED — Appearance 样式、多语言与辅助技术文字

**测试目的**
验证 Light、Dark Appearance 使用不同颜色组合，且各语言运行时提示文字与对应 TS 翻译一致，并通过可访问性事件发布。

**前置条件**
Qt 翻译 QM 已由 lrelease 生成；有效三图序列在 MainWindow 中打开。

**输入数据**
Light：背景 `rgba(34,34,34,232)`、前景白色；Dark：背景 `rgba(242,242,242,238)`、前景 `rgb(28,28,28)`；中文简繁、西班牙语和日语的 Previous 提示译文。

**操作步骤**
1. 在 Light Appearance 触发 Previous 边界并读取提示外观属性。
2. 将设置切至 Dark，再次触发并读取背景/前景颜色。
3. 依次安装四份 QM 翻译，触发 Previous 并读取 `accessibleName`；逐份卸载 translator。
4. 静态解析所有 TS XML，断言两条源文案均有非空译文，并检查 `QAccessible::Alert`。

**预期结果**
Light 与 Dark 色彩不同；翻译后的可访问文本依序等于“没有上一张图片”“沒有上一張圖片”“No hay imagen anterior”“前の画像はありません”；四种译文资源也各自包含 Next 文案。

**后置条件**
卸载测试 translator，恢复 Light 设置并关闭窗口。

**测试代码**
`tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationBoundaryHintFeedbackAndAppearance`；`tests/navigation_boundary_hint_static.py::AC-NAV-HINT-LOCALIZED`。

## TC-NAV-HINT-PRESENTATION — 位置、单条呈现和动效时序

**测试目的**
验证提示底部居中、淡入淡出时长、停留时间及 HDR 原生 viewport overlay。

**前置条件**
MainWindow 有效；Qt 动画事件循环运行。

**输入数据**
180 ms `QPropertyAnimation`；4000 ms single-shot timer；方向文本一条。

**操作步骤**
1. 静态检查 viewport 几何使用水平居中和底边 24 px 留白、动画及 timer 常量、原生 CALayer overlay API。
2. 动态触发 Previous 边界，读取动画 duration、计时器 interval、显示持续时间属性。
3. 等待动画达到不透明端点并检查计时器已开始。
4. 若本机 renderer 启用 native navigation overlay，则检查 renderer 的 `nativeBoundaryHintVisible` 为 true。

**预期结果**
淡入与淡出各 180 ms；完全显示 4000 ms 后淡出；同时至多一个提示，HDR 图层路径有可见提示内容。

**后置条件**
窗口关闭后计时器及动画随 MainWindow 销毁。

**测试代码**
`tests/tst_qviewtests.cpp::WindowBehaviorTests::testNavigationBoundaryHintFeedbackAndAppearance`；`tests/navigation_boundary_hint_static.py::AC-NAV-HINT-APPEARANCE-ANIMATION`。

## TC-VIEWPORT-FOCUS-SHARP — 实际屏幕合成图像在失焦前后保持清晰

**测试目的**
在真实窗口失焦场景中比较 WindowServer 输出，而不是比较控件自己重新绘制的离屏结果，以检出 focus change 引起的模糊、变暗或构图变化。

**前置条件**
- macOS Cocoa QtTest 运行于有可读显示器的会话。
- MainWindow 被恢复为屏幕内非最大化窗口；另一个顶层窗口能被激活且不遮挡目标视口。
- `tests/data/focus_transparency.png` 作为 native SDR 图像完整加载，Metal/CALayer renderer 首帧及几何已就绪。

**输入数据**
用户样本 `auctions_r_34_2x.png` 的仓库固定副本，476×68、RGBA，SHA-256 `c837791cdead6ff31446de1828580d8c7820cfd8751f4cd1bbe14f81896fc6a3`；聚焦与失焦阶段的 `QScreen::grabWindow(mainWindow.winId(), viewportRect...)` 屏幕像素。

**操作步骤**
1. 读取 fixture 并断言 alpha 通道存在，确认文件走 native SDR Metal renderer 路径且首帧已显示。
2. 激活 MainWindow，等待其成为 active window，采集实际屏幕 viewport 像素作为参考帧。
3. 激活一个几何上不与 MainWindow/viewport 相交的信息样式 top-level QDialog。
4. 等待信息窗口 active、MainWindow inactive、系统更新完成，再从相同屏幕矩形采集失焦帧。
5. 比较 viewport 图像和透明点；外层圆角系统合成区不纳入精确像素比较。同时静态检查 native SDR 常驻逻辑及 HDR 另行满足反向淡出契约。

**预期结果**
裁去窗口系统外缘 32 device-pixel margin 后，失焦视口内容像素与聚焦参考帧相同；已知透明源点也单独验证底色。失焦是真实窗口激活切换产生，而非手工发送 FocusOut/WindowDeactivate 事件。

**后置条件**
关闭信息窗口与 MainWindow，恢复 `quitOnLastWindowClosed` 和 QSettings；临时屏幕截图随临时目录清理。

**测试代码**
`tests/tst_qviewtests.cpp::WindowBehaviorTests::testViewportImageRemainsSharpAfterFocusLoss`；`tests/navigation_boundary_hint_static.py::AC-VIEWPORT-FOCUS-SHARP`。

## TC-TRANSPARENT-PNG-BACKGROUND — native SDR 透明像素失焦时仍使用应用底色

**测试目的**
检出失焦隐藏原生 SDR layer 后，透明 PNG 暴露其后桌面/窗口背景的回归。

**前置条件**
与 TC-VIEWPORT-FOCUS-SHARP 相同；应用固定使用 Dark Theme 且禁用 checkerboard，已知 PNG 透明像素位于 source coordinate `(470, 34)`。

**输入数据**
RGBA fixture 的 `(470, 34)` alpha 值为 0；viewport 底色来自 `Qv::viewportBackgroundColor(Qv::Theme::Dark)`；截图直接采自屏幕。

**操作步骤**
1. 确认 `(470, 34)` 的 alpha 为 0，并把该源图坐标映射到 viewport 的实际屏幕采样位置。
2. 聚焦时从该点读取实际屏幕颜色，断言它与 Dark viewport 背景颜色各通道差值不超过 12。
3. 激活不遮挡图像的另一顶层窗口，确认主窗失焦后 renderer 的 `presentationActiveRequested` 仍为 true。
4. 断言 native SDR layer 仍稳定接管 viewport（`NoViewportUpdate`），并重新读取透明点。
5. 静态检查失焦路径保留 native SDR presentation、失焦就绪状态识别 native SDR，以及 proxy 隐藏门槛。

**预期结果**
透明点在聚焦及失焦时均显示相同 Dark viewport 底色，而非后方窗口内容；裁去系统圆角窗口边缘后的视口内容与聚焦时像素一致。Native HDR 图片由独立的 HDR 失焦过渡用例验收。

**后置条件**
同 TC-VIEWPORT-FOCUS-SHARP；fixture 保留在仓库，临时窗口截图清理。

**测试代码**
动态断言与 TC-VIEWPORT-FOCUS-SHARP 共用一次真实窗口切换：`tests/tst_qviewtests.cpp::WindowBehaviorTests::testViewportImageRemainsSharpAfterFocusLoss`；静态原子契约为 `tests/navigation_boundary_hint_static.py::AC-TRANSPARENT-RASTER-BACKGROUND`。

## TC-HDR-FOCUS-FADE — HDR 失焦渐暗且稳定端点保留源分辨率

**测试目的**
检出 HDR 窗口失焦时瞬间跳至 SDR、亮度不渐降、失焦 fade 时长偏离打开时、过早关闭 EDR、SDR 最终端点缺失，以及失焦后图像细节模糊的问题。

**前置条件**
- macOS Cocoa + Metal + EDR 可用；系统测试使用已确认含 gain map 的 HDR JPEG 和真实可见窗口。
- Renderer 已完成首个 final-headroom 帧；`FOVELLE_HDR_TEST_FOCUS_TRANSITION=1` 的测试驱动会先将图像放大至原始视图倍率的两倍，再于 1400ms 失焦、2200ms 重新聚焦，不遮挡窗口。
- `FOVELLE_HDR_TRANSITION_LOG=1` 以 16ms 间隔记录当前 presentation-layer opacity、EDR 状态和单调时钟毫秒值；屏幕采样由 `screencapture` 按原生 window id 获取。

**输入数据**
真实 6048×8064 gain-map HDR JPEG；两张同窗口、相同缩放/视口几何的屏幕采样分别由 `final-frame-visible` 与 `inactive-sdr-visible` 状态事件触发；两状态日志均包含 source pixel size 与 Qt fallback pixmap 实际尺寸；每条 transition 日志记录 active、animation、opacity、fallback、wants-EDR、transition count 和时间戳。

**操作步骤**
1. 由系统测试启动可见真实 HDR 窗口，测试驱动使用 production `zoomAbsolute()` 将视图放大 2×；确认首个 HDR 图像帧出现后捕获聚焦参考帧。
2. 在 1400ms 驱动 production focus state 进入 inactive，连续采集实际 presentation-layer opacity；动画稳定结束且 SDR fallback 已显示后采集同一窗口截图。
3. 过滤 `active_requested=false` 的序列，验证至少 8 个中间动画帧、opacity 单调递减、EDR 在动画中仍启用、稳定态 opacity=0 / SDR fallback 可见 / EDR 关闭；测量动画持续时间为 350–550ms，以容纳 16ms 采样量化与 30ms renderer 完成守卫。
4. 检查 `inactive-sdr-visible` 的 fallback 实际 pixmap 宽、高与 HDR source pixel 宽、高严格相等；放大 2× 使 2048px 上限代理在测试视口中必然被放大，分辨率回归会被精确拒绝，而非通过色调敏感的宽松清晰度猜测。
5. 以 Core Animation presentation-layer 每16ms的实际 opacity 序列核验 HDR 图层贡献逐步递减；聚焦与失焦同窗口截图按固定物理视口比较边缘结构，不把 8-bit PNG 亮度值当作 EDR 光度测量。
6. 用状态日志时间戳将屏幕采样与 `final-frame-visible`、`inactive-sdr-visible` 两个实际稳定端点关联，检查失焦端点与聚焦参考边缘相似度≥0.90；验证重聚焦也单调增亮并在同一时长回到 opacity=1 / EDR enabled。
7. 用单元突变样例将真实 6048×8064 源改为 1536×2048 fallback，断言分辨率验收失败；用静态源码检查 HDR SDR graph 到 Qt fallback 的完整尺寸渲染参数为 0，且失焦状态遥测输出 fallback 实际尺寸。

**预期结果**
失焦路径产生多帧单调 opacity 下降，动画约 450ms；HDR 图层对底层 SDR proxy 的贡献逐步减少，EDR 保持到动画结束。稳定态 SDR fallback 的像素尺寸与 HDR 源图严格一致，聚焦与失焦窗口图像边缘相似度≥0.90，原生 HDR opacity=0 且 EDR 已关闭。反向激活使用相同时间曲线恢复 HDR。若 fallback 缺少尺寸遥测，测试失败关闭。

**后置条件**
关闭测试进程和窗口；原始日志、截图、哈希、亮度序列、边缘相似度和状态指标写入 JSON 证据，不更改用户设置。

**测试代码**
测试分析器及旧逻辑/2048px 尺寸突变用例：`tests/hdr_quality_system.py::focus_transition_metrics`、`focus_proxy_matches_source_resolution`、`tests/test_hdr_focus_transition_metrics.py`；真实窗口系统回归：`tests/hdr_focus_transition_system.py`、`tests/hdr_quality_system.py::SYS-HDR-FOCUS-FADE-AND-SHARP-ENDPOINT`；源码契约：`tests/hdr_quality_static.py::ST-HDR-FOCUS-PRESENTATION-TRANSITION`。

### 原子验收用例拆分

以下每一项均单独判定；共享相同的 HDR fixture 和真实窗口测试运行器，但彼此失败不会被聚合均值抵消。

#### TC-HDR-FADE-01 — 失焦 opacity 连续递减

- **测试目的**：捕获瞬时隐藏 HDR presentation 的回归。
- **前置条件**：macOS 原生窗口和 HDR renderer 可用；真实 gain-map HDR 图像已显示稳定 final-headroom 帧。
- **输入数据**：renderer 的16 ms transition 采样；固定 gain-map JPEG。
- **操作步骤**：驱动生产失焦处理；提取 `active_requested=false` 且 animation-in-flight 的 opacity 序列。
- **预期结果**：至少8个中间帧；opacity 从≥0.8单调下降至≤0.2。
- **后置条件**：窗口回到脚本管理状态并关闭；日志存档。
- **覆盖**：动态；`test_rejects_legacy_instant_deactivation_bug` 负责旧实现突变验证。

#### TC-HDR-FADE-02 — 失焦时长与打开时长一致

- **测试目的**：确保 focus loss 不绕过打开时的既有缓动时长。
- **前置条件**：同 TC-HDR-FADE-01。
- **输入数据**：单调时钟毫秒时间戳、opacity 序列和 settled 事件。
- **操作步骤**：从首个动画帧量到稳定完成时刻，并与0.45秒renderer时长窗口对照。
- **预期结果**：350–550ms，且静态源码仍引用同一个450ms时长与ease-in/ease-out曲线。
- **后置条件**：不写用户配置；保留 transition JSON。
- **覆盖**：动态+静态；`test_accepts_smooth_bidirectional_450ms_transition` 和源码契约。

#### TC-HDR-FADE-03 — EDR 仅在失焦动画结束时关闭

- **测试目的**：捕获淡出期间提前关闭 EDR 或稳定后未关闭的错误。
- **前置条件**：真实 HDR final-headroom 帧已呈现。
- **输入数据**：每帧 `wants_edr`、settled opacity、fallback visibility。
- **操作步骤**：驱动失焦并核对所有中间帧及 `inactive-sdr-visible` 终态。
- **预期结果**：淡出期间 EDR=true；终态 opacity=0、EDR=false、fallback 可见。
- **后置条件**：随后激活以继续验证反向路径。
- **覆盖**：动态；`focus_transition_metrics` 及 renderer 日志。

#### TC-HDR-SHARP-04 — 失焦终态 fallback 与源图像素尺寸一致

- **测试目的**：稳定捕获造成放大模糊的2048px fallback。
- **前置条件**：具备至少2048px长边的真实 HDR gain-map 图像；source metadata 与实际 Qt pixmap size 均被遥测。
- **输入数据**：6048×8064 HDR JPEG；`inactive-sdr-visible` 状态中的 source/fallback 宽高。
- **操作步骤**：放大至2×、触发失焦、等待稳定终态；严格比较 fallback 与源图实际宽高。
- **预期结果**：fallback 为6048×8064，两个维度均严格相等；若降为1536×2048或缺少遥测则失败。
- **后置条件**：窗口关闭；记录源/代理尺寸。
- **覆盖**：动态+突变单测+静态；`focus_proxy_matches_source_resolution`、`test_rejects_regression_to_bounded_2048px_inactive_proxy` 和 decoder contract。

#### TC-HDR-SHARP-05 — 放大后的实际窗口细节不丢失

- **测试目的**：从真实 WindowServer 输出而非 QWidget 离屏绘制核验失焦终态。
- **前置条件**：同 TC-HDR-SHARP-04；聚焦与失焦采样几何相同。
- **输入数据**：由 `final-frame-visible` 与 `inactive-sdr-visible` 事件关联的整窗屏幕截图。
- **操作步骤**：按事件触发两张窗口截图；对齐视口几何并计算边缘 cosine similarity。
- **预期结果**：聚焦/失焦截图相似度≥0.90，且尺寸断言同时通过；尺寸检查是防止宽松边缘阈值漏检的主判据。
- **后置条件**：截图留在 `build/test-results/` 作为 JSON 报告引用的证据。
- **覆盖**：动态屏幕采样；`tests/hdr_focus_transition_system.py`。

#### TC-HDR-RESTORE-06 — 重新聚焦恢复 HDR 终态

- **测试目的**：防止失焦修复牺牲反向激活动画或 EDR 恢复。
- **前置条件**：已通过失焦稳定终态。
- **输入数据**：重新激活时每帧 opacity/EDR 与 renderer settled 事件。
- **操作步骤**：驱动 production 激活状态并采样至稳定。
- **预期结果**：opacity 单调上升、时长350–550ms，终态 opacity=1 且 EDR=true。
- **后置条件**：窗口恢复前台后由runner退出并清理。
- **覆盖**：动态；`test_accepts_smooth_bidirectional_450ms_transition`。

# macOS 原生提示框：测试用例补充

本组按六条原子标准分开执行：直接构造入口、原生平台路径、感叹号类别图标移除、macOS sheet 紧凑布局、parent/sheet 模态，以及 Cocoa Appearance 运行时行为。每条用例含测试目的、前置条件、输入数据、操作步骤、预期结果和后置条件。源代码契约由 Python 静态测试执行；图标、模态和 Appearance 行为由 Cocoa Qt 动态测试执行。

## TC-ALERT-CENTRAL — 消息框创建入口唯一

- **测试目的**：确保生产代码只在统一适配器中直接创建 `QMessageBox`。
- **前置条件**：仓库 `src/` 下生产 C++/Objective-C++ 源文件可读。
- **输入数据**：全部 `.cpp`、`.mm` 文件；扫描前去掉注释，排除文档中的示例代码。
- **操作步骤**：执行 `python3 tests/native_alerts_acceptance.py`；枚举 QMessageBox 构造表达式并核对文件位置。
- **预期结果**：唯一实例化表达式为 `src/nativedialogs.cpp` 中的 `new QMessageBox(parent)`；更新和会话提示只能从共享工厂取得。
- **后置条件**：静态扫描不修改生产源码或 UI 状态。
- **类型/代码**：静态；`NativeAlertsAcceptance.test_ST_ALERT_CENTRAL`。

## TC-ALERT-NATIVE — 不禁用原生消息框

- **测试目的**：阻止某个入口将提示框退回自绘/非平台原生消息框路径。
- **前置条件**：共享工厂源代码可读；测试运行时 Qt 版本宏可解析。
- **输入数据**：`NativeDialogs::createMessageBox()` 构造、选项和内容赋值顺序；全部生产源文件的 `DontUseNativeDialog` 引用。
- **操作步骤**：在 `python3 tests/native_alerts_acceptance.py` 中检查 Qt 6.6+ 的关闭原生禁用选项操作位于正文赋值之前，并搜索是否有代码启用此选项。
- **预期结果**：选项在设置正文前被明确设为 false，且生产代码没有任何 `DontUseNativeDialog=true`。
- **后置条件**：无窗口或配置被打开/修改。
- **类型/代码**：静态；`NativeAlertsAcceptance.test_ST_ALERT_NATIVE`。

## TC-ALERT-ICON — 移除信息/警告感叹号图标

- **测试目的**：去掉 Information 与 Warning QMessageBox 左侧不美观的感叹号类别 glyph，同时不抹去 Critical/Question 的区别。
- **前置条件**：NativeDialogs 工厂和 Cocoa QtTest 环境已构建。
- **输入数据**：Information、Warning、Critical、Question 四种消息框类型；所有用例使用普通文案和标准按钮。
- **操作步骤**：静态运行 `python3 tests/native_alerts_acceptance.py` 检查分类映射；动态创建四种 QMessageBox 并读取 `icon()`。
- **预期结果**：Information/Warning 的 icon 为 `NoIcon`；Critical 仍为 `Critical`，Question 仍为 `Question`；原始正文与按钮保留。
- **后置条件**：临时消息框全部删除，不弹出无关用户提示。
- **类型/代码**：静态+动态；`NativeAlertsAcceptance.test_UT_ALERT_ICON`、`WindowBehaviorTests::testNativeMessageBoxesUseParentSheets`。

## TC-ALERT-LAYOUT — 无类别图标的原生紧凑 sheet

- **测试目的**：确认无感叹号类别图标后，带父级消息框使用原生 sheet 版式，不再为大图标预留布局宽度。
- **前置条件**：macOS Cocoa 平台插件可用；NativeDialogs 保持原生消息框选项。
- **输入数据**：一个短正文、一枚标准 OK 按钮、有父窗口的 Information 提示框。
- **操作步骤**：运行动态用例，检查 icon 为 NoIcon、modal 为 WindowModal、parentWidget 指向 owner；显示消息框并查询其 Cocoa window appearance。
- **预期结果**：提示附着到所属窗口并呈现 Qt 定义的 macOS Sheet；信息/警告类别图标不存在；不通过应用 QSS 绘制额外布局。
- **后置条件**：关闭 sheet 和 owner，Appearance/选项由测试作用域恢复。
- **类型/代码**：静态+动态；`test_ST_ALERT_SHEET`、`WindowBehaviorTests::testNativeMessageBoxesUseParentSheets`。

## TC-ALERT-SHEET — 父窗口附着和无主窗口模态

- **测试目的**：确保有明确所属设置/应用窗口的提示是原生 sheet，同时保留退出流程无主窗口时的应用模态。
- **前置条件**：共享适配器和 Cocoa 动态测试源码存在。
- **输入数据**：非空 QWidget parent 与 null parent。
- **操作步骤**：静态检查工厂 parent 条件；运行 Qt 动态用例并读取两类 QMessageBox 的 window modality。
- **预期结果**：有父级的是 `Qt::WindowModal` 且 `parentWidget()` 指向该 owner；无父级的是 `Qt::ApplicationModal`。Qt 文档规定前者在 macOS 呈现为 parent sheet。
- **后置条件**：动态用例关闭提示框和临时 owner。
- **类型/代码**：静态+动态；`test_ST_ALERT_SHEET`、`WindowBehaviorTests::testNativeMessageBoxesUseParentSheets`。

## TC-ALERT-APPEARANCE — Light、Dark 和 System 外观

- **测试目的**：确保共享提示框的 AppKit 窗口与应用 Appearance 同步。
- **前置条件**：macOS Cocoa Qt 测试二进制、NativeDialogs theme bridge 和 `FOVELLE_SYSTEM_THEME` 测试覆盖可用。
- **输入数据**：应用 Light、Dark；System 配置且受控系统 Appearance 分别为 light、dark；各路径均创建有父窗口的信息框。
- **操作步骤**：运行 `ctest --test-dir build -R 'FovelleNativeAlerts(Static|Dynamic)' --output-on-failure`；测试显示消息框并采集实际窗口 Appearance 名称。
- **预期结果**：Light/System-light 为 Aqua；Dark/System-dark 为 DarkAqua；同时 native option 未禁用、信息图标为 NoIcon、父窗口/模态、正文和标准 OK 按钮正确。
- **后置条件**：所有提示和 owner 关闭；ScopedOptionValues/ScopedEnvironmentValue 恢复测试前设置。
- **类型/代码**：静态+动态；`NativeAlertsAcceptance.test_UT_ALERT_APPEARANCE`、`WindowBehaviorTests::testNativeMessageBoxesUseParentSheets`。

## TC-ALERT-CONTENT — 按钮角色与 Cancel 语义

- **测试目的**：确认通过共享 native sheet 呈现时，退出会话提示的自定义操作仍传回正确业务角色，标准 Cancel 仍可取消。
- **前置条件**：Cocoa QtTest 构建完成；测试临时 owner 可显示。
- **输入数据**：`Remember` (YesRole)、`End Session` (NoRole)、标准 `Cancel` 按钮。
- **操作步骤**：运行 `WindowBehaviorTests::testNativeMessageBoxPreservesActionResponses`；在每个 `exec()` 模态事件循环中由单次 timer 自动点击 Remember 或 Cancel。
- **预期结果**：第一个消息框的 clicked button 为 Remember 且角色为 YesRole；第二个消息框的 clicked standard button 为 Cancel。
- **后置条件**：两个消息框及临时 owner 窗口关闭并释放。
- **类型/代码**：Cocoa 动态；`WindowBehaviorTests::testNativeMessageBoxPreservesActionResponses`，由 `FovelleNativeAlertsDynamic` 执行。


# macOS QMessageBox → NSAlert 全量委托：测试用例补充

日期：2026-09-29
范围：以下用例取代本文件较早版本中的 WindowModal sheet、保留自定义按钮和移除 Information/Warning severity 图标的过期验收方向。系统尺寸、边距、文字位置或按钮横向等距不属于应用级可断言项。

## 原子验收标准

- **AC-NATIVE-01** 生产 QMessageBox 只有一个统一构造入口，无静态消息框旁路。
- **AC-NATIVE-02** 当前 Cocoa 运行时实际显示 NSAlert；无 native 禁用、NonModal/WindowModal fallback、rich text 或 detailed text 触发条件。
- **AC-NATIVE-03** 仅提供 messageText、可选 informativeText、severity 和标准按钮；没有自定义按钮、checkbox、标题窗口属性或图标。
- **AC-NATIVE-04** 业务不控制 alert 视觉/几何，不对宽度/边距/按钮位置作断言。
- **AC-NATIVE-05** 调用方文字仍可本地化；长文案由 AppKit 排版，Light/Dark/System 随有效 Appearance。
- **AC-NATIVE-06** 标准按钮的 Qt 结果映射保持正确。
- **AC-NATIVE-07** 回归屏障能检出代表性的 native fallback 和视觉自绘突变。

## TC-ST-ALERT-CENTRAL — 单一构造入口（静态）

- **测试目的**：确保所有生产 QMessageBox 均由统一入口创建。
- **前置条件**：项目 `src/` 源码可读。
- **输入数据**：所有生产 `.cpp`、`.mm` 文件。
- **操作步骤**：运行 `python3 tests/native_alerts_acceptance.py`，扫描 new/stack 构造、继承和静态便捷 API；确认唯一构造点和每个调用方。
- **预期结果**：仅 `src/nativedialogs.cpp` 构造 QMessageBox，所有 production 提示经 NativeDialogs；未使用 `QMessageBox::information/warning/critical/question`。
- **后置条件**：不修改项目状态。
- **测试代码/类型**：`NativeAlertsAcceptance.test_ST_ALERT_CENTRAL`；静态。

## TC-ST-ALERT-SEMANTICS — 原生 Alert 语义字段（静态）

- **测试目的**：阻止应用在消息语义字段外继续设置视觉控件。
- **前置条件**：统一 factory 源码可读。
- **输入数据**：factory 的 severity、text、informativeText、standardButtons 属性及可能的 title/icon/custom-action API。
- **操作步骤**：扫描 `createMessageBox()` 函数体，验证四种语义字段和 plain-text 规范化；反向扫描自定义标题、图标、按钮、details 和 checkbox。
- **预期结果**：有 `setIcon(severity)`、`setText(plainAlertText(messageText))`、`setInformativeText(plainAlertText(informativeText))`、`setStandardButtons(buttons)`；没有 window title、pixmap、custom button、checkbox、details。
- **后置条件**：无运行时窗口变更。
- **测试代码/类型**：`NativeAlertsAcceptance.test_ST_ALERT_SEMANTICS`；静态。

## TC-ST-ALERT-NATIVE-ROUTE — Cocoa native helper 条件（静态）

- **测试目的**：捕捉 `QMessageBox` 配置可能使 Qt Cocoa 返回 QWidget fallback 的情况。
- **前置条件**：目标为 macOS Cocoa build；参考 Qt 6.11 backend 条件。
- **输入数据**：native option 顺序、模态、show/open、rich/detail 字段。
- **操作步骤**：检查 option 在语义属性前关闭；确认 application-modal、`show()`，并确认 rich input 被归一、无 detailedText；扫描所有 factory 调用。
- **预期结果**：factory 满足 Qt Cocoa NSAlert 的 native helper 前置条件；任何禁用选项、`open()`、详细文本或窗口模态改动均失败。
- **后置条件**：源码不变。
- **测试代码/类型**：`NativeAlertsAcceptance.test_ST_ALERT_NATIVE_ROUTE`；静态。

## TC-ST-ALERT-NO-CUSTOM-LAYOUT — 禁止 widget 视觉/几何（静态）

- **测试目的**：保证尺寸、间距、按钮排布和 Appearance 交给 AppKit。
- **前置条件**：production 源码可扫描。
- **输入数据**：alert 对象的方法调用。
- **操作步骤**：扫描消息框实例上 stylesheet、fixed size、geometry、margins、font、layout、custom button、checkbox、icon pixmap 和 theme adapter 调用。
- **预期结果**：没有业务自设外观或几何；普通设置/关于等非 QMessageBox 对话框不被该规则误扫。
- **后置条件**：只读扫描完成。
- **测试代码/类型**：`NativeAlertsAcceptance.test_ST_ALERT_NATIVE_STYLING`；静态。

## TC-ST-ALERT-LOCALIZATION — 本地化入口（静态）

- **测试目的**：防止生产提示绕过 tr 文案或自行安装标准按钮文字。
- **前置条件**：Qt 翻译和 Cocoa platform theme 编译配置可读取。
- **输入数据**：生产 `showMessage/createMessageBox` 调用参数和标准按钮工厂。
- **操作步骤**：解析生产调用参数；查验至少一个业务文本来自 `tr()`（局部组装的 Delete 文案追到 `messageText = tr(...)`）；确认标准按钮只有枚举值。
- **预期结果**：业务 copy 经翻译系统提供；按钮标签由 Qt Cocoa/AppKit 平台提供；未硬编码按钮文字。
- **后置条件**：不生成或更改翻译目录。
- **测试代码/类型**：`NativeAlertsAcceptance.test_ST_ALERT_LOCALIZATION`；静态。

## TC-UT-ALERT-NSALERT-APPEARANCE — 真实 NSAlert 与长文案（动态）

- **测试目的**：验证四种 severity、三种 Appearance 分支和长多语言文案确实由 Cocoa 原生 alert 呈现。
- **前置条件**：macOS QtTest binary 以 `QT_QPA_PLATFORM=cocoa` 运行；可读 `qt.qpa.dialogs` debug 日志。
- **输入数据**：Information/Warning/Critical/Question；Light、Dark、受控 System-light/System-dark；日文+西班牙文长文案，并包裹 HTML 标签作为归一化输入；标准 OK。
- **操作步骤**：执行 `testNativeMessageBoxesUseCocoaAlertsAcrossAppearanceAndSeverity`；由定时器点击标准 OK；捕获 Qt Cocoa 日志；读取活动 AppKit modal window 的 `effectiveAppearance`。
- **预期结果**：每个 case 包含 `Showing <NSAlert`；severity、plain message/informativeText 和标准 OK 正确；有效外观为期望 Aqua 或 DarkAqua；不测具体几何。
- **后置条件**：每个 alert 关闭并删除，settings 和受控环境变量恢复。
- **测试代码/类型**：`WindowBehaviorTests::testNativeMessageBoxesUseCocoaAlertsAcrossAppearanceAndSeverity`；动态。

## TC-UT-ALERT-STANDARD-RESPONSES — 标准动作映射（动态）

- **测试目的**：确保无自定义 button 时业务确认结果仍稳定。
- **前置条件**：同 TC-UT-ALERT-NSALERT-APPEARANCE。
- **输入数据**：Save、Discard、Cancel 标准按钮，各点击一次。
- **操作步骤**：执行 `testNativeMessageBoxStandardButtonResponses`；逐个显示标准 Cocoa alert，模拟点击相应标准按钮，捕获 native 后端日志和 Qt 响应。
- **预期结果**：每个 Alert 恰有三个标准按钮；Qt result 与 clicked standard button 都等于输入枚举；每个弹窗实际创建 NSAlert。
- **后置条件**：alert 关闭并删除。
- **测试代码/类型**：`WindowBehaviorTests::testNativeMessageBoxStandardButtonResponses`；动态。

## TC-MT-ALERT-REVERSE — 逆向突变检出（静态突变）

- **测试目的**：证明源码验收不会在典型违规改变后仍然通过。
- **前置条件**：干净源码静态检查通过。
- **输入数据**：内存突变：AppModal→WindowModal、插入 fixed size、插入 custom button、native option false→true。
- **操作步骤**：执行 `NativeAlertsAcceptance.test_MT_ALERT_REVERSE_FALSIFICATION_GUARDS`；每个突变只注入内存中的 factory 文本，再调用相应契约测试。
- **预期结果**：四种突变全部由 AssertionError 拒绝；源文件在测试中不被改写。
- **后置条件**：源树哈希/工作区保持不变。
- **测试代码/类型**：静态突变测试。
