# Toggle Fit and 100% 返回适合窗口末尾震荡：技术设计文档

> 日期：2026-09-03
>
> 仓库：`/Users/inostarlin/code/Fovelle`
>
> 基线提交：`40a4bc318f3cf80881703f34cb1b57881d964668`
> 目标平台：macOS / Qt Widgets / Cocoa

## 1. 问题边界与分解

### 1.1 用户可见问题

操作链为：打开大图 → 按 `Z` 从“适合窗口”切换到 100% → 再按 `Z` 返回“适合窗口”。第二次过渡接近结束时，图片先缩到一个尺寸，随后发生一次或数次轻微反向缩放，片刻后才稳定。

用户提供的现场图片为：

```text
/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg
3840 × 4407
```

自动化测试另生成 `2560 × 2938` raster；两者宽高比完全相同，因为 `3840 / 2560 = 4407 / 2938 = 1.5`。这样既能在无外接卷环境下重放几何边界，又能在现场文件存在时交叉验证真实 JPEG/ImageIO 路径。

### 1.2 状态拆分

一次返回 fit 的事务拆为五段：

1. **基准 fit**：图片已适合无滚动条 viewport，记录 `referenceFitLevel/referenceFitSize`。
2. **100% 初态**：图片横纵轴均溢出，H/V `ScrollBarAsNeeded` 均有有效 range。
3. **动画暂态**：`QPropertyAnimation` 连续写入 `animatedZoomLevel`，图片宽高应只减不增。
4. **动画边界**：最后一个 `valueChanged` 与 `finished` 后的图片尺寸必须相同。
5. **终态静默窗**：所有 animation、anchor、constraint、expensive-scale 和 scrollbar-geometry writer 停止后，再观察 650 ms，尺寸和滚动条状态不得改变。

### 1.3 修复前的可重复证据

新增轨迹测试在修复前稳定失败，关键尺寸序列为：

```text
100%: 2561×2939
...
animation-value: 824×945
animation-value: 822×943
animation-finished: 835×958
```

即缩小动画已经到达 `822×943`，`finished` 路径又把图片放大到 `835×958`。高度差为 15 DIP，与测试平台滚动条占位尺度一致。这是几何状态反转，不是仅有纹理锐度变化。

## 2. 原子化验收标准

| ID | 原子验收标准 | 可核验 oracle |
| --- | --- | --- |
| `AC-FIT-01-REAL-Z-ROUND-TRIP` | 必须经真实 `Z` 快捷键完成 fit→100%→fit；最终为实际 fit 且 H/V range 均为零。 | `QTest::keySequence`、action shortcut、`isImageAtFit()`、两轴 range。 |
| `AC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET` | 第二次 `Z` 分发时就确定与基准 fit 相同的 logical target；100% 时存在的滚动条不得改变目标，整个返回事务只允许一次 `zoomLevelChanged`。 | `getZoomLevel() == referenceFitLevel`；signal spy count `== 1`。 |
| `AC-FIT-03-MONOTONIC-SHRINK` | 从 100% 返回 fit 的每个可观察轨迹样本中，图片宽高不得比前一尺寸增长超过 1 DIP。 | 对 range/value、resize、paint、show/hide、animation 和 timer sample 逐项计算 `reversalCount == 0`。 |
| `AC-FIT-04-NO-TERMINAL-RESCALE` | 最后一个动画 value 与 `animation-finished` 观察到的宽高差均不超过 1 DIP。 | `sizesEquivalent(lastAnimationValueSize, animationFinishedSize)`。 |
| `AC-FIT-05-QUIESCENT-FINAL-STATE` | animation 结束、全部已知延迟 writer 停止及额外 650 ms 后，尺寸均保持一致；终态恢复初始 fit 尺寸且无滚动条 range。 | finished/terminal/quiet/reference 四个尺寸两两核验，外加 writer inactivity 和 H/V range。 |
| `AC-FIT-06-CROSS-FIXTURE` | 相同 oracle 必须同时通过可移植同宽高比 fixture；现场 JPEG 存在时也必须通过真实 3840×4407 文件。 | QtTest 两个 data row：`synthetic-same-aspect-ratio` 与 `provided-3840x4407-jpeg`。 |

