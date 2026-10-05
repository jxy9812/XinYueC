/* ==================== 神舟号 GPIO 寄存器助手 ====================
 * MODER 字段是 2 位（00 输入 / 01 输出 / 10 复用 / 11 模拟），直接 OR 新值
 * 会与残留位叠加——必须先清字段再置位。fieldmask 是"整字段掩码"
 * （即 3UL << (pin*2)），由助手内部拆出要写的值。
 */
#ifndef BOARD_GPIO_H
#define BOARD_GPIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MODER：复用功能（10）。 */
static inline void gpio_moder_af(volatile uint32_t* reg, uint32_t fieldmask)
{
    *reg = (*reg & ~fieldmask) | (fieldmask & 0xAAAAAAAAUL);
}

/* MODER：通用输出（01）。 */
static inline void gpio_moder_out(volatile uint32_t* reg, uint32_t fieldmask)
{
    *reg = (*reg & ~fieldmask) | (fieldmask & 0x55555555UL);
}

/* OSPEEDR：高速（11，直接 OR 即可）。 */
static inline void gpio_speed_high(volatile uint32_t* reg, uint32_t fieldmask)
{
    *reg |= fieldmask;
}

/* PUPDR：上拉（01）。 */
static inline void gpio_pull_up(volatile uint32_t* reg, uint32_t fieldmask)
{
    *reg = (*reg & ~fieldmask) | (fieldmask & 0x55555555UL);
}

/* PUPDR：无上下拉（00）。 */
static inline void gpio_pull_none(volatile uint32_t* reg, uint32_t fieldmask)
{
    *reg &= ~fieldmask;
}

#ifdef __cplusplus
}
#endif

#endif /* BOARD_GPIO_H */
