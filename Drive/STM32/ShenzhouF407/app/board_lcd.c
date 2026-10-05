/* ==================== 神舟号 3.5 寸 TFT LCD 驱动实现 ====================
 * 时序/探测/初始化序列均按神舟出厂测试例程（ALIENTEK 同源）移植：
 * - 读时序 BTR4 = ADDSET 0xF / DATAST 0x60（ID 探测可靠）；
 * - 写时序 BWTR4 先 9/17，ID 确认为 5310/9488 后收紧 3/2；
 * - 横屏 MADCTL：5310=0x00（无需 BGR 位）、9488=0x08（BGR）。
 */
#include "stm32f4xx.h"
#include "board_lcd.h"
#include "board_sys.h"
#include "board_gpio.h"
#include "lcd_init_tables.inc"
#include "lcd_init_9806.inc"
#include "lcd_init_9486.inc"

/* ------------------------- FSMC 寄存器速记 ------------------------- */
#define RCC_AHB1ENR_REG  (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_AHB3ENR_REG  (*(volatile uint32_t*)(RCC_BASE + 0x38))
#define GPIO_MODER(p)    (*(volatile uint32_t*)((p) + 0x00))
#define GPIO_OTYPER(p)   (*(volatile uint32_t*)((p) + 0x04))
#define GPIO_OSPEEDR(p)  (*(volatile uint32_t*)((p) + 0x08))
#define GPIO_PUPDR(p)    (*(volatile uint32_t*)((p) + 0x0C))
#define GPIO_BSRR(p)     (*(volatile uint32_t*)((p) + 0x18))
#define GPIO_AFRL(p)     (*(volatile uint32_t*)((p) + 0x20))
#define GPIO_AFRH(p)     (*(volatile uint32_t*)((p) + 0x24))

/* FSMC Bank1 NORSRAM4（NE4）：BCR4/BTR4/BWTR4 */
#define FSMC_BASE        0xA0000000UL
#define FSMC_BCR4_REG    (*(volatile uint32_t*)(FSMC_BASE + 0x18))
#define FSMC_BTR4_REG    (*(volatile uint32_t*)(FSMC_BASE + 0x1C))
#define FSMC_BWTR4_REG   (*(volatile uint32_t*)(FSMC_BASE + 0x11C))

/* 命令/数据地址（RS=A6：0x6C00_0000 段内偏移 0x7E 对齐 16 位总线）。 */
#define LCD_CMD_ADDR     0x6C00007EUL
#define LCD_DATA_ADDR    0x6C000080UL
#define LCD_WR_CMD(v)    (*(volatile uint16_t*)LCD_CMD_ADDR = (uint16_t)(v))
#define LCD_WR_DATA(v)   (*(volatile uint16_t*)LCD_DATA_ADDR = (uint16_t)(v))
#define LCD_RD_DATA()    (*(volatile uint16_t*)LCD_DATA_ADDR)

static uint16_t s_lcd_id;
static int s_lcd_width  = BOARD_LCD_WIDTH;
static int s_lcd_height = BOARD_LCD_HEIGHT;

uint16_t board_lcd_id(void) { return s_lcd_id; }

