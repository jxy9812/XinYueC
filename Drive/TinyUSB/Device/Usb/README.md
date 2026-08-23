# TinyUSB 嵌入式后端说明

## 概述

`Drive/TinyUSB/Device/Usb/` 目录包含基于 TinyUSB 协议栈的嵌入式 USB Host/Gadget 后端实现，
与 XinYueC 的 `XDeviceUsbHost` / `XDeviceUsbGadget` 公共层对接。

支持的 MCU：
- **STM32F4 系列**（OTG_FS / OTG_HS，DWC2 内核）
- **ESP32-S3**（USB OTG，DWC2 内核）

两个平台都走 TinyUSB 的 `synopsys/dwc2` 驱动，区别仅在于：
- 底层硬件初始化（时钟、GPIO、中断）不同（由 BSP 负责）
- `tusb_config.h` 中的 `CFG_TUSB_MCU` 宏不同

## 源文件

| 文件 | 作用 |
|------|------|
| `XDeviceUsb_tinyusb.h` | 公共头，平台检测、错误映射 |
| `XDeviceUsb_tinyusb_gadget.c` | Gadget/Device 后端（TinyUSB device API） |
| `XDeviceUsb_tinyusb_host.c` | Host 后端（TinyUSB host API） |

## 集成方式

### CMake 变量

```cmake
set(XINYUE_C_HAS_TINYUSB ON)
set(TINYUSB_DEVICE ON)           # 需要 Device 栈
set(TINYUSB_HOST ON)             # 需要 Host 栈
set(TINYUSB_OS "none")           # none / freertos / rtthread

# STM32F4 平台
set(TINYUSB_PORT_DIRS "synopsys/dwc2")
add_compile_definitions(CFG_TUSB_MCU=OPT_MCU_STM32F4)
add_compile_definitions(STM32F40_41xxx)   # 或你的具体型号

# ESP32-S3 平台
set(TINYUSB_PORT_DIRS "synopsys/dwc2")
add_compile_definitions(CFG_TUSB_MCU=OPT_MCU_ESP32S2)  # ESP32-S3 复用 S2 的 DWC2
add_compile_definitions(ESP32S3)
```

### BSP 要求（必须由板级代码完成）

TinyUSB 不操作硬件寄存器，以下初始化必须在调用 `XDeviceUsbHost_open` /
`XDeviceUsbGadget_open` 之前由 BSP 完成：

**STM32F4 OTG_FS：**
1. 使能 `RCC_AHB2ENR_OTGFSEN` 时钟
2. 配置 PA11 (DM) / PA12 (DP) 为复用推挽（AF10）
3. 配置 `OTG_FS_IRQn` 中断优先级
4. 使能中断

**STM32F4 OTG_HS：**
1. 使能 `RCC_AHB1ENR_OTGHSEN` 时钟
2. 配置 PB14 (DM) / PB15 (DP)（或 ULPI 接口，视硬件而定）
3. 配置 `OTG_HS_IRQn` 中断优先级
4. 使能中断

**ESP32-S3：**
1. 配置 GPIO19 (D-) / GPIO20 (D+)（或使用内部 USB PHY）
2. 使能 USB OTG 外设时钟
3. 注册 USB 中断处理函数到 `tud_int_handler()` / `tuh_int_handler()`
4. 如使用 ESP-IDF，参考 `tinyusb` 组件的集成方式

### 中断处理

TinyUSB 需要在 USB 中断中调用 `tud_int_handler(rhport)`（Device 模式）
或 `tuh_int_handler(rhport)`（Host 模式）。这由 BSP 的中断服务函数调用。

## 当前状态

### Gadget 后端（已实现）
- ✅ 描述符回调：设备/配置/BOS/字符串（UTF-8→UTF-16LE）
- ✅ 控制传输三阶段桥接
- ✅ 状态回调：mount/umount/suspend/resume
- ✅ 端点发现：从配置描述符自动解析
- ✅ 同步端点传输
- ✅ 异步端点传输 + ID 管理 + 取消
- ✅ STALL / Abort / FIFO 清空
- ✅ 远程唤醒
- ✅ Claim/Release 位图管理
- ⚠️ 待实机验证

