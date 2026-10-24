/**
 * @file xdevice_oled.h
 * @brief SSD1306 OLED 外设接口（128x64，I2C，黄蓝双色屏）
 *
 * @details 本文件是【外设级】封装：对外只暴露"初始化 / 画标题栏 / 增量刷屏 /
 *          错误计数"四件事。底层 I2C 时序在 `XDeviceDisplay_ssd1306_i2c.c`
 *          （oled3 真机验证版，勿改时序）。
 *
 *          初始化分两段，顺序不可颠倒：
 *            earlyInit —— 配引脚 + 总线解锁 + init 序列 + 刷开机图样。
 *                         必须在 net_init/lwip/WiFi 之前（否则 HSPI 抢占
 *                         GPIO12/14，SDA 被钉低）。
 *            init      —— 探测已初始化则跳过，否则重新 init + 清屏。
 */
#ifndef XDEVICE_OLED_H
#define XDEVICE_OLED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OLED 最早初始化（引脚 + 总线解锁 + init + 开机图样）。
 * @return true = init 序列全 ACK
 * @note 必须由 XDevice_init_all() 在 WiFi/lwip 之前调用。
 */
bool xdevice_oled_early_init(void);

/**
 * @brief OLED 常规初始化（若已 earlyInit 则跳过，否则补一次 init + 清屏）。
 * @return true = 可用
 */
bool xdevice_oled_init(void);

/**
 * @brief 绘制默认界面（黑底 + 黄区标题栏 "XinYueC" + 蓝区框线）。
 * @return true = 绘制成功
 */
bool xdevice_oled_draw(void);

/**
 * @brief 增量上屏一页（事件循环每拍调用）。
 * @return 1 = 还有脏页；0 = 无待发
 */
int xdevice_oled_pump(void);

/** @brief I2C NACK 累计数（诊断：非 0 说明总线异常）。 */
unsigned xdevice_oled_ack_errors(void);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICE_OLED_H */
