# HDR信息页输出色彩空间测试完成报告

日期：2026-10-17（Asia/Shanghai）。本次修改保留工作区已有“拍摄机型”功能；前一任务三份报告备份见evidence/hdr_info_output_20261017/previous_*。

## 结论

A1–A5通过本次自动化验收。图片信息页对已成功呈现的当前HDR图，显示其正常HDR输出色彩空间，不再被打开对话框引起的临时SDR预览覆盖。实时诊断API及HDR失活/恢复策略保留；换图和关闭后信息继续刷新。

生产改动：QVGraphicsView新增只读fileInfoColorInformation查询，MainWindow信息页provider改用该查询。未缓存旧图信息，未硬编码色彩空间名，未暂停信息页timer。

环境：macOS 27.0.1 / arm64、Qt 6.11.2、Apple LLVM 17、CMake Release、QT_QPA_PLATFORM=cocoa。

## 红→绿证据

先完成设计、规格及D1/D2测试，再运行旧实现：before.log中gain-map.jpg和bayer.dng均失败，实际为“Preview: Display P3”，预期为正常呈现时记录的“Extended Linear Display P3”；静态门禁也因缺少独立信息页查询失败。之后补齐D3测试并实施生产修复。

修复后重新编译生产/测试目标，两个HDR素材及全部新增回退断言通过；日志保留真实原始失败，没有将预览文字改成测试预期。

## 测试结果

| 项目 | 结果 | 证据 |
|---|---|---|
| S1静态门禁 | 1通过 | evidence/hdr_info_output_20261017/after.log |
| D1/D2真实HDR场景 | JPEG增益图、RAW两组通过；每组两轮开关/700ms刷新及换图/关闭 | 同上 |
| D3回退边界 | 空状态、Qt栅格、原生SDR及关闭状态通过 | 同上 |
| HDR专项QtTest | 5 passed、0 failed、0 skipped，含init/cleanup | 同上 |
| 拍摄机型静态/动态专项（含时间格式） | 通过；动态11 passed、0 failed、0 skipped | 同上 |
| CTest目标合计 | 4/4通过，0失败 | 同上 |
| 完整ImageCoreAndMovieTests | 11 passed、0 failed、0 skipped；含HDR专项、实时颜色/ICC、APNG及原有测试 | evidence/hdr_info_output_20261017/image-core.log |
| 生产及测试构建 | Fovelle、fovelle_tests成功 | evidence/hdr_info_output_20261017/build-after.log |
| 空白错误检查 | git diff --check退出0 | 执行命令结果 |

专项与完整套件包含重复用例，未把两次执行加总为额外覆盖。

## 原子验收与逆向证伪

- A1：两个不同类型HDR图均先记录实际native输出，再打开真实信息页；label保持正常输出名。
- A2：每组执行两轮700ms等待，超过450ms失活动画和多轮100ms刷新；关闭重开不被预览替换。
- A3：静态门禁检查首帧呈现、当前HDR身份、原生renderer与非空实际输出名条件；动态D3验证无图、非HDR、原生SDR采用实时查询。未呈现首帧分支由静态门禁覆盖，不冒称已穷举全部GPU失败条件。
- A4：信息页保持打开时换Qt栅格，输出跟随新图且源ICC清空；closeImage后显示No output与Unknown source，证伪固定快照方案。
- A5：HDR信息页稳定时实时colorInformation仍报告preview，证明没有通过关闭退化行为使测试通过；完整实时颜色测试、机型与时间格式测试通过。测试中的setHDRPresentationActive(false)调用同一生产失活入口，避免CI桌面焦点不确定性。

## 复现命令

```sh
cmake --build build --target fovelle_tests Fovelle -j 6
ctest --test-dir build -R '^Fovelle(HDRInfoOutput|CameraModel)' -V
FOVELLE_TEST_SUITE=ImageCoreAndMovieTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests -v1
git diff --check
```

## 范围与限制

本次验证的是输出色彩空间信息的语义和真实原生呈现生命周期，不是物理亮度、EDR headroom或屏幕HDR峰值测量。没有原生HDR成功呈现证据时继续报告现有实时/预览信息。没有执行无关完整UI/HDR系统测试、发布或远程推送。没有未解决的本次自动化测试失败。
