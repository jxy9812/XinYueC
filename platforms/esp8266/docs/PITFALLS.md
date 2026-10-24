# ESP8266 移植踩坑清单（PITFALLS）

> 定位：**踩过的坑都在这里**，每条含「症状 → 根因 → 修法 → 防复发要点」。
> 叙事型进度见 [PROGRESS.md](PROGRESS.md)（按时间倒序），本文是**可检索的教训库**。
> 记录时间：2026-10-23 深夜（ESP8266 NONOS3 框架原生事件循环战役收官时整理）。
>
> 索引：A 镜像与构建管线 / B 驱动与硬件 / C 框架与移植 / D 真机调试方法论 / E 工具链与环境

---

## A. 镜像与构建管线

### A1 ★★★ 镜像长度+CRC 写进代码段 → 取指损坏 → 恒定崩溃点

**症状**：`Fatal exception 0(IllegalInstructionCause)`，`epc1=0x40201013`
（= `XAtomic_exchange_bool` 入口第 2 条指令），`excvaddr` 每次构建都变
（0x40266404 → 0x40266460 → 0x402645e2…）。

**根因**：`platforms/esp8266/nonos/tools/fix_rodata_pos.py` 在重排镜像后，把
**4 字节镜像长度 + 4 字节 CRC32 写到固定偏移 0x1010 / 0x1014**。
ESP8266 app image 在 byte 16 **没有** size/CRC 字段（eboot 只认镜像尾部的
单字节 checksum）——那是 **ESP32 extended header** 的约定，被误植过来了。
byte 16 起就是第一段的数据：`.rodata` 并入 irom0 后第一段 = `.irom0.text`，
其开头 8 字节恰是**段首函数** `XAtomic_exchange_bool` 的入口指令，于是：

```
ELF .irom0.text[0x40201010]: 12 c1 f0 09 31 30 30 74   ← 正常指令
BIN  [0x1010]:              21 d0 06 00 bd 4d 52 66   ← 长度 0x06D021 + CRC
```

CPU 从 0x40201010 取指拿到垃圾（第 4 字节 0x00 = 非法指令）→ 崩溃。

**为什么几个月没暴露**：当时平台时基返回 -1 → 框架时间轮全哑 → **没有任何
执行流走到那个函数**。这是典型的**假阴性**：bug 一直在，只等第一条真正
执行的路径激活它。

**修法**：删掉 `fix_rodata_pos.py` 里写这两个字段的代码。

**防复发**：
- 改镜像后必须核对 `bin[0x1010..0x1020] == ELF .irom0.text 开头 8 字节`
  （一条命令即可，是这类 bug 的通用体检）。
- ESP8266 与 ESP32 的 app image 格式不同，**不要照抄 ESP32 的偏移约定**。
- 任何"改完镜像后行为诡异"先怀疑镜像布局，再怀疑代码。

### A2 链接脚本 `INCLUDE` 相对 CWD 解析

产物移到 `build/` 后，链接报 `cannot open linker script file
local.eagle.app.v6.common.ld` —— 链接器的 `INCLUDE` 是相对**当前工作目录**
而非脚本目录。修法：链接命令加 `-L"$B"`。

### A3 批量替换漏掉无尾斜杠的 `(cd _esp ...)`

路径迁移脚本改 `cd _esp` → `cd "$B/_esp"` 时漏了 `(cd _esp && ...)` 这种
**无尾斜杠**形式 → `cd: _esp: No such file or directory`。批量替换后必须
grep 一遍所有变体。

### A4 ★★ 增量构建不感知"档案"变更 → 陈旧 .o → 链接期才炸

**症状**：改了 `cfg/xgui_shell_esp8266.h`（如 `XTHREAD_ON` 0/1），
框架 275 个 TU 全部用旧 .o，链接期报 `undefined reference`。

**根因**：`recompile_final_one.sh` 的增量判据只比 `.o` 与**源文件** mtime，
不比 **CFG 档案**（`-include` 强制包含，等于每个 TU 的输入）。
`build_fw_final.sh` 的 `build_tu()` 原本有 CFG 检查，两处口径不一致。