发布条件：

```text
AC-FIT = AC-FIT-01 ∧ AC-FIT-02 ∧ AC-FIT-03
       ∧ AC-FIT-04 ∧ AC-FIT-05 ∧ AC-FIT-06
```

## 3. 证据缺口驱动的联网与多跳检索

### 3.1 证据缺口

| 缺口 | 需要确认的问题 | 检索/验证路径 |
| --- | --- | --- |
| `G1` | AsNeeded 滚动条消失是否会改变 viewport？ | Qt `QAbstractScrollArea` 公共文档。 |
| `G2` | 是否存在公开 API 表示“滚动条无 range 时”的目标 viewport？ | 同一文档的 `maximumViewportSize()` 契约。 |
| `G3` | `QGraphicsView` 内部是否以最大 viewport 为基准、再处理 H/V 交叉占位？ | Qt 6.11.1 对应版本源码。 |
| `G4` | resize/fit 路径中重复切换自动滚动条是否是 Qt 已知风险？ | Qt `QGraphicsView::fitInView` 文档。 |
| `G5` | 动画属性何时真正写入对象，末尾额外写入能否成为可见帧？ | Qt `QPropertyAnimation` 文档 + 本地 signal/paint 轨迹。 |
| `G6` | 问题来自上游 qView 还是 Fovelle 新增动画/settle 链？ | 上游 qView 当前源码与本仓库差异对照。 |

### 3.2 多跳事实链

