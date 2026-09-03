/******************************************************************************
 * @file       XPlatformNativeWindow_unsupported.c
 * @brief      未提供平台原生窗口后端的平台存根（XWindow/…
 *             XPlatformNativeWindow 回落路径）。
 * @details    本文件严格遵循 Drive 平台存根惯例（见
 *             XPlatformBackingStore_unsupported.c / XSystem_unsupported.c）：
 *             在既非 Linux X11（未检出 X11 或子开关关闭）也非 Windows
 *             Win32（子开关关闭）的平台/配置下，保持 XPlatformNativeWindow
 *             契约可链接。所有操作退化为空值：isAvailable 恒 false，
 *             create/属性同步/事件泵/上屏均安全无操作，winId 恒 0，
 *             windowForWinId 恒 NULL，nativeConnection 恒 NULL。
 *             这样 XWindow 首次显示时稳定回落嵌入式自增虚拟 WId 行为，
 *             不连接任何窗口系统，不影响嵌入式构建。
 * @note       模块总开关 XPLATFORMNATIVEWINDOW_ON 与平台子开关
 *             XPLATFORMNATIVEWINDOW_X11_ON / XPLATFORMNATIVEWINDOW_WIN32_ON
 *             定义于 XGuiConfig.h；本文件的哨兵守卫与 Drive/Posix 与
 *             Drive/windows 两个真实实现严格互斥，保证任意平台恰好编译
 *             一份原生窗口后端。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeWindow.h"
#include <stddef.h>

#if XPLATFORMNATIVEWINDOW_ON

#if !XWINDOW_ON || !((defined(__linux__) && defined(XINYUE_C_HAS_X11) && \
       XPLATFORMNATIVEWINDOW_X11_ON) || \
      (defined(_WIN32) && XPLATFORMNATIVEWINDOW_WIN32_ON))

/* ==================== 可用性与生命周期（全部空值/无操作） ==================== */

bool XPlatformNativeWindow_isAvailable(void)
{
    return false;
}

bool XPlatformNativeWindow_create(XWindow* window)
{
    (void)window;
    return false;
}

bool XPlatformNativeWindow_attachForeign(XWindow* window, XWindowId nativeId)
{
    (void)window; (void)nativeId;
    return false;
}

void XPlatformNativeWindow_destroy(XWindow* window)
{
    (void)window;
}

/* ==================== 属性同步（全部无操作） ==================== */

bool XPlatformNativeWindow_setVisible(XWindow* window, bool visible)
{
    (void)window; (void)visible;
    return false;
}

bool XPlatformNativeWindow_setGeometry(XWindow* window, const XRect* geometry)
{
    (void)window; (void)geometry;
    return false;
}

bool XPlatformNativeWindow_setTitle(XWindow* window, const XString* title)
{
    (void)window; (void)title;
    return false;
}

bool XPlatformNativeWindow_setKeyboardGrabEnabled(XWindow* window, bool grab)
{
    (void)window; (void)grab;
    return false;
}

bool XPlatformNativeWindow_setMouseGrabEnabled(XWindow* window, bool grab)
{
    (void)window; (void)grab;
    return false;
}

bool XPlatformNativeWindow_requestActivate(XWindow* window)
{
    (void)window;
    return false;
}

XPixmap* XPlatformNativeWindow_grabWindow(XWindowId window,
                                          int x, int y, int w, int h)
{
    (void)window; (void)x; (void)y; (void)w; (void)h;
    return NULL;
}

/* ==================== 原生句柄与反查（恒空值） ==================== */

XWindowId XPlatformNativeWindow_winId(const XWindow* window)
{
    (void)window;
    return 0;
}

XWindow* XPlatformNativeWindow_windowForWinId(XWindowId id)
{
    (void)id;
    return NULL;
}

/* ==================== 原生事件泵（恒 false） ==================== */

bool XPlatformNativeWindow_processPendingEvents(void)
{
    return false;
}

bool XPlatformNativeWindow_waitForEvents(int maxMilliseconds)
{
    (void)maxMilliseconds;
    return false;
}

bool XPlatformNativeWindow_queryKeyboardModifiers(
        XKeyboardModifiers* outModifiers)
{
    (void)outModifiers;
    return false;
}

/* ==================== 上屏（恒 false，无窗口可提交） ==================== */

bool XPlatformNativeWindow_present(XWindow* window, const XImage* image,
                                   const XRegion* region,
                                   const XPoint* offset)
{
    (void)window; (void)image; (void)region; (void)offset;
    return false;
}

/* ==================== 原生连接（恒 NULL） ==================== */

void* XPlatformNativeWindow_nativeConnection(
        XPlatformNativeWindowConnectionType* outType)
{
    if (outType) *outType = XPlatformNativeWindowConnection_None;
    return NULL;
}

#endif /* 未提供真实平台实现的哨兵守卫 */
#endif /* XPLATFORMNATIVEWINDOW_ON */
