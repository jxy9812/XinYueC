# Qt 6.8 正则表达式移植可行性分析

**分析日期：** 2026-07-27  
**目标：** 在 XinYueC 中对齐 Qt 6.8 的 `QRegularExpression` 核心行为，并明确第三方依赖与跨平台边界。

## 结论

移植可行性为**高**，但不建议直接复制 Qt 的 `qregularexpression.cpp`。

Qt 6.8 的正则表达式由两层组成：

1. Qt 自己的 C++ API、隐式共享、匹配结果和迭代器封装。
2. PCRE2 正则引擎，负责编译、UTF-16 匹配、捕获组、部分匹配和可选 JIT。

其中 PCRE2 是纯 C 库，而 XinYueC 目前是 C99 工程；XinYueC 的 `XString` 内部也以 UTF-16 `uint16_t` 存储，因此底层引擎的适配条件很好。需要重写的是 Qt C++ 封装层，而不是重写正则引擎。

建议的目标分级：

| 目标 | 可行性 | 判断 |
| --- | --- | --- |
| Windows/POSIX 上提供正则匹配能力 | 高 | PCRE2 直接支持，XinYueC 已有 CMake、Windows/POSIX 后端边界 |
| 对齐 Qt 6.8 的 `QRegularExpression` 核心行为 | 中高 | 需要逐项实现匹配、捕获、迭代器、错误和编码语义 |
| 复刻 Qt C++ ABI/API | 不适用 | XinYueC 是 C99，不应引入 Qt 的 C++ ABI |
| 同时支持 Qt 5 `QRegExp` | 中 | 是独立兼容层，语义和 API 不能简单等同于 Qt 6.8 正则 |
| 所有平台都启用相同 JIT | 低 | JIT 受架构、可执行内存和平台策略影响；功能不应依赖 JIT |

## 本机找到的源码库

本机未发现另一个独立的 Qt 正则源码仓库；完整源码位于：

