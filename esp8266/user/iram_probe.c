/**
 * @file iram_probe.c
 * @brief 放在 IRAM 的启动探针：直接写 UART0，不依赖 irom/cache。
 *
 * 链接脚本里 libfreertos.a 的代码留在 IRAM。本文件用同名段属性把自己也
 * 钉在 IRAM 的 .text 里，用于验证「bootloader → call_user_start → 本函数」
 * 这条链路是否活着。验证完可整文件删除。
 */
#include <stdint.h>

/* 放入 IRAM：与链接脚本 *libfreertos.a:(.literal .text ...) 同段不行，
 * 这里用 section 属性直接指定。.UserEnter.text 在 IRAM .text 块最前面。 */
static void uart0_write_raw(const char* s)
{
    volatile uint32_t* fifo = (volatile uint32_t*)0x60000000;
    while (*s)
        *fifo = (uint32_t)(unsigned char)*s++;
}

/* 在 .UserEnter.text：链接脚本把它放在 IRAM .text 最前部。 */
void __attribute__((section(".UserEnter.text"), noinline, used))
xinyuec_iram_probe(void)
{
    uart0_write_raw("IRAM_PROBE\r\n");
}

/* 额外入口：即使 UserEnter 不被调用，也让符号存在于 IRAM 便于 nm 检查。 */
void __attribute__((section(".UserEnter.text"), noinline, used))
xinyuec_iram_probe2(void)
{
    uart0_write_raw("IRAM_PROBE2\r\n");
}