**修法**：统一到 `build_tu()` 口径——`.o` 比源文件**和** CFG 都新才跳过。

**防复发**：任何"全局输入"（强制包含的档案、共享头、编译选项）都要进增量判据。

### A5 构建期间并发跑第二个 build

`all_objs.txt` 被追加成 391 条重复 → 链接报 multiple definition。
**一次只跑一个构建**。

### A6 bash 管线内勿用 `sort -u`

PATH 会解析到 Windows `sort.exe`，它不认 `-u` → 静默得到错误结果。

### A7 GBK 误读吞 `*/`

CFG 档案里的中文注释经 GBK 读回时可能吞掉 `*/` → 后续整段被当注释。
**关键行保持纯 ASCII**。

### A8 `RODATA_IN_FLASH=1` 必须带

不带 → 26KB `.rodata` 落 DRAM → 系统堆只剩 7.2KB → 各种诡异 OOM。

### A9 ★★★ bash 读的**列表文件必须是 LF**（CRLF 会让所有 `-I` 静默失效）

**症状**：
```
xtensa-lx106-elf-gcc.exe: error: <源文件>: Invalid argument
xtensa-lx106-elf-gcc.exe: fatal error: no input files
.../arch/cc.h:10:10: fatal error: XPrintf.h: No such file or directory
```
（每个 TU 都报，看起来像"环境全坏"）

**根因**：构建脚本用
```sh
while IFS= read -r d; do INCDIRS="$INCDIRS -I$ROOT/$d"; done < cfg/inc_dirs.txt
```
**bash 的 `read -r` 会保留行尾的 `\r`**（实测：`line 3: $'Src/XClass\r'`）。
于是 `-I$ROOT/Src/XClass\r` 指向一个不存在的目录 → **所有** include 路径失效 →
头文件找不到。`cfg/inc_dirs.txt` 与 `cfg/filelist_final.txt` 必须存 **LF**。

**为什么难查**：文件被改成 CRLF 的时机与故障现象完全无关（可能是任何一次
编辑器保存），报错信息又是"头文件缺失/参数无效"这种**完全不像行尾问题**的形态。

**防复发**：
- 改 `cfg/*.txt` 时确认行尾是 LF（`file` 命令 / `git diff --check`）；
- 仓库根 `.gitattributes` 里给 `platforms/esp8266/nonos/cfg/*.txt` 标 `text eol=lf`；
- 仓库内工具 `out/x_eol.py check|tolf`（本次新增）可一键体检/转换。

### A10 ★ `.sh` 脚本头部不能有 UTF-8 BOM

`recompile_final_one.sh` 曾因工具写回时带上 BOM（`﻿#!/bin/bash`），
bash 不认这个 BOM → 报 `line 1: ﻿!/bin/bash: No such file or directory`
→ 脚本被当命令跑 → 后续错误满屏且与真因无关。
**BOM 必须剥掉**（`out/x_bom.py`）。

---

## B. 驱动与硬件

### B1 ★★★ UART 帧格式位全部错位 → 收到的字节全乱

**症状**：PC 发 `version\n`（`76 65 72 73 69 6f 6e 0a`），板子收到
`76 59 ae a5 6f 4b`。寄存器现场 `clkdiv=1068`（波特率对）但
`conf0=0x79`（**7 位数据** + 停止位错码）。

**根因**（`Drive/Espressif/ESP8266/Device/XSerialPortESP8266.c`）：四个
函数按错误的位/寄存器写：

| 函数 | 当时写的 | ESP8266 手册实际位置 |
| --- | --- | --- |
| `UART_SetWordLength` | `CONF1[1:0]` | **`CONF0[3:2]`**（CONF1 是 RX FIFO 阈值，与帧格式无关） |
| `UART_SetStopBits` | `CONF0[14:13]` | **`CONF0[5:4]`** |
| `UART_SetParity` | 清 `BIT(12)` | **`CONF0[1]`=PARITY_EN、`CONF0[0]`=奇偶选择** |
| `UART_ResetFifo` | 额外 OR 臆造的 `TX_EN/RX_EN/OUT_EN` | 无此位（落在校验选择/使能位上）→ 污染帧格式 |

