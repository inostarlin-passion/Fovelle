# 全屏准备与交接重复绘制：技术设计

日期：2026-10-01。基线：`1b57b8509d05b57e83f09125eb754bfbf00621d5`。依据：[根因报告](root_cause.md)，重点处理 R1／R3 中同步绘制成本的一个已复现分支。

## 问题界定与原子化拆解

进入／退出全屏的体验由启动准备、中段运动、终点交接、实际屏幕刷新共同决定。已有测试覆盖中段 Core Animation 轨迹与最终几何，却没有给同步布局更新建立绘制成本约束。

本轮拆成三个可验证命题：同一布局更新是否重复绘制视口；消除重复后能否在返回前完成视口绘制；原生全屏动画、标题栏、图像交接是否仍正确。无用户现场栈，不能把某次自然卡顿归为唯一原因。

## 多跳检索与交叉验证

1. 从阶段定义检索 [Apple 渲染循环](https://developer.apple.com/videos/play/tech-talks/10855/) 和 [hitches](https://developer.apple.com/documentation/xcode/understanding-hitches-in-your-app)，确认应用提交期限与渲染期限不同。前一轮中段轨迹通过不能排除同步准备延迟。
2. 沿调用链读取 `startFovelleFullScreenAnimation → handler(Update) → MainWindow::updateFullScreenLayoutTransition`：进入和退出均在准备与最终 progress 中调用该同步槽。槽先更新标题栏／fit、同步 native SDR 几何，随后视口 repaint 和父窗口 repaint。
3. 查询 [Qt 6.11.2 QWidget](https://doc.qt.io/qt-6.11/qwidget.html#repaint)，继续核对版本匹配的 [repaint manager 源码](https://github.com/qt/qtbase/blob/v6.11.2/src/widgets/kernel/qwidgetrepaintmanager.cpp) 与 [QWidget 源码](https://github.com/qt/qtbase/blob/v6.11.2/src/widgets/kernel/qwidget.cpp)：立即重绘与待合并脏区域采用不同请求时机；父窗口与子控件绘制受不透明区域影响。
4. 反查本地 `updateViewportOpacityContract`：macOS 栅格／native SDR 视口不声明不透明；静态 SVG 视口声明不透明。这使“所有图片都会重复绘制”的猜测不成立。
5. 结合 [Apple 动画性能指南](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/ImprovingAnimationPerformance/ImprovingAnimationPerformance.html)：同步绘制仍可占用主线程，属性动画不能自动消除其成本。使用实际 Paint 事件与单变量红绿实验验证本地机制。

Qt 与 Apple 为不同框架的一手来源，版本源码和运行实验补充独立证据。官方 Apple 页面仅显示 JavaScript 时，继续读取其官方 DocC JSON 正文；资料保存在 [证据目录](evidence/fullscreen_paint/)。

## 演绎与逆向证伪

非不透明视口先 repaint，随后的父窗口 repaint 再涉及该区域 → 一次同步更新两次绘制 → 内容绘制较慢时准备／交接承担重复成本。这里确认的是受控条件下的重复成本，未测得用户现场的实际绘制成本。

反例为 SVG：基线每次只绘制一次，证明不能仅按两个 API 调用推定两次 Paint。加入每次 Paint 40 ms 的相同成本后，栅格基线稳定超预算；只改变视口请求为 update，仍保留父窗口同步 repaint，四种组合均只绘制一次且满足预算。该对照排除了“移除实际绘制即可变快”的假修复。

## 实现设计

生产代码仅修改 `src/mainwindow.cpp` 的重绘请求：

```cpp
graphicsView->viewport()->update();
repaint();
```

先将视口标脏，再通过父窗口同步绘制处理合并区域。显式标脏保证不透明 SVG 也包含在本次绘制中。保留 native SDR 几何同步、fit、全屏 inset、pan preservation、源／目标／源测量、端点绘制和原生生命周期。没有将 QWidget 绘制移到非 GUI 线程。

测试新增四行真实加载 fixture：显示／隐藏标题栏 × PNG／SVG。通过视口事件过滤器观察实际 Paint，并附加固定 40 ms 成本；同步槽每行调用三次，返回前恰有一次 Paint，耗时不超过 75 ms，图像矩形与 zoom 不变。75 ms 是辨别一次和两次受控成本的回归预算，不是显示刷新期限或真实 FPS 承诺。

CTest 加入 `FovelleFullScreenPaintBudget`；系统检查脚本加入完整 12 个样本矩阵校验，缺失、重复、零绘制、双绘制和超预算均失败。原生集成由已有 motion、标题栏和 AVIF 画面测试覆盖。

## 风险与验证边界

必须保留同步 endpoint 绘制，否则真实窗口可能显示旧内容；新增测试在不泵事件的槽返回时检查绘制次数。源码变更小，但需要原生画面与几何回归。新成本测试单独调用桥接槽，不声称测量完整输入到首帧耗时；motion 测试读取呈现层近似轨迹，不声称物理屏幕逐帧测量。

快照转换、三轮布局、终点事件队列等待和系统合成等剩余候选没有被此补丁全数排除。它修复了已稳定检出的重复绘制分支。运行结果与限制见 [测试完成报告](test_completion_report.md)。
