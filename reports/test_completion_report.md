# 2026-11-05 四项审查问题：测试完成报告

结论：基于提交 `991d611` 的四项问题均已修复。最终23条独立原子断言全部通过，固化为10个Qt业务测试方法和10个静态测试方法；专项及默认门禁均执行，零跳过。

## 交付与行为

| 问题 | 修复后的行为 |
|---|---|
| 删除确认目标漂移 | 确认A后即使后台显示B，操作仍只针对A；身份变化拒绝删除，取消不删除，B保持显示 |
| EPS限制过晚 | 子进程继承256MiB RLIMIT_FSIZE；stderr增量限64KiB并超限终止；正常转换、失败、取消及真实30秒超时均验证 |
| 复制降分辨率 | 原生权威像素/完整表示用于复制；4000×3000完整保留；分块边界、方向和alpha正确；不再依赖源文件重新解码 |
| 下载文件累积 | 无窗口历史列表；解码、动画、原生对象、MIME、Open With和目录扫描携带共享租约，无消费者后自动清理 |

设计与正确性证明：`reports/technical_design_document.md`。原子规格及每例六要素：`reports/test_case_specification.md`。测试代码：`tests/review_fixes_acceptance.inc`、`tests/review_fixes_static.py`。三份旧报告保存在本地 `reports/history-20261102/`，不覆盖其历史证据。

## 修复前验证与迭代

1. 在生产修改前写入设计、测试规格、静态及动态用例。新增静态检查在基线上失败（red-static.log）；修复前真实Cocoa/文件系统/受控转换器探针记录复制自上一轮审查（baseline-*.log）。新动态代码使用新接口，未将它描述为已在原实现完整编译执行。
2. 首轮专项：8个业务方法通过、2个失败（iteration-1.log；日志“10 passed”包含init/cleanup，不是10个业务方法）。一个失败是PDF色彩空间测试预期，应先转sRGB再比较分量；另一个发现CGImage导出用source-over混合写入未初始化内存，导致半透明alpha错误。
3. 修正CGImage像素拷贝模式，复制tile关闭插值；保留自然尺寸、分块边界、RGB及alpha断言。进一步独立验证MIME、Open With、SDR/HDR对象的租约，避免同时持有多个消费者掩盖遗漏；为renderer夹具补齐打开失败检查及PID回收断言。
4. 专项最终10/10业务方法通过（focused.xml/log；连同init/cleanup共12 pass），约35.6秒；包含生产30秒超时，没有缩短生产阈值。
5. 默认CTest 5/5通过，内部44/44阶段通过；8个变化编译单元的clang-tidy通过；固定版本clang-format及git diff --check通过。扩展配置专项两个入口可发现，随后恢复默认配置。

## 逐条原子验收

每行都在专项XML和默认WindowBehaviorTests.xml中核验所属动态方法存在、Incident=pass、无Skip；静态组也在默认门禁中执行。组合方法中的各原子输入和独立断言位置详见规格台账。

| 原子标准 | 静态用例 | 动态方法 | 结果 |
|---|---|---|---|
| A01 | test_static_d1 | testDeleteConfirmationKeepsTarget | 通过 |
| A02 | test_static_d1 | testDeleteConfirmationKeepsTarget | 通过 |
| A03 | test_static_d2 | testDeleteConfirmationRejectsReplacement | 通过 |
| A04 | test_static_d2 | testDeleteConfirmationRejectsReplacement | 通过 |
| A05 | test_static_e1 | testEPSOutputLimit | 通过 |
| A06 | test_static_e2 | testEPSDiagnosticLimit | 通过 |
| A07 | test_static_e3 | testEPSResourceCompatibility | 通过 |
| A08 | test_static_e3 | testEPSResourceCompatibility | 通过 |
| A09 | test_static_e3 | testEPSResourceCompatibility | 通过 |
| A10 | test_static_e3 | testEPSResourceCompatibility | 通过 |
| A11 | test_static_c1 | testClipboardFullResolution | 通过 |
| A12 | test_static_c1 | testClipboardFullResolution | 通过 |
| A13 | test_static_c1 | testClipboardFullResolution | 通过 |
| A14 | test_static_c2 | testClipboardRepresentations | 通过 |
| A15 | test_static_c2 | testClipboardRepresentations | 通过 |
| A16 | test_static_c2 | testClipboardRepresentations | 通过 |
| A17 | test_static_l1 | testDownloadedSourcesRetire | 通过 |
| A18 | test_static_l2 | testSourceLeaseAsyncLifetime | 通过 |
| A19 | test_static_l2 | testSourceLeaseAsyncLifetime | 通过 |
| A20 | test_static_l3 | testSourceLeaseConsumers | 通过 |
| A21 | test_static_l3 | testSourceLeaseConsumers | 通过 |
| A22 | test_static_l3 | testSourceLeaseConsumers | 通过 |
| A23 | test_static_l3 | testSourceLeaseConsumers | 通过 |

