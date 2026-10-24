# ESP8266 HW-364A 移植操作手册（XinYueC）

> 目标形态: XinYueC 事件循环 + 串口 shell + WiFi(库lwip) + OLED(GME12864-43)。
> 本文件是**按时间倒序的进度流水**（最新在最前）；**踩坑的教训库见
> [PITFALLS.md](PITFALLS.md)**（A 镜像构建 / B 驱动硬件 / C 框架移植 /
> D 调试方法论 / E 工具链），查问题先查它。

## 📌 当前状态速览（2026-10-23 深夜收官）

| 项 | 状态 |
| --- | --- |
| **框架原生事件循环** | ✅ **已跑通**：`main.c` 每拍无条件 `XCoreApplication_processEvents(0)`，由框架自己的时间轮驱动 CoarseTimer；`notifyInput` 手工投事件变通**已删除**（arena churn 随之消失，内存纹丝不动） |
| **平台时基** | ✅ 有效：`XDateTime_currentMSecsSinceEpoch()` 返回单调毫秒（与桌面 `GetTickCount64` 同语义） |
| **WiFi/SoftAP** | ✅ 开启（`XESP8266_NET_ON=1`）：XinYueAP / 192.168.4.1，ping **2–5% 丢包**（丢在 PC 重关联瞬间），板端 `rxOk==rxReq`、`lwhFail=0`、零泄漏 |
| **串口 shell** | ✅ 12 命令可用（help/version/echo/mem/info/uptime/tasks/clear/reset/reboot/exit/history…） |
| **OLED** | ✅ 黄字黑底标题栏 + 蓝区框线，`i2cErrs=0` |
| **RAM** | 80KB 用户区切满：`.bss` 61,096B（arena 19,304 在用 15,988 / MultiPool 3,168 在用 ~204 / cont 栈 6,144）+ lwh 19,424B |
| **Flash** | app 443KB / 4MB（余 ~3.4MB） |
| **线程** | 裸机关线程（`XTHREAD_ON=0`），TLS 退化为全局槽 |
| ⚠ 遗留 | ① cont 栈 6,144/6,144 触顶（零余量，峰值=processEvents 深链）② lwh 水位计不可信（胶水改写块头），真判据用 allocFail ③ 静止期偶发一次无 Fatal 复位（疑 lwh 坏链，胶水深水区） |

**接手前必读**：[PITFALLS.md](PITFALLS.md) A1/B1/C2（本轮修的三处真 bug）
与 §0-Y.6–0-Y.7（框架定时器与 lwh 配比两案的完整证据链）。

---

## 0-Y. 2026-10-22 OLED「黄蓝条纹」误判案 —— 必读防复发（最新）

> 这一节是**流程性事故**的完整复盘：连续多轮"改了没效果/回滚也不行/屏幕还是条纹"，
> 真实原因是**构建根本没产出新固件 + 面板上一直是我自己的诊断图样**，不是渲染 bug。
> 任何人改这块板之前先读本节。

### 0-Y.1 三个叠加根因（全部有串口实证）

| # | 根因 | 证据 | 教训 |
|---|---|---|---|
| 1 | **`_obj/filelist_final.txt` 被截断成 0 字节**（一次实验里 `: >` 清空后没重建）。`build_fw_final.sh` 第 226 行按它生成对象清单 → 链接只剩 33 个 obj → `undefined reference to XCoreApplication_create_ex...` → **链接必挂**。`set -e` 让脚本在链接前就退出，日志尾部无新 `fw_final.bin` 时间戳。 | `build_minimal.log` / `build_pumpfix.log`：`== objs: 33 ==` + `collect2.exe: error: ld returned 1 exit status` | **每次构建后必须看三行**：`== objs: <275+>`（不是 33）、`== link OK ==`、`fw_final.bin` 时间戳变新。任何一条不符 = 没烧到新固件，别去查代码。 |
| 2 | **增量上屏 `flush_step()` 从未发出一页**。`gui_oled_draw()` 只把内容画进 `s_fb` 并置脏页，真正发 I2C 靠 `gui_oled_pump()`（app.c 每拍调）。历史**全部** cap 日志里一条 `[i2c] step pgN done` 都没有 → 标题栏永远停在 RAM 里，面板显示的其实是开机 `earlyInit` 刷的 **4×16 棋盘 testPattern**。 | 所有 cap_*.log grep `step pg` 零命中；加上修复后 `cap_titlebar_boot.log` 立刻出现 `step pg0..pg7 done` | **诊断图样必须与故障形态可区分**。棋盘在黄蓝双色屏上的观感就是"黄蓝条纹+矩形方框"，与总线楔死的断电残留肉眼无法区分——用户多次把它当成"故障依旧"。testPattern 已改成「顶部白条+外边框」，垃圾纹长不出这种形状。 |
| 3 | **I2C 总线楔死无自愈**。烧录复位/看门狗打断半截 I2C 事务 → SSD1306 从机停在等时钟状态、把 SDA 钉在低电平 → 此后每次 boot `twi_start()` 都 `SDA stuck low` 拒绝发起 → init 全线 NACK（`i2cErrs=1100`），**只有整板断电能解**。旧代码失败后只重试 5 轮，从不解锁总线，5 轮全是徒劳。 | `cap_title_retry.log`：`probe start refused (SDA stuck low)`、`init NACK=27`、8 页全 `nack=393` | 驱动已加 `i2c_bus_unlock()`：**标准 9-clock recovery + STOP**，接在 `earlyInit` 与 `init` 的重试路径上。以后烧录/复位打断 I2C 不需要断电。 |

### 0-Y.2 标题栏形态（用户裁定，勿再改）

**黄字黑底**：整屏黑底；行 0–15（黄区）放**点亮**的 "XinYueC"（x=36 居中，8×16 点阵 = 56px 宽）；
行 16–63（蓝区）黑底 + **点亮**的矩形框 (2,22,124,20) + 对角线 → 蓝色线条。

- 点亮像素在黄区发黄光、蓝区发蓝光；熄灭=黑底。**"白条黑字"是错的**（曾误做一版）。
- `gui_oled.c` 画法：整屏 `XPainter_fillRect(0xFF000000)` + `XPainter_drawText(..., 0xFFFFFFFF)`
  + `XPainter_setPen(0xFFFFFFFF)` 画框线（默认画笔是黑，黑底上会隐形）。
- 黑底比整屏点亮省电数倍。

### 0-Y.3 发布前检查清单（每次改这块板逐条过）

```sh
# 1) 构建：必须看到 objs >= 275、link OK、bin 时间戳更新
bash tools/build_fw_final.sh 2>&1 | grep -E "objs:|link OK|merged"
ls -l fw_final.bin

# 2) 烧录：不要带 --port（板在 COM2/COM4 漂移，指错口 esptool 会报
#    假的 "Must be pairs of an address and the binary filename" 错）
python -m esptool write-flash 0x0 fw_final.bin      # 注意用 & 分隔命令，勿用 ;

# 3) 串口验证三条硬指标（缺一即未上屏）
python capture.py COM2 74880 40 cap.log
grep -c "step pg" cap.log        # 必须 = 8
grep "i2cErrs=" cap.log          # 必须全 0
grep "init NACK=" cap.log        # 必须 = 0
```

### 0-Y.4 ⛔ `XMP_GLOBAL_INIT_CUSTOM` 钩子在本平台不可用（2026-10-22 实证）

**症状**：`poolTotal=3168`（档位/块数全对）、`sdk_free` 平稳、`varPool` 平稳，
但 lwip 收包 **100% 失败**：`rxReq` 一直涨、`rxOk=0`、`allocFail` 与 `rxReq`
同步涨；PC 侧 ping 100% 超时（板端收得到包，但分配不出 pbuf）。

**对照**：同一份 `stubs.c`/`lwipopts.h`/`XDeviceNetwork_lwip_esp8266.c`、
同一 `C256=4`，**只要不定义 `XMP_GLOBAL_INIT_CUSTOM`**（走 XMultiPool.c 内
框架默认装配），立刻恢复 `rxReq==rxOk`、`allocFail=0`、ping 19/20。

**结论**：钩子路径与默认流程在子池挂接上存在未定位差异（钩子内
`enable_power_of_two_mode` + 逐档 `XFixedPool_create`/`add_pool` 看似等价，
但实测不等价）。**在差异查清前，CFG 里保持 `XMP_GLOBAL_INIT_CUSTOM` 注释
状态**；`XMultiPool.c` 与 `stubs.c` 的钩子代码保留备查。

> 相关教训：`XMP_GLOBAL_C256` 必须为 **4**，不能砍。砍成 0 时 132/152B 的
> lwip 收包请求无池可落且不回退，症状同上（`allocFail` 全灭）。

### 0-Y.4b 平台差异要写进源码条件编译，不要用假头 shim

**背景**：`Drive/Espressif/ESP8266/Core/XAtomicLibcall_esp8266.c` 提供 GCC
`__atomic_*` libcall 实现（ESP8266 无原子指令），必须参与编译。它原先**无条件**
`#include "freertos/FreeRTOS.h"`，裸机工程只能在 `cfg/shim/freertos/` 下伪造两个
假头（空 `FreeRTOS.h` + 把 `taskENTER_CRITICAL` 宏映射到 `vPortEnterCritical`）
才能编过。

**问题**：把平台差异藏进构建脚手架——任何人看到 `shim/freertos` 都会以为
「是不是又启用 RTOS 了」，而且假头与真实 FreeRTOS 头同名，排查时极易误导。

**正解（已实施）**：差异回到源码里，用框架现成的平台宏分支：

```c
#include "CXinYueConfig.h"          /* XPLATFORM_FREERTOS / XPLATFORM_BAREMETAL */

#if XPLATFORM_FREERTOS
  #include "freertos/FreeRTOS.h"    /* SDK 要求先于 task.h */
  #include "freertos/task.h"
  #define XATOMIC_ENTER_CRITICAL()  taskENTER_CRITICAL()
  #define XATOMIC_EXIT_CRITICAL()   taskEXIT_CRITICAL()
#else
  /* 裸机：直接关中断，不引入任何 FreeRTOS 头 */
  void vPortEnterCritical(void);
  void vPortExitCritical(void);
  #define XATOMIC_ENTER_CRITICAL()  vPortEnterCritical()
  #define XATOMIC_EXIT_CRITICAL()   vPortExitCritical()
#endif
```

于是 `cfg/shim/freertos/` **整个目录已删除**，裸机工程不再需要任何 FreeRTOS
相关头文件或 shim。构建验证：312 obj、link OK、450,945B；真机 `rxReq==rxOk`、
`allocFail=0`、ping 30/30 0% 丢包。

**通用原则**：跨平台代码用 `XPLATFORM_*` 分支；**不要**在平台目录里放与真实
头文件同名的空壳来"骗过"编译器——那会让平台差异隐身。

### 0-Y.4c 同一原则的第二处：`XSerialPortESP8266.c` 与 `cfg/shim/uart.h`

**症状**：`Drive/Espressif/ESP8266/Device/XSerialPortESP8266.c` 一直编不过
（`unknown type name 'UART_Port'`），所以构建脚本靠"存在性守卫"跳过它、
直接用旧机预编译的 `.o`。换机后旧 `.o` 一旦丢失就彻底无法构建。

