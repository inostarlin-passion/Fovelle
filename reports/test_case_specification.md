# 测试用例说明：SDR 图片全屏画面跳变/闪烁

日期：2026-09-25
样本：`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/2.png`（4616×2924 PNG）
设计依据：[技术设计文档](technical_design_document.md)

## 1. 目标与边界

覆盖“打开指定 SDR 图片并进入 macOS 全屏”时代理窗口到原生 SDR layer 的交接。用例包含一个同步调用契约断言，以及真实屏幕过渡帧观测。测试在 Cocoa GUI 会话运行；测试所需路径可由环境变量 `FOVELLE_FULLSCREEN_SDR_IMAGE` 覆盖。

| 用例 ID | 被测行为 | 级别 | 结果判定 |
| --- | --- | --- | --- |
| TC-SDR-FS-SYNC | MainWindow 全屏布局回调返回之前提交最新 SDR layer 几何。 | 集成/回归 | `compositorGeometryUpdateCount` 同步增加 1；去掉生产同步调用时必须失败。 |
| TC-SDR-FS-VISIBLE | 真实屏幕的中心纹理和图像内容在过渡期间持续可见。 | Cocoa 系统 | 每个显示器采样帧中心局部亮度方差不低于 `0.0004`，有色内容像素不少于样本像素的 `1/30`。 |
| TC-SDR-FS-CONTINUOUS | 图像可见颜色分布不能在相邻采样帧间大幅跳变。 | Cocoa 系统 | 归一化颜色质心距离不超过 `0.14`；native SDR renderer 的 `drawableGeometryMatches` 为真。 |
| TC-SDR-FS-REPEAT | 全屏进出路径可重复执行。 | 重复性 | 同一窗口完成三次完整往返，无可见性或 renderer 几何断言失败。 |

## 2. 前置条件

1. macOS Cocoa 图形会话可显示 Qt 窗口，`QScreen::grabWindow(0)` 返回有效屏幕帧。
2. 测试图片存在且可解码；当前样本文件为 18,001,858 字节，PNG RGB，4616×2924。
3. `FovelleSDRFullScreenPresentation` 是串行 CTest 项，避免多个测试窗口争用同一桌面。
4. 使用 `QT_FATAL_WARNINGS=1` 和 `QT_QPA_PLATFORM=cocoa`，保持与 Qt 测试进程的其他 macOS 用例一致。

## 3. 执行步骤

主测试为 `SDRSampleInteractionTests::testProvidedRasterFullScreenTransitionKeepsImageVisible`：

1. 设置 `ZoomToFit`，创建在屏幕中间的 1200×800 `MainWindow`，打开指定 PNG。
2. 确认文件进入 native SDR 路径，并等到显示器中心区域的图片纹理实际可见。
3. 调用全屏转场开始/更新 slot；在 Qt 事件循环推进前比较原生 compositor geometry 计数，要求它同步加 1。这是防止零毫秒 timer 延后覆盖交接的回归断言。
4. 实际切换全屏；在过渡帧中反复采样真实屏幕并核对纹理、颜色内容和 renderer 几何状态。
5. 完成进入、退出三轮，验证每一轮的采样序列均满足阈值。
6. 退出全屏、关闭窗口并恢复测试前的 quit policy。

清理由 scope guard 执行，因此断言失败时也尝试退出全屏并关闭窗口。

## 4. 反向证伪要求

该用例必须在生产同步调用被移除时失败。已执行的反向对照如下：

| 生产实现 | 几何提交计数 | 断言 | 结果 |
| --- | ---: | --- | --- |
| 移除 `MainWindow::updateFullScreenLayoutTransition()` 对 SDR 同步方法的调用 | 当前 2，期望 3 | 回调返回后立即比较计数 | FAIL |
| 恢复同步调用 | 当前 3，期望 3 | 同一 PNG、同一 QtTest 路径 | PASS |

此对照用直接读取 renderer 计数的断言证明测试区分修复前后；只检查最终 `window.isFullScreen()` 或静止图像矩形不足以替代此断言。

## 5. 阈值含义与边界

