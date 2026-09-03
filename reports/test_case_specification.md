# Toggle Fit and 100% 返回适合窗口稳定性：测试用例说明

> 日期：2026-09-03
>
> 被测仓库：`/Users/inostarlin/code/Fovelle`
>
> 动态测试入口：`GraphicsViewTests::testToggleFitReturnHasMonotonicStableTerminalSize`
>
> 静态测试入口：`tests/toggle_fit_stability_static.py`

## 1. 测试范围

本说明只覆盖如下用户事务：打开图片，在“适合窗口”状态按 `Z` 进入 100%，再按 `Z` 返回“适合窗口”，验证返回动画末尾没有图片尺寸反转、二次缩放或延迟震荡。

测试采用两层互补证据：

- **静态测试**验证实现使用不受当前 AsNeeded 滚动条占位影响的 fit viewport，并检查需求、设计、测试代码和报告之间的可追溯性。
- **动态测试**通过真实快捷键驱动窗口，记录动画、paint、resize、滚动条和延迟 timer 的尺寸轨迹；既使用可移植的同宽高比合成图，也在用户现场 JPEG 存在时直接验证该文件。

## 2. 原子验收标准与覆盖矩阵

| ID | 原子验收标准 | 专属结构化用例 | 静态 | 合成图动态 | 现场 JPEG |
| --- | --- | --- | :---: | :---: | :---: |
| `AC-FIT-01-REAL-Z-ROUND-TRIP` | 真实 `Z` 完成 fit→100%→fit，最终实际为 fit，H/V range 均为零。 | `TC-FIT-01-REAL-Z-ROUND-TRIP` | — | ✓ | ✓ |
| `AC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET` | 返回操作分发时目标已等于初始 fit，且整个事务只有一次 zoom-level 变更。 | `TC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET` | ✓ | ✓ | ✓ |
| `AC-FIT-03-MONOTONIC-SHRINK` | 返回过程中可观察的图片宽高只减不增；1 DIP 仅作为取整容差。 | `TC-FIT-03-MONOTONIC-SHRINK` | — | ✓ | ✓ |
| `AC-FIT-04-NO-TERMINAL-RESCALE` | 最后一个 animation value 与 finished 后尺寸相差不超过 1 DIP。 | `TC-FIT-04-NO-TERMINAL-RESCALE` | — | ✓ | ✓ |
| `AC-FIT-05-QUIESCENT-FINAL-STATE` | 所有 writer 停止且再等待 650 ms 后，尺寸、fit 状态和滚动条状态不变。 | `TC-FIT-05-QUIESCENT-FINAL-STATE` | — | ✓ | ✓ |
| `AC-FIT-06-CROSS-FIXTURE` | 同一 oracle 覆盖可移植合成图；现场文件可访问时同时覆盖真实 JPEG。 | `TC-FIT-06-CROSS-FIXTURE` | ✓ | ✓ | ✓（条件执行） |

总体通过条件为六条原子标准的逻辑合取，不允许以“最终尺寸正确”替代过渡轨迹和静默窗验证。

## 3. 公共测试状态与 oracle

### 3.1 固定设置

- `windowresizemode = Never`，防止图片加载反向调整顶层窗口。
- 初始 zoom mode 为 fit；`fitoverscan = 0`，`fitzoomlimit = false`，`smallimage1to1 = false`。
- 平滑缩放关闭，以便图片显示包围盒直接反映几何 transform，而不是纹理插值差异。
- `togglefitand100` action 临时绑定为单键 `Z`；用例退出时恢复设置和快捷键。
- 窗口请求尺寸为 `500 × 550` DIP。

### 3.2 观测量

`ZoomIssueProbe` 对 view、viewport、两根 scrollbar 及其容器安装事件过滤器，并订阅：

- `QPropertyAnimation::valueChanged` 与 `finished`；
- viewport 的 paint、resize、layout、show/hide；
- H/V scrollbar 的 range/value 变化；
- zoom anchor、constraint、expensive scale 与 scrollbar geometry 相关 timer。

每个样本保存当前图片显示包围盒。对于相邻尺寸 `S[i-1]` 和 `S[i]`：

```text
reversal(i) = S[i].width  > S[i-1].width  + 1
           ∨ S[i].height > S[i-1].height + 1
```

`reversalCount` 必须为零。动画最后值、finished、writer terminal、650 ms quiet 和初始 reference-fit 尺寸则分别使用每轴 `≤ 1 DIP` 的等价 oracle。

## 4. 结构化测试用例

### TC-STATIC-FIT-TARGET-CONTRACT

