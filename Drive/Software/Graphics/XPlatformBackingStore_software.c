/******************************************************************************
 * @file       XPlatformBackingStore_software.c
 * @brief      可复用「全功能软件后备存储」平台模板（共享缓冲 + present 回调）。
 * @details    这是给 FreeRTOS/裸机/其它 RTOS 等新平台准备的现成后端：
 *             软件缓冲逻辑已全部收敛在公共层 Src/XGui/Platform/
 *             XPlatformBackingStore.c，本文件只提供 Driver 钩子——全部
 *             no-op，因为「上屏」完全交给公共层统一触发的 present 回调
 *             （显示驱动），平台零系统 API 依赖。
 *
 *             接入步骤（新平台）：
 *             1. 编译选项定义 XPLATFORMBACKINGSTORE_SOFTWARE_ON=1（默认 0，
 *                与 Drive/Unsupported 的 no-op 存根互斥）；
 *             2. 创建后备存储后调用
 *                XPlatformBackingStore_setPresentCallback(store, 回调, 数据)；
 *             3. 回调内从 XPlatformBackingStore_paintDevice(store) 取当前
 *                ARGB32 预乘帧缓冲，把 flushedRegion 的矩形（窗口坐标，
 *                offset 为缓冲相对窗口偏移）逐行拷贝到显示驱动缓冲区即可。
 *             无需修改本文件。若平台需要独立提交路径（如 DMA 双缓冲），
 *             可把本文件复制为平台专属文件并实现 Driver_present/presentTile。
 * @note       文件同时受 XBACKINGSTORE_ON、XPLATFORMBACKINGSTORE_ON 与
 *             XPLATFORMBACKINGSTORE_SOFTWARE_ON 总开关约束；软件模板与
 *             Unsupported 存根互斥编译。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && \
    XPLATFORMBACKINGSTORE_SOFTWARE_ON

#if !defined(__linux__) && !defined(_WIN32)

/* ==================== 平台提交驱动（提交全走公共层 present 回调） ==================== */

bool XPlatformBackingStoreDriver_create(void** outState, XWindow* window)
{
    (void)window;
    if (outState) *outState = NULL;
    /* 软件缓冲始终可用；上屏由显示驱动回调完成。 */
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

#endif /* !defined(__linux__) && !defined(_WIN32) */

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && \
          XPLATFORMBACKINGSTORE_SOFTWARE_ON */
