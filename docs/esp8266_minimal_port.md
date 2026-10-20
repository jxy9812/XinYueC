# ESP8266 最小串口控制台移植（NONOS3 + 仓库 FreeRTOS V9）

> 目标形态：ESP8266 上运行 `XCoreApplication` 事件循环 + `XConsoleShell`
> 串口命令行（UART0，上电 74880，`user_init` 首行切 115200）。
> 砍掉文件系统/网络/GUI/SQL 等大件；FreeRTOS 用仓库 V9.0.0（XTENSA_LX106 移植层），
> SDK 用 NONOS SDK v3.0.5（Arduino 包内 `tools/sdk`，下称 NONOS3）。
>
> 本文档是桌面静态验证的交付：**链接已收敛**（`undefined_all.txt` 为空）。
> 未做真机烧录与运行验证（按任务约束不占 COM2、不烧板）。

所有工作文件在 `build/esp8266_console/`（下称 `<B>`）。

---

## 1. 验证结论（本会话实跑）

| 检查 | 命令 | 结果 |
|---|---|---|
| 全量编译（最终配置头 + `-mlongcalls`） | `bash <B>/build_console.sh`（内部逐个跑 `recompile_one.sh`） | 210 filelist 文件 + `user_main.c` + `console_stubs.c` 全部 PARTICIPATE（`recompile_log.txt` 无 ERROR） |
| 真链接（NONOS3 库组 + `--gc-sections`） | `xtensa-lx106-elf-gcc … -Tlocal.flash.ld -Wl,--gc-sections … @obj/all_objs.txt -Wl,--start-group … -Wl,--end-group` | `LINK_EXIT=0`，`link_err.txt` 无 undefined reference |
| 未定义符号清零 | `xtensa-lx106-elf-nm -u console.elf > undefined_all.txt` | **0 行** |
| 镜像尺寸 | `xtensa-lx106-elf-size console.elf` | text=383422，data=1632，bss=64648（irom 段 1019KB、DRAM 80KB 内，ld 无溢出报错） |
| 符号闭包（保守下界） | `python closure.py`（根 = `user_main.o`） | OBJS 213，REACHABLE 119；全集口径剩余 28 个项目级缺口全部由下述存根/SDK 库在链接期消化（`closure_result.txt`） |

> 历史说明：`console_test.elf`（537B text）只是链接脚本冒烟测试（仅验证
> `local.flash.ld`/`local.eagle.app.v6.common.ld` 可用），不代表应用闭包。
> 真正的应用级 ELF 是本次产出的 `console.elf`（383KB text）。

---

## 2. 交付物

| 文件 | 内容 |
|---|---|
| `<B>/filelist.txt` | **必需 .c 完整清单**，一行一个仓库相对路径，210 个文件 |
| `<B>/esp8266_console_minimal.h` | 最小配置头（`-include` 强制包含；本次核对通过，未再改动，见 §4） |
| `<B>/recompile_one.sh` | 单文件编译配方（含 `-mlongcalls`，被 xargs 并行调用） |
| `<B>/build_console.sh` | 端到端构建脚本（编译 → 对象清单 → 链接 → `nm -u` 清账） |
| `<B>/console_stubs.c` | 链接期缺口存根（构建目录本地；正式归宿见 §5 缺失待写表） |
| `<B>/user_main.c` | 固件入口（`user_pre_init` 分区表 + `user_init` 建控制台任务） |
| `<B>/closure.py` / `closure_result.txt` / `undefined_all.txt` | 符号闭包脚本与产物；`undefined_all.txt` 为空即收敛判据 |
| `<B>/console.elf` / `console.map` | 桌面验证 ELF 与链接映射 |
| `<B>/shim/` | NONOS3 头布局转发 shim（`esp8266/ets_sys.h`、`esp8266/uart_register.h`、`espressif/esp_libc.h`、`freertos/FreeRTOS.h`、`freertos/task.h`、`uart.h`、`esp_system.h`） |
| `<B>/local.flash.ld` / `local.eagle.app.v6.common.ld` | 由 `build/arduino_nos/build_arduino.sh` 同法预处理的链接脚本（4M1M flash） |

