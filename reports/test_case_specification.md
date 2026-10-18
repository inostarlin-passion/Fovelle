# 图片色彩修复测试规范

日期：2026-10-18。标准A1–A9见technical_design_document.md。每个用例均固化为代码；静态用例tests/color_rendering_static.py，动态用例tests/color_rendering_acceptance.inc及native_color_fixtures.mm。日志位于reports/evidence/color_rendering_20261018/。

## S1：SVG规范化唯一源（A1/A2）
1. 测试目的：防止SVG规范化唯一源被后续改动绕开；像素正确性由对应动态用例独立验证。
2. 前置条件：Python3，可读取仓库源文件。
3. 输入数据：loader、GUI与worker的SVG入口。
4. 操作步骤：执行python3 tests/color_rendering_static.py ColorContracts.test_svg_has_one_normalized_source。
5. 预期结果：规范化后以encodedData优先，tile沿用源色彩空间；修复前的相关契约失败，修复后通过。
6. 后置条件：代码及用户偏好不变。

## S2：Netpbm与多图像原生路径（A3/A4）
1. 测试目的：防止Netpbm与多图像原生路径被后续改动绕开；像素正确性由对应动态用例独立验证。
2. 前置条件：Python3，可读取仓库源文件。
3. 输入数据：Netpbm magic分支、selectedImageIndex与loader静态多图像标志。
4. 操作步骤：执行python3 tests/color_rendering_static.py ColorContracts.test_netpbm_and_multi_image。
5. 预期结果：三个入口契约均存在且不依赖Qt插件；修复前的相关契约失败，修复后通过。
6. 后置条件：代码及用户偏好不变。

## S3：矢量与provider精度（A5/A6）
1. 测试目的：防止矢量与provider精度被后续改动绕开；像素正确性由对应动态用例独立验证。
2. 前置条件：Python3，可读取仓库源文件。
3. 输入数据：PDF/分块颜色空间与位深代码、矢量显示转换。
4. 操作步骤：执行python3 tests/color_rendering_static.py ColorContracts.test_vector_and_provider_precision。
5. 预期结果：16位路径与真实组件位数、输出空间转换均保留；修复前的相关契约失败，修复后通过。
6. 后置条件：代码及用户偏好不变。

## S4：动画状态与源证据（A7/A8）
1. 测试目的：防止动画状态与源证据被后续改动绕开；像素正确性由对应动态用例独立验证。
2. 前置条件：Python3，可读取仓库源文件。
3. 输入数据：movie启动后的清理代码及文件信息状态分支。
4. 操作步骤：执行python3 tests/color_rendering_static.py ColorContracts.test_animation_and_source_evidence。
5. 预期结果：HDR引用与标志清除，源ICC状态判断保留；修复前的相关契约失败，修复后通过。
6. 后置条件：代码及用户偏好不变。

## S5：CTest及构建入口注册（A9）
1. 测试目的：防止CTest及构建入口注册被后续改动绕开；像素正确性由对应动态用例独立验证。
2. 前置条件：Python3，可读取仓库源文件。
3. 输入数据：tests/CMakeLists.txt、native fixture与Qt slot注册。
4. 操作步骤：执行python3 tests/color_rendering_static.py ColorContracts.test_ctest_registration。
5. 预期结果：新静态/动态用例注册，原门禁保留；修复前的相关契约失败，修复后通过。
6. 后置条件：代码及用户偏好不变。

## D1：SVG广色域与压缩输入（A1）
1. 测试目的：捕获P3红被画黑及压缩SVG绕开规范化。
2. 前置条件：Qt/Cocoa，临时目录，真实SVG和gzip字节。
3. 输入数据：color(display-p3 1 0 0)，同内容svg/svgz × UTF-8/UTF-16，透明度1及0.5。
4. 操作步骤：通过生产载入器读文件；以规范化源绘制16位图；按颜色空间转换到参考空间比较像素；检查vector尺寸。
5. 预期结果：P3标签与红色分量正确，非黑，alpha在量化容差内，压缩和未压缩一致。
6. 后置条件：销毁渲染器和临时文件。

## D2：SVG颜色混用与负例（A2）
1. 测试目的：防止修P3时把普通sRGB颜色重新解释为P3；验证无效输入不静默变黑。
2. 前置条件：同D1。
3. 输入数据：P3色块、普通red/#hex/rgb、CSS规则、渐变端点、透明度、嵌入PNG；不支持/无效color()；普通SVG。
4. 操作步骤：规范化并渲染；与QColorSpace独立转换参考比较；检查普通源字节不变、无效输入错误。
5. 预期结果：各色块对应原始声明，alpha不变；普通源不改；不支持颜色给出可操作错误。
6. 后置条件：无残留资源。

## D3：Netpbm数学参考（A3）
1. 测试目的：真实像素验证BT.709，排除仅改元数据。
2. 前置条件：Qt测试程序。
3. 输入数据：P2/P3/P5/P6黑、灰128、白、彩色，16位梯度；PBM/PFM反例；改错扩展名。
4. 操作步骤：写最小样本，经生产载入；转换sRGB，逐像素与独立逆709/正sRGB公式比较；检查源描述。
5. 预期结果：128约140，黑白不变，RGB各通道独立，16位有低位差异；PBM/PFM不错误覆盖为709，magic优先。
6. 后置条件：临时目录清理。

