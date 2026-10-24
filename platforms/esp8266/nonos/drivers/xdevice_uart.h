/**
 * @file xdevice_uart.h
 * @brief UART0 调试串口外设接口（ESP8266 NONOS3）
 */
#ifndef XDEVICE_UART_H
#define XDEVICE_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单字节直写 FIFO（带水位流控）。 */
void uart0_putc_raw(char c);

/** @brief 写 0 结尾字符串。 */
void uart0_puts(const char* s);

/** @brief 写无符号十进制数。 */
void uart0_u(unsigned v);

/** @brief 等 TX FIFO 完全排空（崩溃前诊断打点用）。 */
void uart0_drain(void);

/**
 * @brief 初始化 UART0 外设。
 * @param baud 目标波特率（实际分频由 XSerialPortESP8266 驱动设置）
 * @note 必须在任何打印之前调用（XDevice_init_all 的第一项）。
 */
void xdevice_uart_init(unsigned baud);

/**
 * @brief SDK os_printf 的 putc1 钩子（内部使用，user_pre_init 需要安装它）。
 */
void xdevice_uart_putc1(char c);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICE_UART_H */
