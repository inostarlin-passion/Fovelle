# Fovelle 启动与退出耗时：可能根因分析

分析日期：2026-10-20（Asia/Shanghai）。代码基线：`34b22d6fb477934ed7a79bd4929f11c6df19e90a`，macOS 专用 Qt/C++ 应用。本报告结合当前源码、本轮实测、仓库已有受控实验和联网一手资料，区分已获实验支持的机制与待验证假设；没有修改生产代码。

## 1. 问题界定与原子化拆解

用户尚未指定“耗时长”的阈值、操作入口、图片、窗口数或冷/热启动条件，因此不能直接认定两个现象只有一个共同根因。

| 问题 | 起点与终点 | 必须分开的子问题 |
|---|---|---|
| 启动耗时长 | 发起启动 → 首个可见帧；另测 → 首次可响应；另测 → 菜单全部就绪；带图启动另测 → 图片实际呈现 | 系统启动调度、进程装载、Qt/Cocoa 初始化、设置迁移、窗口创建/显示、事件分发、菜单构建、会话恢复、目录扫描、图片解码与呈现 |
| 退出耗时长 | 收到退出请求 → 窗口关闭；另测 → 事件循环返回；另测 → 进程完全结束 | 用户确认、保存会话/几何、窗口任务收尾、全局 Qt 线程池、私有 QvAsync 池、渲染队列同步、对象和平台资源析构 |

源码可审计路径：

```text
启动：main → migrateOldSettings → QVApplication/Qt/Cocoa 构造
    → newWindow 或 tryRestoreLastSession → MainWindow::showEvent
    → 首次事件循环 → 两次零定时器分发 → ensureMenus
带图：文件打开事件/命令行/会话 → QVImageCore → QVImageLoader
    → Image I/O / Qt / EPS 后端 → 图像对象与原生呈现

退出：Quit 事件/close → 可选会话确认 → closeEvent
    → onAboutToQuit：窗口 shutdownBackgroundWork → 可选全局池等待
    → exec 返回 → QVApplication 析构：再次幂等收尾、设置 sync
    → QvAsync::finishOrExit → 其余对象/Qt/Cocoa 析构 → 进程退出
```

不同关闭入口及对象销毁顺序可能使窗口收尾提前发生；这不是每条退出路径都严格相同的调用栈。尤其不能把 `event-loop-return` 当成进程退出，也不能把 `first-event-loop-turn` 当成首帧或全部功能可用。

## 2. 方法、证据分级与实测边界

采用“问题拆解 → 检索 API 语义 → 回查版本源码和本地调用点 → 量化/干预 → 寻找反例 → 补检索”的多跳流程。本文展示可核查的因果依据和证伪结果。

- **强证据**：代码路径、官方语义与受控干预/定向测试相互吻合。
- **中等证据**：路径及机制成立，但没有复现用户自然条件下的慢操作或没有测量该阶段耗时。
- **弱证据**：通用机制可能成立，当前工作负载没有支持它是主要原因。

### 2.1 本轮新增测量

使用现有 `tests/lifecycle_benchmark.py::run()`，直接启动 `build/Fovelle.app/Contents/MacOS/Fovelle`，预热一次后测量十次。每次隔离 INI 设置、无图片、单个最大化窗口、关闭自动更新；由系统探针触发退出。原始日志及统计见 [本轮测量](evidence/lifecycle-root-cause/current-summary.json)。

| 指标 | 中位数 | 均值 | 最小—最大 |
|---|---:|---:|---:|
| 父进程启动子进程 → 首次事件循环和菜单均已就绪 | 333.39 ms | 324.70 ms | 286.99—351.38 ms |
| 探针即将请求退出 → 父进程观察到完整进程结束 | 28.91 ms | 30.34 ms | 26.43—38.64 ms |

十次均返回 0。被测主程序 SHA-256 为 `98925604485b687fde3dee60d88eb3ee67b012eacc692f32b5a3b93fbe97c479`，与已有归因实验的原始程序一致。环境为 macOS 27.0.1、arm64、Qt 6.11.2 Release。

这些是进程重新启动、缓存经过预热的空窗口数据，**不是操作系统冷缓存测试，也不包含 Finder 启动请求到进程创建的耗时**。菜单标记只是特定功能就绪边界；没有光学首帧测量。隔离 INI 也不能代表用户真实 NativeFormat 偏好、会话及所有系统缓存。探针绕过会话询问，因此本轮约 29 ms 的退出结果不覆盖确认框。

