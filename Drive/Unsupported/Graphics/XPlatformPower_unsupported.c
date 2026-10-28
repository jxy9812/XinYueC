/******************************************************************************
 * @file       XPlatformPower_unsupported.c
 * @brief      未提供电源管理实现的平台存根（XPlatformPower 契约回落路径）。
 * @details    严格遵循 Drive 平台存根惯例（见 XPlatformScreen_unsupported.c）：
 *             在无电源管理实现平台/配置下保持 XPlatformPower 契约可链接。
 *             能力位恒 0、动作恒失败——调用方按「本平台不具备电源管理
 *             能力」退化（界面把相关控件置灰），**不编造能力**（0=未知，
 *             严禁编造，见 XPlatformDisplayDriver.h 口径）。
 * @note       守卫与全部真实实现严格互斥（XPlatformNativeWindow 哨兵同款
 *             纪律），保证任意平台恰好编译一份（同符号双定义由链接器随
 *             机择一的防线）：
 *             - POSIX（fbdev/X11）：Drive/Posix/Graphics/XPlatformPower_posix.c
 *             - Win32：Drive/windows/Graphics/XPlatformPower_win32.c
 *             安卓：Drive/Android/Graphics/XPlatformPower_android.c
 *               （委托系统「屏幕超时」；安卓必须显式排除，否则本哨兵会
 *               与之同符号双定义——XPlatformAccessibility 安卓侧踩过
 *               的坑）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformPower.h"

#if XGUI_ON && !defined(_WIN32) && !defined(__ANDROID__) && \
    !(defined(__linux__) && !defined(__ANDROID__))

unsigned XPlatformPowerDriver_capabilities(void)
{
    return 0u;
}

bool XPlatformPowerDriver_setScreenState(XPlatformPowerScreenState state)
{
    (void)state;
    return false;
}

bool XPlatformPowerDriver_setSystemState(XPlatformPowerSystemState state)
{
    (void)state;
    return false;
}

bool XPlatformPowerDriver_setScreenTimeout(int64_t millis)
{
    (void)millis;
    return false;
}

bool XPlatformPowerDriver_requestScreenTimeoutPermission(void)
{
    /* 无平台实现，恒 false（无需授权）。 */
    return false;
}

#endif /* XGUI_ON && !defined(_WIN32) */
