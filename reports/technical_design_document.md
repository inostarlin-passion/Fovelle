# Fovelle：缩放保持、单实例与色彩信息技术设计

日期：2026-10-10（Asia/Shanghai）。源码基线：`9c9d7657cf8427ec237aa2fe14c133955ab23064`。

第 1–8 节保留上一轮设计基线；本轮已实现的范围与验证结果见第 9 节以及测试完成报告。“唯一方案”指每项需求只选定一套推荐实现，并不意味着数学上不存在其他实现。证明采用明确前提下的状态不变量、归纳法和反证法；不能把形式模型的正确性写成尚未实现代码的测试通过结论。原文件的既有内容完整保留在末尾历史记录中。

## 1. 问题界定与原子化拆解

| 编号 | 原子需求与验收定义 |
|---|---|
| Z1 | 同一窗口连续浏览图片时保持应用的数值缩放比例，例如 120% → 120%，不因新图尺寸、格式或预加载而变成适应窗口比例。 |
| Z2 | 设置中提供明确入口；持久化偏好，兼容已有 `navresetszoom`；覆盖英语、简体中文、繁体中文、西班牙语、日语。 |
| Z3 | 首次打开、会话恢复、加载失败、快速连切、切图期间用户缩放及跨屏 DPI 变化有确定语义。 |
| P1 | 原文“任何情况下只有一个进程”包含直接再次执行二进制、并发 CLI 启动；先判断其可满足性。 |
| P2 | 可实施目标：同一用户的同一 Fovelle 产品身份至多一个持锁并运行 GUI 的主实例；其他启动只转交请求后退出，绝不成为第二主实例。 |
| P3 | 勾选 `reusewindow` 时所有外部打开请求复用目标窗口；未勾选时允许同一主实例创建多个窗口。进程身份与窗口策略分开。 |
| P4 | 并发启动、主实例启动未就绪、退出、崩溃、IPC 故障均不得绕过互斥。CLI 不丢弃其余文件参数。 |
| C1 | 文件信息区显示源色彩空间及来源依据，不把解码工作空间、代理空间或默认假设冒充源空间。 |
| C2 | 显示文件内嵌 ICC 的状态、描述与可用标识；区分未内嵌、损坏、不支持和无法判定。 |
| C3 | 显示实际呈现分支的输出空间；区别代理图、Qt 输出、Metal 输出与显示器配置。 |
| C4 | 切图、动画帧、渲染分支切换、显示器/配置变化时信息与画面一致；标签和状态覆盖五种语言。 |

边界：缩放百分比沿用应用现有 `zoomLevel` 定义，并不承诺在不同屏幕上具有相同物理厘米长度。色彩信息描述可观察的文件声明与渲染管线，不承诺无标记图像的真实创作空间可被恢复。单实例保证的协作域为当前 UID、固定产品身份、使用本协议的版本及可信本地文件系统；非协作旧版、另一用户、管理员破坏锁文件等不在形式保证内。这些边界不等于原文 P1 已满足，见第 4 节不可实现证明。

## 2. 现状证据与检索闭环

### 2.1 仓库核查结果

| 位置（相对于仓库根目录） | 已观察事实及设计影响 |
|---|---|
| `src/qvgraphicsview.cpp::postLoad` | `navigationResetsZoom` 决定是否回到默认计算模式；关闭重置时仍可能调用 `fitOrConstrainImage`。只改默认布尔值不足以证明数值保持。 |
| `src/qvgraphicsview.cpp::zoomAbsolute/resizeEvent` | `lastCalculatedZoomMode/lastCalculatedZoomLevel` 可自动恢复计算模式；即使数值未变，旧模式也可能在下一次 resize 改写缩放。 |
| `src/qvnamespace.h` | 统一合法缩放范围为 `[0.01,64.0]`，即 1%–6400%；有既有 DPI、矢量和高分辨率重采样路径。 |
| `src/settingsmanager.cpp`、`src/qvgraphicsview.cpp::settingsUpdated` | 已有默认 `navresetszoom=true` 及运行时绑定；可直接复用此键，不再引入相反含义的第二键。 |
| `src/main.cpp::main` | 先构造 `QVApplication`；CLI 只打开 `positionalArguments().constFirst()`，并直接 `newWindow()`；未见进程互斥或 IPC 入口。 |
| `src/qvapplication.cpp::processPendingFileOpenEvents` | `reusewindow` 目前仅影响本进程的窗口选择；Finder 事件已通过队列分派。窗口复用不能保证进程唯一。 |
| `src/qvimagecore.cpp::handleColorSpaceConversion` | 未知空间被标记 sRGB，随后可能转换；转换后的 `QImage` 无法作为原始来源事实。 |
| `src/qvimagecore.h::FileDetails` | 现有 `targetColorSpace` 来自 Qt 目标策略；不是所有原生渲染分支的最终输出证明。 |
| `src/qvcocoafunctions.mm::HDRRenderer` | 原生渲染器设置扩展线性 Display P3 输出与 `metalLayer.colorspace`；原生 SDR 也可走此路径。 |
| `src/qvcocoafunctions.h::HDRMetadata` | 有 `colorSpaceName/transferFunction`，但字段名本身不保证其来自文件；需要独立来源记录。 |
| `src/qvcocoafunctions.mm::readNativeImage` | RAW 预览读取与元数据读取有明确先后约束，注释说明部分 Nikon 文件会受影响。新增采集不得破坏该顺序。 |
| `src/mainwindow.cpp::refreshProperties`、`src/qvinfodialog.cpp::setInfo` | 信息对话框目前只接收文件、尺寸、帧数；需要传递色彩快照。 |
| `i18n/`、`src/settingsmanager.cpp::loadTranslations` | 英语源码加四份 `.ts`；目前翻译主要在构造时加载，不能假定新增字符串会自动支持运行时切换。 |
| `CMakeLists.txt` | 已依赖 Qt Network，可复用 `QLocalServer/QLocalSocket`；仍存在 Qt5 回退，不能无条件引入 Qt6.8 新接口。 |

以上是静态核查，不声称已复现功能故障或通过测试。核查时 Git 工作区无已有变更，未发现适用的 `AGENTS.md`。

### 2.2 多跳联网检索、交叉核验与饱和条件

检索日同文档日期。过程按“问题 → 官方 API → 限制/反例 → 回到源码”推进，不以搜索摘要代替已经取得的 API 正文。多源包括不同官方 API 文档、标准机构规范和本地源码；同一厂商多页并非完全独立证据，表中明确其交叉关系。

| 跳次 | 检索与跟进路径 | 核验结果、充分性判断 |
|---|---|---|
| 1：缩放 | Qt `QGraphicsView fitInView` → `QTransform` → 本地 `postLoad/resizeEvent/zoomAbsolute` | 视图计算适配会改写矩阵；矩阵缩放与应用数值状态必须区分。两份 Qt 官方 API 与源码相互验证机制，足以设计固定数值状态。 |
| 2：单实例入口 | Apple `createsNewApplicationInstance` → Qt `QLockFile/QLocalServer` → Apple `posix_spawn` | Launch Services 可复用应用，但直接执行仍能创建进程；`QLockFile` 的保证也要求共同路径与协作。由此否定“只改窗口/Launch Services 即满足 P1”。 |
| 3：并发与故障 | Apple `flock(2)` → FreeBSD `flock(2)` → Qt `removeServer` 警告 → 描述符继承 | 两个系统官方手册核验独占锁语义；macOS 行为以 Apple 为准，FreeBSD 仅交叉佐证共同语义。套接字失联不是锁可抢占的证明。选择固定 inode 的内核锁，避免时限误判活实例。 |
| 4：源/ICC | Qt `QImage setColorSpace` → `QColorSpace iccProfile` → Apple Image I/O/Core Graphics → W3C PNG 第三版、ICC 规范 | 标记、转换、生成 profile 是不同操作。PNG 可通过 sRGB/cICP 等声明而不携带 ICC；因此必须单独记录内嵌来源。Qt、Apple、W3C、ICC 构成多源核验。 |
| 5：输出与 HDR | Apple `CAMetalLayer.colorspace` → WWDC22 EDR → 本地 Metal 输出赋值及 Qt 目标分支 | 层的颜色标记与实际缓冲编码须一致；不能用显示器 ICC 或 Qt 代理目标替代 Metal 输出。Apple API、平台示例及源码一致。 |
| 6：多语言 | Qt 国际化指南 → Designer `LanguageChange/retranslateUi` → 本地翻译加载和目录 | 支持五种语言需要键覆盖与重翻译，不只是添加标签；沿用现有资源链路。 |

补充检索由具体信息缺口触发：ICC 是否一定来自文件、服务器失联是否可删除、CLI 是否能避免创建进程、RAW 采集是否扰动预览。各项已得到可引用依据或显式保留未知态。Open Group 的 `posix_spawn` 当前版与旧版链接访问分别失败/403，未作为已读证据；Apple 官方手册已取得创建进程及执行入口语义，无需用未读来源补数。Apple 部分动态 API 页仅取得简短说明，相关结论同时以官方示例、标准或本地实际调用核验。

