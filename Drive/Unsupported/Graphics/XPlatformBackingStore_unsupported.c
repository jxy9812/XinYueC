/******************************************************************************
 * @file       XPlatformBackingStore_unsupported.c
 * @brief      非 Linux/Windows 平台的后备存储 Driver 兜底：未启用软件后端时
 *             为安全降级存根；启用 XPLATFORMBACKINGSTORE_SOFTWARE_ON 时即
 *             全功能软件后端（原 Drive/Software 可复用模板，已并入本文件）。
 * @details    软件缓冲逻辑已全部收敛到公共层 Src/XGui/Platform/
 *             XPlatformBackingStore.c，本文件只提供 no-op Driver 钩子，
 *             由 XPLATFORMBACKINGSTORE_SOFTWARE_ON 分流两种形态：
 *
 *             =0（默认，Drive 平台存根惯例，见 XSystem_unsupported.c）：
 *             Driver_create 返回 false，公共层 create 据此返回 NULL，
 *             XBackingStore_init 安全容错为「空后端」，仅保持可链接。
 *
 *             =1（全功能软件后端）：Driver_create 返回 true，软件缓冲
 *             始终可用，「上屏」完全交给公共层统一触发的 present 回调
 *             （显示驱动），平台零系统 API 依赖。接入步骤（新平台）：
 *             1. 编译选项定义 XPLATFORMBACKINGSTORE_SOFTWARE_ON=1；
 *             2. 创建后备存储后调用
 *                XPlatformBackingStore_setPresentCallback(store, 回调, 数据)；
 *             3. 回调内从 XPlatformBackingStore_paintDevice(store) 取当前
 *                ARGB32 预乘帧缓冲，把 flushedRegion 的矩形（窗口坐标，
 *                offset 为缓冲相对窗口偏移）逐行拷贝到显示驱动缓冲区即可。
 *             无需修改本文件。若平台需要独立提交路径（如 DMA 双缓冲），
 *             可把本文件复制为平台专属文件并实现 Driver_present/presentTile。
 * @note       模块总开关 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 定义
 *             于 XGuiConfig.h；本文件同时编译时为二者共同的 1。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#if !defined(__linux__) && !defined(_WIN32)

/* ============ 平台提交驱动（钩子全 no-op，create 按 SOFTWARE_ON 分流） ============ */

bool XPlatformBackingStoreDriver_create(void** outState, XWindow* window)
{
    (void)window;
    if (outState) *outState = NULL;
#if XPLATFORMBACKINGSTORE_SOFTWARE_ON
    /* 软件缓冲始终可用；上屏由显示驱动回调完成。 */
    return true;
#else
    return false;
#endif
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

void* XPlatformBackingStoreDriver_getNativeBuffer(void* nativeState,
                                                  int width, int height,
                                                  size_t* outStride)
{
    (void)nativeState; (void)width; (void)height; (void)outStride;
    return NULL; /* 无共享缓冲能力：公共层回落自分配缓冲。 */
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

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
