/******************************************************************************
 * @file       XPlatformBackingStore_posix.c
 * @brief      Linux XPlatformBackingStore 平台提交驱动。
 * @details    Linux 后端的全部软件缓冲逻辑（双缓冲 XImage、resize/滚动/
 *             静态内容/tile 遍历/外部缓冲）已上提到公共层
 *             Src/XGui/Platform/XPlatformBackingStore.c；本文件只提供
 *             「把脏区提交到 X11 窗口」的 Driver 钩子：
 *             - 窗口已挂接真实原生窗口（WId 非 0）时，经
 *               XPlatformNativeWindow_present（XPutImage）上屏；
 *             - 否则 no-op（公共层统一触发的 present 回调交给显示驱动）。
 *             本文件不包含任何 Linux API 头，因此同一实现也可在需要时
 *             迁往其它软件光栅化平台。
 * @note       文件同时受 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 总
 *             开关约束（Drive 平台后端惯例）。
 ******************************************************************************/

#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#if defined(__linux__)

#include "XPlatformNativeWindow.h"
#include "XWindow.h"

/* ==================== 平台提交驱动（对标 XPlatformGraphicsDriver_*） ==================== */

bool XPlatformBackingStoreDriver_create(void** outState, XWindow* window)
{
    (void)window;
    if (outState) *outState = NULL;
    /* 软件缓冲始终可用；真实提交按运行期窗口挂接情况决定。 */
    return true;
}

void XPlatformBackingStoreDriver_destroy(void* nativeState)
{
    (void)nativeState;
}

void XPlatformBackingStoreDriver_setNativeTarget(void* nativeState,
                                                 void* nativeWindow)
{
    (void)nativeState; (void)nativeWindow;
}

void XPlatformBackingStoreDriver_surfaceResized(void* nativeState,
                                                int width, int height)
{
    (void)nativeState; (void)width; (void)height;
}

void XPlatformBackingStoreDriver_present(void* nativeState, XWindow* window,
                                         const XImage* image,
                                         const XRegion* region,
                                         const XPoint* offset, bool full)
{
    (void)nativeState; (void)full;
    if (window && XPlatformNativeWindow_winId(window) != 0)
        XPlatformNativeWindow_present(window, image, region, offset);
}

void XPlatformBackingStoreDriver_presentTile(void* nativeState,
                                             XWindow* window,
                                             const XImage* image,
                                             const XRegion* region,
                                             const XPoint* offset)
{
    (void)nativeState;
    if (window && XPlatformNativeWindow_winId(window) != 0)
        XPlatformNativeWindow_present(window, image, region, offset);
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && defined(__linux__) */
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
