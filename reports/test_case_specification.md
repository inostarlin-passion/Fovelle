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
5. 持续按住移动 40 步，跨过有界交互 tile 外扩范围以覆盖 tile 替换过程。记录每个拖动 paint region；每步截取视口区域并计算深色像素占比。对确定性 fixture，最低占比须不低于 10%。
6. 每步移动后立即截取最早可得屏幕帧，再于约 20 ms 和 40 ms 截取后续帧。按实际水平/垂直滚动条位移乘以设备像素比，对每一帧与前一位置帧做平移配准，只比较共同覆盖的视口区域；任一帧通道差值大于 32 的像素占比不得超过重叠区域的 5%。
7. 比较约 40 ms 候选帧与后续静止帧；通道差值大于 24 的变化像素不得超过全帧 5%。最早帧仍参与连续性门槛，不会因之后变清晰而被忽略。以上阈值是项目门槛，不是 Qt/macOS 规范。
8. 释放鼠标，验证恢复 `MinimalViewportUpdate` 并等待矢量细化完成。

**预期**

- EPS/SVG 在首次滚动、超时停顿后、继续拖动期间都保持完整更新。
- 每个鼠标滚动步骤的首个 drag paint region 至少覆盖 90% 视口面积；异步 tile 发布产生的后续局部 paint 不代替滚动帧判据。
- 确定性 fixture 不出现低于暗像素门槛的抽样空白帧；连续静止截图不出现超过 5% 区域的明显变化。
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
