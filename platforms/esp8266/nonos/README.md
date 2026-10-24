# platforms/esp8266/nonos — NONOS3 固件模板（XinYueC 全功能版）

> 本目录是 **XinYueC 在 ESP8266 (NONOS SDK 3.0.5) 上的完整固件模板**：
> XCoreApplication 事件循环 + 串口 shell + WiFi SoftAP(仓库 lwip 2.2.1) +
> XGui(SSD1306) + XMemory 三层分配器（MultiPool/VariablePool/lwh 独立堆）。
> 2026-10-23 真机全场景验证：**框架原生事件循环**（每拍 processEvents，
> 框架时间轮驱动 CoarseTimer，notifyInput 变通已删）+ SoftAP 同开
> 100 发 ping 0% 丢包、板端 `rxOk==rxReq`/`lwhFail=0`/零泄漏、shell 12 命令可用。

## ⚠ 动手前先读

- **[../docs/PITFALLS.md](../docs/PITFALLS.md)** —— 踩坑教训库（镜像布局 /
  UART 寄存器位 / 开关守卫 / 增量构建 / 真机调试方法论）。改这块板之前
  至少读 A、B、C 三节。
- **[../docs/PROGRESS.md](../docs/PROGRESS.md)** —— 进度流水与当前状态。

三条最容易重蹈的：**①改镜像后必须核对 `bin[0x1010..]` 与 ELF
`.irom0.text` 开头一致**（长度/CRC 写错位置会静默覆盖代码段）；
**②自实现的 UART 寄存器操作要逐位对手册**；**③关掉任何框架特性开关后
必须整工程重新链接**。

## 事件循环形态（2026-10-23 起为框架原生）

`main.c` 每拍无条件 `XCoreApplication_processEvents(0)`（`flags=0` 不阻塞），
由框架自己的时间轮（全局 `XTimeWheelGroup`，1ms 刻度，时基=
`XDateTime_currentMSecsSinceEpoch()`）驱动 CoarseTimer 到期——
包括 `XConsoleShell_startAsync` 注册的 10ms 串口轮询定时器。
框架 timerEvent → `xcs_async_read_ready()` 直接读 UART，**不分配事件**，
零 arena churn。

平台层时基必须有效（返回单调毫秒），否则时间轮无刻度、CoarseTimer
永不到期——**"返回 -1 表示不可用"的保守桩会让整个定时器体系静默**。

## 构建与烧录

**构建 = bash+py 脚本管线**（`tools/build_fw_final.sh` 编排：275 源逐个编译 →
链接 → elf2bin → rodata 归位）；**CMake 仅作编排层**（IDE 集成/一键构建/烧录），
见下方"CMake"节。不使用 CMake 编译器检测（xtensa 工具链在管线内部调用）。

```sh
# 构建（产出 fw_final.bin，含 eboot，烧 0x0）
PATH=/d/anaconda3:/c/WINDOWS/system32:/usr/bin:$PATH bash tools/build_fw_final.sh

# 烧录（禁用 erase_flash！会毁 I2C/RF 校准状态）
# 不要带 --port：板会在 COM2/COM4 之间漂移，指错口 esptool 会报一个
# 完全误导的 "Must be pairs of an address and the binary filename" 错误。
python -m esptool --chip esp8266 --baud 115200 write-flash \
  --flash_mode dout --flash_freq 40m --flash_size 4MB 0x0 fw_final.bin
# 串口日志：74880（user_init 首行切到 74880 输出，见 main.c）
# 上屏验证：日志必须出现 8 条 "[i2c] step pgN done" + "i2cErrs=0"
```

首次构建前确认 `tools/build_fw_final.sh` 顶部的外部路径（本机环境，非模板内）：
`ARD=C:/esp/arduino8266`（NONOS SDK+lwip2 胶水）、`TC=C:/esp/toolchain-xtensa`。
构建产物 `_obj/ obj/ *.log` 已在 .gitignore 排除。

**增量规则（2026-10-22 重写，`build_tu()`）**：

| 条件 | 行为 |
|---|---|
| `.o` 不存在 / 比源文件旧 / 比 `cfg/xgui_shell_esp8266.h`（CFG）旧 | 编译 |
| 否则 | 跳过 |
| **编译失败** | 打印 `!! 编译失败: <file>` 并**退出**，绝不静默留旧产物 |

