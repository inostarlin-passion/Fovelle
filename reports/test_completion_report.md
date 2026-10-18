# 图片色彩路径修复测试完成报告

日期：2026-10-18（Asia/Shanghai）。基线：658ce8c，Fovelle 1.3.2。

已完成问题界定、设计与正确性论证、原子验收及六要素测试规范、测试代码、生产实现和执行验证。对应文件：[设计文档](/Users/inostarlin/code/Fovelle/reports/technical_design_document.md)、[测试规范](/Users/inostarlin/code/Fovelle/reports/test_case_specification.md)。旧报告已备份至本次证据目录。

## 修复结果

| 问题 | 实现结果 | 像素/状态证据 |
|---|---|---|
| C1 SVG/SVGZ广色域变黑 | 规范化display-p3/srgb/srgb-linear，属性、CSS、嵌入图片及各renderer共用源；P3工作空间和显示ICC转换；透明度使用solidColor | UTF-8/UTF-16 × SVG/SVGZ四行通过；P3红、普通sRGB红、CSS、渐变端点、alpha、嵌入PNG符合独立参考 |
| C2 PGM/PPM传递函数错误 | 按magic独立读取P2/P3/P5/P6到RGBX64，赋BT.709曲线，源信息标记容器规定空间 | 中灰128转换sRGB约140；黑白和16位原样值通过；不误覆盖PBM/PFM；超大尺寸、截断、越界样本安全失败 |
| C3 多图像容器打不开 | TIFF显示首图，ICNS选最大表示；向loader传递静态多图像标志 | 自生成双页TIFF和双尺寸ICNS，先验证系统每个index可读，再验证App尺寸、像素与不误播放 |
| C4 EPS内部色域与精度限制 | Qt6预览与最终PDF tile均用32位浮点扩展线性P3，最后按显示ICC转换 | P3红保留；AdobeRGB绿保留负红分量，证明未先裁到整数P3；EPS预览及原有缩放回归通过 |
| C5 分块高位深丢失 | 16位RGB、Gray及浮点保持真实组件格式；ICC模型与provider匹配；阈值计入组件字节数、band计入实际stride | 0x8080/0x8081可区分，Gray ICC、浮点小数、alpha和8/16/32位虚拟尺寸阈值通过；不分配数GB测试图 |
| C6 动画残留静态HDR表示 | 播放启动时同时清除静态SDR/HDR引用、标志与HDR元数据 | 真正启动GIF，注入HDR表示后确认全部清除，并观察后续帧推进 |
| C7 无色彩声明的原始空间不可恢复 | 保留默认解释但明确显示假设，保护有效ICC与容器声明 | 无ICC/错误ICC/P3声明的信息及原有源元数据回归通过；不声称可自动推断丢失空间 |

生产改动主要位于svgcolor、colorinformation、qvcocoafunctions、qvimageloader、qvgraphicsimageitem、qvimagecore和qvinfodialog；CMake及qmake源注册同步更新。测试固化到color_rendering_acceptance.inc、native_color_fixtures.mm、color_rendering_static.py和legacy_bt709_tests.cpp；原有测试文件仅新增slots/include，没有修改旧桌面断言或阈值。

## 环境和执行结果

本地macOS 27.0.1、arm64、Qt/QtTest 6.11.2、Release，当前编译器Apple Clang 21.0.0；Qt库本身的构建说明为Apple LLVM 17。部署目标15.0，BUILD_TESTS和FOVELLE_REQUIRE_PORTABLE_HDR开启。

证据目录：[color_rendering_20261018](/Users/inostarlin/code/Fovelle/reports/evidence/color_rendering_20261018)。该目录沿用仓库ignore规则，原始日志在本地保留。