### filelist.txt 构成（210 文件）

| 区域 | 数量 | 说明 |
|---|---|---|
| `Src/XCode` | 72 | 控制台核心：XConsoleShell(8)/XCoreApplication(4)/XEvent(7)/XSync(9)/XStateMachine(9)/XFile*（被 XDeviceConsole 引用，`--gc-sections` 保命）等 |
| `Src/XContainer` | 60 | 容器全量（XString/XVector/XMap/XQueue/迭代器族…） |
| `Src/XData` | 22 | XVariant/XJson/XBson/XDateTime/XChar/XColor/XGeometry |
| `Src/XDevice` | 13 | 设备抽象层（含 XDeviceUsbGadget/Host/Msc——被 XDevice.c 惰性注册拉入） |
| `Src/XIO` | 11 | XIODevice/XSerialPort（控制台传输底座） |
| `Drive/FreeRTOS` | 9 | V9 内核 list/queue/tasks + XTENSA_LX106 port + **仅 heap_4** + Sync 四件套 |
| `Src/XTimer` | 5 | 定时器组（Shell 10ms 轮询兜底依赖） |
| `Drive/Espressif/ESP8266` | 5 | 平台实现（见 §5 三张表） |
| `Src/XMemory` `Src/XClass` `Src/XPlatform` | 4+4+2 | 内存/类系统/平台门面 |
| `Library/xxHash` | 1 | XCryptographicHash_XxHash64 硬依赖 |
| `Drive/Compiler/gcc` | 1 | `XAtomic_GCC.c`——XAtomic_* 类型化原子（XClass/XEvent/XLockFreeList 等 57+ 处引用） |
| `Drive/Unsupported` | 1 | `XDeviceUsb_unsupported.c`——USB host/gadget 平台存根（59 函数） |

裁剪要点（经 213 文件全量编译扫描证实）：
- **只链 heap_4**。heap_1/2/3/5 与 heap_2/4 同时链接触发 `pvPortMalloc/vPortFree/
  xPortGetFreeHeapSize/vPortInitialiseBlocks` 多重定义（实测 ld 报错），已从清单剔除。
- 正则 `XRegularExpression`：档案用 "先 include `XContainerConfig.h` 触发 guard，
  再 `#undef XRegularExpression_ON`" 的技巧全程压 0（配置头 104-114 行），
  XString 侧 8 处引用随裁剪消失（实测全集缺口表中已无 XRegularExpression*）。
- `XBase64_ON=1`（XByteArray 无守卫引用）、`XCryptographic_ON=1`（XHashMap/XString/
  XCommandLineParser 硬依赖 xxHash64）——不可裁。

---

## 3. xtensa 编译命令骨架（已验证配方）

### 3.1 include 顺序（顺序敏感，勿动）

```bash
ROOT=D:/code/CMake/XinYueC
TC=C:/esp/toolchain-xtensa/bin
SDK=C:/esp/arduino8266/tools/sdk
SDKINC=$SDK/include

INCDIRS="-I$ROOT/build/esp8266_console/shim \
 -I$ROOT/Drive/FreeRTOS/include \
 -I$ROOT/Drive/FreeRTOS/portable/GCC/XTENSA_LX106 \
 -I$ROOT/Library/xxHash -I$ROOT/Library/zlib \
 -IC:/esp/esp8266/extra_include \
 -IC:/esp/arduino8266/cores/esp8266 \
 -I$SDKINC -I$SDKINC/esp8266 -I$SDK/lwip2/include"
# 再追加 inc_dirs.txt 的 Src/Drive 子目录（脚本里 while 循环）
```

