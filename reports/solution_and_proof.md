# 设置页问题的唯一解决方案与正确性证明

日期：2026-08-27  
范围：`QVOptionsDialog` 的 General、Mouse 页面，以及 Mouse 的离散动作 cooldown 设置。

## 1. 结论：唯一方案

在“精益完整性”约束下，唯一采用下面这一种不依赖语言分支的方案：

1. 保留内部设置键 `scrollactioncooldown`，其默认值固定为 `true`；移除 Mouse 页的 `scrollActionCooldownCheckbox`、同步连接和所有对应翻译文本。这样新配置仍默认启用，旧配置中的显式值仍可兼容读取。
2. 在翻译文本已经设置且控件已经 `polish` 之后，收集 General 与 Mouse 两页所有表单标签的自然宽度，定义一个共享宽度

   \[
   L=\max\{w_i\mid i\text{ 是 General 或 Mouse 中的标签}\}.
   \]

   清除上一次布局留下的固定宽度，然后把每一个标签固定为 `L`，设置标签内容为 `AlignRight | AlignTrailing | AlignVCenter`，把每个表单设置为 `AlignLeft | AlignTop`、`DontWrapRows`。值项保持 `AlignLeft | AlignTop`。

这是一个单一的运行时算法：不插入空格、不为中文/日文/西文写特殊分支、不为每个表单选择不同宽度。代码对应于 `src/qvoptionsdialog.cpp` 的 `formLabelColumnWidth()`、`clearFormLabelColumnWidths()`、`alignFormLayouts()` 和 `updateNaturalPageSizes()`。

## 2. 可证前提与边界

以下前提全部显式列出；数学结论不超出这些前提。

- **P1（语言集合）**：目标集合为 `en`、`es`、`ja`、`zh_Hans`、`zh_Hant`。这些语言在本应用的设置页中按从左到右书写。
- **P2（有限性）**：每次显示页面时，可见的冒号结尾标签集合是有限集；每个标签在当前字体、主题、DPI 和翻译下有有限的 `sizeHint().width()`。
- **P3（测量时刻）**：翻译、主题和字体已生效，控件已 `ensurePolished()`；发生会改变尺寸的样式、字体或语言变化后重新测量。
- **P4（共同原点）**：同一页面的表单使用相同的页面内容原点；代码强制表单 `AlignLeft | AlignTop`、不换行，General 的组直接位于同一个纵向布局，Mouse 的各组使用相同的原生组内容规则。
- **P5（Qt 表单语义）**：`QFormLayout` 将标签放在 `LabelRole`、值放在 `FieldRole`；标签的水平 alignment 决定标签内容在其矩形中的位置，表单 alignment 决定额外空间相对表单原点的位置。
- **P6（默认值语义）**：SettingsManager 在没有持久化值时返回设置项的 `defaultValue`；`QVGraphicsView` 从 `scrollactioncooldown` 读取有效值。
- **P7（单位）**：布局几何使用 Qt logical pixel；宽度和几何坐标在一次布局计算中是整数。

P4 是实现/运行时验收前提，不是把截图中的某个像素当成规范。跨语言测试会直接检查它产生的几何不变量。

## 3. 多跳检索与确证信息

检索从平台设置页布局原则下钻到 Qt 表单角色、布局 alignment、翻译时机和 Qt 实现源码；外部资料只提供通用机制，应用结论由 P1–P7 与本地代码/测试共同推出。