/* ------------------------- GPIO / FSMC ------------------------- */
static void lcd_gpio_fsmc_init(void)
{
    /* GPIO B/D/E/F/G 时钟（bit1/3/4/5/6）+ FSMC（AHB3）。
     * 注意：此掩码原先误写成 bit1/2/5/6/7（B/C/F/G/H），GPIOD/GPIOE
     * 时钟未开 → NOE/NWE/数据线不动作，LCD 读 ID 恒 0、外扩 SRAM 总线死。 */
    RCC_AHB1ENR_REG |= (1UL << 1) | (1UL << 3) | (1UL << 4) | (1UL << 5)
                     | (1UL << 6);
    RCC_AHB3ENR_REG |= (1UL << 0);                       /* FSMC */

    /* PB15 背光：普通推挽输出，先低（熄灭）。 */
    GPIO_MODER(GPIOB_BASE)   &= ~(3UL << 30);
    GPIO_MODER(GPIOB_BASE)   |=  (1UL << 30);
    GPIO_BSRR(GPIOB_BASE)    = (1UL << (15 + 16));

    /* PD0,1(D2,3)、PD4(NOE)、PD5(NWE)、PD8-10(D13-15)、PD14,15(D0,1)
     * → FSMC AF12。MODER 必须写 10（复用）：早期 |= 3<<n 曾把引脚配成
     * 0b11 模拟模式，总线断开。 */
    {
        uint32_t m = (3UL << 0) | (3UL << 2) | (3UL << 8) | (3UL << 10)
                   | (3UL << 16) | (3UL << 18) | (3UL << 20) | (3UL << 28)
                   | (3UL << 30);
        gpio_moder_af(&GPIO_MODER(GPIOD_BASE), m);
        gpio_speed_high(&GPIO_OSPEEDR(GPIOD_BASE), m);
        gpio_pull_up(&GPIO_PUPDR(GPIOD_BASE), m);
        GPIO_AFRL(GPIOD_BASE)    &= ~((0xFUL << 0) | (0xFUL << 4)
                                    | (0xFUL << 16) | (0xFUL << 20));
        GPIO_AFRL(GPIOD_BASE)    |= (12UL << 0) | (12UL << 4)
                                  | (12UL << 16) | (12UL << 20);
        GPIO_AFRH(GPIOD_BASE)    &= ~((0xFUL << 0) | (0xFUL << 4)
                                    | (0xFUL << 8) | (0xFUL << 24)
                                    | (0xFUL << 28));
        GPIO_AFRH(GPIOD_BASE)    |= (12UL << 0) | (12UL << 4) | (12UL << 8)
                                  | (12UL << 24) | (12UL << 28);
    }
    /* PE7~15 → FSMC AF12（D4~D12）。 */
    {
        uint32_t m = 0xFFFFUL << 14;
        gpio_moder_af(&GPIO_MODER(GPIOE_BASE), m);
        gpio_speed_high(&GPIO_OSPEEDR(GPIOE_BASE), m);
        gpio_pull_up(&GPIO_PUPDR(GPIOE_BASE), m);
        GPIO_AFRL(GPIOE_BASE)    &= ~((0xFUL << 28));
        GPIO_AFRL(GPIOE_BASE)    |= (12UL << 28);          /* PE7 */
        GPIO_AFRH(GPIOE_BASE)    = (GPIO_AFRH(GPIOE_BASE)
                                    & 0x0000FFFFUL)
                                   | 0xCCCCCCCCUL;         /* PE8~15 全 AF12 */
    }
    /* PF12 = FSMC_A6，PG12 = FSMC_NE4 */
    gpio_moder_af(&GPIO_MODER(GPIOF_BASE), 3UL << 24);
    gpio_speed_high(&GPIO_OSPEEDR(GPIOF_BASE), 3UL << 24);
    gpio_pull_up(&GPIO_PUPDR(GPIOF_BASE), 3UL << 24);
    GPIO_AFRH(GPIOF_BASE)    &= ~(0xFUL << 16);
    GPIO_AFRH(GPIOF_BASE)    |= (12UL << 16);
    gpio_moder_af(&GPIO_MODER(GPIOG_BASE), 3UL << 24);
    gpio_speed_high(&GPIO_OSPEEDR(GPIOG_BASE), 3UL << 24);
    gpio_pull_up(&GPIO_PUPDR(GPIOG_BASE), 3UL << 24);
    GPIO_AFRH(GPIOG_BASE)    &= ~(0xFUL << 16);
    GPIO_AFRH(GPIOG_BASE)    |= (12UL << 16);

    /* NE4 16 位 SRAM 型，扩展模式（读写分时序）：
     * BCR4 = MBKEN | MWID=16b | WREN | EXTMOD */
    FSMC_BCR4_REG  = (1UL << 0) | (1UL << 4) | (1UL << 12) | (1UL << 14);
    FSMC_BTR4_REG  = (0x0FUL << 0) | (0x60UL << 8);      /* 读：16/96 个 HCLK */
    FSMC_BWTR4_REG = (0x09UL << 0) | (0x11UL << 8);      /* 写：9/17 个 HCLK */
}

static void lcd_write_timing_fast(void)
{
    /* 写时序：与 SRAM 共享数据总线，过紧（3/2 HCLK）时两 bank 切换
     * 相位余量不足，GUI 高频刷屏会偶发把总线数据写进外扩 SRAM
     * （heap_4 自由链表被污染的实测根因）。取保守的 9/17 HCLK。 */
    FSMC_BWTR4_REG = (0x09UL << 0) | (0x11UL << 8);
}

/* ------------------------- ID 探测 ------------------------- */
static uint16_t lcd_detect_id(void)
{
    uint32_t id;

    LCD_WR_CMD(0x0000);
    id = LCD_RD_DATA();
    if (id < 0xFF || id == 0xFFFF || id == 0x9300)
    {
        /* 0xD3：9341/9486/9488/7796 系列 */
        LCD_WR_CMD(0xD3);
        (void)LCD_RD_DATA();
        (void)LCD_RD_DATA();
        id  = LCD_RD_DATA();
        id <<= 8;
        id |= LCD_RD_DATA();
        if (id != 0x9488 && id != 0x9806 && id != 0x9486)
        {                       /* 9486/9488/9806 同用 0xD3 读 ID */
            /* 0xD4：NT35310 */
            LCD_WR_CMD(0xD4);
            (void)LCD_RD_DATA();
            (void)LCD_RD_DATA();
            id  = LCD_RD_DATA();
            id <<= 8;
            id |= LCD_RD_DATA();
            if (id != 0x5310)
                id = 0;
        }
    }
    return (uint16_t)id;
}