两处顺序陷阱（均为实测踩坑）：
1. **FreeRTOS V9 include 必须先于 NONOS3 SDK include**——NONOS3 根下也有
   `queue.h`（SDK 消息队列头），会顶掉 FreeRTOS 的 queue.h。
2. `shim/freertos/FreeRTOS.h` 不能写 `#include "FreeRTOS.h"`——quote-include
   先搜当前目录命中自身。

### 3.2 单文件编译

```bash
$TC/xtensa-lx106-elf-gcc -std=gnu99 -Os -c \
 -mlongcalls -mtext-section-literals -falign-functions=4 \
 -ffunction-sections -fdata-sections \
 -include $ROOT/build/esp8266_console/esp8266_console_minimal.h \
 -D__ESP8266__ -D__FreeRTOS__ -D__ets__ -DICACHE_FLASH \
 -DF_CPU=80000000L -DNONOSDK305=1 -DLWIP_OPEN_SRC \
 -DTCP_MSS=536 -DLWIP_FEATURES=1 -DLWIP_IPV6=0 -DARDUINO=10819 \
 -Wno-implicit-function-declaration -Wno-builtin-declaration-mismatch \
 $INCDIRS <file.c> -o obj/<stem>.o        # stem = 路径 / → _
```

**`-mlongcalls` 是硬性要求**：缺它时 arduino 链接脚本把所有 `.c.o(.text*)`
排进 irom（0x402xxxxx），call0 直接调 libc/ROM（0x4000xxxx）必超 ±128KB，
链接报 `dangerous relocation: call0: call target out of range`（实测 40+ 处）。

### 3.3 链接（NONOS3 库组）

```bash
$TC/xtensa-lx106-elf-gcc -g -Os -nostdlib -Wl,--no-check-sections \
 -Wl,-static -Wl,--defsym=ets_post=0x40000e24 \
 -Wl,--defsym=ets_intr_lock=0x40000f74 -Wl,--defsym=ets_intr_unlock=0x40000f80 \
 -L$SDK/lib -L$SDK/lib/NONOSDK305 \
 -Tlocal.flash.ld -Wl,--gc-sections -Wl,-Map=console.map -o console.elf \
 @obj/all_objs.txt \
 -Wl,--start-group -lhal -lphy -lpp -lnet80211 -lwpa -lcrypto -lmain -lwps \
 -lespnow -lsmartconfig -lairkiss -lwpa2 -llwip2-536 -lm -lc -lgcc -Wl,--end-group \
 -Wl,-e,call_user_start
```

- ROM 钉桩三条 `--defsym` 与 `-wrap spi_flash_*` 的取舍同
  `build/arduino_nos/build_arduino.sh`：ROM ld 的 PROVIDE 对 archive 内部引用
  不生效；本固件不写 flash，`-wrap` 系列省去。
- `-Wl,-e,call_user_start`：arduino 版 ld 脚本 `ENTRY(app_entry)` 是其 C++ 核心
  的入口，本固件走 NONOS3 启动链 `call_user_start`（libmain）。
- `-llwip2-536` 必须入组：libmain(`eagle_lwip_if.o`) 间接引用 lwip 符号。
- `libstdc++` 不需要（纯 C 固件）。

---

## 4. 配置头核对结论（`esp8266_console_minimal.h`）

本次未改动，全量编译扫描（213 文件 0 ERROR、86 文件裁空）证明其自洽。要点：

- 14 个模块总开关：`XGUI/XNETWORK/XPROTOCOL/XACTION/XPROPERTY/XTUI/XSQL/
  XEXCEL/XXmlStream/XDOM/XRCODE/XUPGRADE=0`；`XSYNC/XFILE/XCryptographic/
  XAbstractNetIoRing/XBase64/XConsoleShell/XSerialPort=1`。
  `XFILE_ON=1` 保守取值：只为让 `XDeviceFile.h` 的公共类型可用，对象层未引用
  部分由 `--gc-sections` 丢弃（实测 XFile 六件套进了闭包但未进最终镜像热路径）。