**测试目的**：静态证明两个 fit 计算入口共享“无有效滚动条 range 的目标 viewport”合同，并验证六条原子标准、三组结构化用例、联网证据和 CTest 注册均可追溯；主要覆盖 `AC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET` 与 `AC-FIT-06-CROSS-FIXTURE`。

**前置条件**：Python 3 可用；仓库包含生产源码、QtTest 源码、CTest 配置及三份报告；不要求 GUI、显示器或外接卷在线。

**输入数据**：`src/qvgraphicsview.{h,cpp}`、`tests/tst_qviewtests.cpp`、`tests/CMakeLists.txt`、三份 `reports/*.md`，以及脚本内声明的 6 个 AC ID、3 个 TC ID 和 6 个必填字段。

**操作步骤**：

1. 执行 `python3 tests/toggle_fit_stability_static.py --repo . --output build/test-results/toggle-fit-stability-static.json`。
2. 脚本解析 `getFitViewportSize()`、`recalculateZoom()` 与 `calculateZoomLevelForMode()` 函数体。
3. 核验 helper 调用 `maximumViewportSize()`，保留 titlebar obscuration 和 `fitOverscan`。
4. 核验两个 fit 入口均调用同一 helper，QtTest 包含真实 `Z`、轨迹、末帧、静默窗及两种 fixture oracle。
5. 核验每个 AC ID 横跨设计、用例、测试代码和完成报告，并输出机器可读 JSON。

**预期结果**：进程退出码为 0；JSON 顶层 `passed` 为 `true`；`ST-FIT-01` 至 `ST-FIT-06` 全部通过；生产代码不再从当前 `getUsableViewportRect(...).size()` 计算 fit target。

**后置条件**：仅生成或覆盖 `build/test-results/toggle-fit-stability-static.json`；不修改应用设置、源图片或生产数据。

### TC-FIT-01-REAL-Z-ROUND-TRIP

**测试目的**：单独判定 `AC-FIT-01-REAL-Z-ROUND-TRIP`，证明测试没有绕过用户入口直接调用 zoom API，并且完整往返后的状态确为 fit。

**前置条件**：公共设置已应用；图片加载并稳定于 reference fit；`togglefitand100` action 的实际 shortcut 为 `Z`；测试窗口已激活且 viewport 拥有焦点。

**输入数据**：合成或现场 data row；两个 `QKeySequence(Qt::Key_Z)` 输入；reference-fit zoom/尺寸；H/V scrollbar range。

**操作步骤**：发送第一次真实 `Z` 并等待 100% 稳定；验证两轴均溢出；发送第二次真实 `Z`；等待 animation、相关 timer 与事件队列稳定。

**预期结果**：第一次操作后 zoom 等价于 1.0；第二次操作后 `isImageAtFit()` 为 true，H/V maximum 均不大于 minimum；全过程由 action shortcut 入口触发。

**后置条件**：图片停在 reference fit；不残留运行中的 zoom writer；由 `TC-FIT-05-QUIESCENT-FINAL-STATE` 继续检查静默稳定性。

### TC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET

**测试目的**：单独判定 `AC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET`，证明 100% 状态下两根 AsNeeded 条不会参与返回 fit 的 logical target 计算。

**前置条件**：已记录无滚动条的 `referenceFitLevel`；第一次 `Z` 后 zoom=1.0 且 H/V range 均非零；已在 view 上安装 `QSignalSpy`。

**输入数据**：`referenceFitLevel`、第二次真实 `Z`、`zoomLevelChanged` 信号序列、100% 时当前 viewport 与 maximum viewport 的不同状态。

**操作步骤**：清空事务外信号后发送第二次 `Z`；在等待动画前立即读取 logical `getZoomLevel()`；动画完全结束后读取 signal 计数。

**预期结果**：操作分发后 target 立即与 `referenceFitLevel` 等价；整个返回事务 `fitZoomChangeSpy.count() == 1`，不存在滚动条消失后第二次 logical target 改写。

**后置条件**：logical zoom 保持 reference fit 值；observer 保留供后续轨迹 oracle 使用。

### TC-FIT-03-MONOTONIC-SHRINK

**测试目的**：单独判定 `AC-FIT-03-MONOTONIC-SHRINK`，捕获即使最终正确也不可接受的中途反向放大帧。

**前置条件**：图片稳定在 100%；`ZoomIssueProbe` 已连接动画、paint、resize、layout、scroll range/value 和 timer 事件；首个样本为 100% 图片尺寸。

**输入数据**：第二次真实 `Z` 后收集的有序 `ZoomIssueSample` 序列；每轴允许 1 DIP 映射/取整容差。