**修法**：四个函数全部按手册重写。

**防复发**：自实现的寄存器操作**逐条对手册核位**，并加一条真机断言：
读回 `CONF0` 打印，帧格式位应与期望一致。

### B2 ★ `erase_flash` 会把 I2C 总线搞坏

烧录**只 `write_flash`，禁止 `erase_flash`**（它会破坏 I2C 总线状态，
表现为屏黑/初始化 NACK）。同理 **永远不要整片擦除**。

### B3 I2C 总线楔死（SDA stuck low）

从机半截事务被复位打断 → SDA 卡低 → init NACK=27。
修法：驱动加 `i2c_bus_unlock()`（标准 9-clock recovery + STOP），
earlyInit 与 init 重试路径都接上。

### B4 `flush_step` 增量上屏从未生效

所有历史日志里**零条** `[i2c] step pgN done` → 面板只有开机棋盘
testPattern。棋盘在黄蓝屏上的观感就是"条纹+方框"，与故障残留肉眼无法区分
→ **testPattern 改成白条+外边框防误判**。

### B5 板子复位后 PC 需 disconnect→connect

旧关联不自动重认证。且 join 刚完成时 ARP 还没建立，**再等 10–15s 才 ping**。
过早抓包会看到 `allocFail` 暴涨的假象（其实是 PC 没发出去）。

### B6 串口端口漂移 + esptool 假错误

板在 COM2/COM4 间漂移；`--port COM4` 指到不存在的口会报**假的 pairs 参数
错误**（真实原因是指错口）。不带 `--port` 自动探测最稳。

### B7 SDK 头必须最先 include

`nonos_v305/include/c_types.h:106` 有 `typedef unsigned char bool;`，
与 `<stdbool.h>` 冲突（"two or more data types in declaration specifiers"）。
同一 TU 里 SDK 头必须**先于**框架头引入。

---

## C. 框架与移植

### C1 ★★★ 平台层"返回 -1 表示不可用"的保守桩 → 上层整体静默

**症状**：框架的 CoarseTimer 在 ESP8266 上**从未工作过**，但没有任何报错；
表现是"串口没反应"，离根因极远。

**根因**：`XDateTime_currentMSecsSinceEpoch()` 返回 -1 → 时间轮时基恒为负
→ `CoarseTimer` 永不到期 → shell 自己注册的 10ms 串口轮询定时器从不触发。

**教训**：**保守桩会让依赖它的整个子系统静默**。移植平台时应优先补齐
**单调时基**（不要求 RTC），宁可"暂时不准"也不要"不可用"。

### C2 ★★ 框架开关守卫 bug：共用函数被包进特性守卫块

**症状**：`XCONSOLE_SHELL_COMPLETION_ON=0` → 链接失败
`undefined reference to 'xcs_redraw_input_line'`。

**根因**：`XConsoleShell.c` 里 `xcs_redraw_input_line` 的注释自述
"**历史回填与补全共用**"，但它被放进了 `#if XCONSOLE_SHELL_COMPLETION_ON`
块（497..1212 行）内。关补全 → 共用函数也没了 → 历史回填路径链接失败。
同类：`xpb_trace_dump` 被包在 `XPB_TRACE_ON` 块内。

**修法**：在共用函数前后闭合/重开守卫。

**防复发**：关掉任一特性开关后，**必须重新链接整个工程**（开关组合矩阵）。

### C3 ★★ 宏传递漏洞：档案开关对某些 TU 不生效

**症状**：`XPB_TRACE_ON=0` 写进了 `cfg/xgui_shell_esp8266.h`，但
`g_xpbTrace`（512B）仍在 `.bss` 里。

