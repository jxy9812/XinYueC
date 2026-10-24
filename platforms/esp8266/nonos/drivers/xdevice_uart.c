/**
 * @file xdevice_uart.c
 * @brief UART0 调试串口外设（ESP8266 NONOS3）
 *
 * @details 提供裸机 FIFO 直写 + 水位流控（铁律 6，源自已验收底座）：
 *          TXFIFO_CNT = UART0_STATUS bits[23:16]，超过 120 字节就等待，
 *          防止长打印把 FIFO 灌爆导致丢字符。
 *          波特率钉 74880（与 bootloader 同速，串口终端一次配置即可看全程）。
 *
 * @note 本文件的 uart0_puts 是**全局符号**，被驱动层/平台层大量引用
 *       （stubs.c 统计打印、OLED 驱动轨迹、WiFi 胶水日志），改动需全量重编。
 */
#include <stdint.h>

#include "xdevice_uart.h"

/* UART0 寄存器：0x60000000 = FIFO 读写口，0x6000001C = 状态
 * （TXFIFO_CNT = bits[23:16]，RXFIFO_CNT = bits[7:0]）。 */
#define UART0_FIFO   (*(volatile unsigned int*)0x60000000)
#define UART0_STATUS (*(volatile unsigned int*)0x6000001c)

/* FIFO 水位上限：超过就自旋等待（上限 2000 次防死等，SDK 会自己排空）。 */
#define UART0_TX_HIGH_WATER 120u
#define UART0_TX_GUARD      2000u

void uart0_putc_raw(char c)
{
    unsigned int guard = UART0_TX_GUARD;
    while (((UART0_STATUS >> 16) & 0xff) > UART0_TX_HIGH_WATER) {
        if (--guard == 0) break;
    }
    UART0_FIFO = (unsigned int)(unsigned char)c;
}

void uart0_puts(const char* s)
{
    while (*s) uart0_putc_raw(*s++);
}

void uart0_drain(void)
{
    /* 等 TX FIFO 完全排空（崩溃前打点用：保证诊断文本已上线）。 */
    unsigned int guard = 200000u;
    while (((UART0_STATUS >> 16) & 0xff) != 0) {
        if (--guard == 0) break;
    }
}

void uart0_u(unsigned v)
{
    char b[11];
    int i = 10;
    b[10] = '\0';
    if (!v) { uart0_puts("0"); return; }
    while (v && i > 0) { b[--i] = (char)('0' + (v % 10)); v /= 10; }
    uart0_puts(&b[i]);
}

void xdevice_uart_putc1(char c)   /* SDK os_printf 钩子 */
{
    uart0_putc_raw(c);
}

void xdevice_uart_init(unsigned baud)
{
    /* 驱动侧（XSerialPortESP8266.c，仓库 Drive/ 内）负责真正配置
     * UART0 分频/格式；这里只做本外设的"就绪"动作：切 putc1 钩子，
     * 让 SDK 的 os_printf 也走同一 FIFO 通道。
     * ⚠ os_install_putc1 只是 ets_install_putc1 的宏别名（osapi.h:37），
     *   直接写 os_install_putc1 在部分头组合下展开不出来 → 用真名。 */
    extern void ets_install_putc1(void (*p)(char c));
    ets_install_putc1(xdevice_uart_putc1);
    (void)baud;   /* 波特率由 XSerialPortESP8266_configureUart 统一设置 */
}
