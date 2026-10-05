/* ==================== 神舟号 W25Q128 SPI Flash 实现 ==================== */
#include "stm32f4xx.h"
#include "board_w25q.h"
#include "board_sys.h"

#define RCC_AHB1ENR_REG  (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_APB2ENR_REG  (*(volatile uint32_t*)(RCC_BASE + 0x44))
#define GPIO_MODER(p)    (*(volatile uint32_t*)((p) + 0x00))
#define GPIO_OSPEEDR(p)  (*(volatile uint32_t*)((p) + 0x08))
#define GPIO_PUPDR(p)    (*(volatile uint32_t*)((p) + 0x0C))
#define GPIO_BSRR(p)     (*(volatile uint32_t*)((p) + 0x18))
#define GPIO_AFRL(p)     (*(volatile uint32_t*)((p) + 0x20))

#define W25_CS_LOW()   (GPIO_BSRR(GPIOB_BASE) = (1UL << 14))
#define W25_CS_HIGH()  (GPIO_BSRR(GPIOB_BASE) = (1UL << 30))

static void spi1_rw_wait(void)
{
    while (!(SPI1->SR & (1UL << 0)))
        ;                                        /* RXNE */
}

static uint8_t spi1_xfer(uint8_t v)
{
    *(volatile uint8_t*)&SPI1->DR = v;
    spi1_rw_wait();
    return *(volatile uint8_t*)&SPI1->DR;
}

bool board_w25q_init(void)
{
    RCC_AHB1ENR_REG |= (1UL << 1);
    RCC_APB2ENR_REG |= (1UL << 12);               /* SPI1 */

    /* PB3/4/5 → AF5；PB14 推挽输出（片选，空闲高）。 */
    GPIO_MODER(GPIOB_BASE)   |= (2UL << 6) | (2UL << 8) | (2UL << 10)
                              | (1UL << 28);
    GPIO_OSPEEDR(GPIOB_BASE) |= (3UL << 6) | (3UL << 8) | (3UL << 10);
    GPIO_PUPDR(GPIOB_BASE)   |= (1UL << 28);
    GPIO_AFRL(GPIOB_BASE)    |= (5UL << 12) | (5UL << 16) | (5UL << 20);
    W25_CS_HIGH();

    SPI1->CR1 = 0;
    SPI1->CR1 = (1UL << 2)                        /* MSTR 主机 */
              | (2UL << 3)                        /* 波特率 fPCLK/8=10.5MHz */
              | (1UL << 8)                        /* SSI 内部拉高 */
              | (1UL << 9)                        /* SSM 软件从选择 */
              | (1UL << 6);                       /* SPE 使能 */
    SPI1->CR2 = 0;

    return true;
}

uint32_t board_w25q_jedec_id(void)
{
    uint32_t id;
    W25_CS_LOW();
    spi1_xfer(0x9F);
    id  = (uint32_t)spi1_xfer(0xFF) << 16;
    id |= (uint32_t)spi1_xfer(0xFF) << 8;
    id |= (uint32_t)spi1_xfer(0xFF);
    W25_CS_HIGH();
    return id;
}
