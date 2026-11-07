# About 模态与更新进度原子验收规格

| 原子标准 | 静态测试 | 动态测试 |
|---|---|---|
| A01 About 显示时保持应用模态 | S01 `test_about_stays_application_modal` | D01 `opensApplicationModal` |
| A02 About 可见期间设置打不开，关闭后可打开 | S02 `test_preferences_respect_visible_about` | D02 `blocksPreferencesWhileOpen` |
| A03 检查进度框显示在重叠的 About 上方 | S03 `test_manual_entries_use_standard_ui` | D03 `showsProgressInFront` |
| A04 真实原生取消输入生效，可在同一 About 重试 | S04 `test_about_button_tracks_update_state` | D04 `canCancelAndRetry` |
| A05 关闭 About 后检查进度仍可操作 | S05 `test_closing_about_detaches_update_windows` | D05 `closingAboutKeepsProgressUsable` |

静态用例位于 `tests/update_acceptance.py`，动态用例位于 `tests/about_update_tests.mm`。动态用例在已登录的 macOS Cocoa 会话中运行，使用 `tests/run_about_update_test.py` 创建的临时应用包和延迟 TLS 回应的回环端点。

## A01 应用模态

| 要素 | S01 | D01 |
|---|---|---|
| 测试目的 | 核对模态设置的位置 | 测量原生模态状态 |
| 前置条件 | About 源码可读 | Cocoa 测试进程已启动 |
| 输入数据 | 构造函数和更新按钮槽 | 通过应用入口打开的 About |
| 操作步骤 | 检查构造时设置应用模态，点击槽无模态变更 | 显示 About 并读取 Qt 和 AppKit 当前模态窗口 |
| 预期结果 | 模态在显示前设置且点击时保持 | About 是 Qt 和 AppKit 的当前模态窗口 |
| 后置条件 | 源码无变动 | 关闭 About |

## A02 设置入口

| 要素 | S02 | D02 |
|---|---|---|
| 测试目的 | 核对设置入口的可见性条件 | 验证设置不会盖住 About |
| 前置条件 | 应用源码可读 | 真实主窗口与原生菜单可用 |
| 输入数据 | 设置入口实现 | 原生设置快捷键及活动 About |
| 操作步骤 | 检查可见 About 的守卫位于设置窗口创建与激活之前 | About 可见时触发设置快捷键和应用设置入口，关闭 About 后再次触发原生快捷键 |
| 预期结果 | 可见时返回，其余状态继续执行 | 可见时设置未打开，About 持续显示。关闭后设置打开 |
| 后置条件 | 源码无变动 | 关闭测试设置窗口和主窗口 |

## A03 进度框前置

| 要素 | S03 | D03 |
|---|---|---|
| 测试目的 | 核对标准驱动和原生子窗口的连线 | 检查真实窗口顺序与父子关系 |
| 前置条件 | 更新桥接源码可读 | 隔离 Sparkle 已启动，About 居中 |
| 输入数据 | About 与菜单按钮入口、标准驱动及挂接方法 | About 按钮和真实 Sparkle 状态窗口 |
| 操作步骤 | 检查两入口均调用 Sparkle，标准进度回调展示后挂接到 About | 点击 About 按钮，确认窗口重叠并读取 WindowServer 顺序、AppKit 父窗口和当前模态窗口 |
| 预期结果 | 仅将 Sparkle 管理的窗口作为 About 子窗口 | 进度框在前且父窗口为 About，About 仍为模态 |
| 后置条件 | 源码无变动 | 取消检查并关闭 About |

## A04 原生取消与重试

| 要素 | S04 | D04 |
|---|---|---|
| 测试目的 | 核对 Sparkle 状态通知传播 | 验证原生输入能穿过模态会话到达进度框 |
| 前置条件 | About 与更新状态源码可读 | 原生进度框有启用的取消按钮 |
| 输入数据 | `canCheckForUpdates` 和按钮启用表达式 | 两次检查和两组 AppKit 鼠标按下与释放事件 |
| 操作步骤 | 核对状态观察、信号连接及按钮更新 | 点击检查，向原生取消按钮投递鼠标事件，等待结束后重复一轮 |
| 预期结果 | 按钮跟随更新器能否接受命令 | 两次进度均关闭，同一个 About 继续显示且按钮可用 |
| 后置条件 | 源码无变动 | 关闭 About 并清理会话 |

## A05 关闭 About 时解除关系

| 要素 | S05 | D05 |
|---|---|---|
| 测试目的 | 核对解除关系与父窗口关闭的顺序 | 验证独立的更新会话继续可用 |
| 前置条件 | About 和原生桥接源码可读 | 已从 About 启动检查 |
| 输入数据 | About 结束入口和子窗口清理方法 | 活动的 About 及 Sparkle 进度窗口 |
| 操作步骤 | 检查关闭前移除 Sparkle 子窗口 | 关闭 About，检查进度仍可见且无父窗口，投递原生取消点击 |
| 预期结果 | 父窗口关闭前已解除关系 | About 销毁后进度仍可见，取消完成 |
| 后置条件 | 源码无变动 | 进程退出并清理临时包 |
