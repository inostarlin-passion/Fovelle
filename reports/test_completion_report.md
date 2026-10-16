# GitHub Actions 失败修复：测试完成报告

日期：2026-10-16（Asia/Shanghai）。基线：2002590。本轮只修改 CI 测试基础设施和工作流，不修改产品的下载时限或 SDR/HDR 呈现策略。

## 1. 已确认的失败

- [Build Fovelle 38898648389](https://github.com/inostarlin-passion/Fovelle/actions/runs/38898648389) 与 [Checks 38898647755](https://github.com/inostarlin-passion/Fovelle/actions/runs/38898647755) 在同一 SHA 的相同两项测试失败。
- testDownloadTotalTimeout 的旧输入不能保证客户端在 150ms 内接收滴流，而断言只接受总超时。Qt 的写缓冲和计时器契约不支持该推导；未把 Nagle 的具体触发声称为已唯一证明的包级根因。
- 原始 HDR XML 的前三行均等不到 persistentHDRSurfaceReady，随后整个子进程超过 180s。headroom=1 在本地复现同一失败（46.7s），确认为显示能力前提缺失。

## 2. 实现与验证结果

| 项目 | 实际结果 |
|---|---|
| 设计／验收 | 设计含五组条件证明，CI1–CI8 原子验收及每例六要素，另含环境/旧结果反例 |
| 红证据 | 新接口未实现时 Python 导入失败、QtTest 缺头文件；SDR headroom=1 真实原生失败；后续补充反例继续在旧实现失败 |
| 构建 | 本地 Qt 6.11.2 Release 目标构建成功 |
| 新契约及执行器测试 | 9/9 通过；成功、非零退出、超时、缺输出、旧结果、缺/重/错数据行及 workflow 证据契约均覆盖 |
| 原有门禁单测 | 3/3 通过，原有静态契约 6/6 通过 |
| 强制 SDR 环境回归 | 两个 headroom 通道均为 1 时，HDR 亮度控制、RemediationDynamic、PortableHDR、ActionsRegressionContracts 四个 CTest 项 4/4 通过（39.60s） |
| 真图门禁 | 三个原生阶段 JSON 为 passed；HDR 全屏十个数据行精确匹配，零失败/跳过 |
| 网络稳定性 | 总超时与空闲重置两个真实 loopback 用例重复十轮，10/10 通过 |
| 资源与环境 | 16 种双环境键存在性/字节组合精确恢复；子进程超时终止自己的进程组并回收，原 XML/日志/JSON 保留 |
| 格式／差异 | 改动头文件和 C++ 改动范围格式校验通过，源码 git diff --check 通过；原始证据日志保留上游空白并采用目录级数据属性。原有 build.sh 格式脚本即使有诊断仍返回 0，未将它算作合规证明，也未批量重排无关历史源文件 |

## 3. 验收映射

| 验收 | 测试与结果 |
|---|---|
| CI1 | LowDelayOption+flush，多个真实 progress，总时限 600ms 小于空闲 2000ms，精确本地化原因；通过 |
| CI2 | 流量持续超过两倍空闲时限仍不失败；停滞控制继续触发空闲保护；通过 |
| CI3 | ScopedHDRTestDisplay 保存并恢复潜在/当前两个键；默认 4，颜色保留原 16 或更高参考值；16 个组合通过 |
| CI4 | 父环境双 headroom=1，真实 DNG/gain-map JPEG、float 像素、十个全屏行均执行；通过 |
| CI5 | suite/函数/十个 DataTag/所有 Incident 必须满足条件，缺失/重复/错误/skip 反例拒绝；通过 |
| CI6 | 非零退出及挂起进程均失败；XML、log、JSON 保留；共享 270s/子进程 180s/清理 5s 预算不靠延长外层 300s 掩盖失败；通过 |
| CI7 | 旧 XML 在启动前删除，未开始的后续阶段为 not-run，退出 0 但无新 XML 仍失败；通过 |
| CI8 | Build、Checks unit、兼容与发布工作流必开门禁并 always 上传完整结果目录和 CTest 日志；本地契约通过，远端结果由 PR Actions 复核 |

## 3.1 真实 PR 复核与第二轮修正

[草稿 PR #4](https://github.com/inostarlin-passion/Fovelle/pull/4) 的提交 3c86f4e 已在 macOS 26 Build、Checks 及 macOS 15 兼容工作流执行。三者的 portable HDR 均通过，原始总超时失败也消失；新执行器单测却因 Python 冷启动超过 200ms，将预期退出 7 的模拟进程正确判为 timeout。另一次 Checks 原容器功能测试在 6.25s 完成解码，超出旧 5s 守卫。

保留本轮失败日志，不修改正式 runner 的失败分类：单测加入 350ms 受控启动延迟，非零退出／挂起用例分别使用 5s 守卫；移除伪子进程不需要的 unittest.mock 导入。图像功能测试按实际 ImageIO/Qt SVG 调用链采用 15s/5s 有界守卫并记录格式、预算和解码耗时，全部结果断言保留，组上限仍为 90s。旧日志没有具体格式，报告不把单一格式的冷启动原因写为已证实事实。

第一轮本地完整回归 47/47 通过（430.00s）。扩展后的十个契约／执行器单测通过，受影响 RemediationDynamic 与 ActionsRegressionContracts 两项复核通过。下一次真实 Actions 将核对新预算前提，不预填成功。

## 4. 证据与边界

证据目录：reports/evidence/actions-fix/。包含远端失败 job 日志与下载 XML、本地 SDR 红 XML、扩展契约红/绿日志、local-sdr-gate 的原生 XML/JSON/log、网络十轮日志与格式观察。

本机为 macOS 27.0.1/arm64；原失败 runner 为 macOS 26.6.2。测试 headroom 注入验证真实原生缓冲/像素/几何支路，不把虚拟屏幕描述为物理 HDR 显示器。错误与 skip 保持硬失败，不采用自动重试洗白。

远端真实复核将在独立修复分支的草稿 PR 中执行，报告不预填未知的远端通过结果。

---

# 历史完成报告

# Fovelle 问题 1–10：测试完成报告

日期：2026-10-16（Asia/Shanghai）。源码基线：`609d554`，修复保留在当前工作区，未创建提交或发布版本。

## 1. 结论与结果口径

上一轮问题 1–10 的生产修复、设计证明、20 条原子验收、静态与动态测试均已完成。新增行为测试 21 个，连同初始化/清理为 **23 passed、0 failed、0 skipped**；最终窗口 URL 成功链路也已实际执行。

必须区分全量记录与最终复核：核心修复阶段全量 **45/45 通过**；增加交付启动门禁后的全量为 **44/46 通过**，其中原有键盘轨迹与外置 RAW HDR 稳态探针各出现一次失败。相同二进制单独复核 **3/3 通过**；随后补充明确窗口激活前置条件，受影响标准 DPI、2× DPI、HDR 全屏和 portable HDR 门禁 **4/4 通过**。最后补充的窗口 URL 生命周期测试所在新增集合再次通过。没有把失败删掉或改为跳过，也没有声称最后一次全量运行是一次全绿。

全部 46 个已注册 CTest 项目均有通过记录；最终复核修复了测试前置条件的缺失，仍不把一次复测当作所有桌面焦点交错下绝无波动的证明。

## 2. 环境与实际执行

| 项目 | 实测环境／结果 |
|---|---|
| 系统与架构 | macOS 27.0.1，arm64 |
| 编译器／SDK | Apple LLVM 17.0.0，macOS SDK 27.0 |
| Qt／配置 | Qt 6.11.2，Release；macOS deployment target 15.0；BUILD_TESTS=ON |
| 构建 | 生产 app、QtTest、辅助目标均成功构建 |
| 静态契约 | 6 个检查组通过，覆盖 URL、目录快照、缓存/异步边界、退出、交付及门禁注册 |
| 门禁反例 | 3 个 unittest 通过；缺失/损坏样本、缺函数、空执行、skip/fail/xfail 均被拒绝 |
| 新增动态行为 | 21 个函数，含初始化/清理合计 23 通过，零失败/跳过 |
| portable HDR | gain-map JPEG 与 Bayer DNG 真正解码；原生颜色像素、10 个全屏数据行执行；XML 汇总非 pass 为 0 |
| 交付 | 部署、整个 bundle Mach-O 校验、重签、压缩、解压、真实 Fovelle --version 和 Ghostscript --version 成功；移除 Cocoa 插件后校验失败 |
| DNG 可重复生成 | 生成器再次输出与提交样本逐字节相同 |
| clang-tidy | urldownload、asyncwork、qvfileenumerator、qvmovie 四个生产源文件完成检查，无诊断错误 |
| 差异检查 | git diff --check 通过；新增 C++ 文件与改动行按仓库 clang-format 规则格式化 |

本地没有执行远端 GitHub Actions，也没有把 SDK 27 本机运行称为 macOS 15 实机验证。macOS 15 兼容 CI、发布门禁和 XML 证据上传已配置。

## 3. 原子验收闭环

| ID | 原问题 | 生产行为与动态证据 | 结果 |
|---|---|---|---|
| R1 | 1 | UrlDownload 负责全部网络/文件/定时器操作，回调与 reply 线程归属检查；MainWindow URL 路径无 QtConcurrent 网络读取 | 通过 |
| R2 | 2 | 带 ICC/作者 PNG、APNG、SVG、gain-map JPEG、Bayer DNG 下载原字节比较；统一 loader 保留矢量、动画及 HDR/RAW；真实窗口 APNG 保留两帧 | 通过 |
| R3 | 3 | 十次成功事务、取消和 404；reply 归零，失败目录回到基线，成功容器在窗口销毁后删除 | 通过 |
| R4 | 4 | 停滞 HTTP 在缩短的空闲时限触发失败；实际简体中文错误信号验证，四份 TS 同步 | 通过 |
| R5 | 4 | 每 30ms 滴流响应仍被总时限终止，不会不断延长截止时间 | 通过 |
| R6 | 5 | Content-Length=1025、预算 1024 时，零正文落盘即拒绝 | 通过 |
| R7 | 5 | 无长度与 chunked 的 1024/1025 字节边界分别成功/失败，累计写入不超过预算 | 通过 |
| R8 | 5 | 未知总量采用不定进度；重复取消只有一个取消终态，真实 MainWindow 路径不弹错误框 | 通过 |
| R9 | 6 | 普通 CI 部署后打包/解压复检；独立动态交付测试实际启动解压 app 与捆绑 Ghostscript，缺插件反例失败 | 通过 |
| R10 | 7 | 无 QObject 跨线程访问的 ScanOptions 值快照，自然排序结果异步返回主线程 | 通过 |
| R11 | 7 | A→B 快速换目录只提交最新结果；关闭使旧代次失效 | 通过 |
| R12 | 7 | 目录首图异步打开、目录快照到达、下一张导航与窗口信息更新 | 通过 |
| R13 | 8 | QImage 缓存实际字节计量、64MiB 默认预算；16/1/1024 字节测试策略不超预算 | 通过 |
| R14 | 8 | APNG 和 Qt GIF 两条解码路径均验证淘汰后回退像素一致，大于预算的帧不缓存 | 通过 |
| R15 | 8 | 异步 start/stop、占满工作池后的换源与主线程 heartbeat、100%/200% 速度比较、非法输入错误终态 | 通过 |
| R16 | 9 | 注入慢读取，加载器销毁小于 100ms；工作结束后没有迟到业务回调；矢量 tile 同样迁入读任务服务 | 通过 |
| R17 | 9 | 子进程通过真实 QVApplication 析构验证：30ms 工作正常退出 0，10s 工作超时退出 75，已保存设置可读 | 通过 |
| R18 | 10 | 原创可移植编码样本、生成源码、CC0 声明、SHA-256 清单；缺失/篡改拒绝，DNG 重生成一致 | 通过 |
| R19 | 10 | 无条件注册关键 portable HDR 门禁；真实原生像素/全屏执行；XML 禁止缺执行、失败或跳过，CI 上传执行证据 | 通过 |
| R20 | 1–10 | 原有完整回归与失败项复核；激活前置条件补充后的四项回归通过，所有原结果阈值保留 | 通过，GUI 环境边界见第 5 节 |

## 4. 红阶段、反证与修复记录

| 观察／反例 | 实际处理及证据 |
|---|---|
| 旧实现没有新接口，测试构建缺 urldownload.h；初始静态检查 5 failures、1 error | 保留 red-build.log、red-contract.log；不把编译失败冒充动态行为失败 |
| 目录动态测试暴露无 QWidget 父对象／窗口句柄时的崩溃 | 对父对象安全转换并处理空句柄，独立 core 与实际窗口路径均通过 |
| 第一版合成 DNG 被 ImageIO 识别为 TIFF，不能进入 RAW 链路 | 补齐 RAW IFD 标记等信息并核验 CGImageSource、CIRAWFilter 与生产 loader；最终是 com.adobe.raw-image |
| 异步非法输入没有 error 信号 | 新用例实测失败后，修正首帧/帧解码失败的明确错误终态；最终通过，不缓存空白失败帧 |
| macdeployqt 后第三方库仍含开发机路径 | 区分 LC_ID_DYLIB 与真正依赖；规范化路径、检查整个 bundle、重签并解压启动；包含 Resources/ghostscript |
| 全量运行出现键盘零动作与 HDR 稳态无法分类 | 原始失败保留在 ctest-final.log；相同二进制复核通过后补充 qWaitForWindowActive，受影响四项再次通过；不放宽像素/几何/动作断言 |
| 缺样本／损坏 XML 可能被当作“未执行但成功” | 正式门禁验证函数的反例测试全部拒绝；新门禁非 pass 数为零 |

## 5. 正确性与适用边界

- 设计文件给出状态机不变量、归纳证明、代次反证、缓存字节预算证明与播放截止时间模型；这些证明以 Qt/Apple API 契约与事件循环可调度为前提，不声称验证了所有 OS/驱动实现。
- 128MiB 是单个 URL 事务落盘字节上限；64MiB 是动画缓存的 QImage 数据预算，当前帧、在途帧、解码器、GPU 和容器开销另计，不能称为整个进程 RSS 上限。
- 1500ms 是读任务池的等待预算，不是整个应用每一步退出的硬实时上界。不可中断解码仍未结束时，持久状态同步后以退出码 75 在 Qt 析构前结束进程；普通窗口关闭只取消票据，不触发此兜底。
- 可移植样本验证真实编码容器、通用 RAW 与 gain-map 呈现，不替代所有相机型号。已有特定 NEF／处理后 DNG 本机回归继续保留，不把私人外置卷样本伪装成可分发资产。
- 原生 GUI 测试要求可激活的桌面窗口；焦点前置条件现在明确检查，原始间歇性失败和复核均如实保存。

## 6. 证据与复现

主要证据位于 `reports/evidence/remediation/`：

| 文件 | 内容 |
|---|---|
| ctest.log | 核心修复阶段 45/45 通过，383.00s |
| ctest-final.log、ctest-final-details.log | 加入交付启动测试后的完整 46 项运行，44 通过、2 个原有 GUI 用例失败；原始记录不覆盖 |
| recheck.log | 相同二进制失败项与交付项 3/3 复核通过 |
| precondition-recheck.log | 明确激活前置条件后，标准 DPI、2× DPI、外置 RAW HDR 和 portable HDR 四项通过 |
| integration-final.log、integration-final-details.log | 最新新增集合通过，23 passed、0 failed、0 skipped |
| portable-hdr-final/*.xml、portable-hdr-summary.json | 必执行函数与真实数据行；非 pass 为 0 |
| static-final.log、gates-final.log | 最新静态契约和门禁反例通过 |
| clang-tidy-final.log | 四个生产源文件的静态诊断记录 |
| source-manifest.json | 本轮生产源、测试、构建与样本的 SHA-256，用于核对审查版本 |

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DFOVELLE_REQUIRE_PORTABLE_HDR=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R 'Fovelle(Remediation|Portable|Deployment)'
```

当前状态：1–10 的修复与验收已闭环；无待确认权限、未执行的关键本轮用例或后台开发任务。