| 跳数 | 一手来源与确证信息 | 显式前提 | 推导 |
|---|---|---|---|
| 1 | [Apple Settings HIG](https://developer.apple.com/design/human-interface-guidelines/settings) 将 macOS 设置组织为 pane；[Apple Layout HIG](https://developer.apple.com/design/human-interface-guidelines/layout) 将 alignment 与 spacing 作为组织层级的手段。 | General 与 Mouse 是两个设置 pane，名称和值必须形成稳定视觉网格。 | 应建立页面级共同列，而不是用每种语言的手工空格。 |
| 2 | [Qt QFormLayout 文档](https://doc.qt.io/qt-6.11/qformlayout.html) 确认 `LabelRole`、`FieldRole`、`SpanningRole` 及 `setLabelAlignment()`、`setFormAlignment()`。 | 标签和值由表单的不同角色承载。 | 共享标签宽度放在 `LabelRole`，值项从共同字段列开始，布局原点固定为左上。 |
| 3 | [Qt QLayout 文档](https://doc.qt.io/qt-6/qlayout.html) 说明可对 widget/layout 设置 alignment，布局负责计算子项几何。 | 需要观察最终布局几何，而非只观察字符串或源码常量。 | 测试读取翻译后 QWidget 的映射右边界，生产代码以布局 alignment 建立几何不变量。 |
| 4 | Qt 6.11.1 的 [QFormLayout 源码](https://raw.githubusercontent.com/qt/qtbase/v6.11.1/src/widgets/kernel/qformlayout.cpp) 展示标签列、字段列及 alignment 对几何的影响。 | 不同翻译会改变自然标签宽度，独立表单的 size hint 可能不同。 | 用所有标签自然宽度的最大值作为唯一共享列宽，消除独立 size hint 的漂移。 |
| 5 | Qt 6.11.1 的 [QLayoutItem 源码](https://raw.githubusercontent.com/qt/qtbase/v6.11.1/src/widgets/kernel/qlayoutitem.cpp) 展示 layout item 与 QWidget 几何之间的转换。 | 验收必须在同一页面坐标系比较最终 widget 几何。 | 测试将 label 的右端映射到 page，而不是混用不同父布局的局部坐标。 |
| 6 | [Qt 翻译源码文档](https://doc.qt.io/qt-6/i18n-source-translation.html) 说明翻译器安装会触发语言变化事件，尺寸相关 UI 应在翻译生效后重新布局。 | 标签宽度是当前语言的运行时量。 | `updateNaturalPageSizes()` 在最终文本和 polish 后重算；测试逐语言安装目录并验证。 |

## 4. 标签右对齐的数学证明

### 4.1 定义

固定一个页面 \(P\)，其可见标签集合为

\[
S_P=\{1,2,\ldots,n\}.
\]

标签 \(i\) 在当前语言下的自然宽度为 \(w_i\)。由 P2，集合有限，故

\[
L=\max_{P\in\{G,M\},\ i\in S_P}w_i
\]

存在。实现对 General 和 Mouse 使用同一个 \(L\)，并将每个标签 widget 的外部宽度设置为 \(L\)。设页面中所有表单的共同内容原点横坐标为 \(x_P\)。

### 4.2 引理一：每个标签均不裁切

对任意 \(i\in S_P\)，由最大值定义有

\[
w_i\le L.
\]

标签外部矩形宽度为 \(L\)，因此自然内容宽度不超过外部矩形宽度。由于 P3 保证测量发生在当前翻译、字体和样式生效后，设置固定宽度不会裁切自然标签内容。

### 4.3 引理二：每个页面的标签右边界相等

实现设置 `AlignRight | AlignTrailing`。对从左到右的目标语言，这意味着标签内容贴近其外部矩形右侧；外部矩形的左边界均为 \(x_P\)，宽度均为 \(L\)。故任意 \(i\in S_P\) 的右边界为

\[
r_{P,i}=x_P+L.
\]

因此对任意 \(i,j\in S_P\)，

\[
r_{P,i}=x_P+L=x_P+L=r_{P,j}.
\]

这正是“同一页所有冒号右边界对齐”。`AlignVCenter` 只改变标签内容在固定高度内的纵向位置，不改变上式的横坐标。

### 4.4 引理三：语言切换不会破坏不变量

设语言从 \(\ell_1\) 切换到 \(\ell_2\)，自然宽度从 \(w_i(\ell_1)\) 变为 \(w_i(\ell_2)\)。更新过程先清除旧固定宽度，再计算

\[
L(\ell_2)=\max_{P,i}w_i(\ell_2).
\]

由引理二，更新后

\[
\forall i,j\in S_P:\quad r_{P,i}(\ell_2)=x_P+L(\ell_2)=r_{P,j}(\ell_2).
\]

因此结论不依赖 \(\ell\) 的具体文字长度；英文、中文、日文和西文只会改变新的 \(L\)，不会改变对齐规则。ASCII 冒号和全角冒号都只是测试识别标签的终止符，不参与布局计算。

### 4.5 定理：共享最大自然宽度方案正确

由引理一，所有标签都有足够的外部宽度；由引理二，同一页面所有标签右边界相等；由引理三，翻译变化后仍成立。P4 保证所有表单使用共同原点，P5 保证 alignment 的含义确实作用于 QFormLayout 的标签列。因此在 P1–P5 下，该方案满足 General 与 Mouse 在所有目标语言中的冒号右对齐要求。

## 5. “唯一”的数学说明

在“精益完整性”中，将“不得引入非必要冗余”形式化为：共享标签列必须取满足不裁切的最小宽度。

设一个不裁切且所有标签同列的方案使用统一宽度 \(W\)。由引理一的必要条件，对每个标签都必须有 \(W\ge w_i\)，所以

\[
W\ge \max_i w_i=L.
\]

若 \(W>L\)，则区间 \((L,W]\) 是未被任何自然标签使用的冗余宽度，违反精益约束；若 \(W<L\)，取达到最大值的标签 \(k\)，则 \(w_k=L>W\)，发生裁切。故满足“不裁切、同一右边界、最小宽度”的统一列宽必为

\[
W=L,
\]

且唯一。手工空格、按语言写 `if/else`、按表单各算一个宽度都不能同时满足这个不变量：前两者没有对任意运行时宽度给出统一的几何证明，后一者允许各表单产生不同右边界。于是本任务在给定精益约束下只有上述一个解决方案。

## 6. Cooldown 默认值与移除的证明

令 \(K=\texttt{scrollactioncooldown}\)，令 \(D(K)\) 为 SettingsManager 的默认值。代码中

\[
D(K)=\texttt{true}.
\]

对没有持久化 \(K\) 的新配置，由 P6，

\[
\operatorname{effective}(K)=D(K)=\texttt{true}.
\]

`QVGraphicsView` 继续读取这个有效值，所以新配置仍启用离散动作 cooldown。与此同时，`.ui` 不再创建 `scrollActionCooldownCheckbox`，`syncSettings()` 不再为它建立读写连接，翻译目录不再包含该选项文本。因此用户界面不再暴露该设置，而内部兼容读取路径不被无必要删除。若旧配置显式保存了 `false`，该显式用户选择仍按兼容性原则保留；“默认值为勾选”针对没有显式持久化值的新配置成立。

## 7. 可执行验收对应关系

- `FeatureTests::testSettingsCooldownOptionIsRemovedAndDefaultEnabled` 验证 \(D(K)=true\) 以及旧 checkbox/文本不在运行时控件树中。
- `WindowBehaviorTests::testSettingsColonAlignmentSurvivesTranslations` 对五种语言分别激活 General 和 Mouse，验证每个可见冒号结尾标签的 `AlignRight | AlignTrailing` 与映射右边界相等。
- `tests/settings_quality_pipeline.py` 先执行静态契约，再执行 FeatureTests（单元）、WindowBehaviorTests（集成），最后执行 CTest 的 `FovelleTests`（系统），并将每个原子 case 的 stage 状态、命令、返回码、超时、耗时和输出 SHA-256 写入 JSON。

这份证明只保证稳定的结构和几何不变量；字体、主题、Qt 或系统升级后，具体 logical-pixel 数值可以变化，但只要 P1–P7 仍成立，等式证明不变。
