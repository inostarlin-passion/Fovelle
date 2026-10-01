# 全屏同步快照与既有交接回归：测试用例说明

日期：2026-10-01。目的：修正此前没有观察到提交前像素工作的覆盖缺口，同时保持既有全屏回归。设计依据见 [技术设计](technical_design_document.md)，版本／记录见 [summary](evidence/fullscreen_snapshot/summary.json)。

## TC-FS-SNAPSHOT：不可变快照复用、失效及原生布局隔离

入口：`WindowBehaviorTests::testFullScreenSnapshotReuse`，四行数据为 rotate90、mirror、flip、identity。每行自动生成4096×3072四象限 XPM，通过现有异步加载器真实加载至 MainWindow；XPM 绕过 native SDR 的限尺寸代理，断言实际像素量，窗口640×480、fit、关闭自动改窗口尺寸。

1. 取加载后未旋转的已管理颜色作参照，应用相应方向，调用公开 `MainWindow::fullScreenTransitionImage()` 并执行原生 provider 使用的预乘 RGBA8 转换。
2. 检查完整图片尺寸、四角颜色和色彩空间。90° 输出3072×4096，其余4096×3072；每张 RGBA 输出50,331,648字节，约48MiB。
3. 保持 first QImage 存活，连续8次相同请求；要求所有 constBits 与 first 相同。指针读取只读，不触发复制；first 存活排除分配器复用旧地址的假阳性。计时只作诊断，不使用随机器速度变化的阈值替代资源断言。
4. 真正进入并退出 macOS 原生全屏，分别等公开原生 entries／exits 计数增加；Qt 请求状态不足以结束等待。完成后相同源／方向输出仍须复用，确认布局和 zoom 不使缓存失效。
5. 调用方复制 first 后 fill 紫色，原输出颜色须保持不变；证明共享写入 detach 不污染生产缓存。
6. 再旋转90°，输出尺寸改变、缓冲不同于 first，后续请求复用新输出。
7. 在同一路径重写为青色并 reload，等新的 fileChanged 和加载完成；要求画面变更、alpha为255、缓冲不同于旧方向，再次请求复用新源。青色按色彩管理后的通道关系检查，方向四角仍按已加载像素严格比较。
8. 汇总所有复用失败，输出一条 `FULLSCREEN_SNAPSHOT` JSON，再断言无失败。每行必须完成两个原生方向；清理窗口、退出残留全屏并恢复 quit 策略。

`first_oriented_ms` 是方向设置后第一次测量；此前已取未旋转颜色参照，identity 行不能称为冷启动。`warm_8_ms` 是8次公开 provider＋格式转换的累计 CPU 墙钟时间；不是一次切换时间、真实屏幕帧率或首次可见响应。`rebuilds` 统计不同完整 RGBA 输出缓冲的请求数，不统计变换内所有临时分配。

稳定检出要求：冻结最终测试，在本轮生产起点上跑三轮；每轮四行都应因为重复输出而失败，不能把颜色期望错误或缺失原生完成当作有效红证据。只改生产快照逻辑，保持相同最终用例再跑三轮，应四行全过且无跳过。

## 系统门禁负向验证

新增 `snapshot_summary()` 要求 rotate90／mirror／flip／identity 恰好各一条，rebuilds=0、native_directions=2、failures为空、完整实际像素量／字节量，以及非负有限时间。空输出、缺行、重复行、一次重建、原生方向未齐、失败列表非空、NaN时间、零像素、错误字节及缺字段均拒绝；最终红记录拒绝、绿记录接受。记录见 [gate-validation](evidence/fullscreen_snapshot/gate-validation.json)。这是遥测变异检查，不能作为生产故障注入的替代。

## 已有回归

| 用例／门禁 | 继续保障的行为 |
| --- | --- |
| FovelleFullScreenMotion | 标题栏显示／隐藏、raster／vector、idle／busy、两循环两方向；130ms主线程受控停顿下呈现轨迹继续 |
| FovelleFullScreenPaintBudget | 12个直接 Update 样本，40ms注入时仅一次视口 Paint |
| FovelleFullScreenPreparationBudget | 8个原生方向过程；源0–1次、终点恰好1次 Paint，40–75ms受控终点成本 |
| FovelleHiddenTitlebarFullScreen | 原生标题栏状态及全屏生命周期 |
| FovelleSDRFullScreenPresentation | 实际 SDR AVIF 画面连续性与接管位置 |
| GraphicsView fit／pan／overflow | 窗口尺寸、退出平移与标题栏 padding 业务回归 |

上述运动指标来自呈现轨迹；Paint 延迟来自受控注入。两者均不是物理显示帧统计。快照用例自动生成 fixture；AVIF 回归依赖现有本机 fixture，可通过 `FOVELLE_FULLSCREEN_AVIF_IMAGE` CMake 配置异机路径。缓存动画帧清理、空源清理及超大图片内存峰值本轮仅源码审查，未增加动态故障注入。

## 执行命令

```bash
cmake --build build --target fovelle_tests Fovelle -j 6
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 QTEST_FUNCTION_TIMEOUT=60000 FOVELLE_TEST_SUITE=WindowBehaviorTests build/tests/fovelle_tests testFullScreenSnapshotReuse -v1
ctest --test-dir build -R 'Fovelle(SDRFullScreenPresentation|HiddenTitlebarFullScreen|FullScreen)' --output-on-failure
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests --output reports/evidence/fullscreen_snapshot/system.json
QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 FOVELLE_TEST_SUITE=GraphicsViewTests build/tests/fovelle_tests testFitZoomSurvivesInverseWheelStepsAndFullscreenResize testFullscreenExitPreservesVerticalPan testFullscreenAfterOverflowRemovesTitlebarScenePadding -v1
```

原生测试串行执行，不能并发切换这些窗口。之前终点交接用例说明保存在 [prior_reports](evidence/fullscreen_snapshot/prior_reports/)。
