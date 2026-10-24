/**
 * @file XAtomicLibcall_esp8266.c
 * @brief Xtensa LX106 的 GCC 原子操作 libcall 实现。
 * @details
 * ESP8266 的 Xtensa LX106 没有原子指令（无 LDADD/STADD，也没有双核），因此
 * GCC 会把原子读改写降级成 `__atomic_*` 形式的库调用。实测这些符号在
 * SDK 的 libhal/libmain/libgcc 以及工具链自带的 libgcc.a 里都不存在，链接期
 * 报 undefined reference。
 *
 * ESP8266 是单核，**关中断**即可构成临界区：关中断期间不响应任何中断或异常，
 * 不存在另一个执行上下文同时修改同一地址，满足 GCC 对这些 libcall 的正确性
 * 要求。
 *
 * ## 平台适配（2026-10-22 修正）
 *
 * 临界区的获取方式按平台二选一，由框架的 XPLATFORM_* 宏决定：
 *
 *   - **RTOS 平台**（XPLATFORM_FREERTOS=1，即定义了 `__FreeRTOS__`）：
 *     用 FreeRTOS 的 `taskENTER_CRITICAL()/taskEXIT_CRITICAL()`。
 *     SDK 强制要求 FreeRTOS.h 必须先于 task.h 出现，否则触发 #error。
 *
 *   - **裸机平台**（XPLATFORM_BAREMETAL=1，本工程 ESP8266 NONOS3 即此）：
 *     **不引入任何 FreeRTOS 头**，直接调用平台提供的
 *     `vPortEnterCritical()/vPortExitCritical()`（由 platform/stubs.c 实现，
 *     内容就是 rsil 15 关中断 + 嵌套计数，语义与 FreeRTOS 版一致）。
 *
 * 早期版本无条件 `#include "freertos/FreeRTOS.h"`，导致裸机工程必须在
 * `cfg/shim/freertos/` 下伪造两个假头才能编译——那是把平台差异藏进了构建
 * 脚手架。现在差异回到本文件的条件编译里，裸机工程不再需要任何 FreeRTOS
 * 相关头文件或 shim。
 *
 * @note Xtensa 是小端 LSB 优先，但 memcpy/memset 之外的字节序假设在此不成立
 *       ——下面的实现都是按机器字（1 或 4 字节）整体读写，不做字节拆分。
 */

#if defined(__ESP8266__) || defined(ESP8266)

#include <stdint.h>
#include <string.h>

#include "CXinYueConfig.h"   /* XPLATFORM_FREERTOS / XPLATFORM_BAREMETAL */

#if XPLATFORM_FREERTOS
/* ---- RTOS 平台：用 FreeRTOS 临界区 ---- */
/* SDK 强制要求 FreeRTOS.h 必须先于 task.h 等其它头出现，否则触发 #error。 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define XATOMIC_ENTER_CRITICAL()  taskENTER_CRITICAL()
#define XATOMIC_EXIT_CRITICAL()   taskEXIT_CRITICAL()
#else
/* ---- 裸机平台：直接关中断，不引入任何 FreeRTOS 头 ----
 * 实现在 platform/stubs.c：rsil 15 + 嵌套计数，仅最外层恢复 PS。 */
void vPortEnterCritical(void);
void vPortExitCritical(void);
#define XATOMIC_ENTER_CRITICAL()  vPortEnterCritical()
#define XATOMIC_EXIT_CRITICAL()   vPortExitCritical()
#endif

/* ---- 1 字节宽度 ---- */

uint8_t __atomic_exchange_1(volatile void* ptr, uint8_t value, int memorder)
{
    uint8_t result;
    (void)memorder;
    XATOMIC_ENTER_CRITICAL();
    result = *(volatile uint8_t*)ptr;
    *(volatile uint8_t*)ptr = value;
    XATOMIC_EXIT_CRITICAL();
    return result;
}

_Bool __atomic_compare_exchange_1(volatile void* ptr, void* expected,
                                   uint8_t desired, int weak, int succ, int fail)
{
    _Bool result;
    (void)weak; (void)succ; (void)fail;
    XATOMIC_ENTER_CRITICAL();
    if (*(volatile uint8_t*)ptr == *(volatile uint8_t*)expected) {
        *(volatile uint8_t*)ptr = desired;
        result = 1;
    }
    else {
        *(uint8_t*)expected = *(volatile uint8_t*)ptr;   /* 回写当前值 */
        result = 0;
    }
    XATOMIC_EXIT_CRITICAL();
    return result;
}

/* ---- 4 字节宽度 ---- */

uint32_t __atomic_exchange_4(volatile void* ptr, uint32_t value, int memorder)
{
    uint32_t result;
    (void)memorder;
    XATOMIC_ENTER_CRITICAL();
    result = *(volatile uint32_t*)ptr;
    *(volatile uint32_t*)ptr = value;
    XATOMIC_EXIT_CRITICAL();
    return result;
}

_Bool __atomic_compare_exchange_4(volatile void* ptr, void* expected,
                                   uint32_t desired, int weak, int succ, int fail)
{
    _Bool result;
    (void)weak; (void)succ; (void)fail;
    XATOMIC_ENTER_CRITICAL();
    if (*(volatile uint32_t*)ptr == *(volatile uint32_t*)expected) {
        *(volatile uint32_t*)ptr = desired;
        result = 1;
    }
    else {
        *(uint32_t*)expected = *(volatile uint32_t*)ptr;
        result = 0;
    }
    XATOMIC_EXIT_CRITICAL();
    return result;
}

uint32_t __atomic_fetch_add_4(volatile void* ptr, uint32_t value, int memorder)
{
    uint32_t result;
    (void)memorder;
    XATOMIC_ENTER_CRITICAL();
    result = *(volatile uint32_t*)ptr;
    *(volatile uint32_t*)ptr = result + value;
    XATOMIC_EXIT_CRITICAL();
    return result;
}

uint32_t __atomic_fetch_sub_4(volatile void* ptr, uint32_t value, int memorder)
{
    uint32_t result;
    (void)memorder;
    XATOMIC_ENTER_CRITICAL();
    result = *(volatile uint32_t*)ptr;
    *(volatile uint32_t*)ptr = result - value;
    XATOMIC_EXIT_CRITICAL();
    return result;
}

#endif /* defined(__ESP8266__) || defined(ESP8266) */
