# ESP8266 HW-364A 移植操作手册（XinYueC）

> 更新: 2026-10-18。本版为精简重写：只含已真机验证的事实与可直接复制的命令。
> 目标形态: XinYueC 事件循环 + 串口 shell + WiFi(库lwip) + OLED(GME12864-43) + FreeRTOS V9。
> 旧版长篇排查记录已删除（其中 GPIO4/5、SPI 穷举、EXT_RSTB 门控等结论有误，勿参考）。

---

## 0. 当前状态（2026-10-20 收工 —— 有新问题，先读 HANDOVER_2026-10-20.md）

> ⚠️ **2026-10-20 出现新问题**：WiFi 能正常工作约 2 分钟后 arena 被慢速
> pbuf 泄漏填满，收包分配失败（"变哑"）。屏幕/shell/串口/XGui 均正常。
> 完整的问题清单、已定位根因与接手建议见
> [`HANDOVER_2026-10-20.md`](./HANDOVER_2026-10-20.md)。
>
> 该文档同时记录了今天踩的 10 个坑，其中最重要的两条：
> **① 基准固件目录只读，任何实验先复制；② 烧录禁止 `erase_flash`。**

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
就不构成对照。基线快照已固化在 `esp8266/baseline_20261018_gui/`（在版本控制内）。

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
