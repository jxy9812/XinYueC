/* ==================== 神舟号 XPT2046 电阻触摸实现 ====================
 * 位时序按 LVGL 模板 Public/touch/touch.c（ALIENTEK 同源）移植：
 * 命令字 MSB 先行、TCLK 上升沿移出；16 位回读仅高 12 位有效；
 * 双采样 ±50 一致性过滤。采样次数较模板调小（40→16）保轮询响应。
 */
#include "stm32f4xx.h"
#include "board_touch.h"
#include "board_lcd.h"
#include "board_sys.h"

/* ---------------- 坐标校准旋钮（-D 覆盖；配合串口原始值打印校准） ---------------- */
#ifndef XINYUE_TP_XMIN
#define XINYUE_TP_XMIN 200
#endif
#ifndef XINYUE_TP_XMAX
#define XINYUE_TP_XMAX 3900
#endif
#ifndef XINYUE_TP_YMIN
#define XINYUE_TP_YMIN 200
#endif
#ifndef XINYUE_TP_YMAX
#define XINYUE_TP_YMAX 3900
#endif
#ifndef XINYUE_TP_SWAP_XY
#define XINYUE_TP_SWAP_XY 0
#endif
#ifndef XINYUE_TP_FLIP_X
#define XINYUE_TP_FLIP_X 0
#endif
#ifndef XINYUE_TP_FLIP_Y
#define XINYUE_TP_FLIP_Y 0
#endif

/* ------------------------- 引脚（寄存器级） ------------------------- */
#define TP_GPIO_CLK     RCC_BASE
#define RCC_AHB1ENR_REG (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define GPIO_MODER(p)   (*(volatile uint32_t*)((p) + 0x00))
#define GPIO_PUPDR(p)   (*(volatile uint32_t*)((p) + 0x0C))
#define GPIO_IDR(p)     (*(volatile uint32_t*)((p) + 0x10))
#define GPIO_BSRR(p)    (*(volatile uint32_t*)((p) + 0x18))
#define GPIO_AFRL(p)    (*(volatile uint32_t*)((p) + 0x20))

#define TCLK_H()  (GPIO_BSRR(GPIOB_BASE) = (1UL << 0))
#define TCLK_L()  (GPIO_BSRR(GPIOB_BASE) = (1UL << 16))
#define TCS_H()   (GPIO_BSRR(GPIOC_BASE) = (1UL << 13))
#define TCS_L()   (GPIO_BSRR(GPIOC_BASE) = (1UL << 29))
#define TDIN(v)   (GPIO_BSRR(GPIOF_BASE) = (v) ? (1UL << 11) : (1UL << 27))
#define DOUT      ((GPIO_IDR(GPIOB_BASE) >> 2) & 1UL)
#define PEN_IRQ   ((GPIO_IDR(GPIOB_BASE) & 1UL) == 0UL)   /* 按下为低 */

#define CMD_RDX 0xD0
#define CMD_RDY 0x90

void board_touch_init(void)
{
    RCC_AHB1ENR_REG |= (1UL << 1) | (1UL << 2) | (1UL << 5) | (1UL << 6);
    /* PB0 T_CLK 推挽输出；PB2 T_MISO 输入下拉（无触压时悬浮）；PB1 T_PEN
     * 输入上拉（按下拉低）；PC13 T_CS 推挽输出（空闲高）；PF11 T_MOSI 推挽。 */
    GPIO_MODER(GPIOB_BASE) &= ~((3UL << 0) | (3UL << 2) | (3UL << 4));
    GPIO_MODER(GPIOB_BASE) |=  (1UL << 0);
    GPIO_PUPDR(GPIOB_BASE) &= ~((3UL << 0) | (3UL << 2) | (3UL << 4));
    GPIO_PUPDR(GPIOB_BASE) |=  (2UL << 2) | (1UL << 4);
    TCLK_H();

    GPIO_MODER(GPIOC_BASE) &= ~(3UL << 26);
    GPIO_MODER(GPIOC_BASE) |=  (1UL << 26);
    GPIO_PUPDR(GPIOC_BASE) &= ~(3UL << 26);
    TCS_H();

    GPIO_MODER(GPIOF_BASE) &= ~(3UL << 22);
    GPIO_MODER(GPIOF_BASE) |=  (1UL << 22);
    GPIO_PUPDR(GPIOF_BASE) &= ~(3UL << 22);
    TDIN(1);
}

