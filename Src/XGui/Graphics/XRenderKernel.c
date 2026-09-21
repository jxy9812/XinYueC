/******************************************************************************
 * @file       XRenderKernel.c
 * @brief      目标格式渲染内核表注册中心（对标 Skia blitter 的格式分派）。
 * @details    槽位数组按 XImageFormat 枚举值索引；内置内核在 forFormat
 *             首次调用时惰性注册，平台/加速器变体可随后经
 *             XRenderKernel_register 覆盖注入（NEON/Helium/DMA2D 挂点）。
 *             逐格式编译开关（XRENDERKERNEL_*_ON，定义见 XGuiConfig.h）：
 *             关闭的格式不编译其内核文件（ROM 归零），槽位保持 NULL，
 *             painter 自动回退逐像素路径（零回归语义见 XRenderKernel.h）。
 *             GUI 单线程访问，无锁（与 XPainter 既有静态缓存同纪律）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRenderKernel.h"

#if XPAINTER_ON

#include <stdbool.h>
#include <stddef.h> /* NULL */

/* ---- 内置内核注册入口（各格式内核文件提供；文件名=格式族名） ---- */
#if XRENDERKERNEL_RGB565_ON
extern void XRenderKernel_registerRgb565(void);   /* XRenderKernel_rgb565.c */
#endif
#if XRENDERKERNEL_RGB555_ON
extern void XRenderKernel_registerRgb555(void);   /* XRenderKernel_rgb555.c */
#endif
#if XRENDERKERNEL_RGB888_ON || XRENDERKERNEL_BGR888_ON
extern void XRenderKernel_registerRgb888(void);   /* XRenderKernel_rgb888.c */
#endif
#if XRENDERKERNEL_RGB32_ON || XRENDERKERNEL_RGBX8888_ON || \
    XRENDERKERNEL_RGBA8888_ON || XRENDERKERNEL_ARGB32_ON
extern void XRenderKernel_registerRgb32(void);    /* XRenderKernel_rgb32.c */
#endif
#if XRENDERKERNEL_GRAYSCALE8_ON
extern void XRenderKernel_registerGray8(void);    /* XRenderKernel_gray8.c */
#endif

/** @brief 格式槽位表（索引即 XImageFormat 枚举值；NULL=未注册）。 */
static const XRenderKernelOps*
    g_kernelSlots[XRENDERKERNEL_FORMAT_SLOTS];

/** @brief 内置内核是否已注册（惰性一次性）。 */
static bool g_kernelBuiltinRegistered;

/** @brief 惰性注册内置内核（幂等；各注册入口内部按开关门控到槽位级）。 */
static void xrenderkernel_registerBuiltins(void)
{
    if (g_kernelBuiltinRegistered) return;
    g_kernelBuiltinRegistered = true;
#if XRENDERKERNEL_RGB565_ON
    XRenderKernel_registerRgb565();
#endif
#if XRENDERKERNEL_RGB888_ON || XRENDERKERNEL_BGR888_ON
    XRenderKernel_registerRgb888();
#endif
#if XRENDERKERNEL_RGB32_ON || XRENDERKERNEL_RGBX8888_ON || \
    XRENDERKERNEL_RGBA8888_ON || XRENDERKERNEL_ARGB32_ON
    XRenderKernel_registerRgb32();
#endif
#if XRENDERKERNEL_RGB555_ON
    XRenderKernel_registerRgb555();
#endif
#if XRENDERKERNEL_GRAYSCALE8_ON
    XRenderKernel_registerGray8();
#endif
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
