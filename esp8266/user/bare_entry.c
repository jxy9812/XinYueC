/**
 * @file bare_entry.c
 * @brief 完全绕过 SDK 启动链的裸机入口。
 *
 * 链接时用 -e bare_entry。在 IRAM 里自己设栈指针，直接写 UART，
 * 不调用 user_start、不碰 irom、不依赖 libmain。
 * 用于二分：若此固件能打出 BARE_OK，说明 bootloader+烧录布局+IRAM 执行
 * 三者组合可用，问题出在 SDK 启动链或 irom；若仍无输出，问题更底层。
 */
#include <stdint.h>

#define UART_FIFO   (*(volatile uint32_t*)0x60000000)
#define UART_CLKDIV (*(volatile uint32_t*)0x60000014)
#define UART_CONF0  (*(volatile uint32_t*)0x60000020)

/* DRAM 顶端向下留栈。dram0 = 0x3FFE8000..0x40000000。 */
#define BARE_STACK_TOP 0x3FFFF000u

static void bare_puts(const char* s)
{
    while (*s)
        UART_FIFO = (unsigned char)*s++;
}

void __attribute__((noreturn, used)) bare_entry(void)
{
    /* 自己设栈——bootloader 可能没设 a1。用 naked 风格：先切 a1。 */
    __asm__ __volatile__("mov a1, %0" :: "r"(BARE_STACK_TOP) : "a1");

    bare_puts("BARE_OK\r\n");

    /* 切 115200：F_CPU/115200 = 694 */
    UART_CLKDIV = 80000000u / 115200u;
    bare_puts("BAUD115200\r\n");

    for (;;)
        bare_puts("ALIVE\r\n");
}
