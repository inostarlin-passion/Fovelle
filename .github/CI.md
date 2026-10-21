# CI 环境与维护

主入口是 `workflows/build.yml`：先调用 `test.yml` 在 Ubuntu 24.04 ARM64 检查格式，再在一个 macOS 15 ARM64 job 中完成 clang-tidy、构建、测试和开发包验证。`test.yml` 只接受 reusable/manual 调用，不再对同一个 push/PR 重复构建和测试。常规 push 的 macOS job 从四个减少到一个；PR 另外运行独立 SDK 15 兼容通道。

| 通道 | Runner | Xcode / SDK | 用途 |
|---|---|---|---|
| 主 CI | macos-15 | 26.2 / 26.2 | 新 SDK 条件编译路径、构建、静态分析、完整默认测试、开发包 |
| 兼容与发布 | macos-15 | 16.4 / 15.5 | 保持现有 SDK 15 发布合同和最低 macOS 15；发布为 x86_64+arm64 Universal |
| 格式 | ubuntu-24.04-arm | 不需要 Xcode 或 Qt | 同一固定版本 clang-format，读取 Git 跟踪文件且不改写源码 |

`ci-environment.json` 是版本验收基线。Python 3.14.7、Qt 6.11.2、CMake 4.4.2、Ninja 1.13.2、clang-format/clang-tidy 23.1.3 均显式固定。aqtinstall 3.3.0 和 py7zr 1.1.0 也固定；Qt action 禁用自身的 Python 自动选择，避免覆盖已固定的解释器。`DEVELOPER_DIR` 指向具体 Xcode 路径，CMake 使用该 Xcode 的 SDK；SDK 版本和最低部署版本是不同维度。

`verify_environment.py` 在配置/构建前检查实际工具、OS/架构及 SDK，写出 JSON 诊断；不允许悄悄回退到另一版本。版本升级应同时更新 JSON、工作流输入和 requirements，并通过策略测试。操作系统镜像补丁和 Homebrew Ghostscript 仍由上游维护，因此这不是完全封闭、按镜像摘要复现的构建。正式发布继续使用已有校验和固定的 Ghostscript 源码及 Universal/minimum-OS 校验。

Qt 6.11 的官方支持从 Xcode 15/macOS14 SDK 起，以上两条 Apple 工具链在支持范围内。主 CI 用 SDK26 编译新接口，SDK15 发布通道是项目已有的兼容/行为合同；此项目不通过 App Store 发布，不把它描述为满足 App Store 最新 SDK 提交规则。最低系统实际运行由 macOS15 ARM64 兼容通道验证。全部 CI 执行机使用 ARM64；普通构建显式指定 arm64，正式 Universal 包仍在 ARM64 主机交叉编译，以保留现有产物兼容策略。

第三方及官方 Actions 固定到完整提交 SHA，旁边标注版本系列；Dependabot 每周提出升级。CI 默认 token 为 contents:read，checkout 不持久保存凭据；只有发布 job 获得 contents:write。PR 不执行 Developer ID 签名、公证或发布；兼容包仅使用临时签名。Qt/pip 使用依赖键缓存，构建目录不跨运行缓存，避免 SDK 和生成头文件污染。

新提交会取消同一分支/PR 的旧 CI，释放容量；发布不会被新提交中断。这种主动取消与“未取得 runner 因容量不足被取消”不同。ARM64 云池仍可能因容量不足排队；通过去重和取消旧任务减轻压力，不自动回退 Intel，也不指向未配置的自托管 runner。没有重新添加 CI job、测试 step、CTest 或 Python 总执行时限；GitHub/QtTest 自身限制以及用例内部性能/功能阈值仍然存在。

本地验证：

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r tests/requirements.txt
.venv/bin/python tests/ci_runner_policy_tests.py
```

格式检查需要 `.github/requirements-format.txt` 中的固定工具：`python .github/scripts/check_format.py`。构建分析使用编译器数据库和 `CMAKE_CXX_CLANG_TIDY` / `CMAKE_OBJCXX_CLANG_TIDY`，不再另建一套 clang-tidy job。修改不能以跳过格式错误、测试失败、HDR fixture 或产物验证来换取绿灯。

当前主 CI 采用 macOS15 ARM 主机上的 Xcode26.2/SDK26.2：Apple 支持该组合，GitHub 镜像已安装它。这是在 macos-26 托管池反复未分配机器后的显式路由调整，仍为 ARM64，SDK26 编译路径和所有测试断言保留；没有 Intel 或浮动 latest 回退。主 CI 的 macOS26 专属运行时覆盖不能由 macOS15 替代，需在对应系统单独验证；本机 macOS27 ARM 回归也不冒充 macOS26 hosted 执行。

## 发布门禁与最终产物验收

- Release 通过本地 reusable `build.yml` 依赖同一提交的格式、SDK26 C++/Objective-C++ 静态分析、构建、默认测试和开发包校验。嵌套调用的 CI 并发组有 `ci-` 前缀，且不会取消 Release。
- 只支持正式版 `vMAJOR.MINOR.PATCH`；`validate-release.py` 要求 tag、VERSION、最终 plist 和 appcast 一致，并验证下载 URL、归档长度及签名字段。预发布标签直接拒绝，避免进入稳定更新源。
- `workflow_dispatch` 执行与 tag 发布相同的签名、公证、appcast 生成及验收，只上传 `release-candidate`，不创建 GitHub Release。需要与正式发布相同的仓库变量及 secrets。候选 tag 从当前提交 VERSION 推导；只有 tag push 可发布。
- PR 兼容通道调用同一个 `package-macos-release.sh`（`RELEASE_UNSIGNED=true`）。Qt 部署、固定 Ghostscript 源码构建、逐依赖/逐架构校验、ZIP 解压和实际解码与正式发布共用；Developer ID、公证和 appcast 生成限于 Release。
- `verify-release-artifact.py` 对每个 Mach-O 的每个架构分别执行 otool，验证 Universal、最低运行系统、主程序 SDK 家族、plist 版本及动态依赖，不接受只检查第一个架构的结果。
- 最终 ZIP 解压后，`smoke-release-artifact.py` 清除 Qt/DYLD/GS/Fovelle 环境覆盖，并用系统 PATH 启动实际 Cocoa 应用，在临时设置目录中解码 PNG、SVG、EPS，确认最终包的固定 Ghostscript 版本；结果保存在 `build/test-results/release-artifact/`。
- Release 显式启用 `FOVELLE_ENABLE_UPDATE_INSTALL_TESTS=ON`。该门禁独立于 `FOVELLE_EXTENDED_TESTS`，通过临时测试应用、临时 Ed25519 密钥和本地 HTTP 服务验证更新下载、签名拒绝、安装、替换和重启，不使用正式更新密钥或现有应用。证据保存在 `build/test-results/update-install/`。
- clang-tidy 已启用检查的警告升级为错误。测试中少数用于模拟“Qt 活跃但原生焦点事件缺失”的旧 Qt 接口使用局部废弃告警抑制，保留反例语义；其余诊断仍阻断构建。

相关官方依据：[工作流复用与并发](https://docs.github.com/en/actions/reference/workflows-and-actions/reusing-workflow-configurations)、[CMake 按语言启用 clang-tidy](https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html)、[LLVM 告警升级](https://clang.llvm.org/extra/clang-tidy/)、[Sparkle 版本和更新发布](https://sparkle-project.org/documentation/publishing/)。