每个被重编的 TU 会打印 `CC <src>` / `AS <src>`，日志一眼可见。

**模板自身代码 100% 可从源码构建**：`main.c`、`drivers/**`、`app/**`、
`platform/**` 共 13 个 TU，删光 `obj/*.o` 后仍能完整构建并跑通
（已实测：312 obj、link OK、ping 25/25 0% 丢包）。**不再依赖任何旧机 .o。**

**`_obj/` 里的 275 个框架 .o**（`Src/**` 258 + `Drive/**` 16 + `Library/xxHash` 1）
同样**可以从源码全量重编**，已实测 `FORCE_ALL=1` 清空后完整重建成功
（312 obj、link OK、零编译错误）。它们来自**仓库根目录**的源码，模板只持有
`cfg/filelist_final.txt` 这份清单：

| 清单顶层 | 数量 | 举例 |
|---|---|---|
| `Src/` | 258 | `Src/XGui/Graphics/XPainter.c` → `_obj/Src_XGui_Graphics_XPainter.c.o` |
| `Drive/` | 16 | `Drive/Espressif/ESP8266/Core/XAtomicLibcall_esp8266.c` |
| `Library/` | 1 | `Library/xxHash/xxhash.c` |

## 一键构建 / 烧录（推荐用这个）

`platforms/esp8266/build.py` 是最省事的入口，**不需要 CMake**：

```bat
python platforms\esp8266\build.py                 :: 只构建
python platforms\esp8266\build.py --flash         :: 构建 + 烧录（一步到位）
python platforms\esp8266\build.py --flash-only    :: 只烧录（跳过构建）
python platforms\esp8266\build.py -c COM4         :: 指定串口（默认自动探测）
python platforms\esp8266\build.py --clean         :: 先清中间产物再全量构建
```

它做的事：

1. **依赖自检** —— 打印 bash / 工具链 / Arduino SDK / python 的位置，
   缺什么直接告诉你缺在哪、怎么补（与 `tools/esp_env.sh` 同一套查找规则：
   环境变量 → 仓库内 `platforms/esp8266/external/` → 本机默认路径）
2. **构建** —— 调 `tools/build_fw_final.sh`，实时显示进度：
   - 框架 TU（275 个）并行编译时脚本**不打印日志**，所以按 `.o` 文件数量
     监控进度条：`[████████░░░░░░]  42%  框架 115/275  lwip 24/24  模板 0/13`
   - 模板 TU（13 个）逐条显示 `CC main.c` / `AS platform/cre.S`
3. **烧录** —— 调 esptool，百分比进度条 + 写入字节数 + Hash 校验结果

## 目录布局

```
nonos/
├── main.c          固件入口（框架初始化 + 事件循环）
├── app/            用户实现：user_business.c + OLED 底层驱动 + XGui 裁剪头
├── drivers/        外设驱动层：xdevice_uart / xdevice_oled / xdevice_init
├── platform/       平台适配：stubs / WiFi 胶水 / lwipopts / 同步原语 / RNG
├── cfg/            裁剪档案 + 源清单 + shim
├── tools/          构建管线（build_fw_final.sh / esp_env.sh / rodata 后处理）
├── build/          ★ 所有构建中间产物都在这（可整个删掉重编，已 gitignore）
└── fw_final.bin    最终固件（随仓库走，可直接烧录）
```

清理构建产物：

```bat
python platforms\esp8266\build.py --clean     :: 只清中间目录（_obj/ obj/ _lwip/ _esp/）
python platforms\esp8266\build.py --purge     :: 彻底清（含链接脚本/中间 .o/链接产物）
```

依赖可执行文件会按 `platforms/esp8266/external/setup_deps.ps1` 的指引准备。

## CMake 集成（VS / CLion 一键构建 + 烧录）

### ⚠ 本工程必须【独立打开】，不能被根工程 add_subdirectory 纳入

2026-10-22 实测：把它 `add_subdirectory()` 进根 `CMakeLists.txt` 后，VS 的
启动目标列表里会出现**两个同名** `esp8266_nonos_fw.slnx`（VS 的 CMake 扩展
既扫描子目录、又跟随父工程的引用，两份都注册进来），**两个都无法切换/执行**。

