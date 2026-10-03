# 全屏底部闪烁：测试用例说明

## 公共环境与输入

真实 macOS Cocoa 桌面，窗口能够激活且屏幕捕获可用；关闭棋盘背景、选择深色主题、禁用按图自动调整窗口。SDR 为测试生成的红色 800×1600 PNG，HDR 为真实 DNG；通过 `FOVELLE_HDR_FULLSCREEN_IMAGE` 指定，默认使用本地挂载样本 `3.dng`。不存在则 HDR 行 skip；存在但未识别 HDR 或表面未准备则失败。

`FOVELLE_FULLSCREEN_BOTTOM_EVIDENCE` 可选指定证据目录。未指定不写图片；指定时按数据行保存自然 `band-*.png`、受控 `fallback.png` 和 stdout 中带时间/坐标/颜色的 BOTTOM_DISPLAY 记录。捕获使用屏幕局部坐标，考虑屏幕原点；Retina 尺寸由实际截图决定。

## TC-BOTTOM-HANDOFF（四个独立数据行）

| 数据行 | 图像 | 恢复状态 |
|---|---|---|
| sdr-normal | SDR | 普通 720×500 |
| sdr-maximized | SDR | 最大化 |
| hdr-normal | 已准备 HDR | 普通 720×500 |
| hdr-maximized | 已准备 HDR | 最大化 |

步骤：建立并加载窗口→把图像缩小使稳态底部没有图片→进入全屏并等待原生 DidEnter→约 16ms 定时读取底部 20 点显示窄带→请求退出并等待 DidExit→等待 500ms 尾部→检查稳态中央颜色→受控隐藏原生 contentView 150ms→抓取中央底部 80×12 点→逐像素比较→恢复内容与设置。

期望：有至少五次自然采样、截图非空、稳态中央背景 RGB 误差≤2；受控交接区域所有像素 RGB 误差≤2且错误像素数为零；独立原生读色与主题基色一致。基线应在最后受控断言失败（本机每行 3840/3840 错误）；修复应通过。异常截图不能被当作成功。

该用例稳定检出**底层背景色不一致导致的受控交接闪白机制**，不是稳定复现用户自然退出闪烁的用例。自然过渡采样只作证据，不对移动快照 ROI 强行作白条判定，也不假设每一显示帧已捕获。

## TC-THEME-NATIVE-BACKGROUND

扩展 `testThemeAppliesNativeAppearanceAndViewportBackground`：浅色 appearance 与画布正确后，等待原生背景达到 `#969696`；设置深色并重新加载设置后，等待原生背景达到 `#212121`。每项等待上限 2s，不降低颜色要求。失败也清理窗口与 quitOnLastWindowClosed。用于防止仅深色全屏路径被局部硬编码修复。

## 回归与运行

新增 CTest：`FovelleFullScreenBottomBackgroundHandoff`（RUN_SERIAL，120s，单用例上限 60s）。系统驱动 `quality_fullscreen_system.py` 包含该函数。继续运行 SDR/HDR 几何连续性、原生全屏往返、标题栏、Escape、refinement、主题和棋盘测试。

复现命令（仓库根目录）：

```bash
FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa FOVELLE_FULLSCREEN_BOTTOM_EVIDENCE=reports/evidence/fullscreen_bottom_flash/local build/tests/fovelle_tests testFullScreenBottomBackgroundHandoff -v1
```

## 原始缺陷验收缺口

需要在用户实际显示配置下稳定抓到自然闪烁，并明确异常属于应用还是 Dock/桌面区域。当前测试尚不能稳定检出所有自然退出闪烁；该验收项不得因受控用例通过而标为完成。
