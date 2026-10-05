# Fovelle 更新功能测试完成报告

执行日期：2026-10-03（Asia/Shanghai）。环境：macOS 27.0.1、arm64、Qt 6.11.2、Sparkle 2.10.0。项目版本：1.2.7；安装夹具：1.0.0 → 2.0.0。

## 结论

代码实现及本地验收通过：手动检查使用标准进度框；标准更新器完成应用内下载、验证、实际替换与重启。发布脚本生成的 appcast 签名经真实 `sign_update --verify` 验证，并拒绝与应用公钥不匹配的 seed。

生产发布未执行。本地开发包没有生产更新地址/公钥，手动检查会提示配置缺失。线上启用需要 repository variable `SPARKLE_PUBLIC_ED_KEY`、secret `SPARKLE_PRIVATE_ED_KEY`、原有 Apple 发布 secrets，以及首次发布包含 appcast 的新版本。该外部配置与线上验证未被计为已通过。

## 实际执行

| 验证 | 实际结果 | 证据 |
| --- | --- | --- |
| CMake 全部目标构建 | 通过 | `evidence/update/cmake-build.log` |
| qmake 应用构建与框架/许可证嵌入 | 通过 | `evidence/update/bundle-checks.json` |
| Python 更新验收 | 13/13 通过，24.776 秒（6 静态 + 7 动态） | `evidence/update/update_acceptance.log` |
| CTest 更新与原生弹窗回归 | 4/4 test entries 通过 | `evidence/update/ctest.log` |
| Qt FeatureTests 整组 | 输出 28 passed，0 failed，0 skipped | `evidence/update/feature-tests.log` |
| appcast 生成、签名验证、错误 seed 拒绝及安装复验 | 通过 | `evidence/update/appcast-signature-verification.log`，最终更新验收日志 |
| 生产 Sparkle rpath | 包内 `@executable_path/../Frameworks` | `evidence/update/rpaths.txt` |
| shell 语法与 git diff 空白检查 | 通过 | 已执行 `bash -n` 与 `git diff --check` |

## 原子验收追踪

| 标准 | 静态证据 | 动态结果 |
| --- | --- | --- |
| AC1 检查进度 | 两个 Qt 入口接标准控制器；关于窗口非模态 | 慢速 feed 响应前已观察到原生进度组件 |
| AC2 取消恢复 | KVO 状态接线与旧 Qt 回调删除 | 检查与下载取消后可用性恢复，应用摘要不变；重复手动检查只发出一次 feed 请求 |
| AC3 版本与错误 | 标准驱动处理结果；没有第二套 Qt 结果提示 | 同版本不下载；XML 错误 1000、HTTP 503 错误 2001 均显示结果提示并恢复 |
| AC4 下载安装重启 | 没有浏览器下载路径、没有手写替换器 | DOWNLOAD_FINISHED → INSTALL_STARTED → RELAUNCHED_NEW_VERSION，旧路径包版本实变 2.0.0 |
| AC5 篡改拒绝 | 公钥、归档验证配置与固定依赖 | 签名不匹配错误 4005/底层 3002；无安装、无重启、旧包摘要不变 |
| AC6 配置校验 | 发布参数失败门禁 | 生产校验函数覆盖 HTTPS/空地址/HTTP/认证地址/短密钥/非规范 base64；无配置更新器安全初始化/销毁 |
| AC7 打包发布 | ditto、包内 rpath、深度优先签名、发布先验证后生成 feed | 两种构建均嵌入框架/许可证；真实脚本生成 feed，URL/版本/签名验证通过；错误 seed 被拒绝 |
| AC8 频率设置 | Sparkle 调度器接线、禁用开关、避免重复背景检查 | 实际生产映射函数返回 0/86400/604800/2592000 秒；FeatureTests 回归通过 |

16 条用例说明（每条原子标准各 1 组静态与动态）全部有六项要素和测试代码映射；允许多个标准复用同一个端到端测试。元数据完整性与映射存在性本身也由测试检查。

## 失败迭代与修正

初次隔离驱动只识别错误框的 OK 按钮，导致错误/签名失败场景超时。通过原生进程采样和 modal run-loop 中的按钮清单，确认实际标题是 Cancel Update；驱动补齐该按钮并在模态模式运行计时器，最终异常场景全部通过。另有早期断言把 Sparkle 的 didExtractUpdate 回调当作实际成功解压，改为明确的签名错误、安装未发生及旧包未变判据；SDK 错误详情确认验证在 unarchiving 前失败。

qmake 本机编译暴露出原有 VERSION 文件遮蔽 `<version>`、宏引号转义、缺失可选 qtbase 翻译的问题，修复后构建通过。qmake 有非阻断的 SDK 版本提示与重复 rpath 链接提示；没有将其视为编译错误或公证成功。

## 验证边界

本次动态安装采用独立的 Cocoa 夹具、真实 Sparkle 标准 UI/Installer、临时 ad-hoc 签名与回环 HTTP。Qt 包装层以真实生产校验/生命周期/设置测试和源接线验证覆盖；没有声称在已签名线上 Fovelle 中完成了完整 UI 端到端测试。生产包装层仍强制 HTTPS。

未执行 Apple Developer ID 生产签名、公证、GitHub 远端发布或线上 HTTPS 更新；未覆盖未来 Sandbox、只读挂载与管理员权限安装路径。生产启用后仍需旧版本 → 新的公证版本端到端发布验证。测试 seed 不写入生产仓库或登录钥匙串；测试应用、defaults、缓存及遗留夹具已清理。
