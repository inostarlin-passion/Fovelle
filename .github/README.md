# CI 环境与维护

主入口是 `workflows/build.yml`：先调用 `test.yml` 在 Ubuntu 24.04 ARM64 检查格式，再在一个 macOS 26 ARM64 job 中完成 clang-tidy、构建、测试和开发包验证。`test.yml` 只接受 reusable/manual 调用，不再对同一个 push/PR 重复构建和测试。常规 push 的 macOS job 从四个减少到一个；PR 另外运行独立 SDK 15 兼容通道。

| 通道 | Runner | Xcode / SDK | 用途 |
|---|---|---|---|
| 主 CI | macos-26 | 26.6 / 26.5 | 新 SDK 条件编译路径、构建、静态分析、完整默认测试、开发包 |
| 兼容与发布 | macos-15 | 16.4 / 15.5 | 保持现有 SDK 15 发布合同和最低 macOS 15；发布为 x86_64+arm64 Universal |
| 格式 | ubuntu-24.04-arm | 不需要 Xcode 或 Qt | 同一固定版本 clang-format，读取 Git 跟踪文件且不改写源码 |

`ci-environment.json` 是版本验收基线。Python 3.14.7、Qt 6.11.2、CMake 4.4.2、Ninja 1.13.2、clang-format/clang-tidy 23.1.3 均显式固定。aqtinstall 3.3.0 和 py7zr 1.1.0 也固定；Qt action 禁用自身的 Python 自动选择，避免覆盖已固定的解释器。`DEVELOPER_DIR` 指向具体 Xcode 路径，CMake 使用该 Xcode 的 SDK；SDK 版本和最低部署版本是不同维度。

`verify_environment.py` 在配置/构建前检查实际工具、OS/架构及 SDK，写出 JSON 诊断；不允许悄悄回退到另一版本。版本升级应同时更新 JSON、工作流输入和 requirements，并通过策略测试。操作系统镜像补丁和 Homebrew Ghostscript 仍由上游维护，因此这不是完全封闭、按镜像摘要复现的构建。正式发布继续使用已有校验和固定的 Ghostscript 源码及 Universal/minimum-OS 校验。

Qt 6.11 的官方支持从 Xcode 15/macOS14 SDK 起，以上两条 Apple 工具链在支持范围内。主 CI 用 SDK26 编译新接口，SDK15 发布通道是项目已有的兼容/行为合同；此项目不通过 App Store 发布，不把它描述为满足 App Store 最新 SDK 提交规则。最低系统实际运行由 macOS15 ARM64 兼容通道验证。全部 CI 执行机使用 ARM64；普通构建显式指定 arm64，正式 Universal 包仍在 ARM64 主机交叉编译，以保留现有产物兼容策略。

第三方及官方 Actions 固定到完整提交 SHA，旁边标注版本系列；Dependabot 每周提出升级。CI 默认 token 为 contents:read，checkout 不持久保存凭据；只有发布 job 获得 contents:write。PR 不执行签名、公证或发布。Qt/pip 使用依赖键缓存，构建目录不跨运行缓存，避免 SDK 和生成头文件污染。

新提交会取消同一分支/PR 的旧 CI，释放容量；发布不会被新提交中断。这种主动取消与“未取得 runner 因容量不足被取消”不同。ARM64 云池仍可能因容量不足排队；通过去重和取消旧任务减轻压力，不自动回退 Intel，也不指向未配置的自托管 runner。没有重新添加 CI job、测试 step、CTest 或 Python 总执行时限；GitHub/QtTest 自身限制以及用例内部性能/功能阈值仍然存在。

本地验证：

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r tests/requirements.txt
.venv/bin/python tests/ci_runner_policy_tests.py
```

格式检查需要 `.github/requirements-format.txt` 中的固定工具：`python .github/scripts/check_format.py`。构建分析使用编译器数据库和 `CMAKE_CXX_CLANG_TIDY`，不再另建一套 clang-tidy job。修改不能以跳过格式错误、测试失败、HDR fixture 或产物验证来换取绿灯。
