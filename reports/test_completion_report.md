# 悬浮导航按钮统一样式测试完成报告

2026-11-08 在 macOS 27.0.1（Apple Silicon）、Qt 6.12.0 的本地 Cocoa 会话中验证。三条原子标准的三项静态用例及五个动态用例或数据行全部通过。

| 原子标准 | 静态结果 | 动态结果 |
|---|---|---|
| A01 Qt 控件统一样式 | S01 通过。旧的样式选择及专用取样已移除。 | D01 两行通过。在明暗背景互换后，两个按钮都绘制灰色底板和方向正确的深灰箭头。 |
| A02 原生图层统一样式 | S02 通过。原生 API 无样式参数，底板和箭头固定使用原深色背景款。 | D02 两行通过。原生覆盖层的填色、描边、方向及组透明度符合预期，Qt 控件处于隐藏状态。 |
| A03 整体淡化与透明外缘 | S03 通过。Qt 在合成后应用透明度，原生路径在父图层应用透明度。 | D03 通过。两个按钮四角透明，按下态底板完整 alpha 为 235，半透明帧的每个像素均在允许误差内。 |

## 回归结果

- 旧实现在静态基线中未通过 S01 和 S02。动态基线观察到浅色区域的 Qt 按钮没有底板，原生底板不可见，按下态 Qt 底板取样 alpha 为 0。
- 首轮完整测试中，排队的 Cocoa 鼠标进入或离开事件打断了模拟悬浮。用例改为打开目录中间的图像并在等待淡入时维持模拟指针位置。修正后，按默认套件的窗口用例顺序连续运行五轮，每轮的 14 项 QtTest 记录均通过，包含初始化和清理。
- 导航专项的 12 项断言用例或数据行通过，另含 QtTest 初始化与清理。既有回归覆盖边缘命中、尺寸、淡入淡出时长、点击切图及文件边界的按钮可用性。
- 完整 CTest 重跑 7 组全部通过，耗时 118.37 秒。首轮的 About 用例曾在 `ActionManager::updateRecentsMenu` 调用 `QAction::setVisible` 时崩溃，重跑时该用例通过。
- 应用与测试目标构建成功。clang-format 23.1.3 和 `git diff --check` 均通过。本机构建的链接器提示 QtSvg 依赖为 macOS 27.0 构建。运行验证以本地 macOS 27.0.1 为准。

D02 使用进入同一原生覆盖层的 PNG，直接读取 Core Animation 图层的颜色、路径和透明度。测试范围是按钮绘制属性。实际 HDR 屏幕亮度不属于本次测量。

## 复跑

```sh
python3 tests/navigation_button_style_static.py
QT_QPA_PLATFORM=cocoa FOVELLE_TEST_SUITE=WindowBehaviorTests \
  FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build-about-update/tests/fovelle_tests \
  testNavigationButtonsUseDarkArtwork testNavigationButtonUsesTransparentPaintOnlyFade
ctest --test-dir build-about-update --output-on-failure
```

本地证据保存在 `build-about-update`。旧版基线见 `navigation-red-static.log` 和 `navigation-red-dynamic.log`，专项与重复运行见 `navigation-focused.log` 和 `navigation-repeat-1.log` 至 `navigation-repeat-5.log`，完整 CTest 的首轮及重跑见 `navigation-ctest.log` 和 `navigation-ctest-final.log`。
