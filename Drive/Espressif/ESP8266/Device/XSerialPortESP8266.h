/**
 * @file XSerialPortESP8266.h
 * @brief ESP8266 的 XDeviceSerialPort 平台后端。
 * @details
 * 实现 Drive/Posix/Device/XSerialPortPosix.c 与
 * Drive/STM32/Device/Serial/XSerialPortSTM32.c 同构的后端钩子，把 XinYueC 的
 * 串口设备层落到 ESP8266 RTOS SDK 的 UART0 上。
 *
 * 器件约束：NodeMCU 的 CH340 固定接在 GPIO1/GPIO3（即 UART0）。UART1 的
 * GPIO13/GPIO15 是 SPI flash 的 SDO/SDXL，物理不可用，因此本后端只支持
 * UART0，端口名参数一律忽略。
 *
 * @note SDK 的 driver_lib/include/uart.h 自身不含任何头文件，依赖调用方先
 *       包含 esp8266/uart_register.h（UART_RXD_INV 等宏在其内定义）。本
 *       实现文件已按该顺序包含。
 */

#ifndef XSERIALPORTESP8266_H
#define XSERIALPORTESP8266_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 复位接收 FIFO 的单次最大取字节数（硬件 FIFO 128 字节，留余量）。 */
#define XESP8266_SERIAL_RX_BURST_MAX 64

/** @brief 把 UART0 配置为指定波特率的 8N1，并复位收发 FIFO。 */
void XSerialPortESP8266_configureUart(int32_t baudRate);

#ifdef __cplusplus
}
#endif

#endif /* XSERIALPORTESP8266_H */