**两层根因（都已修）**：

1. **`cfg/shim/uart.h` 是空壳**（内容只有一句注释）。驱动需要
   `UART_Port / UART_WordLength / UART_StopBits / UART_ParityMode` 四个枚举，
   本机 Arduino SDK 与 nonos_v305 都**没有** `uart.h`。
   → 已把 SDK 原始枚举名补进该 shim（`UART_WordLength_8b`、
   `USART_StopBits_1`、`USART_Parity_None` 等，值对应 CONF0/CONF1 寄存器编码）。
   驱动里 `UART_*` 函数是寄存器直写自实现的，只需类型、不需函数原型。

2. **`cfg/shim/esp8266/uart_register.h` 是空壳**，而驱动写的是
   `#include "esp8266/uart_register.h"`（RTOS SDK 布局），本机平面 SDK 的
   `uart_register.h` 在 `include/` 根下。
   → 驱动已改为 `__has_include` 自动选路：
   ```c
   #if __has_include("esp8266/uart_register.h")
   #  include "esp8266/uart_register.h"
   #else
   #  include "uart_register.h"
   #endif
   ```
   该 shim 空壳已删除。

**另外删掉一个真正的垃圾文件**：`cfg/shim/uart_register.h` 内容竟是
`404: Not Found`（14 字节，当初网络下载失败留下的），它遮住了 SDK 的真头。

**验证**：`XSerialPortESP8266.c` 现在**本地真编译**（12,240B .o，19:51），
不再依赖旧机产物；整机构建 312 obj、link OK、450,945B；真机
`rxReq==rxOk`、`allocFail=0`、ping **25/25 0% 丢包**。

### 0-Y.4d 构建脚本的"存在性守卫"已全部废除（同一事故的第三层）

**旧写法（错误）**：
```sh
[ -f obj/xxx.o ] || $TC/xtensa-lx106-elf-gcc ... -o obj/xxx.o
```
语义是「.o 存在就永不重编」。由此引出四个连环坑：

1. 源文件改了**不生效**（脚本按 mtime 跳过是另一套逻辑，本地 TU 根本没走）；
2. 上一次编译失败留下的旧 .o、或别的机器拷来的 .o，被当成有效产物；
3. `XSerialPortESP8266.c` 因头文件缺失编不过 → 守卫让它一直用旧机 .o →
   **脚本不报错、构建照样绿**，问题被掩盖到换机时才爆；
4. 任何人想验证"能不能从源码构建"都得手动删 .o。

**现在的规则（`build_tu()` 函数，2026-10-22 重写）**：

| 条件 | 行为 |
|---|---|
| `.o` 不存在 | 编译 |
| `.o` 比源文件旧 | 编译 |
| `.o` 比全局档案 `CFG` 旧 | 编译（CFG 是 `-include` 强制包含，改它等于改所有 TU） |
| 以上都不满足 | 跳过 |
| **编译失败** | **打印 `!! 编译失败: <file>` 并 `exit 1`**，绝不静默留旧产物 |

同时给每个 TU 打印 `CC <src>` / `AS <src>`，构建日志一眼能看出谁被重编了。

**三条实测验证**：
- 删光 `obj/*.o` 后构建 → **13 个本地 TU 全部现场编译**，312 obj、link OK；
- 原样再构建一次 → **零编译**，直接链接（增量正确）；
- 改 `drivers/xdevice_uart.c` 一个字节 → **只重编它一个**，其余 12 个跳过；
- 故意写入语法错误 → 打印 `error:` + `!! 编译失败` 并退出（不再掩盖）。

**结论：模板现在完全由源码构建，不依赖任何预编译 .o。** 唯一的例外是
`_obj/` 里的 275 个**框架** .o（`Src/**`，非模板代码）——它们由
`recompile_final_one.sh` 全量重编，换机后需要旧机的 SDK 头环境（见 README
「全量重编注意」）。模板自身代码（main/drivers/app/platform）已无此依赖。

### 0-Y.6 事件循环时序与框架定时器（2026-10-22 专项排查）

**问题**：用户看到 `os_timer_arm(&lwip_tmr, 5, 1)` 以为事件循环 5 秒才跑一次，
并指出"我原先的框架设计也不是这样的"。

#### 结论 1：5 是毫秒，不是 5 秒

SDK 的 `os_timer_arm(a, b, c)` = `ets_timer_arm_new(a, b, c, 1)`，末参 1 表示
**b 以毫秒计**。所以：

| 定时器 | 周期 | 作用 |
|---|---|---|
| `lwip_tmr` | **5ms** | 泵 `sys_check_timeouts()`（ARP 老化/TCP 重传/ICMP） |
| `s_pumpTmr` | **5ms** | 事件循环一拍 = 200Hz |
| 统计打印 | 5s | 1000 拍 × 5ms（用户看到的"5s"是这条） |

#### 结论 2：框架的定时器在 ESP8266 上**从未工作过**（根因）

框架定时器分两层：精确定时器 → 调度器 `m_hrtimerGroup`（红黑树）；
普通/粗定时器（CoarseTimer）→ 全局 `XTimeWheelGroup`（时间轮，1ms 刻度）。
时间轮的高分辨率时基是 `XDateTime_currentMSecsSinceEpoch()`
（`XTimeWheelGroup.c:673` 的 `setHighResTimeFunc`）。

而 ESP8266 平台桩里该函数**返回 -1**（"不可用"，
`Drive/Espressif/ESP8266/Device/XPlatformStub_esp8266.c`）：

> 时基恒为 -1 ⇒ 时间轮永远算不出"过了多久" ⇒ **CoarseTimer 永不到期**
> ⇒ 框架定时器（含 `XConsoleShell_startAsync` 注册的 10ms 串口轮询定时器）
> 在 ESP8266 上**一次都没触发过**。

这就是 NONOS 移植版为何绕开框架、自己每 5ms 手动
`XConsoleShell_notifyInput()`：框架的定时器路径不工作，只能手工投事件顶上。
代价是**每 5ms 无条件 `XEvent_create` 一个事件**（哪怕 UART FIFO 是空的），
每秒 200 个 → arena churn（当初实测 768B/5s）→ 几分钟后耗尽 → "WiFi 变哑"。

框架本来的设计是：CoarseTimer 到期 → `VXConsoleShell_timerEvent` →
`xcs_async_read_ready()` → **直接读串口，不分配任何事件**；只有真读到数据
才用 `notifyInput` 续读（`XConsoleShell.c:136-138`）。零分配。

#### 结论 3：补上时基后，框架定时器路径**首次运行即崩溃**

已在平台层补上单调毫秒时基（1ms `ets_timer_arm_new` 累加，
`XPlatformStub_esp8266_timeInit()`）——绝对时间（currentDateTime）依旧不可用
（板上无 RTC/NTP），但时间轮只需要增量。

**但**：把 `main.c` 改成"每拍都调 `XCoreApplication_processEvents(0)`"后，
板子立刻进入崩溃循环：

```
Fatal exception 0(IllegalInstructionCause):
epc1=0x40201013, excvaddr=0x402663dc
```

- `epc1` 落在 `XAtomic_exchange_bool`（0x40201010+3）——跳进函数中段；
- `excvaddr` 指向 `memp_SYS_TIMEOUT` 一带的**数据符号**
  → 典型的**野指针 / 坏 vtable**（某对象的 vtable 指到了数据区）。

**三项排除实验**（都做了）：

| 假设 | 实验 | 结果 |
|---|---|---|
| cont 栈不够深 | 4KB → 8KB（`-DCONT_STACKSIZE=8192`，main.c 与 stubs.c 两处同步） | **无效**，崩溃点完全相同 |
| 调用频率过高 | 「每拍调 processEvents」vs「有输入才调」 | **都崩** ⇒ 与频率无关 |
| 时基实现本身有毒 | 「有时基 + 有输入才调」 | **也崩** ⇒ 触发因素是**时基让 CoarseTimer 真正到期** |

**最终结论**：不是"每拍调用"的问题，而是**框架的定时器/事件派发路径在 ESP8266
上从未被运行过**（此前因缺时基而全哑）。一旦拿到可用的时基，CoarseTimer 真的
开始到期，就走进一条 ESP8266 上从未跑过的分支——该分支里有未初始化的东西
（候选：时间轮惰性单例的 vtable 手工赋值
`XClassGetVtable(group) = XTimeWheelGroup_class_init();`，在 32 位裸机下与
桌面 64 位行为不一致）。

**当前处置**（2026-10-22 更新）：时基实现保留在平台层
（`XPlatformStub_esp8266_timeInit()`，1ms `ets_timer_arm_new` 累加，实测推进正常：
1000 拍 = 5000ms 时平台计数 5422ms，SDK 定时器 tick 偏快约 8%），
但 `XDateTime_currentMSecsSinceEpoch()` **回退为返回 -1**（时基保持"不可用"），
`main.c` 维持"有输入才 processEvents + notifyInput 变通"的稳定形态。
板子已验证：无崩溃、WiFi 正常、屏幕正常。

**桌面端复现方案**（已落地为探针 + 工作流）：
- 探针 `Test/XGuiTest/x_timer_coarse_repro.c`（独立 exe，链接框架库）：
  建 XCoreApplication → 注册 10ms CoarseTimer → 反复 `processEvents(0)`，
  打印定时器触发次数；
- 桌面跑同一场景：**崩 → 纯框架 bug，可 gdb 直接定位；不崩 → 与 32 位裸机路径相关**；
- 已接入根 `CMakeLists.txt`（从 `Test/*.c` glob 剔除以避双 main，单独
  `add_executable(x_timer_coarse_repro)`，并进强制包含配置头的目标列表）。

#### 桌面端复现结果（2026-10-22 实测，结论：桌面不崩）

| 项目 | 结果 |
| --- | --- |
| 探针退出码 | 0 |
| 是否崩溃 | **否** |
| 定时器是否真的到期 | **是**，`fired=197`（200 轮 × 5ms sleep） |
| 日志 | `out/logs/probe_run3.log` |

⇒ **这个缺陷是 32 位 / 裸机（无 OS）特有的**，桌面 64 位 Windows 路径健康。
后续排查应聚焦"只有 32 位裸机才会走到的分支"，而不是"框架通病"。

