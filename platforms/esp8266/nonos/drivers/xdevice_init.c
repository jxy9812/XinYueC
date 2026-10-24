/**
 * @file xdevice_init.c
 * @brief 板级外设总初始化（唯一汇总点）
 *
 * @details 新增外设的固定套路（两步，别在 main.c 里堆初始化代码）：
 *            1. `drivers/` 下新建 xdevice_xxx.c/.h，实现
 *               `bool xdevice_xxx_init(void)`；
 *            2. 在下面 XDevice_init_all() 里按**正确顺序**加一行调用。
 *          main.c 永远只写 `XDevice_init_all();` 一行。
 *
 * @warning 顺序有硬约束：
 *          - UART 必须最先（后面所有外设的初始化日志都靠它输出）；
 *          - OLED earlyInit 必须在 WiFi/lwip 之前（HSPI 会抢占 GPIO12/14，
 *            导致 SDA 被钉低、屏永远不亮——真机事故见 PROGRESS §0-Y）。
 */
#include "xdevice_init.h"
#include "xdevice_uart.h"
#include "xdevice_oled.h"

bool XDevice_init_all(void)
{
    bool ok = true;

    /* 1. 调试串口（74880 8N1，与 bootloader 同速）——必须最先。 */
    xdevice_uart_init(74880);

    /* 2. SSD1306 OLED：引脚 + 总线解锁 + init + 开机图样。
     *    必须在 WiFi/lwip 启动之前完成。 */
    if (!xdevice_oled_early_init())
        ok = false;

    return ok;
}

int XDevice_poll(void)
{
    /* 外设周期任务：OLED 增量上屏（每拍一页，8 拍整屏）。 */
    return xdevice_oled_pump();
}
