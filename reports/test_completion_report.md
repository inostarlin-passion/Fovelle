# Toggle Fit and 100% 返回适合窗口稳定性：测试完成报告

> 报告日期：2026-09-03
>
> 被测工作树：`/Users/inostarlin/code/Fovelle`（未提交）
>
> 基线提交：`40a4bc318f3cf80881703f34cb1b57881d964668`
>
> 环境：macOS / Cocoa / arm64 / Qt 6.11.1

## 1. 结论

本轮结论：**PASS**。修复后，从 100% 返回“适合窗口”的逻辑目标在第二次 `Z` 分发时即固定；合成图片与用户现场 JPEG 的缩小轨迹均无反转，动画 finished 后没有二次 rescale，所有延迟 writer 静默 650 ms 后尺寸仍稳定。

本轮重新配置并构建成功；最终一次完整、单次串行 CTest 执行中，当前 8 个项目为 8/8 PASS，总耗时 175.84 s。专项目标门禁包含 1 个静态 CTest 和 1 个动态 CTest；动态 CTest 内的合成图、现场 JPEG 两个 data row 均为 PASS。

## 2. 修复前失败证据

新增动态回归在原实现上先失败，合成同宽高比 fixture 的关键轨迹为：

```text
100%                 2561×2939
animation-value       824×945
animation-value       822×943
animation-finished    835×958
```

动画已缩至 `822×943`，finished 后却反向放大到 `835×958`，宽高分别增加 13/15 DIP。该失败同时击中：

- `AC-FIT-03-MONOTONIC-SHRINK`；
- `AC-FIT-04-NO-TERMINAL-RESCALE`；
- `AC-FIT-05-QUIESCENT-FINAL-STATE`。

增量与滚动条释放的 viewport 增量同量级，支持“100% 当前 viewport 被 AsNeeded 条缩小，返回动画先使用旧 viewport 算出过小 target；条消失后 finished 再按扩大的 viewport 改写比例”的根因链。

## 3. 实现完成项

### 3.1 生产代码

- 在 `QVGraphicsView` 增加 `getFitViewportSize(bool addOverscan)`。
- helper 以 `maximumViewportSize()` 获取无有效 scrollbar range 时的 viewport，再扣除 Fovelle 原生标题栏遮挡，并按需加入两侧 `fitOverscan`。
- `recalculateZoom()` 和 `calculateZoomLevelForMode()` 统一使用该 helper，保证 Toggle 目标判定、动画目标与 finished 核验使用同一几何合同。
- 未改变 `Z` 状态机、200 ms `OutCubic` 动画、方向性 anchor、scrollbar policy 或其他 zoom mode。

### 3.2 测试代码

- 增加静态门禁 `tests/toggle_fit_stability_static.py`，输出逐检查 JSON。
- 增加 data-driven QtTest `testToggleFitReturnHasMonotonicStableTerminalSize`：通过真实 `Z`，采集 animation、paint、resize、scrollbar 与 timer 轨迹。
- 合成 row 固定执行；用户提供的 `/Volumes/CRYSTAL/画作/GALLERY/153 Poolside - Yellow Towel - 永井博 2019.jpeg` 存在时注册真实 JPEG row，并显式断言加载源尺寸为 `3840×4407`。
- CTest 分别注册 `FovelleToggleFitStabilityStatic` 和 `FovelleToggleFitStabilityAcceptance`，以便静态/动态独立判定。
- 完整套件复验发现既有 `testScrollBarGeometryMatchesViewMetricAndDoesNotRebound` 在 2× 动画尚未结束时读取动态增长中的 scrollbar maximum；增加 `waitForZoomTerminal(view)` 固化其“2× 已稳定”前置条件，未改变产品代码或 oracle。

## 4. 原子标准追溯与结果

| 原子标准 | 专属结构化用例 / 固化 oracle | 结果 |
| --- | --- | --- |
| `AC-FIT-01-REAL-Z-ROUND-TRIP` | `TC-FIT-01-REAL-Z-ROUND-TRIP`：两次真实 `QTest::keySequence(..., Z)`；终态 `isImageAtFit()` 且 H/V range=0 | PASS |
| `AC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET` | `TC-FIT-02-SCROLLBAR-INDEPENDENT-TARGET`：第二次 Z 后 target=`referenceFitLevel`；两个 row 的 `zoom_writes=1` | PASS |
| `AC-FIT-03-MONOTONIC-SHRINK` | `TC-FIT-03-MONOTONIC-SHRINK`：全事件相邻尺寸 `reversalCount == 0`；两个 row 的 `reversals=0` | PASS |
| `AC-FIT-04-NO-TERMINAL-RESCALE` | `TC-FIT-04-NO-TERMINAL-RESCALE`：last animation value 与 finished 尺寸每轴差≤1 DIP | PASS |
| `AC-FIT-05-QUIESCENT-FINAL-STATE` | `TC-FIT-05-QUIESCENT-FINAL-STATE`：finished/terminal/650 ms quiet/reference 一致且无滚动条 | PASS |
| `AC-FIT-06-CROSS-FIXTURE` | `TC-FIT-06-CROSS-FIXTURE`：合成 `2560×2938` row + 现场 `3840×4407` JPEG row 均实际执行并核验 source size | PASS |