1. [Qt `QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html) 说明：AsNeeded 条在 range 非零时显示、否则隐藏；隐藏后 viewport 扩展。该文档还把 `maximumViewportSize()` 明确定义为滚动条没有有效 range 时的 viewport 尺寸。
2. [Qt `QGraphicsView`](https://doc.qt.io/qt-6/qgraphicsview.html) 说明：场景完整可见、没有滚动条时由 alignment 定位；同时明确警告，在 `resizeEvent()` 中 fit 若切换自动滚动条，可能产生不希望的 resize recursion。
3. [Qt 6.11.1 `qgraphicsview.cpp`](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp) 的 `recalculateContentSize()` 先读取 `maximumViewportSize()`，再依据内容是否越界决定 H/V 条，并处理“一轴出现后压缩另一轴”的交叉条件。
4. [Qt `QPropertyAnimation`](https://doc.qt.io/qt-6/qpropertyanimation.html) 说明动画通过 property setter 更新目标属性；因此 `finished` 前后的额外 setter 调用不是抽象状态变化，而会改变实际显示 transform。
5. [qView 上游 `qvgraphicsview.cpp`](https://github.com/jurplel/qView/blob/main/src/qvgraphicsview.cpp) 用于确认产品血缘和基本缩放语义；当前上游路径没有 Fovelle 的 `animatedZoomLevel`/terminal fit correction 组合，因此本问题属于本仓库的动画与滚动布局耦合，不能直接套用上游补丁。
6. 本地逐事件轨迹把上述机制落到现场：修复前末帧 `822×943`，finished 后 `835×958`；差值与滚动条释放后的 viewport 增量同量级。

### 3.3 交叉验证约束

- **API 与实现交叉**：公共文档给出 `maximumViewportSize()` 语义；同版本 Qt 源码证明 `QGraphicsView` 自身也以它作为布局基准。
- **静态与动态交叉**：Python 静态门禁验证两个 fit 计算入口共用该目标；QtTest 动态门禁验证用户可见轨迹。
- **替代 fixture 交叉**：XPM/Qt raster 路径排除 JPEG 解码和 Metal 独有因素；用户 JPEG data row 再确认真实 ImageIO/本机呈现路径。
- **终点与静默窗交叉**：正确终态不能抵消短暂错误帧，因此既检查每个轨迹样本，也检查 650 ms 后的稳定性。
- **独立基准交叉**：终态不仅与自己比较，还必须回到第一次进入 100% 前记录的 `referenceFitLevel/referenceFitSize`。

## 4. 显式前提

1. 测试事务期间外层窗口尺寸、屏幕和标题栏模式不由用户主动改变；否则新的窗口大小理应产生新的 fit target。
2. “适合窗口”的终态定义包含图片被目标 viewport 容纳且 H/V scrollbar range 为零。
3. 100% 状态中的 AsNeeded 滚动条是源状态的临时布局成员，不是 fit 目标几何的一部分。
4. `maximumViewportSize()` 已包含 frame、viewport margins 与 AlwaysOn policy 的 Qt 契约；Fovelle 仍需另外扣除覆盖在 viewport 上方的原生标题栏遮挡。
5. `fitOverscan` 在宽、高两侧各扩展一次，因此目标尺寸需要分别增加 `2 × fitOverscan`。
6. 1 DIP 仅用于 `QRectF → QRect`、设备像素与逻辑像素取整容差；超过该值或发生方向反转均判失败。
7. 650 ms 覆盖当前 200 ms zoom transition、350 ms anchor settle、500 ms delayed constraint，并留下额外 event-loop 余量。

## 5. 根因与链式推理

旧计算可写为：

```text
V0 = 100% 状态下的当前 viewport（已被 H/V 条缩小）
z0 = fit(image, V0)
动画：1.0 → z0
图片在末尾变小 → H/V 条消失 → viewport 扩展为 V1
z1 = fit(image, V1)，且 z1 > z0
finished 回调同步写入 z1
```

由此得到：

1. `z0` 是“含滚动条 viewport”的 fit，并非真正无滚动条终态的 fit。
2. 动画到达 `z0` 时会消除产生 `V0` 的滚动条，系统布局变为 `V1`。
3. 旧 `finishZoomTransition()` 与 `resizeEvent()` 的补偿逻辑发现 `z0 != z1`，执行非动画 `recalculateZoom(false)`。
4. 用户看到的就是 `1.0 → z0 → z1`；若布局/延迟 writer 再参与，末尾可表现为多次轻微震荡。
5. 因果修复不是隐藏最后一帧，而是在开始前令 `z0 = z1`。

新计算为：

```text
Vfit = maximumViewportSize - titlebarObscuredHeight + 2×fitOverscan
zfit = fit(image, Vfit)
动画：1.0 → zfit
滚动条消失时 Vfit 不变，因此 finished 无需改变比例
```

## 6. 实现设计

### 6.1 单一目标几何

在 `QVGraphicsView` 新增 `getFitViewportSize(bool addOverscan)`：

- 以 `QAbstractScrollArea::maximumViewportSize()` 取得无有效 scrollbar range 的尺寸；
- 高度扣除 `MainWindow::getViewportPosition().obscuredHeight`；
- 按需在两轴加入 `2 × fitOverscan`；
- 将空高度钳制为 0。

### 6.2 消除双入口分歧

以下两个入口都改用同一 helper：

- `recalculateZoom()`：执行实际 fit；
- `calculateZoomLevelForMode()`：供 Toggle 状态判定与终点核验。

若只修改其中之一，Toggle 的“是不是 fit”判断与实际写入仍可能不同，因此静态门禁要求两者同时使用 helper。

### 6.3 保留的行为

- 不改变 200 ms `OutCubic` 过渡；
- 不改变 fit/100% Toggle 状态机与 `Z` 默认快捷键；
- 不改变 cursor/viewport-center 的方向性锚点策略；
- 不禁用滚动条，也不临时修改 scrollbar policy；
- 保留标题栏安全区、fit overscan、fit zoom limit、小图 1:1 与逻辑像素取整。

## 7. 备选方案与否决理由

| 方案 | 结论 | 原因 |
| --- | --- | --- |
| 取消返回 fit 的动画 | 否决 | 掩盖而非修复目标不稳定，并改变全局交互约定。 |
| 在 finished 后继续二次动画到新 fit | 否决 | 把一次突跳改成第二段动画，仍违反单一目标与时长语义。 |
| 动画期间强制 scrollbar AlwaysOff | 否决 | 改变可导航性和 Qt policy，且可能影响其他 zoom source。 |
| 继续使用 current viewport，仅调小等价容差 | 否决 | 15 DIP 差异不是浮点噪声；放宽阈值会把真实可见反转判成通过。 |
| 从无 range 的最大 viewport 预先计算 fit | 采用 | 符合 Qt 公共契约与同版本内部算法，并从因果起点消除反馈。 |

## 8. 测试与可观测性设计

- 静态测试：`tests/toggle_fit_stability_static.py` 检查 helper、双入口、原子 ID、六字段结构化用例、联网证据与 CTest 注册，输出 JSON。
- 动态测试：`GraphicsViewTests::testToggleFitReturnHasMonotonicStableTerminalSize` 使用真实 `QTest::keySequence`，监听 animation、paint、resize、layout、scroll range/value 与所有相关 timer。
- 系统样本：同一 QtTest 的 data row 在用户 JPEG 可访问时直接打开该文件；也可通过 `FOVELLE_TOGGLE_FIT_SAMPLE` 指定其他现场样本。
- 机器可读/可审计输出：静态 JSON、CTest/QtTest 文本日志以及每个 data row 的 `TOGGLE_FIT_STABILITY` 摘要。
- 邻近既有端点测试在把 scrollbar 调到 maximum 前增加 `waitForZoomTerminal()`；否则它可能在 2× 动画尚未结束、range 仍增长时错误地把中间 maximum 当终点。这是测试前置条件固化，不修改边缘 oracle。
- 原 `FovelleZoomIssueStatic` 绑定此前占用同三份固定报告路径的四问题文档；按本任务要求重写这些报告后，其 CTest 注册由当前 `FovelleToggleFitStabilityStatic` 取代。原四问题动态门禁仍保留并纳入全套回归。

## 9. 风险与边界

- 当前修复针对“外层窗口未主动变化但 AsNeeded 条在动画中消失”的反馈。用户在 200 ms 内主动调整窗口时，结束阶段按新窗口重新 fit 是合理行为，不属于本缺陷。
- 自动 fixture 验证几何与 Qt raster 路径；现场 JPEG row 验证本机可用的真实文件，但外接卷不存在时不会令可移植测试失败。
- 测试证明应用提交的 transform/geometry 无反转；不声称检测显示器固件或 WindowServer 在正确几何之外的独立合成异常。

## 10. 验收追溯

| 原子标准 | 设计落点 | 测试落点 |
| --- | --- | --- |
| `AC-FIT-01-REAL-Z-ROUND-TRIP` | 保留 Toggle/action/shortcut 入口 | `TC-FIT-01-REAL-Z-ROUND-TRIP` |
| `AC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET` | `getFitViewportSize()` + 双计算入口 | `TC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET` + 静态合同 |
| `AC-FIT-03-MONOTONIC-SHRINK` | 动画从当前显示比例直达唯一 `zfit` | `TC-FIT-03-MONOTONIC-SHRINK` |
| `AC-FIT-04-NO-TERMINAL-RESCALE` | finished 重新查询得到相同目标 | `TC-FIT-04-NO-TERMINAL-RESCALE` |
| `AC-FIT-05-QUIESCENT-FINAL-STATE` | 保留 writer settle，目标不再依赖其 scrollbar 状态 | `TC-FIT-05-QUIESCENT-FINAL-STATE` |
| `AC-FIT-06-CROSS-FIXTURE` | 几何修复不依赖图片编码 | `TC-FIT-06-CROSS-FIXTURE` + `TC-SYSTEM-PROVIDED-JPEG` |
