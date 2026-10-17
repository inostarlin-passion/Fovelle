# 完整HDR源缓存测试规格

## S1（A1、A3、A4）：映射所有权静态检查
1. 测试目的：现代persistent使用完整源，任务标签按值捕获，旧Metal/current与取消/预算路径保留。
2. 前置条件：源码可读。
3. 输入数据：qvcocoafunctions.mm及CTest配置。
4. 操作步骤：运行hdr_surface_metadata_static.py与既有hdr_headroom_restore_static.py，检查preparedHDRImage源、CGImage标签复制、macOS可用性保护、serial及安装读取自身标签。
5. 预期结果：不从完成时全局目标推断标签；现代monitor不随current重建源缓存；旧策略仍存在。
6. 后置条件：无修改。

## D1（A1、A2、A5）：返回后实际完整HDR缓存
1. 测试目的：捕获“像素被current预先压缩”的旧路径，覆盖用户真实文件。
2. 前置条件：macOS15+原生renderer，仓库素材；用户文件通过FOVELLE_HDR_REPRO_IMAGE指定。
3. 输入数据：gain-map.jpg、bayer.dng、用户1.JPG；current从16到后台1，返回(源内容余量+1)/2。
4. 操作步骤：testHDRPersistentSurfaceMetadata建立正常缓存字节/身份基线；打开信息页并走生产失活入口，低余量停留；回到真实窗口并关闭信息页；读取CALayer.contents的CGImage及layer标签。
5. 预期结果：完整源字节、对象身份不变，CGImage标签等于source内容余量，layer标签与其一致或合法未覆盖值0；真实native输出恢复。旧应用预先映射的缓存会改变像素或较低标签，不能通过。
6. 后置条件：关闭窗口，恢复所有环境变量，释放CF数据。

## D2（A2、A3、A4）：current降额及稳定缓存
1. 测试目的：确认current不再被烘焙进现代persistent，系统仍可根据完整HDR信号自适应。
2. 前置条件：D1已加载。
3. 输入数据：current再降到1，再回16；既有headroom测试的JPEG/RAW。
4. 操作步骤：D1后半段核对完整内容标签及缓存身份保持，回16核对字节基线；testHDRHeadroomSurfaceRestores现代分支验证失活、current变化、恢复、缓存稳定和换图清理。旧系统保留原应用侧降额交错断言。
5. 预期结果：现代完整源缓存不因current变化重建；换图后无旧HDR缓存；Metal政策仍由当前余量控制。不能把“缓存字节保持完整HDR”误称为物理屏幕无视低余量强行增亮。
6. 后置条件：释放窗口、设置及资源。

## D3（A3、A5）：端点与界面回归
1. 测试目的：保留真实完整源颜色、焦点恢复和正常输出信息。
2. 前置条件：已构建两组完整套件，HDRPolicy配置仓库JPEG样本。
3. 输入数据：ImageCoreAndMovieTests、HDRPolicyTests及机型门禁。
4. 操作步骤：完整运行两组套件，含probePersistentHDRPixels与直接解码源端点的独立比较；执行CameraModel及信息页静态门禁。
5. 预期结果：全部通过；current/potential政策、真实像素保真、关闭恢复和机型正确。
6. 后置条件：资源清理。

映射：A1→S1/D1；A2→D1/D2；A3→S1/D2/D3；A4→S1/D2；A5→D1用户行/D3。current覆盖是确定性模拟，不冒称真实跨应用物理亮度计测量。旧OS运行保护仅静态核验。