停止条件：各原子需求已有状态定义、实现入口、支持来源与可证伪用例；继续同义搜索未能使“任意启动零瞬时第二进程”可实现，也不能从缺失元数据恢复事实。前者为逻辑反例，后者为信息不可辨识，均不能靠无限检索消除。不宣称所有格式的所有元数据都已穷尽；未知情况必须保留为未知，而非凭空补全。

## 3. 需求一：保持数值缩放的唯一推荐方案

### 3.1 决策与状态

唯一选择：**复用 `navresetszoom`，在图片替换事务中保存窗口当前缩放数值，提交新图时清除全部自动适配记忆，以绝对缩放事务恢复该数值。** 选项放在设置的图片浏览区域，正向文案“切换图片时保持缩放比例”；勾选值 `keepZoom = !navresetszoom`。新安装保留已有默认（未勾选），旧用户键值含义不变；点击立即写回原键，设置系统按既有机制持久化。不要另设 `keepzoom`，否则两个键可能互相矛盾。

窗口拥有状态 `(z,m,h,g,u)`：`z` 为合法有限缩放数值；`m` 为当前计算模式；`h` 为历史计算模式及数值；`g` 为当前加载代次；`u` 为用户缩放修订号。保持状态按窗口隔离，不把某窗口的 120% 广播到其他窗口。

事务规则：

1. 所有图片替换入口统一归一化，包含上一张/下一张、首尾、跳转、幻灯片、删除后邻图、拖放、外部请求进入已有窗口及同图重载。首张成功图无可继承数值，按默认策略；会话首次恢复使用其保存状态。
2. 在替换前保存当前**已提交、画面正在使用**的 `z`，冻结计算模式，并清除 `m/h`；同时取消旧缩放动画、延迟适配及旧图精细化回调。不能只保存标题栏四舍五入后的百分比。
3. 请求记录 `(generation, savedZoom, userRevision)`。快速 A→B→C 且 B 尚未显示时，以仍在显示的 A 的有效数值为准；不能拿 B 的中间初始化值覆盖快照。已有请求 ID 机制须延伸到视图几何回调。
4. 切图等待期间用户若继续缩放，同步更新有效 `z` 与 `u`；提交时若修订号已变，以最新有效 `z` 为准。显式“适应窗口/原始大小”同样属于用户意图：对当前图计算并提交数值，下一张保持这一数值，而不继承计算公式。
5. 只有最新请求成功才能安装图像。安装后，在允许第一张新图代理帧或 Metal 帧显示之前，以同一几何事务清除 `m/h`，更新新图尺寸、DPI 与场景范围，调用既有 `commitZoomImmediately(makeZoomPlan(z,...))`，将新图中心作为锚点。原生层使用该事务的同一内容矩形。
6. 不通过普通 `zoomAbsolute` 直接恢复：它存在 `shouldRestoreCalculatedZoom` 分支。需要专用恢复入口或先完整清除该分支的历史状态，即便 `z` 未变也必须重建新图变换，不能被 `!isChanging` 提前返回跳过。
7. 保持模式的安装/布局只约束滚动范围和锚点，不重新求适应比例；窗口自动调整、滚动条、全屏和跨屏 resize 不得恢复已清除的计算模式。用户主动再次选择计算模式可在当前图生效；下一次切图再冻结为数值。
8. 失败不改有效缩放。若沿用当前错误占位图行为，应在独立字段保留最后成功图的 `z`；不能让占位图适配值成为下一张继承来源。关闭图片清除继承状态，新窗口从默认开始。

