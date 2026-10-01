# 全屏同步快照卡顿机制：测试完成报告

日期：2026-10-01。本轮以 [根因报告](root_cause.md) 的 R1 为入口，修正提交前像素工作未被现有 Paint／运动测试观察到的覆盖缺口，并修复同源同方向重复生成快照。既有终点交接修复继续保留。

## 完成结论

**同一最终快照用例在本轮生产起点上连续三轮失败，修复后连续三轮通过。** 原代码四个分支每次都重建整张 RGBA 输出；修复后不变请求均共享缓冲，原生进入／退出保持复用，方向／同名文件重载后的新画面正确，调用方写入不污染缓存。测试和应用构建成功，六项全屏 CTest、系统门禁及三个 fit／pan 业务回归通过。

确认的是重复同步像素工作的成本及其消除；没有用户现场时间线，不能声称唯一自然根因已定位或所有负载下零卡顿。首次新源／方向生成仍同步，缓存会保留一张按实际图片尺寸增长的 RGBA 图像。

## 版本与实验可复核性

Git 基线 `98f1e06dd0169a5bf5d7efc5dbdd4376826babe0` 还包含工作区此前的交接改动，因此不是纯 HEAD 红测试。本轮红生产代码是修改快照前的工作区；原始 [cpp](evidence/fullscreen_snapshot/qvgraphicsview.cpp)／[header](evidence/fullscreen_snapshot/qvgraphicsview.h) 已保存。红测试运行完毕后才修改这两份生产文件；最终 C++ 测试和 CTest 注册保持相同，再构建绿轮。

[summary](evidence/fullscreen_snapshot/summary.json) 保存红／绿全部指标、进程退出码、环境、九项当前源码 SHA-256、原始快照源文件指纹、红／绿测试二进制及应用二进制指纹。[新增生产 patch](evidence/fullscreen_snapshot/snapshot-production.patch) 区分本轮修复；[完整源码差异](evidence/fullscreen_snapshot/changes.patch) 包含仍未提交的之前修复。环境记录：macOS27.0（26A428）、arm64、Qt6.11.2、Release。

## 最终红绿结果

四行 rotate90／mirror／flip／identity，每行4096×3072像素、约48MiB RGBA；每行8次不变请求和原生进入／退出各一次。三轮每侧12行、96次不变请求及24个原生方向。

| 指标 | 修复前＋最终用例 | 修复后＋同一最终用例 |
| --- | --- | --- |
| 每轮四行结果 | 三轮均四行失败 | 三轮均四行通过 |
| QtTest总结 | 三轮均2 passed／4 failed／0 skipped | 三轮均6 passed／0 failed／0 skipped |
| 进程退出码 | 4／4／4 | 0／0／0 |
| 不变请求重新产生完整输出缓冲 | 每行8／8，合计96次 | 每行0／8，合计0次 |
| 90°旋转行8次请求累计耗时 | 514.342／512.105／512.625ms | 0.001042／0.000792／0.001417ms |
| 所有行8次请求累计区间 | 8.764–514.342ms | 0.000416–0.001417ms |
| 布局改变后的快照复用 | 原生进入、退出后均仍重建 | 所有原生方向均复用 |
| 方向／同路径重载刷新、写入隔离 | 像素断言通过，但后续仍重建 | 像素及复用断言均通过 |

passed包含init／cleanup。时间来自公共图像 provider＋RGBA归一化，不含磁盘加载、标题栏捕获、CGImage创建、完整轨迹提交或物理显示。测试对返回临时 QImage 做转换，Qt可使用rvalue就地路径；原生 provider 从const source转换，基线可能额外复制，因此这些数字不是原生代理的总耗时。输出缓冲身份的资源断言不受这一优化差异影响。绿色微秒量级数值只作本机诊断，门禁不以它设速度阈值。

原始日志：[红1](evidence/fullscreen_snapshot/red-1.txt)、[红2](evidence/fullscreen_snapshot/red-2.txt)、[红3](evidence/fullscreen_snapshot/red-3.txt)、[绿1](evidence/fullscreen_snapshot/green-1.txt)、[绿2](evidence/fullscreen_snapshot/green-2.txt)、[绿3](evidence/fullscreen_snapshot/green-3.txt)。

## 构建、门禁与回归

- [构建](evidence/fullscreen_snapshot/build-green.txt)：fovelle_tests与Fovelle成功。
- [CTest](evidence/fullscreen_snapshot/ctest.txt)：6／6通过，67.22s；包括新增快照、运动、直接Paint预算、完整准备／交接、标题栏和真实SDR AVIF画面。
- [系统门禁](evidence/fullscreen_snapshot/system.json)：passed=true，两套进程退出码均0，约50.99s；32个运动记录、12个直接Paint样本、8个准备／交接过程、4个快照行完整通过。
- [GraphicsView回归](evidence/fullscreen_snapshot/graphics-regression.txt)：fit resize、退出垂直pan、overflow标题栏padding三项通过；5 passed／0 failed／0 skipped，包括init／cleanup。
- [门禁负向验证](evidence/fullscreen_snapshot/gate-validation.json)：空／缺行／重复、重建、原生方向未齐、失败列表、NaN时间、零像素、错误字节、缺字段均拒绝；三份红拒绝、三份绿接受。属于遥测变异检查。
- Python语法检查及 `git diff --check`通过。

快照 fixture 自动生成，不依赖外部原图。AVIF回归沿用本机既有 `/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif`，异机需配置相应fixture。执行入口及原子断言见 [用例说明](test_case_specification.md)。

## 检索、证伪与未覆盖范围

沿 Apple提交／渲染阶段→Qt图像变换／格式转换→固定版本共享源码→pixmap内容身份→本地provider路径→实际缓冲与红绿对照进行多跳核验；来源及链式推导见 [技术设计](technical_design_document.md)。没有把论坛猜测或源码可达性直接升级为用户自然卡顿的确定归因。

初期测试误将原始RGB当作加载后显示色域值，随后尝试反向sRGB精确比较也不可靠；这些 `color-*-exploratory` 日志只记录测试准备问题，不算有效红轮。最终以已加载的未旋转像素作参照，四角和色彩空间断言通过后，正式红轮全部失败于重复资源工作；未通过放宽复用断言获得绿结果。

不透明／非不透明终点绘制、运动采样等已有修复继续由回归验证。动画帧与空源清理仅源码审查，本轮没有专门动态注入；所有HDR／动画图片、多屏刷新率组合、极大图峰值内存及冷缓存首次生成未做完整性能矩阵。现有实验足以支持本次重复快照机制与修复，仍不足以保证任意设备／负载下所有自然卡顿消失。之前三份终点交接报告副本保存在 [prior_reports](evidence/fullscreen_snapshot/prior_reports/)。
