# 三分钟默认测试入口设计

## 问题与边界

现有本地66个CTest入口约589.28秒，主入口246.18秒，且后续专项重复运行相同Qt函数。用户目标：默认本地测试和CI测试各在180秒内完成。此处计量从测试启动到退出，依赖安装、编译、签名发布另计；明确保留超过180秒的显式深度回归入口，不宣称精简后仍执行每个原数据行。

原子拆解：消除重复入口；缩减状态笛卡尔积；保留关键风险的真实动态验证；强制总预算；保持失败/跳过可见；本地/CI使用同一清单；保留完整矩阵；实测两个入口。

## 多跳检索与交叉核验

- 先核对上轮原始CTest日志、QtTest输出及当前66项注册，避免将CTestCostData移动平均误当单次实测。
- [Qt命令行选择函数/数据行](https://doc.qt.io/qt-6/qtest-overview.html)：支持function和function:row，可保留原断言而精简重复矩阵。
- [CTest TIMEOUT](https://cmake.org/cmake/help/latest/prop_test/TIMEOUT.html)：单测试属性覆盖--timeout默认值；因此只改命令的90秒不能限制总时长。
- [Python子进程](https://docs.python.org/3/library/subprocess.html)：start_new_session与超时配合进程组清理；复用仓库真实HDR执行器，避免只杀包装器、留下Qt进程。
- [GitHub工作流语法](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)：步骤timeout-minutes独立于job，可给测试步骤3分钟硬界限。
- 交叉核验代码：run_portable_hdr.py已严格检查实际XML、所需数据行、零失败/零skip、fixture校验和；精简入口复用此校验，HDR的10个真实全屏行全部保留。

## 实现与覆盖选择

默认仅注册FovelleFastTests，执行清单tests/fast_test_manifest.json。使用唯一Qt suite分组，函数和数据行不重复；各组在独立进程顺序执行，避免GUI并发抢焦点。全部原注册和用例保留在FOVELLE_EXTENDED_TESTS=ON分支。历史目标名仍在原文件，静态契约仍可核验。

保留：全部ImageLoader格式/颜色/矢量性能用例，HDR数学与像素、真实便携HDR及其完整10行全屏、代表性SDR全屏/原生提示框、缩放拖动、动画pause/queued read/倍速、网络失败与清理、设置持久化、元数据、旧Qt BT.709后端及现有静态契约。完整矩阵中的多窗口/多状态重复、外部本地fixture、故障注入长等待、全部Info关闭途径和独立打包部署重复验证改由深度入口执行。CI构建/发布仍保留真实bundle验证。

清单是覆盖边界，不靠提前退出或skip冒充通过。Qt执行XML必须包含所选函数/行且全部pass；同一函数的多个显式数据行可产生多个XML节点，节点数必须精确匹配选择器数量，行集合也必须精确匹配；清单缺失、重复、未知函数、缺失数据行、fixture损坏、非零退出和超时均失败。阶段日志及总JSON保留全部计划项，未运行项标明not-run，清理旧XML防止读到上次成功。

预算三层：执行器共享单调165秒总deadline（不是每项165秒）；默认CTest属性175秒；四个CI工作流的测试步骤3分钟。失败即非零，超时不会被报告为成功。local/ci仅标识证据，执行集合、断言和预算完全相同。

## 正确性与反证

去重正确性：每个suite仅一个普通执行组，所选函数/行集合无交叠；真实HDR特殊组与普通组的函数也不交叠。改变执行次数不改原用例断言。完整功能等价不在本次承诺内，完整矩阵可显式恢复。

预算正确性：阶段i的可用时间为D-now，各阶段共享D；任一阶段不能重新获得完整预算。Qt子进程由原执行器以独立session启动并在超时killpg；普通命令也同样回收。预算耗尽或任何失败立即停止并写失败摘要。175/180秒外层为异常清理和CTest留余量。

反证测试：重复选择、缺少核心覆盖、XML缺失/skip/错行、命令非零、子进程超时、耗尽共享预算、旧产物清理均应失败。真实本地与CI配置入口分别执行并记录wall time，不能以设置超时替代成功实测；远端实际时长需推送后Actions确认。