根 `CMakeLists.txt` 已撤掉该引用，esp8266/ 保持独立子工程。

### 独立打开（推荐）

```bat
cmake -S esp8266 -B build_esp
cmake --build build_esp                    :: 构建固件
cmake --build build_esp --target flash     :: 烧录
cmake --build build_esp --target monitor   :: 串口日志
```

VS 里：**File > Open > Folder… 选 `esp8266/`**（或打开 `build_esp\*.slnx`）。
`CMakePresets.json` 已提供 `default` / `fw` / `flash` 预设。

### 不用 CMake 也行

底层构建不依赖 CMake，直接跑脚本即可产出固件：

```sh
bash platforms/esp8266/nonos/tools/build_fw_final.sh     # → platforms/esp8266/nonos/fw_final.bin
```

### 端口配置

```bat
cmake -DFW_COM_PORT=COM4 ...    :: 锁定端口
:: 默认 auto = 让 esptool 自动探测（板子在 COM2/COM4 间漂移）
```

## 换电脑：依赖布局（零外部依赖方案）

### 目录约定

```
<仓库根>/
├── Src/  Drive/  Library/        ← 框架源码（275 个 TU 的来源）
├── platforms/esp8266/external/                    ← 外部依赖集中地
│   ├── setup_deps.ps1            依赖检查/安装指引脚本
│   ├── nonos_v305/include/       ★ 已提交进仓库（149KB 纯文本）
│   │                               NONOS SDK 3.0.5 头（user_interface.h 等）
│   ├── toolchain-xtensa/         ⬜ 需自备（226MB，不入库）
│   │   └── bin/xtensa-lx106-elf-gcc.exe
│   └── arduino8266/              ⬜ 需自备（75MB，不入库）
│       ├── tools/sdk/{include,lib,ld,lwip2}
│       ├── cores/esp8266/
│       ├── bootloaders/eboot/eboot.elf
│       └── tools/elf2bin.py
└── platforms/esp8266/nonos/                ← 模板（本目录）
```

**只有 `nonos_v305/include` 进仓库**——它是纯文本 149KB，让模板在拿到工具链后
就能直接编译，不必再去别的机器找 NONOS SDK 头。

工具链与 Arduino 包体积约 300MB，**不进仓库**（会让 clone 变得极慢，
且二进制在 git 里无法有效压缩）。

### 新电脑上的步骤

```powershell
# 1. clone 仓库（自带框架源码 + NONOS 头 + 模板）
git clone <repo>

# 2. 检查缺什么（不下载，只检查并给出获取指引）
powershell -ExecutionPolicy Bypass -File platforms\esp8266\external\setup_deps.ps1

# 3. 按提示把工具链与 Arduino 包放到 platforms\esp8266\external\ 下，再跑一次确认
powershell -ExecutionPolicy Bypass -File platforms\esp8266\external\setup_deps.ps1

# 4. 构建（全量编译 288 个 TU）
bash platforms/esp8266/nonos/tools/build_fw_final.sh
```

### 路径解析优先级

`tools/esp_env.sh` 按以下顺序决定用哪套路径，**无需改脚本**：

| 优先级 | 来源 | 说明 |
|---|---|---|
| 1 | 环境变量 | `ESP_TOOLCHAIN` / `ESP_ARDUINO` / `ESP_NONOS_V305` / `ESP_EXTRA_INCLUDE` / `ESP_PYTHON` |
| 2 | **仓库内 `platforms/esp8266/external/`** | 零外部依赖方案，clone 即用 |
| 3 | 本机默认路径 | `C:/esp/toolchain-xtensa` 等（旧机布局） |

**注意**：一旦用环境变量显式指定，就不会再做 `platforms/esp8266/external/` 或默认路径的探测
（避免"我明明指定了却没用上"）。

查看实际解析结果：

```sh
ESP_ENV_VERBOSE=1 bash -c '. platforms/esp8266/nonos/tools/esp_env.sh; _esp_check_paths'
```

输出示例：

```
== ESP 依赖解析 ==
  ESP_TOOLCHAIN   = C:/esp/toolchain-xtensa   (来源: default)
  ESP_ARDUINO     = C:/esp/arduino8266     (来源: default)
  ESP_NONOS_V305  = <repo>/platforms/esp8266/external/nonos_v305  (来源: bundled)
  ESP_EXTRA_INCLUDE = C:/esp/esp8266/extra_include (来源: default)
  ESP_PYTHON      = python
```

