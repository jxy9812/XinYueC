/* ==================== 神舟号 外扩 SRAM（IS62WV51216）实现 ====================
 * FSMC Bank1 NORSRAM3（NE3=PG10）。数据/读写线与 LCD 共享，A6 与 LCD RS
 * 共享（两 bank 片选互斥，总线时分复用，互不干扰）。
 * 时序按 55ns SRAM @168MHz HCLK 配置（读 1+10，写 0+4 个 HCLK）。
 */
#include "stm32f4xx.h"
#include "board_sram.h"
#include "board_sys.h"
#include "board_gpio.h"

#define RCC_AHB1ENR_REG  (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_AHB3ENR_REG  (*(volatile uint32_t*)(RCC_BASE + 0x38))
#define GPIO_MODER(p)    (*(volatile uint32_t*)((p) + 0x00))
#define GPIO_OSPEEDR(p)  (*(volatile uint32_t*)((p) + 0x08))
#define GPIO_PUPDR(p)    (*(volatile uint32_t*)((p) + 0x0C))
#define GPIO_AFRL(p)     (*(volatile uint32_t*)((p) + 0x20))
#define GPIO_AFRH(p)     (*(volatile uint32_t*)((p) + 0x24))

#define FSMC_BASE        0xA0000000UL
#define FSMC_BCR3_REG    (*(volatile uint32_t*)(FSMC_BASE + 0x10))
#define FSMC_BTR3_REG    (*(volatile uint32_t*)(FSMC_BASE + 0x14))
#define FSMC_BWTR3_REG   (*(volatile uint32_t*)(FSMC_BASE + 0x114))

static bool s_sram_ready;

/* 现场观测（J-Link/gdb 读取；串口不可用时的调试出口）：
 * [0]=magic [1]=BCR3 [2]=BTR3 [3]=probe 读回 [4]=verify 结果 */
volatile uint32_t g_sramDebug[8];

