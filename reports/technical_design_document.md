# Fovelle 更新功能技术设计

日期：2026-10-03（Asia/Shanghai）。需求依据：`/Users/inostarlin/Downloads/方案.md`。

## 问题与显式前提

原实现查询 GitHub Releases JSON 后打开浏览器，没有检查进度、包验证或应用内安装。目标是在 macOS 应用中提供手动检查进度，并完成应用内下载、验证、安装、重启。

前提：Developer ID 直接分发；非 Mac App Store；现有工程没有开启 App Sandbox；更新服务采用 HTTPS；发布方保有 Ed25519 签名 seed 和 Apple 发布证书。开发环境没有可用生产更新公钥，因此允许开发构建缺省配置，但手动检查明确提示缺失；tag 发布强制要求配置。本文不声称生产服务已经上线。

## 原子验收标准

| 编号 | 可单独判定的标准 |
| --- | --- |
| AC1 | 菜单与关于按钮的手动检查显示标准检查进度；关于窗口不阻挡更新界面。 |
| AC2 | 检查、下载均可取消，状态恢复，不替换应用。 |
| AC3 | 当前版本、网络故障、无效更新源都有标准反馈；无新版本不下载。 |
| AC4 | 新版本在应用内下载、安装，重启后实际运行新版本，无下载网页跳转。 |
| AC5 | 篡改的归档验证失败，不安装、不替换、不重启。 |
| AC6 | 仅接受有效 HTTPS 更新源和规范 32 字节 Ed25519 公钥；缺失配置明确反馈；不完整发布构建失败。 |
| AC7 | 固定校验和的框架被嵌入；签名由内到外；经过公证验证的归档生成签名 appcast。 |
| AC8 | Never/禁用选项禁止自动检查；Daily/Weekly/Monthly 对应 1/7/30 天；手动检查可用。 |

每条标准的静态和动态测试、六项用例要素及代码映射见 `test_case_specification.md` 和 `tests/update_test_cases.json`。

## 多跳检索与交叉验证