**操作步骤**：遍历全部样本；将相邻样本的图片宽、高分别比较；任何一轴增长超过 1 DIP 时增加 `reversalCount`，同时保留可诊断的 phase/尺寸转换列表。

**预期结果**：`reversalCount == 0`；从 100% 到 fit 的全部可观察尺寸只减不增，不能用正确终态抵消一次错误帧。

**后置条件**：尺寸转换序列仅用于日志与断言，不改变 view；继续执行末帧和静默窗检查。

### TC-FIT-04-NO-TERMINAL-RESCALE

**测试目的**：单独判定 `AC-FIT-04-NO-TERMINAL-RESCALE`，证明 animation 最后一次 property 写入后，finished 回调不会因 scrollbar 布局变化再修改图片尺寸。

**前置条件**：probe 至少采集一个 `animation-value` 样本和一个 `animation-finished` 样本；返回动画正常结束。

**输入数据**：`lastAnimationValueSize`、`animationFinishedSize`、1 DIP 等价容差，以及带 phase 的尺寸转换列表。

**操作步骤**：提取最后一个 `animation-value` 尺寸；提取 `animation-finished` 时尺寸；逐轴计算绝对差。

**预期结果**：两个样本均有效，宽度差≤1 DIP 且高度差≤1 DIP；finished 边界没有反向 correction。

**后置条件**：记录 finished 尺寸作为 terminal/quiet 比较基准；不启动第二段动画。

### TC-FIT-05-QUIESCENT-FINAL-STATE

**测试目的**：单独判定 `AC-FIT-05-QUIESCENT-FINAL-STATE`，覆盖 animation finished 以后仍可能写 transform 的 anchor、constraint、expensive-scale 或 scrollbar-geometry 延迟路径。

**前置条件**：返回动画已经发出 finished；`waitForZoomTerminal()` 能同时观察所有已知 writer；已保存 reference-fit 与 animation-finished 尺寸。

**输入数据**：writer terminal 尺寸、额外 `QTest::qWait(650)` 后 quiet 尺寸、reference-fit 尺寸、H/V range、1 DIP 容差。

**操作步骤**：等待所有 writer inactive 并记录 terminal；额外运行事件循环 650 ms 后记录 quiet；比较 finished/terminal/quiet/reference，随后检查 fit 状态和两轴 range。

**预期结果**：四个尺寸每轴差≤1 DIP；静默窗内无尺寸变化；最终 `isImageAtFit()` 为 true 且 H/V range 均为零。

**后置条件**：view 处于可继续交互的稳定 fit 状态；测试清理可安全关闭窗口。

### TC-FIT-06-CROSS-FIXTURE

**测试目的**：单独判定 `AC-FIT-06-CROSS-FIXTURE`，用独立来源但相同宽高比的图片交叉验证几何结论，而不是只针对一个编码文件调参。

**前置条件**：QtTest data function 可创建临时图片；现场路径的注册逻辑只在文件可读时启用；每个 row 运行完全相同的动态 oracle。

**输入数据**：固定 `2560×2938` 合成 raster；默认 `3840×4407` 现场 JPEG（若可读）；加载后 `loadedPixmapSize` 的显式期望值。

**操作步骤**：始终注册并运行 `synthetic-same-aspect-ratio`；现场路径可读时注册 `provided-3840x4407-jpeg`；每行加载后先断言 source size，再执行 AC-01 至 AC-05 的所有 oracle。

**预期结果**：合成 row 必须 PASS；现场文件存在时该 row 必须实际运行且 PASS；两个 row 均报告 `reversals=0`、`zoom_writes=1` 和一致的 animation-end/terminal/quiet 尺寸。

**后置条件**：临时 raster 随 `QTemporaryDir` 回收；现场 JPEG 保持只读未修改；每个 data row 独立创建和销毁窗口。

### TC-DYNAMIC-FIT-RETURN-TRAJECTORY

**测试目的**：在完全可移植的环境中动态覆盖全部六条原子标准，尤其证明真实 `Z` 返回 fit 时尺寸单调缩小、动画边界无二次 rescale、延迟 writer 静默后仍稳定。

**前置条件**：已以 `BUILD_TESTS=ON` 构建 `fovelle_tests`；Qt 使用 Cocoa 平台插件；测试能够创建并激活 GUI 窗口；测试进程拥有独立、可恢复的 `QSettings` 状态。

**输入数据**：测试运行时创建的 `2560 × 2938` Qt raster/XPM 图片；窗口请求尺寸 `500 × 550`；快捷键 `Z`；稳定观察窗 650 ms；尺寸容差 1 DIP。

**操作步骤**：

