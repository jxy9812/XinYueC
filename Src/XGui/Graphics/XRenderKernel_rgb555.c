/******************************************************************************
 * @file       XRenderKernel_rgb555.c
 * @brief      XImageFormat_RGB555（15 位 RGB）目标格式渲染内核。
 * @details    延续 XRenderKernel_rgb565.c 的组织方式（Skia blitter 的
 *             逐格式内核表 + LVGL blend_to_* 的"目标格式域收尾"）：
 *             输入统一为 0xAARRGGBB 预乘 ARGB32 规范色（XPainter 的
 *             规范色），内核内部完成 source-over 合成与 555 压缩，
 *             外部零格式分支。
 *             打包口径经 XImage.c 核实：XImage_writePixelValue 的
 *             RGB555 分支打包为 (compress5(r)<<10)|(compress5(g)<<5)|
 *             compress5(b)，复用共享的四舍五入压缩 XImage_compress5
 *             （(v*31+127)/255），并非"无 compress5、直接 >>3 截断"；
 *             按本文件"内核打包口径与 XImage_writePixelValue 一致"的
 *             契约，打包采用同一四舍五入式。注意这与 rgb565 内核的
 *             截断式契约（对标 Qt qConvertRgb32To16）有意不同——两
 *             文件各自独立成立，同一颜色经两层可差 1 LSB，见
 *             XImage.c 压缩基元处的口径互引注释与 rgb565 内核文件头。
 *             合成口径与 rgb565 内核一致：painterMul255 同式精确四舍
 *             五入 (a*b+127)/255；sa==0 保目标、sa==255 直写两条快捷
 *             路径对应 painterComposeColor 的 SourceOver 快捷分支；
 *             字形覆盖率调制等价 Qt BYTE_MUL。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRenderKernel.h"

#if XPAINTER_ON && XRENDERKERNEL_RGB555_ON

/* ========== 555 编解码基元 ========== */


/** @brief 8 位分量相乘并按 255 四舍五入（painterMul255 同式）。 */
static unsigned rgb555_mul255(unsigned a, unsigned b)
{
    return (a * b + 127u) / 255u;
}

/**
 * @brief 5 位压缩：四舍五入式 (v*31+127)/255（XImage_compress5 同式）。
 * @note  经核实 XImage_writePixelValue 的 RGB555 分支正是经它打包；
 *        与 rgb565 内核的截断式契约有意不同（见文件头）。
 */
static unsigned rgb555_compress5(unsigned value)
{
    return (value * 31u + 127u) / 255u;
}

/**
 * @brief 把预乘 ARGB32 规范色压缩为 RGB555（口径同 XImage_writePixelValue）。
 * @note  仅绿通道 5 位（writePixelValue 的 RGB555 分支同构）。预乘色
 *        的 RGB 分量在 555（无 alpha 位）目标上无需反预乘，直接压缩。
 */
static uint16_t rgb555_pack(uint32_t argbPrem)
{
    unsigned r = (argbPrem >> 16) & 0xffu;
    unsigned g = (argbPrem >> 8) & 0xffu;
    unsigned b = argbPrem & 0xffu;
    return (uint16_t)((rgb555_compress5(r) << 10) |
                      (rgb555_compress5(g) << 5) |
                      rgb555_compress5(b));
}

/** @brief 5 位展开到 8 位：位复制 (v<<3)|(v>>2)（31 → 255 无损）。 */
static unsigned rgb555_expand5(unsigned value)
{
    return (value << 3) | (value >> 2);
}

/**
 * @brief 预乘 source-over 单像素合成后写入 555 目标像素。
 * @param dstPixel 目标像素（555）。
 * @param srcPrem 源（预乘 ARGB32）。
 *        555：三通道均按 5 位位复制展开到 8 位合成域，压缩回 555
 *        （LVGL blend_to_rgb565 的"目标格式域收尾"，但合成在
 *        ARGB32 精度域完成以保证与 painter 口径一致）。源 alpha 为 0
 *        或 255 时走快捷路径（painterComposeColor 的 SourceOver 快捷
 *        分支同款），避免读改写与无效乘法。
 */
static uint16_t rgb555_overPixel(uint16_t dstPixel, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned invA = 255u - sa;
    unsigned dr, dg, db;
    if (sa == 255u) return rgb555_pack(srcPrem); /* 不透明源：免混合直写 */
    if (sa == 0u) return dstPixel;               /* 全透明源：保目标 */
    dr = rgb555_expand5((dstPixel >> 10) & 0x1fu);
    dg = rgb555_expand5((dstPixel >> 5) & 0x1fu);
    db = rgb555_expand5(dstPixel & 0x1fu);
    {
        unsigned or = ((srcPrem >> 16) & 0xffu) + rgb555_mul255(dr, invA);
        unsigned og = ((srcPrem >> 8) & 0xffu) + rgb555_mul255(dg, invA);
        unsigned ob = (srcPrem & 0xffu) + rgb555_mul255(db, invA);
        return (uint16_t)((rgb555_compress5(or) << 10) |
                          (rgb555_compress5(og) << 5) |
                          rgb555_compress5(ob));
    }
}

/* ========== RGB555 内核表六个成员 ========== */

/**
 * @brief 不透明纯色填充一段 555 目标行。
 * @note  对标 Skia 的 565 blitH 宽字节填充与 LVGL fill 操作：打包一次，
 *        循环只做 16 位写。
 */
static void rgb555_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                  uint32_t argbPrem)
{
    uint16_t* dst = (uint16_t*)(void*)rowBytes + x;
    uint16_t packed = rgb555_pack(argbPrem);
    int i;
    for (i = 0; i < count; ++i) dst[i] = packed;
}