## 5. 执行证据

### 5.1 构建

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --parallel 2
```

结果：PASS。CMake 配置/生成成功；`Fovelle`、`fovelle_tests` 和 `fovelle_native_drag_helper` 均构建成功。

### 5.2 静态验收

```bash
ctest --test-dir build -R '^FovelleToggleFitStabilityStatic$' --output-on-failure
```

结果：PASS；最终全套中的耗时为 0.09 s。`ST-FIT-01` 至 `ST-FIT-06` 全部通过，机器可读证据为 `build/test-results/toggle-fit-stability-static.json`。

### 5.3 动态与现场样本验收

```bash
ctest --test-dir build -R '^FovelleToggleFitStabilityAcceptance$' --output-on-failure
```

结果：PASS；最终全套中的 CTest 耗时为 5.46 s。专项 verbose 复验另耗时 5.45 s，QtTest 汇总为 4 passed、0 failed、0 skipped（含 init/cleanup），可审计日志为 `build/test-results/toggle-fit-stability-dynamic.log`。两条关键轨迹为：

```text
synthetic-same-aspect-ratio:
  start=2561x2939 animation_end=480x551 terminal=480x551
  quiet=480x551 reversals=0 zoom_writes=1

provided-3840x4407-jpeg:
  start=3841x4408 animation_end=480x551 terminal=480x551
  quiet=480x551 reversals=0 zoom_writes=1
```

现场文件 data row 并未跳过，证明本次执行实际读取了用户给出的 JPEG。

### 5.4 邻近回归

返回 fit 会触及通用 zoom/scrollbar 布局，因此在所有修订完成后用一条 `ctest --test-dir build --output-on-failure` 串行执行全部现有 CTest；合计 8/8 PASS：

| CTest | 结果 | 耗时 |
| --- | --- | ---: |
| `FovelleTests` | PASS | 83.84 s |
| `FovelleToggleFitStabilityStatic` | PASS | 0.09 s |
| `FovelleToggleFitStabilityAcceptance` | PASS | 5.46 s |
| `FovelleFiveIssueZoomAcceptance` | PASS | 15.83 s |
| `FovelleFourIssueZoomAcceptance` | PASS | 6.65 s |
| `FovelleShortcutSettingsTests` | PASS | 4.83 s |
| `FovelleZoomScrollbarTrajectory` | PASS | 14.47 s |
| `FovelleZoomScrollbarTrajectoryHiDpi` | PASS | 44.67 s |

总耗时 175.84 s。其中普通 DPR 与 HiDPI 滚动条轨迹、既有 Toggle/anchor/blank-space 验收均通过，未观察到相邻产品行为回归。

复验过程还保留了一条反证：在加入稳定等待前，旧端点测试可隔离失败并因未完成动画随窗口析构而 SIGSEGV；加入等待后连续隔离执行 5/5 PASS，随后完整套件 8/8 PASS。这证明所做修改补齐的是测试前置条件，而不是放宽边缘 oracle。

## 6. 交叉验证结论

| 证据层 | 能证明什么 | 不能单独证明什么 |
| --- | --- | --- |
| Qt 公共文档与同版本源码 | 无 range viewport 的 API 语义、AsNeeded 条与 viewport 的反馈关系 | Fovelle 现场一定无震荡 |
| 修复前失败轨迹 | 缺陷可复现且是实际几何反转，不只是观感 | 修复方法长期正确 |
| 静态门禁 | 两个 fit 入口及文档/代码追溯未漂移 | 运行时事件顺序 |
| 合成图动态测试 | GUI 事务、逐事件轨迹、动画末帧和静默窗 | 特定 JPEG 解码路径 |
| 用户现场 JPEG row | 真实分辨率和真实加载路径也满足同一 oracle | 外接卷不可用的其他机器环境 |

结论只在这些证据相互一致时成立；任何单层 PASS 都不能替代完整验收。

## 7. 已知边界

- 测试事务假设 200 ms 动画期间用户未主动 resize 或切换屏幕；外层几何真的变化时重新 fit 是预期行为。
- 1 DIP 是矩形映射和设备像素取整容差，不允许掩盖超过 1 DIP 的方向反转。
- 自动化验证应用提交的 transform/geometry；不覆盖显示器固件或 WindowServer 独立合成问题。
- 现场 JPEG data row 取决于外接卷可达性；不可达时必须如实标记条件未执行，不能由合成 row 冒充现场验证。