**探针自身的一个坑（已修）**：初版 2000 轮 `processEvents(0)` 全速跑完
不足 1ms，10ms 的 CoarseTimer 一次都没到期 → `fired=0`，看起来像"没驱动"。
且初版把可执行文件路径写成 `build_desktop\bin\Release\...`（实际输出在
`bin\Release\`），exe 找不到 → 脚本异常退出码 1 → 被误判成"崩溃"。
现在探针每拍之间 `sleep(5ms)`，路径已修正。

#### 独立复核（主代理侧，2026-10-22）

已**排除**的几条嫌疑（都有实证）：

1. **"虚表数据被搬进 flash 导致槽位写不进去"** —— 不成立。
   `fw_final.map` 里所有 `vtable_data$N` 符号都在 `.bss.*` 段（DRAM），
   不在 `.rodata`。`XVTABLE_STACK_INIT` 建的 `static void* vtable_data[Size]`
   是可写的，未受 `move_rodata_to_flash.py` 影响。
2. **"缺框架 TU 导致弱符号/空槽"** —— 不成立。
   `platforms/esp8266/nonos/cfg/filelist_final.txt`（275 条）里 XClass/XObject/
   XCoreApplication/XEvent/XTimer/XTimeWheelGroup/XHrTimerGroup 全部在列。
3. **"lwip `memp_pools` 指针数组被搬进 flash 后失效"** —— 不成立（一度误判，
   已自我纠正）。`memp_pools` 在 0x402663e4，元素为
   `0x4026640c, 0x4026640a, 0x40266408, ...`（间隔 2 字节看着像坏指针，
   其实是**正常**的）：ESP8266 的 lwip 走 `MEM_LIBC_MALLOC`，
   `struct memp_desc` 只剩 `u16_t size` 一个字段（2 字节），
   所以各描述符对象自然相隔 2 字节，数组里存的正是它们的地址。
   `readelf -r` 显示整个 ELF **无重定位**，指针是链接期定死的绝对地址，
   而 ESP8266 应用恒定加载在 0x40200000，绝对地址有效。

崩溃点反汇编（`out/logs/crash_dis_01010.log`、`crash_dis_66404.log`）：

```
40201010 <XAtomic_exchange_bool>:      ← 注意：这是 .irom0.text 的【段首】
40201010: f0c112   addi a1, a1, -16
40201013: 3109    s32i.n a0, a1, 12      ← epc1 落在这里
4020101d: 000b45  call0 402010d4 <__atomic_exchange_1>
...
40266404 <memp_SYS_TIMEOUT>:            ← excvaddr 落在这里（.rodata 数据符号）
```

固件段布局（`objdump -h`）：

| 段 | VMA | 大小 | 说明 |
| --- | --- | --- | --- |
| `.text` | 0x40100000 | 0xE3 | DRAM 代码 |
| `.text1` | 0x401000E4 | 0x64A0 | DRAM 代码 |
| `.irom0.text` | **0x40201010** | 0x600F0 | flash 代码（段首 = `XAtomic_exchange_bool`） |
| `.rodata` | 0x40261100 | 0x666C | flash 常量（`.rodata` 被搬进 flash） |
| `.data` | 0x3FFE8000 | 0x570 | DRAM |
| `.bss` | 0x3FFE8570 | 0xF740 | DRAM |

未决：`epc1` 是一条**往栈帧写寄存器**的指令（`s32i.n a0, a1, 12`），
`excvaddr` 却指向 `.rodata` 的数据符号。若 `a1`（栈指针）算错而落到
flash 段，这条写就会打到 flash → 触发异常。这与"32 位裸机特有"吻合，
但**尚未证实**（`a1` 为何会错、谁传的坏栈基址，都还没有直接证据）。

#### 【收官 2026-10-22 深夜】真根因：两个工程 bug，框架无罪

上面的所有"框架嫌疑"（vtable 错位 / 裸机分支 / 栈坏）**全部被真机证据推翻**。
真根因有两个，都在**工程脚手架**里：

**根因 A：`fix_rodata_pos.py` 把镜像长度+CRC 写进了代码段**（崩溃的真凶）

- 该脚本重排镜像后把 4 字节长度 + 4 字节 CRC 写到固定偏移
  `CSZ_OFF=0x1010 / CVAL_OFF=0x1014`——**ESP8266 app image 在 byte 16 处
  根本没有这个字段**（那是 ESP32 extended header 约定；eboot 只认镜像
  尾部的单字节 checksum）。
- byte 16 起就是第一段的数据。`.rodata` 并入 irom0 后第一段 =
  `.irom0.text`，其开头 8 字节恰是 `XAtomic_exchange_bool`（.irom0.text
  段首函数）的入口指令——**被长度+CRC 覆盖**：
  ```
  ELF:  12 c1 f0 09 31 30 30 74   （addi a1,a1,-16; s32i.n...）
  BIN:  21 d0 06 00 bd 4d 52 66   （= 0x06D021 镜像长度 + CRC！）
  ```
- CPU 从 0x40201010 取指拿到垃圾（第 4 字节 0x00 = 非法指令）→
  `Fatal exception 0(IllegalInstructionCause)`，epc1 恒为 0x40201013。
  excvaddr 是残留寄存器值，随每次布局漂移（0x40266404 → 0x40266460 →
  0x402645e2）——这就是它看起来"指向不同数据符号"的原因。
- **为何以前从未暴露**：时基 -1 → 时间轮全哑 → 没有任何执行流走到该函数
  → 假阴性。补上时基让 CoarseTimer 真正到期后，时间轮 handler 第一次
  调 `sweep_cancelled_timers → XAtomic_exchange_bool`，取指即崩。
  「崩在 WiFi 初始化 / shell 初始化」全是同一取指损坏的错位读法。
- **修复**：删掉这两个字段的写入（`fix_rodata_pos.py` 尾部）。
  修复后 `bin[0x1010..]` 与 ELF 逐字节一致，崩溃消失。

**根因 B：`XSerialPortESP8266.c` 的 UART 帧格式位全部写错位**（命令乱码）

- `UART_SetWordLength` 把字长写进 `UART_CONF1[1:0]`——手册上字长
  `UART_BIT_NUM` 在 **CONF0[3:2]**；CONF1 是 RX FIFO 阈值，写它完全无效。
- `UART_SetStopBits` 写 CONF0[14:13]——手册在 **CONF0[5:4]**。
- `UART_SetParity(None)` 清 BIT(12)——`UART_PARITY_EN` 在 **bit1**。
- `UART_ResetFifo` 给 CONF0 OR 臆造的 `TX_EN/RX_EN/OUT_EN`（bit0/1/6——
  实为 PARITY 选择/使能等位），进一步污染帧格式。
- 真机现场（诊断读回）：`clkdiv=1068`（74880 ✓）但 `conf0=0x79`
  （7 位数据+停止位错码）。PC 发 8N1，板子按 7 位解析 →
  `version\n`（76 65 72 73 69 6f 6e 0a）收到 `76 59 ae a5 6f 4b`。
- **修复**：四个函数按手册重写（CONF0[3:2] 字长、CONF0[5:4] 停止位、
  CONF0[1]/[0] 校验使能/选择、ResetFifo 只动两枚复位位）。
  修复后收到 `76 65 72 73 69 6f 6e 0a` 逐字节正确，`version` 命令
  正确响应 `XinYueC XConsoleShell`。

**框架侧的真实缺陷（顺带修复，非崩溃原因）**：

1. `XTHREAD_ON=0`（裸机关线程）时四个文件编译/链接失败——
   `XTask.h/.c`（线程注册表与 XThread 硬耦合，无条件声明/定义）、
   `XConsoleShellTasks.c`（无条件调 `XTask_enumerateThreads`）、
   `sync_baremetal.c`（XThread 对象族无守卫）。均已加 `#if XTHREAD_ON`
   守卫；`XTaskInfo/XTaskState/XTaskVisitor` 平台无关契约保留。
2. `recompile_final_one.sh` 的增量只比较 `.o` 与源文件 mtime，**不比较
   CFG 档案**——改档案（如 XTHREAD_ON 0/1）后框架 TU 全部用旧 .o，
   链接期才爆。已补上与 `build_tu()` 同口径的 CFG mtime 检查。
3. `XCONSOLE_SHELL_ASYNC_OUTPUT_ON=0` 时异步模式命令输出只进内部缓冲
   **没人刷新**（flush 调用点被编译掉）——敲命令无回显无结果。ESP8266
   档案已开 `=1`。
4. 模板 main.c 注册的 7 个静态命令与 Shell 内建（help/version/echo/
   clear/reset/reboot/exit/history/mem/info/uptime/tasks 共 12 个）重名，
   `xcs_register_static_commands` 整批预检失败——模板改为不注册
   （内建已覆盖），避免误导。

**ESP8266 档案新开关**（cfg/xgui_shell_esp8266.h）：

| 开关 | 默认 | 作用 |
| --- | --- | --- |
| `XESP8266_NET_ON` | 0 | =0 时 `net_init` 只把 WiFi 置 NULLMODE、不泵 lwip 定时器（lwip 仍编译链接，SDK 库硬依赖其符号） |
| `XTHREAD_ON` | 0 | 裸机关闭线程对象（XThreadData 保留，TLS 退化为全局槽） |
| `XCONSOLE_SHELL_ASYNC_OUTPUT_ON` | 1 | 异步模式命令输出刷新（框架默认 0 会吞输出） |

**最终真机验收**（`XESP8266_NET_ON=0` + 时基有效 + 每拍 processEvents +
XTHREAD_ON=0）：60 秒零 Fatal、零重启；时间轮到期回调真实触发（
`[tw] fire` 800 次/8s）；`help` 列出 12 命令、`version` 正确响应；
内存恒定（`varPoolUsed=13924` 不再增长——notifyInput 变通的
arena churn 随之消失）；OLED 零 I2C 错误。**框架原生事件循环在
ESP8266 裸机上正式跑通**。

**遗留**：
- `cont stackUsed=8192/8192` 打满（首拍框架初始化曾触及栈顶，未越界
  但无余量）——后续把首拍 OLED 重画移出 cont 栈或加大 cont 栈。
- `uptime` 显示"不可用"（Uptime 用绝对时间语义，板上只有单调毫秒）。
- WiFi/SoftAP 尚未回开（`XESP8266_NET_ON=1`）——回归网络时按
  "先开 net、再验 ping"的顺序逐步回开。

### 0-Y.7 shell RAM 裁剪 + WiFi 回归的 lwh 耗尽案（2026-10-23 深夜）

**shell RAM 裁剪（✅ 完成验收）**：关 `COMPLETION_ON`（省 ~1.9KB .bss 静态
候选表 + ~2KB 补全函数栈峰值）、关 `HISTORY_ON`（省 8×64B 历史行）、
`ASYNC_OUTPUT_CAPACITY 512→128`（省 384B）。实测 .bss 63,688→62,784
（-904B），flash 449,073→445,585（-3,488B）。**顺带修了框架开关守卫 bug**：
`xcs_redraw_input_line`（注释自述"历史回填与补全共用"）被包进
`XCONSOLE_SHELL_COMPLETION_ON` 块（497..1212 行）内，关补全即链接失败——
已在其前后闭合/重开守卫。shell 12 命令交互真机全绿。

**WiFi 回归撞上 lwh 耗尽（⛔ 未收官，下会话专项）**：`XESP8266_NET_ON=1`
回开后，SoftAP 关联正常、lwip 池初始化正常、**前 60+ 包收发零失败**
（rxOk==rxReq、allocFail=0、leakTotal=0），但持续流量下 `xesp_lwh_free`
跌到 **0** —— **lwh 堆耗尽** → pbuf 分配永久失败 → 丢包率 80-100%。

