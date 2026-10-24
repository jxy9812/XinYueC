/**
 * @file xdevice_init.h
 * @brief 板级外设总初始化入口（ESP8266 HW-364A / XinYueC 模板）
 *
 * @details 设计约定：
 *          1. `drivers/` 下【每个外设一个 .c 文件】，各自只负责本外设的
 *             引脚/时序/初始化，互不直接调用；
 *          2. 本文件（xdevice_init.c）是唯一的汇总点：按**固定顺序**调用各
 *             外设的初始化函数，并汇总返回值；
 *          3. 业务层（main.c）只调一行：
 *                 XDevice_init_all();
 *             即可完成全部外设初始化，不需要知道有哪些外设、谁先谁后。
 *
 * @note 新增外设时只改两处：drivers/ 里加文件 + 在 xdevice_init.c 的
 *       XDevice_init_all() 里加一行调用（顺序见函数内注释）。
 */
#ifndef XDEVICE_INIT_H
#define XDEVICE_INIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化板载全部外设（按固定顺序）。
 *
 * 当前包含（顺序即调用顺序，不可随意调换）：
 *   1. UART0 —— 调试串口 74880 8N1（必须先于任何打印）
 *   2. SSD1306 OLED —— I2C SDA=GPIO14 / SCL=GPIO12 / 0x3C
 *
 * @return true = 全部外设初始化成功；false = 至少一个失败（详情见串口日志）
 * @note 必须在 user_init 的**最前面**调用：OLED 的 earlyInit 要在
 *       net_init/lwip/WiFi 启动之前完成（否则 SDA 可能被 HSPI 抢占，
 *       见 PROGRESS §4.0）。
 */
bool XDevice_init_all(void);

/**
 * @brief 外设周期轮询（在事件循环每拍调用，非阻塞）。
 *
 * 目前只驱动 OLED 的增量上屏（每拍发一页，8 拍刷完整屏）。
 * @return 1 = 还有脏页待发；0 = 本次无待发内容
 */
int XDevice_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICE_INIT_H */
