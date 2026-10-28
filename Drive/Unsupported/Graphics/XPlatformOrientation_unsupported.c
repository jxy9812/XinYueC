/******************************************************************************
 * @file       XPlatformOrientation_unsupported.c
 * @brief      未提供显示方向实现的平台存根（XPlatformOrientation 契约回落路径）。
 * @details    严格遵循 Drive 平台存根惯例（见 XPlatformScreen_unsupported.c）：
 *             在无显示方向实现平台/配置下保持 XPlatformOrientation 契约
 *             可链接。能力位恒 0、申请恒失败、回读恒 -1——调用方按「本
 *             平台不支持变更显示方向」退化（界面把相关控件置灰），
 *             **不编造能力**（0=未知，严禁编造，见 XPlatformDisplayDriver.h
 *             口径）。
 * @note       守卫与全部真实实现严格互斥（XPlatformNativeWindow 哨兵同款
 *             纪律），保证任意平台恰好编译一份（同符号双定义由链接器随
 *             机择一的防线）：
 *             - 安卓：Drive/Android/Graphics/XPlatformOrientation_android.c
 *               （Activity.setRequestedOrientation；安卓必须显式排除，
 *               否则本哨兵会与之同符号双定义——XPlatformAccessibility
 *               安卓侧踩过的坑）。
 *             - POSIX（Linux/X11）：Drive/Posix/Graphics/XPlatformOrientation_posix.c
 *             - Win32：Drive/windows/Graphics/XPlatformOrientation_win32.c
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformOrientation.h"

#if XGUI_ON && !defined(_WIN32) && !defined(__ANDROID__) && \
    !defined(__linux__)

unsigned XPlatformOrientationDriver_capabilities(void)
{
    return 0u;
}

bool XPlatformOrientationDriver_setOrientation(int degrees)
{
    (void)degrees;
    return false;
}

int XPlatformOrientationDriver_currentOrientation(void)
{
    return -1;
}

#endif /* XGUI_ON && !defined(_WIN32) && !defined(__ANDROID__) */
