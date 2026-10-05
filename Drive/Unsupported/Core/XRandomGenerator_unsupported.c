/******************************************************************************
 * @file       XRandomGenerator_unsupported.c
 * @brief      安全随机数平台后端存根（FreeRTOS/裸机等无 OS 熵源的平台）。
 * @details    严格遵循 Drive 平台存根惯例（见 XSystem_unsupported.c）：
 *             XRandomGenerator_platformFillSecure() 是库内统一安全随机
 *             入口（mbedtls PSA EXTERNAL_RNG 桥接、XRandomGenerator 内部
 *             播种均经此），桌面端由 Drive/windows、Drive/Posix 各自桥接
 *             系统 CPRNG；裸机没有 OS 熵源，本存根默认返回 false——
 *             上层据此报告「无熵可用」（PSA_ERROR_INSUFFICIENT_ENTROPY），
 *             而不是静默退化成可预测的伪随机。
 *
 *             产品接入硬件 RNG（如 STM32 RNG 外设）时，把本文件替换为
 *             平台专属实现（或定义 XINYUE_PLATFORM_FILL_SECURE 指向
 *             固件提供的填充函数）：
 *                 bool XINYUE_PLATFORM_FILL_SECURE(void* buffer, size_t size);
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRandomGenerator.h"

#if !defined(__linux__) && !defined(_WIN32)

#if defined(XINYUE_PLATFORM_FILL_SECURE)
/* 固件自带硬件 RNG 填充：经编译选项指名接入。 */
extern bool XINYUE_PLATFORM_FILL_SECURE(void* buffer, size_t size);

bool XRandomGenerator_platformFillSecure(void* buffer, size_t size)
{
    if (!buffer || size == 0) return false;
    return XINYUE_PLATFORM_FILL_SECURE(buffer, size);
}
#else
/* 无熵源存根：显式失败，不伪造随机性。 */
bool XRandomGenerator_platformFillSecure(void* buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return false;
}
#endif

#endif /* !defined(__linux__) && !defined(_WIN32) */
