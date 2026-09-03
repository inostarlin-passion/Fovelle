# 图片缩放垂直滚动条跳变：测试完成报告

> 本轮验证日期：2026-09-03
>
> 验证提交：Fovelle `b378540e16a4c90a205320ea05a3653460ebd9a7`
>
> 环境：macOS Cocoa、Qt 6.11.1、普通 DPR 与 `QT_SCALE_FACTOR=2`

## 1. 完成结论

本次修复已将“垂直滚动条跳变”从只看终态 value 的弱检查，升级为同时检查数值、thumb、物理控件 geometry、图片锚点、延迟回调和真实输入矩阵的全过程验证。

当前代码在本机 Qt 6.11.1 / macOS Cocoa 下，普通 DPR 与 `QT_SCALE_FACTOR=2` 专项轨迹测试均通过。最终可核验结论限定为：在本报告第 3 节的显式矩阵和前提内，没有观测到可绘制或已提交的错误垂直滚动条位移；测试能够检出“中间跳开、最终又恢复”的 A→B→A 情形。

这不是“任意平台、任意窗口、任意图片都绝不可能跳变”的证明；未覆盖范围见第 7 节。

## 2. 对原先绿色测试的复核结论

肉眼看到的现象与旧测试通过并不矛盾，因为旧测试验证的是数值/相对 thumb，而不是滚动条控件的屏幕几何。

旧 trace 的代表性序列为：

```text
v_value:          291 → 291 → 291
v_bar_global_rect: (1169,291,15,480)
                  → (1169,263,15,508)
                  → (1169,291,15,480)
```

旧 `validateZoomTrace()` 没有消费已记录的 `verticalBarGlobalRect`；旧 actual/expected thumb 又都基于同一当前 `QScrollBar` geometry 调用 style，因此整个 container 移动时，两者同步移动仍会相等。结果是 value oracle 绿色、视觉却红色。

本次新增的 `verticalBarGlobalRect` 与 `verticalBarContainerGlobalRect` 基线比较，直接关闭了这个证据缺口。修复前的独立 geometry 检查已经捕获了上述 `y=291→263→291` 轨迹；修复后该阶段不再出现错误 paint geometry。

## 3. 原子验收结果

| 原子标准 | 判定内容 | 固化代码 | 结果 |
| --- | --- | --- | --- |
| `AC-ZOOM-VBAR-VALUE` | 每个 checkable sample 的 value 与不读取 actual value 的 `V*` 一致 | `zoomTraceSampleError()` / `validateZoomTrace()` | PASS |
| `AC-ZOOM-VBAR-GEOMETRY` | bar/container global `x/top/width` 无可见错误往返移动 | `ZoomTraceProbe::record()` / `validateZoomTrace()` | PASS |
| `AC-ZOOM-VBAR-ANCHOR` | 同一归一化图片点保持在目标 viewport，误差 ≤2 DIP | `ZoomTraceProbe` anchor oracle | PASS |
| `AC-ZOOM-VBAR-THUMB` | actual thumb 与当前 `QStyle` 的 expected thumb 一致 | `zoomScrollBarThumbRect()` / `zoomTraceSampleError()` | PASS |
| `AC-ZOOM-VBAR-ASYNC` | animation、geometry、anchor、constraint、Expensive writer 结束后无回弹 | live timer/quiet/terminal checks | PASS |
| `AC-ZOOM-VBAR-MATRIX` | keyboard、wheel、pinch × in/out × Disabled/Expensive × normal/HiDPI | data-driven QtTest + 两个 CTest | PASS |

总标准 `AC-ZOOM-VBAR-TRANSIENT`：PASS。

## 4. 实现变更

- `QVGraphicsView` 增加具名 `verticalScrollBarGeometryTimer`，采用 single-shot、0 ms、coalesced 调度；timeout 可被测试观察并在 terminal 检查 inactive。
- bar/container 的 `Move/Resize/Show` 事件在下一次 paint 前同步调用安全区 geometry 修复；`isUpdatingVerticalScrollBarGeometry` 防止 `setGeometry()` 引起递归，0 ms timer 继续作为兜底。
- 保留 displayed-frame range、Expensive backing pixmap 的归一化 anchor UV 重基准、pending-anchor generation 和手动滚动取消逻辑。
- 轨迹测试改为 12 行：真实 keyboard shortcut、真实 wheel、真实 native pinch；两个方向、两种 scaling mode 和中心/非中心图片锚点均覆盖。
- failure trace 增加 event object、viewport/bar/container global rect；失败 frame 在事件循环回合结束后捕获，避免从 paint 回调中递归 `grab()`。
- baseline 建立前等待平台初始化的 `100×30` scrollbar placeholder 消失，避免把不可绘制的初始化 geometry 当作起点。

## 5. 实际执行记录

### 5.1 构建

