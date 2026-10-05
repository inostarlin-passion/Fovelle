# 更新功能测试用例说明

日期：2026-10-03（Asia/Shanghai）。验收分解为 AC1–AC8，每条含静态与动态用例，元数据固化在 `tests/update_test_cases.json`，执行入口为 `tests/update_acceptance.py` 和 Qt FeatureTests。

静态检查验证应用接线、构建、安全配置和发布约束；动态检查运行真实 Sparkle 标准界面与 Installer，不以模拟下载/安装结果代替。安装测试使用 Cocoa 隔离夹具，Qt 包装层通过生产函数与生命周期测试验证。此组合不能等同于已完成线上 Developer ID 公证版端到端验证。

## AC1-S：手动检查显示进度（静态）

- **测试目的**：手动检查显示进度
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：慢速 appcast，菜单/关于按钮
- **操作步骤**：两个入口调用标准检查 API；延迟响应期间观察 NSProgressIndicator
- **预期结果**：进度在响应前可见，关于窗口不阻挡 Cocoa UI
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_manual_entries_use_standard_ui`

## AC1-D：手动检查显示进度（动态）

- **测试目的**：手动检查显示进度
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：慢速 appcast，菜单/关于按钮
- **操作步骤**：两个入口调用标准检查 API；延迟响应期间观察 NSProgressIndicator
- **预期结果**：进度在响应前可见，关于窗口不阻挡 Cocoa UI
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`test_check_cancel`, `test_download_install_relaunch`

## AC2-S：检查与下载可取消且可恢复（静态）

- **测试目的**：检查与下载可取消且可恢复
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：检查延迟与 2 MiB 慢速下载
- **操作步骤**：点击 Cancel，等待 canCheckForUpdates 恢复
- **预期结果**：不安装、不替换；状态恢复；一次请求
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_state_and_scheduler`

## AC2-D：检查与下载可取消且可恢复（动态）

- **测试目的**：检查与下载可取消且可恢复
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：检查延迟与 2 MiB 慢速下载
- **操作步骤**：点击 Cancel，等待 canCheckForUpdates 恢复
- **预期结果**：不安装、不替换；状态恢复；一次请求
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`test_check_cancel`, `test_download_cancel`

## AC3-S：当前版本与错误有反馈（静态）

- **测试目的**：当前版本与错误有反馈
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：同版本、畸形 XML、HTTP 503
- **操作步骤**：运行各响应场景，观察标准结果提示并关闭
- **预期结果**：当前版本不下载；错误显示提示，当前应用不变
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_manual_entries_use_standard_ui`

## AC3-D：当前版本与错误有反馈（动态）

- **测试目的**：当前版本与错误有反馈
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：同版本、畸形 XML、HTTP 503
- **操作步骤**：运行各响应场景，观察标准结果提示并关闭
- **预期结果**：当前版本不下载；错误显示提示，当前应用不变
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`test_no_update`, `test_invalid_feed`, `test_network_error`

## AC4-S：应用内下载、安装、重启新版本（静态）

- **测试目的**：应用内下载、安装、重启新版本
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：1.0.0 → 签名的 2.0.0 测试归档
- **操作步骤**：点击 Install Update，再点 Install and Relaunch；读取版本和重启标记
- **预期结果**：实际下载完成、安装发生、包版本变为 2.0.0、重启标记出现
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_manual_entries_use_standard_ui`

## AC4-D：应用内下载、安装、重启新版本（动态）

- **测试目的**：应用内下载、安装、重启新版本
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：1.0.0 → 签名的 2.0.0 测试归档
- **操作步骤**：点击 Install Update，再点 Install and Relaunch；读取版本和重启标记
- **预期结果**：实际下载完成、安装发生、包版本变为 2.0.0、重启标记出现
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`test_download_install_relaunch`

## AC5-S：拒绝篡改更新（静态）

