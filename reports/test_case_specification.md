# HDR信息页输出测试规格

日期：2026-10-17。动态测试加入ImageCoreAndMovieTests，静态测试tests/hdr_info_output_static.py。每项用例均自动执行，失败不跳过本机HDR能力。

## S1（A1、A3、A5）：只读信息页路径静态检查
1. 测试目的：核验独立正常HDR查询、真实就绪条件、provider接线，防止硬编码或更改实时语义。
2. 前置条件：源码可读。
3. 输入数据：graphicsview header/cpp、MainWindow、CMake。
4. 操作步骤：执行hdr_info_output_static.py。
5. 预期结果：查询包含hasOutput、isNativeHDRLoaded、rendererActive、imageActive、firstFramePresented、非空输出；只读、使用renderer输出名；MainWindow调用新查询；原实时谓词保留；CTest已注册。
6. 后置条件：无文件或窗口变更。

## D1（A1、A2、A5）：真实HDR信息页退化场景
1. 测试目的：复现正常HDR图打开信息页后临时SDR代理覆盖信息的问题。
2. 前置条件：macOS原生renderer可用，GUI事件循环；工作区两个portable-hdr素材存在。
3. 输入数据：gain-map.jpg、bayer.dng两个独立数据行。
4. 操作步骤：testHDRInfoUsesNormalOutput加载并等待真正nativeOutput，记录正常输出名与源信息；打开真实信息页并调用相同生产失活入口；等待实时API报告preview；等待700ms跨越450ms动画与多轮timer；关闭/重开信息页，重复两轮。
5. 预期结果：信息页输出持续等于正常HDR输出名且无Preview前缀；实时API仍报告预览；源ICC不变。
6. 后置条件：scope guard关闭窗口，恢复退出策略，释放临时素材。

## D2（A3、A4）：换图与关闭
1. 测试目的：证明不冻结整张旧图的信息。
2. 前置条件：D1中HDR信息页已打开并经历退化。
3. 输入数据：临时Qt栅格XPM（非原生HDR）及空状态。
4. 操作步骤：D1后半段保持信息页打开、切换XPM，再关闭当前图片；比较新方法与实时值；检查label和source。
5. 预期结果：XPM不带旧HDR输出、不标预览、显示真实Qt色彩空间；关闭后No output及Unknown source。
6. 后置条件：与D1一起清理；两种HDR数据均执行本组断言。

## D3（A3、A5）：实时与正常查询的回退边界及回归
1. 测试目的：验证非HDR/空图/原生SDR采用实时语义，原来实时色彩信息测试不失效。
2. 前置条件：既有ImageCoreAndMovieTests和机型专项可运行。
3. 输入数据：空视图、Qt栅格、DisplayP3 PNG以及既有颜色/机型/时间素材。
4. 操作步骤：testFileInfoColorFallback检查加载前、Qt栅格、关闭后新旧查询字段一致；完整ImageCoreAndMovieTests及FovelleCameraModel运行现有真实原生SDR、颜色、信息页用例。
5. 预期结果：无HDR时新旧值一致；source与output分离；已有测试通过。
6. 后置条件：关闭测试窗口、释放loader和临时资源。

映射：A1→S1/D1；A2→D1；A3→S1/D3；A4→D2/D3；A5→S1/D1/D3。D1/D2共享相同端到端生命周期，每条断言实际执行；没有只写规格而不实现的测试用例。先运行旧provider验证D1失败，再接入生产修复并复测。
