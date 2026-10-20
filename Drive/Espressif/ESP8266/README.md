# Drive/Espressif/ESP8266 — ESP8266 平台适配

本目录是 XinYueC 在 ESP8266 上的平台层。目标硬件 **HW-364A**（NodeMCU 形态：
ESP8266EX + CH340G USB 转串口 + 4MB Flash + 板载 0.96" OLED）。

> **操作手册（构建/烧录/引脚/OLED/命令速查）见仓库根 `esp8266/PROGRESS.md`。**
> 本 README 只描述平台层代码的组织与状态。

## 现状总览（2026-10-18，全部真机验证）

| 能力 | 状态 | 代码/固件位置 |
|---|---|---|
| NONOS3 启动链（SDK 3.0.5） | ✅ | `build/arduino_nos/`（底座：app.c/stubs.c/cre.S/build_arduino.sh） |
| 串口 shell（XCoreApplication+XConsoleShell） | ✅ 提示符真机出现 | `build/arduino_console/` |
| WiFi SoftAP | ✅ | `build/arduino_wifi/` |
| **库 lwip 2.2.1 数据链路**（ping 零丢包） | ✅ | `build/arduino_lwip/` |
| shell + lwip 合体双活 | ✅ | `build/arduino_final/`（fw_final.bin） |
| **OLED 驱动**（SSD1315/SSD1306 I2C，图案上屏） | ✅ | `build/arduino_oled3/app.c` |
| XinYueC 最小集清单（210 文件+裁剪头） | ✅ | `build/esp8266_console/` + `docs/esp8266_minimal_port.md` |
| FreeRTOS V9 调度器 | ✅ 单独跑通；**最终不并入**（与 NONOS WiFi 结构互斥），代码保留由宏门控 | `Drive/FreeRTOS/portable/GCC/XTENSA_LX106/`（`XESP8266_FREERTOS_V9_ON` 默认 0） |
| 内存分配 | ✅ 框架池：XVariablePool(.bss arena) + XMultiPool(小对象快路径) | `Src/XMemory/`（配置见 `esp8266/PROGRESS.md` §0 配方） |
| XGui 显示集成 | ⏸️ 待 OLED 驱动合入 | `build/arduino_gui/`（配置档案已产出） |

## 运行时架构（定稿）

**单线程事件循环 + 框架统一时间轮驱动一切**（Arduino 官方同款路线）：

```
XCoreApplication 事件循环（一拍一循环）
  ├─ XTimeWheelGroup      框架定时器（唯一时间基准）
  ├─ sys_check_timeouts() lwip 协议栈超时
  ├─ XConsoleShell        串口 shell
  └─ ets_run()            SDK os 事件（WiFi/net80211 回调）
```

- 不使用 FreeRTOS：V9 与 NONOS WiFi 结构互斥（`ets_run` 是 WiFi 唯一事件消费者），
  且 V9 净占 28KB 静态堆 + 4–8KB 任务栈，而本架构不需要它。
  V9 移植层代码保留在 `Drive/FreeRTOS/portable/GCC/XTENSA_LX106/`，宏门控，随时可开。
- 内存分配走框架自己的池：`XMEMORY_MULTIPOOL_ON=1`（小对象 ≤512B 走多级固定池，快、
  零碎片）+ `XMEMORY_VARIABLEPOOL_ON=1`（裸机 arena = .bss 静态缓冲），
  池档位按实际裁（`XMP_GLOBAL_C512=0`，`XMEMORY_HYBRID_THRESHOLD` 必须 ≤ 最大档位，
  否则 XMultiPool 找不到档位会直接返回 NULL 且不回退变量池）。
- 完整配方与踩坑：仓库 `esp8266/PROGRESS.md` §0 / §3.5。

## SDK 与构建体系（重要）

**实际使用的不是 RTOS SDK CMake 工程**。RTOS SDK v1.5 的 FreeRTOS V7.5.2
预编译库与仓库 V9 内核不兼容，已放弃。当前体系：

- **NONOS SDK 3.0.5**（Arduino 框架包内的预编译库，`C:/esp/arduino8266/tools/sdk`）
  只作 radio blob + ROM 胶水用：boot 横幅、分区表、WiFi、PHY 全来自它；
- 应用镜像 = **eboot@0x0 + 应用@0x10000 合并单文件**（`elf2bin.py` 生成）；
- 构建脚本模板：`build/arduino_nos/build_arduino.sh`（xtensa gcc + NONOSDK305
  库组 + eboot 合并），各实验目录复制改造。

