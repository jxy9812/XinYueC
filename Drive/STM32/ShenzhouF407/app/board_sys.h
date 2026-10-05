/* ==================== 神舟号 STM32F407 板级系统服务 ====================
 * UART1 调试口（PA9/PA10 → CH340，P6 默认接通）、DWT 微秒延时、
 * newlib 存根（_write/_sbrk 等）、FreeRTOS 断言与钩子。
 * 时钟：CMSIS system_stm32f4xx.c 在启动文件里完成（HSE 8MHz → PLL 168MHz）。
 */
#ifndef BOARD_SYS_H
#define BOARD_SYS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USART1 115200-8N1（PA9 TX 复用推挽；RX 不接亦可发送）。 */
void board_uart1_init(void);

/* DWT CYCCNT 微秒/毫秒忙等（供 XPT2046 位 bang 时序；调度器无关）。 */
void board_delay_init(void);
void board_delay_us(uint32_t us);
void board_delay_ms(uint32_t ms);

void board_putc(char c);
size_t board_extbuf_size_of(const void* ptr);
int board_extbuf_grow(void* ptr, size_t oldSize, size_t newSize);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_SYS_H */