根因链：shell 裁剪把 .bss 缩了 904B → `_heap_start` 下移 → **lwh 从
21,408B（P0 定案验证配比）缩到 16,840B**。lwip 池常驻 ~16.3K + WiFi 启动
消耗（rf/phy/scan 的 os_malloc）≈8K+ ——16,840B 装不下，运行期收包空间
被挤没。基线版（28/30）只是流量少没跑到耗尽点。`lwhFree` 遍历乱值另证
lwh 块头被 esp 胶水越权改写（P0 定案警告过的深水区）。

**已做/已回退的实验（全部留档防重蹈）**：
| 实验 | 结果 |
| --- | --- |
| `alloc_for_recv` 前置 `drv_ensure_init` | **有害**：ensure 在 SDK 收包上下文半途而废（lwip_ready 已置位不重试）→ allocFail 100%；已回退 |
| 砍 lwip 池（PBUF_POOL 12→6 / TCP_SEG 8→2 / TCP_PCB 4→1 / PBUF 16→8） | 不够：省的 ~8K 被 WiFi 启动消耗填满，lwhFree 仍 0；参数保留（ICMP+ARP 场景合理） |
| arena +904 回锚 `_heap_start` 到基线值 | 部分改善（join 恢复、一度 rxOk==rxReq=62）但耗尽依旧 |

**下会话修复路径（按优先级）**：
1. **恢复 P0 配比 lwh≈21.4K**：从 .bss 再腾 4.6KB——候选：cont 栈 8K 减半
   （先把首拍 OLED 重画移出 cont 栈降真水位）/ 砍取证钩子（XPB/XTAKE/XVR
   静态表，浸泡验证已完成的可裁）。
2. 或修 esp 胶水越权写（需 builder 源码，P0 定案明示深水区）。
3. 修 `xesp_lwh_free` 遍历（size 字段被改写时累加乱值；需加块序校验）。
4. ⚠ PC 网卡（WLAN 89%）当晚状态劣化，ping 统计噪声大——下次 A/B 前先
   重启 PC 无线或换测法（板端 rxOk/allocFail 计数比 PC ping 可信）。

#### 0-Y.7.1 lwh 恢复落地（2026-10-23 深夜续，功能级验收通过）

按上表实施了三项，lwh 16,840→**18,904B**（+2,064）：

| 改动 | 收益 | 备注 |
| --- | --- | --- |
| `XPB_TRACE_ON=0`（cfg 档案） | 512B | RX pbuf 取证表；驱动注释自述"定位后移除"。⚠ `xpb_trace_dump` 被守卫包在 XPB 块内，main.c 的调用点需同步 `#if XPB_TRACE_ON`（同 redraw 型问题） |
| cont 栈 8192→**6144**（build_fw_final.sh 两处 CFLAGS） | 2,048B | **前提**：首拍 OLED 整屏重画从 console_init_once 移到 user_init（SDK SYS 大栈）——已做，console_init_once 内只剩框架初始化 |
| `xesp_lwh_free` 遍历加双重围栏（地址区间+next 单调递增+size 上界） | 观测可信化 | 仍会保守报 0（块头被越权写后提前断表）——lwhFree 已不可作水位计，只能当"链是否完好"的探针 |

**功能级验收（NET=1 全开）**：60 发 ping 58/60（3%，丢在关联瞬间）→
**100 发 ping 100/100（0% 丢包）**——对比修复前 50 发丢 47；shell `mem`
命令正常（XMultiPool 3168/232/7%）；`station join` 正常；
`rxReq=25 rxOk=25` 收发一致；`varPoolUsed=15988/19304` 恒定。

**遗留观测（不阻塞，下会话）**：
1. `[cont] stackUsed=6144/6144` —— 移出 OLED 后仍触顶（processEvents 深链
   或 shell 初始化是新峰值），零余量风险仍在；下一步需实测首拍后逐层
   水位或再挪重活出 cont 栈。
2. `lwhFree` 保守报 0（块头被越权写后遍历早停）——水位计失效，只能当
   "链完好性探针"；真实水位需在 os_malloc/free 记账（后续可加两个字段）。
3. 静止期偶发复位一次（ping 结束后、抓取前；无 Fatal 输出）——疑似 lwh
   坏链在后续 malloc 走飞，与胶水越权写同源，同属深水区专项。

#### 0-Y.7.2 XPB 宏传递漏洞补刀（2026-10-23 深夜续）

`XPB_TRACE_ON=0` 写进 cfg 档案后 512B 的 `g_xpbTrace` 仍驻留——**宏传递
漏洞**：`platform/XDeviceNetwork_lwip_esp8266.c` 由 lwip 段编译
（`CFLAGS_LW`，**无 `-include CFG`**），档案宏传不进去，驱动里的
`#ifndef→1` 默认值照常生效。修法=直接改驱动源码默认值（该文件 esp8266
专用，根 CMakeLists 明确排除 Espressif 路径，不影响桌面）。

**最终 .bss 61,096B**（基线 63,688 → 净省 **2,592B**），**lwh 19,432B**
（0x3FFF7418 起）。回归：60 发 ping 58/60（3%）、shell `mem` 正常
（XMultiPool 3168/204/6%）。

**.bss 大对象清单（nm top，防后人再找"还能裁什么"）**：
`xvariablepool_global_data` 21,580（=arena 本体，在用 15,988）/
`s_pumpCont` 6,184（cont 栈，水位触顶不可再砍）/
`s_tile` 2,048（OLED 绘图 tile，每拍在用）/ `g_ic` 1,704（SDK 内部）/
`s_fb` 1,024（framebuffer）/ `xvh_ring` 512（字库缓存）/
`g_deviceRegistry` 416 / `g_glyphMemo` 384 / `g_fdPool_data` 300 /
字体 LUT×3 ~800。**shell 实例仅 616B（s_staticShell）+40B（Io）**——
shell 侧已挖干（再抠 ASYNC_OUTPUT 64B + COMMAND_CAPACITY 16B，
无意义）。RAM 侧剩余的可挪空间只在 cont 栈（需先降真水位）与
arena 余量（3,316B，已是安全垫）。

#### 教训

- 平台层"返回 -1 表示不可用"这种保守桩，会让**依赖它的上层子系统整体静默**
  （定时器全哑），且症状远离根因（表现为"串口没反应"而不是"没时基"）。
  移植平台时应优先补齐**单调时基**（不要求 RTC）。
- 未经运行的代码路径 ≠ 可用代码路径。打开一条从未走过的路径前，
  预期它需要自己的初始化（`XTimeWheelGroup_global()` 惰性单例等）。
- include 顺序陷阱：`nonos_v305/include/c_types.h:106` 有
  `typedef unsigned char bool;`，与 `<stdbool.h>` 冲突
  （"two or more data types in declaration specifiers"）；
  同一 TU 里 SDK 头必须**先于**框架头引入，否则 c_types.h 会在 stdbool.h
  之后才被解析而报错。
- **`.bat` 必须是纯 ASCII**：cmd 按 OEM 代码页（GBK）读批处理，UTF-8 的中文
  注释会被当成命令 → `'绌烘牸鐨?rem' 不是内部或外部命令`。
  `out/x_cmake.bat` / `out/x_python.bat` 已全部改成 ASCII 注释。
- `cmd /c` 传**带空格的绝对路径**会被拆词（`'C:\Program' 不是内部或外部
  命令`）；要么整体加引号，要么走无空格的 `.bat` 包装器（工作流里用后者）。



**症状**：烧录/重启后 ping 192.168.4.1 超时或"无法访问目标主机"，ARP 显示
`Unreachable`、邻居 MAC 全 0，但 `netsh wlan show interfaces` 明明显示已连接
`XinYueAP`、信号 90%。**板子其实一切正常**——串口里能看到
`station: <PC MAC> join, AID = 1`、`rxReq=N rxOk=N allocFail=0`。

**根因**：板子复位后 AP 重启，PC 的无线驱动仍持着旧关联/旧密钥上下文，没有
重新走 4 次握手和 ARP，于是包发不出去。这是 Windows + MT7921 的常见行为，
**不是固件 bug**（底座固件同样如此，PROGRESS 旧版已记过一条）。

**标准处置（一次就好）**：
```bat
netsh wlan disconnect
timeout /t 4
netsh wlan connect name=XinYueAP ssid=XinYueAP
timeout /t 8
arp -a 192.168.4.1     :: 必须看到 f6-cf-a2-51-10-2d
ping -n 30 192.168.4.1
```
实测（2026-10-22 07:5x）：重连后立即 **30/30 回包、0% 丢包、3–5ms**。

**排查顺序（别倒着来）**：① `netsh wlan show interfaces` 看是否真关联；
② `arp -a` 看 MAC 是否学到（全 0 = PC 侧没通）；③ 串口 `rxReq/rxOk` 是否在涨
（在涨 = 板端收发正常，问题在 PC）；④ 才去查固件。
**静态 IP 仍是必需的**：`dhcps_start` 是空桩（`esp2glue_dhcps_start (server NOT started)`），
PC 必须手工配 `192.168.4.2/24`。测完恢复 DHCP：
`netsh interface ipv4 set address name="WLAN" source=dhcp`。

### 0-Y.7 本机 cmd 环境的坑（会伪装成程序错误）

- `python -m esptool ... ; echo` —— cmd 把 `;` 当参数分隔符塞进 argv，报
  `Address ";" must be a number`。**用 `&`，或写进 .py 脚本**。
- 多行 `python -c "..."` 经 cmd 会静默失败（无输出、无报错）→ 逻辑一律写 .py 文件。
- PortableGit bash 里 `python` 不在 PATH → `bash -c "export PATH=/d/anaconda3:$PATH; ..."`。

---

## 0-Z. 2026-10-21 换机部署 + P0 泄漏定案修复（先读 0-Y 再读本节）

> 换了台电脑，本节记录新机环境、随旧机丢失的资产、P0 泄漏的完整取证链
> 与**已真机验证的修复**。

### 0-Z.0 ✅ P0 泄漏已修复（2026-10-21 晚，真机验证通过）

**根因（三环取证 + 反汇编定案）**：esp 胶水层（lwip-esp.o）以 esp 私有布局
（ref@14 为 u16、type@12 为事件魔数 66）操作驱动递给它的 lwip pbuf，与仓库
2.2.1 布局（ref@14 为 u8）错位。发送/重试路径把 arena 块头写成 245+ 垃圾值
→ `pbuf_free` 永远减不到 0 → ICMP 应答 pbuf（92B 块）每 ping 漏一块，
arena ~140B/s 涨满 → 收包分配失败「WiFi 变哑」。修复前实测：160 发 ping
`≤128 档 leak +195`、varPool 15,972→36,596。

**修复（底座架构回归：lwip 移出 arena）**：
1. `stubs.c` 新增 lwh 独立 first-fit 堆（算法逐行取自底座 arduino_lwip），
   区间 `[_heap_start, 0x3FFFC000)`，对外提供 `os_malloc/os_zalloc/os_free`；
2. `lwipopts.h`：`MEM_CUSTOM_MALLOC/FREE/CALLOC` → `os_malloc/os_free/
   xesp_lwip_calloc`（lwip 整体离开 arena）；
