# 全屏切换同步快照复用：技术设计文档

日期：2026-10-01。Git 基线为 `98f1e06dd0169a5bf5d7efc5dbdd4376826babe0`，本轮起点还包含工作区既有的终点交接修复。新增生产变更只在 `qvgraphicsview.cpp/.h`；既有运动、准备绘制、终点交接修复继续保留。版本与九项源码指纹见 [summary](evidence/fullscreen_snapshot/summary.json)，新增生产差异见 [patch](evidence/fullscreen_snapshot/snapshot-production.patch)。

## 问题界定与原子化拆解

用户问题：进入或退出全屏有时卡顿。依据 [根因报告](root_cause.md) 的 R1，将提交前快照、准备布局、已提交运动、终点绘制与原生完成分开。现有测试已覆盖后三类中的主要受控机制，但源阶段 Paint 数量不能观察不产生 Paint 的像素变换／格式转换。

本轮可验证问题是：源像素和方向不变时，进入、退出及重复请求仍同步重建整张 RGBA 快照。其成本处于 Core Animation 轨迹提交前，会增加准备等待；是否解释用户某次自然卡顿仍须现场时间线。本轮不把八次请求的累计时间当作一次切换时间。

| 原子工作 | 基线位置与触发 | 验证方式 |
| --- | --- | --- |
| 源 pixmap 转 QImage | `fullScreenTransitionImage()`，进入／退出均调用 | 当前源码与实际加载尺寸 |
| 旋转／镜像／翻转 | 同步 `transformed(getUnspecializedTransform())` | 四角颜色、方向变化、耗时 |
| 转预乘 RGBA8 | `createFullScreenSnapshotCGImage()` | 公共 provider 输出后执行相同格式转换，检查不可变缓冲复用 |
| 布局／fit／zoom 改变 | 原生进入／退出 | 原生完成通知后检查源／方向未变时仍可复用 |
| 源／方向真正改变 | 旋转、同路径重新加载、动画帧更新 | 失效正确性与源码审查分别记录 |

## 多跳联网检索与多源交叉验证