机器可核验汇总：`reports/review_fixes_evidence/acceptance-results.json`。

## 最终执行与证据

环境：macOS 27.0.1 / arm64，AppleClang 21.0.0，Qt 6.12.0；Python venv含仓库固定依赖；clang-format/clang-tidy 23.1.3。构建独立于工作区，目录 `/tmp/fovelle-review-fixes-build`。

```sh
cmake -S . -B /tmp/fovelle-review-fixes-build \
  -DBUILD_TESTS=ON -DFOVELLE_EXTENDED_TESTS=OFF \
  -DFOVELLE_BUNDLE_GHOSTSCRIPT=OFF \
  -DFOVELLE_SPARKLE_ROOT=/Users/inostarlin/code/Fovelle/build-install-release/_deps/sparkle \
  -DPython3_EXECUTABLE=/Users/inostarlin/code/Fovelle/.tmp/ci-fix-venv/bin/python \
  -DQV_DISABLE_ONLINE_VERSION_CHECK=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/fovelle-review-fixes-build --parallel 4
python3 tests/review_fixes_static.py
ctest --test-dir /tmp/fovelle-review-fixes-build --output-on-failure
.tmp/ci-fix-venv/bin/python .github/scripts/check_format.py
.tmp/ci-fix-venv/bin/clang-format --dry-run --Werror tests/review_fixes_acceptance.inc
git diff --check
```

专项命令使用 `FOVELLE_TEST_SUITE=WindowBehaviorTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 QT_FATAL_WARNINGS=1`，运行规格中10个方法并写出focused.xml/log；默认门禁同样启用QT_FATAL_WARNINGS并隔离用户设置。

| 检查 | 结果 | 证据 |
|---|---|---|
| app及测试完整Release编译 | 通过 | configure.log、build.log |
| 新静态用例 | 10/10通过 | static.log |
| 新动态专项 | 10/10业务方法通过；0 skip | focused.xml、focused.log |
| 默认CTest | 5/5通过；87.42秒 | ctest.log |
| 默认阶段明细 | 44/44通过；71.128秒 | default-test-results/fast/summary.json |
| 默认HDR实际文件/色彩/全屏 | 3阶段通过，包含于44阶段 | default-test-results/portable-hdr/ |
| clang-tidy | 8/8变化单元通过；未关闭检查 | tidy-summary.log、tidy-*.log |
| 固定clang-format | 通过，另检查新增inc文件 | format.log、inc-format.log |
| diff空白检查 | 通过 | diff-check.log |
| 扩展专项注册 | 静态/动态2入口存在；已恢复默认5入口 | registration.log、extended-registration.json |

所有上述证据位于 `reports/review_fixes_evidence/`，构建产物未加入仓库。报告生成后再次核对manifest、XML及原子映射，没有用历史绿色结果替代本次执行。

## 验证边界

- 本机QtSvg依赖实际最低系统为27.0，链接器对应用15.0声明给出既有警告；本轮是本机功能验证，未声称证明macOS15发布兼容性。未弱化部署校验，也未改变发布目标。本地EPS使用已安装的真实Ghostscript；独立测试构建关闭第三方打包，生产默认打包保持原样。
- clang-tidy首轮因独立二进制未发现SDK libc++头而无法分析；读取实际编译器搜索路径后，附加同一SDK的`-isysroot`和`-isystem .../usr/include/c++/v1`重新运行，8/8通过。没有抑制源码诊断或更改检查集合。
- Qt5子进程modifier兼容分支保留，但本机只有Qt6，未宣称Qt5动态执行。完整历史扩展矩阵及在线CI/签名发布未执行；默认矩阵及新增专项已实际通过。
- 文件身份检查阻止确认期间的替换和应用内切图竞态，不承诺对其他进程最后一瞬间替换路径提供原子unlink保证。RLIMIT_FSIZE约束单个常规文件，诊断流有界；不是对子进程全部堆内存/全部临时文件总量的通用沙箱。仍被消费者使用的下载按租约保留，最后消费者结束后回收。
