# 保留 About 模态的更新进度框修复设计

## 问题界定与证据

About 有两个独立的交互要求。它可见时，其他窗口不能抢走交互，尤其是设置。它发起检查后，更新进度框应在其上方，且「取消」可点击。

| 证据 | 设计约束 |
|---|---|
| 旧实现通过 `Qt::ApplicationModal` 显示 About，然后在点击时才改为 `NonModal`。 | Qt 在显示时建立模态会话，事后改属性无法解除。上轮将 About 永久改成非模态，修正了层叠，却使设置可以打开。[Qt QWidget 模态属性](https://doc.qt.io/qt-6/qwidget.html#windowModality-prop) |
| Sparkle 标准驱动使用独立的 AppKit 窗口显示检查进度，标准控制器没有设置父窗口的接口。 | AppKit 模态会话会过滤其他窗口的输入。原生探针在模态 About 上分别提高普通 `NSWindow` 的层级和建立父子关系。前者的取消按钮收不到点击，后者能接收点击且 About 仍为模态窗口。[AppKit 模态会话](https://developer.apple.com/documentation/appkit/nsapplication/beginmodalsession%28for%3A%29) · [AppKit 子窗口顺序](https://developer.apple.com/documentation/appkit/nswindow/addchildwindow%28_%3Aordered%3A%29) |
| Sparkle 允许给 `SPUUpdater` 注入 UI 驱动。其标准驱动通过同步展示方法创建检查、更新提议和下载等窗口，错误及无更新提示使用自己的原生模态会话。 | 复用标准驱动，在呈现后用 AppKit 公开的窗口控制器和 framework 身份限定挂接对象。原生模态提示继续由 Sparkle 管理。[Sparkle 用户驱动 API](https://sparkle-project.org/documentation/api-reference/Protocols/SPUUserDriver.html) · [Sparkle 2.10.0 标准驱动实现](https://github.com/sparkle-project/Sparkle/blob/2.10.0/Sparkle/SPUStandardUserDriver.m) |

## 实现与正确性

1. 恢复 About 构造时的 `Qt::ApplicationModal`。检查更新槽直接调用更新器。设置入口对可见的 About 提前返回，也覆盖 Qt 在应用仅剩一个模态窗口时仍允许菜单项的情形。About 关闭后重新允许设置。
2. 用 `SPUUpdater` 配合继承标准用户驱动的小型子类。子类调用标准实现，在活动模态窗口为 About 时，将可见且由 Sparkle framework 自己的控制器管理的窗口挂接到 About 上方。这个判断不依赖本地化标题或 Sparkle 私有类名。无模态 About 时，调用标准实现即可。窗口创建和重新聚焦沿用 Sparkle 的公开驱动回调。
3. Qt 和 AppKit 的 About 模态会话持续存在，保持对其他应用窗口的输入约束。AppKit 子窗口关系确保进度在 About 之上，且实际鼠标事件可到达子窗口。取消直接走标准 Sparkle 控件和状态回调，About 在同一模态会话中继续可用。
4. About 关闭时，在父窗口退场前移除 Sparkle 子窗口关系。AppKit 原生探针表明，直接关闭父窗口会把仍在运行的子窗口一起隐藏。提前解除关系可保持更新进度显示，并让 Sparkle 继续管理该会话。