1. 从方案引用进入 [SPUStandardUpdaterController API](https://sparkle-project.org/documentation/api-reference/Classes/SPUStandardUpdaterController.html)，确认标准控制器的手动检查入口与进度界面。
2. 从基础说明进入 [programmatic setup / Qt](https://sparkle-project.org/documentation/programmatic-setup/)，核对 Objective-C++、ARC、控制器生命周期、KVO、链接与复制要求，再与下载的官方头文件比对。
3. [Sparkle 基础文档](https://sparkle-project.org/documentation/)分别支持 HTTPS、EdDSA 归档签名、公钥/更新源配置、appcast 生成和更新验证；[sandboxing 文档](https://sparkle-project.org/documentation/sandboxing/)补充手工签名顺序与 entitlements 保留要求。当前非沙盒工程不启用沙盒 XPC 开关。
4. [Apple Updating Mac Software](https://developer.apple.com/documentation/security/updating-mac-software)说明原地修改签名代码可能引发签名缓存问题，支持采用完整更新安装器；[Apple 公证说明](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)与现有 Developer ID 发布脚本交叉核对。Apple 页面正文通过其官方 Markdown 链接读取。
5. [App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/)用于核对 MAS 分发边界；[Sparkle 2.10.0 官方发布](https://github.com/sparkle-project/Sparkle/releases/tag/2.10.0)与 GitHub Release API 提供的 SHA-256 交叉核对官方二进制。最终固定 2.10.0，不依赖可漂移的 latest 下载。
6. 官方 API 定义 + 仓库接线 + 隔离真实安装共同验证方案。检索在架构、API、打包、签名和发布边界均有直接证据后收敛。

## 实现与链式推导

`Qt 菜单/关于按钮 → UpdateChecker::check(true) → SPUStandardUpdaterController::checkForUpdates: → 标准检查进度 → appcast 版本判定 → 标准下载进度 → EdDSA 验证 → Sparkle Installer 替换 → 重启`。

`src/updatechecker_sparkle.mm` 用 ARC 和私有 Impl 持有控制器与 KVO observer。在 Qt 完成 Cocoa 启动后的事件循环初始化；配置不合法时不创建联网更新器。配置校验位于实际生产调用的 `isConfigurationValid`。显式调用 `startUpdater:` 接收启动错误，再观察 `canCheckForUpdates`；析构时解除观察。Qt 关于按钮在状态变化后更新可用性，并将关于窗口改为非模态，以免 Qt 模态窗口阻挡 Cocoa 更新界面。

删除原有 JSON 查询、数字拼接版本比较、Qt 结果窗口、跳过版本数据库及浏览器下载路径。版本比较、跳过版本、错误提示、重复检查期间的界面管理、取消、下载和安装均由标准更新器负责。自动调度也由 Sparkle 持有；偏好变更只在值不同的时候更新属性。Monthly 改为 Sparkle 的固定 30 天间隔，区别于旧代码的日历月策略，已显式固化测试。

CMake 下载并验证官方分发归档，用 ditto 保留框架符号链接/权限。生产应用只添加 `@executable_path/../Frameworks` 的 Sparkle rpath，避免依赖开发机路径。qmake 使用同一框架和模板、独立 ARC 编译规则与配置生成脚本；同时修复本机暴露出的 VERSION 大小写文件名遮蔽标准头、版本字符串转义以及缺失可选 qtbase 翻译目录问题。框架第三方许可位于 `third_party/sparkle`。

## 发布配置

GitHub repository variable：`SPARKLE_PUBLIC_ED_KEY`，值为 base64 公钥。GitHub secret：`SPARKLE_PRIVATE_ED_KEY`，值为 base64 的 32 字节私钥 seed；Apple 发布 secrets 延用原流程。发布者可使用 Sparkle 的 `generate_keys` 生成并管理密钥，导出时注意 seed 格式；代码不创建或写入生产密钥。

开发/发布构建参数：

```bash
cmake -S . -B build \
  -DFOVELLE_UPDATE_FEED_URL=https://github.com/inostarlin-passion/Fovelle/releases/latest/download/appcast.xml \
  -DFOVELLE_UPDATE_PUBLIC_KEY='<发布公钥>' \
  -DFOVELLE_REQUIRE_UPDATE_CONFIG=ON
```

发布脚本保留组件 entitlements，按内层 Mach-O、嵌套 bundle、框架、应用顺序签名；完成公证、staple、Gatekeeper、解包回验后生成 appcast。生成前用 Swift CryptoKit 从 stdin 的 seed 派生公钥，与应用内公钥比对；生成器也通过 stdin 接收 seed。归档下载地址指向固定 tag 的 Release，appcast 随同该 Release 上传，客户端读取 latest/download/appcast.xml。发布构建启用正常自动检查，仅测试步骤通过环境变量禁用自动联网。没有部署新服务或发布远端 Release。

## 逆向证伪与限制

- 断网/错误 feed：标准错误界面，当前应用不变。
- 用户取消：恢复可再次检查，不安装。
- 篡改包/签名 seed 不匹配：分别拒绝安装/拒绝生成发布 feed。
- 缺省/HTTP/认证地址/错误公钥：生产配置校验拒绝；发布配置失败。
- 检查按钮重复触发、旧 Qt 回调重复提示：标准控制器管理会话；旧 `checkedUpdates` 路径已删除。
- 依赖符号链接、外部 rpath、错误签名顺序：ditto、包内 rpath、深度优先签名与实际 bundle 检查防止遗漏。
- 未开启 Sandbox 不代表以后可以直接开启：未来沙盒分发须补充 Installer XPC 配置与 mach-lookup entitlements，并重新验证。
- 隔离安装夹具采用临时 ad-hoc 应用和回环 HTTP；它验证真实标准 UI 与 Installer，但不能替代线上 HTTPS、Developer ID、公证、安装权限和只读挂载场景的最终发布验收。旧版未集成 Sparkle，首次升级仍需安装一次新包。
