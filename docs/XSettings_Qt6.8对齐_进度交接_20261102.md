# XSettings（Qt 6.8 QSettings 全量对齐）进度与交接

> 状态：**功能已全部打通，19/19 测试通过；剩 4 处小量线性内存泄漏待修**
> 分支：`codex/xdevice-file-platform`
> HEAD：`45b790c9`（与远端最新提交一致；XSettings 全部工作在未提交的工作区，共 17 项）
> 交接日期：2026-11-02
> 接续环境：Linux（本文档第七节给出迁移、构建与验证步骤）

---

## 一、任务目标与完成度

### 目标
在 XinYueC 中实现对齐 Qt 6.8 `QSettings` 的全量 API、行为语义与继承关系，并编写全面测试接入 Test 菜单、无内存泄漏。

### 完成度

| 项 | 状态 |
|---|---|
| 公开 API 全量对齐（6 构造器 + 全部实例/静态方法） | ✅ 完成 |
| 继承关系对齐（`QSettings : QObject` → `XSettings : XObject`） | ✅ 完成 |
| INI 行为语义逐分支对齐 qsettings.cpp | ✅ 完成 |
| XRect / XSize 接入 XVariant（原缺失） | ✅ 完成 |
| 测试接入 Test 菜单 + `Test xsettings` 自动化 | ✅ 完成 |
| 19 项功能测试 | ✅ 全部通过 |
| 内存零泄漏 | ⚠️ 修复 10504 → 2016 字节，剩 4 处待修 |

---

## 二、新增文件

全部为新增，无历史文件被删除。

| 文件 | 行数 | 职责 |
|---|---|---|
| `Src/XCore/XCode/XSettings/XSettings.h` | 606 | 公开 API（对齐 QSettings 全量接口） |
| `Src/XCore/XCode/XSettings/XSettings.c` | 2592 | 对象层：QConfFile 回退链、惰性节解析、分组/数组栈、原子写盘、自定义格式 |
| `Src/XCore/XCode/XSettings/XSettings_ini.h` | 178 | INI 算法内部接口（仅 XSettings*.c 可见） |
| `Src/XCore/XCode/XSettings/XSettings_ini.c` | 2174 | INI 算法核心：键/值转义、类型前缀、TLV、节解析、文件写出 |
| `Test/XCodeTest/XSettingsTest.h` | 34 | 测试声明 |
| `Test/XCodeTest/XSettingsTest.c` | 1391 | 19 项测试 + 内存平衡断言 |

### 已修改的文件（13 处）

```
Src/CXinYueConfig.h                              新增 XSETTINGS_ON 开关
Src/XCore/XCode/XFile/XDir/XDir.c                 框架根修 ×2
Src/XCore/XContainer/XContainer/XContainer_virtual.c  框架根修
Src/XCore/XContainer/XMap/XMap/XMap.c             工厂契约文档
Src/XCore/XData/XGeometry.c/.h                    XRect/XSize 接入 XVariant
Src/XCore/XData/XVariant/XVariant.c               框架根修
Src/XCore/XData/XVariant/XVariantTypeOps.c/.h     枚举与 ops 表补充 Rect/Size
Test/XCodeTest/XCodeTest.c/.h                     测试菜单注册
Test/XCodeTest/XTestCommand.c                     Test 命令注册
main.c                                            --test xsettings 入口
```

---

## 三、过程中修复的框架缺陷（重点，均为真 bug）

按"遇到框架问题不绕开、直接修到对齐 Qt"的要求，以下都是在框架层修的，不是 XSettings 内部绕过：

| # | 缺陷 | 文件 | 违反的 Qt 语义 |
|---|---|---|---|
| 1 | `XDir_mkpath` 对**已存在**目录返回 false | `XDir.c` | `QDir::mkpath` 已存在应返回 true |
| 2 | `XDir_filePath` 对**绝对路径**仍强制拼接 | `XDir.c` | `QDir::filePath` 绝对路径应原样返回 |
| 3 | 容器拷贝读未初始化的 `m_data` 触发段错误 | `XContainer_virtual.c` | 未初始化目标的拷贝必须先清三元组 |
| 4 | `XVariant` 拷贝未清 `m_type/m_data/m_dataSize` | `XVariant.c` | 同上 |
| 5 | `XMap_create_XVariantMap` 契约不明（值槽 vs 指针槽） | `XMap.c` | 补 @warning 说明调用方必须传**对象地址**而非指针变量地址 |

> 说明：第 5 项确认**框架实现本身正确**（值槽内联存储 + `XClass_*_base` 对象级方法，因为 XString/XVariant 首成员均为 XClass）。原判断为框架 bug 是误判，已回退并在工厂补契约文档。

---

## 四、XSettings 内部的 13 处实现缺陷修复

