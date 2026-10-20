# 原子验收与测试用例规格（2026-10-20）

每条验收独立列出；数据驱动测试可在同一函数内覆盖多个原子验收，但每条均有明确断言。静态审查约束与真实 Cocoa 动态测试互补；性能失败保留原始证据。

## TC-M1a：延迟主菜单标题栏文案

- 验收标准：延迟主菜单标题栏文案。
- 类型：动态回归。
- 测试代码：`testLateMenuState`。

1. 测试目的：验证延迟主菜单标题栏文案。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：hidden=false/true × deferMenus=false/true。
4. 操作步骤：显示窗口；改变标题栏；发送 F13 ShortcutOverride 物化主菜单/虚拟菜单；再反向切换。
5. 预期结果：隐藏时 Show Title&bar，显示时 Hide Title&bar，每个所属窗口副本一致。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M1b：延迟右键菜单标题栏文案

- 验收标准：延迟右键菜单标题栏文案。
- 类型：动态回归。
- 测试代码：`testLateMenuState`。

1. 测试目的：验证延迟右键菜单标题栏文案。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：同 M1a 四行数据。
4. 操作步骤：先隐藏/显示标题栏，再首次调用真实右键菜单构建入口；切换回来。
5. 预期结果：首次右键菜单及后续切换均反映下一步操作。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M1c：新窗口继承标题栏隐藏偏好

- 验收标准：新窗口继承标题栏隐藏偏好。
- 类型：动态回归。
- 测试代码：`testTitlebarHiddenPersistsToNewWindow`。

1. 测试目的：验证新窗口继承标题栏隐藏偏好。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：持久化 titlebarhidden=true。
4. 操作步骤：第一窗口隐藏标题栏；从 QVApplication::newWindow 创建第二窗口。
5. 预期结果：第二窗口原生标题栏隐藏；偏好保留。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M2a：全屏菜单文案

- 验收标准：全屏菜单文案。
- 类型：动态回归。
- 测试代码：`testLateFullscreenMenu`。

1. 测试目的：验证全屏菜单文案。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：正常窗口→全屏→正常。
4. 操作步骤：先进入原生全屏，再首次创建右键菜单；读取主菜单/虚拟/右键动作；退出。
5. 预期结果：全屏 Exit F&ull Screen；退出后 Enter F&ull Screen。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M2b：暂停/继续菜单文案和启用

- 验收标准：暂停/继续菜单文案和启用。
- 类型：动态回归。
- 测试代码：`testLatePauseMenu`。

1. 测试目的：验证暂停/继续菜单文案和启用。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：内嵌两帧 APNG。
4. 操作步骤：真实加载动画；暂停；首次创建右键菜单；恢复。
5. 预期结果：所有 pause 副本先 Res&ume 后 Pa&use，加载动画时启用。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M2c：幻灯片菜单文案

- 验收标准：幻灯片菜单文案。
- 类型：动态回归。
- 测试代码：`testLateMenuState`。

1. 测试目的：验证幻灯片菜单文案。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：3600 秒定时器，开始/停止。
4. 操作步骤：先启动幻灯片，再构建菜单；cancelSlideshow。
5. 预期结果：运行 Stop S&lideshow；停止 Start S&lideshow。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M2d：置顶勾选

- 验收标准：置顶勾选。
- 类型：动态回归。
- 测试代码：`testLateMenuState`。

1. 测试目的：验证置顶勾选。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：窗口置顶 true/false。
4. 操作步骤：先置顶再构建菜单；反向切换。
5. 预期结果：所有置顶副本勾选值等于实际置顶状态。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M2e：填充窗口勾选

- 验收标准：填充窗口勾选。
- 类型：动态回归。
- 测试代码：`testLateMenuState`。

1. 测试目的：验证填充窗口勾选。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：FillWindow true/false。
4. 操作步骤：先填充再构建菜单；反向切换。
5. 预期结果：所有 fillwindow 副本同步勾选。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M2f：已移除排序菜单不复活

- 验收标准：已移除排序菜单不复活。
- 类型：静态与动态。
- 测试代码：`testSortConfigurationIsIgnoredAndContextMenuHasNoSortMenu / menu_lifecycle_static.py`。

