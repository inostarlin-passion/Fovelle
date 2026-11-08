# GitHub Actions Universal 发布构建验收规格

| 原子验收标准 | 用例 |
|---|---|
| A01 生产探针的 Objective-C 布尔值可在 x86_64 和 arm64 目标上严格编译为 C++ 快照 | S01 显式类型边界，S02 同一翻译单元的双架构编译 |
| A02 已找到的可见按钮在原生属性为 `NO` 时返回 `false` | D01 `disabled` |
| A03 已找到的可见按钮在原生属性为 `YES` 时返回 `true` | D02 `enabled`，D03 真实按钮集成 |

## S01 显式类型边界（静态）

- 测试目的　检测聚合初始化重新使用未转换的原生属性。
- 前置条件　Python 可运行，工作区包含导航探针源码。
- 输入数据　`NativeNavigationArtwork` 声明和 `nativeNavigationArtwork` 实现。
- 操作步骤　执行 `navigation_button_style_static.py`，核对快照字段类型及源属性在聚合中的显式转换。
- 预期结果　字段为 `bool`，聚合将显式转换后的 `button.allowsGroupOpacity` 写入该字段。
- 后置条件　脚本只读取源码，可重复运行。

## S02 同一翻译单元的双架构编译（静态）

- 测试目的　用编译器验证原始报错所在翻译单元同时支持两种 `BOOL` 表示。
- 前置条件　已配置 macOS CMake 测试构建，生成 Ninja 编译命令，SDK 和 Qt 头文件可用。
- 输入数据　`fovelle_tests` 中的 `native_titlebar_probe.mm` 编译命令、macOS 15.0 部署目标和两个目标架构。
- 操作步骤　以该命令编译真实翻译单元，在一个编译调用中指定 x86_64 与 arm64，开启窄化错误检查，再用 `lipo` 核验目标文件架构。
- 预期结果　编译退出码为 0，目标文件包含 x86_64 与 arm64。
- 后置条件　验证目标文件写入临时目录并清理。正式发布和 SDK 15 兼容通道可用各自的正常构建入口重复验证。

## D01 原生属性关闭（动态）

- 测试目的　核对实际 `CALayer` 的关闭状态。
- 前置条件　已构建 `fovelle_tests`，macOS Cocoa 平台可运行 QtTest。
- 输入数据　临时窗口中的可见前进按钮，`allowsGroupOpacity = NO`。
- 操作步骤　运行 `testNativeNavigationGroupOpacity:disabled`，构造原生图层树并调用实际探针。
- 预期结果　快照确认按钮可见且 `groupOpacity == false`。
- 后置条件　销毁临时窗口和图层。

## D02 原生属性开启（动态）

- 测试目的　核对实际 `CALayer` 的开启状态。
- 前置条件　已构建 `fovelle_tests`，macOS Cocoa 平台可运行 QtTest。
- 输入数据　临时窗口中的可见前进按钮，`allowsGroupOpacity = YES`。
- 操作步骤　运行 `testNativeNavigationGroupOpacity:enabled`，构造原生图层树并调用实际探针。
- 预期结果　快照确认按钮可见且 `groupOpacity == true`。
- 后置条件　销毁临时窗口和图层。

## D03 真实按钮集成（动态）

- 测试目的　验证应用实际创建的上一张和下一张原生按钮均使用组透明度，探针的可见性、方向与样式断言仍成立。
- 前置条件　已构建 `fovelle_tests`，Cocoa 窗口可激活。
- 输入数据　由 `testNavigationButtonsUseDarkArtwork` 生成的黑白 PNG，左右各一组。
- 操作步骤　运行 `native-white-left` 和 `native-white-right`，分别将鼠标移向两侧边缘，等按钮淡入并检查快照。
- 预期结果　每行的两个按钮都可见，组透明度开启，不透明度、箭头方向和颜色满足原断言。
- 后置条件　关闭窗口，释放临时图片并恢复临时选项。