/**
 * @brief 半透明纯色 source-over 填充一段 555 目标行。
 * @note  alpha==255 退化为不透明直写免读改写；alpha==0 直接返回
 *        （painter 的 SourceOver 快捷分支同款）。src 常量把拆包/invA
 *        提到循环外，逐像素只做 555 展开、三次乘加与压缩。
 */
static void rgb555_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                 uint32_t argbPrem)
{
    uint16_t* dst = (uint16_t*)(void*)rowBytes + x;
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        uint16_t packed = rgb555_pack(argbPrem);
        for (i = 0; i < count; ++i) dst[i] = packed;
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sr = (argbPrem >> 16) & 0xffu;
        unsigned sg = (argbPrem >> 8) & 0xffu;
        unsigned sb = argbPrem & 0xffu;
        for (i = 0; i < count; ++i)
        {
            uint16_t d = dst[i];
            unsigned dr = rgb555_expand5((d >> 10) & 0x1fu);
            unsigned dg = rgb555_expand5((d >> 5) & 0x1fu);
            unsigned db = rgb555_expand5(d & 0x1fu);
            unsigned or = sr + rgb555_mul255(dr, invA);
            unsigned og = sg + rgb555_mul255(dg, invA);
            unsigned ob = sb + rgb555_mul255(db, invA);
            dst[i] = (uint16_t)((rgb555_compress5(or) << 10) |
                                (rgb555_compress5(og) << 5) |
                                rgb555_compress5(ob));
        }
    }
}

/**
 * @brief 不透明源行拷贝到 555 目标行。
 * @note  对标 Skia 的 S32_D16 blitRow：srcRow 恒为 alpha==255 的
 *        ARGB32/RGB32 规范行（契约保证），逐像素打包拷贝，无混合。
 */
static void rgb555_blitSpan(uint8_t* dstRow, int dstX,
                            const uint32_t* srcRow, int srcX, int count)
{
    uint16_t* dst = (uint16_t*)(void*)dstRow + dstX;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i) dst[i] = rgb555_pack(src[i]);
}

/**
 * @brief 预乘 source-over 源行混合到 555 目标行。
 * @note  逐像素先查源 alpha：255 直写免读改写（对标 Skia
 *        blitRow_s32a_opaque 的不透明快捷分支）、0 跳过
 *        （painterComposeColor 的 SourceOver 快捷分支同款），其余走
 *        rgb555_overPixel 合成。
 */
static void rgb555_blendSpan(uint8_t* dstRow, int dstX,
                             const uint32_t* srcRow, int srcX, int count)
{
    uint16_t* dst = (uint16_t*)(void*)dstRow + dstX;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        if ((s >> 24) == 255u)
        {
            dst[i] = rgb555_pack(s);
        }
        else
        {
            dst[i] = rgb555_overPixel(dst[i], s);
        }
    }
}

/**
 * @brief 字形灰度 mask 混合到 555 目标行。
 * @note  覆盖率调制等价 Qt BYTE_MUL：预乘色各通道乘覆盖率仍为预乘色，
 *        再 source-over 入目标。coverage==0 跳过；调制用 rgb555_mul255，
 *        与 painter 文本回退路径 painterGlyphAlphaBlend 的覆盖率缩放
 *        （(coverage*a+127)/255）同口径；调制后源不透明时直写，
 *        半透明走 rgb555_overPixel。
 */
static void rgb555_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                 const uint8_t* mask, uint32_t colorPrem)
{
    uint16_t* dst = (uint16_t*)(void*)rowBytes + x;
    int i;
    for (i = 0; i < count; ++i)
    {
        unsigned coverage = mask[i];
        uint32_t modulated;
        if (coverage == 0u) continue;
        if (coverage >= 255u)
        {
            modulated = colorPrem;
        }
        else
        {
            modulated =
                ((uint32_t)rgb555_mul255((colorPrem >> 24) & 0xffu,
                                         coverage) << 24) |
                ((uint32_t)rgb555_mul255((colorPrem >> 16) & 0xffu,
                                         coverage) << 16) |
                ((uint32_t)rgb555_mul255((colorPrem >> 8) & 0xffu,
                                         coverage) << 8) |
                (uint32_t)rgb555_mul255(colorPrem & 0xffu, coverage);
        }
        dst[i] = rgb555_overPixel(dst[i], modulated);
    }
}

/**
 * @brief putPixel 终端存储：预乘 ARGB32 单像素写入 555 目标行。
 * @note  XPainter putPixel 已完成合成，此处仅做 555 压缩与 16 位写；
 *        无 alpha 位，源 alpha 位与未用高位丢弃。
 */
static void rgb555_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    ((uint16_t*)(void*)rowBytes)[x] = rgb555_pack(argbPrem);
}


/* ========== 注册入口 ========== */

/** @brief RGB555 内核表（六个槽位全满，无 NULL；顺序即契约声明序）。 */
static const XRenderKernelOps g_rgb555Kernel =
{
    rgb555_fillSpanOpaque,
    rgb555_fillSpanBlend,
    rgb555_blitSpan,
    rgb555_blendSpan,
    rgb555_glyphMaskSpan,
    rgb555_storePrem
};

/**
 * @brief 把 RGB555 内核表注册到对应槽位。
 * @note  由 XRenderKernel.c 的惰性注册（xrenderkernel_registerBuiltins）
 *        调用；后注册覆盖先注册，平台/加速器变体可随后覆盖。
 */
void XRenderKernel_registerRgb555(void)
{
    XRenderKernel_register(XImageFormat_RGB555, &g_rgb555Kernel);
}

#endif /* XPAINTER_ON && XRENDERKERNEL_RGB555_ON */
