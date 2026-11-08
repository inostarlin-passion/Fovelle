# GitHub Actions Universal 发布构建修复设计

## 问题界定与证据

| 检索路径 | 核验结果 |
|---|---|
| 标签与提交 → 主 CI → 发布工作流 | `v1.3.7` 与 `main` 指向同一提交 `73b536f`。主 CI 的格式和 arm64 构建、测试全部通过。发布工作流复用的主 CI 也通过，之后的独立发布 job 在 Build 阶段失败。[主 CI](https://github.com/inostarlin-passion/Fovelle/actions/runs/42479746137) · [发布 job](https://github.com/inostarlin-passion/Fovelle/actions/runs/42481278343/job/128736201193) |
| Build 日志 → 编译命令 → 源码 | 发布命令使用 Xcode 16.4、SDK 15.5、C++17、`-arch x86_64 -arch arm64`。编译 `tests/native_titlebar_probe.mm` 时，Clang 报出运行时 `BOOL`（`signed char`）到 `bool` 的列表初始化窄化错误。报错行把 `CALayer.allowsGroupOpacity` 直接填入 `NativeNavigationArtwork::groupOpacity`。 |
| SDK 声明 → 目标架构 → 差异解释 | QuartzCore 声明该属性为 `BOOL`。本机 SDK 对同一 macOS 15 部署目标预处理，x86_64 得到 `OBJC_BOOL_IS_BOOL=0`，arm64 得到 `OBJC_BOOL_IS_BOOL=1`。这与 Universal 发布失败而 arm64 CI 通过一致。[QuartzCore 属性](https://developer.apple.com/documentation/quartzcore/calayer/allowsgroupopacity) · [Clang 窄化诊断](https://clang.llvm.org/docs/DiagnosticsReference.html#wc-11-narrowing) |

## 变更

在 `nativeNavigationArtwork` 的聚合返回值中使用 `static_cast<bool>(button.allowsGroupOpacity)`。Objective-C 属性只读取一次，转换发生在原生探针的 C++ 快照边界。发布、SDK 15 兼容通道及应用正常使用原有入口，继续构建和运行原有门禁。

将静态断言放入默认快速测试已注册的导航样式脚本。新增数据驱动 QtTest，使用临时 `QWindow` 和真实 `CALayer` 层级给探针分别输入 `NO` 与 `YES`。沿用已有原生按钮集成测试核验实际应用的两个按钮。

## 正确性证明

1. 在 x86_64 上，显式转换接受整型 `BOOL` 并产生 `bool`。在 arm64 上，`BOOL` 已是 `bool`，结果类型同样是 `bool`。聚合字段接收同类型表达式，因此该初始化在两个目标上均没有窄化转换。
2. C++ 到布尔类型的转换将零映射为 `false`，将非零映射为 `true`。因此原生属性为 `NO` 或 `YES` 时，快照分别返回 `false` 或 `true`，包括原先已经能编译的 arm64 通道。[C++ 布尔转换规则](https://eel.is/c++draft/conv.bool)
3. 修改只发生在返回结构的第二个表达式。按钮筛选、其余字段和找不到按钮时的默认快照沿用同一路径。静态检查会阻止移除该边界转换，动态测试会发现常量替代或反转语义，实际按钮集成测试会继续检查样式及方向。