1. 加载合成图并等待初始 fit、所有缩放 writer 和 event queue 稳定，记录 `referenceFitLevel` 与 `referenceFitSize`。
2. 用 `QTest::keySequence(window, QKeySequence(Qt::Key_Z))` 进入 100%，等待稳定并断言 zoom 为 1.0、H/V scrollbar range 均非零。
3. 安装 `ZoomIssueProbe` 与 `QSignalSpy`，再次发送真实 `Z`。
4. 在事件刚分发后断言 logical zoom target 已等于 `referenceFitLevel`，随后等待 animation finished 和全部 writer inactive。
5. 遍历全部 probe 样本，计算相邻宽高的 `reversalCount`。
6. 比较 animation 最后值、finished、terminal、quiet 和 reference-fit 尺寸；检查 zoom-level signal 次数、fit 状态与两轴 range。

**预期结果**：覆盖 `AC-FIT-01-REAL-Z-ROUND-TRIP` 至 `AC-FIT-06-CROSS-FIXTURE`；`reversalCount == 0`；zoom-level signal 恰为 1 次；所有终点尺寸每轴差不超过 1 DIP；650 ms 后仍为 fit 且 H/V range 都为 0；QtTest data row `synthetic-same-aspect-ratio` PASS。

**后置条件**：销毁窗口和临时图片；恢复原 action shortcut 与测试涉及的全部设置；无残留 timer、动画或外部文件修改。

### TC-SYSTEM-PROVIDED-JPEG

**测试目的**：使用用户报告问题的真实 JPEG 交叉验证同一动态 oracle，排除合成图片编码、加载器或分辨率选择导致的假阴性；覆盖全部六条原子标准并直接落实 `AC-FIT-06-CROSS-FIXTURE`。

**前置条件**：除动态用例公共条件外，`/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg` 必须在执行机上可读；若需迁移，可通过 `FOVELLE_TOGGLE_FIT_SAMPLE` 指向等价现场图片。默认路径不可读时该 data row 不注册，不影响可移植门禁。

**输入数据**：用户提供的 `3840 × 4407` JPEG；真实 `Z` 快捷键；与 `TC-DYNAMIC-FIT-RETURN-TRAJECTORY` 相同的窗口、设置、观测器、1 DIP 容差和 650 ms 静默窗。

**操作步骤**：

1. QtTest data function 检查环境变量指定路径或默认现场路径是否存在。
2. 若存在，注册 `provided-3840x4407-jpeg` data row，并确认加载后的图片尺寸为 `3840 × 4407`。
3. 完整复用动态用例的 fit→100%→fit 两次真实 `Z` 操作和逐事件轨迹采集。
4. 分别核验 logical target、signal 次数、单调性、动画边界、terminal/quiet/reference 一致性、fit 状态及 H/V range。

**预期结果**：data row `provided-3840x4407-jpeg` PASS；记录中显示 100% 到 fit 的尺寸只减不增；animation-end、terminal 与 quiet 尺寸一致；全部六条 AC 判定为 PASS。

**后置条件**：现场 JPEG 只读且内容、时间戳不被测试修改；窗口、observer 和设置被清理；未挂载现场卷时报告明确标记为“条件未执行”，不得伪称真实样本通过。

## 5. 测试代码与执行入口

| 层级 | 固化位置 | CTest 名称 | 主要产物 |
| --- | --- | --- | --- |
| 静态 | `tests/toggle_fit_stability_static.py` | `FovelleToggleFitStabilityStatic` | `build/test-results/toggle-fit-stability-static.json` |
| 动态/系统 | `tests/tst_qviewtests.cpp` | `FovelleToggleFitStabilityAcceptance` | QtTest/CTest 日志与 `TOGGLE_FIT_STABILITY` 行 |

推荐执行：

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build \
  -R '^FovelleToggleFitStability(Static|Acceptance)$' \
  --output-on-failure
```

若要明确指定现场样本：

```bash
FOVELLE_TOGGLE_FIT_SAMPLE='/absolute/path/to/sample.jpeg' \
  build/tests/fovelle_tests \
  testToggleFitReturnHasMonotonicStableTerminalSize -v1
```

## 6. 判定约束

- “最终是 fit”但轨迹发生过反向增长：FAIL。
- 轨迹单调但动画 finished 后或 650 ms quiet 窗内尺寸变化：FAIL。
- 返回 target 依赖 100% 状态下当前滚动条占位，或一次事务产生多次 zoom-level write：FAIL。
- 合成 fixture 未执行：FAIL；现场路径存在却未注册/未执行：FAIL。
- 现场路径确实不可访问：只将系统交叉行标记为条件未执行，静态与可移植动态门禁仍必须 PASS。
