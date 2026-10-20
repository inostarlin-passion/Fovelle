# 三分钟默认测试完成报告

## 结果与边界

默认本地/CI入口已改为同一份去重清单，原完整矩阵保留为显式深度回归。测试执行的预算为180秒，编译、依赖安装、lint及打包签名另计。不是把原所有矩阵强行压缩至180秒：默认覆盖选择及移出的长场景见docs/TESTING.md与设计文档。

|入口|结果|墙钟耗时|
|---|---|---:|
|原默认完整CTest（上轮基线）|66/66通过|589.28秒|
|新本地默认CTest|全部计划阶段通过|96.56秒|
|新CI配置入口（本机模拟同一工作流命令）|全部计划阶段通过|96.65秒|

本地精简清单包含25个静态脚本、111个Qt函数/数据行选择器、旧Qt BT.709可执行程序和3个真实HDR阶段。合并后仅一个CTest入口；其内部逐阶段输出日志、XML与状态，不以“1项CTest通过”代替实际覆盖记录。Qt XML实际验证183个用例数据行（不含init/cleanup和旧Qt独立程序），旧Qt独立程序另有8个数据行。真实HDR保留两个fixtures和完整十行全屏连续性，全部pass、零skip。

## 原子验收

|标准|执行结果|
|---|---|
|AC1清单及静态|六项执行器/契约单元测试通过；清单拒绝重复和关键覆盖缺失；原静态契约在默认入口继续执行。|
|AC2失败传播|退出码7被判失败，未变绿。|
|AC3总预算|过期deadline不启动进程；短deadline长sleep触发killpg并wait；执行器165秒共享总预算，CTest175秒、CI步骤3分钟。|
|AC4真实证据|缺函数、skip、错误数据行、意外重复节点均失败；显式多数据行仅按精确节点数和行集合通过；陈旧XML清理，未执行阶段为not-run。|
|AC5本地实测|默认入口成功完成，96.56秒<180秒。|
|AC6CI配置实测|同一清单、同一预算、全部39个阶段通过，96.65秒<180秒；远端实际耗时待推送后Actions验证。|
|AC7完整矩阵保留|真实配置FOVELLE_EXTENDED_TESTS=ON后，66个原目标名称与基线集合完全相等；随后恢复OFF，默认仅FovelleFastTests。|

首次试跑36.41秒时，用例均成功但XML校验因合法的显式多数据行节点而失败；该次失败完整保留，没有删除数据行。修复校验器为“精确次数+精确行集合”，增加正反回归后本地重跑成功。此适配保留旧入口拒绝意外重复的行为。

## 执行与证据

本地：`ctest --test-dir build --output-on-failure`。
CI配置：`FOVELLE_TEST_PROFILE=ci QT_QPA_PLATFORM=cocoa ctest --test-dir build --output-on-failure --timeout 90`。
深度矩阵：先`cmake -S . -B build -DFOVELLE_EXTENDED_TESTS=ON`，再运行CTest；不受默认三分钟目标约束，用后恢复OFF。

完整执行日志、配置清单、XML和上轮报告备份位于reports/evidence/fast_tests_20261019/（沿用忽略规则）；可提交的摘要见[evidence/actions-fix/fast-tests-20261019.json](evidence/actions-fix/fast-tests-20261019.json)。四个工作流都显式配置精简入口，并上传fast及portable-hdr证据目录。

当前未推送，不能将本机CI配置实测等同于远端runner实测。预算耗尽会明确失败；不承诺任意机器、任意负载下都成功，但不会超时后把未执行覆盖报告为通过。

最终检查：git diff --check、三个Python脚本编译和六项执行器单元测试均通过。默认build配置保持FOVELLE_EXTENDED_TESTS=OFF；完整矩阵注册集合核验与基线完全一致。两次成功实测均有183个Qt XML用例数据行（另有旧Qt后端8行），真实HDR十行全屏校验全部通过；执行集合在两个profile间一致。相比基线默认测试约减少83.6%的墙钟耗时。