3. `xgui_shell_esp8266.h`：`XVP_GLOBAL_ARENA_BYTES` 36KB→**18KB**（框架
   峰值 15,972 + 余量 2.4K；lwh 自适应 21.4KB，装 lwip 池 16.3K + wifi ~5K；
   ⚠️ arena 不得低于 17KB——17KB 实测缺 1KB wifi 启动余量，14KB 连
   `wifi_softap_get_config` 都挂死）；
4. `XDeviceNetwork_lwip_esp8266.c`：XTAKE 守卫——完成回调只对"本驱动交出
   且未收回"的指针执行 `pbuf_free`，SDK 陈旧/重复回调一律忽略（防御深度）。

**验证终版（cap_a18_verify / cap_soak10min / ping_restore2）**：
- 420 秒 + 2 次 station join（此前必然 ASSERT 挂死的场景）+ 360 发 ping：
  **360/360 0% 丢包**、varPool 15,972/18,400 全程纹丝不动、ASSERT/复位/
  allocFail/TX FAILED 全 0；
- 200 发连续 ping 复测：**200/200 0% 丢包**；
- 固件 `build/arduino_final/fw_final.bin` 450,164B，
  `_heap_start=0x3fff6c60`（lwh 堆 21,408B）。

**已知残留（勿浅尝）**：station 重新关联时偶发一次 esp 胶水陈旧回调 →
`ASSERT: pbuf_free: p->ref > 0` → 挂 ~6s → WDT 复位自愈（间歇性，10min
一遇级别，自愈后 WiFi 恢复；360/360 那轮未触发属运气）。ref0 守卫"修复"
已实证失败并回滚（join 期 SDK 完成事件合法但 ref 已被 SDK 侧清零，跳过
free 反而使 join 分配永久泄漏 → allocFail 全灭）。真修需 lwip-esp.o 的
wrapper 生命周期分析（builder 源码未迁移）。
- 另：txFreed 恒 0（esp 完成回调对新堆 pbuf 不触发，实际回收走 SDK 自己
  的 free_fn→os_free 静默路径）——lwh 堆 24h 浸泡确认无慢漂移（[beat] 的
  sdk_free=水位计）；
- 取证钩子（XPB/XTAKE/XVR 环）保留供浸泡观测，定案后裁除。

### 0-Z.1 随旧机丢失的资产（用户已拷回 build/ 全目录 ✅，factory_dump 待补）

| 丢失物 | 状态 |
|---|---|
| `build/arduino_final/` 全套（含胶水层源码） | ✅ 已拷回，本节修复即在其上 |
| `build/arduino_lwip/`（ping 0% 黄金基准） | ✅ 已拷回 |
| `build/factory_dump*.bin` | ❌ 仍未到（不影响开发，仅影响恢复出厂） |
| 板上旧固件整片备份 | ✅ `C:/esp/esp8266/esptool/board_backup_20261021/full_flash_4mb.bin` |

### 0-Z.2 新机环境档案（全部实测可用）

| 组件 | 新机路径/值（旧机 → 新机差异） |
|---|---|
| 调试板串口 | **COM2**（CH340 驱动已装；USB 重插会在 COM2/COM4 间漂，烧前先 `Get-PnpDevice` 确认） |
| xtensa GCC 10.3 | `C:\esp\toolchain-xtensa\bin`（原样） |
| RTOS SDK（CMake 线用） | `C:\esp\esp8266`；`C:\esp\nonos_v305` 仍不完整（lib/bin 空） |
| Python | `py` → `D:\anaconda3\python.exe`（**py 启动器+相对脚本静默不执行，一律 anaconda 直连**）；多行 python -c 经 cmd 会静默失败，逻辑一律写成 .py 文件 |
| CMake 4.4.4 + Ninja | `C:\Users\jxy\.workbuddy\...\cmake.exe`；bash 用 `C:\Program Files\WorkBuddy\resources\vendor\PortableGit\bin\bash.exe`（PATH 需补 /d/anaconda3） |
| esptool 5.6 | 子命令连字符式（read-flash/write-flash）；旧机 shim 不再需要 |
| WLAN 取证配置 | `XinYueAP` profile + 静态 IP 192.168.4.2/24（UAC 授权过一次；**测完恢复：`netsh interface ipv4 set address name="WLAN" source=dhcp`**） |
| NONOS3 uart 头 | 官方 driver_lib 版与平面 SDK 宏冲突，shim 用空转发 + 旧机预编译 .o（`arduino_gui/_obj` 是全量备份源） |

### 0-Z.3 构建脚本新机适配（build/arduino_final/build_fw_final.sh）

- `ROOT` 改指 `D:/code/CMake/Container`；`_obj/filelist_final.txt` 存在则复用；
- 本地 TU（app/stubs 等）加存在性守卫；`rf_cal_stub.c`（4MB→1019 扇区）与
  `partition_stub.c`（迁移 SDK 无 system_partition_table_regist）补链接；
- lwip TU 加 `-fcommon`（GCC10 默认 -fno-common 与 gluedebug.h 暂定定义冲突）；
- `esp8266_console/shim` 已重建：7 个转发头 + `freertos/task.h` 的
  `taskENTER/EXIT_CRITICAL → vPortEnter/ExitCritical` 宏映射（stubs.c 已有
  嵌套实现）；`lib/NONOSDK305` 由 `NONOSDK3V0` 复制；
- ⚠️ 增量判定只比对 .c mtime，**改 CFG/shim/头文件后必须 `clean_objs.py`
  清 `_obj` 全量重编**，否则坏 .o 会被静默跳过。

### 0-Z.4 ⚠️ CMake 控制台固件（已入库主线）首次真机即崩

`build/esp8266` 产物烧板后启动循环（ROM 三段加载校验全过 → 静默复位，
user_init 未执行），疑 Zbit flash + ROM Cache_Read_Enable 同族问题，未展开修。

### 0-Z.5 P0 泄漏取证链（H1/H2 双否决 → 胶水层定案的过程记录）

1. **H1（TLSF 配对缺陷）否决**：`build/h1_drift/` 五组压力零漂移；
2. **H2（lwip 内部）否决**：`build/lwipsim/` 板端口径桌面模拟 630 虚拟秒零漂移；
3. 旧机 `build/` 拷回后真机三环取证：XTX 环（TX 配对）+ XVR 环（分配指针
   身份证：sz=92/ping 永不销账）+ CBREF 探针（回调时刻 ref=245 垃圾值）
   → 定案 0-Z.0 的 esp 布局改写机制；
4. 桌面模拟曾"洗清"lwip 的原因：模拟器用仓库自己的 pbuf 语义，没有 SDK
   胶水的 esp 布局改写——教训：**跨层指针所有权的契约要反汇编验证**。

复跑：
```sh
cd D:\code\CMake\Container\build\h1_drift  && h1_drift.exe
cd D:\code\CMake\Container\build\lwipsim   && lwipsim.exe
```

5. **MultiPool 静态平级重构（用户提议）已回滚存档**：XMultiPool.c 已带
   `XMP_GLOBAL_INIT_CUSTOM` 宏门控（定义=平台函数名 void fn(XMultiPool*)，
   禁用默认建池流程由平台装配；未定义=原行为，桌面零影响）。曾在 stubs.c
   手动 init 32/64/128 三档（静态缓冲 + create_from_memory）——add_pool 走
   power-of-two 严格序被全拒（UART 标记 hPa!b@c# 实证），且 arena 压到
   17KB 缺 wifi 启动余量 → Fatal 28 循环 → 整体回滚到
   arduino_final_ok_20261021。重做要点：钩子内用原 `XFixedPool_create`
   （勿用 from_memory）+ arena≥17KB + C256=0，独立会话配平验证。

---

## 0. 当前状态（2026-10-21 凌晨 —— 历史记录）

> ⚠️ **2026-10-20 出现新问题**：WiFi 能正常工作约 2 分钟后 arena 被慢速
> 泄漏填满，收包分配失败（"变哑"）。屏幕/shell/串口/XGui 均正常。
>
> **2026-10-21 凌晨进展**（详见 HANDOVER §8）：
> - 桌面端已确认框架事件路径不是泄漏源（HANDOVER §7）；
> - 真机环形记录表取证完成：**RX pbuf 零泄漏**（freedTotal==rxOk），
>   §3.1"<=128 档泄漏"的推论是直方图记账假象（lwip 走 XMalloc_System
>   直通 arena，free 不过直方图）；
> - **根因重定向**：arena（TLSF）实际占用 ~140B/s 线性上涨直至分配失败，
>   下一个会话按 HANDOVER §8.2 的 H1（TLSF 配对缺陷）/ H2（lwip 内部
>   非 pbuf 分配）顺序查；**→ 已按 §0-Z.4 完成桌面判定：H1/H2 均否决，
>   根因收敛到丢失的胶水/app 层**；
> - 完整问题清单、已定位根因、环境流程经验（netsh 重联、增量构建、
>   后台任务协议）见 [`HANDOVER_2026-10-20.md`](./HANDOVER_2026-10-20.md)。

## 0-A. 2026-10-19 收官状态（历史记录，供对照）

**四项验收全部通过，同一固件运行。**固件：`build/arduino_final/fw_final.bin`
（450689 字节，含 eboot；构建必须带 `RODATA_IN_FLASH=1`，烧录**禁止** `erase_flash`）。

| 验收项 | 结果 | 证据 |
|---|---|---|
| ① 稳定无重启 | ✅ | 80s 连续，`rst cause` 仅开机 1 次，KEX/wdt/Fatal = 0 |
| ② shell 响应 | ✅ | `> [arduino-console] console init done` |
| ③ ping 192.168.4.1 | ✅ | 15 发 12 收，延迟 23ms 起步，丢包 20%（见 §0.3） |
| ④ 屏幕 XGui 内容 | ✅ | `pg0..pg7 nack=0 ok`、`i2cErrs=0`、`draw text ok` |

**内存全绿**：`histo` 各档 `fail=0`；`poolUsed=204/3168`；`varPoolUsed=15828/24544` 平稳不涨。

**最终架构**：单线程时间轮事件循环（无 FreeRTOS）· XConsoleShell 走串口 ·
WiFi SoftAP + 仓库自带 lwip 2.2.1 · XGui 直绘上屏 · 分配器 = XVariablePool(arena) +
XMultiPool(小对象快路径)，**混合分配带回退**（§0.4）。

**阅读顺序建议**：想复现问题先看 §4.0（今天五个根因）→ §0.2（构建开关）→
§0.4（框架改动）。§4.1 是被推翻的假说，仅存档。

### 0.0 烧录与构建的铁律（违反 = 全盘失败）

```sh
# 构建：必须带 RODATA_IN_FLASH=1，否则 26KB rodata 落 DRAM，堆只剩 7.2KB
RODATA_IN_FLASH=1 bash build_fw_final.sh

# 烧录：只 write_flash，禁止 erase_flash（它会把 I2C 总线搞坏，见 §4.0）
python esptool.py --chip esp8266 --port COM2 --baud 115200   write_flash --flash_mode dout --flash_freq 40m --flash_size 4MB   0x00000 fw_final.bin 0x3FB000 esp_init_data_default.bin 0x3FC000 esp_init_data_default.bin
```

