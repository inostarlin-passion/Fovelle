# 测试用例说明：放大 AVIF 退出全屏时的跳变/闪烁

日期：2026-09-25
样本：`/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif`（AVIF，1200×1085）
技术依据：[技术设计文档](technical_design_document.md)

## 1. 测试目标

现有 SDR 全屏专项原来使用 `2.png`，并未按报告中的顺序放大图片；也没有检查全屏退出 `Cancel` 收尾回调返回前的 SDR layer 提交。本用例改用指定 AVIF、实际 2 倍缩放，并分别验证交接提交、画面连续性、缩放状态和退出锚点。

## 2. 原子用例

主执行项：`SDRSampleInteractionTests::testProvidedRasterFullScreenTransitionKeepsImageVisible`
CTest 名称：`FovelleSDRFullScreenPresentation`

| 用例 ID | 级别 | 操作 | 通过条件 |
| --- | --- | --- | --- |
| TC-AVIF-LOAD-ZOOM | Cocoa 集成 | 打开 `1.avif`，等待 native SDR renderer 就绪，执行 `zoomAbsolute(2.0, viewportCenter)`。 | 文件加载、native SDR renderer 有效，最终 zoom 与 2.0 等价，drawable 几何匹配。 |
| TC-FS-UPDATE-SYNC | 集成契约 | 调用全屏布局更新回调。 | `compositorGeometryUpdateCount` 同步增加 1。 |
| TC-FS-CANCEL-SYNC | 集成回归 | 调用最终 `cancelFullScreenLayoutTransition()` 收尾。 | 收尾回调返回前计数再增加 1，且 `drawableGeometryMatches` 为真。 |
| TC-FS-VISIBLE | Cocoa 系统 | 真实屏幕进出动画期间以 8ms 间隔采样。 | 每一帧中心局部亮度方差至少 `0.0004`，彩色内容像素至少达到抽样像素的 `1/30`。 |
| TC-FS-CONTINUOUS | Cocoa 系统 | 比较相邻屏幕采样中的归一化有色像素质心。 | 单步距离不超过 `0.14`；发现 renderer 几何不匹配即失败。 |
| TC-FS-ANCHOR-ZOOM | 集成回归 | 每轮全屏退出前后比较 usable viewport center 对应的 scene point。 | zoom 仍为 2.0，scene point 距离不超过 2 个图像坐标单位。 |
| TC-FS-ROUNDTRIP | 重复性 | 对真实窗口执行三次进入/退出。 | 三轮均完成且上面各项通过。 |

## 3. 前置条件与可复现步骤

1. macOS Cocoa 桌面会话可用，`QScreen::grabWindow(0)` 可读取屏幕。
2. 样本路径存在并能由应用 native SDR 路径解码。CMake 默认路径为本机给定卷；其他机器可用 `-DFOVELLE_FULLSCREEN_AVIF_IMAGE=/绝对路径/1.avif` 指定。测试进程也接受 `FOVELLE_FULLSCREEN_SDR_IMAGE` 环境变量覆盖。
3. 测试窗口居中显示，设置 fit、无平滑插值、无棋盘背景和 1:1 pixel size 后加载样本。
4. 等待 native SDR Metal renderer 启动，执行 2 倍 zoom 并等待 drawable geometry 对齐。
5. 直接调用一次布局更新及一次取消收尾回调，逐次核对 compositor 几何提交计数；此段让最后交接的同步契约可确定性复现，不依赖 AppKit 动画计时。
6. 经真实 `toggleFullScreen()` 进入全屏，逐帧采样；保留 2 倍缩放并记录全屏 usable viewport center 对应 scene point。
7. 经真实 `toggleFullScreen()` 退出全屏，逐帧采样；检查 2 倍 zoom 仍存在，并确认退出后 usable viewport center 对应原 scene point。
8. 连续执行第 6–7 步三轮。scope guard 在断言失败时也会尝试离开全屏、关闭窗口并恢复应用 quit policy。

## 4. 逆向证伪

把生产代码 `MainWindow::cancelFullScreenLayoutTransition()` 中新增的同步调用暂时移除，重建后执行同一 CTest：

| 版本 | 收尾前实际计数 | 期望计数 | 结果 |
| --- | ---: | ---: | --- |
| 移除退出收尾同步 | 4 | 5 | FAIL，`TC-FS-CANCEL-SYNC` 精确失败 |
| 恢复退出收尾同步 | 5 | 5 | PASS |

该断言在测试真实三轮动画前执行，直接锁定新增生产行为。真实动画检查补充验证画面内容、帧间跳变和最终几何状态。

## 5. 阈值和解释

- 屏幕图像先缩至最长边不超过 360×240 的探测图；中心 16×16 邻域的亮度方差用于排除中心只剩纯色/空白的帧。
- 对探测图隔点扫描；亮度至少 48 且 RGB 最大/最小通道差至少 42 的像素计为可见彩色内容，阈值为扫描像素数的 1/30。
- 有色像素质心以探测图宽高归一化，邻帧欧氏距离最大 0.14。该门槛检测大幅跳变，不声称检测每个子像素变化。
- 退出锚点必须在 usable viewport 测量：`QVGraphicsView` 会把 `MainWindow::getViewportPosition().obscuredHeight` 以上部分视为标题栏遮挡区。直接使用整个 viewport 的几何中心会把被遮挡区域纳入比较，产生错误失败。

## 6. 当前覆盖边界

该测试是依赖显示器的 macOS 集成测试。低于屏幕采样频率的短闪帧可能落在两次截图之间；若需证明每次显示刷新均无闪烁，仍缺少 WindowServer/Core Animation 逐呈现帧捕获信源。本用例不覆盖非 SDR 文件和其他平台。