/* ------------------------- 初始化序列回放 ------------------------- */
static void lcd_play_init_table(const uint16_t* table)
{
    uint16_t reg;
    while ((reg = *table++) != 0xFFFE)
    {
        if (reg == 0xFFFF)
        {
            board_delay_ms(*table++);
            continue;
        }
        {
            uint16_t n = *table++;
            LCD_WR_CMD(reg);
            while (n--)
                LCD_WR_DATA(*table++);
        }
    }
}

/* ------------------------- 窗口 ------------------------- */
static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    LCD_WR_CMD(0x2A);
    LCD_WR_DATA((uint16_t)(x0 >> 8));
    LCD_WR_DATA((uint16_t)(x0 & 0xFF));
    LCD_WR_DATA((uint16_t)(x1 >> 8));
    LCD_WR_DATA((uint16_t)(x1 & 0xFF));
    LCD_WR_CMD(0x2B);
    LCD_WR_DATA((uint16_t)(y0 >> 8));
    LCD_WR_DATA((uint16_t)(y0 & 0xFF));
    LCD_WR_DATA((uint16_t)(y1 >> 8));
    LCD_WR_DATA((uint16_t)(y1 & 0xFF));
}

void board_lcd_init(void)
{
    int retry;

    lcd_gpio_fsmc_init();
    board_delay_ms(50);

    /* 出厂例程复位探测前奏：写 0x00 寄存器后延时再读 ID。
     * ILI9806 上电要 ~120ms 退出 sleep，过早读恒 0——重试至多 5 次。 */
    LCD_WR_CMD(0x00);
    LCD_WR_DATA(0x01);
    for (retry = 0; retry < 25; ++retry)
    {
        board_delay_ms(100);
        s_lcd_id = lcd_detect_id();
        if (s_lcd_id != 0)
            break;
    }

    if (s_lcd_id == 0x9488)
        lcd_play_init_table(xlcd_init_9488);
    else if (s_lcd_id == 0x5310)
        lcd_play_init_table(xlcd_init_5310);
    else if (s_lcd_id == 0x9806)
        lcd_play_init_table(xlcd_init_9806);
    else if (s_lcd_id == 0x9486)
        lcd_play_init_table(xlcd_init_9486);
    else
        return;                                      /* 未识别：不写时序 */

    if (s_lcd_id == 0x5310 || s_lcd_id == 0x9488 || s_lcd_id == 0x9486)
        lcd_write_timing_fast();                     /* 9806 保守起见沿用探测时序 */

    /* 横屏 480x320：MADCTL（5310 无需 BGR 位；9488 需 BGR=1；9806 用模板
     * 竖屏值 0xC0——方向如与触摸轴不符，调 board_touch.c 的 XINYUE_TP_*）
     * + 全窗钳位。 */
    /* 屏幕物理竖装（H1098 玻璃原生 320x480 竖屏）：9486 用出厂竖屏
     * MADCTL=0x08（BGR），窗口 0..319 × 0..479 不越界。 */
    LCD_WR_CMD(0x36);
    LCD_WR_DATA((s_lcd_id == 0x9488 || s_lcd_id == 0x9486) ? 0x08
              : (s_lcd_id == 0x9806 ? 0xC0 : 0x00));
    lcd_set_window(0, 0, s_lcd_width - 1, s_lcd_height - 1);
}

void board_lcd_backlight(int on)
{
    if (on)
        GPIO_BSRR(GPIOB_BASE) = (1UL << 15);
    else
        GPIO_BSRR(GPIOB_BASE) = (1UL << (15 + 16));
}

void board_lcd_fill(int x, int y, int w, int h, uint16_t color)
{
    int32_t count;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > s_lcd_width || y + h > s_lcd_height)
        return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    LCD_WR_CMD(0x2C);
    count = (int32_t)w * h;
    while (count--)
        LCD_WR_DATA(color);
}

void board_lcd_blit(int x, int y, int w, int h,
                    const uint8_t* src, int srcStrideBytes)
{
    int row;
    if (!src || x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > s_lcd_width || y + h > s_lcd_height)
        return;
    lcd_set_window(x, y, x + w - 1, y + h - 1);
    LCD_WR_CMD(0x2C);
    for (row = 0; row < h; ++row)
    {
        const uint16_t* line = (const uint16_t*)(src
            + (size_t)row * (size_t)srcStrideBytes);
        int col;
        for (col = 0; col < w; ++col)
            LCD_WR_DATA(line[col]);
    }
}