1. 测试目的：验证已移除排序菜单不复活。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：旧排序配置和当前动作库。
4. 操作步骤：执行现有排序配置回归；静态遍历动作定义。
5. 预期结果：不出现已移除排序命令；原有固定排序策略断言通过。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M3a：逐项菜单静态命令及启用状态

- 验收标准：逐项菜单静态命令及启用状态。
- 类型：静态与动态。
- 测试代码：`testLateMenuState / menu_lifecycle_static.py`。

1. 测试目的：验证逐项菜单静态命令及启用状态。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：50 个字面动作、动态 recent/Open With 菜单。
4. 操作步骤：枚举动作模板和窗口所有副本；检查静态文案与空窗口启用规则；核对全部动态文案写入点。
5. 预期结果：50 个模板逐项分类；静态文案一致；disable/gifdisable/undodisable/folderdisable 空窗口禁用。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M3b：动态图标策略

- 验收标准：动态图标策略。
- 类型：动态回归。
- 测试代码：`testLateMenuState / testLatePauseMenu / testFullscreenMenuIconsRespectMainMenuPolicy`。

1. 测试目的：验证动态图标策略。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：主菜单、虚拟菜单、右键菜单。
4. 操作步骤：状态切换前后读取 QAction::icon 和所属菜单类型。
5. 预期结果：主菜单及虚拟菜单无图标；右键菜单保留相应图标；全屏现有回归通过。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-M4：不同窗口状态隔离

- 验收标准：不同窗口状态隔离。
- 类型：动态回归。
- 测试代码：`testLateMenuState`。

1. 测试目的：验证不同窗口状态隔离。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：两个相反标题栏状态的窗口。
4. 操作步骤：构建第一个窗口全部菜单；创建第二窗口并设置相反状态；重新读取第一窗口。
5. 预期结果：第一窗口各副本文案不被第二窗口覆盖。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-P1：启动耗时减半

- 验收标准：启动耗时减半。
- 类型：性能/动态系统。
- 测试代码：`lifecycle_benchmark.py`。

1. 测试目的：验证启动耗时减半。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：同 Release 配置的冻结基线与候选，各预热一次、交替 10 次。
4. 操作步骤：隔离 INI 设置、禁用自动更新；父进程 monotonic 时钟从 Popen 前计时至首轮事件循环与菜单均就绪；保存原始日志。
5. 预期结果：候选启动中位数 ≤ 基线 × 0.5；任意异常/缺标记/未最大化/超时失败。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-P2：完整退出耗时减半

- 验收标准：完整退出耗时减半。
- 类型：性能/动态系统。
- 测试代码：`lifecycle_benchmark.py`。

1. 测试目的：验证完整退出耗时减半。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：与 P1 相同的全部进程。
4. 操作步骤：在 system-probe 退出前标记处开始计时，等待进程完全终止；排除退出请求前的固定 300 ms 等待。
5. 预期结果：候选退出中位数 ≤ 基线 × 0.5；所有进程正常返回 0，包含析构成本。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-R1a：生命周期安全静态约束

- 验收标准：生命周期安全静态约束。
- 类型：静态与动态。
- 测试代码：`menu_lifecycle_static.py`。

1. 测试目的：验证生命周期安全静态约束。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：生产构造和关闭路径、异步排空接口。
4. 操作步骤：检查构造前置样式、关闭不重设样式、settings sync、shutdownBackgroundWork、finishOrExit 和 1500ms 安全边界。
5. 预期结果：无关闭重排事务，原有持久化及异步排空边界保留。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。

## TC-R1b：功能回归

- 验收标准：功能回归。
- 类型：动态回归。
- 测试代码：`ctest --test-dir build --output-on-failure；原生全屏聚焦测试`。

1. 测试目的：验证功能回归。
2. 前置条件：macOS Cocoa 桌面可用；Release 应用/Qt 测试已构建。Qt runner 使用临时设置；性能进程使用 FOVELLE_DEPLOYMENT_PROBE 隔离 INI 设置。
3. 输入数据：默认 fast manifest、真实 HDR fixture、最大化/全屏场景。
4. 操作步骤：完成构建；串行运行默认门禁；另运行最大化和原生全屏聚焦函数。
5. 预期结果：所有计划阶段完成，零失败零跳过；缺 fixture/超时不视为通过。
6. 后置条件：窗口关闭/进程回收；恢复测试修改的偏好和退出策略；原始日志及结果存入 `reports/evidence/menu-lifecycle/`。