仓库里遗留的 `esp8266/CMakeLists.txt` / `toolchain-esp8266.cmake` 是 RTOS SDK
时代的产物，**不再是当前构建路径**（保留作参考）。

## 启动铁律（app_entry 必须按序完成，违反 = 黑屏/假 ACK/重启循环）

1. **cre.S 直写 SPI0 寄存器建立 cache**——ROM `Cache_Read_Enable` 在 Zbit
   flash 上挂死/失效，绝不可调用（参数走 call0 ABI：a10-a12）；
2. **手动清零 .bss**（eboot 不清）；
3. `spi_flash_read` 走 `-Wl,-wrap` + `__real_spi_flash_read`（SDK 原生路径可用；
   手写 cache 映射拷贝会读到 stale）；
4. 堆函数（pvPortZalloc/sdk3_pvPortMalloc/xPortGetFreeHeapSize/
   xPortWantedSizeAlign）必须是真实堆实现——空存根 = 静默 memcpy(NULL) 死；
5. ROM 符号用 `--defsym` 钉地址：`ets_post=0x40000e24
   ets_intr_lock=0x40000f74 ets_intr_unlock=0x40000f80`（archive 内引用对
   ROM PROVIDE 不生效；绝不能空存根）；
6. 串口=直写 0x60000000 + **FIFO 水位流控**（128B FIFO 裸写丢字节）；
   `os_install_putc1` 接 SDK os_printf；
7. 固件身份标记（app_entry 打印独立字符），多固件混测区分归属；
8. 长循环调 `pp_soft_wdt_feed()`（libpp），否则 ~8s 复位。

详见 `esp8266/PROGRESS.md` §3。

## 引脚占用（实测确证，修正过卖家错误资料）

| 引脚 | 占用 |
|---|---|
| **GPIO14(D6)** | OLED I2C SDA |
| **GPIO12(D5)** | OLED I2C SCL |
| GPIO16 | 屏 EXT_RSTB（上电已释放；翻转它=屏复位，用屏期间勿动） |
| GPIO1/3 | UART0（CH340G） |
| GPIO6-11 | SPI flash，物理不可用 |
| GPIO0/2/15 | 可用但有启动 strap（0=下载键、2 启动须高、15 启动须低） |
| **GPIO4/5/13** | **自由 IO**（无任何连接/限制） |

⚠️ 卖家/通用资料声称 OLED 在 GPIO4/5——**是错的**（实测四象限扫描 + 出厂固件
对照 + 两个独立信源确认在 GPIO14/12，且部分批次 SDA/SCL 互换）。

## OLED 驱动（最终可用版在 `build/arduino_oled3/app.c`）

- GME12864-43 = 黄蓝双色 SSD1315（兼容 SSD1306 init），I2C 0x3C，无复位脚
- I2C 为纯软件 bit-bang，**精确复刻 Arduino `core_esp8266_si2c`**：
  开漏（GPEC 释放/GPES 拉低，不写 GPO）、busywait(dcount) 标定时序、
  SCL 高电平窗口采 ACK
- init 序列（26B）：`AE D5 80 A8 3F D3 00 40 8D 14 20 00 A1 C8 DA 12 81 FF
  D9 F1 DB 40 A4 A6 2E AF`
- 事务 = START + **0x78（地址字节，必发）** + 控制(0x00 命令/0x40 数据) + 数据 + STOP
- **三大寄存器坑**（曾造成假 ACK/浮空，详见 `esp8266/PROGRESS.md` §4）：
  1. 内部上拉 = IOMUX **bit7**（GPIO12/14: 0x60000804/0x0C），不是 0x60000328；
  2. FUNC 域在位[4:6]且重排：GPIO12-15 func=3 → mux 值 0x30（用 SDK
     `PIN_FUNC_SELECT` 公式），直接 `func<<4` 或 `func|0x80` 都错；
  3. 开漏驱动器 GPC bit2=1（`pinMode INPUT_PULLUP` 的展开包含它）；
- **教训**：任何"设备应答"结论必须先确认总线空闲电平为高，否则是浮空假 ACK。

## 待并入本目录的代码

后续整理时把以下已验证实现从 build/ 移入本目录（保持 XinYueC 命名规范）：

