# 原子验收与测试用例

统一环境：macOS、Qt6.11.2、BUILD_TESTS=ON、portable-hdr/bayer.dng及remediation.gif可读。所有用例均固化为测试代码。

|标准 / 用例|测试目的|前置条件|输入数据|操作步骤|预期结果|后置条件|
|---|---|---|---|---|---|---|
|AC1 / S1 静态|固定准备期限和可诊断性|源码可读|HDR用例源码|运行 actions_followup_static.py|15秒仅用于目标用例准备、5500ms延迟行、等待耗时日志、全部像素/标签断言存在|源码不变|
|AC2 / D1 正常HDR|确保功能不变|真实窗口/原生renderer可用|gain-map.jpg、bayer.dng，无延迟|testHDRPersistentSurfaceMetadata原两行|15秒内加载且原有像素、对象身份、headroom及恢复断言通过|窗口关闭、环境恢复|
|AC3 / D2 迟到RAW|复现并修复5秒准备误报|四线程队列可控|bayer.dng，5500ms队列阻塞|raw-delayed行占用线程池→openFile→GUI释放→执行完整HDR验收|旧5秒期限失败；新15秒内成功，实测准备>=5500ms，全部HDR断言通过|信号量作用域释放，无遗留阻塞|
|AC4 / S2 静态|防止返回墙钟比例误判|源码可读|动画用例与注册|运行 actions_followup_static.py|独立源帧延时、真实timer、逐帧像素、单侧最早时间边界、受控延迟和CTest注册存在；无fast/normal比例上界|源码不变|
|AC5 / D3 动画倍率|验证真实速度、帧序和停止|GIF独立解码成功|50%、100%、200%，20帧|testMovieAsyncPlaybackSpeed数据行，观察frameChanged及timer事件|每帧像素/顺序正确；逻辑延时精确缩放；timer区间合法；累计时间不早于理论下界；10秒内完成，stop后不多发|movie停止、无悬挂任务|
|AC6 / D4 动画延迟反证|合法调度晚到不误报|同上|200%，第5帧阻塞GUI1000ms|gui-delayed行执行同样全部断言|受控阻塞确实发生，逐帧和timer断言仍通过|恢复事件循环|
|AC7 / D5 故障检出反证|证明没有掩盖真正倍率错误|临时突变可恢复|将speedAdjustedDelay临时改为原始delay|重建并执行200%数据行，再恢复生产文件并重建|逻辑delay断言明确失败，恢复后通过|生产源码与基线完全一致|
|AC8 / D6 集成|保证其他功能及上一轮修复|完整构建|全部CTest|先专项再全量ctest|所有测试通过；完整记录失败而非只记录重跑结果|正常退出|

静态测试在CMake注册；动画数据由QtTest自动注册于RemediationTests，HDR数据由现有独立门禁及主套件执行。D5为可重复的反向突变执行脚本，脚本必须finally恢复生产源码。
