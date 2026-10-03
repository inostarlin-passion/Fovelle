# HDR全屏测试用例说明

## 测试夹具与硬性前提

函数 `WindowBehaviorTests::testHDRFullScreenVisualContinuity`。默认源图 `/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/3.dng`，可通过 `FOVELLE_HDR_FULLSCREEN_IMAGE` 覆盖。正常窗口720×500、ZoomToFit、禁用窗口自动缩放和棋盘背景。断言文件可读、图像已加载、真实原生HDR标志、内容headroom>1、持久HDR准备完成；探针只接受可见16位浮点CGImage。任何前提不成立都失败，不能静默改成SDR或skip。

进入/退出都以AppKit Did计数确认完成。退出行先完成进入，待稳态通过后才开始退出采样。可见/隐藏标题栏各五行，共十行。

|编号|数据行前缀|问题|验收指标|
|---|---|---|---|
|TC-HFS-01|exit-bottom|退出底部闪烁机制|容器/背景底边误差≤2点；底部8行内部错误像素≤2|
|TC-HFS-02|enter-size|HDR进入尺寸跳变|原生resize样本宽高最大绝对误差≤2点|
|TC-HFS-03|enter-position|HDR进入位置跳变|原生resize样本图像中心欧氏距离≤2点|
|TC-HFS-04|exit-size|HDR退出尺寸跳变|原生resize样本宽高最大绝对误差≤2点|
|TC-HFS-05|exit-position|HDR退出位置跳变|原生resize样本图像中心欧氏距离≤2点|

数据行后缀分别为 `-visible`、`-hidden`。每行都要求有效原生采样数>0、转换前后稳态几何和像素匹配；不允许零采样伪通过。稳态等待有界3秒，HDR准备等待有界15秒。实际通知回调中不等待、不抽事件、不调用生产刷新。

## 独立oracle

实际矩形由真实HDR层bounds经过CALayer坐标转换得到。预期矩形由实际源图尺寸、Qt viewportTransform及viewport到窗口偏移独立计算。尺寸和位置单独度量。

底部实际条带由真实CGImage与观察到的图层变换栅格化。参考条带保留同一像素/方向，将变换尺寸和位置独立归一化到Qt预期。两者使用同一背景、sRGB转换和8行采样，避免附着EDR层与未附着参考层的色调映射差异。排除最外侧1像素，RGB任一分量差>8计为错误像素。这是应用模型几何对应的底部内容验证，不是物理屏幕录像。

## 漏检原因与稳定性要求

此前测试只扫描SDR瓦片、使用纯红SDR PNG；未断言HDR路径。生产SDR专用同步跳过HDR，所以旧测试通过不能排除HDR错误。新增真实HDR探针和用例保留SDR原测试，并在系统门禁区分 `FULLSCREEN_CONTINUITY` 与 `HDR_FULLSCREEN_CONTINUITY`。缺失/重复行、零样本、非法数值、超限、HDR/SDR数据互相替代均失败。

最终测试先在未改生产基线连续三轮运行，各十行必须因对应视觉指标失败且稳态通过；修复后同一测试连续三轮十行全部通过。另一真实HDR格式完整运行一次基线/修复矩阵，交叉排除单一文件布局因素。探针探索期的稳态失败不计入最终统计。

## 执行与相关回归

```sh
cmake --build build -j 6
FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 \
  build/tests/fovelle_tests testHDRFullScreenVisualContinuity -v1
FOVELLE_HDR_FULLSCREEN_IMAGE='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG' \
  FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa QT_FATAL_WARNINGS=1 \
  build/tests/fovelle_tests testHDRFullScreenVisualContinuity -v1
ctest --test-dir build -R 'Fovelle(HDRFullScreenVisualContinuity|FullScreenVisualContinuity|NativeFullScreenRoundTrip|HiddenTitlebarFullScreen|FullScreenRefinement|FullScreenMetricsGate)' --output-on-failure
python3 tests/quality_fullscreen_system.py --binary build/tests/fovelle_tests \
  --output reports/evidence/hdr_fullscreen_continuity/system.json
```

需要真实macOS Cocoa桌面，原生窗口测试串行。继续回归SDR连续性、PNG/SVG原生全屏往返、快捷键、标题栏、昂贵精化、退出pan及padding。HDR未准备阶段、超预算Metal回退、多屏DPR和系统Dock/Space合成的全部物理帧不在本夹具证明范围内。
