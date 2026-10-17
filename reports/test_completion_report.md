# HDR跨应用返回偏暗：完整源缓存完成报告

日期：2026-10-17。用户环境：macOS27、内建屏幕；真实文件 /Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG。前一任务报告备份于evidence/hdr_surface_metadata_20261017/previous_*。

## 完成内容

macOS15+持久化CGImage路径改为缓存完整HDR源端点和准确的source内容余量，由Core Animation Automatic负责动态显示适配。临时current回落不再被预先烘焙进缓存，也不会为其反复销毁重建。Metal与旧系统回退保留原current策略；此前焦点、timer及任务代次隔离保留。

1.JPG为8064×6048、内容余量4.9473、约390MiB半浮点缓存，走预算内persistent路径。此次已将用户文件加入可通过FOVELLE_HDR_REPRO_IMAGE配置的真实回归，而非仅使用仓库小素材。

## 验收结果

| 检查 | 结果 | 证据 |
|---|---|---|
| 新surface/原headroom静态与动态专项 | 4个CTest目标通过；包含用户图数据行 | evidence/hdr_surface_metadata_20261017/final-surface.log |
| 完整ImageCoreAndMovieTests，配置用户图 | 27 passed、0 failed、0 skipped | evidence/hdr_surface_metadata_20261017/final-image-core.log |
| 完整HDRPolicyTests，真实用户JPEG作端点保真样本 | 26 passed、0 failed、0 skipped | evidence/hdr_surface_metadata_20261017/final-policy-user-image.log |
| 机型静态/动态及4组HDR信息页/缓存静态门禁 | 6个CTest目标全部通过 | evidence/hdr_surface_metadata_20261017/final-contracts.log |
| 生产构建及最终恢复后的测试构建 | 成功 | evidence/hdr_surface_metadata_20261017/final-app-build.log、restored-build.log |
| git diff --check | 通过 | 命令退出0 |

QtTest计数含init/cleanup；重复出现在专项/完整套件中的数据行不重复计为覆盖。

## 检索、反证与修正过程

上次“像素恢复相同即修复”的结论不足以覆盖重复反馈。最初检查元数据后发现，正常JPEG/RAW与用户图已能携带正确的CGImage标签；layer覆盖值0在图像标签已知时合法。因此撤回“正常图像普遍缺标签”的初步猜测，也没有把该错误断言当作产品失败。更深入测试发现RAW SDR边界不保证明确的1标签，显式标记可统一解释，但只补标签仍不能解决完整内容范围被压低的问题。

旧应用物化路径会按瞬时显示余量预先压缩；用户图在部分恢复余量下，缓存标签从完整4.9473变为2.97365。其后还有Core Animation Automatic适配，存在双重映射和EDR需求反馈。Apple官方当前/潜在余量、HDR图像和CALayer文档、SDK注释以及实际CALayer.contents读数共同支持改用完整源缓存、由系统单独动态适配的设计。

user-app.log记录到current从5.52降至1.2、potential仍16。该桌面诊断尝试期间曾额外启动安装版实例，不能把全部AX操作和日志当作已完成无歧义的物理跨应用重现；两测试实例已结束。最终验证以明确配置用户文件的原生测试程序为准，不把截图或AX内容当作亮度测量。

## 逆向变异验证

修复通过后，临时仅撤回“完整源选择”和“现代monitor不再按current重建”两段逻辑，恢复旧current烘焙行为，保留最终测试与元数据复制。运行用户图单行：mutation-user-image.log显示source=4.9473、restored_tag=2.97365，新完整源断言失败，退出1。

随后finally恢复原生产源码并重新构建，完整27项测试及26项HDR政策测试通过。此实验表明测试能够区分新的映射所有权与旧路径，不是仅调整预期使其变绿。变异构建/恢复构建分别保留在mutation-build.log、restored-build.log。

## 原子标准追踪

- A1：仓库JPEG、RAW及用户1.JPG均读取真实CALayer.contents半浮点像素和CGImage标签；完整源内容标签准确。
- A2：current从16降到后台1，返回较低余量后，缓存字节/对象身份保持完整源基线；不会重新物化较暗版本。
- A3：current到1也保留完整源信号，系统负责实际映射；Metal纯策略仍证明current=1时不得用potential16虚构可用余量。
- A4：元数据按任务捕获，serial/图片代次/预算保留；稳定缓存不重建，换图清理通过；旧OS API有可用性保护。
- A5：用户图通过新专项及独立真实完整端点颜色保真测试；既有关闭恢复、正常输出查询、ICC、APNG、机型/时间回归通过。

## 测试解释的变化

旧testHDRHeadroomSurfaceRestores要求current降低时缓存像素改变，那是在应用自行烘焙适配前提下的测试。新的现代persistent分支要求完整源像素和身份稳定，由系统处理最终显示；保留旧系统分支原断言，新增独立完整源端点比对和用户图变异实验，避免把修改测试本身当作修复证据。

## 复现命令

```sh
cmake --build build --target fovelle_tests Fovelle -j 6
FOVELLE_HDR_REPRO_IMAGE='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG' ctest --test-dir build -R '^FovelleHDR(SurfaceMetadata|HeadroomRestore)' -V
FOVELLE_HDR_REPRO_IMAGE='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG' FOVELLE_TEST_SUITE=ImageCoreAndMovieTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests -v1
FOVELLE_HDR_JPEG_SAMPLE='/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test/1.JPG' FOVELLE_TEST_SUITE=HDRPolicyTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests -v1
ctest --test-dir build -R '^Fovelle(CameraModel|HDR(SurfaceMetadata|HeadroomRestore|InfoOutput|InfoRestore)Static)' -V
git diff --check
```

## 交付及边界

本次构建为build/Fovelle.app，未替换/Applications/Fovelle.app或发布推送。真实用户图已用于像素/元数据及原生呈现回归；没有亮度计，尚不能宣称测得该内建屏幕跨应用恢复后的绝对nits或完整排除系统亮度/电源策略。旧OS路径主要静态核验，未在旧系统运行。当前没有未解决的本次自动化失败。
