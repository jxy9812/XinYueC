/**
 * @file XAtomicLibcall_esp8266.c
 * @brief Xtensa LX106 的 GCC 原子操作 libcall 实现。
 * @details
 * ESP8266 的 Xtensa LX106 没有原子指令（无 LDADD/STADD，也没有双核），因此
 * GCC 会把原子读改写降级成 `__atomic_*` 形式的库调用。实测这些符号在
 * SDK 的 libhal/libmain/libgcc 以及工具链自带的 libgcc.a 里都不存在，链接期
 * 报 undefined reference。
 *
 * ESP8266 是单核且 FreeRTOS 关中断即可构成临界区，因此这里用
 * taskENTER_CRITICAL / taskEXIT_CRITICAL 实现单字宽度的原子语义：
 * 关中断期间不响应任何中断或异常，不存在另一个执行上下文同时修改同一地址，
 * 满足 GCC 对这些 libcall 的正确性要求。
 *
 * @note Xtensa 是小端 LSB 优先，但 memcpy/memset 之外的字节序假设在此不成立
 *       ——下面的实现都是按机器字（1 或 4 字节）整体读写，不做字节拆分。
 */

#if defined(__ESP8266__) || defined(ESP8266)

#include <stdint.h>
#include <string.h>

/* SDK 强制要求 FreeRTOS.h 必须先于 task.h 等其它头出现，否则触发 #error。 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---- 1 字节宽度 ---- */

uint8_t __atomic_exchange_1(volatile void* ptr, uint8_t value, int memorder)
{
    uint8_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile uint8_t*)ptr;
    *(volatile uint8_t*)ptr = value;
    taskEXIT_CRITICAL();
    return result;
}

_Bool __atomic_compare_exchange_1(volatile void* ptr, void* expected,
                                   uint8_t desired, int weak, int succ, int fail)
{
    _Bool result;
    (void)weak; (void)succ; (void)fail;
    taskENTER_CRITICAL();
    if (*(volatile uint8_t*)ptr == *(volatile uint8_t*)expected) {
        *(volatile uint8_t*)ptr = desired;
        result = 1;
    }
    else {
        *(uint8_t*)expected = *(volatile uint8_t*)ptr;   /* 回写当前值 */
        result = 0;
    }
    taskEXIT_CRITICAL();
    return result;
}

/* ---- 4 字节宽度 ---- */

uint32_t __atomic_exchange_4(volatile void* ptr, uint32_t value, int memorder)
{
    uint32_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile uint32_t*)ptr;
    *(volatile uint32_t*)ptr = value;
    taskEXIT_CRITICAL();
    return result;
}

_Bool __atomic_compare_exchange_4(volatile void* ptr, void* expected,
                                   uint32_t desired, int weak, int succ, int fail)
{
    _Bool result;
    (void)weak; (void)succ; (void)fail;
    taskENTER_CRITICAL();
    if (*(volatile uint32_t*)ptr == *(volatile uint32_t*)expected) {
        *(volatile uint32_t*)ptr = desired;
        result = 1;
    }
    else {
        *(uint32_t*)expected = *(volatile uint32_t*)ptr;
        result = 0;
    }
    taskEXIT_CRITICAL();
    return result;
}

uint32_t __atomic_fetch_add_4(volatile void* ptr, uint32_t value, int memorder)
{
    uint32_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile uint32_t*)ptr;
    *(volatile uint32_t*)ptr = result + value;
    taskEXIT_CRITICAL();
    return result;
}

uint32_t __atomic_fetch_sub_4(volatile void* ptr, uint32_t value, int memorder)
{
    uint32_t result;
    (void)memorder;
    taskENTER_CRITICAL();
    result = *(volatile uint32_t*)ptr;
    *(volatile uint32_t*)ptr = result - value;
    taskEXIT_CRITICAL();
    return result;
}

#endif /* defined(__ESP8266__) || defined(ESP8266) */
