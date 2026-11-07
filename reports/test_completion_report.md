# About 模态与更新进度修复测试完成报告

2026-11-07 在 macOS 27.0.1（Apple Silicon）、Qt 6.12.0、Sparkle 2.10.0 上执行。五条原子验收标准的十个静态和动态用例全部通过。

## 验收结果

| 标准 | 静态 | 动态 | 实际观察 |
|---|---|---|---|
| A01 About 保持应用模态 | S01 通过 | D01 通过 | Qt 和 AppKit 均将 About 识别为当前模态窗口。 |
| A02 About 可见时设置打不开 | S02 通过 | D02 通过 | 原生设置快捷键和应用设置入口均未打开设置。关闭 About 后，原生快捷键打开设置。 |
| A03 Sparkle 进度框在 About 上方 | S03 通过 | D03 通过 | 两窗重叠，WindowServer 中进度框在前，原生父窗口为 About。About 持续为模态窗口。 |
| A04 取消可操作且可重试 | S04 通过 | D04 通过 | 两轮 AppKit 鼠标按下及释放事件均触发原生取消。进度关闭后，同一个 About 可重新检查。 |
| A05 关闭 About 后可继续操作进度 | S05 通过 | D05 通过 | 在应用继续运行的条件下，About 销毁后进度框保持可见，原生父窗口清空，取消仍可用。 |

## 交叉验证与回归

- 上轮非模态实现在新增测试中复现了设置窗口可以打开。Qt 模态状态及 Sparkle 子窗口关系也未满足新断言。
- 独立 AppKit 探针显示，提升普通进度窗口的层级时，鼠标点击未到达其按钮。设置 AppKit 父子关系时，点击到达按钮，About 仍是模态窗口。探针还确认父窗口退场会隐藏附属窗口，而子窗口自行关闭会清理父子关系。
- 应用和测试目标均构建成功。五项原生专项连续运行五次，每次全部通过。完整本地 CTest 的 7 组通过。既有独立 Sparkle 套件的 17 项通过，包含静态检查及取消、签名拒绝、安装和重启等原生场景。
- 固定版本 clang-format 检查和 `git diff --check` 均通过。

## 复跑与证据

构建启用 `BUILD_TESTS=ON` 和 `FOVELLE_ENABLE_UPDATE_INSTALL_TESTS=ON`，原生测试需要已登录的 macOS 图形会话。

```sh
python3 tests/update_acceptance.py --static-only
ctest --test-dir build-about-update -R '^FovelleAboutUpdate$' --repeat until-fail:5 --output-on-failure
ctest --test-dir build-about-update --output-on-failure
```

本地构建目录的验证记录为 `modal-red-static.log`、`modal-red-native.log`、`modal-window-probe.log`、`modal-static.log`、`modal-native.log`、`modal-repeat.log` 和 `modal-full-ctest.log`。
