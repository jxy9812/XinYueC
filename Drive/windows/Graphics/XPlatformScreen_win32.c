/******************************************************************************
 * @file       XPlatformScreen_win32.c
 * @brief      XPlatformScreen 契约的 Win32 实现（活动主屏的 DPI 与尺寸）。
 * @details    统一平台 API 的 Win32 侧：queryDpi 经屏幕 DC 的
 *             LOGPIXELSX/Y（进程为每监视器 DPI 感知时即活动监视器真实
 *             DPI，见 XPlatformNativeWindow_win32.c 的感知初始化）；
 *             queryFrameSize 取主屏设备尺寸。多屏差异按契约口径由实现
 *             内部消化（主屏视图）。queryOrigin 恒 false——桌面窗口
 *             系统直接投递窗口本地坐标，无触屏原点概念（与 X11 同口径）。
 *             last-known-good：Win32 读数无抖动（不像安卓 Activity 过渡
 *             期），直接实时查询即可，无需缓存。
 * @note       平台 API 只出现在 Drive；本文件仅在 _WIN32 下编译出实体，
 *             其他平台整文件为空（Unsupported 哨兵同样让位）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformScreen.h"

#if defined(_WIN32) && XGUI_ON

#include <windows.h>

bool XPlatformScreen_isAvailable(void)
{
    return true;
}

bool XPlatformScreen_queryDpi(XPlatformScreenDpiInfo* out)
{
    HDC dc;
    int dpiX;
    int dpiY;
    if (!out) return false;
    dc = GetDC(NULL); /* 主屏 DC：感知进程下 LOGPIXELS 即真实 DPI。 */
    if (!dc) return false;
    dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    if (dpiX <= 0 || dpiY <= 0) return false;
    out->m_density = XPlatformScreen_applyDpiOverride((float)dpiX / 96.0f);
    out->m_densityDpi = dpiX;
    out->m_xdpi = dpiX;
    out->m_ydpi = dpiY;
    out->m_valid = true;
    return true;
}

bool XPlatformScreen_queryFrameSize(int* outWidth, int* outHeight)
{
    int w;
    int h;
    if (!outWidth || !outHeight) return false;
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    if (w <= 0 || h <= 0) return false;
    *outWidth = w;
    *outHeight = h;
    return true;
}

bool XPlatformScreen_queryOrigin(int* outX, int* outY)
{
    /* 桌面无触屏原点概念：OS 直接投递窗口本地坐标（X11 同口径）。 */
    (void)outX;
    (void)outY;
    return false;
}

#endif /* defined(_WIN32) && XGUI_ON */
