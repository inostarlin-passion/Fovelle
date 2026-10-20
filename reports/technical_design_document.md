# 菜单状态与生命周期性能设计（2026-10-20）

## 问题界定与原子验收

M1：隐藏/显示标题栏后，View、快捷键动作和右键菜单显示对应的下一步操作，包含先改变状态后创建菜单。
M2：全屏、暂停/继续、幻灯片、置顶、填充窗口和固定排序策略，在菜单创建及状态变化后正确；不同窗口互不污染。
M3：遍历每个动作，核查文案、勾选、启用、可见和图标策略，不更改静态命令的语义。
P1：同环境 Release 构建，进程启动至窗口及菜单就绪的中位数至少减少 50%。
P2：同一空窗口工作负载，退出请求前标记至进程正常结束的中位数至少减少 50%。
R1：现有默认回归门禁通过，退出仍保留设置写入、任务取消及安全排空。

## 证据与推导

本地源码：setTitlebarHidden/fullscreenChanged/pauseChanged/toggleSlideshow/toggleWindowOnTop 只更新已经存在的窗口动作副本；ensureMenus 和 ensureContextMenu 只同步启用、填充和排序。addCloneOfAction 从默认动作库取值，因此状态先变、菜单后创建必然留下旧默认值。不能把状态写回全局模板，否则会污染另一个窗口。

联网两跳核查（查询后打开原始文档）：[QAction 文案、勾选及 changed 信号](https://doc.qt.io/qt-6/qaction.html)、[QMenu 生命周期与 macOS 限制](https://doc.qt.io/qt-6/qmenu.html)、[Qt 退出信号](https://doc.qt.io/qt-6/qcoreapplication.html#aboutToQuit)、[线程池排空语义](https://doc.qt.io/qt-6/qthreadpool.html)、[Apple 启动测量建议](https://developer.apple.com/documentation/xcode/reducing-your-app-s-launch-time)。文档用于确认框架契约；性能收益必须由本机测量证明，不能由文档推定。

## 设计及正确性论证

新增无状态投影函数 syncMenuState：仅把实际窗口状态投影到该窗口的所有动作副本，含标题栏、全屏、播放、幻灯片、置顶、填充和启用状态；排序菜单已移除，维持固定策略。菜单创建完成后调用；现有状态事件继续调用对应投影。提取标题栏/幻灯片/置顶投影，避免同步过程中切换业务状态。动态图标遵循主菜单无图标、右键有图标策略。

不变式：对窗口 W 的每个已创建副本 a，a 的显示状态等于 f(W)。初始时在菜单完整创建后投影建立不变式；每个状态转移后重投影保持不变式；晚建菜单重新投影恢复不变式。所有查询限定祖先 W，因此不同窗口隔离。反例测试采用状态先变、右键菜单后创建；仅测试先建菜单再切换不足以证实修复。

性能：先冻结未修改 Release 可执行文件，重复运行真实 Cocoa 进程。启动计时包含进程创建、首轮事件循环和延迟菜单创建；退出计时包含 aboutToQuit、析构及进程终止。固定 300 ms probe 等待不计入退出，也不能算作优化收益。两版本各预热一次，交替测量至少 10 次。所有样本须正常退出且具有就绪/退出标记。禁止跳过析构、减少任务安全等待或把未完成工作移动到计时终点后制造通过。

候选优化：生产窗口无需在构造自身菜单前额外物化一份无窗口应用菜单；保留显式请求时的懒创建接口。先验证兼容性与实际收益，不保证可单凭此达到 50%。若分段数据显示系统/Qt 固定成本主导，报告应明确失败，不捏造达标。

## 验证边界

只对当前机器、空窗口、隔离 INI 设置、自动更新禁用的可重复负载建立性能结论。冷系统缓存、图片解码、会话恢复及多窗口性能不能从空窗口样本外推。历史报告备份于 evidence/menu-lifecycle/previous_*。

## 第二轮性能假设

Qt 6.9+ 在窗口显示前设置 ExpandedClientAreaHint，避免首轮显示之后再改变 native style mask 并恢复 frame。关闭中的窗口无需取消 full-size content view；保留 native 窗口直至正常析构，避免关闭时额外 style/frame 更新。Qt [窗口标志契约](https://doc.qt.io/qt-6/qwidget.html#windowFlags-prop) 和 [expanded client area 设计](https://www.qt.io/blog/expanded-client-areas-and-safe-areas-in-qt-6.9) 与本地 setFullSizeContentView 实现交叉核验；本地 alterWindowFlags 直接更新 QWindow，不使用 QWidget 隐藏重显路径，因此性能收益仍需实测。兼容旧 Qt 的既有 showEvent 分支保留。

反证条件：最大化、safe-area、标题栏或原生全屏回归失败即不得保留；安全退出取消/排空边界不变。

## 菜单逐项审计结果

完整清单见 evidence/menu-lifecycle/action-audit.json，共 50 个字面动作。具有动态文案的四类为标题栏、全屏、动画暂停及幻灯片；具有窗口勾选的两类为置顶和填充。其余为固定命令文案，文件/动画/撤销/目录相关启用规则由 disableActions 同步。recent 和 Open With 的名称来自项目已有动态列表刷新路径，不属于布尔开关文案。旧排序命令不在动作库/右键菜单中，已有固定排序测试继续覆盖。

无需将窗口状态复制至全局动作模板；无窗口 fallback 的窗口命令原本禁用，生产窗口使用自身菜单。直接调用 getMenuBar 的契约保留。
