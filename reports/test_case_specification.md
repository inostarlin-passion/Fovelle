# 滚动条与右下角缩放测试用例说明

## 1. 测试范围与规则

本规格对应 [`reports/technical_design_document.md`](technical_design_document.md) 的四条原子验收标准。动态测试使用 CMake 生成的 QtTest 程序 `build-current/tests/fovelle_tests`，在 macOS Cocoa 平台运行；静态测试读取源码、测试 marker 与本规格文件，输出机器可读 JSON。

Qt 行为依据：[`QGraphicsView::sceneRect`](https://doc.qt.io/qt-6/qgraphicsview.html#sceneRect-prop)、[`QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)、[`QAbstractSlider::value`](https://doc.qt.io/qt-6/qabstractslider.html#value-prop)、[`Qt::ScrollBarAsNeeded`](https://doc.qt.io/qt-6/qt.html) 和样式表 [box model](https://doc.qt.io/qt-6/stylesheet-customizing.html#box-model)。

测试容差的理由：QScrollBar 使用整数值，且 Cocoa viewport 有 titlebar unobscured inset；因此功能端点允许 2px，中心归一化坐标允许 0.005，锚点稳定的二次位移允许 1px。

## TC-SB-IMAGE-EDGES

### 测试目的

验证水平和垂直滚动条在最小值/最大值时都能把图片真实左、上、右、下边缘送到可用 viewport 对应边缘，避免“滑块已经到端点但图片仍不到边”的双重 scene/range 问题。

### 前置条件

- 使用 macOS Cocoa 可见窗口，尺寸为 640×480。
- `windowresizemode=Never`、`calculatedzoommode=OriginalSize`、平滑缩放关闭。
- 打开 1600×1600 的确定性 PNG，使两个 `ScrollBarAsNeeded` 都可见并具有非零范围。
- 窗口 titlebar 的遮挡高度由被测 view 提供，不在测试中硬编码为图片位置。

### 输入数据

- 图片：`scrollbar-image-edges.png`，颜色 `darkCyan`，尺寸 1600×1600。
- 横向、纵向 scrollbar 的 `minimum()` 与 `maximum()`。
- 端点判定容差：2 个逻辑像素。

### 操作步骤

1. 显示窗口并打开临时图片。
2. 等待图片加载及两个滚动条出现，取得 `scene()->itemsBoundingRect()`。
3. 将两轴分别设置为 `minimum()`，处理事件后比较映射图片矩形与可用 viewport 的左/上边缘。
4. 将两轴分别设置为 `maximum()`，处理事件后比较映射图片矩形与可用 viewport 的右/下边缘。
5. 在同一 QtTest 中确认生产样式包含无运动方向内缩的两条 handle 规则。

### 预期结果

- 两个滚动条均可见且确有滚动范围。
- 最小值时：图片左边缘与可用 viewport 左边缘、图片上边缘与可用 viewport 上边缘的差值均不超过 2px。
- 最大值时：图片右边缘与可用 viewport 右边缘、图片下边缘与可用 viewport 下边缘的差值均不超过 2px。
- 用例通过 `QVGraphicsView::testScrollBarsReachImageEdges`，退出码为 0。

### 后置条件

关闭窗口，恢复应用 `quitOnLastWindowClosed`，临时目录由 `QTemporaryDir` 清理；不留下持久化设置。

### 固化代码

[`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp) 的 `GraphicsViewTests::testScrollBarsReachImageEdges()`。

## TC-SB-VISUAL-ENDPOINT

### 测试目的

验证滑块本身在视觉上可以贴到轨道的运动方向两端，排除 QSS margin 造成的“数值到了但看起来没到”。

### 前置条件

生产代码可调用 `QVGraphicsView::scrollBarStyleSheet(Qv::Theme::Light)`，无需创建窗口或修改用户设置。

### 输入数据

- Light theme 的完整滚动条样式字符串。
- 期望垂直规则：`margin: 0px 1px`。
- 期望水平规则：`margin: 1px 0px`。

### 操作步骤

1. 读取静态样式函数返回值。
2. 检查垂直 handle 规则与水平 handle 规则。
3. 检查旧的 `margin: 2px 1px` 与 `margin: 1px 2px` 不再出现。

### 预期结果

垂直方向上下 margin 为 0，水平方向左右 margin 为 0，侧向 1px 留白保持；用例通过 `GraphicsViewTests::testScrollBarHandleTrackEndpoints`。

### 后置条件

不创建窗口、不改变滚动值、不修改 QSettings；返回值仅在测试进程内读取。

### 固化代码

[`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp) 的 `GraphicsViewTests::testScrollBarHandleTrackEndpoints()`；运行时结构合同另由 [`tests/scrollbar_zoom_acceptance_static.py`](../tests/scrollbar_zoom_acceptance_static.py) 复核。

## TC-ZOOM-CENTER-THRESHOLD

### 测试目的

验证以可用 viewport 中心缩放跨过 `AsNeeded` 阈值时，横/纵滚动条出现及延迟布局窗口结束后，中心对应的图片归一化坐标保持稳定。

### 前置条件

- 使用可见 640×480 Cocoa 窗口、`OriginalSize`、`Never` 窗口调整模式。
- 测试图片尺寸取当前可用 viewport 的 90%，初始不溢出。
- 缩放使用一次中心锚定的 1.25 倍步骤；两轴出现后 viewport 会缩小。

### 输入数据

- 确定性 PNG：宽高为初始可用 viewport 的 90%。
- 目标：`Qv::CalculateViewportCenterPos`。
- 归一化坐标误差阈值：0.005。

### 操作步骤

1. 打开图片并确认初始没有滚动条。
2. 记录可用 viewport 中心对应的图片归一化 `(x,y)`。
3. 调用 `zoomIn()`，等待两个滚动条出现并处理布局事件。
4. 再等待 150ms，记录延迟窗口后的归一化坐标。

### 预期结果

- 两轴 `AsNeeded` 滚动条均出现。
- 滚动条出现后与初始坐标的距离不超过 0.005。
- 150ms 后与出现后坐标的距离不超过 0.005。
- 用例通过 `GraphicsViewTests::testZoomAcrossScrollbarThresholdKeepsViewportCenterStable`。

### 后置条件

关闭窗口并恢复应用退出策略；临时图片及设置保护对象自动释放。

### 固化代码

[`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp) 的 `GraphicsViewTests::testZoomAcrossScrollbarThresholdKeepsViewportCenterStable()`。

## TC-ZOOM-BOTTOM-RIGHT

### 测试目的

验证右下区域显式鼠标锚点缩放时，水平滚动条出现引起的联动 viewport resize 不会造成图片锚点瞬间跳变，且延迟窗口结束后不发生第二次跳变。

### 前置条件

- 使用可见 640×480 Cocoa 窗口、`OriginalSize`、`Never` 窗口调整模式和平滑缩放关闭。
- 打开 620×420 图片：1.0 倍时可放入 viewport，1.25 倍时出现横向溢出并使两轴滚动条可见。
- 目标点位于可用 viewport 右下方，但向内留 40px（水平）和 100px（垂直），确保不是“目标本身超过 maximum”造成的合法截断。

### 输入数据

- 确定性 PNG：620×420、颜色 `darkYellow`。
- 显式目标点：`(usableViewport.right()-40, usableViewport.bottom()-100)`。
- 缩放级别：1.25。
- 立即锚点容差：2px；稳定后二次位移容差：1px。

### 操作步骤

1. 打开图片，确认 1.0 倍时两轴滚动条隐藏。
2. 记录目标点下的 `scenePos`。
3. 调用 `zoomAbsolute(1.25, target)`。
4. 等待横向滚动条出现，确认纵向滚动条也因耦合布局出现，记录 `mapFromScene(scenePos)`。
5. 等待 150ms 并处理事件，再次记录该映射点。

### 预期结果

- 滚动条出现后的映射点与原目标的距离不超过 2px。
- 150ms 后映射点与原目标的距离仍不超过 2px。
- 两次观测之间的距离不超过 1px，即没有延迟二次位移。
- 用例通过 `GraphicsViewTests::testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar`，日志应显示本次样例的 `before=(599,407)`、`after_layout=(599,407)`、`after_settling=(599,407)`（当前 640×480 Cocoa 测试环境）。

### 后置条件

关闭窗口，恢复应用退出策略，删除临时图片；pending anchor 的 generation timer 随 view 生命周期结束。

### 固化代码

[`tests/tst_qviewtests.cpp`](../tests/tst_qviewtests.cpp) 的 `GraphicsViewTests::testZoomAtBottomRightKeepsAnchorAcrossHorizontalScrollbar()`。

## TC-STATIC-CONTRACT

### 测试目的

用源码结构静态检查确认动态用例所依赖的关键实现合同没有被回退：AsNeeded/QSS、active item scene geometry、pending anchor、resize 分支、测试 marker 和本规格六字段。

### 前置条件

- 仓库根目录可读。
- 已生成本规格文件和 `tests/scrollbar_zoom_acceptance_static.py`。
- Python 3 可执行。

### 输入数据

- 源码：`src/qvgraphicsview.cpp`、`src/qvgraphicsview.h`。
- 测试源码：`tests/tst_qviewtests.cpp`。
- 本规格文件。
- 输出文件：`reports/evidence/scrollbar_zoom_static.json`。

### 操作步骤

执行：

```bash
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output reports/evidence/scrollbar_zoom_static.json
```

读取 stdout 与 JSON 的 `passed` 及每个 `checks[].pass`。

### 预期结果

`ST-SB-01`、`ST-SB-02`、`ST-ZOOM-01`、`ST-ZOOM-02`、`ST-TEST-01`、`ST-TEST-02` 全部为 `true`，脚本返回码为 0。

### 后置条件

JSON 证据文件保留在 ignored 的 `reports/evidence/` 目录供报告溯源；源码与用户设置不变。

### 固化代码

[`tests/scrollbar_zoom_acceptance_static.py`](../tests/scrollbar_zoom_acceptance_static.py)。
