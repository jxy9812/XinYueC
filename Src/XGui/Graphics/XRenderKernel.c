/******************************************************************************
 * @file       XRenderKernel.c
 * @brief      目标格式渲染内核表注册中心（对标 Skia blitter 的格式分派）。
 * @details    槽位数组按 XImageFormat 枚举值索引；内置内核（RGB565 首批）
 *             在 forFormat 首次调用时惰性注册，平台/加速器变体可随后经
 *             XRenderKernel_register 覆盖注入（NEON/Helium/DMA2D 挂点）。
 *             GUI 单线程访问，无锁（与 XPainter 既有静态缓存同纪律）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRenderKernel.h"

#if XPAINTER_ON

#include <stdbool.h>
#include <stddef.h> /* NULL */

/* 内置内核注册入口（XRenderKernel_rgb565.c 提供；新格式在此追加）。 */
extern void XRenderKernel_registerRgb565(void);

/** @brief 格式槽位表（索引即 XImageFormat 枚举值；NULL=未注册）。 */
static const XRenderKernelOps*
    g_kernelSlots[XRENDERKERNEL_FORMAT_SLOTS];

/** @brief 内置内核是否已注册（惰性一次性）。 */
static bool g_kernelBuiltinRegistered;

/** @brief 惰性注册内置内核（幂等）。 */
static void xrenderkernel_registerBuiltins(void)
{
    if (g_kernelBuiltinRegistered) return;
    g_kernelBuiltinRegistered = true;
    XRenderKernel_registerRgb565();
}

void XRenderKernel_register(XImageFormat format, const XRenderKernelOps* ops)
{
    int slot = (int)format;
    if (slot <= 0 || slot >= XRENDERKERNEL_FORMAT_SLOTS) return;
    g_kernelSlots[slot] = ops;
}

const XRenderKernelOps* XRenderKernel_forFormat(XImageFormat format)
{
    int slot = (int)format;
    xrenderkernel_registerBuiltins();
    if (slot <= 0 || slot >= XRENDERKERNEL_FORMAT_SLOTS) return NULL;
    return g_kernelSlots[slot];
}

#endif /* XPAINTER_ON */