| 现位置 | 目标 | 说明 |
|---|---|---|
| `build/arduino_lwip/XDeviceNetwork_lwip_esp8266.c` | `Device/` | 库 lwip 的 esp2glue netif 驱动 |
| `build/arduino_oled3/app.c` 的 I2C/SSD1306 部分 | `Device/XDeviceDisplay_ssd1306_i2c.c` | OLED 驱动（剥离验证日志） |
| `build/arduino_final/stubs.c` 的堆/defsym/存根 | `Device/XPlatformStub_esp8266.c` 增补 | 合并底座存根 |

## 已知风险 / 遗留

1. XinYueC socket API（`platform/XDeviceNetwork_lwip.c` 桌面层）尚未接到
   ESP8266 lwip 上——当前只有协议栈双活；
2. FreeRTOS V9 的 L1 tick 中断未打通（二分到 soft-int 备选方案）；
3. 堆余量紧张：XGui init 后 ~6.5KB，加控件需盯 `heap free`；
4. RAM 极限时 lwip `MEM_SIZE` 需从桌面默认值大幅下调。

---

## 2026-10-19 收官补充

### 分配器：`XMalloc_Hybrid` 已加回退（框架级修改）

`Src/XMemory/XMemory.c` 的 hybrid 分配链原本是**单向**的——`size <= 阈值` 直接
返回 MultiPool 的结果，**池找不到块就返回 NULL，一行回退都没有**。
配合 `XMultiPool_malloc` 找不到档位即返回 NULL（`XMultiPool.c:152`），就得到一条
要命规则：**池耗尽 = 分配硬失败**。在几 KB 内存的平台上，这足以掐死整条应用
初始化链（GUI 起不来 → 应用对象建不出来 → 主循环不跑 → 只剩 shell）。

已改为"**池耗尽 = 走慢路径**"，共五处，全部加了回退到变量池/系统堆的兜底：
`hybrid_malloc`、`hybrid_calloc`、`hybrid_realloc` 的 XMultiPool 分支、
XVariablePool 分支、以及系统堆分支（后者原来还会丢掉原块后返回 NULL）。

释放侧无需改动：`hybrid_free` 本就是三路探测（`XMultiPool_is_from_pool` →
`XVariablePool_is_from_pool` → 系统堆），回退出来的块仍能被正确识别释放。

另补两处健壮性：`XMemory_malloc/realloc/calloc/free` 取槽位函数指针后判空；
`hybrid_malloc`/`hybrid_calloc` 快路径补 `#if XMEMORY_MULTIPOOL_ON`，与
`hybrid_free` 现有写法对齐。

**推论：不要用"压低 `XMEMORY_HYBRID_THRESHOLD`"来绕这个问题。**阈值压低等于把
原本被池吸走的流量全赶到 arena，arena 一爆就是同样的死法。正确姿势是
**档位按实测直方图配够 + 阈值对齐最大档位 + 有兜底**。

当前 ESP8266 档案实测值：
```
XMP_GLOBAL_C32=16  C64=12  C128=8  C256=4  C512=0
XMEMORY_HYBRID_THRESHOLD = 256
XVP_GLOBAL_ARENA_BYTES   = (24 * 1024)
```
运行期 `histo` 各档 `fail=0`，`varPoolUsed` 15828/24544 平稳。

### 两条烧录/构建纪律

1. **构建必须带 `RODATA_IN_FLASH=1`** —— 否则 26KB `.rodata` 落进 DRAM 变成
   `.dram.rodata` 段，系统堆从 33KB 掉到 7.2KB，WiFi/lwip 直接分不到内存，
   固件卡在 `user_init` 第一行之前（连启动横幅都打不出）。
2. **烧录禁止 `erase_flash`** —— 本板 + ESP8266 的组合下，整片擦除后 I2C 总线
   会读成 `SDA stuck low`、所有事务 `start refused`、八页全 NACK，看起来像硬件
   坏了，实际只 `write_flash` 就一切正常。

### I2C 探针必须无副作用

`XDeviceDisplay_ssd1306_i2c.c` 的 `probe()` 曾用 `0xAE`（**关显示**）做探测命令，
而 `gui_oled_init()` 又拿探针返回值当"是否已初始化"的判据。链路是：

```
probe() 发 0xAE  →  判为"已初始化"  →  跳过 re-init  →  0xAF(开显示) 永远发不出去
```

结果：I2C 日志全对（`init NACK=0`、八页 `nack=0 ok`、`i2cErrs=0`、`flush done`），
**但面板一直关着，屏幕全黑**。探针已改为发 `0xE0`(NOP)。

详见 `esp8266/PROGRESS.md` §4.0。
