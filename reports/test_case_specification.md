# 全屏方向快照性能修复：测试用例说明

日期：2026-10-02（Asia/Shanghai）；检索与红绿验证从2026-10-01开始。基线`bfd3aa0a7b51a62cf0430262a6c0e7c98b8dccca`；机制与多跳检索见 [技术设计](technical_design_document.md)，执行结果见 [完成报告](test_completion_report.md)。

## 1. 问题界定与测试目标

将用户“偶发掉帧”拆为启动、中段运动、终点交接、反向等待和几何跳动。本轮重点约束启动阶段：方向变化后首次原生进入／退出，不能重建整张定向源像素；代理的方向、颜色与比例必须保持正确。已有中段独立运动、终点Paint与功能用例继续回归。

旧`testFullScreenSnapshotReuse`在目标方向上先capture再toggle，只覆盖热快照，漏掉第一次方向变化后的原生provider。本轮新增原生冷方向用例，且未把“测试能稳定检出此机制”写成“直接测到了所有物理掉帧”。

## 2. TC-FS-COLD-ORIENTATION（新增）

入口：`WindowBehaviorTests::testFullScreenColdOrientation`；CTest：`FovelleFullScreenColdOrientation`。平台须为真实cocoa；使用真实AppKit进入／退出与实际代理，不以Qt请求状态作为完成条件。

**数据：** 六行identity、rotate90、rotate180、rotate270、mirror、flip。每行加载4096×3072 XPM四角红／绿／蓝／黄图，共12,582,912像素；XPM避开Image I/O有尺寸上限的native SDR解码代理。RGBA量级为50,331,648字节（48MiB），这是实际受控输入，不是从文件大小估算。

**前置条件：** 普通窗口640×480、ZoomToFit、关闭1:1像素 sizing、禁止自动窗口缩放；临时禁用最后窗口关闭时退出。先获取identity图作为色彩管理参考，仅预热源内容，没有生成目标方向快照。

**步骤：**

1. 按数据行改变方向，等待几何稳定；禁止调用目标方向snapshot getter。
2. 请求真实进入全屏，记录请求的线程CPU及墙钟耗时。
3. 在代理可见期间，通过公开窗口属性定位代理；读取其实际CGImage宽高及缩小源预览，按实际model-layer bounds、position、transform离屏栅格化代理图像。
4. 等待原生did-enter计数增加、代理退休及绘制恢复，核对全屏状态。
5. 在全屏内再旋转90°（镜像状态遵循生产旋转角补偿），不预热定向快照，执行真实退出并重复观测。各行出口覆盖旋转／翻转组合。
6. 保持同一文件路径，将内容改为cyan后重载；不调用快照getter，再真实进入观察native源像素是否更新，最后退出并清理窗口／选项。

**期望：**

- 进入和退出均捕获到真实代理，原生完成且恢复窗口绘制。
- CGImage始终4096×3072，四角源像素与identity参考一致；用户方向不重排源像素。
- 定向离屏预览四角与独立Qt方向参考一致；通道误差不超过12、alpha一致。图像frame比例与期望定向比例误差小于0.005。
- 同路径重载的native源为cyan、alpha255，不能显示旧四角色。
- 退出恢复普通窗口几何。缺失proxy／完成／重载证据不能静默跳过。

**稳定失败要求：** 固定最终测试及输入，在原始生产HEAD连续三轮运行，每轮六个数据行均失败、无skip；identity进入可通过，退出前新增方向变化应失败。尺寸不变的mirror／flip靠源角颜色检出，90°／270°还靠宽高检出。离屏图像在红阶段本应正确，明确失败来自冗余定向源工作，而不是测试图像错误。

**遥测：** `FULLSCREEN_COLD_ORIENTATION`输出12行；字段含row、entering、completed、observed、源宽高、source_matches、rendered_matches、geometry_matches、request_cpu_ms、request_wall_ms。`FULLSCREEN_SOURCE_RELOAD`输出六行。请求计时用于红绿成本比较，不是通过阈值或FPS。

**观测边界：** 探针读取模型并离屏栅格化，不读取显示器实际呈现时间；新测试稳定约束源方向工作的资源行为与图像正确性。它不保证GPU／WindowServer任意负载下零掉帧。

## 3. 既有测试保留与回归

| 用例／CTest | 作用 | 不能替代的观测 |
| --- | --- | --- |
| SnapshotReuse／FovelleFullScreenSnapshotReuse | 热定向快照共享、方向／同路径重载失效、调用方写入隔离、色彩空间 | 冷方向原生provider |
| PresentationKeepsMoving／FovelleFullScreenMotion | 栅格／vector、标题栏显隐、idle／130ms GUI争用、两轮进入／退出，32条运动记录 | 物理屏幕帧时间 |
| LayoutPaintBudget／FovelleFullScreenPaintBudget | 四分支各三次直接Update，12样本，恰好一次视口Paint及CPU预算 | 完整原生通知／收尾 |
| PreparationPaintBudget／FovelleFullScreenPreparationBudget | 四分支×进入／退出，八过程；source最多一次、endpoint恰好一次Paint与CPU预算 | GPU期限、新源首次转换 |
| TitlebarPresentation／FovelleHiddenTitlebarFullScreen | 原生标题栏隐藏／显示一致性 | 大图方向成本 |
| ProvidedRasterFullScreenTransitionKeepsImageVisible／FovelleSDRFullScreenPresentation | 提供栅格native SDR全屏画面 | 所有HDR／多屏性能 |

fit／pan回归运行`testFitZoomSurvivesInverseWheelStepsAndFullscreenResize`、`testFullscreenExitPreservesVerticalPan`及`testFullscreenAfterOverflowRemovesTitlebarScenePadding`。

## 4. TC-FS-METRICS-GATE（新增负向门禁）

入口：`tests/test_fullscreen_system_metrics.py`；CTest：`FovelleFullScreenMetricsGate`，五项Python单元测试并含多个subTest。

方向门禁要求完整唯一的12个方向记录及六个重载记录。负例覆盖空输出、缺方向、重复方向／重载、未完成／未观测、源像素重排、方向错误、比例错误、宽高错误、过期重载、布尔值冒充时间、字段缺失、负数／NaN／Infinity；全部必须拒绝。

Paint门禁与C++统一：线程CPU预算≤75ms，直接Update恰好一次Paint，原生source最多一次、endpoint恰好一次；墙钟保留诊断与40ms注入下界，不以墙钟上限代替CPU成本。负例仍拒绝两次／零次终点Paint、超CPU及无效CPU；合成150ms墙钟／41ms CPU样本只证明预算口径一致，不是物理性能通过证据。

## 5. 执行与证据要求

先构建只改测试的基线，固定最终测试跑三轮红，再修复生产并构建同一测试跑三轮绿。每轮保存完整stdout／stderr、进程退出码，失败不得以缺样本或skip通过。生产patch、源码／测试／二进制SHA-256与环境记录同步保存。

全屏验收运行`ctest --test-dir build --output-on-failure -R 'FullScreen|TitlebarFullScreen'`（实际选择结果见完成报告）；系统运行`python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests --output reports/evidence/fullscreen_orientation/system.json`。系统门禁同时要求编译进程成功、完整遥测、既有运动／Paint／快照及新增方向指标通过。

只做CPU／资源机制、模型／呈现层运动和离屏内容验证；后续若用户仍报告中段物理掉帧，需补现场屏幕／渲染／调度时间线，按根因报告R4等路径继续定位。
