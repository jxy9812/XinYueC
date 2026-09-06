/******************************************************************************
 * @file       XPlatformBackingStore_unsupported.c
 * @brief      未提供平台后备存储后端的平台存根（XBackingStore 回落路径）。
 * @details    本文件严格遵循 Drive 平台存根惯例（见 XSystem_unsupported.c）：
 *             软件缓冲逻辑已全部收敛到公共层 Src/XGui/Platform/
 *             XPlatformBackingStore.c，本文件只提供 no-op Driver 钩子：
 *             Driver_create 返回 false，公共层 create 据此返回 NULL，
 *             XBackingStore_init 安全容错为「空后端」。
 *             在既非 Linux 也非 Windows、且未启用可复用软件后端模板
 *             （XPLATFORMBACKINGSTORE_SOFTWARE_ON=0）的平台（FreeRTOS/裸机/
 *             其它 RTOS）上保持 XBackingStore 可链接。产品需要在该类平台
 *             接入显示驱动时，打开 XPLATFORMBACKINGSTORE_SOFTWARE_ON 使用
 *             Drive/Software/Graphics/XPlatformBackingStore_software.c 的
 *             全功能软件后端，只提供 present 回调（显示驱动）即可。
 * @note       模块总开关 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 定义
 *             于 XGuiConfig.h；本文件同时编译时为二者共同的 1。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#if !defined(__linux__) && !defined(_WIN32) && \
    !XPLATFORMBACKINGSTORE_SOFTWARE_ON

/* ==================== 平台提交驱动（全部 no-op / 空值） ==================== */

bool XPlatformBackingStoreDriver_create(void** outState, XWindow* window)
{
    (void)window;
    if (outState) *outState = NULL;
    return false;
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
    (void)nativeState; (void)window; (void)image;
    (void)region; (void)offset; (void)full;
}

void XPlatformBackingStoreDriver_presentTile(void* nativeState,
                                             XWindow* window,
                                             const XImage* image,
                                             const XRegion* region,
                                             const XPoint* offset)
{
    (void)nativeState; (void)window; (void)image;
    (void)region; (void)offset;
}

#endif /* !defined(__linux__) && !defined(_WIN32) && \
          !XPLATFORMBACKINGSTORE_SOFTWARE_ON */

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
