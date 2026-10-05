/* ==================== 神舟号 XPT2046 电阻触摸（位 bang SPI） ====================
 * 引脚（板级排针，TFTLCD 模块）：T_CLK=PB0、T_MOSI=PF11、T_MISO=PB2、
 * T_CS=PC13、T_PEN=PB1（按下为低）。
 * 坐标映射到横屏 480x320：原始 12 位 ADC 量程与方向经宏旋钮校准
 * （XINYUE_TP_*，默认与 ALIENTEK 3.5 寸横屏口径一致；串口打原始值辅助校准）。
 */
#ifndef BOARD_TOUCH_H
#define BOARD_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PENIRQ 引脚初始化（其它线在 board_touch_init 内一并配置）。 */
void board_touch_init(void);

/* 采样一次：按下返回 true 并给出面板坐标（已映射/钳位）；抬起返回 false。
 * 内部做双采样一致性过滤（|Δ|>阈值丢弃重采），单点语义。 */
bool board_touch_read(int* outX, int* outY);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_TOUCH_H */