**根因**：`platform/XDeviceNetwork_lwip_esp8266.c` 由 **lwip 编译段**
（`CFLAGS_LW`）编译，**该段没有 `-include $CFG`** → 档案里的宏传不进来 →
驱动源码里 `#ifndef XPB_TRACE_ON → 1` 的默认值照常生效。

**修法**：在该 TU 源码里直接改默认值（esp8266 专用文件，不影响桌面）。

**防复发**：一个 TU 用哪套 CFLAGS 编译，就只能收到那套 CFLAGS 里的 `-D/-include`；
**"档案开关"与"分段编译"是两条独立的通道**。

### C4 ★ `XTHREAD_ON=0` 时四个文件编译/链接失败

裸机关线程时：`Src/XPlatform/XTask.c/.h`、`XConsoleShellTasks.c`、
`platforms/esp8266/nonos/platform/sync_baremetal.c` 都无条件引用 `XThread` 类型与
`XTask_enumerateThreads` → 编译报 incomplete typedef、链接报 undefined。
修法：四者加 `#if XTHREAD_ON` 守卫（`XTaskInfo/XTaskState/XTaskVisitor`
这类平台无关契约保留）。

### C5 ★ `XCONSOLE_SHELL_ASYNC_OUTPUT_ON=0` → 命令输出被吞

异步模式下命令结果写进 shell 内部输出缓冲，而 `xcs_async_read_ready` 里的
`flushOutput` 调用点整个被 `#if` 编译掉 → **敲命令无回显无结果**，只有走
`XPrintf` 直出的错误文本（如"未知命令"）可见。裸机档案必须开 `=1`。

### C6 ★ 命令重名 → 整批注册被拒

`xcs_register_static_commands` 的预检是**整批**语义：一个重名 → 全批返回
false。模板注册 7 个静态命令，其中 4 个与 shell 内建（clear/reset/reboot/exit）
重名 → **连不重名的 info/memory/uptime 也一起丢**，表现为"所有命令都未知"。
修法：模板不再注册（内建已覆盖 12 个），或逐个注册并检查返回值。

### C7 ★ 桌面复现探针跑太快 → 误判"定时器没驱动"

2000 轮 `processEvents(0)` 全速跑完不足 1ms → 10ms 的 CoarseTimer 一次
都没到期 → `fired=0`，看起来像"框架没驱动定时器"。
修法：**每拍之间 sleep**，让时间真的流逝。
（附带：探针 exe 路径写错 → 脚本异常退出码 1 → 被误读成"崩溃"。）

### C8 ★★★ lwh 隔离堆的配比是"配比"，不是"随便给点"

P0 泄漏定案验证过的配比：**lwh ≈ 21.4KB**（lwip 池 16.3K + WiFi 关联
~5K）。裁剪 `.bss`（如 shell 关补全省 904B）→ lwh 缩到 16.8K → 跑 60+ 包后
**lwh 耗尽** → `allocFail` 永久 → 丢包 80–100%。

**教训**：80KB RAM 是零和的，**任何 `.bss` 缩减都要检查它是不是从 lwh
那边挪走的**。lwh 配比是 P0 定案的验收基线，动它要连带重验。

### C9 ★★ 隔离堆的水位计不可信

esp 胶水会**越权改写堆块头**（P0 定案已知黑历史）。于是：
- 遍历式水位计：遇坏 `size`/`next` 早停 → **报 0（假"耗尽"）**；
- 记账式水位计：`free` 时不知该扣多少（块头 `size` 可能已被改写）→
  计数反复归零 → **读数毫无意义**。

**真判据只有一个：分配失败计数**（`lwh_allocFail` / lwip 侧 `allocFail`）。
本轮曾因水位计误报，把"分配正常"误判成"耗尽"，白查一轮。

### C10 ★ `ensure` 前置到收包路径 = 有害