| # | 缺陷 | 文件 | 症状 |
|---|---|---|---|
| 1 | 组栈用默认 COW 的 `XVector_Create` | `XSettings.c` | 数组下标读回乱码（`ui/407017632/win/`） |
| 2 | 节表（指针槽裸 map）移除槽位不释放条目对象 | `XSettings.c` | 每次解析泄漏 XString + XByteArray |
| 3 | 值解析 move 后未释放源壳 | `XSettings_ini.c` | 每解析一个值泄漏一个 XString 壳 |
| 4 | 同名节覆盖而非合并 | `XSettings_ini.c` | 多个 `[General]` 只剩最后一段 |
| 5 | `[%General]` 未剥 `%` 前缀 | `XSettings_ini.c` | 组名变成 `%General/` |
| 6 | 行扫描注释/空行分支 `goto restart` 未推进 `pos` | `XSettings_ini.c` | **死循环挂起** |
| 7 | 键 `%HH` 高低半字节写反 | `XSettings_ini.c` | 空格写成 `%02` 而非 `%20` |
| 8 | TLV 编码 Map 时重复写类型字节 | `XSettings_ini.c` | 解码错位、内层值变垃圾 |
| 9 | 引号内逗号未被消费 | `XSettings_ini.c` | **死循环** |
| 10 | 引号打开后错误进入 StSkipSpaces | `XSettings_ini.c` | 引号内容被当空白跳过 |
| 11 | 非列表值解析后直接 `XClassDelete` 未入表 | `XSettings_ini.c` | **所有普通值解析为空** |
| 12 | 引号包裹时 `len` 只 +1（应 +2） | `XSettings_ini.c` | 闭引号丢失被后续写入覆盖 |
| 13 | `@Variant/@ByteArray` 载荷被引号包裹 | `XSettings_ini.c` | base64 的 `=` 触发引号，读回损坏 |
| 14 | base64 结果含 NUL 终止符被计入长度 | `XSettings_ini.c` | `@Variant(...)` 载荷尾部多 `\0` |
| 15 | TLV 解码判"未消费完"过严 | `XSettings_ini.c` | 尾部 NUL 导致解码失败返回无效变体 |
| 16 | 读取时只用缓存、从不重读回退链文件 | `XSettings.c` | 先构造的实例读不到后来写入的回退文件 |

---

## 五、测试现状（19/19 功能通过）

### 运行方式
```bash
# Windows
./bin/XinYueC_Static.exe --test xsettings

# Linux（详见第七节）
./bin/XinYueC_Static --test xsettings
```

### 已通过的 19 项
1. 生命周期/拷贝/移动
2. 标量与 INI 数值字符串口径
3. 类型前缀往返（Point/Rect/Size/ByteArray/StringList/DateTime）
4. Map 变体 TLV 往返
5. 分组
6. 数组
7. 键枚举排序去重
8. contains/remove/clear
9. 落盘/重开/外部变更导入
10. INI 转义 roundtrip
11. 注释与坏行（含 FormatError 触发）
12. `[%General]` 与重复键
13. 回退链开关
14. isWritable 与原子写开关
15. 自定义格式注册
16. XRect/XSize 的 XVariant 接入
17. 组织名/应用名派生
18. 键规范化与空键容错
19. 内存平衡（结构断言本身通过，见下节）

---

## 六、剩余工作：4 处线性内存泄漏

### 现状
内存平衡断言仍报 `systemBytes 220040 → 222056`（**+2016 字节**），因此 `--test xsettings` 返回码为 1。

已用"同一用例连跑两遍"的方法区分真泄漏与惰性缓存：

| 用例 | 下标 | 首遍 | 第二遍 | 判定 |
|---|---|---|---|---|
| 生命周期/拷贝/移动 | 0 | +168 | +0 | 一次性缓存，非泄漏 |
| 类型前缀往返 | 2 | +1424 | +1424 | **真泄漏** |
| Map 变体 TLV 往返 | 3 | +136 | +136 | **真泄漏** |
| INI 转义 roundtrip | 9 | +96 | +96 | **真泄漏** |
| 注释与坏行 | 10 | +48 | +48 | **真泄漏** |
| 自定义格式/几何/默认构造 | 14 | +144 | +0 | 一次性缓存，非泄漏 |

### 已确认无泄漏的路径
- 单对象 create → setValue → allKeys → sync → destroy：**net +0**
- `XVariantMap` 单独 insert + destroy：**+0**
- `XDateTime_toVariant` 创建/删除循环：**+0**
- 纯读回已存在文件的重开循环：**+0**（修完节表释放后）

