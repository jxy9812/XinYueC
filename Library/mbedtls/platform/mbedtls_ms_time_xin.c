/******************************************************************************
 * @file       mbedtls_ms_time_xin.c
 * @brief      mbedTLS 毫秒时钟统一桥接（MBEDTLS_PLATFORM_MS_TIME_ALT）。
 * @details    platform_util.c 默认的 mbedtls_ms_time 只有 Unix/Windows 两套
 *             自调平台 API 实现（clock_gettime/GetSystemTimeAsFileTime），
 *             裸机直接 #error。本文件把毫秒时钟统一收敛到库内跨平台 API
 *             XDateTime_currentMSecsSinceEpoch()：桌面与裸机同一入口，
 *             平台差异（POSIX clock_gettime / Win32 FILETIME / FreeRTOS
 *             tick 等）由 XDateTime 与 Drive/<平台> 后端消化。
 * @note       mbedtls 仅消费相对时长（握手/重传超时等），起点因平台而异
 *             无碍；单调性由 XDateTime 后端保证。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "mbedtls/platform_time.h"

#if defined(MBEDTLS_PLATFORM_MS_TIME_ALT)

#include "XDateTime.h"

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t) XDateTime_currentMSecsSinceEpoch();
}

#endif /* MBEDTLS_PLATFORM_MS_TIME_ALT */
