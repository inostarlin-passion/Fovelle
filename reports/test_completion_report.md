# Actions 检查修复测试报告

## 结论和修复范围

基线4a8dac8的[Checks失败](https://github.com/inostarlin-passion/Fovelle/actions/runs/39140631482)包含RAW功能用例准备等待5秒超时（Qt诊断5200ms即可成功）和动画播放墙钟比例断言失败。[同提交Build Fovelle成功](https://github.com/inostarlin-passion/Fovelle/actions/runs/39140631516)，上一轮矢量绘制门禁已通过。

本次修复实际执行的CI验收代码：目标HDR用例采用现有15秒原生解码准备预算，新增5500ms真实队列阻塞数据行；动画改为独立源延时、逐帧像素/顺序、逻辑倍速、真实定时器、最早播放时间下界和有界完成检查。删除两次独立运行之间的墙钟比例上界，添加1秒GUI阻塞反证。保留HDR全部功能断言与原有CTest超时，未新增skip或删去功能断言。生产算法经源码和反向突变核验，无已证实缺陷，不作无依据修改。

## 原子标准执行结果

|标准|证据与结果|
|---|---|
|AC1/S1、AC4/S2静态|新增2项静态检查修复前失败，修复后2/2通过；既有movie_resume_static 3/3及HDR元数据静态1/1也通过。|
|AC2/D1正常HDR|gain-map、RAW两行完整功能断言通过。|
|AC3/D2延迟RAW|旧5秒预算在raw-delayed行稳定报错，Qt诊断5600ms可成功；新15秒预算下受控延迟行通过，且验证等待>=5500ms。|
|AC5/D3逐帧倍速|50%、100%、200%四轮以上循环共20帧，帧序、源像素及精确逻辑延时全部通过；实际19次定时器事件合法、无error，stop后无多发。|
|AC6/D4 GUI迟到|1000ms受控GUI阻塞行通过：200%播放1300ms，正常播放821ms；虽墙钟比例会误判，逐帧20ms逻辑延时、像素和19个timer事件全部正确。|
|AC7/D5反向突变|执行tests/actions_speed_mutation.py，临时去除生产speedAdjustedDelay的缩放后，200%行立即失败：实际40ms、期望20ms；脚本finally恢复原文件并重建，git diff确认生产源码无差异。|
|AC8/D6集成|专项CTest 3/3通过（34.90秒），最终动画数据行6/6（含初始化/清理）通过；全量CTest 66/66通过，0失败，用时589.28秒；FovelleTests 246.18秒、Remediation 20.73秒、HDR元数据14.85秒均通过。|

## 可重复命令和证据

- 构建：`cmake --build build --parallel 2`。
- 静态：`python3 tests/actions_followup_static.py`。
- 反向突变（须独占构建与测试）：`python3 tests/actions_speed_mutation.py --build-dir build`。脚本固化输入、故障检出条件和finally恢复。
- 专项：`QT_QPA_PLATFORM=cocoa ctest --test-dir build -R 'Fovelle(ActionsFollowupStatic|HDRSurfaceMetadataDynamic|RemediationDynamic)' --output-on-failure -V`。
- 全量：`QT_QPA_PLATFORM=cocoa ctest --test-dir build --output-on-failure --timeout 90`。

本地macOS27.0.1 arm64、Qt6.11.2、Apple Clang17；远端macOS26.6.2、Qt6.11.2、Apple Clang16。完整日志与前一轮报告备份位于本工作区reports/evidence/actions_followup_20261018/（沿用忽略规则）。精简可提交证据保存于[evidence/actions-fix/followup-20261018.json](evidence/actions-fix/followup-20261018.json)。

限制：原动画失败日志未输出两次实际墙钟值，不能声称已证明其具体调度来源。新测试验证功能契约及合法迟到，不能承诺任意系统负载下的显示FPS。15秒仍是有界准备预算，加载死锁或像素/标签错误仍会失败。尚未推送，远端新提交状态须由GitHub实际执行确认。

## 最终全量结论

执行跨2026-10-18至2026-10-19（Asia/Shanghai）。本次最终代码一次全量运行即66/66通过，无全量失败后重跑取绿。全量包括既有矢量CPU性能、颜色、动画生命周期、原生全屏、Portable HDR必需执行与发布产物验证。源码差异检查、修改行clang-format检查和新增Python编译检查通过。原始全量QtTest输出另存ctest-qt-output.log，其哈希与逐帧/RAW诊断已写入精简证据。

设计与测试规格均已按本次最终实现更新；所有AC均有对应自动化实现及执行结果。新增测试脚本与测试注册需要和修复一起提交，当前没有推送或远端新提交运行结果。