- 控制台子开关：保留 info/mem/uptime/tasks/clear/reset/reboot/exit；
  裁 datetime/date（无 RTC）、设备类（GPIO/PWM/SPI/I2C/ADC/CAN）、
  文件系统命令树、登录/exec、全部网络传输后端、vim/TUI。
- 容量收缩：命令表 16、历史 8、行缓冲 64、参数 8、输出块 128、单会话。
- FreeRTOS 调参：`configTOTAL_HEAP_SIZE=32KB`（#ifndef 覆盖）、
  `configMAX_PRIORITIES=8`（include guard 后 #undef/#define 技巧）、
  `configUSE_PORT_OPTIMISED_TASK_SELECTION=0`（LX106 无 CLZ 级指令加速）。
- `XCHAR_USE_CODE_GBK=1`（不能定义成 0 关 SYSTEM 分支，`defined()` 会误命中）。

---

## 5. 平台缺口三张表（对照 `Drive/Espressif/ESP8266/` 现有文件）

该目录现有 5 个 .c：`Core/XSystem_esp8266.c`、`Core/XAtomicLibcall_esp8266.c`、
`Device/XSerialPortESP8266.c`、`Device/XDeviceFile_esp8266.c`、
`Device/XPlatformStub_esp8266.c`。

### 表一：已实现（现有文件，被本构建链接采用）

| 文件 | 覆盖符号（族） | 证据 |
|---|---|---|
| `Core/XSystem_esp8266.c` | `XSystem_platformReset/Reboot/Shutdown/Environment/HasEnvironment/Pid/ProcessAlive/ExecutableFilePath/CpuUsage/GpuUsage/MemoryInfo` | 编译 11 defs（scan）；reset/reboot→`system_restart()`，shutdown→NotSupported，meminfo 合并 `system_get_free_heap_size()` |
| `Core/XAtomicLibcall_esp8266.c` | `__atomic_exchange/compare_exchange/fetch_add/fetch_sub` **1/4 字节宽** | LX106 无原子指令，`taskENTER_CRITICAL` 实现；链接收敛证实 1/4 字节族足够（8 字节见表三 G-atomic8） |
| `Device/XSerialPortESP8266.c` | `XSerialPortESP8266_configureUart` + UART0 寄存器级 read/write/flush | 编译 17 defs；函数体自实现（NONOS3 库不导出 `UART_*`） |
| `Device/XDeviceFile_esp8266.c` | `XDeviceFile_openStandardInput/legacyClose/legacyRead/legacyWrite/legacyFlush/legacySetStandardInputEcho` | 控制台 stdio 后端：持静态 XSerialPort 单例接 UART0，read 非阻塞 |
| `Device/XPlatformStub_esp8266.c` | `XDateTime_current*`（全零/-1）、`XDeviceFile_legacyOpen/Seek/Resize/Fstat/Map/Unmap/SetFileTime`（拒绝）、`XDeviceDir_platformOpen/Close/Read`（拒绝）、`XAbstractNetIoRing_createPlatform`（NULL，dispatcher 判空降级） | 编译 15 defs；契约式"不支持"而非编造行为 |

### 表二：存根可用（`<B>/console_stubs.c` + `Drive/Unsupported`，降级但契约成立）