- `centerVariance >= 0.0004` 和 chromatic coverage `>= 1/30` 用来识别中心内容消失/大面积空白帧。屏幕像素已受显示器色彩管理，因此不与 PNG 原始 RGB 值逐像素相等比较。
- `centroidStep <= 0.14` 是屏幕归一化坐标中的相邻帧上限，用于发现大幅图像位移；细小移动需由 `drawableGeometryMatches` 几何契约补充。
- 屏幕采样并非 WindowServer/CALayer 每次扫描输出捕获。测试能证明采样帧和 Qt/renderer 提交边界，不声称逐刷新率无缺帧。
- 样本不可用时 CMake 不注册此专项；测试不以合成图片冒充该外部样本。

测试结果和当前环境见[测试完成报告](test_completion_report.md)。

## 附录：仓库既有缩放回归用例

以下验收 ID 由既有静态脚本检查，并在本附录用例与 `tst_qviewtests.cpp` 中追溯：

| 验收 ID | 覆盖点 |
| --- | --- |
| AC-ZOOM-NO-ANIMATION-STATIC | 缩放没有几何动画状态。 |
| AC-ZOOM-NO-ANIMATION-INPUT | 滚轮缩放同步提交。 |
| AC-ZOOM-NO-ANIMATION-SHORTCUT | 键盘快捷键缩放同步提交。 |
| AC-ZOOM-NO-ANIMATION-MENU | 菜单入口汇入共同缩放 API。 |
| AC-ANCHOR-MOUSE-PREFERRED | 优先使用有效鼠标锚点。 |
| AC-ANCHOR-PROJECT-FEASIBLE | 投影到目标可行区间。 |
| AC-ANCHOR-NO-POST-CORRECTION | 无缩放后延迟位置修正。 |
| AC-ANCHOR-HBAR-TOPOLOGY | 横向滚动条拓扑变化时保持锚点。 |
| AC-VBAR-TOPOLOGY-ANCHOR | 纵向滚动条拓扑变化时保持锚点。 |

### TC-ZOOM-SYNC-ALL-ENTRY-POINTS

- 测试目的：验证 wheel、键盘、标题栏菜单与右键菜单缩放都同步提交。
- 前置条件：可见 Qt 图像窗口已打开栅格图片。
- 输入数据：wheel 往返、缩放快捷键与两类 View 菜单 action。
- 操作步骤：依次触发各入口并检查 zoom、scene、scrollbar 终态。
- 预期结果：所有入口共享立即提交路径，静默观察期无延迟几何写入。
- 后置条件：恢复窗口与应用 quit policy。

### TC-ANCHOR-FEASIBLE-PROJECTION

- 测试目的：验证目标图片尺寸改变后缩放锚点仍处于可行位置。
- 前置条件：图像视口和纯锚点投影 helper 可用。
- 输入数据：图片内部/外部位置、放大/缩小目标和可行原点区间。
- 操作步骤：运行纯函数边界断言，再由真实缩放入口检查映射点。
- 预期结果：锚点被投影到最近可行位置，不制造可避免的空白。
- 后置条件：测试视口关闭，临时设置恢复。

### TC-HBAR-FOUR-IN-ONE-OUT

- 测试目的：验证四步放大和一步回退穿越横向滚动条显示边界时不移位。
- 前置条件：真实或确定性合成图片可加载，窗口可 resize。
- 输入数据：四次前向缩放及一次反向缩放。
- 操作步骤：采集滚动条 range/value、viewport 和最终图片锚点。
- 预期结果：range 按预期出现/消失，图片锚点保持在允许误差内。
- 后置条件：图片与测试窗口关闭。

### TC-VBAR-TOPOLOGY-ANCHOR

- 测试目的：验证纵向 range 首次出现时已提交的场景锚点不被后续布局覆盖。
- 前置条件：900×400 专用 viewport 和足够尺寸的测试图片。
- 输入数据：横向已有 range、纵向无 range 的起始状态及一次缩放。
- 操作步骤：比较提交态、range 收敛态和 paint probe 中的同一场景点。
- 预期结果：纵向锚点保持在一 DIP 内，横向值不被连带改写。
- 后置条件：测试窗口与临时图片释放。