| 跳次与问题 | 一手联网来源 | 源码／实验核验与推论 |
| --- | --- | --- |
| 1：为什么 Paint 门禁会漏检 | [Apple render loop](https://developer.apple.com/videos/play/tech-talks/10855/) | 提交与渲染迟滞分开；本地 imageProvider 在准备日志及轨迹提交前同步执行，既有 Paint 预算未计入它 |
| 2：图像变换与格式转换做什么 | [Qt 6.11 QImage](https://doc.qt.io/qt-6.11/qimage.html) | 当前 view 每次 transformed，原生桥每次 convertToFormat；固定版本 [qimage.cpp](https://github.com/qt/qtbase/blob/v6.11.2/src/gui/image/qimage.cpp) 第2207行起表明同格式返回共享对象，异格式走转换分配路径 |
| 3：能否可靠识别源像素变化 | [QPixmap cacheKey](https://doc.qt.io/qt-6.11/qpixmap.html#cacheKey) | 内容改变时 key 改变；文件路径、尺寸或 zoom 都不足以作为内容身份。本地 original loadedPixmap 与显示缩放 pixmap 分开，使用前者 |
| 4：共享输出是否引入观测副作用 | [QImage constBits](https://doc.qt.io/qt-6.11/qimage.html#constBits)、[隐式共享](https://doc.qt.io/qt-6.11/implicit-sharing.html) | constBits 不 detach；测试保持 first 存活，地址不能被下一次分配复用。调用方写入触发 detach，应不污染下一次快照 |
| 5：平台是否支持共享图像内容 | [Apple 图层性能指南](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/ImprovingAnimationPerformance/ImprovingAnimationPerformance.html)、[图层内容](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CoreAnimation_guide/SettingUpLayerObjects/SettingUpLayerObjects.html) | 当前 CGDataProvider 持有 QImage 并读 constBits；同格式转换可共享 CPU 像素。资料不能证明 GPU 上传／合成也被缓存，未据此作该主张 |
| 6：是否只是理论性能猜测 | 最终原代码／修复代码三轮对照，见 [完成报告](test_completion_report.md) | 原代码每行八次重新生成完整输出缓冲；旋转行八次约512–514ms。修复后零次重建，必要新方向与新源仍保持正确 |

检索由阶段划分→Qt 图像 API→固定版本源码→内容身份／共享语义→Apple 图层规范→项目动态反例逐跳收敛。Qt 概要文档没有承诺所有转换零复制，因此继续核查 `v6.11.2` 实现；使用 `qt-6.11`，避免默认文档版本漂移。Apple 与 Qt 是不同平台来源，本地红绿是另一类证据；多篇同厂商文档不能算多个独立因果证明。

证据已充分支持“重复 CPU 像素工作”及其修复，停止于此机制。没有用户现场 CPU／GPU／屏幕时间线，一般联网资料不能补齐其事实，故保留首次生成、标题栏捕获、必要布局和系统合成等边界。

## 链式演绎与逆向证伪

源和方向不变→相同变换输出及预乘格式相同→每次重建不提供新像素→同步 GUI 工作重复→轨迹提交前等待可累加。原代码没有共享输出，四类行三轮都能检测；90° 旋转成本明显大于恒等分支，符合像素变换工作量差异。

反向排查：原生 SDR 常用有尺寸上限的代理，不能把原文件尺寸当作快照尺寸。本测试用 XPM 走 Qt 栅格分支，并断言实际4096×3072输出。原加载器有色彩管理，初期用原始 RGB／反向 sRGB 近似作精确比较产生误报；最终以已加载、未旋转像素为参照，四角位置和色彩空间仍严格检查。探索日志不计为正式红轮。

只缓存旋转图、仍每次异格式转换，会产生新 RGBA 缓冲，违反最终复用断言。只以文件路径缓存，会在同名重载后返回旧画面；只以源 key 缓存，会在方向改变时错用旧尺寸；缓存可写像素会被调用者 fill 污染。测试同时约束这些反例。原生布局改变不应成为失效条件，否则进入／退出后仍重复工作。

## 实现方案

`QVGraphicsView` 保存一个不可变 QImage、源 `QPixmap::cacheKey()` 和无缩放／平移的方向变换。命中时直接返回共享 QImage；未命中时释放上一条缓存，再完成 toImage→方向变换→RGBA8888_Premultiplied，并保存新结果。原生 CGImage provider 的同格式转换于是共享此缓冲，生命周期仍由其拥有的 QImage 保证。

源为空时清空并返回空；fileChanging 的 beforeLoad 与 animatedFrameChanged 清空缓存，避免旧图／旧帧驻留；源 key 和方向检查继续作为内容正确性保护。viewport、标题栏、fit、zoom、窗口位置和显示几何不参与像素缓存键。接口继续仅在现有 GUI 路径调用，未增加跨线程访问。

缓存仅保存一条；本测试驻留输出约48MiB。它按实际图像尺寸增长，并非固定内存上限。切换文件、动画帧更新、下次不匹配请求或 view 析构释放缓存引用；原生 provider 或调用方仍持有共享引用时，像素应继续存活。首次新源／方向生成仍同步且可能昂贵，这是换取后续切换避免重复工作的内存／时延权衡；未通过降低图片尺寸或画质换取通过。

新增用例注册为 `FovelleFullScreenSnapshotReuse`，系统门禁要求四个完整且不重复的快照记录、零重建、两个原生方向完成、正确像素量／字节量、无失败及有效诊断时间。缺失观测不能通过。已有交接／运动门禁继续独立运行。

相关源码：[view](../src/qvgraphicsview.cpp)、[声明](../src/qvgraphicsview.h)、[原生 provider](../src/qvcocoafunctions.mm)、[测试](../tests/tst_qviewtests.cpp)、[CTest](../tests/CMakeLists.txt)、[系统门禁](../tests/quality_fullscreen_system.py)。之前终点交接设计保存在 [prior_reports](evidence/fullscreen_snapshot/prior_reports/)，其机制和既有回归仍有效。
