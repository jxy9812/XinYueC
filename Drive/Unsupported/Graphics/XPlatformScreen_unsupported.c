/******************************************************************************
 * @file       XPlatformScreen_unsupported.c
 * @brief      未提供屏幕信息源的平台存根（XPlatformScreen 契约回落路径）。
 * @details    严格遵循 Drive 平台存根惯例（见
 *             XPlatformNativeWindow_unsupported.c）：在无屏幕信息源实现
 *             的平台/配置下保持 XPlatformScreen 契约可链接。全部查询
 *             返回 false——调用方按自身安全退化链处理（fbdev/桌面窗口
 *             系统平台的 DPI 值经 WSI 推送通道（handleScreen*）已就位，
 *             不依赖本契约的拉取面）。
 * @note       守卫与安卓真实实现（Drive/Android/Core/XSystemAndroid.c）
 *             严格互斥，保证任意平台恰好编译一份（同符号双定义由链接器
 *             随机择一的防线，XPlatformNativeWindow 哨兵同款纪律）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformScreen.h"

/* 守卫与全部真实实现严格互斥（XPlatformNativeWindow 哨兵同款纪律）：
 * - 安卓：Drive/Android/Core/XSystemAndroid.c（JNI DisplayMetrics）
 * - Win32：Drive/windows/Graphics/XPlatformScreen_win32.c（LOGPIXELS）
 * - X11：Drive/Posix/Graphics/XPlatformNativeWindow_posix.c（Xft.dpi）
 * - fbdev：Drive/Posix/Graphics/XPlatformFramebuffer_posix.c（面板 mm）
 * FreeRTOS/STM32 无独立实现，同样回落本哨兵（用户裁定 2026-10-01：
 * 默认调用级别，无源可拉时查询恒 false）。 */
#if XGUI_ON && !defined(__ANDROID__) && !defined(_WIN32) && \
    !(defined(__linux__) && (defined(XINYUE_C_HAS_X11) || \
      (defined(XPLATFORM_FBDEV_ON) && XPLATFORM_FBDEV_ON)))

bool XPlatformScreen_isAvailable(void)
{
    return false;
}

bool XPlatformScreen_queryDpi(XPlatformScreenDpiInfo* out)
{
    (void)out;
    return false;
}

bool XPlatformScreen_queryOrigin(int* outX, int* outY)
{
    (void)outX;
    (void)outY;
    return false;
}

bool XPlatformScreen_queryFrameSize(int* outWidth, int* outHeight)
{
    (void)outWidth;
    (void)outHeight;
    return false;
}

#endif /* XGUI_ON && !defined(__ANDROID__) */