### 自检失败时的输出

缺失路径会在**构建开始前**报错，不会跑到一半才失败：

```
!! 缺少：C:/esp/toolchain-xtensa/bin/xtensa-lx106-elf-gcc.exe
==============================================================
外部工具链路径不完整。请按下面之一处理：
  1) 零外部依赖方案（推荐）：把工具链与 Arduino 包放到仓库
     platforms/esp8266/external/ 下，然后运行
       powershell -ExecutionPolicy Bypass -File platforms/esp8266/external/setup_deps.ps1
  2) 设环境变量后再构建 ...
  3) 参考 README「换电脑：外部依赖清单」一节
==============================================================
当前解析到的路径：
  ESP_TOOLCHAIN   = ...   (来源: ...)
```

### `extra_include` 说明

旧机上的 `C:/esp/esp8266/extra_include` 是一组 SDK 头的补丁版本，
历史上用于解决 lwip2 与 NONOS 的 `ip_addr` 结构冲突。

**实测结论（2026-10-22）**：当前编译配方下**非必需**——把它指向不存在的路径，
全量构建依然 312 obj / link OK。因此不需要放进 `platforms/esp8266/external/`。
构建时若缺失只会打印一条提示，不影响结果。

## 目录结构（2026-10-22 分层重构）

```
nonos/
├── main.c                  ★ 固件入口：只做 4 件事
│                             ① XDevice_init_all()  一行初始化全部外设
│                             ② net_init()          SoftAP 网络
│                             ③ console_init_once() 框架 + shell
│                             ④ os_timer 5ms → cont 泵（事件循环）
├── drivers/                ★ 外设驱动层：每个外设一个文件
│   ├── xdevice_init.c/.h     总初始化入口（XDevice_init_all / XDevice_poll）
│   ├── xdevice_uart.c/.h     UART0 调试串口（FIFO 直写 + 水位流控）
│   └── xdevice_oled.c/.h     SSD1306 OLED（绘制 + 增量上屏）
├── app/                    ★ 用户实现层
│   ├── user_business.c       业务模板（覆盖 user_setup/user_tick）
│   ├── XDeviceDisplay_ssd1306_i2c.c   OLED 底层 I2C 时序（勿改）
│   └── xgui_esp8266_ssd1306.h         XGui 裁剪底座
├── platform/               平台适配（与具体外设无关）
│   ├── stubs.c               C 运行时转发 + lwh 独立堆 + 临界区 + MultiPool 钩子
│   ├── XDeviceNetwork_lwip_esp8266.c  WiFi↔lwip 胶水（esp2glue 契约）
│   ├── lwipopts.h            lwip 2.2.1 配置
│   ├── sync_baremetal.c      裸机同步原语
│   ├── cre.S                 cache 建立汇编（ROM Cache_Read_Enable 绝不可调）
│   ├── rf_cal_stub.c / partition_stub.c  NONOS3 必需回调
│   └── xdevice_random.c      硬件 RNG 平台实现（XRandomGenerator 依赖）
├── cfg/                    裁剪档案（CFG，-include 强制包含）+ 源清单 + shim
├── tools/                  构建管线（build_fw_final.sh / recompile / rodata 后处理）
├── fw_final.bin            最新固件
└── fw_final_verified.bin   2026-10-22 已验证基线（黄字黑底标题栏 + 0% 丢包）
```

### 新增一个外设（两步）

1. `drivers/` 下建 `xdevice_xxx.c/.h`，实现 `bool xdevice_xxx_init(void)`；
2. 在 `drivers/xdevice_init.c` 的 `XDevice_init_all()` 里按正确顺序加一行调用。

`main.c` 永远只写 `XDevice_init_all();`，不需要知道有哪些外设、谁先谁后。

> ⚠️ 顺序硬约束：UART 必须最先（后面所有日志靠它）；OLED earlyInit 必须在
> WiFi/lwip 之前（HSPI 会抢占 GPIO12/14，SDA 被钉低 → 屏永远不亮）。

## 内存架构（80KB DRAM 分配）