### 2.2 已有证据的复核

复核了 [共同阶段原始样本](evidence/menu-lifecycle/startup-attribution/common-stages/profile.json)、[菜单时机干预原始样本](evidence/menu-lifecycle/startup-attribution/menu-timing-intervention/profile.json)、实验补丁及逐次日志；重新计算各批均值、中位数和样本数，核对基线提交和主程序哈希，结果见 [复核清单](evidence/lifecycle-root-cause/historical-evidence-audit.json)。这些是已有实验，不能称为本轮重新执行的干预。

本轮还执行现有测试：

- `RemediationTests::testLoaderNonblockingShutdown`、`testShutdownProcess`：通过，见 [日志](evidence/lifecycle-root-cause/shutdown-tests.log)。后者检查 30 ms 与 10000 ms 的模拟任务，长任务走退出码 75 的超时路径。
- `FeatureTests::testOpenWithWorkerTeardownContract:controlled-platform-latency`：通过；提供者耗时 5204 ms，窗口收尾总耗时 5212 ms，close 事件本身约 19 ms，见 [日志](evidence/lifecycle-root-cause/openwith-controlled.log)。这是人为注入的耗时，证明等待传播，不代表真实 Launch Services 必然需要五秒。

测试程序为现有 `build/tests/fovelle_tests`，SHA-256：`d9277f0ca8b4a0826c7e686c00511ed1d806727337cc3073020bbcfa8759af73`。本轮未重编译测试程序；结果按该现有构建记录，并与当前对应源码核对。通过的含义是生命周期合同成立，不能据此宣称所有退出路径快速。

## 3. 多跳联网检索与多源交叉核验

以下资料均于分析日检索/核验。Qt API 尽量固定至 6.11，源码固定至 v6.11.2。Apple 动态库与主线程机制可用于 macOS；资料中的 UIKit 场景没有直接套用于 Qt 窗口。