### Host 后端（已实现）
- ✅ 控制器生命周期
- ✅ 设备列表 + 热插拔
- ✅ 枚举 + 设备打开
- ✅ 设备描述符读取
- ✅ 配置描述符获取 + 接口/端点解析
- ✅ 配置切换
- ✅ Claim/Release + Alternate Setting
- ✅ Clear Halt / Reset
- ✅ 控制传输（异步）
- ✅ Bulk/Interrupt/Iso 端点传输（异步 + 取消）
- ⚠️ 待实机验证

## 事件循环集成

### 单线程模型

TinyUSB 是**单线程**的协议栈，不需要额外线程。所有处理都在 	uh_task()
（Host）/ 	ud_task()（Gadget）中完成，调用方决定何时调用它。

### 与 XinYueC 事件循环的对接

TinyUSB 已经封装在平台后端的 processEvents 里：

- Host 侧：XDeviceUsbHost_processEvents(fd, timeoutMs)
  → XDeviceUsbHost_platformControllerProcessEvents()
  → 	uh_task() + 传输完成回调分发

- Gadget 侧：XDeviceUsbGadget_processEvents(fd, timeoutMs)
  → XDeviceUsbGadget_platformProcessEvents()
  → 	ud_task() + 传输完成回调分发

**主线程事件循环集成方式：**

`c
/* 主事件循环中，每次迭代调用一次 USB processEvents */
while (running) {
    XAbstractEventDispatcher_poll(dispatcher, -1);  /* 你的事件分发 */

    /* USB Host 轮询（打开了 Host 控制器时） */
    if (hostFd) {
        XDeviceUsbHost_processEvents(hostFd, 0);
    }

    /* USB Gadget 轮询（启动了 Gadget 时） */
    if (gadgetFd) {
        XDeviceUsbGadget_processEvents(gadgetFd, 0);
    }
}
`

### 宏隔离

未启用 TinyUSB 时（XINYUE_C_HAS_TINYUSB 未定义），平台后端通过
#if 条件编译被裁剪，processEvents 调用走的是该平台的实际后端
（Windows 走 WinUSB/IOCP、Linux 走 libusb/eventfd），
**不会调用 	uh_task() / 	ud_task()**，也不会链接 TinyUSB 符号。

### 轮询频率建议

- **全速 USB（12 Mbps）**：1~10 ms 轮询一次足够
- **高速 USB（480 Mbps）**：1 ms 以内轮询更好（或在中断里 set event 唤醒）
- **Gadget 模式**：对延迟不敏感的应用 10 ms 也能工作
- 中断驱动方式：USB 中断里只设置标志，事件循环下一轮执行 	uh_task() / 	ud_task()

### 超时/调度说明

TinyUSB 的"定时"全部基于 	usb_time_millis_api() 时间戳差值判断，
没有独立的定时器线程或硬件定时器：

- Host 枚举去抖/复位等待 → call_after 单槽 + 	uh_task() 中检查时间
- 端点传输超时 → HCD/DMA 硬件级超时或 	uh_task() 差值判断
- 类驱动心跳 → 各自用 	usb_time_millis_api() 差值
- 不使用 	usb_time_delay_ms() 阻塞运行时

因此配合你的时间轮/事件循环，只需要保证：
1. 毫秒级时间戳（已通过 XDateTime 对接）
2. 定期调用 processEvents（事件循环中调用即可）

## 内存管理

**TinyUSB 0.21.0 全程不使用 malloc/free**，所有缓冲区静态分配，
由 `tusb_config.h` 宏在编译时确定大小。无需接入 XinYueC 的 XMemory。

## 下一步

1. 选定具体 MCU 型号（STM32F407 / STM32F411 / ESP32-S3 等）
2. 准备 BSP 代码（时钟、GPIO、中断）
3. 在 MCU 编译环境中验证编译
4. 逐步完善未实现的传输功能
5. 实机测试