### 0.1 框架修复：`XMalloc_Hybrid` 加回退（池耗尽不再等于硬失败）

`Src/XMemory/XMemory.c` 的 hybrid 分配链原本是**单向**的：

```c
static void* hybrid_malloc(size_t size) {
    if (size <= XMEMORY_HYBRID_THRESHOLD)
        return XMemory_malloc(size, XMEMORY_TYPE_MULTIPOOL);   // ← NULL 直接抛出去
    else
        return XMalloc_System(size);
}
```

而 `XMultiPool_malloc` 找不到档位或档位耗尽就返回 NULL（`XMultiPool.c:152`），
于是"**池耗尽 = 分配硬失败**"。在小内存嵌入式平台上这一条足以掐死整条应用初始化链
（GUI 起不来 → 应用对象建不出来 → 主循环不跑 → 只剩 shell）。

**已改为"池耗尽 = 走慢路径"**，五处全部加了回退到变量池/系统堆的兜底：
`hybrid_malloc`、`hybrid_calloc`、`hybrid_realloc` 的 XMultiPool 分支、
XVariablePool 分支、以及系统堆分支（后者原本还会丢掉原块后返回 NULL）。
纯增量：快路径命中时行为完全不变，只在原本会返回 NULL 的情况下多一次尝试。

**推论：不要用"压低 `XMEMORY_HYBRID_THRESHOLD`"来绕这个问题。**
阈值压低 = 把原本被池吸走的流量全部赶到 arena，arena 一爆就是同样的死法
（这正是 R11 只剩 shell 的原因）。正确姿势是**档位按实测直方图配够 + 阈值对齐最大档位 + 有兜底**。

### 0.2 构建开关：`RODATA_IN_FLASH` 漏带 → 系统堆被吃干

`build_fw_final.sh:71` 是 `if [ "${RODATA_IN_FLASH:-0}" = "1" ]`，**默认 0**。
带 `=1` 时会把 `.rodata` 从 DRAM 搬到 irom0(flash)；不带，就回落 DRAM。

R11/R12 两轮构建漏了这个开关（日志里没有 `rodata -> irom0_0_seg` 和
`merged .rodata into irom0 segment` 两行），26KB 常量掉进 DRAM 变成
`.dram.rodata` 段，`_bss_end` 顶到 `0x3fffdfe0`，**系统堆只剩 7200 字节**，
WiFi/lwip 分不到内存 → 卡死在 `user_init` 第一行之前，连启动横幅都打不出来。

**识别方法**（一眼看出 DRAM 预算）：
```sh
xtensa-lx106-elf-nm -td fw_final.elf | grep -E '_heap_start|_bss_end'
# dram0_0_seg 末地址 = 0x3ffffc00，减去 _heap_start 即可用系统堆
```

### 0.3 ✅ 2026-10-19 最终验收（四项全过）

固件：`build/arduino_final/fw_final.bin`（450689 字节，含 eboot）

| 验收项 | 结果 | 证据 |
|---|---|---|
| ① 稳定运行无重启 | ✅ | 80 秒连续，`rst cause` 仅开机 1 次，KEX/wdt/Fatal 全 0 |
| ② shell 提示符响应 | ✅ | `> [arduino-console] console init done` |
| ③ ping 192.168.4.1 | ✅ | 15 发 12 收，延迟 23ms 起步 |
| ④ 屏幕 XGui 内容 | ✅ | `pg0..pg7 nack=0 ok`、`i2cErrs=0`、`draw text ok` |

内存全绿：`histo` 各档 `fail=0`、`poolUsed=204/3168`、`varPoolUsed=15828/24544` 平稳。

**已知遗留**：ping 丢包 20%（优化前 33%）。原因是软件位翻转 I2C 刷整屏要占用
约 1.3 秒忙等，期间 lwip 定时器跑不了。已做的优化：驱动新增
`XDeviceDisplay_ssd1306_i2c_flush_step()`（每拍只发一页，把 1.3 秒摊到 8 拍），
重绘周期从 5 秒拉到 30 秒。**实测心跳周期从 6.3s 回到 5.03s（理论值 5s）**，
丢包 33%→20%。要进一步降到 0，需要把 I2C 时钟从 50kHz 提到 400kHz
（`twi_dcount` 142 → 约 35），或让 lwip 跑在独立上下文中。

### 0.4 框架改动清单（`Src/XMemory/XMemory.c`，2026-10-19）

三组**纯增量**修改，快路径命中时行为完全不变：

1. **hybrid 五处加回底** —— `hybrid_malloc` / `hybrid_calloc` /
   `hybrid_realloc` 的 XMultiPool 分支 / XVariablePool 分支 / 系统堆分支。
   把"池耗尽 = 硬失败 NULL"改成"池耗尽 = 走慢路径"。释放侧 `hybrid_free`
   本就是三路探测（MultiPool → VariablePool → System），所以回退出来的块
   仍能被正确识别和释放，**不会串池**。
2. **分发层四处判空** —— `XMemory_malloc/realloc/calloc/free` 取槽位函数指针
   后判空。槽位表在"裸机 + 可变池也被裁 + 未接自定义后端"时会是 `{NULL,...}`
   （槽位表注释自己承认的唯一例外），原来直接空函数指针跳转。
3. **hybrid 快路径补 `#if XMEMORY_MULTIPOOL_ON`** —— 与 `hybrid_free` 现有写法对齐。

## 1. 硬件档案（实测确证）

| 部件 | 事实 |
|---|---|
| 板卡 | HW-364A，NodeMCU 0.96" OLED 版 |
| 主控 | ESP8266EX (ESP8266MOD)，26MHz 晶振，芯片ID 0x0051102d，MAC f4:cf:a2:51:10:2d |
| USB 串口 | CH340G→UART0；自动下载电路 DTR→GPIO0、RTS→EN |
| Flash | Zbit 0x5E:0x4016，4MB（同款板 flash 供应商可能不同，如 XMC 0x20，无影响） |
| **OLED** | GME12864-43 = **黄蓝双色**0.96" 128×64，控制器 **SSD1315（兼容 SSD1306 init）** |
| OLED 接线 | **I2C: SDA=GPIO14(D6), SCL=GPIO12(D5), 地址 0x3C，无独立复位脚**（两独立信源+实测 ACK 确认；⚠️ 通用资料误标为 GPIO4/5，勿信） |
| GPIO16 | 接屏 EXT_RSTB，上电默认已释放，驱动**无需**操作它 |
| 按键/LED | FLASH(GPIO0)、RST；LED=GPIO16 |
| **IO 占用** | OLED 占 **GPIO14+12**；GPIO16 接屏 EXT_RSTB（动它=屏复位）；GPIO1/3=UART0；**自由 IO = GPIO4/5/13**（完全无限制），GPIO0/2/15 可用但有 strap 限制（0=下载键、2 启动须高、15 启动须低） |

## 2. 工具链与环境（全部已装好）

| 组件 | 路径 |
|---|---|
| Xtensa GCC 10.3 | `C:/esp/toolchain-xtensa/bin` |
| NONOS SDK 3.0.5 + eboot + elf2bin | `C:/esp/arduino8266`（Arduino 框架包） |
| esptool 4.11 + 串口 shim | `C:/esp/esp8266/esptool`（PYTHONPATH=shim） |
| **串口互斥锁** | `C:/esp/esp8266/esptool/serial_lock.py`（多进程抢 COM2 必用） |
| 串口捕获 | `C:/esp/esp8266/esptool/capture.py COM2 74880 秒数`（先硬复位再抓） |
| 出厂固件备份 | `build/factory_dump.bin` + `factory_dump2.bin`（4MB 全量，可随时刷回） |

约定：调试板=COM2；串口被占则等 30 秒重试（最多 5 次）。

## 3. 启动链与"铁律"（违反任一条 = 黑屏/假 ACK/重启循环）

NONOS3 应用镜像布局（eboot 方案，单文件烧 0x0）：
`elf2bin.py --eboot eboot.elf --app app.elf -m dout -f 40 -s 4M` → fw.bin
= eboot@0x0 + 应用@0x10000；init 区: PHY_DATA@0x3FB000、RF_CAL@0x3FC000(空)、
SYS_PARAMETER@0x3FD000(空×3页)。

app_entry 必须按序完成（参考 `build/arduino_nos/app.c`+`stubs.c`+`cre.S`）：
1. **cre.S 直写 SPI0 寄存器建立 cache**——ROM `Cache_Read_Enable` 在 Zbit flash 上
   挂死或失效，绝不可调用（参数 a10-a12 为 call0 ABI 传参）。
2. **手动清零 .bss**（eboot 不清，不清则 SDK 全局变量是垃圾）。
3. `__wrap_spi_flash_read`：SDK 原生 flash 读内部会 cache 关/开循环，在 Zbit 上
   挂死；用 `-Wl,-wrap,spi_flash_read` 换成 `__real_spi_flash_read`
   （SDK 原生路径，已验证可用），或经 cache 映射区直接拷贝。
4. **真实堆**：pvPortZalloc/sdk3_pvPortMalloc/xPortGetFreeHeapSize/
   xPortWantedSizeAlign 必须是真实实现（stubs.c 的 first-fit 堆），空存根会让
   分区注册尾部 memcpy(NULL) 静默死。
5. **ROM 符号用 --defsym 钉地址**：`ets_post=0x40000e24
   ets_intr_lock=0x40000f74 ets_intr_unlock=0x40000f80`
   （ROM PROVIDE 对 archive 内引用不生效；绝不能空存根——lmac/esf_buf/ets_timer 全靠它们）。
6. 串口输出=直写 0x60000000，**必须带 FIFO 水位流控**（128B FIFO 裸写丢字节）；
   `os_install_putc1(uart0_putc1)` 让 SDK os_printf 同样进 UART。
7. **固件身份标记**：app_entry 打印独立标记（如 'EC'），多固件混测时区分归属。
8. 长循环喂狗：`pp_soft_wdt_feed()`（libpp），否则 ~8s 硬件复位。

## 3.5 运行时架构：框架时间轮驱动一切（单线程事件循环模型）

**这是本平台确定的运行时形态，任何新功能都按它设计，不要引入 RTOS 任务。**

```
XCoreApplication 事件循环（一拍一循环，无阻塞）
  ├─ XTimeWheelGroup 轮转 ── 框架定时器唯一时间基准
  │     采集 / 周期上报 / 界面节流 / 超时 / 心跳
  ├─ sys_check_timeouts() ── lwip 协议栈超时（由同一拍驱动，不用 os_timer）
  ├─ XConsoleShell 输入/输出
  └─ SDK os 事件循环（ets_run）── WiFi/net80211 回调
```

要点：

1. **唯一时间基准 = 框架统一时间轮**（XTimeWheelGroup，已在 final 固件编译清单中：
   `Src_XTimer_XTimerGroup_XTimeWheelGroup_XTimeWheelGroup.c.o` 等）。
   lwip 的 `sys_check_timeouts()` 由事件循环同一拍调用（`app.c` 内，
   include `lwip/timeouts.h` 的是仓库 lwip2，符号未改名）。