把 `drv_ensure_init()` 提到 `esp2glue_alloc_for_recv` 开头（想让池最早
物化）→ `lwip_init`/`netif_add` 在 **SDK 收包上下文**里跑 → 半途而废
（`lwip_ready` 已置位不再重试）→ 池永久缺失 → `allocFail` 100%。
**ensure 的首次触发必须留在底座验证过的两条路径上**（blob 的 `lwip_init`
钩子 / `ethernet_input`）。

---

## D. 真机调试方法论

### D1 ★★★ `epc1` 恒定 + `excvaddr` 漂移 = 取指损坏/镜像错位

不是"某个数据符号被当函数指针"。**先怀疑镜像布局与取指**，别顺着
"野指针"深挖框架数据结构——那样会一路错到框架头上（本次就错了一整晚）。

### D2 ★★★ "稳定版"可能只是**没人执行到坏路径**

时基 -1 让时间轮全哑 → 坏函数永远不被调用 → 一切看起来正常。
**换机后所有"已验证"结论都必须在变量真实激活后重验**；新增的运行时功能
（定时器到期、异常路径）必须单独触发一次。

### D3 ★ 诊断打点刷屏会触发 soft WDT 复位

@74880 下一行 ≈ 27ms > 5ms 拍间隔 → 打印本身拖垮事件循环 → 无 Fatal 的
复位。**定位完成立即删打点**；调试期打点必须稀疏（每 N 拍一条）。

### D4 ★ 板端计数比 PC 侧 ping 可信

PC 网卡状态会劣化（当晚 WLAN 89% 时 ping 100% 丢），板端
`rxOk / allocFail / lwhFail` 才是真判据。

### D5 崩溃点会"漂移"

同一根因（取指损坏）在不同构建里表现为**不同函数**（被覆盖的那段代码的
入口）——曾先后被误判为"崩在 WiFi 初始化"、"崩在 shell 初始化"、"崩在
框架定时器路径"。**先验证根因假设，再修症状**。

---

## E. 工具链与环境（本机 Windows）

| 坑 | 处置 |
| --- | --- |
| `.bat` 含 UTF-8 中文注释 → cmd 按 OEM(GBK) 读，被当命令 → `'绌烘牸鐨?rem' 不是内部或外部命令` | `.bat` **必须纯 ASCII** |
| `cmd /c` 传带空格的绝对路径 → 拆词 → `'C:\Program' 不是内部或外部命令` | 用无空格的 `.bat` 包装器整体转发 |
| cmd 里 `;` 会被当参数塞进 argv | 用 `&` 或写成 `.py` |
| 多行 `python -c` 经 cmd 静默失败 | 逻辑一律写成 `.py` 文件 |
| git 不在 PATH | 用 VS 自带 `...\Team Explorer\Git\cmd\git.exe` |
| PortableGit bash 里 `$HOME`/`$USERPROFILE` 为空 | 不能用来拼路径 |
| `command -v python` 命中 WindowsApps 占位符（能查到但跑不动/弹商店） | 先 `-c "import sys"` 验证能真跑 |
| CMake `CMAKE_GENERATOR` 被翻成 NMake → "配置成功不产 build.ninja" 死循环 | 删缓存后 `-G Ninja` 重配 |
| `esp8266/` 被根 `CMakeLists.txt` `add_subdirectory` 纳入 → VS 出现**两个同名** `esp8266_nonos_fw.slnx`，两个都切不动 | 根工程**不纳入** esp8266，单独 Open Folder |
| 工作流/脚本的 stdout 有上限（ZCode world.run 256KB） | 重输出命令落盘到日志文件，只读尾部摘要 |

---

## 附：本轮修复的三处真 bug（一句话版）

1. `fix_rodata_pos.py` 把镜像长度+CRC 写进 0x1010 → 覆盖 `.irom0.text`
   段首函数入口 → 取指损坏 → 恒定崩溃点。（A1）
2. `XSerialPortESP8266.c` 的 UART 帧格式位全错位 → 串口字节全乱。（B1）
3. 框架开关守卫：`xcs_redraw_input_line` / `xpb_trace_dump` 被包进
   特性守卫块 → 关对应开关即链接失败。（C2）