### 排查建议（下次继续的起点）
1. **case 2（1424B，最大）**：约合每键 237 字节。重点看 `xsettingsEncodeTlv` / `xsettingsTlvFromContent` 的容器分支（List/Map 递归）与 `XSettings_setValue` 的深拷贝路径。
2. **case 3（136B）**：TLV Map 编解码分支的中间对象。
3. **case 9（96B）**：`xsettingsEscapedString` / `xsettingsUnescapedStringList` / `xsettingsEscapedKey` 的临时对象在成功与失败分支是否都释放。
4. **case 10（48B）**：`xconfReadFile` 的 FormatError 早退分支、`xsettingsReadIniSections` 的 `ok=false` 分支。

### 排查工具（已内置）
`Test/XCodeTest/XSettingsTest.c` 的 `XSettingsTest_runAll` 里有：
- 逐用例内存增量打印：`[MEM][case N] first +X, again +Y (线性=真泄漏 | 一次性=惰性缓存)`
- `xsettings_test_memoryBalance` 里有 4 段最小复现探针（create/destroy、reopen、只读重开、map insert+destroy）

> **注意**：这些是调试设施，修复完成后应清理（见第八节）。

---

## 七、Linux 侧继续的步骤

### 0. 当前仓库状态（重要）

交接时仓库处于**干净起点 + 未提交工作区**的状态：

| 项 | 值 |
|---|---|
| HEAD | `45b790c9`（= 远端 `origin/codex/xdevice-file-platform` 最新） |
| 未推送提交 | **无**（本地 HEAD 与远端完全一致） |
| 暂存区 | 空 |
| 工作区改动 | **17 项，全部是 XSettings 相关工作**（13 个修改 + 4 个新增） |

> 历史说明：开发过程中曾有一次本地合并提交 `99471866`，已撤回。
> 撤回时远端内容一度混入工作区，随后通过 `git reset 45b790c9` 修正——
> HEAD 现已指向远端最新提交，工作区只保留 XSettings 改动。

### 1. 把这 17 项改动带到 Linux

**方式 A：生成 patch（推荐，可留档）**

Windows 侧（Git Bash）执行：
```bash
cd /d/code/CMake/XinYueC
# 已跟踪文件的修改
git diff > xsettings-tracked.patch
# 新增文件（含 docs），逐个 pack
git add -N Src/XCore/XCode/XSettings Test/XCodeTest/XSettingsTest.c \
           Test/XCodeTest/XSettingsTest.h "docs/XSettings_Qt6.8对齐_进度交接_20261102.md"
git diff > xsettings-all.patch
git reset    # 退回未暂存状态
```
Linux 侧：
```bash
git checkout codex/xdevice-file-platform && git pull
git apply /path/to/xsettings-all.patch
```

**方式 B：直接拷贝（最省事）**

只拷这 17 项路径即可，其余文件一律不要拷（避免把远端内容重复覆盖）：

```bash
# 新增目录/文件
Src/XCore/XCode/XSettings/                       # 整个目录（4 个文件）
Test/XCodeTest/XSettingsTest.c
Test/XCodeTest/XSettingsTest.h
docs/XSettings_Qt6.8对齐_进度交接_20261102.md

# 修改的文件（13 个）
Src/CXinYueConfig.h
Src/XCore/XCode/XFile/XDir/XDir.c
Src/XCore/XContainer/XContainer/XContainer_virtual.c
Src/XCore/XContainer/XMap/XMap/XMap.c
Src/XCore/XData/XGeometry.c
Src/XCore/XData/XGeometry.h
Src/XCore/XData/XVariant/XVariant.c
Src/XCore/XData/XVariant/XVariantTypeOps.c
Src/XCore/XData/XVariant/XVariantTypeOps.h
Test/XCodeTest/XCodeTest.c
Test/XCodeTest/XCodeTest.h
Test/XCodeTest/XTestCommand.c
main.c
```

> **切勿整目录覆盖**：`Drive/`、`HighSpeedFilter/`、`Src/Industry/XVfd/`、
> `Src/XCore/XProtocol/`、`Test/XProtocolTest/` 等目录的当前内容已由远端
> `df7b7dac` / `45b790c9` 提交持有，覆盖回退会丢掉远端新功能。

### 2. 配置与构建
项目使用 CMake + 文件 glob，新增文件后**必须重新 configure**：
```bash
cd XinYueC
cmake -S . -B out/build/linux-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/linux-debug --target XinYueC_Static -j$(nproc)
```

> Windows 侧若用 MSVC 需先加载环境（见 `build_desktop.bat`）；Linux 侧直接用系统的 gcc/clang 即可。

### 3. 运行测试
```bash
./bin/XinYueC_Static --test xsettings
```
预期：19 项 PASS，但最后一行内存平衡 FAIL（+2016 字节，即第六节的待修项）。

