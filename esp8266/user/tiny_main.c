/**
 * @file tiny_main.c
 * @brief 极小应用：不用 libmain 的 user_start，自己提供入口。
 * 链接时用 -Wl,-e,tiny_entry 强制入口，完全绕过 SDK 启动链。
 */
#include <stdint.h>

#define UART_FIFO (*(volatile uint32_t*)0x60000000)
#define UART_CLKDIV (*(volatile uint32_t*)0x60000014)

static void puts0(const char* s)
{
    while (*s)
        UART_FIFO = (uint32_t)(unsigned char)*s++;
}

void tiny_entry(void)
{
    /* 直接写 UART，不依赖任何 SDK/libmain。栈由链接脚本/__stack 提供，
     * 若没有就靠 bootloader 设置的 a1——这里尽量少用栈。 */
    puts0("TINY_OK\r\n");

    /* 切 115200 */
    UART_CLKDIV = 80000000 / 115200;
    puts0("BAUD115200\r\n");

    for (;;)
        puts0("ALIVE\r\n");
}

/* 提供 user_rf_cal_sector_set 符号避免 SDK 库未定义（若被链接）。 */
uint32_t user_rf_cal_sector_set(void) { return 1024 - 5; }