## D3-L：旧Qt的ICC采样曲线后端（A3/A9）
1. 测试目的：防止引入旧Qt缺少的采样曲线构造器/描述API，并验证兼容ICC后端的真实像素。
2. 前置条件：CMake测试构建；当前Qt运行FOVELLE_TEST_LEGACY_BT709分支，不等同于安装旧Qt的完整构建。
3. 输入数据：P2/P3/P5/P6 × Maxval255/65535；黑、线性toe、分段边界、中灰、白及16位低位；RGB通道错位排列。
4. 操作步骤：构建fovelle_legacy_bt709_tests，执行ctest -R '^FovelleLegacyBt709Backend$'；原有D3也以替换对象的方式验证同一后端进入完整App加载器。
5. 预期结果：8个数据行通过，逐通道匹配独立BT.709逆曲线与sRGB正曲线，容差0.004；原始16位值精确保留。正常Qt6分支的预处理token与兼容守卫前完全相同。
6. 后置条件：临时信号清理；不改用户设置、不访问桌面窗口。

## D4：多图像原生解码（A4）
1. 测试目的：证明系统能读而App不能读的反例被消除，且图标选取正确。
2. 前置条件：ImageIO生成器，不依赖外部相机或系统应用路径。
3. 输入数据：两页不同色TIFF；不同尺寸、不同色ICNS/ICO；单图TIFF对照。
4. 操作步骤：先直接ImageIO验证count与每个index可读，再通过App加载，检查选中尺寸/像素/多图像静态标志。
5. 预期结果：TIFF显示首图、图标显示最大有效表示；不依赖Qt TIFF/ICNS插件；不误播放静态表示。
6. 后置条件：关闭source/destination，清除临时文件。

## D5：EPS/PDF颜色精度（A5）
1. 测试目的：覆盖预览与最终tile，识别内部先裁到sRGB的问题。
2. 前置条件：CoreGraphics PDF生成器，EPS分支需仓库捆绑Ghostscript。
3. 输入数据：含ICC P3颜色的PDF、超出P3的AdobeRGB绿色PDF、常规EPS、透明/非透明色块。
4. 操作步骤：生成PDF，调用生产renderPDFVectorTile；加载EPS检查预览；与ColorSync P3参考比较像素与位深。
5. 预期结果：Qt6浮点premultiplied、线性P3标签，P3端点保留且AdobeRGB绿保留负红分量；普通EPS几何、透明度及缩放回归通过。
6. 后置条件：释放CG与临时资源。

## D6：真实高精度provider（A6）
1. 测试目的：用小图覆盖超大图同一工厂，检查低位真实保存而非仅标记16位。
2. 前置条件：生产分块工厂及仅测试构建的像素采样接口。
3. 输入数据：RGBA64相邻值0x8080/0x8081及alpha；Qt6浮点信号；P3 ICC；带RGB及Gray模型ICC的Grayscale16；8/16/32位虚拟尺寸provider阈值。
4. 操作步骤：构造真实分块，采样CIImage像素，与源值比较；验证组件位数、分块范围与无效输入。
5. 预期结果：保留低位差异与alpha/ICC，不统一降8位；空输入安全失败。
6. 后置条件：工厂和provider释放，无多GB分配。

## D7：动画清理（A7）
1. 测试目的：静态HDR状态不能遮盖后续播放帧。
2. 前置条件：真实电影fixture、可注入loader结果的生产core测试接口。
3. 输入数据：包含静态HDR表示并可播放的APNG/GIF加载结果。
4. 操作步骤：经loadPixmap实际启动电影；观察HDR/SDR标志和引用、后续帧。
5. 预期结果：电影运行，两个静态表示均清空，HDR标志和元数据不残留，帧推进。
6. 后置条件：stop、关闭core，恢复设置。

## D8：无声明/错误ICC边界（A8）
1. 测试目的：源空间未知不能误报成已知，已有ICC不应覆盖。
2. 前置条件：现有信息对话框测试设施。
3. 输入数据：无ICC普通PNG、有效P3 ICC、损坏/截断ICC，PNM容器声明。
4. 操作步骤：调用源元数据读取和真实对话框刷新；转换无标签像素与有效ICC像素。
5. 预期结果：明确默认假设，不声称恢复原空间；有效ICC和容器声明保持；错误/未知不伪装成嵌入。
6. 后置条件：对话框与设置恢复。

## D9：完整回归（A9）
1. 测试目的：确保新修复不破坏原生HDR、电影生命周期、EPS/SVG矢量缩放与部署。
2. 前置条件：Release构建、BUILD_TESTS=ON，portable HDR fixtures。
3. 输入数据：完整CTest及定向色彩测试。
4. 操作步骤：先运行新增测试取得失败证据，再实现；构建并执行定向测试和完整ctest --test-dir build --output-on-failure。
5. 预期结果：新增验收通过，原门禁保持；失败与跳过必须如实披露，不以重试成功替换失败。
6. 后置条件：记录日志与最终报告，不提交/推送或修改用户偏好。
