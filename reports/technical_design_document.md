# GitHub Actions 的 About 更新检查修复设计

## 问题界定与交叉核验

| 检索路径 | 证据 |
|---|---|
| 远端提交 → `Build Fovelle` → `Test` | `015dbbd` 的格式检查和构建成功。七个 CTest 入口中只有 `FovelleAboutUpdate` 失败。日志显示设置用例结束后，进度窗口用例收到 SIGSEGV。[失败运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/42475098087) |
| 本机崩溃堆栈 → `ActionManager::updateRecentsMenu` → `MainWindow` 的关闭和析构 | 本机同类崩溃发生在刷新最近文件项时调用 `QAction::setVisible`。设置用例用栈上的 `MainWindow`，离开作用域直接析构。窗口注册表和菜单副本只在 `closeEvent` 清除，析构路径留下裸指针。远端日志缺少堆栈，因此由定向回归用例核验这条因果链。 |
| 前一次失败 → `canCancelAndRetry` → AppKit 事件及回环端点 | `e3e8998` 的进度窗口保持可见。用例在控件可见后就投递鼠标事件，端点也没有回报连接何时开始。Sparkle 先展示进度再开始 appcast 请求，两个信号分别代表不同阶段。可见性也早于 WindowServer 完成排序。[前一次运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/42365504806) · [Sparkle 2.10.0 发起检查流程](https://github.com/sparkle-project/Sparkle/blob/2.10.0/Sparkle/SPUUserInitiatedUpdateDriver.m) |
| 取消按钮 → Sparkle 标准驱动 → 可再次检查 | 原生按钮动作调用取消回调并关闭检查进度。进度消失后还要等待更新器恢复可检查状态，才能证明同一 About 可开启下一轮检查。[Sparkle 2.10.0 标准用户驱动](https://github.com/sparkle-project/Sparkle/blob/2.10.0/Sparkle/SPUStandardUserDriver.m) |

## 方案与正确性

1. 抽出窗口注销步骤，由正常关闭和析构共同调用。在 Qt 销毁窗口子对象前，从活动窗口集合移除窗口，并解除上下文菜单、菜单栏和虚拟菜单的副本注册。按键和值移除条目，重复注销同一窗口是幂等的。正常关闭仍在原位置保存几何和会话数据。析构遇到应用已进入 Qt 基类清理时，应用注册表已退出使用，由应用实例检查避开访问。
2. About 测试端点接受本地 TLS 连接，读到握手记录后追加到本轮专用计数文件，并将连接保持到测试结束。每轮从当前计数取基线。新增握手、应用激活、目标按钮启用以及进度窗口已在 WindowServer 中排序都成立后，才投递 AppKit 的鼠标按下和释放。About 可见时，验证进度在其上方。关闭 About 后，验证独立进度窗口仍可接收鼠标输入。
3. 在窗口仍有有效子对象时解除注册，任何随后查找都只会返回其他存活窗口的副本。已关闭再析构时，第二次移除也只匹配该窗口自己的地址。直接销毁未显示窗口同样解除菜单副本，因为菜单在构造时创建。
4. 端点只确认握手并保持连接，进度无法因收到 appcast 而结束。鼠标输入仍经过 AppKit。每次断言进度关闭、更新器空闲及 About 按钮重新启用。重复检查需要新的握手，因此第一次检查的计数无法满足第二次。现有 CTest 入口继续调用原生用例，并保留更新安装回归。
