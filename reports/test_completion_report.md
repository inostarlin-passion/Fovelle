# GitHub Actions 的 About 更新检查测试完成报告

2026-11-08 验证。失败运行 `42475098087` 的格式和构建均成功，`FovelleAboutUpdate` 是唯一失败的 CTest 组。前一次运行的同一组曾在取消用例超时。[崩溃运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/42475098087) · [取消超时运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/42365504806)

| 原子标准 | 静态 | 动态 |
|---|---|---|
| A01 窗口注销 | S01 通过。关闭和析构均移除活动窗口及三类菜单注册。 | D01 三行通过。显示后直接析构、未显示直接析构、正常关闭再析构均恢复应用级菜单和动作列表。旧版的两行直接析构均因多出一个最近文件菜单条目失败，正常关闭行通过。 |
| A02 模态 About 内取消并再次检查 | S02 通过。CI 运行原生套件，每次点击前等待新连接及 WindowServer 排序。 | D02 通过。同一个 About 内完成两轮真实 AppKit 鼠标取消，更新器每轮恢复空闲，Qt 和 AppKit 都维持 About 的模态状态。 |
| A03 关闭 About 后仍可取消 | S03 通过。解除原生父子关系先于 About 退场。 | D03 通过。原进度窗口继续显示，可接收原生取消事件，更新器恢复空闲。 |

## 本机回归

- macOS 27.0.1、Qt 6.12.0 上，12 项更新静态检查全部通过。固定版本 clang-format 23.1.3、CI 配置测试及 `git diff --check` 通过。
- `FovelleAboutUpdate` 完整套件包含 8 条用例或数据行，连续运行 20 轮全部通过。
- 使用 `FOVELLE_TEST_PROFILE=ci` 重跑完整 CTest，7 组全部通过，耗时 117.76 秒。首轮的 About 与安装组均通过，`FovelleFastTests` 内的 HDR 全屏 `enter-size-hidden` 结束态断言失败。该行在完整重跑中通过。

## GitHub Actions

验证分支为 `codex/about-update-ci`，提交为 `6dbd59ecbd35f7dd57706e5b8a24533e7805b5a4`。在 GitHub 托管的 macOS 26 ARM64、Qt 6.11.2 环境中，格式和构建任务均成功。CTest 7 组全部通过，耗时 183.66 秒，其中 `FovelleAboutUpdate` 用时 3.94 秒。Qt 应用包校验、压缩、解包复验及产物上传均成功。[验证运行](https://github.com/inostarlin-passion/Fovelle/actions/runs/42476657735) · [应用包产物](https://github.com/inostarlin-passion/Fovelle/actions/runs/42476657735/artifacts/13615723730)

## 复跑

```sh
python3 tests/update_acceptance.py --static-only
ctest --test-dir build-about-update -R '^FovelleAboutUpdate$' --repeat until-fail:20 --output-on-failure
FOVELLE_TEST_PROFILE=ci ctest --test-dir build-about-update --output-on-failure
```

本机日志在 `build-about-update`。旧版回归见 `ci-about-red-*.log`，修复后的静态、原生和完整回归见 `ci-about-static.log`、`ci-about-focused.log`、`ci-about-repeat.log`、`ci-about-full.log` 和 `ci-about-full-recheck.log`。

远端构建步骤及 CTest 明细见 `ci-about-actions-build.log`。
