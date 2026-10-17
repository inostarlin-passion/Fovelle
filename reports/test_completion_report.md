# 动图暂停恢复测试完成报告

日期：2026-10-17（Asia/Shanghai）。基础提交：623e39a；验证对象为本工作区未提交修改。

## 完成结论

A1–A6 全部满足本次自动化验收。暂停保留 decoder 和唯一在途任务；后台结果可暂存，恢复继续下一帧；停止、换源、跳帧及销毁隔离旧结果。生产应用及测试程序编译成功。

环境：macOS 27.0.1 / arm64，Qt 6.11.2，Apple LLVM 17.0.0，CMake Release，QT_QPA_PLATFORM=cocoa；测试设置使用独立临时目录。

## 执行结果与证据

| 检查 | 结果 | 证据（本目录相对路径） |
|---|---|---|
| S1静态门禁 | 1项通过 | evidence/movie_resume_20261017/restored-ctest.log |
| D1 GIF/APNG、同步/异步、无缓存/1字节预算 | 8组通过；每组两轮暂停恢复 | 同上 |
| D2 排队/暂存后的恢复与破坏性状态变更 | 10组通过 | 同上 |
| 专项QtTest总计 | 20 passed，0 failed，0 skipped（含init/cleanup） | 同上 |
| 完整RemediationTests | 43 passed，0 failed，0 skipped（包含专项，不重复计数） | evidence/movie_resume_20261017/final-remediation.log |
| 完整ImageCoreAndMovieTests | 8 passed，0 failed，0 skipped | evidence/movie_resume_20261017/final-image-core.log |
| 生产及测试构建 | Fovelle、fovelle_tests成功 | evidence/movie_resume_20261017/build-final.log |
| 恢复证伪实验后的测试构建 | 成功 | evidence/movie_resume_20261017/restored-build.log |
| diff空白检查 | git diff --check通过 | 命令退出0 |

## 红→绿与逆向证伪

第一轮旧实现测试见 before.log：静态门禁失败；APNG和排队任务的创建次数明确从1变2。该轮同时暴露测试自身的GIF参考读帧问题（CacheNone不支持这里假设的任意跳帧），已将参考reader及跳帧测试设为CacheAll；这部分失败不作为产品故障证据。

为排除测试修正造成假绿，追加隔离变异实验：只把暂停时 cancelNextLoad 放回正确实现，保持最终测试及其他生产代码不变。mutation-red.log 显示同步4组通过、异步GIF/APNG四组全部因decoderCreations=2而失败，退出4。随即恢复源码、重新构建、重新运行专项门禁，全部通过。该实验直接证明测试能检出本次恢复重建decoder的问题。

## 原子标准验收

- A1：异步D1四组及D2完成前/完成后恢复均保持decoderCreations=1，且GIF测试预算不足以缓存一帧；修复不依赖扩大缓存。
- A2：暂停120ms画面/信号不变；暂存分支通过readCompletions确定已交付；没有继续发布帧。
- A3：序号为精确后继，像素等于独立同步解码；快速恢复复用唯一任务。
- A4：重复pause/resume不新增任务；恢复后的timer保持正常帧时长附近，下一帧在测试1000ms门槛内到达。
- A5：排队和结果暂存两种阶段的stop/换源/跳帧/销毁均通过；停止后等待120ms没有帧泄漏。
- A6：同步GIF、APNG、已有速度比例、bounded cache、错误输入、旧源隔离回归全部通过。

## 复现命令

```sh
cmake --build build --target fovelle_tests Fovelle -j 6
ctest --test-dir build -R '^FovelleMovieResume' -V
FOVELLE_TEST_SUITE=RemediationTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests -v1
FOVELLE_TEST_SUITE=ImageCoreAndMovieTests QT_QPA_PLATFORM=cocoa FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1 build/tests/fovelle_tests -v1
git diff --check
```

## 范围与限制

没有未解决的本次测试失败。未获得用户原始大GIF，因此没有宣称特定素材的物理显示延迟或绝对性能倍数；测试证明恢复不再重建decoder，并验证真实GIF/APNG帧序和像素。磁盘读取、线程池占用及单帧解码仍影响实际时间。未执行与此变更无关的全部UI/HDR系统测试，也未进行发布、安装或远程推送。上一任务三份同名报告备份见 evidence/movie_resume_20261017/previous_*。