static void sram_gpio_fsmc_init(void)
{
    /* GPIO B/D/E/F/G 时钟（bit1/3/4/5/6）+ FSMC（AHB3）。 */
    RCC_AHB1ENR_REG |= (1UL << 1) | (1UL << 3) | (1UL << 4) | (1UL << 5)
                     | (1UL << 6);
    RCC_AHB3ENR_REG |= (1UL << 0);

    /* 自包含配置全部 FSMC 引脚（不依赖 LCD 先行——main 里 SRAM 先初始化，
     * 若 D0-15/NOE/NWE 尚未配置，verify 就会读写无效）：
     * - 地址线 A0~A18：PF0-5(A0-5)、PF12-15(A6-9)、PG0-5(A10-15)、PD11-13(A16-18)；
     * - 数据线 D0~D15：PD14,15(D0,1)、PD0,1(D2,3)、PE7-15(D4-12)、PD8-10(D13-15)；
     * - 控制：PD4=NOE、PD5=NWE、PG10=NE3、PE0/PE1=NBL0/NBL1(UB/LB)。 */
    {
        /* 各脚的 MODER/PUPDR 整字段掩码（2 位/脚，值 3）。
         * 注意：MODER 复用模式必须写 10——早期版本用 |= 3<<n 把引脚全配成
         * 0b11（模拟模式），总线彻底断开，这就是 LCD/SRAM 双双读 0 的根因。 */
        uint32_t mF = (3UL << 0) | (3UL << 2) | (3UL << 4)          /* PF0-2 */
                    | (3UL << 6) | (3UL << 8) | (3UL << 10)          /* PF3-5 */
                    | (3UL << 24) | (3UL << 26) | (3UL << 28) | (3UL << 30);
        gpio_moder_af(&GPIO_MODER(GPIOF_BASE), mF);
        gpio_speed_high(&GPIO_OSPEEDR(GPIOF_BASE), mF);
        gpio_pull_up(&GPIO_PUPDR(GPIOF_BASE), mF);
        GPIO_AFRL(GPIOF_BASE)    |= (0xCUL << 0) | (0xCUL << 4)   /* PF0,1 */
                                  | (0xCUL << 8) | (0xCUL << 12)  /* PF2,3 */
                                  | (0xCUL << 16) | (0xCUL << 20);/* PF4,5 */
        GPIO_AFRH(GPIOF_BASE)    |= (0xCUL << 16) | (0xCUL << 20) /* PF12,13 */
                                  | (0xCUL << 24) | (0xCUL << 28);/* PF14,15 */

        uint32_t mG = (3UL << 0) | (3UL << 2) | (3UL << 4)
                    | (3UL << 6) | (3UL << 8) | (3UL << 10)
                    | (3UL << 20);                                /* PG0-5,10 */
        gpio_moder_af(&GPIO_MODER(GPIOG_BASE), mG);
        gpio_speed_high(&GPIO_OSPEEDR(GPIOG_BASE), mG);
        gpio_pull_up(&GPIO_PUPDR(GPIOG_BASE), mG);
        GPIO_AFRL(GPIOG_BASE)    |= (0xCUL << 0) | (0xCUL << 4)   /* PG0,1 */
                                  | (0xCUL << 8) | (0xCUL << 12)  /* PG2,3 */
                                  | (0xCUL << 16) | (0xCUL << 20);/* PG4,5 */
        GPIO_AFRH(GPIOG_BASE)    |= (0xCUL << 8);                 /* PG10 NE3 */

        /* PE0/PE1 = FSMC_NBL0/NBL1（16 位 SRAM 的 UB/LB 字节使能，未驱动
         * 则芯片恒禁用输出——总线读恒 0）。PE7~15 = D4~D12。 */
        uint32_t mE = (3UL << 0) | (3UL << 2)                     /* PE0,1 */
                    | (3UL << 14) | (3UL << 16) | (3UL << 18)
                    | (3UL << 20) | (3UL << 22) | (3UL << 24)
                    | (3UL << 26) | (3UL << 28) | (3UL << 30);    /* PE7-15 */
        gpio_moder_af(&GPIO_MODER(GPIOE_BASE), mE);
        gpio_speed_high(&GPIO_OSPEEDR(GPIOE_BASE), mE);
        gpio_pull_up(&GPIO_PUPDR(GPIOE_BASE), mE);
        GPIO_AFRL(GPIOE_BASE)    |= (0xCUL << 0) | (0xCUL << 4)   /* PE0,1 */
                                  | (0xCUL << 28);                /* PE7 */
        GPIO_AFRH(GPIOE_BASE)    |= 0xCCCCCCCCUL;                 /* PE8~15 */

        /* PD0,1(D2,3)、PD4(NOE)、PD5(NWE)、PD8-10(D13-15)、PD14,15(D0,1)、
         * PD11-13(A16-18)。 */
        uint32_t mD = (3UL << 0) | (3UL << 2) | (3UL << 8) | (3UL << 10)
                    | (3UL << 16) | (3UL << 18) | (3UL << 20)
                    | (3UL << 22) | (3UL << 24) | (3UL << 26)
                    | (3UL << 28) | (3UL << 30);
        gpio_moder_af(&GPIO_MODER(GPIOD_BASE), mD);
        gpio_speed_high(&GPIO_OSPEEDR(GPIOD_BASE), mD);
        gpio_pull_up(&GPIO_PUPDR(GPIOD_BASE), mD);
        GPIO_AFRL(GPIOD_BASE)    |= (0xCUL << 0) | (0xCUL << 4)   /* PD0,1 */
                                  | (0xCUL << 16) | (0xCUL << 20);/* PD4,5 */
        GPIO_AFRH(GPIOD_BASE)    |= (0xCUL << 0) | (0xCUL << 4)   /* PD8,9 */
                                  | (0xCUL << 8) | (0xCUL << 12)  /* PD10,11 */
                                  | (0xCUL << 16) | (0xCUL << 20) /* PD12,13 */
                                  | (0xCUL << 24) | (0xCUL << 28);/* PD14,15 */
    }

    /* 与出厂例程同为 SRAM 型 16 位；为与 LCD bank 共享总线的切换相位
     * 留余量，时序在出厂口径上略放宽（ADDSET 2 + DATAST 15 HCLK）。 */
    FSMC_BCR3_REG  = (1UL << 0) | (1UL << 4) | (1UL << 12);
    FSMC_BTR3_REG  = (2UL << 0) | (15UL << 8);
}

bool board_sram_init(void)
{
    volatile uint16_t* base = (volatile uint16_t*)BOARD_SRAM_BASE;
    sram_gpio_fsmc_init();
    board_delay_ms(5);
    base[0] = 0x1234;
    s_sram_ready = (base[0] == 0x1234);
    g_sramDebug[0] = 0x52414D31UL;                /* "RAM1" */
    g_sramDebug[1] = FSMC_BCR3_REG;
    g_sramDebug[2] = FSMC_BTR3_REG;
    base[2] = 0xBEEF;
    g_sramDebug[3] = base[2];
    g_sramDebug[4] = s_sram_ready ? 1u : 0u;
    return s_sram_ready;
}

size_t board_sram_verify(void)
{
    volatile uint16_t* base = (volatile uint16_t*)BOARD_SRAM_BASE;
    size_t offset;
    int i;
    if (!s_sram_ready)
        return 0;

    for (i = 0; i < 4096; ++i) base[i] = (uint16_t)(0xAA55 ^ i);
    for (i = 0; i < 4096; ++i)
        if (base[i] != (uint16_t)(0xAA55 ^ i)) {
            g_sramDebug[5] = 0xA1;
            return 0;
        }

    offset = BOARD_SRAM_SIZE / 2 - 4096;
    for (i = 0; i < 4096; ++i) base[offset + i] = (uint16_t)(0x55AA ^ i);
    for (i = 0; i < 4096; ++i)
        if (base[offset + i] != (uint16_t)(0x55AA ^ i))
            return 0;

    offset = BOARD_SRAM_SIZE / 4;
    for (i = 0; i < 4096; ++i) base[offset + i] = (uint16_t)i;
    for (i = 0; i < 4096; ++i)
        if (base[offset + i] != (uint16_t)i)
            return 0;
    return BOARD_SRAM_SIZE;
}
