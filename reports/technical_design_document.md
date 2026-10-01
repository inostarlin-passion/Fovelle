# 全屏切换方向快照性能修复：技术设计

日期：2026-10-02（Asia/Shanghai）；检索与红绿验证从2026-10-01开始。生产起点：`bfd3aa0a7b51a62cf0430262a6c0e7c98b8dccca`；最终实现为该HEAD加当前工作区差异，指纹与执行证据见 [本轮证据](evidence/fullscreen_orientation/summary.json)。[根因报告](root_cause.md) 描述的是此次修复前的候选与证据边界。

## 1. 问题界定与原子化拆解

问题为“进入／退出全屏，有时出现掉帧”。拆成提交前静止、已提交动画中段长帧、终点接管迟缓、快速反向排队、几何跳动五类；不能仅凭Qt全屏状态、最终截图或presentation层运动就确定物理屏幕掉帧。

本轮选择根因报告R1中一个可以受控复现的子机制：**源内容未变，但旋转／翻转后首次原生进入或退出需要同步重排大图像素，推迟动画提交。** 原生退出也必须测量，不能只优化首次进入。R2布局、R3交接、R4渲染期限、R5特定栅格高质量缩放与R6串行反向仍是其他现场候选，本轮不将它们全部升级为已确认缺陷。

## 2. 多跳检索与多源交叉验证