- Qt 正则公开头文件：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/text/qregularexpression.h`
- Qt 正则实现：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/text/qregularexpression.cpp`
- Qt 正则测试：`/home/xinyue/Qt/6.8.3/Src/qtbase/tests/auto/corelib/text/qregularexpression/tst_qregularexpression.cpp`
- Qt 内置 PCRE2：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/3rdparty/pcre2`
- Qt 5 兼容层：`/home/xinyue/Qt/6.8.3/Src/qt5compat/src/core5/text/qregexp.{h,cpp}`

源码规模可以作为移植范围的参考：

- `qregularexpression.h`：402 行
- `qregularexpression.cpp`：3084 行
- Qt 核心正则测试：2587 行，覆盖默认构造、移动、选项、普通/部分/全局匹配、捕获组、线程安全、通配符和视图生命周期
- Qt 5 `qregexp.cpp`：4873 行，属于额外的旧 API 兼容工作
- Qt 内置 PCRE2：31 个核心 `.c` 文件，另含 SLJIT JIT 源码和配置/许可证文件

安装树中也存在：

- `/home/xinyue/Qt/6.8.3/gcc_64/include/QtCore/QRegularExpression`
- `/home/xinyue/Qt/6.8.3/gcc_64/include/QtCore/QRegularExpressionMatch`
- `/home/xinyue/Qt/6.8.3/gcc_64/include/QtCore/QRegularExpressionMatchIterator`
- `/home/xinyue/Qt/6.8.3/gcc_64/include/QtCore5Compat/QRegExp`

## Qt 6.8 是否依赖第三方库

### 正则核心依赖 PCRE2

Qt 6.8 的 `qregularexpression.cpp` 在源码第 22-24 行设置：

```c
#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>
```

Qt 的 CMake 在 `qtbase/src/corelib/CMakeLists.txt` 第 897-902 行把正则特性链接到 `WrapPCRE2::WrapPCRE2`。这个包装目标可以选择：

- 系统 PCRE2；或
- Qt 内置的 `BundledPcre2`；或
- 关闭正则表达式特性。

Qt 内置 PCRE2 的配置在 `qtbase/src/3rdparty/pcre2/CMakeLists.txt`：

- 静态库构建；
- `PCRE2_CODE_UNIT_WIDTH=16`；
- 包含 Unicode 表；
- 默认包含 JIT 源码，但部分平台显式关闭 JIT。

本机还找到系统运行时库：`/lib/x86_64-linux-gnu/libpcre2-16.so.0`。当前检索没有找到对应的 `/usr/include/pcre2.h` 和 `pkg-config` 开发配置，因此不建议把本机系统库作为 XinYueC 的唯一构建依赖。

### 附加依赖边界

`QRegularExpression` 本身不依赖 ICU 才能工作。Qt 的正则核心依赖是 PCRE2；Unicode 属性由 PCRE2 的 Unicode 数据支持。`QRegularExpressionValidator` 属于 QtGui 上层功能，额外依赖 Qt 的 `QValidator`/QObject 体系，不是正则引擎本身的必需依赖。

Qt 5 的 `QRegExp` 位于 `QtCore5Compat`，是另一套旧 API 和行为实现，不是 Qt 6 `QRegularExpression` 的简单别名。

## Qt 6.8 的跨平台性

### 功能层面跨平台

正则的编译和匹配行为主要由 PCRE2 决定，Qt 统一使用 PCRE2 的 16 位接口并在编译时开启 UTF 模式。因此以下功能可以作为跨平台公共行为：

- Perl/PCRE 风格模式语法；
- UTF-16 输入；
- 大小写、点号匹配换行、多行、扩展语法、反转贪婪、禁止捕获、Unicode 属性选项；
- 普通匹配、部分匹配、全局匹配；
- 捕获组、命名捕获组和 UTF-16 code unit 偏移；
- 非法模式的错误码、错误文本和错误偏移；
- 通配符转正则、正则转义和锚定模式。

### 性能层面不是完全一致

Qt 6.8 会尝试使用 PCRE2 JIT，但源码明确保留了平台条件：

- Debug 构建默认不启用 JIT；
- macOS 在 Rosetta 下不启用 JIT；
- Qt CMake 对 QNX、UIKIT、部分 Windows ARM/ARM64 配置关闭 JIT；
- JIT 栈不足时，Qt 会创建线程局部 JIT 栈并重试；
- JIT 失败不能改变匹配结果，只应退回解释执行。

因此，XinYueC 的跨平台策略应定义为：

> 匹配结果、捕获偏移、错误语义跨平台一致；JIT 是否启用只影响性能，不作为功能验收条件。

## 与 XinYueC 的适配条件

### 已有的有利条件

- 工程使用 C99，PCRE2 核心源码正是 C；
- `XChar` 是 `uint16_t`，`XString` 内部以 UTF-16 code unit 存储；
- `XString_utf16()` 可以直接返回内部 UTF-16 数据指针，适合作为 `pcre2_match_16()` 输入；
- `XStringView` 已经是指针加长度的非拥有型 UTF-16 视图，可以对应 Qt 的 `QStringView`；
- `XSharedData` 已提供原子引用计数和带析构回调的释放接口，适合保存编译后的 `pcre2_code_16`；
- `XVector` 可以保存捕获偏移数组或命名捕获索引；
- CMake 已经通过 `Library/*/CMakeLists.txt` 集成 zlib、FatFs、lwIP 和 mbedTLS 静态库，PCRE2 可以沿用这个边界。

### 不能直接复制 Qt 源码的原因

Qt 的 `qregularexpression.cpp` 依赖：

- `QString`、`QStringView`、`QAnyStringView`、`QStringList`；
- `QSharedData` 和 `QExplicitlySharedDataPointer`；
- `QMutex`、`QThreadStorage`、`std::unique_ptr`；
- `QVariant`、`QDataStream`、`QDebug`、`QCoreApplication`；
- Qt 的命名空间、导出宏、元类型和 C++ 移动/拷贝语义。

这些都不是 XinYueC 的 C API。可复用的是行为边界和 PCRE2 调用方式，封装层需要按 XinYueC 的生命周期、内存和命名规则重写。

## 推荐的 XinYueC 设计

建议模块名称为 `XRegularExpression`，采用不依赖 XClass 虚表的普通 C 不透明对象；正则对象本身不需要继承 GUI 或事件对象。

### 核心对象

```text
XRegularExpression
  -> 共享私有数据
     - XString pattern
     - pattern options
     - pcre2_code_16 *compiledPattern
     - error code / error offset
     - capture count
     - named capture metadata
     - dirty / compiled state

XRegularExpressionMatch
  -> 正则对象共享引用
  -> subject 所有权或 subject view 生命周期标记
  -> (start, end) 捕获偏移数组
  -> valid / hasMatch / hasPartialMatch

XRegularExpressionMatchIterator
  -> 当前匹配
  -> 下一个匹配所需的 offset
  -> 正则对象和匹配参数共享引用
```

### 生命周期建议

- `XRegularExpression_create()` 创建空正则；
- `XRegularExpression_create_utf8()`、`XRegularExpression_create_string()` 创建模式；
- `XRegularExpression_copy()` 共享编译状态或共享私有数据；
- `XRegularExpression_delete()` 释放引用，最后一个引用释放 `pcre2_code_16`；
- `XRegularExpression_match()` 对 `XString` 输入时持有字符串副本/共享引用，保证返回结果不会悬空；
- `XRegularExpression_match_view()` 对 `XStringView` 输入时只保存非拥有视图，明确要求调用者保证源数据生命周期；
- 空捕获组和没有匹配的捕获组必须区分：前者是有效的长度 0，后者返回 `start=-1`、`end=-1`；
- 空 subject 需要给 PCRE2 传入有效的 dummy UTF-16 code unit，因为 PCRE2 不接受空长度下的 NULL subject 指针。

特别注意：`XString_create_with_length_utf16()` 当前对长度 0 返回 NULL，因此生成空捕获结果时应使用 `XString_create()` 或先创建空字符串再赋值，不能直接把零长度传给该函数。

### 第一阶段建议 API

优先实现以下最小闭环：

- 模式创建、拷贝、移动、删除；
- `pattern()`、`setPattern()`、`patternOptions()`、`setPatternOptions()`；
- `isValid()`、`errorString()`、`patternErrorOffset()`；
- `captureCount()`；
- 普通 `match()`；
- `hasMatch()`、`hasPartialMatch()`、`isValid()`；
- `capturedStart()`、`capturedLength()`、`capturedEnd()`、`captured()`；
- `globalMatch()` 迭代器；
- `escape()`、`anchoredPattern()`、`wildcardToRegularExpression()`。

第二阶段再增加命名捕获、部分匹配完整语义、线程安全、JIT 控制和与 `XString` 的正则替换/分割接口。Qt 的替换和分割主要是 `QString`/`QStringView` 对 `QRegularExpression` 的扩展，不应误认为正则类本身必须一次实现所有字符串算法。

## 第三方库集成方案

### 推荐方案：内置 PCRE2 10.45

复制 Qt 当前使用的 PCRE2 10.45 源码及其许可证/归属文件到 `Library/pcre2`，但不复制 Qt 的 C++ 正则封装。新增独立静态目标，例如 `pcre2_xin`：

1. 仅编译 16-bit API 所需的 PCRE2 核心源文件；
2. 保留 Unicode 表；
3. 第一阶段可定义 `PCRE2_DISABLE_JIT`，先保证 Windows/POSIX 功能一致；
4. 所有静态库启用 `POSITION_INDEPENDENT_CODE ON`，以便链接 XinYueC 动态库；
5. 将目标同时链接到 `XinYueCS`、`XinYueC` 以及对应测试程序；
6. 后续按平台逐个开启 JIT，并保留解释执行回退。

### 不推荐方案：直接依赖系统 PCRE2

系统 PCRE2 的优点是减少仓库体积，缺点是：

- 版本、Unicode 表、JIT 配置和编译选项随发行版变化；
- Windows、嵌入式系统和交叉编译环境未必提供 16-bit 开发包；
- 难以复现 Qt 6.8 的行为测试结果。

如果目标是“对齐 Qt 6.8”，应以内置 PCRE2 为默认，系统 PCRE2 作为可选构建选项。

## 许可证与发布注意事项

Qt 正则封装源文件头部声明为 Qt Commercial 或 LGPL-3.0-only/GPL-2.0-only/GPL-3.0-only 组合许可。PCRE2 自身为 BSD-3-Clause with PCRE2 binary-like packages exception，SLJIT 部分为 BSD-2-Clause。

建议采用以下边界：

- 不复制 Qt 的 C++ 封装实现；
- 只集成 PCRE2，并完整保留其版权、许可证和归属信息；
- 用 Qt 测试数据和公开文档建立 XinYueC 自己的行为测试；
- 在工程许可证/发布包中增加 PCRE2 NOTICE 或 THIRD_PARTY_LICENSES 文件；
- 如果后续复制 Qt 源码片段，必须单独做 LGPL/GPL/商业许可审查，不能因为 PCRE2 是 BSD 就认为 Qt 封装也可任意复制。

## 验收矩阵

### 功能一致性

- ASCII、中文、补充平面字符和非法 UTF-16；
- `\w`、`\d`、Unicode 属性选项；
- 大小写、dotall、multiline、extended、ungreedy、no-capture；
- 普通匹配、从 offset 匹配、锚定匹配；
- partial soft / partial hard；
- 全局匹配中的空匹配、CRLF、代理对推进；
- 未参与匹配的捕获组、空捕获组、命名捕获组；
- 无效模式的有效状态、错误文本和错误偏移；
- wildcard 的路径模式、非路径模式和锚定模式。

### 工程证据

- Linux CMake 全量编译；
- Windows 编译和运行；
- 静态库、动态库两种链接方式；
- AddressSanitizer/UndefinedBehaviorSanitizer；
- 多线程共享同一个已编译正则对象；
- JIT 开启和关闭两组结果一致；
- PCRE2 解释执行回退；
- 空输入、超大 subject、超多捕获组和内存分配失败路径；
- Qt 6.8 测试数据的逐项迁移结果，而不是只验证几个常用模式。

## 最终建议

可以移植，且应按“PCRE2 内置库 + XinYueC C API 封装”实施。

第一版目标建议锁定为：**Windows/POSIX、UTF-16、Qt 6.8 核心匹配/捕获/全局迭代/错误语义，JIT 默认可关闭**。在核心行为测试稳定后，再扩展正则替换、分割、验证器和 Qt 5 `QRegExp` 兼容层。

这样可以实现跨平台功能一致，同时把 PCRE2 版本和 Unicode 数据固定下来，避免系统库差异破坏 Qt 6.8 对齐结果。
