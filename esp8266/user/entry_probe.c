/**
 * @file entry_probe.c
 * @brief 放进 .UserEnter.text（IRAM 最前部）的启动探针。
 *
 * 链接脚本把 .UserEnter.text 放在 IRAM .text 最前面，紧跟异常向量。
 * call_user_start 之前 CPU 就在执行这片 IRAM——在这里打串口即可
 * 验证「bootloader 跳进 IRAM」这一段是否活着，完全不碰 irom。
 */
#include <stdint.h>

void __attribute__((section(".UserEnter.text"), noinline, used))
xinyuec_entry_probe(void)
{
    volatile uint32_t* fifo = (volatile uint32_t*)0x60000000; /* UART_FIFO(UART0) */
    const char* s = "ENTRY_PROBE\r\n";
    while (*s)
        *fifo = (uint32_t)(unsigned char)*s++;
}
