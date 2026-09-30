# 隐藏标题栏进入全屏：测试完成报告

日期：2026-09-30（Asia/Shanghai）。结论：新增过程级回归在旧生产代码上连续三轮稳定检出问题；修复生产代码后连续三轮通过。修正邻接测试的原生时序等待后，再次执行新回归通过，12 项邻接用例全部通过。

## 环境与基线

- macOS 27.0（26A428）、arm64、Qt/QtTest 6.11.2、Apple LLVM 17.0.0、Release、Cocoa。
- 原生产代码基线：`193dc10f263ecae73fc63886a30b218459fbe7a8`。
- 红阶段保留新增测试，仅将 `src/mainwindow.cpp/.h` 临时替换为该提交的原始生产代码并重新构建。三次执行完成后恢复修复内容并重新构建；最终工作区为修复版本。
- 测试输入为临时生成的 PNG/SVG 竖图，没有使用外部磁盘样本。

## 执行结果

| 验证 | 运行次数 | 结果 | 原始证据 |
| --- | --- | --- | --- |
| 最终版新增测试 + 旧生产代码 | 3 | 每轮 2 个隐藏数据行失败、2 个可见对照通过；无跳过，进程返回 2 | [第 1 轮](evidence/titlebar_fullscreen/red-final-1.txt)、[第 2 轮](evidence/titlebar_fullscreen/red-final-2.txt)、[第 3 轮](evidence/titlebar_fullscreen/red-final-3.txt) |
| 新增测试 + 修复生产代码 | 连续 3 | 每轮 4 数据行通过；每行完成 2 次原生进入退出；共 24 次进入、24 次退出 | [CTest 详细重复日志](evidence/titlebar_fullscreen/green-final-repeat.txt) |
| 隐藏偏好、全屏快捷键/菜单/Escape | 1 | 6 项通过，无失败或跳过 | [WindowBehaviorTests](evidence/titlebar_fullscreen/WindowBehaviorTests.txt) |
| 标题栏应用图标/文档图标/幂等清理 | 1 | 3 项通过，无失败或跳过 | [FeatureTests](evidence/titlebar_fullscreen/FeatureTests.txt) |
| fit zoom、退出 pan、全屏 scene padding | 修正后连续 3 | 每轮 3 项通过，无失败或跳过 | [第 1 轮](evidence/titlebar_fullscreen/graphics-final-1.txt)、[第 2 轮](evidence/titlebar_fullscreen/graphics-final-2.txt)、[第 3 轮](evidence/titlebar_fullscreen/graphics-final-3.txt) |
| 邻接测试修正后的新回归复验 | 1 | 4 数据行通过，CTest 返回 0 | [最终日志](evidence/titlebar_fullscreen/green-final-after-adjacent-repair.txt) |
| 构建生产应用与测试目标 | 恢复修复后 | `fovelle_tests` 与 `Fovelle` 构建成功；随后测试源时序修正重新构建测试目标成功 | [恢复修复后的构建日志](evidence/titlebar_fullscreen/fixed-build.txt) |
| 补丁格式 | 最终检查 | `git diff --check` 通过 | 工作区检查 |

表格中的用例数量不包含 QtTest 的 initTestCase/cleanupTestCase。红阶段会在首个失败断言结束对应数据行，未完成第二周期，不能算作全周期成功验证。

## 关键观测

| 指标（隐藏数据行） | 旧生产代码：三轮一致 | 修复后：连续三轮及最终复验 |
| --- | --- | --- |
| 原生标题栏曾暴露 | true | false |
| 有效顶部最大占用 | 32 个逻辑像素 | 0 |
| 进入前 zoom | 0.32 | 0.32 |
| 进入期间最小 zoom | 0.30 | 0.32 |
| PNG/SVG 结果 | 两者同样失败 | 两者均通过 |
| 退出后标题栏与偏好 | 失败用例未执行最终断言 | 保持原始设置 |

32 是本机观察值，不是测试硬编码阈值。正式断言要求隐藏状态占用为 0，最小 zoom 不低于进入前真实值（浮点容差 0.0001）。从 0.32 到 0.30 的比例下降为 6.25%，足以证实进入前发生图像挤压。原生状态检查与生产 Qt getter 独立，且等待本窗口的 AppKit did-enter/did-exit 通知。

## 首次扩展回归的失败与处理

首次将三条既有 GraphicsView 全屏用例组合执行时，前两条通过，`testFullscreenAfterOverflowRemovesTitlebarScenePadding` 的第二窗口退出超时，随后进程 SIGSEGV（返回 -11）。[首次原始日志](evidence/titlebar_fullscreen/GraphicsViewTests.txt) 保留了断言失败，不将该轮计为通过。

检查发现该测试只等待 Qt 的全屏状态，未等待 AppKit 动画完成，随后便缩放、退出及创建下一窗口。根据已查证的原生完成边界，给两个窗口分别增加 did-enter/did-exit 等待与 scope guard 清理，保留原来的 sceneRect 和图像顶部断言，不增加超时或放宽判据。修正后三轮组合执行均成功，约 5.6–5.8 秒/轮。该结果支持时序修正；首次崩溃未另做系统栈符号化，不宣称已逐帧重建其全部系统内部过程。

## 修复范围

生产进入路径不再先恢复隐藏标题栏，因此也移除了退出时补偿隐藏的分支和 `storedTitlebarHidden` 字段；代理布局重叠量依据当前原生有效遮挡。新增测试及探针仅进入测试目标；既有场景边距测试补充原生完成等待。没有通过吞掉警告、跳过用例或降低图像断言要求取得通过。

## 复现与证据保全

在仓库根目录：

```bash
cmake --build build --target fovelle_tests Fovelle -j 4
ctest --test-dir build -R '^FovelleHiddenTitlebarFullScreen$' --repeat until-fail:3 -V
```

原始日志保存在 `reports/evidence/titlebar_fullscreen/`；[机器可读汇总](evidence/titlebar_fullscreen/summary.json) 记录基线提交、结果、逐次进入指标及最终生产/测试源 SHA-256。完整数据矩阵与执行判据见 [测试用例说明](test_case_specification.md)，多跳检索、多源交叉验证和因果推导见 [技术设计文档](technical_design_document.md)。

## 验证边界

执行了本问题回归与相关的 12 项邻接测试，没有执行仓库全部 HDR、外部样本及发布流水线。macOS 15、其他系统/Qt 版本、多显示器及独立 HiDPI 组合未实测。视觉结论依据原生呈现属性、同步布局事件、有效遮挡与实际 zoom 的组合证据，不等同于显示器逐帧录像验收。