### 4. 内存泄漏排查（Linux 优势）
Linux 侧可直接用 ASan，比 Windows 的 cdb + 手工打点高效得多：
```bash
cmake -S . -B out/build/linux-asan \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
cmake --build out/build/linux-asan --target XinYueC_Static -j$(nproc)
ASAN_OPTIONS=detect_leaks=1 ./bin/XinYueC_Static --test xsettings
```
LeakSanitizer 会直接给出泄漏点的调用栈，四个 case 的根因应能一次定位。

### 5. 修复后的验证清单
- [ ] 四个 case 的 `[MEM][case N]` 均为 `+0`
- [ ] 最终 `systemBytes` 前后相等，`--test xsettings` 返回 0
- [ ] 清理全部调试打点（第八节）
- [ ] 全量回归：`./bin/XinYueC_Static --test all`

---

## 八、清理清单（修完泄漏后必做）

当前源码中有 **43 处调试打点**，分布在：
```
Src/XCore/XCode/XSettings/XSettings.c        21 处 [XSettings][DBG]
Src/XCore/XCode/XSettings/XSettings_ini.c     4 处 [XSettings][DBG]
Test/XCodeTest/XSettingsTest.c               18 处 [XSettings][DBG] / [MEM]
```

清理方式：
```bash
grep -n "\[XSettings\]\[DBG\]\|\[MEM\]" \
     Src/XCore/XCode/XSettings/*.c Test/XCodeTest/XSettingsTest.c
```
逐条删除；`XSettingsTest_runAll` 里的逐用例增量打印与 `xsettings_test_memoryBalance` 的四段探针属于调试设施，应还原为简洁的最终断言。

同时可移除的临时守卫（保留与否自行判断，保留也无害）：
- `xsettingsReadIniLine` 的 `dbgRestarts` 循环保护
- `xsettingsUnescapedStringList` 的 `dbgIter`/`dbgSame` 循环保护
- `xconfEnsureAllSectionsParsed` 的 `dbgLoops` 上限

---

## 九、关键设计决策（勿轻易改动）

### 1. 平台语义统一取"非 Windows"口径
Qt 在 Windows 上 INI 键大小写不敏感、按文件原始顺序写出；其他平台大小写敏感、按字典序。
本项目**统一采用非 Windows 语义**（键大小写敏感、`\n` 换行、节/键字典序），读端同时兼容 Windows 写出的文件。

### 2. 映射一律非 COW
`XSettings` 内部的 `XVariantMap` 与组栈均显式关闭 COW（`XMap_create_ex(..., false)` / `XVector_create_ex(..., false)`）。
原因：XSettings 的拷贝语义要求源目标完全独立；COW 共享会让 `addedKeys` 互见，且组栈 COW 会导致读回值错乱（已踩坑）。

### 3. 契约边界（已在 `XSettings.h` 文件头 @details 注明）
- `NativeFormat` 退化为 INI 文件后端（无平台 API）
- `Registry32/64Format` 枚举保留但无后端，操作置 `AccessError`
- `@Variant(...)` / `@DateTime(...)` 用内部 TLV + base64 承载，**与 Qt 二进制不兼容**（Qt 自身跨流版本也不兼容）
- 不提供 `setIniCodec`（Qt 6 已移除）
- 提供 copy/move 槽位（Qt 禁拷贝；本项目按 XClass 对象模型提供深拷贝供容器使用）

### 4. 值槽 vs 指针槽（最容易再踩的坑）
- **值槽**（`XMap_create_XVariantMap`）：槽是内联对象，调用方必须传 `&obj`（对象地址），**绝不能传 `&ptr`**（指针变量地址）。传错必段错误。
- **指针槽**（`XSettings` 内部的节表裸 map）：槽是 `XString*`/`XByteArray*`，必须传 `&ptr`，且移除槽位后要手动释放对象。

---

## 十、常见错误速查（本次已踩）

| 症状 | 原因 |
|---|---|
| 段错误在 `XContainer_size_base` / `vtable_data` | 值槽 map 传了 `&ptr` 而非 `&obj` |
| 段错误在 `XClass_copy_base` | 同上，或未初始化目标对象 |
| 死循环挂起 | 行扫描 `goto restart` 未推进 `pos`；或状态机某分支不消费游标 |
| 值解析为空 | 非列表分支 `XClassDelete(current)` 而未入表 |
| 数组下标乱码 | `XVector` 用默认 COW |
| 同名节数据丢失 | flush 时覆盖而非合并 |
| 写盘状态 AccessError | `XDir_mkpath`/`XDir_filePath` 框架缺陷（已修） |

---

## 十一、相关文档

- 本目录：`docs/XSettings_Qt6.8对齐_进度交接_20261102.md`（本文档）
- Qt 源码基线：`D:/Qt/6.8.3/Src/qtbase/src/corelib/io/qsettings.cpp`
- 项目规范：`代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md`
- 对齐先例：`XRegularExpression_Qt6.8_移植可行性分析.md`、`XGui_Qt_Alignment_Handoff.md`
