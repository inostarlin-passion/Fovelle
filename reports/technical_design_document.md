# 悬浮导航按钮统一样式设计

## 问题界定与代码证据

| 检索路径 | 交叉核验结果 |
|---|---|
| `MainWindow::initializeNavigationButtons` → `ImageNavigationButton::paintEvent` | 上一张和下一张共用同一个控件类型。原有深色背景款绘制灰色圆角底板及深灰色箭头，浅色背景款只绘制箭头。 |
| 边缘鼠标事件、图像加载及窗口变化 → `updateNavigationButtonAppearance` → `QVGraphicsView::sampleDisplayedImageBrightness` | 每个按钮的位置都会单独取样。亮度低于 0.5 时选用深色背景款。搜索调用者确认，亮度 API 和它的图像缓存只为这项样式选择服务。 |
| `MainWindow::syncNavigationButtonOverlay` → `QVGraphicsView::setHDRNavigationOverlay` → `HDRRenderer::setNavigationOverlay` | 原生渲染也根据同一个样式标志绘制。支持原生覆盖层的 SDR 图像同样走这条路径。只改 QWidget 的绘制无法覆盖这类图像。 |
| `setNavigationButtonVisible`、几何更新和鼠标事件 → `syncNavigationButtonOverlay` | 样式选择与可见性、几何、点击和动画分别处理。几何更新、动画推进和鼠标状态变化各自同步覆盖层。 |

## 方案与正确性证明

1. 在 QWidget 和原生图层中，直接绘制已有的深色背景款。底板 RGB 为 128，默认 alpha 为 220，悬停或按下时为 235，禁用时为 100。箭头 RGB 为 48，禁用时 alpha 为 90。保留各自已有的圆角、尺寸和箭头路径。
2. 删除样式标志及两条渲染路径之间的样式参数，同时删除亮度采样入口、专用缓存和样式更新调用。选择 QWidget 或原生图层仍由现有的渲染能力决定。
3. 两条绘制路径中，颜色和形状仅依赖交互状态、尺寸及箭头方向。两处保留的表达式均来自原深色背景分支，因此对任意背景，同尺寸、同方向、同交互状态生成的绘制内容都等同于原深色背景款。半透明底板与图像混合后，最终屏幕像素仍会随图像变化。
4. QWidget 先合成底板和箭头，再对合成图应用 `paintOpacity`。原生路径继续只对允许组透明度的父图层应用动画值，子图层用自身的颜色 alpha。背景透明的圆角外区域和淡入淡出的整体乘数由此保留。
5. 按钮的边缘命中、文件可用性及点击连接继续使用原有输入和事件处理。移除的调用只更新样式并重复同步图层。文件加载仍排队更新可见性，几何和动画各自保留同步入口，因此移除样式更新后仍能完成显示、切换文件和隐藏。