| 存根 | 行为 | 为什么在这里 |
|---|---|---|
| `Drive/Unsupported/Device/XDeviceUsb_unsupported.c`（59 函数） | 全部返回 `XDeviceUsbError_Unsupported` | ESP8266 无 USB 控制器；`XDevice.c:374-379` 按类名惰性注册 host/gadget，vtable 链强制拉入平台函数族 |
| `vAssertCalled` / `vApplicationMallocFailedHook` / `vApplicationStackOverflowHook` | `system_restart()` 自愈 | `FreeRTOSConfig.h:84` 契约：断言由应用实现 |
| `NMIIrqIsOn=0`、`MacIsrSigPostDefHdl→pdFALSE`、`_xt_user_exit→任务自删替身` | 见右 | `port.c:80/159/114` 引用；NONOS3 库组不导出（全库 nm 实测）。本固件不启用 NMI/WiFi 中断投递，任务入口从不 return，替身等价 |
| newlib syscall 族：`_read_r(→0)/_write_r(丢弃)/_lseek_r/_fstat_r/_close_r/_exit(→restart)/_isatty_r(→1)` | 空设备语义 | 工具链 `libc.a` 不含（nm 实测无 `_malloc_r` 等）；arduino 由 `cores/heap.cpp`+核心补，本固件自补 |
| 堆胶水：`pvPortZalloc(3参)/sdk3_pvPortMalloc(4参)/xPortWantedSizeAlign` + 带账 `_malloc_r/_free_r/_realloc_r/free`（8B 头 size+魔数，`free` 魔数判别兼容 `pvPortZalloc` 裸块） | 映射到 FreeRTOS heap_4 | NONOS3 `libmain.a(user_interface.o)` 引用（链接错误实测：`user_interface.c:426/1289/1536/os_printf_plus`）；`__call_atexit.c:144` 与 `app_partition.c:312` 直引 `free` |
| `dhcps_stop()` / `dhcps_start_LWIP2()` 空实现 | no-op | `liblwip2-536.a` 只导出 `dhcps_start`（nm 实测），`dhcps_stop/dhcps_start_LWIP2` 由 arduino 核心补；本固件不起 SoftAP DHCP |
| `millis()`（tick 换算）/`settimeofday()`（恒 0） | 降级 | liblwip glue（`lwip-git.c:118/454`）引用；无 RTC 对时无处可写 |
| `g_lastMallocFailSize`、`core_version`（弱定义） | 只写/清账 | heap_4.c:328 引用；`core_version` 是链接后 symtab 唯一悬空 UND（ld 判定零引用不报错，来源不在 NONOS3/hal 库组也不在工程对象） |

### 表三：缺失待写（正式化清单，建议归宿 `Drive/Espressif/ESP8266/`）