- **测试目的**：拒绝篡改更新
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：签名后翻转 ZIP 资源数据字节
- **操作步骤**：下载篡改包，观察签名错误；对比旧包摘要
- **预期结果**：EdDSA 验证失败，不安装、不替换、不重启
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_secure_configuration_and_lifetime`

## AC5-D：拒绝篡改更新（动态）

- **测试目的**：拒绝篡改更新
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：签名后翻转 ZIP 资源数据字节
- **操作步骤**：下载篡改包，观察签名错误；对比旧包摘要
- **预期结果**：EdDSA 验证失败，不安装、不替换、不重启
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`test_tampered_archive_rejected`

## AC6-S：校验更新配置并拒绝不完整发布构建（静态）

- **测试目的**：校验更新配置并拒绝不完整发布构建
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：HTTPS/HTTP/空地址/认证地址，32 字节密钥/短密钥/非规范 base64
- **操作步骤**：执行生产校验函数、初始化无配置更新器；用 CMake 脚本模式尝试无效发布参数
- **预期结果**：仅规范 HTTPS 和有效公钥通过；无配置有可读错误且不联网；无效发布失败
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_release_configuration_rejects_invalid`

## AC6-D：校验更新配置并拒绝不完整发布构建（动态）

- **测试目的**：校验更新配置并拒绝不完整发布构建
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：HTTPS/HTTP/空地址/认证地址，32 字节密钥/短密钥/非规范 base64
- **操作步骤**：执行生产校验函数、初始化无配置更新器；用 CMake 脚本模式尝试无效发布参数
- **预期结果**：仅规范 HTTPS 和有效公钥通过；无配置有可读错误且不联网；无效发布失败
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`FeatureTests.testUpdateConfigurationValidation`

## AC7-S：依赖打包与签名发布闭环（静态）

- **测试目的**：依赖打包与签名发布闭环
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：官方固定校验和框架，匹配/不匹配的临时签名 seed
- **操作步骤**：检查复制/rpath/签名顺序；用真实发布脚本生成 appcast 并核对 enclosure；换错误 seed
- **预期结果**：框架自包含；发布在公证验证后生成 feed；URL/签名正确；错 key 被拒绝
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_release_closes_download_install_path`

## AC7-D：依赖打包与签名发布闭环（动态）

- **测试目的**：依赖打包与签名发布闭环
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：官方固定校验和框架，匹配/不匹配的临时签名 seed
- **操作步骤**：检查复制/rpath/签名顺序；用真实发布脚本生成 appcast 并核对 enclosure；换错误 seed
- **预期结果**：框架自包含；发布在公证验证后生成 feed；URL/签名正确；错 key 被拒绝
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`test_download_install_relaunch`

## AC8-S：自动检查频率和禁用设置（静态）

- **测试目的**：自动检查频率和禁用设置
- **前置条件**：仓库源文件存在；Python/CMake 可用。
- **输入数据**：Never、Daily、Weekly、Monthly，编译/环境禁用选项
- **操作步骤**：检查调度器接线；执行生产间隔映射函数
- **预期结果**：禁用选项禁止自动检查；其余对应 1/7/30 天；手动仍走标准 UI
- **后置条件**：生产文件与配置不变。
- **测试代码**：`StaticTests.test_state_and_scheduler`

## AC8-D：自动检查频率和禁用设置（动态）

- **测试目的**：自动检查频率和禁用设置
- **前置条件**：macOS 桌面已登录；Qt、CMake、Sparkle 已按固定版本配置；测试使用临时应用和测试密钥。
- **输入数据**：Never、Daily、Weekly、Monthly，编译/环境禁用选项
- **操作步骤**：检查调度器接线；执行生产间隔映射函数
- **预期结果**：禁用选项禁止自动检查；其余对应 1/7/30 天；手动仍走标准 UI
- **后置条件**：临时应用、私钥、更新缓存和测试 defaults 清理；生产应用与密钥不变。
- **测试代码**：`FeatureTests.testUpdateCheckFrequencyPolicy`

## 执行命令

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build -R 'FovelleUpdate|FovelleNativeAlerts' --output-on-failure
python3 tests/update_acceptance.py
```

也可设置 `-DFOVELLE_ENABLE_UPDATE_INSTALL_TESTS=ON`，将隔离 UI/安装测试注册为 CTest。该测试需要已登录的 macOS 图形会话和支持 Ed25519 的 OpenSSL，仅在明确启用时运行。测试 HTTP 服务器只绑定 127.0.0.1；生产包装层仍只接受 HTTPS。生产发布脚本的 Swift CryptoKit 校验从 stdin 读取临时测试私钥，测试不写入登录钥匙串。