| 跳次 | 检索问题 | 一手来源与项目交叉核查 |
| --- | --- | --- |
| 1 | 掉帧、提交迟滞和渲染迟滞的边界 | [Apple迟滞诊断](https://developer.apple.com/documentation/xcode/understanding-hitches-in-your-app) → 本地imageProvider在轨迹提交之前 → 聚焦启动阶段而不声称测到了物理FPS |
| 2 | 热缓存为何漏掉首次方向处理 | [QImage](https://doc.qt.io/qt-6.11/qimage.html) → [Qt 6.11.2 qimage.cpp](https://github.com/qt/qtbase/blob/v6.11.2/src/gui/image/qimage.cpp)的rotated90分配新图并逐像素旋转 → 当前缓存按sourceKey与orientation区分 → 旧测试先capture定向图再toggle |
| 3 | 能否保留源像素而改变方向 | [CALayer.setAffineTransform](https://developer.apple.com/documentation/quartzcore/calayer/setaffinetransform(_:))、[Core Animation基础](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/CoreAnimationBasics/CoreAnimationBasics.html) → 本地代理以CGImage作为contents，方向可由固定仿射矩阵表达 |
| 4 | 旋转后bounds与frame能否直接等同 | [CALayer.frame](https://developer.apple.com/documentation/quartzcore/calayer/frame)、[bounds](https://developer.apple.com/documentation/quartzcore/calayer/bounds)、[Qt QTransform](https://doc.qt.io/qt-6.11/qtransform.html) → Qt方向仅含90°倍数与翻转 → CA父坐标frame须逆变换为未旋转bounds |
| 5 | 同格式native转换是否再次复制 | Qt固定版本convertToFormat_helper在格式相同时返回共享图像 → 本地CGImage provider持有QImage、以constBits提供像素 → 原生源缓存输出预乘RGBA，重复同格式转换无需重建像素 |
| 6 | 如何证明方向正确而非跳过变换 | 读取实际代理CGImage尺寸与源角颜色；离屏栅格化实际model-layer bounds／position／transform；与Qt图像方向参考及纵横比核验 → 红阶段图像正确但源被重排；绿阶段源保持原序且图像仍正确 |
| 7 | 能否把此证明升级为所有显示掉帧消失 | [CALayer.presentation](https://developer.apple.com/documentation/quartzcore/calayer/presentation())及[Apple动画性能指南](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/ImprovingAnimationPerformance/ImprovingAnimationPerformance.html) → 明确模型／呈现层及系统渲染边界，保留物理屏幕呈现限制 |

部分Apple网页只给JavaScript外壳，继续读取官方tutorials/data/documentation路径下API JSON，正文快照保存在本轮证据目录；Qt继续读取官方仓库固定版本raw源码。Apple文档、Qt文档／实现、本地调用链及最终测试红绿构成不同证据类型，不以多篇同站文档冒充独立性能实验。

检索收敛于方向快照这个可验证机制：资料足够解释像素工作和坐标转换；一般联网检索无法补齐用户自然异常的屏幕／GPU记录。其余候选的排查标准沿用根因报告，不能仅因本轮成功就排除。

## 3. 演绎链与逆向证伪

旧链路：方向改变 → orientation缓存未命中 → `toImage().transformed(orientation).convertToFormat(...)` → 大像素缓冲重排／分配 → 同步native provider返回更晚 → 动画提交推迟。

测试反例与判定：

- 旧测试的热快照复用仍全部通过，证明只看热请求不是充分条件。
- 新测试在进入前不生成目标方向快照，退出前再改方向；实际代理源尺寸／源角颜色揭示定向像素重排，不依赖脆弱的绝对耗时阈值。
- identity进入为对照；其后退出前旋转，退出仍须检出。mirror／flip尺寸不变，因此还要检查原始角颜色，不能只判宽高。
- 离屏输出角颜色和图像纵横比必须正确，防止仅取消旋转得到假性能改善。
- 同路径文件重载必须呈现新像素，防止路径缓存或永久快照制造假复用。
- 请求CPU／墙钟耗时作诊断；结构机制与相同最终测试的三轮红绿支持受控因果，不将请求耗时直接换算FPS。

## 4. 生产设计与实现

### 4.1 原生源快照与方向分离

`QVGraphicsView::fullScreenTransitionSourceImage()`按loaded pixmap的内容cacheKey缓存一份未执行用户方向变换的预乘RGBA图像。源内容未变时返回共享QImage；方向变化不失效。beforeLoad、动画帧变化、空源清理及内容key变化负责失效。

`fullScreenTransitionOrientation()`单独返回既有`getUnspecializedTransform()`，只包含四分之一圈旋转／镜像／翻转，没有缩放或平移。MainWindow提供对应Qt槽，native桥为它注册单独的方向provider。进入与退出均使用未定向源provider。

既有`fullScreenTransitionImage()`保持返回定向图像的语义，避免改变旧调用者与其正确性／复用测试；原生切换不再调用它。正常应用原生链仅需要新源缓存；若其他调用者主动请求旧定向快照，可能额外保留一张定向图，本轮没有取消该兼容接口。

### 4.2 固定图层方向与几何动画

Qt图像坐标y向下，AppKit图层坐标y向上；线性方向矩阵按垂直翻转共轭，CA系数为`(m11, -m12, -m21, m22)`。图像layer直接引用原序CGImage，以固定affineTransform表示方向。

`fullScreenLayerBoundsForFrame()`将父坐标中的目标frame尺寸通过layer仿射矩阵的逆变换映射回本地bounds。90°／270°交换宽高；镜像保持尺寸。初始图像使用bounds与中心position设定，轨迹仍一次提交bounds与position动画；不新增每帧GUI回调或每帧像素变换。窗口／标题栏layer为恒等方向，既有几何行为保持一致。

native完成观察者后的异步交接、expose、绘制暂停／恢复、必要最终Paint与代理清理保持原有流程；本轮没有用提前揭示真实窗口或跳过终点绘制换取测试通过。

### 4.3 测试与系统门禁

新用例`testFullScreenColdOrientation`进入CTest与系统门禁。探针独立枚举公开AppKit窗口属性寻找代理，不读生产association key、不读取生产缓存标志，获取实际CGImage并栅格化其图层模型。该栅格化是离屏正确性验证，明确不等于屏幕逐帧采集。

系统门禁要求六行×进入／退出共12个方向记录及六个重载记录，完整、唯一且各项断言为真；缺失方向、重复行、空观测、错误角颜色、错误比例、陈旧重载、缺失／NaN／负数计时均拒绝。另有独立Python负向单元测试。

同时修正旧Paint门禁与最新C++用例口径不一致的问题：C++已经按线程CPU预算判定，Python仍用墙钟75ms上限。本轮将Python对齐CPU预算，墙钟保留诊断及必要注入成本下界；仍严格约束Paint次数、CPU上限与样本完整性。此变更避免主机抢占／原生等待被误报成重复CPU绘制，不放宽重复Paint断言。

## 5. 验证、风险与适用范围

最终相同测试对生产起点执行三轮红、对修复执行三轮绿；再构建应用和测试，运行全屏CTest、系统门禁及fit／pan业务回归。命令、计数、版本与指纹以 [测试完成报告](test_completion_report.md) 为准。

本轮消除的是原生切换前的用户方向像素重排。首次新源必要格式转换／图层创建仍同步；大内容上传／透明合成、终点队列延迟、其他应用负载及物理显示掉帧仍可能存在。native HDR／动画全矩阵、多屏刷新率／极大图峰值内存没有完整性能覆盖。失败／零时长路径做源码检查，未专门故障注入，不以常规回归冒充这些测试通过。

资源断言观测的是代理源的方向重排；仅凭尺寸和角颜色不能排除一切隐藏的同像素复制或GPU上传。源缓存及同格式共享还由固定版本Qt源码和生产调用链核验，不能把这些额外成本写成已实测为零。