既有 DPI 和重采样算法保留：保持的是应用数值，图像代理换成原生层、矢量瓦片变清晰等精细化不能成为第二次缩放。有关“适应窗口改变矩阵”及缩放映射的 API 依据见 [QGraphicsView](https://doc.qt.io/qt-6/qgraphicsview.html#fitInView)、[QTransform](https://doc.qt.io/qt-6/qtransform.html)。

### 3.2 正确性证明

定义裁剪函数 `B(x)=min(64,max(0.01,x))`。前提：GUI 状态在主线程串行提交；旧代回调经过代次检查；所有被继承的缩放已合法化为有限数值；安装事务在可见帧前完成。

**定理 Z-A（比例保持）。** 对连续成功显示序列 `I₀,…,Iₙ`，若保持开启且期间没有显式缩放输入，则所有 `zᵢ=z₀`。

证明：初态合法。归纳假设 `zᵢ=z₀`。下一次提交读取 `zᵢ`，删除计算模式并赋值 `zᵢ₊₁=B(zᵢ)`。因 `zᵢ∈[0.01,64]`，`B(zᵢ)=zᵢ`，故 `zᵢ₊₁=z₀`。任意 resize/精细化事件不改变保持状态中的 `z`；归纳成立。120% 对应 `z₀=1.2`，因此下一张仍为 120%。若有用户输入，则从该输入最后提交的 `z*` 开始同样归纳。

**定理 Z-B（几何一致）。** 在未附加旋转的坐标中，设源图到设备坐标映射为 `p_d=d·a·z·p_s+t`，其中 `d` 为 DPR，`a` 为应用已有 DPI 因子。非退化位移有 `||Δp_d||/(d·a·||Δp_s||)=z`。图像尺寸只影响可见范围与平移 `t`，不影响该比值。代理重采样倍率为 `r` 时，用代理内像素坐标 `p'=r·p_s` 并以逆倍率补偿，则 `(d·a·z/r)·p'=d·a·z·p_s`，故代理与原生表达同一比例。屏幕变化允许 `d/a` 改变，数值 `z` 不变。像素对齐可有既有量化误差，此定理不是逐像素完全相同的承诺。

**定理 Z-C（异步不回滚）。** 安装条件为 `g_result=g_current`。任何旧结果的代次不等，因此不能写 `z/m/h`；用户修订号使保存值不会覆盖较新的缩放输入。失败分支不写有效值。因此乱序返回、失败和预加载均不破坏 Z-A。

反证排查：只关闭 `navigationResetsZoom` 但保留适应模式，会在两张尺寸不同图片上重新求比例；只调用数值相同的 `zoomAbsolute` 可能提前返回；只还原 transform 而不还原 `zoomLevel` 会令标题与后续输入失配。以上三种反例都由专用提交事务消除。

实现落点：`qvoptionsdialog.ui/.cpp` 加设置绑定；`qvgraphicsview.h/.cpp` 增加替换事务与代次约束；必要时由 `QVImageCore` 传递加载身份；现有 `navresetszoom` 会话字段统一沿用。完整翻译见第 6 节。

## 4. 需求二：单实例的唯一推荐方案及不可实现边界

### 4.1 原文 P1 的不可实现证明

设已有 Fovelle 进程 A，用户或脚本用系统进程创建接口直接执行同一可执行文件，生成 B。系统成功创建 B 的时刻为 `t_c`，B 执行第一条应用检查的时刻为 `t_e`。创建先于应用指令执行，因此在检查之前 A、B 已同时存在；即使检测后立即退出，仍存在两个进程的可达状态。Apple 的 [posix_spawn(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/posix_spawn.2.html)明确区分创建新进程与进入 `main`。

于是，“允许任意直接 CLI 执行”与“任意时刻系统只存在一个 Fovelle 进程”相矛盾。除此之外，未启动时或崩溃后也不能保证数量恰好等于 1。没有应用内方案能证明原文的无条件命题；本报告不能伪造其正确性证明。

Launch Services 的 [createsNewApplicationInstance](https://developer.apple.com/documentation/appkit/nsworkspace/openconfiguration/createsnewapplicationinstance)默认复用运行实例，适合正常打开路径，但不是直接执行的内核禁令。更换 CLI 为单独 helper 或 shell wrapper 只改变进程分类，仍不满足“包括任意直接执行”的字面要求，故不作为第二套推荐方案。

**交付判定：P1 按字面不可实现；以下唯一推荐实现满足 P2–P4。对 P1 的产品验收必须显式改为“至多一个 GUI 主实例，重复启动转交后退出”，不能默认为原要求已经满足。**

### 4.2 唯一实现：固定内核互斥锁 + 本地请求转交

进程唯一性独立于 `reusewindow`，始终启用；该设置仅控制主进程内窗口路由。这样勾选时无需查杀旧实例，取消勾选也不会制造第二主进程。未勾选的兼容行为是多窗口，仍为一个 GUI 主进程。

增加 RAII `InstanceCoordinator`：在 `main` 设置产品身份后、设置迁移和 `QVApplication` 构造之前，使用原生 `open/flock` 完成选主。锁身份采用固定产品 ID `io.github.inostarlin-passion.Fovelle` 与 UID，不含应用路径、构建版本或 CWD；同用户的多个安装副本须竞争同一锁。目录使用用户本地 `~/Library/Application Support/Fovelle/instance/`，权限 0700；文件 0600。禁止将此目录重定向到网络盘或按版本分目录；检查符号链接、所有者、文件类型，失败则报错退出。

锁文件永久保留，同一 inode 使用 `flock(fd,LOCK_EX|LOCK_NB)`；不通过删除锁文件恢复。持锁描述符设 `FD_CLOEXEC`，禁止 fork 后继续运行应用逻辑；启动 Ghostscript 等子程序必须关闭该描述符。锁在 GUI、解码任务、IPC 全部停止后才关闭，主实例有效期与持锁期一致。互斥语义由 [Apple flock(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/flock.2.html)与 [FreeBSD flock(2)](https://man.freebsd.org/cgi/man.cgi?query=flock&sektion=2&format=html)交叉核验；后者不用于推断 Apple 未承诺的实现细节。

启动状态机：

```text
Candidate --成功持锁--> OwnerStarting --IPC监听成功--> OwnerReady
Candidate --锁被占用--> Client --请求确认--> Exit
Client --连接失败--> 有界重试连接/尝试持锁
Candidate/Client --权限/协议/预算错误--> ErrorExit
OwnerReady --> Draining --> GUI与任务停止 --> 关闭IPC --> 释放锁 --> Exit
```

只有 `OwnerStarting/OwnerReady/Draining` 可以持有或构造 GUI；客户端不构造 `QVApplication`、主窗口、菜单、渲染器和更新器。若 Qt 客户端事件驱动需要 `QCoreApplication`，使用独立函数作用域构造并销毁，成功转主后再构造唯一的 `QVApplication`；不能同时存在两个 Qt application 对象。

持锁者清理旧 socket 并监听固定私有路径，失败则不进入 GUI。IPC 采用 `QLocalServer/QLocalSocket`，限制同 UID。仅持锁者可调用 `removeServer`；退出时先关闭监听、清理自身 socket，再释放锁。Qt 官方明确提醒不能删除活实例的 socket，见 [QLocalServer::removeServer](https://doc.qt.io/qt-6/qlocalserver.html#removeServer)。这里用锁判定所有权，不以“连接超时”认定主进程已死。

选择内核锁而不选择基于时间的文件过期策略，是为了让挂起的主进程继续阻止第二主实例。作为排除依据，[QLockFile](https://doc.qt.io/qt-6/qlockfile.html)也要求所有参与者使用相同路径，长期锁需特别处理 stale 时间；普通超时不是所有权转移依据。

### 4.3 请求、窗口与故障语义

请求帧定义为 `uint32 length + UTF-8 JSON`，上限 1 MiB，含 `protocolVersion/requestId/operation/files[]`；文件列表上限 4096，零个文件为激活请求。按实际 UTF-8 字节数校验，超限显式拒绝，不截断。按长度缓冲分包、粘包，拒绝未知操作与协议版本；路径在客户端基于其 CWD 变为绝对路径，URL 保留为 URL，不通过 shell 拼接转发。支持空格、Unicode 和 `--` 后的横杠文件名。CLI 解析全部位置参数，帮助/版本请求直接输出后退出。

主实例 GUI 线程将已验证请求按接收顺序加入统一队列；Finder `QFileOpenEvent`、菜单、拖放与 CLI 转交进入同一窗口策略函数。主实例的当前设置是唯一裁决依据，客户端不使用自己读到的陈旧 `reusewindow`。

- 勾选：选择最近活动且未关闭的主窗口；没有则新建一个。所有该请求中的文件依序在此窗口打开，最后一个成为当前图；前面的加载可以被取消，但每个路径均需记录接受/失败结果。显式“新建窗口”不被解释成创建新进程。
- 未勾选：依既有空窗口复用逻辑为各文件分配窗口；都在同一主进程中。
- 已有多个窗口再勾选：后续请求复用上述目标窗口，不擅自关闭已有窗口。需求是进程唯一，不能额外推断必须只留一个窗口。

确认响应表示“主实例已将请求加入内存队列”，不是“图片解码成功”；后续加载失败按既有 UI 呈现。相同 `requestId` 的重试在同一主实例存活期去重，重复请求返回原确认。去重表最多 4096 项且不驱逐已确认项；满时拒绝新请求并给出错误，避免既声称无限期去重又使用无约束内存。可在未来独立设计持久队列，但本方案不承诺跨崩溃 exactly-once。

客户端总等待预算 10 秒，使用事件驱动退避，例如 20/40/80/160/320 ms 后封顶 320 ms；可重试连接或重新非阻塞取锁。主实例挂起且锁仍占用时，客户端报错非零退出，不能“先开一个备用窗口”。主进程死亡后内核释放锁（以无子进程遗留描述符为前提），一个竞争者选主；其它继续转交。退出期间未确认请求由客户端重试；已确认请求随后遇崩溃可丢失，必须明确此可靠性边界。

启动更新前的旧版本若不遵守协议，不能由新版本互斥锁约束；升级说明需要求退出旧版。不得为了假装实现“任何情况”按进程名杀死用户实例。协议不同则报告错误，不能按版本创建不同锁绕开互斥。

### 4.4 数学证明

设 `L(t)` 为同一锁文件 inode 的独占持锁进程集合，`G(t)` 为已获准构造且尚未完全停止 GUI 的主实例集合。

**定理 P-A（主实例安全性）。** 假设所有参与者遵守协议、锁 inode 不被替换、内核锁正常工作、锁不被子进程意外释放，则 `|G(t)|≤1`。

证明：内核独占语义给出 `|L(t)|≤1`。进入 GUI 必须先持锁，GUI 完全停止前不放锁，故 `G(t)⊆L(t)`，推出 `|G(t)|≤1`。若有两个主实例同时存在，则二者同时持同一独占锁，与锁语义矛盾。并发冷启动、`open -n` 或直接 CLI 都只能增加候选/客户端数，不能增加 `G`。这不是总 OS 进程数 `P(t)` 的证明；事实上 `|P(t)|` 可大于 1。

**定理 P-B（窗口策略一致）。** 对主线程顺序处理的请求 `r₁,…,rₙ`，每次读取当前 `reusewindow` 并应用同一选择函数。勾选时函数给出唯一活动目标或创建唯一目标，随后整个批次复用该目标，因此该批外部请求不会额外创建第二个目标窗口。没有跨进程窗口路由，因为 P-A 已保证只有一个拥有窗口的主实例。已存在多窗口不违反本命题。

**定理 P-C（不因故障破坏安全性）。** 锁失败只进入客户端/退出；IPC 超时不能进入 GUI；socket 清理仅由持锁者执行；故任何失败转移均保持 `G⊆L`。证明逐个检查状态机所有转移即可。特别是删除锁路径会使两个进程锁住不同 inode，此反例由“锁文件永不 unlink”排除。

**活性条件。** 若调度公平、主实例在等待预算内完成监听并处理请求、通道可用，则客户端最终获得确认或在主实例死亡后竞争选主。永久挂起、资源耗尽或持续崩溃时不能保证成功，但仍有界报错且不破坏安全性。ACK 后崩溃可丢内存队列，因此不声称任意故障下不丢请求。

实现落点：新增 `instancecoordinator.h/.cpp`；`main.cpp` 重排选主、迁移与 GUI 构造；`qvapplication.h/.cpp` 统一请求路由；构建文件登记新源码；原选项标签保留并补充工具提示“外部打开请求复用当前窗口；命令行启动会转交给运行中的应用”。

## 5. 需求三：带来源证据的色彩快照是唯一推荐方案

### 5.1 数据契约

唯一选择：**在转换前采集不可变源元数据，由实际渲染分支提交输出描述，信息区只显示两者组成的同代快照。** 不从转换后的代理图倒推源信息，也不按当前设置推断实际输出。

```text
SourceColorInfo {
  fileIdentity, frameOrImageIndex,
  declaredSpace, primaries, transfer, sourceModel,
  declarationOrigin: ICC | Container | RAW | DecoderOnly | Unspecified,
  embeddedICC: PresentValid | PresentInvalid | PresentUnsupported |
               AbsentVerified | Unknown,
  rawICCBytes?, profileDescription?, parseError?, evidenceLocation
}
RenderColorInfo {
  imageGeneration, frameRevision, renderRevision,
  activeBranch: Qt | Metal | Vector | None,
  actualSurfaceSpace, actualSurfaceProfile?, assumedInputSpace?,
  displayProfile?, presentationStage: Proxy | Final | Pending
}
ColorInfoSnapshot = (SourceColorInfo, RenderColorInfo)
```

所有元数据字符串是数据：以纯文本显示，限制长度，不能被 QLabel 当富文本解析。显示三行“源色彩空间”“内嵌 ICC”“当前输出空间”；详情可展示传递函数、声明来源、原始 ICC 长度/诊断摘要、显示器 profile。标准技术名称（sRGB、Display P3、PQ、HLG、ICC）及文件提供的 profile 描述不自行翻译；标签、来源、状态、说明通过翻译键生成。

源定义为当前显示的主图/帧所对应的文件声明，RAW 为相机原始数据模型及可取得的声明；预览图另附“RAW 预览”来源，不冒充 RAW 传感器有 P3 ICC。多页面/多帧文件以实际页/帧索引采集，不能默认所有帧与第 0 帧相同。若现有解码器不能提供独立声明，显示“未知（仅解码器提供空间：…）”。

### 5.2 源与 ICC 的采集规则

采集位于 `QVImageLoader::readFile` / 原生桥接中，早于 sRGB 假设、颜色转换和缩略图替代。源快照跟随 `NativeImageReadResult → QVImageLoader::Result → FileDetails`，预加载缓存同样保存源快照；窗口输出状态不缓存为文件事实。RAW 采集遵守现有先读取预览、后访问部分 Image I/O 元数据的顺序，避免改变 Nikon 目录选择。

文件 ICC 的存在必须有容器层证据：完整解析 JPEG ICC APP2 分片、PNG iCCP、TIFF ICC tag、WebP ICCP，以及各已支持解码器能明确给出的 HEIF/AVIF、JPEG XL 等原始 profile 字段。以规范规定的图像项、分片和长度规则读取，使用受限解压（默认 profile 上限 16 MiB）、边界检查和错误态；其余格式或 API 不能提供 provenance 时为 `Unknown`。不得仅扫描文件字节寻找字符串，也不能仅因 `CGImageSourceCopyPropertiesAtIndex` 有 profile 名称便宣称已取到原始 ICC。

容器规则追加核验：JPEG/TIFF 采用 [ICC 官方嵌入要求](https://www.color.org/profile_embedding/)及其指向的 ICC 规范附录；WebP 的 ICCP 由 [Google 容器规范](https://developers.google.com/speed/webp/docs/riff_container)与 [RFC 9649](https://www.rfc-editor.org/rfc/rfc9649.html)交叉核验。HEIF/AVIF、JPEG XL 的具体适配器实现仍需逐项核对其格式与解码器接口；接口不能明确提供原始内嵌证据时，本设计已经规定返回 Unknown，不把尚未核实的支持能力写成事实。

源采集与像素解码必须对应同一文件版本：优先共享同一打开的文件对象/不可变输入缓冲；只能分别打开时，至少比较前后文件身份、大小与修改时间，变化则整次作废重读。仅靠大小/时间不能排除恶意同值替换，因此形式证明以前者或文件读取期间不变为前提，不能据后者声称绝对一致。

`AbsentVerified` 只在对应图像项的合法容器结构已完整检查且确无 ICC 时成立；损坏结构、读取被限额中止或缺少适配器，均不能归为无 ICC。初版允许未覆盖格式显示未知，不承诺每种文件都能给出肯定名称。API/规范证据：[Apple Image Properties](https://developer.apple.com/documentation/imageio/image-properties)、[PNG 第三版色彩块](https://www.w3.org/TR/png-3/#11iCCP)、[ICC 规范入口](https://www.color.org/icc_specs2/)。

`rawICCBytes` 仅存实际提取内容；描述读取 ICC `desc/mluc` 并标注语言，缺失描述显示“内嵌 ICC（无描述）”。解析不支持与结构损坏分开：以容器/ICC 结构校验确定是否损坏，再记录渲染库能否支持，不把 `QColorSpace::isValid()==false` 一概判为损坏。可显示 SHA-256 用于诊断，但正确性判等使用原始字节，不将哈希绝无碰撞当作证明前提。

源空间选择有证据优先级：按具体格式规范解析声明，多个声明不一致时显示冲突并注明实际解码器采用的空间，不能简单规定所有格式一律 ICC 优先。例如 PNG 的 cICP、iCCP、sRGB 等遵从其规范，不套用 JPEG 规则。ICC 缺失不等于源空间未知；明确 sRGB 或 cICP 声明仍可识别。未声明的 RGB 显示“未指定（显示时按 sRGB 解释）”，而非“源：sRGB”。

[QColorSpace::iccProfile](https://doc.qt.io/qt-6/qcolorspace.html#iccProfile)可能返回生成 profile；[CGColorSpaceCopyICCData](https://developer.apple.com/documentation/coregraphics/cgcolorspace/copyiccdata())描述的是一个颜色空间对象的 profile。因此两者可用于管线诊断，却不能独立证明文件内嵌。此结论与 PNG 可以不使用 iCCP 而声明空间的标准事实相互核验。

### 5.3 输出必须由实际呈现路径报告

| 实际呈现路径 | 当前输出空间的数据来源 |
|---|---|
| Qt 光栅/动画 | 在成功转换后、提交 pixmap 前读取实际图像空间；目标配置无效或转换失败则记录有效回退/错误，不能显示期望目标。动画逐帧记录。 |
| 原生 Metal HDR/SDR | 从当前渲染器最终提交 drawable 时的 `outputColorSpace` 与 `CAMetalLayer.colorspace` 生成描述，验证两者一致；现有路径通常为扩展线性 Display P3，而非 Qt 代理目标。 |
| 原生图就绪前的 Qt 代理 | 显示代理实际空间，并加“预览”；已准备但未显示的原生帧不能提前成为当前输出。 |
| SVG/EPS 矢量 | 报告实际光栅化/合成表面空间；文档内部可能有多种色彩模型，源空间允许“混合/由文档定义”或未知，不能统一猜为 sRGB。 |
| 无图/加载错误 | 显示“无输出”；不保留上一张颜色信息。独立保留缩放状态不意味着保留旧色彩信息。 |

“当前输出空间”定义为 Fovelle 交给系统合成器的当前可见表面编码，不是显示器物理色域；显示器配置作为附加项。Metal 输出即使在 SDR 模式或 headroom 变化时仍可能保持同一空间，不能因亮度状态变化就显示 sRGB。依据：[CAMetalLayer.colorspace](https://developer.apple.com/documentation/quartzcore/cametallayer/colorspace)、[WWDC22 EDR 示例](https://developer.apple.com/videos/play/wwdc2022/10113/)。

移动窗口、主显示屏改变、颜色配置改变或输出设置改变时，触发输出重新解析与渲染修订：Qt 必须保留或重新取得未转换源图，不能把已转显示器 A 的低位深结果反复转往 B；原生层从实际输出对象重新报告。使用窗口屏幕变更信号及原生 screen/profile 通知，必要时按已有呈现定时检查 profile 内容变化。检查不表示已应用，只有对应新表面实际呈现后才切换输出记录。

每次文件请求携带 `imageGeneration`，每帧携带 `frameRevision`，每次输出变更携带 `renderRevision`。暂未提交新图时可继续显示旧图及其旧快照，但应显示加载状态；新图开始显示时原子替换。原生异步 present 完成回调只有三个身份都匹配时才能更新快照；信息对话框每次读取完整值对象，延迟重绘读取最新快照，不能分别读取三个可变字段。

### 5.4 正确性证明

**定理 C-A（来源真实性）。** 设文件/图像项字节为 `F`，容器提取器为 `E`，源快照 `S=E(F)`；提取器对未知、无声明、损坏显式建模且不调用生成 profile 补全，转换函数仅作用于像素 `T(P)` 而不修改 `S`。那么任意次颜色转换后源快照仍为 `E(F)`。证明：初始成立；每次转换的写集不含 `S`，由归纳法恒成立。`embeddedICC=Present*` 仅由提取器观察到原始 profile 转移得到，故不会因 Qt 合成 profile 产生假阳性。解析器符合格式规范是必要前提，须以独立文件夹具检验。

**不可辨识性说明。** 两份像素数值相同、均无颜色声明的文件，可分别由 sRGB 与 Adobe RGB 创作流程产生。观察文件字节相同而真实语义不同，任意仅以文件为输入的算法都返回相同结果，不能对两者均恢复真实空间。因此 `Unspecified/Unknown` 是正确结果的一部分，猜测不能构成证明。

**定理 C-B（输出真实性）。** 设当前实际呈现表面对象为 `Rᵥ`，输出描述函数为 `D`。通过同一次提交将图像和元数据绑定，并只在该修订确认呈现时发布，则信息行 `O=D(Rᵥ)`。Qt 与 Metal 每个分支都从已应用对象生成 `D`，故分支选择的并集仍成立。若空间不可查询则 `D=Unknown`，不伪造目标。源空间 `S` 与输出 `O` 可以不同，不影响命题。

**定理 C-C（代次一致）。** 令快照键为 `(imageGeneration,frameRevision,renderRevision)`。只接受与可见提交键相同的更新，其他键的回调均丢弃；UI 读取一个不可变快照。因而 UI 不可能合法组合 A 图源 ICC、B 图输出空间和 C 帧的状态。若接受较旧回调或分别读取字段，会构造出此反例，故三个键和原子快照均为必要约束。

**证明范围。** 本节证明显示字段忠实于观测来源及应用输出对象，不证明物理显示颜色误差为零，也不证明平台解码器/ICC 变换无缺陷。改变 metadata 标签不等于改变像素，见 [QImage::setColorSpace](https://doc.qt.io/qt-6/qimage.html#setColorSpace)；该 API 与真实 `convertToColorSpace` 必须区分。

实现落点：在 `qvcocoafunctions.h/.mm` 增加源采集与渲染呈现描述接口；`qvimageloader.h/.cpp`、`qvimagecore.h/.cpp` 扩展快照传输；`mainwindow.cpp::refreshProperties` 改传值对象；`qvinfodialog.h/.cpp/.ui` 增加可选择文本的三行和详情。不要用 `HDRMetadata.colorSpaceName` 的旧值直接替代来源模型。

## 6. 两项新增 UI 的统一多语言方案及证明

唯一语言方案为现有 Qt `tr/.ts/.qm` 资源链路：在各实际类上下文添加文字，用 `lupdate` 提取、人工填写四种目标语言、`lrelease` 编译，英语继续采用源文本。设置复选框与 `navresetszoom` 反向绑定。以下是固定主文案：

| 英语源键 | 简体中文 | 繁体中文 | 西班牙语 | 日语 |
|---|---|---|---|---|
| Keep zoom level when switching images | 切换图片时保持缩放比例 | 切換圖片時保持縮放比例 | Mantener el nivel de zoom al cambiar de imagen | 画像の切り替え時にズーム倍率を維持 |
| Source color space | 源色彩空间 | 來源色彩空間 | Espacio de color de origen | 元の色空間 |
| Embedded ICC profile | 内嵌 ICC 配置文件 | 內嵌 ICC 描述檔 | Perfil ICC incrustado | 埋め込み ICC プロファイル |
| Current output color space | 当前输出色彩空间 | 目前輸出色彩空間 | Espacio de color de salida actual | 現在の出力色空間 |
| Not embedded | 未内嵌 | 未內嵌 | No incrustado | 埋め込みなし |
| Unknown | 未知 | 未知 | Desconocido | 不明 |
| Invalid profile | 配置文件损坏 | 描述檔損毀 | Perfil no válido | 無効なプロファイル |
| Unsupported profile | 不支持的配置文件 | 不支援的描述檔 | Perfil no compatible | 未対応のプロファイル |
| Unspecified (assumed sRGB for display) | 未指定（显示时按 sRGB 解释） | 未指定（顯示時以 sRGB 解讀） | Sin especificar (se asume sRGB para la visualización) | 未指定（表示時は sRGB として解釈） |
| Preview | 预览 | 預覽 | Vista previa | プレビュー |
| No output | 无输出 | 無輸出 | Sin salida | 出力なし |

其余详情文字、工具提示、错误、冲突/RAW/解码器来源等均须枚举为翻译键，禁止拼接未标记中文或英文短语。动态描述以 `%1/%2` 参数模板插入；文件 profile 名称原样显示不计为缺少翻译。数值按应用选择的语言映射 `QLocale` 格式化，技术 ICC 字节/标准标识可保持标准格式。

支持语言即时切换：`SettingsManager` 切换时先移除旧翻译器，再加载新资源；切回英语也须移除旧翻译器。对话框处理 `LanguageChange`，调用 `retranslateUi` 并从快照重新生成状态文字，不缓存翻译后的枚举名；调整布局以容纳长西语和日语。依据分别为 [Qt 翻译指南](https://doc.qt.io/qt-6/i18n-source-translation.html#prepare-for-dynamic-language-changes)与 [Designer UI 语言切换](https://doc.qt.io/qt-6.8/designer-using-a-ui-file.html#reacting-to-language-changes)。

形式证明：设新增 UI 所有非技术常量文本键集合为有限集合 `K`，语言集合 `L={en,zh_Hans,zh_Hant,es,ja}`，译文映射为 `T:L×K→String`。资源检查要求每个 `(l,k)` 存在非空、已完成译文且占位符集合与英文相同；英语由源键定义。UI 的任何可达标签/状态只由某个 `k∈K` 经 `T(l,k)` 与不翻译技术数据格式化而成，所以全部可达状态有对应语言文本。语言切换后重新计算这些函数值，旧语言不再残留。此证明以键枚举完整和正确上下文为前提，不能仅凭译文表替代资源覆盖检查；翻译是否自然仍需要语言审校。

## 7. 逆向证伪与待实施验收

下表是**计划与判定标准**，本次未运行功能测试。实施时先构造反例，在修复前观察失败，再在同一输入上验证；期望值取独立状态/实际表面，不把待测函数返回值当正确性依据。

| 用例 | 试图推翻的命题与验收证据 |
|---|---|
| Z-T1 | 大小图、横竖图、RAW/HDR/SDR/SVG 混切，从 120% 开始；首张可见代理帧、最终原生帧、随后 resize 的 `z` 均为原保存值。数值容差取 `1e-12·max(1,|z|)`，并检查无 fit 模式复活；几何以既有像素对齐误差单独判定。 |
| Z-T2 | 初始适应模式恰为 120%，下一张尺寸翻倍；必须保持 120%，而非重新适应得到 60%。专门击穿“仅取消重置”的实现。 |
| Z-T3 | A→B→C，故意让 B 最后返回；C 不能被 B 覆盖。加载中缩放到 150%，最终图保持 150%；坏图之后仍继承最后有效值。 |
| Z-T4 | 首张、会话恢复、全屏、DPR 1/2、1%/6400%、设置开关与重启；验证明确的优先级及键兼容，不从标题舍入值恢复。 |
| P-T1 | 100 次并发直接 CLI、Finder、`open -n`、多个 bundle 路径；用每个实例生命周期日志及持锁区间验证主实例区间不相交。允许瞬时客户端，不能只以低频 `pgrep` 证明 P1。 |
| P-T2 | 持锁后延迟监听超过客户端预算；客户端失败非零退出且无窗口。主实例 SIGSTOP 时禁止因超时新开 GUI。 |
| P-T3 | 正常退出、持锁后/监听后/ACK 前后分别 SIGKILL；恢复时只能有一个持锁者，锁 inode 不变，旧 socket 安全清理；明确 ACK 后崩溃不保证交付。 |
| P-T4 | IPC 半包/粘包/错误版本/过长输入/确认丢失；同一 ID 重试不重复路由，去重表满时拒绝；含空格、Unicode、URL、相对路径和多个文件参数均正确。 |
| P-T5 | 勾选/取消、已有多个窗口、会话恢复、主实例退出中收到请求；检查每批次使用主进程当前策略，且会话恢复不另启进程。 |
| C-T1 | 独立制作并检查容器字节的有 ICC、无 ICC+sRGB、无 ICC+cICP、未标记、损坏 ICC、结构合法但库不支持的样本；信息与文件层证据一致。不得用应用自己的合成 profile 验证内嵌事实。 |
| C-T2 | Adobe RGB 源图转换到 sRGB、P3；源空间与原始 ICC 不变，输出随实际提交分支变化；检查原始 profile 字节一致。 |
| C-T3 | 原生 SDR/HDR、代理→Metal、矢量、动画不同帧；通过独立调试探针读取实际表面 profile 与层标记，信息必须与当前已呈现对象一致。 |
| C-T4 | 快速切图、移至另一屏幕、更新显示器 profile、旧 present 回调延迟返回；快照三个身份一致，无旧图 ICC 残留。 |
| C-T5 | Nikon RAW 预览与完整解码、混合色彩矢量及缺少元数据适配器的格式；无法判定就显示未知，新增读取不改变现有方向/预览行为。 |
| L-T1 | 对 `L×K` 检查译文覆盖、上下文、占位符，编译 QM；打开设置和信息区逐语言切换再切回英语，长文本无截断、状态不混用旧译文。 |

## 8. 实施顺序、风险与完成口径

先实现公共语言键与值对象，再实现缩放事务、启动协调器、色彩采集/呈现快照；每项分别执行第 7 节的反例验收，最后跑现有相关 Qt/CTest 回归。测试范围应覆盖生产路径，不需要为文档改动运行整个应用测试。

主要风险及约束：缩放历史模式被隐藏回调恢复；持锁描述符被子进程继承；升级时旧版不协作；来源读取干扰 RAW 预览；ICC 提取器把无法读取误报为未内嵌；Qt5/Qt6 API 差异。分别以专用事务、CLOEXEC/子进程检查、升级边界、保留读取顺序、显式 Unknown 和版本兼容实现约束应对。内嵌 profile 大小上限属于资源策略，不应导致图像无法显示，只使详情呈现读取受限状态。

本次完成口径：三个需求均给出一个推荐设计及对应数学论证；P1 同时给出不可实现证明并明确可实施替代保证，未宣称原文可无条件满足。联网与本地证据已足以选择设计；具体实现正确性、格式适配覆盖率、物理多显示器表现以及语言自然度仍需实施后验证。以下历史内容与本设计的测试状态分离。

---

## 9. 2026-10-10 实施增补：本轮只实现缩放保持与色彩信息

### 9.1 原子验收与实现对应

本轮原子标准为 Z1 数值保持、Z2 设置持久化、Z3 失败/乱序处理、Z4 窗口隔离/会话/关闭保持、C1 源证据、C2 ICC 状态与资源边界、C3 实际输出路径、C4 信息区刷新/清空、L1 静态资源覆盖、L2 实际 QM 与重翻译。每条标准的六部分用例和代码入口写入 `reports/test_case_specification.md`、`tests/zoom_color_cases.json`；本轮不实施第 4 节的单实例设计。

| 生产入口 | 已实现行为 |
|---|---|
| `QVOptionsDialog` | 浏览设置增加保持缩放复选框，反向绑定既有 `navresetszoom`，立即持久化，旧默认不变；加入现有第四设置组及自然尺寸计算。 |
| `QVGraphicsView::beforeLoad/postLoad` | 在已有 requestId 筛选后的安装时刻读取最新数值；清除计算模式及历史记忆；调用既有同步提交，数值相同时仍重建新图几何；坏图保留最后成功比例，关闭图片清除继承。 |
| `colorinformation.h/.cpp` | 新增值对象与容器证据提取器，读取 JPEG APP2 分片、PNG iCCP/sRGB/cICP、单页经典 TIFF 34675、WebP ICCP。不调用生成 ICC 的 API 推测内嵌；检查长度、偏移、PNG 颜色块 CRC、分片重复/缺失及 ICC 头/标签表边界。 |
| `QVImageLoader` | 转换前取得源记录并与解码缓存同行；同时保留解码开始的文件大小/时间身份，让原有 `jobFinished` 在读取期间文件变化时重试。RAW 与矢量单独标明来源类型；未知格式保留 Unknown。 |
| `QVImageCore` | 记录成功转换后实际 QImage 空间；动画每帧更新输出空间；源记录不因目标转换改变。 |
| `HDRRenderer` | 验证实际 Metal 层 colorspace 与渲染输出对象一致，向诊断快照提供本渲染器固定的扩展线性 Display P3 名称。 |
| `QVGraphicsView::colorInformation` | 主线程同步读取当前 FileDetails 和呈现状态；只有实际原生帧已呈现且代理已隐藏，才报告 Metal 输出；否则报告实际 Qt 代理及 Preview。 |
| `QVInfoDialog` | 三行可选择的纯文本，显示来源/ICC 状态、profile 描述与字节数、当前输出；可见期间约每 100 ms 获取完整值对象，无图显示 No output；空文件与 LanguageChange 均安全。macOS 表单值列使用可用宽度，防止换行值被裁切。 |
| `i18n/*.ts` 与构建 | 英语源码及简/繁/西/日 QM；新增标签和状态均翻译；CMake 与 qmake 登记新源文件和 zlib。 |

### 9.2 本轮检索与证伪闭环

在前文检索基础上重新读取 Qt `fitInView`、`QColorSpace::iccProfile`，沿容器嵌入规则继续读取 ICC 官方要求、W3C PNG 第三版、Google WebP 与 RFC 9649；沿输出路径复查 Apple EDR 示例；沿语言更新复查 Qt LanguageChange；针对解压限额补读 zlib 官方接口。来源与可观察代码相互核验，避免依据旧报告直接假定实现已经正确。

- 缩放：适应窗口会重新求矩阵（Qt）→ 源码仍保留计算模式 → 数值保持必须清除此模式。恢复原缩放源码并运行相同新测试，旧实现把 0.5 改为 0.9028571428571428；修复后同一测试通过。因采用已有的结果筛选，快照不必在请求发起时另造一个异步代次系统，加载期间最后用户输入自然进入提交值。
- ICC：Qt 可生成 profile → 非空 profile 不能证明文件内嵌 → 以容器原始字节为证据；独立 PNG 写入器与手工分片/偏移夹具验证源记录，Qt 生成的 sRGB profile 不能改变无 ICC 文件的 Absent 状态。
- 输出：原生 SDR 也使用 Metal 的扩展线性输出 → Qt 设置 sRGB 并不意味着所有可见表面为 sRGB → P3 PNG 经原生显示时报告扩展线性 P3，切到 Qt XPM 后报告实际 sRGB。
- 资源与错误：有限目标缓冲调用 zlib `uncompress`；输出超过 16 MiB 得到受限未知态，损坏/不支持状态与无 ICC 分离；未知或多页选择不冒充首图信息。

补充一手依据：[ICC 嵌入要求](https://www.color.org/profile_embedding/)、[PNG 颜色元数据](https://www.w3.org/TR/png-3/#11iCCP)、[WebP 容器](https://developers.google.com/speed/webp/docs/riff_container)、[RFC 9649](https://www.rfc-editor.org/rfc/rfc9649.html)、[zlib 手册](https://zlib.net/manual.html)、[Qt 颜色空间](https://doc.qt.io/qt-6/qcolorspace.html#iccProfile)、[Qt 重翻译](https://doc.qt.io/qt-6/i18n-source-translation.html#prepare-for-dynamic-language-changes)、[Apple EDR](https://developer.apple.com/videos/play/wwdc2022/10113/)。Google 与 RFC 的 WebP 内容有共同作者背景，作为规范与发布版本互校，不声称完全独立实验来源。

### 9.3 实施后的证明边界与设计收敛

缩放证明仍采用第 3 节的合法值不变量：每次成功继承 `z′=B(z)=z`；本轮在接受结果时而非发起请求时采集，复用已有 requestId 过滤，不接受旧结果写回。会话首次恢复绕过保持分支；失败不使占位比例成为成功值；各视图拥有独立字段。测试分别覆盖这些状态转移。关闭图片还发出计算模式变更信号，使 UI 状态与默认模式同步。

源记录 `S=E(F)` 随 Result 传递，颜色转换写集不包含 `S`，所以输入/输出记录分离的归纳证明成立。容器读取每次受 8192 项上限和 16 MiB profile/颜色块上限约束；PNG 解压目标固定至多 16 MiB，不使用能够继续扩张目标数组的解压方案。ICC 标签表中 `offset≤N` 且 `size≤N−offset`，因此访问范围处于原始字节数组内。文件大小/时间检查能处理正常外部编辑，不保证抵抗同大小同时间的恶意替换。

实现使用同步值查询代替独立异步 metadata 发布：同一 GUI 事件内源与实际渲染分支共同构成 Information，避免对话框分别维护异步源/输出回调。原生帧身份保护复用已有渲染器机制。100 ms 是正常调度下的刷新间隔，不承诺物理实时；证明内容是快照内部一致和来源诚实，不是每个显示刷新周期 UI 文字零延迟。

与理想设计相比，以下边界明确保留：

1. HEIF/AVIF、JPEG XL、BigTIFF、多页/多分辨率选择和其他没有实现原始 profile 提取器的格式，ICC 显示 Unknown；有解码器空间时明确标为 Decoder。不能把它写成容器声明。RAW 显示相机空间，矢量显示文档定义空间。不存在“任意无声明文件必能给出真实创作色彩空间”的算法。
2. ICC 的 Invalid 只表示已检测到的头、标签范围或容器损坏；Unsupported 表示当前版本/解析库无法解释，不等于已通过完整 ICC 规范认证。源空间与 profile 描述分别保存；命名技术空间保持标准标识，profile 名称原样纯文本显示，最长 256 字符。ICC 可为损坏、版本/变换不支持或读取受限，不强行转换为 sRGB 来源事实。
3. 输出定义为应用当前交给系统的表面编码；当前实际表面仍使用旧目标空间时，如实显示该空间，不因设置/显示器配置期望已变就提前宣称完成转换。未新增物理显示器 profile 的详情行或整套跨屏颜色重渲染功能。
4. 语言选项沿用应用已有“重启后切换整体语言”的约定，未改造全部菜单即时切换；新增设置/信息对话框本身响应 LanguageChange。测试安装真实 QM 验证重翻译及回到英语，产品重启加载同一资源。
5. 测试程序改用独立临时 INI 设置目录，避免继承桌面偏好污染验收或把测试窗口尺寸写入用户设置。原测试窗口的 closeEvent 必须先于选项恢复，以注销菜单引用；新测试用作用域清理保证断言提前返回时仍执行。

实际构建、测试数量、失败归因与剩余验证范围以 `reports/test_completion_report.md` 为准，不能以此前“计划”表格视为已经通过。

---

## 10. 持久化缺陷修正：启动迁移覆盖了已保存选择

### 10.1 问题界定与可能根因

问题是“勾选 Keep zoom level 后重启又取消”，需区分 UI 没写入、写盘失败、读错偏好域、启动迁移覆盖、会话/窗口内存状态覆盖。原子标准 KZ-P1–P4 及六部分用例见测试用例说明。

**已确认根因：** `SettingsManager::migrateOldSettings()` 的 `removedPreferenceDefaults` 仍包含 `{ "navresetszoom", true }`。这是旧版移除设置入口时的固定策略；上一轮恢复入口并反向绑定该键，却漏掉启动迁移的旧规则。`main.cpp` 每次启动都先执行迁移，再构造 `QVApplication`。所以勾选实际写入的 `false` 会在下次启动被改为 `true`。

| 候选解释 | 证据与结论 |
|---|---|
| UI 信号没连接或取反错误 | 写进程点击真实复选框后，UI=true、磁盘=false、manager=false；否定本路径的连接/取反错误。 |
| 尚未同步、Qt/系统缓存或写入权限错误 | 写进程已 sync 且 NoError，INI 与原生后端均先写成功、再重启失效；不能据此否定用户机器任何潜在权限问题，但本次复现不需要该假设。 |
| 组织名/偏好域变化 | 两进程显式使用同一共享目录或UUID专属原生域；仍失败，故不是这次复现的必要根因。 |
| 启动迁移覆盖 | 源码存在确定赋值；新迁移测试 false→true，双后端重启也false→true。只删除该项后同一测试通过，建立因果对照。 |
| 会话恢复或窗口临时值覆盖 | 子进程不恢复会话、不创建图片窗口，已经能复现；不是此次缺陷的必要条件。 |

### 10.2 现有测试为何漏检

旧 `testKeepZoomPreferenceAndColorTranslations` 只在同一进程中点击并重建 `QVOptionsDialog`。这能证明即时写入、读取和反向绑定，但不会执行下一次启动的迁移。普通 Qt 测试主函数也没有像生产 `main.cpp` 一样在构造应用前运行迁移；旧的迁移测试只检查已退役鼠标/预加载选项，没有验证恢复为活动设置的缩放键。之前报告将“同进程重建通过”概括为持久化通过，证据范围不足，本节予以更正。

此次保留原用例，并增加：迁移保值/缺省/连续两次迁移的数据驱动测试；共享真实持久存储的独立进程 GUI 写入及读回；静态检查活动键不能留在退役表。标准测试仍使用每进程临时INI目录，只有受控重启探针共用测试目录或UUID偏好域，不改真实Fovelle用户偏好。

### 10.3 多跳检索与交叉验证

第一跳查 [Qt QSettings](https://doc.qt.io/qt-6/qsettings.html)：setValue 覆盖同键，sync 用于提交/同步，status 用于识别错误。第二跳查其[跨进程读写说明](https://doc.qt.io/qt-6/qsettings.html#accessing-settings-from-multiple-threads-or-processes-simultaneously)及 [QProcess](https://doc.qt.io/qt-6/qprocess.html)：用独立子进程检验真实重启，而非用同进程新对象充当独立缓存。第三跳核对 [Apple 偏好域](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/UserDefaults/AboutPreferenceDomains/AboutPreferenceDomains.html)：持久应用域与缺省注册域含义不同，不能把默认值当成每次启动必须写入的用户值。第四跳沿 [QSignalBlocker](https://doc.qt.io/qt-6/qsignalblocker.html) 与 [QCheckBox](https://doc.qt.io/qt-6/qcheckbox.html) 回查 UI 更新/信号绑定，再检索仓库全部 navresetszoom 写入与启动调用。

Qt 官方 API、Apple 原生偏好域说明、本地实际调用链、INI及NativeFormat两个运行后端相互核验。两个Qt页面不是独立厂商实验，Apple文档也不证明Fovelle源码无误；源码与修复前后实验承担应用层因果证明。证据已足够定位确定写覆盖；没有继续用无关文件系统/显示器检索取代直接反例。

### 10.4 唯一生产修复与正确性

只从 `removedPreferenceDefaults` 移除 `navresetszoom`，补注释标明它已经恢复为活动偏好。保留 settingsLibrary 默认 `true`、UI 的 `checked=!navresetszoom`、立即 sync，以及全部其它退役设置迁移。不引入新键、不删除整个迁移函数、不强制给所有用户开启保持。

令保存值为 `v∈{false,true,⊥}`，其中⊥表示没有用户键。旧迁移 `M_old(v)=true`，故写入保持开启的false后，`checked=!M_old(false)=false`，与用户选择矛盾。修复后迁移对该键为恒等映射 `M(v)=v`；默认解释函数 `D(⊥)=true`，其余 `D(v)=v`。因此任意启动次数n，有 `Mⁿ(v)=v`，复选框始终为 `!D(v)`；false与true分别保持勾选/取消，缺省仍取消。该证明以前提“设置已成功写入、身份不变、没有后续用户/外部修改”为限，动态测试同时检查 NoError 与相同偏好域。

实际UI选择发生在生产启动迁移之后，此时firstlaunch标记已存在；重启探针初始化该标记，以排除依赖机器上旧qView数据的首次导入。保留首次导入的既有语义，不把手工删除初始化标记等外部修改混入本次保证。已被旧版覆盖成true的历史选择无法可靠区分于用户主动关闭，因此不自动猜测恢复为false；用户重新选择后按新规则保存。

### 10.5 修复前检出与实施范围

生产修改前：静态门禁失败；动态五行中四行失败、一行通过。失败包括已开启值被迁移改写、缺省被强行落盘，以及 INI/原生后端的重启保持失败。已关闭输入原本就通过，保留它用于防止误修为全部强制开启。原生和INI写进程均记录checked=true/storedReset=false，随后读进程checked=false/storedReset=true。生产只移除一项后，全部专项通过。完整回归结果以测试完成报告最新增补为准。

---

## 附录：原文件历史记录（本次完整保留）

# HDR 局部偏色技术设计与根因分析

日期：2026-10-08。用户问题：HDR 图片显示异常，进一步确认表现为“一部分偏色”。本次任务是在上一轮亮度渐变改动的基础上，先使补充测试失败，再修改生产代码。此前代码及报告快照保存在 `reports/evidence/hdr_color_regression/before/`，不覆盖用户其他工作。

## 问题界定与原子验收标准

| ID | 验收标准 |
|---|---|
| CR-01 | 显示余量足够、渐变结束时，所有 RGB 区域均恢复正确解码 HDR 端点，包括 HDR 值低于 1、白点附近和饱和高光，不能按局部峰值留下 SDR 颜色。 |
| CR-02 | 中间时刻在扩展线性光空间按同一时间进度插值；不得另加空间 RGB 掩码；若 SDR/HDR 某区域本来相同，该区域应保持稳定。 |
| CR-03 | 当前余量不足时由增益图或 Core Image 色调映射适配；余量足够时保持解码 HDR 原样；预热、可见帧与最终缓存共用端点选择。 |
| CR-04 | 对 JPEG、处理后 DNG、普通 DNG、NEF 的真实图像进行全图 RGB 网格比较，参考值来自独立解码 HDR 分支，不能来自待测公式或就绪标志。 |
| CR-05 | 实际交给合成器的持久图像缓存也必须保持端点颜色；参考与实际使用同样的计算/采样顺序；不能把缩小口径差异当成偏色。 |
| CR-06 | 80+520 ms 默认渐变、250 ms 连续切图、减弱动态效果、动态余量、缩放和 SDR 窗口行为继续通过原测试与完整 Qt 回归。 |

## 多跳检索、多源核验

第一跳检索 HDR/增益图、CIColorKernel、线性颜色；第二跳阅读 WWDC24 原文、增益图和 ToneMap API；第三跳针对缓存比较差异检索 Core Image 延迟计算、合并/重排滤镜与中间缓存。以下是各判断的官方依据，工程推导与已测事实另列，不把候选根因当成事实。

| 官方来源 | 核验结论及用途 |
|---|---|
| [WWDC24 HDR 图像](https://developer.apple.com/videos/play/wwdc2024/10177/) 与 [增益图 headroom API](https://developer.apple.com/documentation/coreimage/ciimage/applyinggainmap(_:headroom:)) | SDR 与 HDR 是各自正确的呈现；增益图与元数据负责显示适配。不能因为 HDR 某个像素未超过白点，就认定该处必须退回 SDR 颜色。 |
| [CIToneMapHeadroom](https://developer.apple.com/documentation/coreimage/citonemapheadroom) 与 [WWDC22 EDR 示例](https://developer.apple.com/documentation/coreimage/generating-an-animation-with-a-core-image-render-destination) | 区分内容 headroom 和当前显示 headroom；在扩展线性色彩空间绘制，用指定 source/target headroom 进行映射，而非自定义逐像素最大通道硬压缩。 |
| [Core Image 内建滤镜处理](https://developer.apple.com/documentation/coreimage/processing-an-image-using-built-in-filters) 与 [Processing Images](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/CoreImaging/ci_tasks/ci_tasks.html) | CIImage 是延迟计算图，滤镜可能合并/重排；没有明确物化的缩小图不能直接当作全分辨率缓存的等价像素参考。 |
| [缓存中间图](https://developer.apple.com/documentation/coreimage/ciimage/insertingintermediate(cache:)) 与 [cacheIntermediates](https://developer.apple.com/documentation/coreimage/cicontextoption/cacheintermediates) | 缓存影响计算路径和复用；缓存就绪不代表缓存内容正确，须实际读回比较。 |

## 可能根因、证据与逆向证伪

### 1. 已确认：空间亮度门槛把两套颜色端点混为一幅图

上一轮 `hdrBrightnessImage` 执行：`P=max(HDR.r,HDR.g,HDR.b)`，`W=smoothstep(1,1.1,P)`，`output=SDR+(compressedHDR-SDR)*W*E(t)`。

即便 `E(t)=1`：

- P≤1 的区域永远显示 SDR 颜色，而非正确 HDR 颜色。
- 1<P<1.1 的区域永远停留在两种颜色之间。
- P≥1.1 的区域才完全进入 HDR。由此形成区域性的颜色关系差异；不只是时间上的缓动效果。

反例：SDR=(.45,.40,.35)，HDR=(.90,.60,.30)，可用 headroom=8。最终应该为 HDR，旧实现却返回 SDR；中间 50% 应为 (.675,.50,.325)，旧实现仍为 SDR。近白点反例最终应为红通道 1.05，旧实现为 .925。新增测试在修改生产算法前均失败。

真实四类样本也失败，详见完成报告中的修复前后比较。充足 headroom 条件下仍失败，反证“只是显示器亮度不够”；单像素可复现，反证“必须是文件解码损坏或缩放几何问题”。同一批数据仅替换端点处理就通过，为本次代码缺陷建立了因果证据。

### 2. 已发现的设计缺陷：绕过平台适配，并使预热与最终绘制使用不同图像图

旧可见帧与缓存使用最大 RGB 通道硬缩放，预热却使用原先的增益图 / CIToneMapHeadroom 路径。这会忽略文件的显示适配语义，也无法保证预热和实际显示内容一致。逐像素同比例缩放本身不直接改变该像素的 RGB 比例，因此不将“硬缩放本身必然偏色”当作已证明结论；已确认的偏色原因是前述空间掩码混合端点。此次一并统一显示适配路径。

### 3. 未获支持：持久缓存损坏或 ICC 变换错误

新增实际缓存像素比较最初得到最大误差 .130859，但参考图直接缩小 CI 图，实际缓存先全分辨率计算再缩小。控制变量实验仅把独立参考也先物化全分辨率，再以相同方式缩小，误差归零，生产缓存流程未为这个实验改动。结合官方延迟计算机制，直接缩小与全分辨率物化后的结果不能用作无条件相等的参考。因此保留这次失败日志，但不将其归因于缓存损坏或 ICC 错误，也没有放宽 .01 容差来让测试通过。

数学上，含空间增益的运算与重采样一般不交换：`R(S*G)` 与 `R(S)*R(G)` 不必相等。该解释是工程推导；实际实验确认的是两种采样口径产生差异，未声称定位到了 Core Image 内部某条未公开优化指令。

## 生产修复

移除自定义 CIColorKernel、空间白点门槛和逐像素最大通道压缩。`hdrDisplayEndpoint` 先确定正确端点：

1. current headroom≤1 时选择 SDR。
2. current≥content headroom 时直接使用解码 HDR，保留全部颜色。
3. 余量不足且有可用增益图时，通过 `imageByApplyingGainMap:headroom:` 重建。
4. 其他已知内容余量使用 `CIToneMapHeadroom`；无法使用新 API 的旧系统保留原生 EDR 合成器回退。

`hdrBrightnessImage` 只做 `SDR+(adaptedHDR-SDR)*E(t)` 的线性时间插值，进度 0/1 精确返回端点。预热、Metal 可见帧、最终持久缓存和图像测试探针共用 `hdrBrightnessDisplayImage`。正确的 HDR 中间调可能与 SDR 不同；“中间调基本稳定”不能解释为永久覆盖 HDR 中间调，否则与最终色彩正确性矛盾。相同端点的非高光区域仍保持不变。

色彩探针 `probeHDRBrightnessImage(..., sourceEndpoint=true)` 绕开待测显示处理，读取独立原生 HDR 参考；`probePersistentHDRPixels()` 读取实际缓存 CGImage。缓存参考明确采用全分辨率物化后再采样，不依赖就绪状态推断内容正确。

## 风险与验证边界

当前用户未提供发生偏色的具体文件或截图。本次已经复现并修复能导致局部颜色错误的生产缺陷，验证了本地四类真实样本；不能据此宣称已一比一复现用户那张照片的全部现场。数值容差用于 half-float 渲染误差，未宣称为视觉色差 ΔE 或亮度计测量结果。显示器 ICC、物理峰值和主观观感没有在多个物理显示器上验证。

## 2026-10-08 GitHub Actions 关闭测试计时修正

问题界定：提交 `1fffabb` 的 Build Fovelle [运行 37746276287](https://github.com/inostarlin-passion/Fovelle/actions/runs/37746276287) 编译成功，31 项 CTest 中仅 FovelleTests 失败；唯一 QtTest 失败为 Open With 关闭耗时断言。同一提交的 [Checks](https://github.com/inostarlin-passion/Fovelle/actions/runs/37746276317) 全部通过。

原子验收：A1 启动准备时间不占用关闭预算；A2 从请求 Open With 到窗口关闭及析构仍须小于 5000 ms，后台工作必须安全结束；A3 任意断言返回均恢复 quitOnLastWindowClosed；A4 推送后的两个工作流全部成功。

根因：计时器在 MainWindow 构造前启动，实际测量包含菜单初始化、窗口显示和图片加载。源码与两个远端任务交叉验证表明原断言无法区分慢启动与慢关闭。加入 5100 ms 准备延迟后旧代码稳定失败；缩小计时范围后同一输入通过，构成反证检查。未发现需修改生产关闭逻辑的证据。

设计：增加正常启动和慢启动数据行；就绪检查与准备延迟完成后、Open With 请求前启动单调计时器，作用域退出后读取耗时。保留五秒限制及真实 Cocoa 后台任务，用 qScopeGuard 恢复全局设置，并输出阶段耗时。

联网核查路径：失败 Actions 日志 → 同提交成功工作流 → MainWindow 关闭源码 → Qt 官方 API/测试建议。Qt 文档确认 [QElapsedTimer](https://doc.qt.io/qt-6/qelapsedtimer.html) 测量显式起点以来的时间，[QFutureWatcher](https://doc.qt.io/qt-6/qfuturewatcher.html) 指出 QtConcurrent::run 返回的 future 不能取消，因此保留 waitForFinished；[Qt Test Best Practices](https://doc.qt.io/qt-6/qttest-best-practices.html) 支持修复前失败/修复后通过的回归验证及 RAII 设置恢复。证据已充分，无需扩展到无关 HDR 或生产逻辑。


## 2026-10-10 Actions：Open With 关闭计时契约修正

原子验收：A1 默认原生应用枚举与图标加载继续执行；A2 关闭/析构返回时任务确已完成，GUI依赖仍存活期间排空；A3 请求分派、close事件及超出provider工作区间的关闭开销各保留5000ms预算；A4 0/5100ms启动准备与5200ms可控provider都覆盖；A5 推送后同一提交的Checks与Build Fovelle均成功。

失败证据：[Checks 38049175144](https://github.com/inostarlin-passion/Fovelle/actions/runs/38049175144)只有Open With正常启动数据行失败，总耗时5080ms；慢启动行56ms。同SHA的[Build Fovelle 38049175130](https://github.com/inostarlin-passion/Fovelle/actions/runs/38049175130)全部通过。日志未记录原生调用内部阶段，因此冷枚举/图标查询是有源码和冷热差异支持的候选解释，不宣称已定位到某个系统内部调用。已确认的问题是五秒断言混合了窗口收尾与不可由应用承诺时限的原生工作。

检索链：GitHub失败日志→同SHA另一工作流→MainWindow排空顺序→Qt future/thread pool文档→Apple实际调用的Launch Services接口。依据：[QFuture](https://doc.qt.io/qt-6.11/qfuture.html)、[QThreadPool](https://doc.qt.io/qt-6/qthreadpool.html)、[QElapsedTimer](https://doc.qt.io/qt-6/qelapsedtimer.html)、[LSCopyAllRoleHandlersForContentType](https://developer.apple.com/documentation/coreservices/1448020-lscopyallrolehandlersforcontentt)。Qt说明basic run不能强制取消、pool销毁会等待，Apple说明原生枚举功能但没有五秒时延保证。源码、两份CI结果、官方API及可控反例交叉验证充分，不继续以同义检索替代复现。

实现为每窗口可传入按值保存的OpenWithProvider，默认仍为现有原生函数，空provider回退到默认函数。任务同时按值捕获路径与provider。排空的waitForFinished→clear→waitForDone顺序保留。测试以原子标志确认完成，分别测量dispatch、close、总时长T和provider区间W；要求W≤T+1ms（计时量化容差）且max(0,T−W)<5000ms，同时dispatch/close各<5000ms。QtTest整个函数看门狗继续约束包含系统等待在内的总运行。T−W是关键路径增量开销，不是所有CPU耗时之和，不承诺总关闭时长小于五秒。

逆向证伪：保留旧总时长断言并注入5200ms provider，旧测试稳定以5210ms失败；纠正计时分类后该输入总5211ms、provider5203ms、开销8ms并通过，同时完成标志为真。原生正常/慢启动行仍调用真实函数，验证没有只测空任务或跳过平台路径。新的源码检查同步核验provider/path按值捕获，保留原生命周期策略。
