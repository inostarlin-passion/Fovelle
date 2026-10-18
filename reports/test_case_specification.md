# 三分钟测试验收规格

|标准/用例|测试目的|前置条件|输入数据|操作步骤|预期结果|后置条件|
|---|---|---|---|---|---|---|
|AC1/S1 清单契约|防止空集、重复、遗漏关键风险|源码可读|清单及CMake/四个工作流|test_fast_tests.py静态与清单测试|关键组存在、无函数/行重复、默认精简/完整可选、CI步骤3分钟|源码不变|
|AC2/D1 失败传播|避免错误变绿|Python可运行|返回7的子进程|调用普通阶段执行器|非零失败并生成失败记录|子进程回收|
|AC3/D2 总预算|防止逐项预算重置|同上|已过期deadline、短deadline+长sleep|执行阶段|耗尽预算不启动；超时killpg并wait，非零|无遗留执行器进程|
|AC4/D3 证据真实性|防止少跑/skip/旧结果通过|XML校验器可导入|正确、缺函数、错数据行、skip XML；旧文件|执行校验及输出准备|仅精确pass通过，其余失败；旧证据清理、未执行标记not-run|仅临时目录变化|
|AC5/D4 本地实测|验证成功而非超时终止|构建完成、便携HDRfixtures|默认CTest，profile local|ctest --test-dir build --output-on-failure|完整精简清单pass，耗时<180秒，HDR零skip|JSON/XML完整，GUI关闭|
|AC6/D5 CI配置实测|验证集合与预算一致|同上|FOVELLE_TEST_PROFILE=ci，同一CTest命令|执行与CI一致入口|同一清单pass，<180秒，JSON profile=ci|证据另存|
|AC7/S2 深度入口|防止丢失原回归|可配置CMake|FOVELLE_EXTENDED_TESTS=ON|配置并ctest -N，不执行长矩阵|原66项（无本地fixture环境时63项）仍注册；新默认不并行GUI|默认构建保持精简配置|

以上S/D均固化在test_fast_tests.py或真实入口中；精简入口自身包含其单元测试。测试阶段不包含编译和依赖安装；显式深度矩阵不受默认三分钟目标约束，覆盖边界详见设计和docs/TESTING.md。
