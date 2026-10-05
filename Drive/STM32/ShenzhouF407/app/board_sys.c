/* ==================== 神舟号 STM32F407 板级系统服务实现 ====================
 * 全寄存器级（仅依赖 CMSIS 设备头），不引入 SPL/HAL。
 */
#include "stm32f4xx.h"
#include "board_sys.h"
#include <stdint.h>
#include <stddef.h>

/* ------------------------- 寄存器速记 ------------------------- */
#define RCC_AHB1ENR_REG   (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_APB2ENR_REG   (*(volatile uint32_t*)(RCC_BASE + 0x44))
#define GPIO_MODER(p)     (*(volatile uint32_t*)((p) + 0x00))
#define GPIO_OTYPER(p)    (*(volatile uint32_t*)((p) + 0x04))
#define GPIO_OSPEEDR(p)   (*(volatile uint32_t*)((p) + 0x08))
#define GPIO_PUPDR(p)     (*(volatile uint32_t*)((p) + 0x0C))
#define GPIO_ODR(p)       (*(volatile uint32_t*)((p) + 0x14))
#define GPIO_AFRL(p)      (*(volatile uint32_t*)((p) + 0x20))
#define GPIO_AFRH(p)      (*(volatile uint32_t*)((p) + 0x24))

/* ------------------------- USART1 调试口 ------------------------- */
void board_uart1_init(void)
{
    RCC_AHB1ENR_REG |= (1UL << 0);              /* GPIOA 时钟 */
    RCC_APB2ENR_REG |= (1UL << 4);              /* USART1 时钟 */
    GPIO_MODER(GPIOA_BASE) &= ~(3UL << 18);
    GPIO_MODER(GPIOA_BASE) |=  (2UL << 18);     /* PA9 复用 */
    GPIO_OSPEEDR(GPIOA_BASE) |= (3UL << 18);
    GPIO_PUPDR(GPIOA_BASE)  &= ~(3UL << 18);
    GPIO_AFRH(GPIOA_BASE)   &= ~(0xFUL << 4);
    GPIO_AFRH(GPIOA_BASE)   |=  (7UL << 4);     /* AF7 = USART1 */
    /* APB2 84MHz：USARTDIV=84M/(16*115200)=45.5729 → 45+9/16 */
    USART1->BRR  = (45UL << 4) | 9UL;
    USART1->CR1  = (1UL << 13) | (1UL << 3);    /* UE | TE */
}

void board_putc(char c)
{
    if (c == '\n')
        board_putc('\r');
    while (!(USART1->SR & (1UL << 7)))
        ;                                        /* TXE */
    USART1->DR = (uint8_t)c;
}

/* ------------------------- DWT 延时 ------------------------- */
void board_delay_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

void board_delay_us(uint32_t us)
{
    /* 校准 NOP 忙等（168MHz 下 ~42 次空操作/µs，+Overal 头 ≈ 42）：
     * 不依赖 DWT——J-Link 会话断开冻结 CYCCNT 的坑已实测两次。 */
    volatile uint32_t n = us * 42u + 40u;
    while (n--)
        __NOP();
}

void board_delay_ms(uint32_t ms)
{
    while (ms--)
        board_delay_us(1000);
}

/* ------------------------- 外扩 SRAM 大块 bump 分配 -------------------------
 * SYSTEM 包装的大分配（≥8KB：后备存储/大图像缓冲）落外扩 SRAM；
 * 常驻对象语义（bump 不回收），小对象仍走片内 FreeRTOS 堆。 */
extern unsigned char __extbuf_start;
extern unsigned char __extbuf_end;
static unsigned char* s_extBrk = &__extbuf_start;

/* bump 块登记（≤8 块）：供 realloc 就地扩容与尺寸查询。 */
static struct { unsigned char* base; size_t size; } s_extTab[8];
static unsigned s_extCount;

int board_extbuf_alloc(void** out, size_t size)
{
    if ((size_t)(&__extbuf_end - s_extBrk) < size)
        return 0;
    *out = s_extBrk;
    if (s_extCount < 8)
    {
        s_extTab[s_extCount].base = s_extBrk;
        s_extTab[s_extCount].size = size;
        ++s_extCount;
    }
    s_extBrk += size;
    return 1;
}

size_t board_extbuf_size_of(const void* ptr)
{
    unsigned i;
    for (i = 0; i < s_extCount; ++i)
        if (s_extTab[i].base == ptr)
            return s_extTab[i].size;
    return 0;
}

static void board_extbuf_resize(void* ptr, size_t newSize)
{
    unsigned i;
    for (i = 0; i < s_extCount; ++i)
        if (s_extTab[i].base == ptr)
        {
            s_extBrk += (ptrdiff_t)newSize - (ptrdiff_t)s_extTab[i].size;
            s_extTab[i].size = newSize;
            return;
        }
}

int board_extbuf_owns(const void* ptr)
{
    unsigned i;
    for (i = 0; i < s_extCount; ++i)
        if ((const unsigned char*)ptr >= s_extTab[i].base
            && (const unsigned char*)ptr < s_extTab[i].base + s_extTab[i].size)
            return 1;
    return (const unsigned char*)ptr >= &__extbuf_start
        && (const unsigned char*)ptr < s_extBrk;
}

/* 就地扩容：ptr 是 bump 区最近一块（ptr+oldSize == s_extBrk）时直接延伸。 */
int board_extbuf_grow(void* ptr, size_t oldSize, size_t newSize)
{
    if ((unsigned char*)ptr + oldSize != s_extBrk)
        return 0;
    if ((size_t)(&__extbuf_end - (unsigned char*)ptr) < newSize)
        return 0;
    board_extbuf_resize(ptr, newSize);
    s_extBrk = (unsigned char*)ptr + newSize;
    return 1;
}

/* newlib 系统调用存根已上提 Drive/STM32/newlib_stubs.c（家族通用）。 */