```
.data+.bss ~59KB（含 arena 18KB = XVariablePool，框架动态分配）
lwh 独立堆 21.4KB（os_malloc：lwip 池 16.3K + SDK wifi ~5K）
SDK 顶部保留 16KB（0x3FFFC000+）
```

**配比铁律**：arena ≥ 框架峰值 15,972 + 2K；lwh ≥ lwip 16.3K + wifi 5K；
两者总预算 ~39.4KB 恒定。arena 17KB 实测缺 1KB wifi 启动余量（Fatal 28），
14KB 连 `wifi_softap_get_config` 都挂死。

## 已知行为

- **station 重新关联**偶发一次 ASSERT 挂 ~6s → WDT 复位自愈（esp 胶水陈旧
  回调，真修需 lwip-esp.o builder 源码）；自愈后 WiFi 恢复。
- **PC 静态 IP 接入**：`192.168.4.2/24`（DHCP 未实现，`dhcps_start` 是空桩；
  可选后续：胶水层实现最小 DHCP 响应器）。板复位后 PC 需先
  `netsh wlan disconnect` 再 connect（旧关联不自动重认证）。
- **Wi‑Fi「不通」先查 PC 侧**：烧录重启后 ping 不通、ARP 邻居 MAC 全 0，但
  `netsh wlan show interfaces` 显示已连 XinYueAP —— 板子正常（串口
  `station: ... join, AID = 1`、`rxReq==rxOk`、`allocFail=0`），是 Windows
  持有旧关联。标准处置：`netsh wlan disconnect` → 等 4s →
  `netsh wlan connect name=XinYueAP ssid=XinYueAP`。实测 30/30 0% 丢包。
- 泄漏取证钩子（XPB/XTAKE/XVR，~1KB .bss）保留供浸泡观测，稳定后可裁
  （`XDeviceNetwork_lwip_esp8266.c` 的 XTAKE/XVR 段 + `XVariablePool.c`
  的 XVR 环）。

## ⚠️ OLED 上屏三坑（2026-10-22 定案，改这块屏前必读）

1. **构建没产出新 bin 时，屏幕当然不变**。`build_fw_final.sh` 依赖
   `_obj/filelist_final.txt`（275 行）；它若为空/被截断，链接只剩 33 个 obj
   必然失败。**每次构建后必须确认 `== objs: 275+ ==`、`== link OK ==`、
   `fw_final.bin` 时间戳更新**，否则后面所有调试都是假的。
2. **上屏靠 `gui_oled_pump()`（每拍发一页）**，`gui_oled_draw()` 只写
   `s_fb`。串口必须看到 `[i2c] step pg0..pg7 done`（8 条）才算真上屏；
   零条 = 面板上还是开机 testPattern。开机图样已从"棋盘"改为
   "白条+外边框"——旧棋盘在黄蓝屏上长得就像故障条纹，会误导排查。
3. **I2C 总线楔死会自愈了**：驱动内置 `i2c_bus_unlock()`（9-clock + STOP），
   烧录/看门狗打断半截事务后不再需要断电。若串口仍报
   `bus unlock FAIL: SDA still low`，那才是真硬件问题。

**标题栏形态（用户裁定）**：整屏黑底 + 黄区（行 0–15）点亮 "XinYueC" = 黄字黑底；
蓝区（行 16–63）点亮矩形/对角线 = 蓝线。**不要画"白条黑字"**。

## 用户业务接入（模板 API）

业务代码写在本目录 `user_business.c`，覆盖两个弱符号即可，**构建脚本自动
编译入库**（无此文件时用 main.c 里的空弱实现，固件照常构建）：

```c
void user_setup(void);  /* 事件循环启动【前】调用一次：业务对象/定时器/客户端初始化 */
void user_tick(void);   /* 事件循环每拍（~5ms）调用：只做非阻塞轮询 */
```

调用时机（main.c）：`user_setup()` 在 XCoreApplication/shell/WiFi 全部就绪后、
重复泵启动前；`user_tick()` 在每拍事件处理尾部。勿在 tick 里长阻塞。

## 历史文档

- `../docs/PROGRESS.md` — 操作手册（硬件档案/构建铁律/踩坑全记录，**当前状态看这个**）
- `../docs/HANDOVER_2026-10-20.md` — 2026-10-20 排查过程存档（已归档）
