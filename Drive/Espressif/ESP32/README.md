# Drive/Espressif/ESP32 — ESP32 平台适配（预留）

本目录为 ESP32 目标预留，当前**尚无实现**。它与同级的
`Drive/Espressif/ESP8266/` 按芯片分目录，后续 ESP32 适配放在这里。

## 与 ESP8266 的差异

ESP32 走的是 **ESP-IDF**（本机已装 v6.1，`/c/esp/v6.1`），而 ESP8266 走
**ESP8266 RTOS SDK**——两者是不同 SDK，不共享代码，只共享
`Drive/Espressif/` 这一层目录归属。

相对 ESP8266，ESP32 在移植本库时有几个实质优势：

- **FreeRTOS 原生**：ESP-IDF 自带 FreeRTOS，`Drive/FreeRTOS/` 可直接复用，
  不存在 ESP8266 上"仓库 V9.0.0 与 SDK V7.5.2 ABI 冲突"的问题；
  仍需按 `Drive/FreeRTOS/portable/GCC/` 的约定新增 Xtensa 移植层目录。
- **内存宽裕**：约 520KB SRAM，`configTOTAL_HEAP_SIZE` 不必像 ESP8266 那样
  从 32KB 压到 24KB，GUI 与 TUI 都有腾挪余地。
- **引脚充裕**：不再有 ESP8266 那种"GPIO0/2/15 是启动绑带、GPIO6~11 被 SPI
  flash 占用"的硬约束，OLED 与外设的 IO 余量明显更松。
- **mbedTLS 已集成**：若要保留 SSH/Telnet 控制台后端，ESP8266 上要额外裁掉
  全部加密模块，ESP32 上则可直接启用。

## 计划中的文件

```
Drive/Espressif/ESP32/
  ESP32Minimal.h            裁剪档案（对应 ESP8266Minimal.h）
  Core/XSystem_esp32.c      复位/重启/内存快照
  Device/XSerialPort_esp32.c UART 原语
  Device/XDeviceConsole_esp32.c 标准输入后端
```

裁剪档案的宏划分、平台文件的命名与守卫风格都应与 `../ESP8266/` 保持一致，
以便两侧对照维护。