| 轮次与检索问题 | 一手资料与交叉来源 | 对本项目的约束 |
|---|---|---|
| 第一跳：启动究竟分哪些阶段，是否能直接归因于 dylib 数量？ | [Apple 启动性能](https://developer.apple.com/documentation/xcode/reducing-your-app-s-launch-time)、[Apple WWDC22：Link fast](https://developer.apple.com/videos/play/wwdc2022/110362/)；对照本地 main 的阶段标记 | 装载和初始化是候选阶段，必须测量；通用文档无法证明本项目的主瓶颈 |
| 第二跳：`singleShot(0)` 是否意味着没有延迟、是否后台执行？ | [Qt 6.11 QTimer](https://doc.qt.io/qt-6.11/qtimer.html)、[v6.11.2 QTimer 源码](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/corelib/kernel/qtimer.cpp)；对照 showEvent 和干预样本 | 零定时器与其他事件源的先后不保证；在接收者线程执行，延后不等于减少工作量 |
| 第三跳：clear/cancel 是否足以立即退出？ | [Qt 6.11 QThreadPool](https://doc.qt.io/qt-6.11/qthreadpool.html)、[v6.11.2 实现](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/corelib/thread/qthreadpool.cpp)、[Qt Concurrent run](https://doc.qt.io/qt-6.11/qtconcurrentrun.html)；对照三种线程池和定向测试 | clear 处理尚未开始的任务；等待和析构可阻塞；已运行的基本 run 任务没有自动抢占式停止 |
| 第四跳：Open With 的后台操作究竟是什么？ | [Apple 内容类型处理程序查询](https://developer.apple.com/documentation/coreservices/1448020-lscopyallrolehandlersforcontentt)、[Qt Concurrent run](https://doc.qt.io/qt-6.11/qtconcurrentrun.html)；对照 getOpenWithItems 与受控耗时测试 | 存在系统应用发现和图标读取；可确认等待机制，不能从 API 名称或 deprecated 状态推导实际延迟 |
| 第五跳：退出时渲染队列为何可能阻塞？ | [Apple Dispatch sync](https://developer.apple.com/documentation/dispatch/dispatchqueue/sync%28execute%3A%29-3segw)、[Apple libdispatch queue.h](https://raw.githubusercontent.com/apple/swift-corelibs-libdispatch/main/dispatch/queue.h)；对照 HDRRenderer::Impl 析构 | 同步提交要等 block 完成；串行队列前序任务会影响返回时机，不等价于所有 GPU 工作都已完成 |
| 第六跳：图片及持久化能否解释所有场景？ | [Qt 图像读取](https://doc.qt.io/qt-6/qimagereader.html)、[Qt 6.11 QSettings](https://doc.qt.io/qt-6.11/qsettings.html)、[Apple 响应性诊断](https://developer.apple.com/documentation/xcode/improving-app-responsiveness)；对照解码、保存路径和空窗口反例 | 图片工作与设置同步只能在实际执行和测得耗时后升级为根因；主线程阻塞须定位到具体栈 |

公开资料、库源码、本地业务源码与实测承担不同证明任务；同一文档的多个镜像不计为独立来源。Apple 页面正文读取受动态页面限制时，补查官方检索摘录及 Apple 开源头文件；不可访问的误定位 URL 未用作证据。

## 4. 问题一：启动耗时长

### S1：菜单延后到显示后的事件轮次，且菜单树仍在 GUI 线程集中构建

**结论：对“空窗口启动到菜单就绪的额外耗时”，证据最强；不代表全部启动耗时或光学首帧。**

代码位置：`src/mainwindow.cpp:609` 的 `ensureMenus()`、`src/mainwindow.cpp:1298` 的 `showEvent()`，以及 `src/qvapplication.cpp:117` 附近的惰性应用菜单。

可核验因果链：显示窗口 → 两层 `singleShot(0)` 推后菜单构建 → 前序/插入事件及分发耗时落在菜单就绪之前 → GUI 线程构建菜单树和虚拟快捷键动作 → 菜单完成才达到完整测量终点。零定时器语义见 [Qt 文档](https://doc.qt.io/qt-6.11/qtimer.html)，具体耗时由本地实验提供。

共同阶段批次中，两程序均使用 Qt 6.11.2、Release、空白最大化窗口、隔离设置，预热后交替测量各 20 次：

| 阶段均值 | qView 7.1 | Fovelle | Fovelle − qView |
|---|---:|---:|---:|
| spawn → 身份标记 | 36.19 ms | 39.47 ms | +3.28 ms |
| 设置准备 | 0.89 ms | 4.85 ms | +3.97 ms |
| 应用构造 | 103.96 ms | 93.79 ms | −10.17 ms |
| 窗口构造及显示 | 113.83 ms | 105.20 ms | −8.63 ms |
| 窗口完成 → 首轮事件 | 40.76 ms | 34.64 ms | −6.12 ms |
| 首轮事件 → 菜单就绪边界 | 0.04 ms | 74.27 ms | +74.24 ms |
| 总计 | 295.66 ms | 352.23 ms | +56.56 ms |

阶段使用均值相加；各阶段中位数不能相加解释总中位数。qView 的菜单工作主要在更早阶段完成，因此最后一行阶段差异也包含“工作移动到哪个阶段”的影响。

独立干预批次保留相同菜单工作，只在窗口构造末尾、原生 show 前执行 `ensureMenus()`：原行为总中位数 385.18 ms，实验行为 318.99 ms，减少 66.19 ms（17.18%）。相同批次，原行为首次事件后至菜单开工的间隔均值 49.12 ms，菜单树构建均值 46.80 ms；实验树构建约 40.15 ms。菜单状态同步仅约 0.054 ms，菜单挂接约 0.017 ms。

反向检验：若主要成本是新增状态同步，约 0.054 ms 无法解释数十毫秒差异；若只统计首轮事件，Fovelle 在共同阶段批次反而更早到达，因而会掩盖菜单未就绪。若提前执行同一工作完全无效，干预应无明显改善，现有样本不支持这一预测。干预同时改变了执行上下文，不能把全部收益精确归为“消除了纯空等”。菜单树内部 AppKit/Qt 各调用的份额尚未单独测量。

原始依据：[归因记录](evidence/menu-lifecycle/startup-attribution/findings.json)、[菜单明细](evidence/menu-lifecycle/startup-attribution/menu-body-summary.json)、[干预补丁](evidence/menu-lifecycle/startup-attribution/eager-menu.patch)。该实验不是已发布修复，也未证明提前构建在所有带图、会话和菜单场景均最优。

### S2：Qt/Cocoa 与原生窗口创建构成显著固定成本

**证据中等：解释绝对耗时的一部分；未支持其是相对 qView 变慢的主因。**

应用构造和窗口构造/显示在上述批次分别约 94 ms、105 ms，位于第一轮响应前；其中包含 Qt/Cocoa、成员对象、布局、native peer、show/raise 等混合操作。主线程工作影响响应的机制与 [Apple 响应性资料](https://developer.apple.com/documentation/xcode/improving-app-responsiveness) 一致。

反证：同批 qView 对应阶段更慢，且两边均最大化；因此“新增功能使构造更慢”或“默认最大化独自解释差异”均不成立。需要 Time Profiler/Thread State Trace 继续分解，不能将整个阶段直接命名为 Metal 初始化。

### S3：带图启动、恢复会话和首次格式发现增加了用户等待的工作量

**证据中等：代码路径成立，具体影响依赖文件和会话，尚无本轮自然场景耗时。**

`src/main.cpp` 和 `src/qvapplication.cpp::tryRestoreLastSession()` 可创建多个窗口并安排恢复；`src/qvimagecore.cpp` 请求目录列表与图像；`src/qvimageloader.cpp::readFile()` 调用 Image I/O、RAW/HDR、SVG 或 Qt 回退读取；`src/qvfileenumerator.cpp::scanOptions()` 在调用线程取得格式缓存，扫描本体才交给异步任务。首次取格式列表仍可能付出插件/类型注册表发现成本。解码和数据流读取语义见 [QImageReader](https://doc.qt.io/qt-6/qimagereader.html)。

可能链条：待恢复窗口/大图增加 → 文件元数据、解码、色彩处理和呈现准备增加 → 图片实际呈现更晚；大目录、内容 MIME 检测、递归目录标记或慢文件系统还可能增加扫描与资源竞争。后台执行能减轻 GUI 直接阻塞，但不会消除用户等待目标图像和呈现的时间。

反证与限制：空窗口数据中无图片，不能拿 RAW/HDR 解码解释 S1；当前渲染器按需创建，不能仅因链接 Metal 就认定空启动执行了完整 HDR 初始化。目录扫描异步，也不能直接认定“先扫完整目录才允许显示当前图片”。要升级为根因，需分别记录当前图像读取、扫描、首次格式发现与实际呈现的时间，做单图/会话/目录规模对照。

### S4：设置迁移、动态装载及更新初始化是次要或条件性候选

- **设置迁移：次要、有证据。** 共同阶段差异约 3.97 ms，不能解释主要差距。`SettingsManager::migrateOldSettings()` 已写入 `firstlaunch` 标记，不能沿用“每次启动都无条件复制旧设置”的历史解释。真实用户设置较大或后端异常仍需另测。[QSettings](https://doc.qt.io/qt-6.11/qsettings.html)
- **装载及首次系统缓存：弱证据。** pre-main 相关宽边界差异仅约 3.28 ms，含创建进程、日志接收与调度，并非直接 dyld 指标。Apple 说明框架和初始化可能影响启动，但现有热测不支持其主导当前差异；真正冷启动需单独取样。[Apple Link fast](https://developer.apple.com/videos/play/wwdc2022/110362/)
- **自动更新：当前基线不支持。** `src/updatechecker_sparkle.mm::initialize()` 校验 feed/key 后才启动 Sparkle；现有归因构建未配置有效更新参数，测量也关闭自动检查。有有效配置的发行包仍应单独测量初始化，不应把网络往返直接认定为阻塞首窗的原因。

## 5. 问题二：退出耗时长

### E1：“打开方式”任务不可立即停止，窗口析构同步等待其完成

**结论：存在强证据的条件性慢退出机制；是否命中用户实际慢退出仍待现场栈确认。**

`src/mainwindow.cpp:1709` 附近的 `requestPopulateOpenWithMenu()` 将 provider 放入窗口自己的 `backgroundThreadPool`；`shutdownBackgroundWork()` 在取消其他工作前，若 watcher 运行则 `waitForFinished()`，随后 `waitForDone()`。`src/qvcocoafunctions.mm:5345` 附近的 provider 进行文件类型查询、Launch Services 应用枚举、应用路径/显示名解析及图标获取。

因果链：Open With 任务仍在运行或待运行 → 退出/析构调用窗口收尾 → 主线程等待 future 完成 → provider 的剩余耗时传递到窗口销毁及退出尾部。基本模式任务的语义见 [Qt Concurrent run](https://doc.qt.io/qt-6.11/qtconcurrentrun.html)；系统查询功能见 [Apple API](https://developer.apple.com/documentation/coreservices/1448020-lscopyallrolehandlersforcontentt)。

本轮人为令 provider 耗时约 5.2 秒，实测收尾 5212 ms、provider 5204 ms、close 事件 19 ms。符合“窗口已经关闭，进程/析构还在等待”的表现，且证明 **1.5 秒并非所有退出路径的总上限**。

反向检验：如果异步派发本身阻塞，应看到 dispatch 耗时大；本轮为 0 ms。如果对象析构无需等待 worker，总收尾不应随 provider 增至约 5.2 秒；实测相反。但真实系统查询延迟没有在本轮自然复现，不能据此断言 Launch Services 数据库损坏、图标服务故障或每次退出均有此任务。

### E2：图片、动画帧或目录任务被逻辑取消后仍有在途工作，触发 QvAsync 排空等待

**证据强于纯推测：取消/等待机制已验证；用户文件上的发生频率与耗时未知。**

`src/qvimageloader.cpp::shutdownAsyncWork()` 只设置票据取消、清理请求和回调生命周期；`src/asyncwork.h::run()` 在 work 前后检查取消，不能抢占 work 中的同步系统解码。`src/asyncwork.cpp::drain()` 清除排队任务，再等待运行中的私有池任务；析构调用 `finishOrExit()`，默认等待 1500 ms，超时同步设置、清理登记子进程并 `_Exit(75)`。

因果链：前台读取/相邻预载/帧读取或文件系统操作仍未返回 → 逻辑取消仅阻止交付或后续工作 → 私有池尚非空 → 退出等待剩余任务，或触发约 1.5 秒的排空超时。池语义由 [Qt 文档](https://doc.qt.io/qt-6.11/qthreadpool.html) 与 [版本实现](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/corelib/thread/qthreadpool.cpp) 交叉支持。

当前代码已有预载源文件/解码内存准入限制（各 128 MiB）、相邻距离 1、部分请求的 500 ms 防抖和目录循环取消检查；它们降低风险，但不能限制每一次底层读取的墙钟耗时。EPS 外部进程轮询还包含取消检查，不能与不可中断的 Image I/O 调用一概而论。

反向检验：本轮 loader 对象快速析构测试通过，并不代表 worker 同时完成；进程探针以长任务验证了超时退出码 75。若自然慢退出完全无 QvAsync 在途任务，或耗时集中于另一个同步点，此假设应降级。空窗口约 29 ms 的反例排除了“无论何时都固定等满 1500 ms”。

### E3：原生图像渲染器析构同步排空串行队列

**证据中等：明确等待点，尚无用户负载下的定量归因。**

`src/qvcocoafunctions.mm:2411` 附近 `HDRRenderer::Impl::~Impl()` 停止显示驱动后，对 `renderQueue` 和 `persistentSurfaceQueue` 分别执行 `dispatch_sync(queue, ^{})`。这些路径也服务部分 SDR 持久图像，不能仅凭类名限制为 HDR。

因果链：渲染/持久表面准备尚在串行队列执行 → 析构向队列同步提交空 block → 等前序 CPU 队列任务完成 → 后续图层、图像和上下文释放才继续。`dispatch_sync` 的同步语义由 [Apple 文档](https://developer.apple.com/documentation/dispatch/dispatchqueue/sync%28execute%3A%29-3segw) 与 [Apple queue.h](https://raw.githubusercontent.com/apple/swift-corelibs-libdispatch/main/dispatch/queue.h) 支持。

反向检验：仅链接 Metal、持有 HDR 类或提交过 command buffer，不能证明退出在等 GPU，也不能证明死锁。当前未发现足以认定互相等待的证据。若退出栈未停在这些队列同步、且两队列已空，该候选不解释长尾。该等待属于另一条生命周期边界，QvAsync 的 1500 ms 不自动约束它。

### E4：应用级全局池存在无截止时间的条件等待

**证据中等偏弱：代码成立，但当前主要任务已用专属池。**

`src/qvapplication.cpp::onAboutToQuit()` 检查 `QThreadPool::globalInstance()->activeThreadCount()`，非零时调用无截止时间的 `waitForDone()`。若遗留/未来任务进入全局池，且退出时仍运行，就可能拉长退出。该池不同于窗口 Open With 池，也不同于 QvAsync 私有池。

反证：已有空窗口日志显示 `active_threads=0`、该阶段约 0 ms；不能把所有慢退出归给全局 QtConcurrent。30 秒的线程池空闲回收超时也不等于退出必等 30 秒，实际等待语义须以 [Qt 6.11.2 实现](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/corelib/thread/qthreadpool.cpp) 为准。

### E5：会话确认、持久化及其余平台资源释放

**证据中等至弱：存在相关工作，但无异常耗时测量。**

- `getSessionSaveDecision()` 在开启会话保存且存在图片时用 `QMessageBox::exec()` 等待选择；对话框被遮挡或等待用户会表现为未退出。这属于交互等待，不能直接称为性能故障。
- `closeEvent()` 保存几何，`onAboutToQuit()` 可能整理会话，应用析构执行 `QSettings().sync()`。大量窗口或异常存储后端可能增加耗时；本轮隔离 INI 没有覆盖真实偏好后端。[QSettings 同步语义](https://doc.qt.io/qt-6.11/qsettings.html)
- `event-loop-return` 后仍有应用成员、窗口、Qt/Cocoa 和图像资源释放；只有测到该区间显著增大，才应进一步归因到具体资源。目前无依据宣称是 Sparkle、内存泄漏或平台销毁缺陷。

## 6. 反向证伪汇总与下一步可区分的验证

| 待检验解释 | 应出现的可观测结果 | 已知反例/现状 | 后续判别方法 |
|---|---|---|---|
| 延后菜单改善全部启动体验 | 首轮响应及菜单终点都更早 | 两个终点可能相反；提前执行相同菜单工作显著降低菜单终点 | 固定图片/窗口状态，对首帧、响应、菜单分别计时 |
| 更多库或 RAW/HDR 是空启动主因 | 装载/图像阶段占据主要额外耗时 | 热测装载差距小，空窗口无解码 | 真冷启动记录 pre-main；带图独立测解码与呈现 |
| 一旦 cancel/clear 就可以立即退出 | 在途任务立即结束 | loader 交付被取消，work 可继续；Open With 受控尾部约 5.2 秒 | 为每个任务记录开始、取消、返回和所属池 |
| 退出最多 1.5 秒 | 所有等待均受同一 deadline 约束 | Open With、窗口池、全局池及原生队列存在其他等待点 | 在每个同步等待前后用单调时钟记录，另由父进程记录退出 |
| 空窗口慢退出已复现 | 当前空窗口持续出现显著长尾 | 本轮中位 28.91 ms；已有匹配 qView 对比中 Fovelle 为 28.29 ms、qView 为 33.37 ms | 取得用户真实入口、配置、图片及等待阶段；不把空测结果外推到带图 |

优先验证顺序：启动先定位菜单树内部成本和真实首帧；慢退出现场先采主线程等待栈，再区分 Open With、QvAsync、原生队列和偏好同步。保存失败/取消/超时退出码，避免把退出码 75 计成正常快速退出。不要简单移除等待来优化计时；它们目前也承担 Qt 图像对象和后台任务的生命周期安全职责。

可复用的定向命令（现有测试程序由测试框架隔离设置）：

```bash
FOVELLE_TEST_SUITE=RemediationTests FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests testLoaderNonblockingShutdown testShutdownProcess
FOVELLE_TEST_SUITE=FeatureTests FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests testOpenWithWorkerTeardownContract:controlled-platform-latency
```

## 7. 检索收敛与结论边界

启动方面，在“同环境空窗口、至菜单就绪”的问题上，官方零定时器语义、当前调用点、分阶段数据和菜单时机干预已经闭合；额外差异的主要位置和时机效应有充分证据。菜单树内部细分、真正冷启动以及带图恢复会话属于尚未量化的独立子问题。

退出方面，公开资料与代码足以说明多个同步等待边界；受控 Open With 测试验证了超过 1.5 秒的具体机制，QvAsync 探针验证了逻辑取消与排空超时。但没有用户自然慢退出样本、现场栈、图片和真实设置，不能在 E1—E5 中确定唯一现场根因。针对 API 语义的追加检索已收敛；继续泛搜无法补上本地工作负载证据，达到的是“现场归因所需信息的检索饱和”，不是宣称所有可能性均已排除。

因此：**启动优先怀疑菜单构建时机与 GUI 线程菜单工作；退出优先核查在途后台任务和原生渲染队列的同步收尾。当前空窗口测试没有复现普遍性的慢退出。**

原路径之前的全屏闪烁报告已保存在 [旧报告归档](evidence/lifecycle-root-cause/previous-root-cause-fullscreen.md)。