/* ------------------------- 位 bang SPI ------------------------- */
static void tp_write_byte(uint8_t num)
{
    int count;
    for (count = 0; count < 8; ++count)
    {
        TDIN(num & 0x80);
        num <<= 1;
        TCLK_L();
        board_delay_us(1);
        TCLK_H();                    /* 上升沿移出 */
    }
}

static uint16_t tp_read_ad(uint8_t cmd)
{
    int count;
    uint32_t num = 0;
    TCLK_L();
    TDIN(0);
    TCS_L();
    tp_write_byte(cmd);
    board_delay_us(6);               /* ADS7846 转换最长 6us */
    TCLK_L();
    board_delay_us(1);
    TCLK_H();                        /* 清 BUSY */
    board_delay_us(1);
    TCLK_L();
    for (count = 0; count < 16; ++count)
    {
        num <<= 1;
        TCLK_L();                    /* 下降沿后数据有效 */
        board_delay_us(1);
        TCLK_H();
        if (DOUT)
            num++;
    }
    num >>= 4;                       /* 高 12 位有效 */
    TCS_H();
    return (uint16_t)num;
}

/* ------------------------- 采样与映射 ------------------------- */
#define READ_TIMES 16
#define LOST_VAL   2

static uint16_t tp_read_xoy(uint8_t xy)
{
    uint16_t buf[READ_TIMES];
    uint32_t sum = 0;
    int i, j;
    for (i = 0; i < READ_TIMES; ++i)
        buf[i] = tp_read_ad(xy);
    for (i = 0; i < READ_TIMES - 1; ++i)                 /* 升序 */
    {
        for (j = i + 1; j < READ_TIMES; ++j)
        {
            if (buf[i] > buf[j])
            {
                uint16_t t = buf[i];
                buf[i] = buf[j];
                buf[j] = t;
            }
        }
    }
    for (i = LOST_VAL; i < READ_TIMES - LOST_VAL; ++i)
        sum += buf[i];
    return (uint16_t)(sum / (READ_TIMES - 2 * LOST_VAL));
}

/* 原始值映射到面板坐标（横屏 480x320；旋钮可翻转/交换轴）。 */
static void tp_map(uint16_t rx, uint16_t ry, int* px, int* py)
{
    int32_t x = rx;
    int32_t y = ry;
#if XINYUE_TP_SWAP_XY
    { int32_t t = x; x = y; y = t; }
#endif
    x = (x - XINYUE_TP_XMIN) * BOARD_LCD_WIDTH
        / (XINYUE_TP_XMAX - XINYUE_TP_XMIN);
    y = (y - XINYUE_TP_YMIN) * BOARD_LCD_HEIGHT
        / (XINYUE_TP_YMAX - XINYUE_TP_YMIN);
#if XINYUE_TP_FLIP_X
    x = BOARD_LCD_WIDTH - 1 - x;
#endif
#if XINYUE_TP_FLIP_Y
    y = BOARD_LCD_HEIGHT - 1 - y;
#endif
    if (x < 0) x = 0;
    if (x > BOARD_LCD_WIDTH - 1) x = BOARD_LCD_WIDTH - 1;
    if (y < 0) y = 0;
    if (y > BOARD_LCD_HEIGHT - 1) y = BOARD_LCD_HEIGHT - 1;
    *px = (int)x;
    *py = (int)y;
}

bool board_touch_read(int* outX, int* outY)
{
    uint16_t x1, y1, x2, y2;
    if (!PEN_IRQ)
        return false;
    x1 = tp_read_xoy(CMD_RDX);
    y1 = tp_read_xoy(CMD_RDY);
    if (!PEN_IRQ)                                        /* 采样中抬起 */
        return false;
    x2 = tp_read_xoy(CMD_RDX);
    y2 = tp_read_xoy(CMD_RDY);
    /* 前后两次 ±50 一致性（模板 TP_Read_XY2 口径）。 */
    if (!((x2 <= x1 && x1 < x2 + 50) || (x1 <= x2 && x2 < x1 + 50)))
        return false;
    if (!((y2 <= y1 && y1 < y2 + 50) || (y1 <= y2 && y2 < y1 + 50)))
        return false;
    tp_map((uint16_t)((x1 + x2) / 2), (uint16_t)((y1 + y2) / 2),
           outX, outY);
    return true;
}
