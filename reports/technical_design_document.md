# 启动与退出性能修复技术设计

日期：2026-10-20（Asia/Shanghai）。依据：`reports/root_cause.md`。保留用户已有 README 和根因报告修改；旧版同名报告归档至 `reports/evidence/lifecycle-fix/baseline-reports/`。

## 问题与边界

原子问题 S：应用创建窗口后经两轮零定时器才创建菜单；菜单就绪终点包含事件调度等待。原子问题 E：Open With 的窗口私有线程池和 future 在窗口销毁时无期限等待；5.2 秒受控查询已证明其可拖延退出。空窗口原有退出约 29 ms，不能要求它也下降 50%。图片解码与原生渲染队列的等待属于独立路径，本修复不以删除资源安全边界优化计时。

## 多跳检索、交叉验证与收敛

1. [Qt 6.11 QTimer](https://doc.qt.io/qt-6.11/qtimer.html) 的零定时器不保证与其他事件源的顺序；对照本地 showEvent 两次排队和根因报告中提前菜单的干预实验。由 API 语义推出可能延迟，由本地干预验证实际影响，不能把移动工作误称为消除了全部菜单成本。
2. [Qt QThreadPool](https://doc.qt.io/qt-6.11/qthreadpool.html) 与 [v6.11.2 源码](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/corelib/thread/qthreadpool.cpp) 交叉确认 clear 仅丢弃排队任务，析构和无限 wait 会等待运行任务；[Qt Concurrent run](https://doc.qt.io/qt-6.11/qtconcurrentrun.html) 说明基本任务不会被自动中断。回查 MainWindow 的 watcher 和私有池，定位实际等待点。
3. [Apple Launch Services 查询](https://developer.apple.com/documentation/coreservices/1448020-lscopyallrolehandlersforcontentt) 确认其工作为系统应用发现，且接口线程安全；[Qt QPointer](https://doc.qt.io/qt-6.11/qpointer.html) 提供 QObject 销毁后的弱引用清零语义。结合已有 QvAsync 取消票据、GUI 队列交付、应用析构有时限排空，选择重用现有生命周期机制。
4. 反向检验：只删 wait 仍会被成员线程池析构阻塞；分离线程会越过 QApplication 生命周期；只调小 QvAsync timeout 无法约束原窗口池。均不采用。公开检索不能证明真实系统查询延迟分布，采用信号量阻塞与真实子进程验收补足。默认 provider 仍可读取应用级配置并使用图标缓存；这些应用依赖在 QvAsync 排空期间保持存活，“独立”指不依赖窗口，而非不依赖 QApplication。API 语义证据收敛，性能收益以本轮交替实测为准。

## 方案

启动窗口保留 deferMenus 参数的“跳过备用全局菜单”语义，在窗口构造末尾（设置、会话几何完成后、show 前）同步 ensureMenus；移除 showEvent 的菜单定时器。菜单仍保持幂等、状态同步、窗口快捷键和惰性上下文菜单。

Open With 改用 QvAsync::run。工作闭包仅复制路径和 provider，不访问窗口；完成闭包在 GUI 线程检查 QPointer 和停止状态，核对当前文件后再写菜单。单个窗口只允许一个查询在途，后续请求合并，完成后重试最新文件。shutdownBackgroundWork 幂等取消票据，先阻止交付，再取消下载及图像任务；移除窗口私有池及 watcher 等待。应用析构继续执行 QSettings sync 和 QvAsync::finishOrExit（默认 1500 ms），慢系统调用超时保留退出码 75；正常完成退出码 0。

## 正确性论证

- 菜单构造 happens-before show，因此可见窗口不再等待两轮事件才能取得菜单和快捷键；ensureMenus 的幂等条件避免重复树。最终 settings/session 几何已应用，状态测试覆盖多窗口、全屏、动画暂停及上下文菜单。
- worker 只持有值与 provider，窗口销毁无需等待它。取消票据同时阻断尚未开始工作及已排队交付；QPointer 在 GUI 线程检查和访问，没有跨线程解引用窗口的竞态。
- 只有 GUI 线程更新运行票据、pending 和路径，单飞与最新路径检查阻止旧文件结果覆盖新文件；完成后清除票据允许重试。
- 应用析构排空同一受管理池，完成时线程已结束，Qt 可正常销毁；超时则在 Qt 销毁前保存设置并 _Exit(75)，不能把它报告为正常退出。不会把任务移至无监管线程。

## 原子验收标准

| ID | 标准 | 验证 |
|---|---|---|
| A1 | 应用工厂返回的窗口在未派发下一轮事件前已有主菜单和虚拟快捷键 | 静态 S1、动态 D1 |
| A2 | 提前构建后菜单状态、上下文菜单和多窗口隔离保持正确 | 现有动态 D2 |
| A3 | 正在运行的 Open With 不阻塞窗口收尾，重复收尾安全且取消后不交付 | 静态 S2、动态 D3 |
| A4 | 同窗查询单飞，旧结果不写入新文件菜单，最新请求最终交付 | 动态 D4 |
| A5 | Open With 真实子进程短任务正常退出；10 秒任务在 4 秒内退出 75，设置落盘 | 动态 D5 |
| A6 | 有限排空、设置同步和 loader 取消安全边界保持 | 静态 S2、现有动态 D6 |
| A7 | 热缓存空窗口菜单就绪中位启动时间较基线降低，退出中位时间不增加超过 20 ms | 性能 P1，预热后交替各 10 次 |

性能边界为父进程 spawn → 首轮事件且菜单就绪，以及探针退出请求 → 完整进程结束；不代表光学首帧、Finder 调度或系统冷缓存。没有自然慢查询 SLA；受控阻塞验证机制。原生 renderQueue 与全局遗留池的独立等待不在本次已证实修复覆盖范围。