2. **不需要 FreeRTOS**。WiFi 跑在 NONOS SDK 自己的 os 事件系统
   （`ets_post`/`ets_run`）上，与任何内核无关——实测：无 V9 的固件里
   SoftAP + lwip ping 零丢包 + shell + 屏幕全部正常。
3. **FreeRTOS V9 是可选增强，不是缺口**：仅当将来出现"阻塞操作 / 子系统隔离 /
   真·XThread 语义 / CPU 密集任务分时"需求时才需要；届时也不必搬运 ets_run
   （保持主循环即可）。
4. **待办后台工作一律用定时器，不要开线程**：
   定时采集 = XTimer 回调；周期上报 = XTimer 回调；界面节流 = 定时器 + 脏标记。
5. **内存分配器 = 框架自带的 XVariablePool + XMultiPool**（不用手搓 h_alloc，也不用
   FreeRTOS heap_4）：
   - `XMEMORY_MULTIPOOL_ON=1`（连带 `XMEMORY_FIXEDPOOL_ON=1`，`XMemory_config.h:61` 有依赖约束）：
     小对象(≤512B) 走多级固定池(32/64/128/256/512 档)，**分配快、零碎片**，非桌面默认块数约 3KB；
   - `XMEMORY_VARIABLEPOOL_ON=1` + `XPLATFORM_BAREMETAL`：arena 是 **.bss 静态缓冲**
     （`XVariablePool.c:987-1002`），须把 `XVP_GLOBAL_ARENA_BYTES` 从非桌面默认
     32KB/16KB **调到 10-12KB**（`XMemory_config.h:164-171`）；
   - `XMalloc_Hybrid`（lwip 的 `MEM_CUSTOM_MALLOC` 就是它）路由规则（`XMemory.c:539`）：
     ≤`XMEMORY_HYBRID_THRESHOLD`(512B) → XMultiPool 快路径；>512B → 变量池 arena。
     lwip 小 pbuf 因此走快路径且不碎大池；
   - 收益：白拿 XMemory 统计（poolUsedBytes/poolTotalBytes）便于按数字调优；
     删掉 h_alloc 与 heap_4 = 省 28KB 静态堆 + 4-8KB 任务栈；
   - 禁忌：框架内不得有裸 malloc/free 直连（会绕过池）。
5. **lwip 为 NO_SYS=1**，这正是 lwip 官方推荐的裸机单线程用法；
   编译清单里的 `MEM_SIZE` 需按实际 RAM 下调（默认桌面值会撑爆）。

## 4. OLED 驱动（✅ 已打通，屏幕显示真实图案）

**权威驱动代码：`build/arduino_oled3/app.c`**（精确复刻 Arduino si2c 位时序）。

要点：
- I2C: SDA=GPIO14, SCL=GPIO12, 地址 0x3C（探测备选 0x3D），50kHz
- init 序列（SSD1315 兼容 SSD1306）：
  `AE D5 80 A8 3F D3 00 40 8D 14 20 00 A1 C8 DA 12 81 FF D9 F1 DB 40 A4 A6 2E AF`
- 每事务 = START + **0x78(地址字节,必发)** + 控制字节(0x00 命令/0x40 数据) + 数据 + STOP
- 面板黄蓝双色：page0-1=黄区，page2-7=蓝区
- **寄存器三坑（都会造成假象）**：
  1. 内部上拉 = IOMUX 寄存器 **bit7**（GPIO4/5:0x6000083C/40；GPIO12/13/14/15:
     0x60000804/08/0C/10）。⚠️ 0x60000328 是 pad-driver 寄存器，写它无效 →
     总线浮空 → **假 ACK**（症状：地址探测"成功"但实为浮空误读）。
  2. FUNC 域在 IOMUX 位[4:6] 且重排：`(((f&4)<<2)|((f&2)<<1)... )` 用 SDK 宏
     `PIN_FUNC_SELECT` 的公式：GPIO12-15 的 func=3 → mux 值 0x30|0x80=0xB0。
     ⚠️ 直接写 `func|0x80` 或 `func<<4|0x80` 都配错功能 → 引脚浮空。
  3. **任何"设备应答"结论，必须先确认总线空闲电平为高**，否则是浮空假 ACK。
- 命令明细：0xA5=强制全亮（黄区矩形+蓝区矩形，用于点亮验证）；
  0xA4=跟随 GDDRAM；对比度 0x81+值。

### 4.0 ✅ 2026-10-19 收官：屏幕全黑的五个根因（全部已修）

**这一节是今天全部血泪的浓缩。**总装固件从"内存爆掉 + 屏幕全黑 + ping 不通"
一路收敛到四项验收全过，过程中撞出五个独立的坑，按发现顺序：

| # | 现象 | 真实根因 | 修法 |
|---|---|---|---|
| 1 | XGui 分配失败、shell 建不起来 | 仓库 lwip 的 TCP 缓冲默认 8192，每建一个 PCB 吃掉 16KB，arena 只有 14K | 见 §5.1，降到 1460/2920 |
| 2 | lwip `TX FAILED code=-13` | MultiPool 128/256 档拍脑袋配被打空，pbuf 分配 NULL | 见 §5.2 + §0.1 回退 |
| 3 | R11/R12 只活一个 shell、连横幅都打不出 | 构建漏带 `RODATA_IN_FLASH=1`，26KB rodata 落进 DRAM，系统堆只剩 7200 字节 | 见 §0.2 |
| 4 | 屏幕全黑，日志却一切正常 | **探针用 `0xAE`（关显示）探测，且 `gui_oled_init()` 拿探针当"已初始化"判据 → 关屏后再没人开屏** | 探针改发 `0xE0`(NOP) |
| 5 | 画面糊、字形散架 | tile 被"瘦身"成 32 像素分块，靠 painter `-chunkX` 平移裁剪对齐，实测对不齐 | tile 改回 128 整行 |

**第 4 条是今天最难查的一个**，它把所有日志指标都骗过去了：

```
[i2c] init NACK=0          ← 初始化零错
[i2c] pg0..pg7 nack=0 ok   ← 八页全部送达
[gui] flush done           ← 刷屏函数正常返回
[gui] i2cErrs=0            ← 错误计数为 0
```
**全是对的，只是面板一直关着。**根因在 `gui_oled.c:59`：
```c
if (XDeviceDisplay_ssd1306_i2c_probe()) {   // probe() 内部发 0xAE = 关显示
    uart0_puts("[i2c] already init@boot, skip re-init

");
    return 1;                                 // ← 于是永远不再发 0xAF
}
```
探针返回"通过" → 跳过 re-init → **`0xAF`（开显示）永远发不出去**。

**两条通用教训**：
- **探测/诊断代码必须无副作用**。拿"关显示"命令去探测设备，等于自己把设备关掉。
- **返回值的 0 到底代表什么，必须写在注释里**。`init()` 的约定是 `0=成功`（`nack ? -1 : 0`），
  而 `app.c` 一度写成 `r ? OK : FAIL` ——判断反了，一直打印假警报 `early oled init FAIL`。

### 4.0.1 ⚠️ `erase_flash` 会把 I2C 总线搞坏（今天最大的弯路来源）

子代理的烧录脚本每轮都是 `erase_flash` 再全量写。结果每次跑起来：
`idle sda=0 scl=1` + `probe start refused` + 八页全 `nack=393 NACK`——
**看起来像硬件坏了**。真实情况是：**同一块板子、同一份固件，跳过 `erase_flash` 直接
`write_flash`，一切正常**（`probe addr=1 ctrl=1 cmd=1 ACK`、`init NACK=0`、八页 `nack=0 ok`）。

**结论：ESP8266 + 本板 OLED 的组合，烧录一律只用 `write_flash`，不要 `erase_flash`。**
今天绝大部分时间耗在这条错误路径上。

**另一个相关教训**：`erase_flash` 之后误用重新编译的固件当"对照组"，得出"硬件故障"的
错误结论。**对照实验必须用未改动的原始二进制**——哪怕源码同源，只要夹带了当天任何改动，
就不构成对照。~~基线快照已固化在 `platforms/esp8266/baseline_20261018_gui/`（在版本控制内）。~~
（该快照已于 2026-10-22 清理；本条教训已固化在此，原始二进制可从 git 历史
`git show <commit>:platforms/esp8266/baseline_20261018_gui/fw_gui.bin` 取回。）

### 4.1 ⚠️【已被 §4.0 推翻，仅存档】曾怀疑的软件位翻转 I2C 中断假说

**症状**：串口日志一切正常（`draw text ok` / `flush done` 都照常打印，因为这两个打印
是无条件的，**不能当成功标志**），但屏幕全黑；同时
`[gui] i2cErrs` 每 5 秒涨 1048（= 8 页 × 131 字节），
`[i2c] pg0..pg7 128B nack=393 NACK`（393 ≈ 131 字节 × 3 次重试 = 每字节每次都 NACK），
而且 `[gui] oled init FAIL` 出现在 t=0.61s。

**根因**：`XDeviceDisplay_ssd1306_i2c.c` 的 `twi_read_bit()` 顺序是
释放 SDA → busywait → 采样 SDA。WiFi/lwip 的毫秒级定时中断一旦插进
「释放 SDA」和「采样」之间，就会在从机把 SDA 拉低之前采到高电平 → 读成 NACK。
`init` 一旦记到 NACK 就提前中止，`display-on (0xAF)` 没发出去 → 屏永远全黑
（数据其实在往外发，只是屏没被点亮）。

**为什么之前是好的**：同一驱动文件在 `build/arduino_gui/fw_gui.bin`
（串口日志 `serial_draw_r2b.log`）里 8 页全是 `nack=0 ok`，用户目视确认有内容。
那次 GUI 跑在安静的栈泵上下文里；总装固件同屏时 lwip/WiFi 定时器全开。
**驱动代码一行没改（自 10-18 15:10 起 mtime 未变）** —— 坏的是运行环境。

**修法**：用中断锁包住 I2C 事务。ROM 地址铁律里已经钉好：
`ets_intr_lock=0x40000f74`、`ets_intr_unlock=0x40000f80`，`--defsym` 声明后 extern 调用。
粒度上**按字节锁**（不要整页 128 字节一把锁），锁内禁止调
`uart0_puts` / `gui_wdt_feed` / 任何会喂软狗或长时间阻塞的函数。

**通用教训**：
- 「绘制日志齐全」≠「上屏成功」，验收必须看 `nack=0` 和 `i2cErrs` 是否停止增长。
- 裸机位翻转外设一旦和 WiFi/lwip 共存，中断就是一等公民，不 mask 一定坏。

## 5. WiFi + 库 lwip（✅ 已打通，ping 零丢包）

- 驱动: `build/arduino_lwip/XDeviceNetwork_lwip_esp8266.c`
  （esp2glue 全套实现 + netif + TX/RX；仓库 lwip 2.2.1 core，NO_SYS=1）
- 实测: SoftAP `XinYueAP`（MAC f6:cf:a2:51:10:2d，IP 192.168.4.1，DHCP server 起），
  PC 关联后 `ping 192.168.4.1` 零丢包 TTL=255