| ID | 缺口 | 符号/依据 | 建议落点 |
|---|---|---|---|
| **G-xtglue** | RTOS SDK 的 xtensa context/interrupt 移植汇编：`_xt_int_exit`、`_xt_tick_timer_init`、`_xt_timer_int1`、`_xt_isr_unmask`（`_xt_user_exit/NMIIrqIsOn/MacIsrSigPostDefHdl` 的正式版同源） | **当前最大缺口**：`user_main.c` 一旦置 `XESP8266_START_SCHEDULER=1`（`vTaskStartScheduler` 可达），链接即报这 4 个符号 undefined——它们在 RTOS SDK `C:/esp/esp8266/lib/libmain.a`（xtensa_vectors.o/ets_timer.o/os_cpu_a.o）与 `libpp.a`（MacIsr/NMIIrq），但这些对象引用 V7 内核 API（`xTaskGenericCreate`）与 `user_start` 启动链，**不能直接摘用**，需按仓库 V9 重写 | `Core/XInterruptGlue_esp8266.S` |
| G-atomic8 | `__atomic_*_8` 六件（`load/store/exchange/compare_exchange/fetch_add/fetch_sub`） | `XAtomic_GCC.c` 的 8 字节类型（`XAtomic_Int64/Uint64`）在 32 位 LX106 降级为 libcall，工具链无 libatomic（实测无 `libatomic.a`）；现暂存 `console_stubs.c` | 并入 `Core/XAtomicLibcall_esp8266.c` |
| G-hooks | FreeRTOS 三钩子 + `g_lastMallocFailSize` 正式版（可加 UART 输出再复位） | `FreeRTOSConfig.h:84`、`heap_4.c:328` | `Core/XFreeRTOSHooks_esp8266.c` |
| G-nonosheap | `pvPortZalloc/sdk3_pvPortMalloc/xPortWantedSizeAlign` 正式版（可接 umm_malloc 或统一 heap_4） | NONOS3 libmain 引用（见表二） | `Core/XHeapNonos_esp8266.c` |
| G-newlib | newlib syscall 族正式版：`_write_r` 建议接 UART0（当前丢弃），`_read_r` 接 RX FIFO | 工具链 libc.a 缺失（实测）；`Drive/Espressif/ESP8266/README.md` 风险 1 同源 | `Core/XSyscalls_esp8266.c` |
| G-file | 文件系统平台族：`XDeviceFile_stat/remove/rename/renameReplace/mkdir/rmdir/link/copy/resolvePath/enumerateDrives/readLink/setPermissions/getSpecialPath/setCurrentPath/getStorageInfo`、`XDeviceDir_openPath/readNext` | 全集口径被 XFile/XFileInfo/XDir 引用（closure_result GAP 行），真链中被 `--gc-sections` 丢弃故不阻塞收敛；启用 SPIFFS 或 `XCONSOLE_SHELL_FILESYSTEM_ON=1` 时必须补齐 | `Device/XDeviceFile_fs_esp8266.c`（SPIFFS） |
| G-rand | `XRandomGenerator_platformFillSecure` | `XRandomGenerator.c:44` 平台契约；POSIX/win32 有实现，ESP8266 应接 `os_get_random`（真随机，硬件 RNG） | `Core/XRandom_esp8266.c` |
| G-stdout | `fwrite(stdout)`→UART0 的 newlib stdout 通路 | README 风险 1，未真机验证（当前控制台 write 走 XSerialPort 寄存器直写，不经 newlib，控制台路径不受此影响） | 随 G-newlib 一并处理 |
| G-sched | 调度器启动编排：`user_init` 末尾 `vTaskStartScheduler()`（`user_main.c` 已写好，默认 `XESP8266_START_SCHEDULER=0`） | 依赖 G-xtglue | user_main 正式版 |

---

## 6. 启动链与运行时序（桌面已核对的静态事实）

1. eboot/ROM → libmain `call_user_start` → `user_pre_init()`（分区表注册，
   `user_main.c:90-95`）→ `user_init()`。
2. `user_init`：`XSerialPortESP8266_configureUart(115200)`（先于任何输出，
   因 SDK 默认 74880）→ `xTaskCreate(console 任务, 栈 8192B, 优先级 2)`。
3. console 任务：`XCoreApplication_create_ex` → 取事件分发器 → 注册 7 条
   静态命令 → `XAbstractEventDispatcher_runDefaultShell`（挂 stdin/stdout
   Shell 并进事件循环）；stdin 由 `XDeviceFile_esp8266.c` 懒初始化接到
   UART0（非阻塞读 + 10ms 轮询兜底）。
4. **G-xtglue 未移植前调度器不会启动**（`XESP8266_START_SCHEDULER=0`），
   这是"链接收敛"与"可运行"之间的唯一剩余鸿沟。

## 7. 已知风险与未验证项

- 未真机烧录/运行（任务约束）；`console.elf` 仅证明符号与段布局收敛。
- bss 64648B 占 DRAM0（80KB）近八成：32KB FreeRTOS 堆 + Shell/XClass 静态
  数据；上板若 OOM，先降 `configTOTAL_HEAP_SIZE` 与控制台栈。
- SDK 预编译库针对自家 ABI，当前组合（NONOS3 库 + 仓库 V9 内核）靠
  `--gc-sections` 隔离了 V7 符号面；一旦扩大 WiFi/网络使用面，README 风险 2
  （V7 ABI 引用）可能重新暴露。
- `xtensa-lx106-elf-nm -u console.elf` 为空是收敛判据；重跑
  `bash build_console.sh` 应始终复现 `undefined symbols: 0`。