```bash
cmake --build build --parallel 2
```

结果：`Fovelle`、`fovelle_tests`、`fovelle_native_drag_helper` 构建成功。

### 5.2 静态测试

```bash
python3 -m py_compile tests/scrollbar_zoom_acceptance_static.py
python3 tests/scrollbar_zoom_acceptance_static.py \
  --repo . --output build/test-results/scrollbar-zoom-acceptance-static.json
```

结果：返回码 0；15/15 项静态合同通过，源码/测试/CTest 合同、六字段结构、geometry oracle、真实 pinch、非中心锚点、具名 timer 和失败证据检查均为 `pass=true`。

### 5.3 普通 DPR 动态轨迹

```bash
ctest --test-dir build \
  -R '^FovelleZoomScrollbarTrajectory$' --output-on-failure
```

结果：12 行数据执行 deterministic + live 两阶段，测试通过，耗时 13.18 秒。

### 5. HiDPI 动态轨迹

```bash
ctest --test-dir build \
  -R '^FovelleZoomScrollbarTrajectoryHiDpi$' --output-on-failure
```

结果：独立 `QT_SCALE_FACTOR=2` 进程的 12 行数据执行 deterministic + live 两阶段，测试通过，耗时 39.42 秒。

### 5.4 默认全量 CTest

```bash
ctest --test-dir build --output-on-failure --timeout 120
```

结果：`4/4` 通过，总耗时 110.48 秒：

- `FovelleTests`：54.35 秒；
- `FovelleShortcutSettingsTests`：2.49 秒；
- `FovelleZoomScrollbarTrajectory`：13.24 秒；
- `FovelleZoomScrollbarTrajectoryHiDpi`：40.39 秒。

### 5.5 证据文件

- 静态机器证据：[scrollbar-zoom-acceptance-static.json](/Users/inostarlin/code/Fovelle/build/test-results/scrollbar-zoom-acceptance-static.json)
- 失败时的完整轨迹目录：`/Users/inostarlin/code/Fovelle/build/test-results/zoom-vbar/<case>/<stage>/`
- 技术设计：[technical_design_document.md](/Users/inostarlin/code/Fovelle/reports/technical_design_document.md)
- 结构化用例：[test_case_specification.md](/Users/inostarlin/code/Fovelle/reports/test_case_specification.md)

## 6. 证据链与联网核验

结论来自本地源码、动态 trace 和 Qt 官方资料的交叉验证：

1. [Qt `QAbstractScrollArea`](https://doc.qt.io/qt-6/qabstractscrollarea.html#details)确认 `ScrollBarAsNeeded` 的显示会改变 viewport 可用尺寸，解释了 H/V layout 交叉影响。
2. [Qt `QPropertyAnimation`](https://doc.qt.io/qt-6/qpropertyanimation.html)确认动画属性存在中间插值状态，解释了只看终点为什么不充分。
3. [Qt `QGraphicsView`](https://doc.qt.io/qt-6/qgraphicsview.html)和 [Qt 6.11.1 `qgraphicsview.cpp`](https://github.com/qt/qtbase/blob/v6.11.1/src/widgets/graphicsview/qgraphicsview.cpp)确认 scene/transform/viewport/range 的联动。
4. [Qt `QStyle`](https://doc.qt.io/qt-6/qstyle.html)支持用当前平台 style 计算 thumb，而不是用固定像素模板。
5. [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html)支持把 widget geometry 以 DIP 记录，并用独立 `QT_SCALE_FACTOR=2` 进程验证 DPR 差异。

链式推理为：

```text
AsNeeded layout 改变 viewport
  → vcontainer 产生新的 Move/Resize
  → 原匿名 0 ms 修复晚于一次 paint
  → bar y 从 safeTop 暂时到 viewportTop 再返回
  → value 可能完全不变，旧 value/thumb oracle 仍可通过
  → 独立 global geometry oracle 才能检出
```

## 7. 边界与未覆盖项

- 当前证据只覆盖 Qt 6.11.1 Cocoa/arm64、测试使用的窗口/fixture 尺寸、三种输入、两个 scaling mode、两个方向和两个 DPR。
- 系统级 Accessibility/HID 驱动未纳入默认 CTest；它依赖外部权限和外部图片路径。
- 未证明 GPU 合成器在 QWidget 已提交正确 geometry 后自行显示错误帧的情形。
- 变异校准不作为本次通过的必要前提；已用真实旧 geometry 缺陷产生的 RED trace 验证了新增 oracle 能捕获“value 不变但控件跳变”。若后续要做 stale-range 或单帧 `V+24` mutant，应在 disposable worktree 中执行，不能修改正式源码。
- 如用户在当前版本仍复现，首先保留 `trace.json` 和三帧图，按 value/range、bar geometry、anchor、timer phase 四类证据定位，不应仅凭终态或肉眼截图作单一归因。