| 验收 | 实际执行 | 结果 |
|---|---|---|
| A1/A2 | testColorSvgWideGamut、testColorSvgMixedPaints；S1 | 通过，含压缩与UTF-16反例、普通红独立W3C数值参考 |
| A3 | testColorNetpbm；S2；LegacyBt709Tests::transferCurve | 通过；兼容后端额外覆盖toe、分段边界及各RGB通道 |
| A4 | testColorNativeMultiImage；S2 | TIFF、ICNS通过，静态多图像未被当电影 |
| A5 | testColorPdfPrecision；S3；既有EPS/矢量回归 | 通过，预览和最终tile均验证；额外CoreGraphics探针得到AdobeRGB绿约(-0.150082,0.953588,0.0264872) |
| A6 | testColorBoundedProvider；S3 | 实际CG/CI provider保留低位、Gray ICC、浮点与alpha；边界阈值通过 |
| A7 | testColorAnimationClearsStaticRepresentations；S4；原有电影生命周期/恢复 | 通过 |
| A8 | testColorUnspecifiedSource；S4；原有源ICC/容器信息测试 | 通过 |
| A9 | 完整CTest及新增兼容门禁 | 原登记的62项完整回归全过；随后兼容分支门禁单独通过并注册为第63项 |
| 构建 | cmake --build build --parallel 3 | 成功；有既有Qt弃用警告 |
| 静态专项 | color_rendering_static.py | 5/5通过；对HEAD运行同组契约为5失败，保留红色证据 |
| 色彩动态专项 | FovelleColorRenderingDynamic | 16 passed、0 failed、0 skipped，包含初始化/清理；14个实际数据行 |
| 兼容后端专项 | FovelleLegacyBt709Backend | 10 passed、0 failed、0 skipped，包含初始化/清理；8个实际数据行 |
| 差异与格式 | git diff --check；新C++/ObjC++文件及inc的clang-format检查 | 通过；既有文件只格式化修改范围 |

完整回归命令为`QT_QPA_PLATFORM=cocoa ctest --test-dir build --output-on-failure --timeout 120`，最终为**62/62通过，555.11秒**，含主套件、真实信号HDR/RAW、部署包、窗口/缩放/动画及本次色彩测试。详见[完整最终日志](/Users/inostarlin/code/Fovelle/reports/evidence/color_rendering_20261018/ctest-full-final.log)。

完整运行结束后，补核发现旧Qt缺少采样曲线构造器及描述API，增加条件编译的ICC曲线后端，并固化新的兼容CTest。正常Qt6生产分支在守卫前后预处理token完全相同，SHA-256均为`3baafe9d6487a0f758bdf633af9575d7d036f5122e12632184dcccb6b194ec5a`；它未改变上述完整回归覆盖的Qt6逻辑。重新构建后，色彩静态、色彩动态和新增兼容门禁**3/3通过，0.48秒**，见[交付验证](/Users/inostarlin/code/Fovelle/reports/evidence/color_rendering_20261018/ctest-delivery.log)及[Qt逐行结果](/Users/inostarlin/code/Fovelle/reports/evidence/color_rendering_20261018/delivery-qt-cases.log)。没有把两次结果伪称为同一次完整63项运行。

## 首次失败与证伪记录

- 首轮实现的动态测试为7失败：Qt SVG也不接受rgba()，Qt PNM会丢弃16位输入。分别修为solidColor及独立RGBX64解码后通过。后续Gray ICC反例暴露跨模型扩展失效与Qt转换低位合并，改为保留原组件模型的provider后通过。保留focused-first.log、focused-complete.log、gray-provider-before.log等原始记录。
- 首次完整CTest为59/62通过、581.08秒。失败分别为失焦透明背景截图、HDR全屏端点控制、HDR信息窗口reject恢复；详见ctest-full-first.log，首次portable HDR XML另行备份。它们没有被删除、放宽阈值或跳过。
- 用未修改HEAD的独立归档构建做对照：两个HDR用例独立通过；导航测试首次因仅构建test目标未生成QM资源失败，不能作为产品缺陷证据。生成原有Fovelle_lrelease资源后，导航用例独立通过。最终本分支完整整轮的三项也均通过。没有足够证据把首次桌面状态异常断言为本次色彩修改引入的确定根因，也不因一次绿色结果宣称系统时序永远稳定。

## 验证边界

- 这里修复已定位的渲染/读取问题，不表示所有相机、纹理子格式、任意ICC或每种显示器都已完成色度仪验收。portable DNG是合成信号，不替代厂商相机样本。
- SVG仍受Qt SVG能力与颜色输入精度限制；支持本次列出的现代颜色，其他现代空间明确报错。渐变验证覆盖端点，不承诺完整CSS4插值/滤镜实现。
- 动画清理修复静态HDR遮盖播放帧；当前动画继续使用既有SDR帧路径，没有新增逐帧原生HDR播放。
- 无声明图片的原始色彩空间无法可靠恢复。标准PGM/PPM按BT.709解释，采用sRGB/线性样本的非标准变体需先转换。
- 本次没有真实Qt5全量构建、qmake全量构建或macOS15整机测试。兼容后端是在当前Qt上验证旧构造分支；Qt5 EPS兼容分支保持16位P3，不等同Qt6的扩展浮点输出。
- 未提交、推送或触发远端CI。最终源文件SHA-256见source-manifest.json。
