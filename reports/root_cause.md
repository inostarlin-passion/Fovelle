# SDR/HDR 退出全屏底部闪烁：根因分析

## 问题界定与原子拆解

本轮以 `69cec025a4b75060bc61cd74b904cade27ecb024` 为基线，只处理“打开 SDR 或 HDR 图片，退出全屏时屏幕底部闪烁”。此前几何修复报告归档于 `evidence/fullscreen_bottom_flash/prior_reports/`。

拆解为：图片路径（SDR/HDR）、恢复状态（普通/最大化）、退出阶段（请求、WillExit、resize、DidExit、尾部显示）、区域（应用内部、窗口圆角、桌面/Dock）、呈现层（Qt backing store、原生图像层、NSWindow 背景、实际显示像素）。正常窗口缩小、移动与 Dock 出现不算应用闪烁。

## 已验证的前提

1. 本机为 macOS 27.0.1、arm64、Qt 6.11.2 Cocoa，主屏逻辑分辨率 1728×1117，Retina 2 倍。HDR 使用真实 `3.dng`，必须成功识别 HDR 且持久表面准备完成。
2. 深色画布为 `#212121`。基线的 `setWindowTheme()` 只设置 appearance，未设置 NSWindow 背景；SDR/HDR 的原生窗口背景实测均为 `#ffffff`。
3. `WA_OpaquePaintEvent`、Qt 画布颜色及原生背景图层颜色不是 NSWindow 背景的赋值接口。
4. `QScreen::grabWindow()` 在本版 Cocoa 实现中读取显示屏像素；离屏绘制 CGImage 或比较图层模型几何不能代替它。抓取仍不是每次屏幕扫描的无损录像。
5. AppKit 动画中的实际快照位置与 Qt 已恢复的目标窗口坐标不同。移动图像经过采样区和窗口圆角的亮像素都不能直接判为闪烁。

## 多跳联网检索与多源交叉验证

| 检索跳 | 一手来源 | 与本地验证的联系 |
|---|---|---|
| 1：qView 参考行为 | [qView MainWindow](https://raw.githubusercontent.com/jurplel/qView/master/src/mainwindow.cpp) | toggleFullScreen 短暂禁用 QWidget 更新并恢复保存状态；本项目已有对应保护。该保护不等于原生背景已同步。该链接为移动分支参考，不能声称其当前提交固定。 |
| 2：底层窗口颜色 | [Apple NSWindow.backgroundColor](https://developer.apple.com/documentation/appkit/nswindow/backgroundcolor)、[Qt 6.11.2 QNSWindow](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/plugins/platforms/cocoa/qnswindow.mm) | Apple 定义独立窗口背景；Qt 的 backgroundColor getter 对普通有边框窗口返回 superclass 背景。本地独立 Objective-C 探针读出白色。 |
| 3：生命周期与取样语义 | [Qt Cocoa 窗口](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/plugins/platforms/cocoa/qcocoawindow.mm)、[Qt Cocoa 屏幕捕获](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/plugins/platforms/cocoa/qcocoascreen.mm) | 原生 Will/Did 与 Qt 状态分开；grabWindow 使用显示捕获。测试据此等待原生完成并读取真实显示。 |
| 4：系统动画边界 | [Apple 全屏退出委托](https://developer.apple.com/documentation/appkit/nswindowdelegate/customwindowstoexitfullscreen%28for%3A%29) | 原生全屏退出涉及系统动画；没有证据支持替换为自定义动画或把 Dock 动作归罪于图片渲染。 |

这些来源相互支持“背景独立”“生命周期异步”“模型不等于显示”三个前提，并不能单凭文档证明用户机器上的自然闪烁原因。

## 分路径可能根因与演绎

| 条件 | 可能根因 | 已验证部分 | 尚未验证部分 |
|---|---|---|---|
| SDR 退出 | 内容覆盖交接时暂露白色 NSWindow 背景；Qt opaque 绘制覆盖不足或提交延迟可能触发暴露 | 白色背景与深灰画布不一致；受控暴露后底部 3840/3840 显示像素错误 | 自然退出是否出现同样的覆盖空隙 |
| HDR 退出 | 同一原生窗口兜底色差；另有持久 HDR/Metal 内容交接延迟的可能 | 真实 HDR 已准备；受控暴露出现相同白色条带 | HDR 自然交接是否造成暴露，或是否还有独立亮度/合成问题 |
| 两者共同 | Qt backing store 的不完整底部绘制，Core Animation presentation 与模型不同步，或者系统 Space/Dock 合成异常 | 前两轮模型几何测试通过不能排除这些问题 | 本轮没有稳定捕获自然异常帧，不能认定它们为确定根因 |

推导：若某一显示帧缺少内容覆盖，则该区域可能显示 NSWindow 背景；基线背景白色、画布深灰，RGB 色差为 222；覆盖恢复时就可能产生明暗跳变。受控隐藏 contentView 只验证“暴露背景会产生色差”这一后半链条，不能反推自然退出必然隐藏或丢失内容。

## 逆向证伪、检索收敛与测试遗漏

- 自然普通/最大化退出的四组底部采样，中央三分之一区域未发现高亮白条（分析见 `natural-red-analysis.json`）。红色图像短暂经过目标 ROI 是系统快照运动；少量亮角是稳定窗口圆角。拒绝将二者算作复现。
- 第一版全屏 PNG 保存阻塞约 200–360ms，改为内存中的窄带取样、完成后保存；仍有抓取开销与漏帧风险，故自然采样仅作观察。
- 使用独立 NSWindow 读色探针与实际显示取样，排除“测试只复述生产方法返回值”的假通过。
- 原测试检查最终状态、模型几何和离屏底部颜色，内容层始终参与，根本不会显露独立 NSWindow 背景；因此即使背景错误也全部通过。
- 新增受控交接测试在生产代码未改时连续两轮四组全失败；最终红测四组和浅色主题共五项失败。修复统一窗口背景后同一像素断言通过；最终版测试负向移除赋值仍四行及主题全部失败（`negative-final-tests.txt`），排除异步等待造成假通过。

检索在可执行机制层已收敛：发现并修复确定的背景不一致。用户原始“自然退出闪烁”尚未稳定复现，未形成完整因果证明。仍需用户实际显示配置/区域信息或能捕获原始异常帧的复现条件；本轮不能宣告整个视觉缺陷已完成验收。