- 注意: lwip 定时器用 os_timer 5ms→`sys_check_timeouts()` 驱动
- 遗留: XinYueC socket API（XDeviceNetwork_lwip.c 桌面层）未接到 lwip 上

### 5.1 ⚠️ 仓库 lwip 的 TCP 缓冲默认值是桌面尺寸，嵌入式必须覆盖

`Src/XCode/XNetwork/XNetwork_config.h`（非桌面分支同样）：
```c
#define XNETWORK_LWIP_RECV_BUFFER_SIZE  8192   ← TCP_WND
#define XNETWORK_LWIP_SEND_BUFFER_SIZE  8192   ← TCP_SND_BUF
```
lwip **每建一个 TCP PCB 就从堆里要 8K 收 + 8K 发**。ESP8266 的 arena 只有 14KB →
PCB 缓冲分配失败 → 连锁症状：`[lwip] TX FAILED code=-1` → arena 耗尽 →
XGui 的 `XImageData_create` 里那笔 `XMalloc_System` 拿不到 → `tile image init FAIL` →
shell 对象创建也失败 → 提示符出不来。

**ESP8266 档案必须覆盖**：
```c
#define XNETWORK_LWIP_RECV_BUFFER_SIZE  1460   /* 1×MSS，配置有 #error 要求 >= TCP_MSS */
#define XNETWORK_LWIP_SEND_BUFFER_SIZE  2920   /* lwip 要求 >= 2×MSS */
/* lwipopts: MEMP_NUM_TCP_PCB 1, MEMP_NUM_TCP_SEG 4 */
```
单 PCB 从 16K 降到 4.4K，arena 余量立刻回来。

### 5.2 ⚠️ MultiPool 档位拍脑袋配 → lwip 发包中止（code=-13 不是 esp 侧的问题）

**症状**：PC 关联 XinYueAP 成功、拿到 DHCP 地址（说明 AP+DHCP+**接收**都正常），
但 ping 192.168.4.1 **100% 丢包**，串口同步刷 `[lwip] TX FAILED by esp side, code=-13`。
`-13 = GLUE_ERR_ABRT`（SDK `glue.h` 枚举：-12=IF、**-13=ABRT**、-14=RST）。

**根因不是 esp 侧，是我们自己的池不够**。看 ping 流量打进来那几拍的分配直方图：
```
t=39.56  <=128: fail=0   <=256: fail=0    poolUsed=204 /1928   ← 空闲
t=45.20  <=128: fail=42  <=256: fail=82   poolUsed=1080/1928   ← ping 开始
t=56.68  <=128: fail=340 <=256: fail=154  poolUsed=1324/1928
```
lwip 的 MEMP 池在 `MEMP_MEM_MALLOC=1` 下是从 `XMalloc_Hybrid` 出的，pbuf/TSEG 落在 128/256 档；
档位 C128=4、C256=2 被抽干，而 **`XMultiPool_malloc` 找不到块直接返回 NULL 且不回退变量池**
（`XMultiPool.c:152`）→ pbuf 分配失败 → 发包中止。

**教训**：档位数不能拍脑袋估，必须用真机的分配直方图反推。
本次 C32=16/C64=8/C128=4/C256=2/C512=0 是估的，真实分布直接推翻了它。
另外，**"池耗尽 = 硬失败 NULL"这个设计在小内存嵌入式上是最大的坑**：
要么把 `XMEMORY_HYBRID_THRESHOLD` 压到让快路径只覆盖极小对象，要么给池加回退路径。

**排查方法固化**：任何"某个功能一有流量就挂"的症状，先看 `[mem]`/`[histo]` 两行
——`fail` 从 0 开始增长的时刻，就是流量到来的时刻，一一对应。

**通用诊断法（本次定位靠它）**：池化架构下任何"分配失败"症状，都先看
`XMemory` 统计：`poolUsed/poolTotal`（MultiPool 档位）与 `varPoolUsed/varPoolTotal`
（变量池 arena）。本次数据 `varPoolUsed=12992 varPoolTotal=14304` + `poolUsed=204`
直接指认"arena 被大块吃光、小对象快路径空转"。

## 6. 串口 shell（✅ 已打通，双活固件）

- `build/arduino_final/`：XCoreApplication+XConsoleShell+lwip 合体，fw_final.bin
- 真机：每启动周期 shell 提示符 `>` 与 lwip 日志并存；netsh 关联 XinYueAP 成功
- 依赖清单: `build/esp8266_console/filelist.txt`（210 文件）+
  `esp8266_console_minimal.h`（裁剪头）+ `docs/esp8266_minimal_port.md`（缺口表）
- 坑: XMemory 槽签名 bug（已修）；shell/派生器创建会耗尽默认堆（已扩）
- 应用对象选择: 纯控制台用 XCoreApplication；**带 GUI 必须用 XGuiApplication**

## 7. FreeRTOS V9（✅ 单独跑通；❌ 最终不并入，施工图归档）

- 内核: 仓库 V9.0.0；移植层: **自研汇编** `build/esp_v9_freertos/portasm.S`
  （L1 向量表 + vecbase 重定位 + yield/tick，XT_STK 80 字节帧，CCOMPARE0 tick）
- 单独验证 ✅: `build/esp_v9_freertos/fw.bin` 输出 TASK_A/TASK_B 交替心跳
- **最终决定：不并入**（2026-10-18）。三条理由：
  1. 与 NONOS WiFi **结构上互斥**：`ets_run`(ROM 0x40000e04) 是 WiFi 唯一事件消费者
     （libpp/libnet80211 收包、PP deferred blob、lwip netif 定时器、ETSTimer 全靠
     `ets_post` 进 ROM 队列），而 `user_init` 是在 `ets_run` 之前的 boot 栈上被直接调用的
     ——调度器一启动就遗弃该循环，队列只进不出；
  2. **净占 28KB 静态堆 + 4-8KB 任务栈**（占 80KB RAM 的一半），而无 V9 的
     单线程版（shell+WiFi+lwip+XGui）已全部真机验证通过；
  3. 当前架构（§3.5）不需要多任务。
- **代码保留 + 宏门控（不删）**：自研移植层（port.c/portasm.S/port_isr.c/heap_4.c）
  搬回仓库 `Drive/FreeRTOS/portable/GCC/XTENSA_LX106/`，由
  `#define XESP8266_FREERTOS_V9_ON 0`（ESP8266 裁剪档案内）门控；
  构建脚本按该宏决定是否编 V9 内核/移植层/heap_4 与是否调 xTaskStartScheduler。
  验证方式：最终固件 elf 中不应出现 FreeRTOS 任务符号（nm 查
  pxPortInitialiseStack/xTaskStartScheduler/uxTaskGetTickCount）——证明"没编进去"而非"被删了"。
- **施工图归档（将来若必须上 RTOS）**：
  - 代码: `build/esp_v9_freertos/`（port.c/portasm.S/port_isr.c/heap_4.c）；
  - 调研结论：esp-open-rtos 做法（删 app_main.o/ets_timer.o 重写 user_start、5 向量表+
    独立 NMI 栈 canary、把 `MacIsrSigPostDefHdl()` 从 NMI 挪到软中断）、SDK→FreeRTOS
    移植层**契约符号表**（vPortEnterCritical 必须是可重入真函数、NMIIrqIsOn、PendSV、
    xPortSysTickHandle、cpu_sr）、RTOS SDK v3 的 **`cpu_sr=0x20`** 首拍崩修复、
    `ets_post` 需保存/恢复 PS（ROM bug, arduino#6484）、arduino 官方协作调度
    (ets_task + cont.S 4096B 独立栈) 方案；
  - 已知踩坑：KEX 打印**不能用 ets_printf**（本环境静默失效）要用 `uart0_putc_raw` 逐字符；
    崩溃地址必须 `nm`+`objdump -l` 反查符号（本项目最后定位到的其实是应用代码空指针
    `app.c` 的 `*pz=0xA5A5A5A5`，不是内核问题）。

## 8. 待办（按优先级）

### P1 — ping 丢包 20%（已从 33% 优化下来，仍未到 0）
软件位翻转 I2C 刷整屏占用约 1.3 秒忙等，期间 lwip 定时器停摆。已做：驱动新增
`XDeviceDisplay_ssd1306_i2c_flush_step()` 每拍只发一页（1.3s → 摊到 8 拍），
重绘周期 5s → 30s。**实测心跳周期 6.3s → 5.03s，丢包 33% → 20%。**

可选下一步：
- I2C 从 50kHz 提到 400kHz（`twi_dcount` 142 → ~35），整屏刷新的忙等降到 1/8；
  代价是位时序余量变小，需要重新验证 `nack=0`。
- 把 GUI 绘制挪到 lwip 之外的时机，或给 lwip 定时器更高优先级。

### P2 — XGui 控件集瘦身 + 入口规范化
当前是直绘路径（`gui_oled_draw()` 用 XPainter 逐 band 画），还没走
XGuiApplication + 窗口 + backingstore 的正规路径。`esp8266_console_minimal.h`
已把控件集裁到 XLabel + XLayout 的最小闭包，入口切 XGuiApplication 尚未做。

### P3 — GB2312 字体
点阵字体够用，但中文字形表尚未启用。

### P4 — 长时稳定性
目前只验到 80 秒。需要 24 小时以上的运行观察，确认 `varPoolUsed` 不缓慢增长。

### 已完成（不再列入）
- 启动链 / WiFi SoftAP / 仓库 lwip 数据通路 / 串口 shell / XGui 上屏 —— 四项验收全过
- FreeRTOS V9：单独跑通，最终不并入（代码保留，宏 `XESP8266_FREERTOS_V9_ON` 门控）
- 内存：arena 24544 + MultiPool 分档 + 混合分配回退，histo 全档 fail=0
- 构建：`RODATA_IN_FLASH=1` 固化为默认要求；烧录禁用 `erase_flash`

## 9. 常用命令速查

```bash
# 构建（任一固件目录）
cd D:/code/CMake/XinYueC/build/<目录> && bash build_arduino.sh

# 烧录（NONOS3 单文件布局; OLED/扫描类）
cd C:/esp/esp8266/esptool
python serial_lock.py flash 300 -- env PYTHONPATH=shim python esptool.py \
  --chip esp8266 --port COM2 --baud 115200 write_flash \
  --flash_mode dout --flash_freq 40m --flash_size 4MB \
  0x00000 <fw.bin> \
  0x3FB000 <esp8266_sdk>/esp_init_data_default.bin \
  0x3FC000 <esp8266_sdk>/blank.bin 0x3FD000 <esp8266_sdk>/blank.bin \
  0x3FE000 <esp8266_sdk>/blank.bin 0x3FF000 <esp8266_sdk>/blank.bin

# 串口捕获（自动硬复位）
python serial_lock.py cap 90 -- python capture.py COM2 74880 20

# 恢复出厂固件（对照实验用）
python esptool.py --chip esp8266 --port COM2 --baud 115200 write_flash \
  --flash_mode keep --flash_size 4MB 0x00000 build/factory_dump.bin \
  0x200000 build/factory_dump2.bin
```

`<esp8266_sdk>` = `C:/esp/arduino8266/tools/esptool/test/images/esp8266_sdk`

---
