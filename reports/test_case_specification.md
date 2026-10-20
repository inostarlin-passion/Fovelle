# 启动与退出测试用例规格

日期：2026-10-20。验收 ID 与设计文档一致。每个用例均固化为测试代码；D2/D6 复用现有测试。静态测试验证结构约束，动态测试验证实际行为，P1 单独记录环境噪声。

映射：A1→S1/D1；A2→D2；A3→S2/D3；A4→D4；A5→D5；A6→S2/D6；A7→P1。

## S1

1. 测试目的：静态：菜单在 show 前初始化。
2. 前置条件：源码树。
3. 输入数据：mainwindow.cpp / qvapplication.cpp。
4. 操作步骤：执行 lifecycle_fix_static.py。
5. 预期结果：构造末尾有 ensureMenus；showEvent 无菜单排队；应用使用该构造路径。
6. 后置条件：源码不变。

## S2

1. 测试目的：静态：移除无期限 Open With 等待并保留应用安全边界。
2. 前置条件：源码树。
3. 输入数据：mainwindow、asyncwork、qvapplication。
4. 操作步骤：执行 lifecycle_fix_static.py。
5. 预期结果：窗口无私有池与 watcher 等待；使用取消票据和弱引用；保留 sync 与 1500 ms 排空。
6. 后置条件：源码不变。

## D1

1. 测试目的：动态：工厂返回时菜单和快捷键就绪。
2. 前置条件：Cocoa Qt 测试程序、隔离设置。
3. 输入数据：空窗口。
4. 操作步骤：testStartupMenusReadyBeforeEvents：调用 newWindow，立即检查动作。
5. 预期结果：主菜单与虚拟菜单克隆存在，无需 qWait；重复 show 不增加克隆。
6. 后置条件：关闭窗口，恢复 quit 策略。

## D2

1. 测试目的：动态：状态回归。
2. 前置条件：Cocoa、临时图像、隔离设置。
3. 输入数据：显隐标题栏、全屏、暂停、幻灯片、两个窗口。
4. 操作步骤：运行 testLateMenuState / testLateFullscreenMenu / testLatePauseMenu。
5. 预期结果：主菜单、虚拟菜单、上下文菜单状态一致且跨窗隔离。
6. 后置条件：临时目录销毁，设置恢复。

## D3

1. 测试目的：动态：阻塞查询时快速销毁及取消。
2. 前置条件：Cocoa、隔离设置、信号量。
3. 输入数据：可阻塞 provider 与临时 PNG。
4. 操作步骤：testOpenWithWorkerTeardownContract：等 worker 真正开始，关闭、幂等收尾及销毁窗口，随后释放 worker 并排空。
5. 预期结果：窗口收尾 <250 ms；销毁时 worker 尚未结束；释放后池正常排空；无悬空回调崩溃。
6. 后置条件：释放信号量并清空池，恢复 quit 策略。

## D4

1. 测试目的：动态：单飞、旧路径抑制、最新交付。
2. 前置条件：Cocoa、两个临时 PNG。
3. 输入数据：第一查询被门闩阻塞、重复请求与第二文件。
4. 操作步骤：testOpenWithLatestRequest：查询 A，切 B 并重复请求，释放 A，保持 B 阻塞并核查未显示 A，再释放 B 等待完成。
5. 预期结果：A 在途时调用数为 1；最终菜单显示 B；总查询数 2。
6. 后置条件：门闩释放，窗口及目录销毁，池排空。

## D5

1. 测试目的：动态：实际 Open With 全进程收尾。
2. 前置条件：可派生自身测试程序、隔离 INI。
3. 输入数据：30 ms 与 10000 ms provider。
4. 操作步骤：testOpenWithShutdownProcess：启动专用子进程，实际加载 PNG、派发 Open With，等 provider 已开始后销毁应用。
5. 预期结果：均正常进程退出状态；短任务码 0，长任务码 75；各 <4000 ms；saved.ini 的 saved=true。
6. 后置条件：子进程退出，临时设置删除。

## D6

1. 测试目的：动态：现有 loader 安全与退出合同。
2. 前置条件：Cocoa、隔离测试程序。
3. 输入数据：阻塞 loader；30/10000 ms QvAsync 任务。
4. 操作步骤：运行 testLoaderNonblockingShutdown 和 testShutdownProcess。
5. 预期结果：loader 快速销毁不交付；短任务码 0、超时码 75、设置落盘。
6. 后置条件：释放任务及销毁临时目录。

## P1

1. 测试目的：动态：热启动和空退出实测。
2. 前置条件：Release 基线副本与新构建；无图片隔离 INI；Cocoa。
3. 输入数据：各预热一次，交替 10 对。
4. 操作步骤：执行 lifecycle_fix_benchmark.py，记录原始日志与 JSON。
5. 预期结果：启动中位数下降；退出中位数增幅 <=20 ms；所有样本退出 0 且完整就绪标记存在。
6. 后置条件：所有进程结束，仅保留证据日志。
