/**
 * @file minimal_main.c
 * @brief 二分用最小应用：不依赖 XinYueC，验证 bootloader+链接脚本+烧录布局。
 */
#include "esp_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 直接写 UART0 FIFO，绕过一切封装。 */
static void uart0_puts(const char* s)
{
    volatile uint32_t* fifo = (volatile uint32_t*)0x60000000;
    while (*s)
        *fifo = (uint32_t)(unsigned char)*s++;
}

static void blink_task(void* arg)
{
    (void)arg;
    for (;;) {
        uart0_puts("MINIMAL_ALIVE\r\n");
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    switch (size_map) {
    case FLASH_SIZE_32M_MAP_1024_1024:
    case FLASH_SIZE_32M_MAP_512_512:
    case FLASH_SIZE_32M_MAP_2048_2048:
        return 1024 - 5;
    default:
        return 0;
    }
}

void user_init(void)
{
    uart0_puts("USER_INIT\r\n");

    /* 切到 115200：F_CPU/115200 */
    *(volatile uint32_t*)0x60000014 = 80000000 / 115200; /* UART_CLKDIV */
    uart0_puts("BAUD_115200\r\n");

    xTaskCreate(blink_task, "blink", 256, NULL, 2, NULL);
}
