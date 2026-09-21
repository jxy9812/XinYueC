/******************************************************************************
 * @file       XRenderKernel_32bit.c
 * @brief      32 位目标格式渲染内核（RGB32/RGBX8888/RGBA8888/ARGB32）。
 * @details    对标 Skia blitter 的逐格式 blit 组织与 LVGL
 *             lv_draw_sw_blend_to_* 的"目标格式域收尾"思路，沿
 *             XRenderKernel_rgb565.c 确立的内核表消费模式扩展到四个
 *             32 位格式：输入统一为 0xAARRGGBB 规范色，内核内部完成
 *             source-over 合成与目标布局存取，外部零格式分支。
 *             字节布局（依据 XImage.c 的 XImage_readPixelValue/
 *             XImage_writePixelValue 各格式分支的存储口径，小端逐字节；
 *             全部按字节组装读写、不做整型 32 位写，避免端序假设）：
 *             - RGB32    ：4B/像素，[0]=B [1]=G [2]=R [3]=x。x 忽略，
 *               读出恒 0xff000000|R<<16|G<<8|B（XImage_readPixelValue
 *               的 RGB32 分支同口径）；写入 x 恒 0xff，对齐
 *               XImage_writePixelValue 的 RGB32 分支
 *               （XImage_store32(…, color | 0xff000000u)）的存储结果；
 *             - RGBX8888 ：4B/像素，[0]=R [1]=G [2]=B [3]=x。x 写
 *               0xff（XImage_writePixelValue 的 RGBX8888 分支逐字节
 *               同款）；
 *             - RGBA8888 ：4B/像素，[0]=R [1]=G [2]=B [3]=A，非预乘
 *               直存（XImage_writePixelValue 的 RGBA8888 分支同款）；
 *             - ARGB32   ：4B/像素，小端 uint32 = 0xAARRGGBB，即字节
 *               [0]=B [1]=G [2]=R [3]=A，非预乘（与预乘规范色同布局，
 *               alpha 语义不同；XImage 的 RGB32/ARGB32 经
 *               XImage_load32/XImage_store32 的 memcpy 读写在小端下
 *               即本字节布局，大端平台需连 XImage 的 memcpy 口径一并
 *               复核，超出本文件契约）。
 *             合成口径（与 XRenderKernel_rgb565.c 文件头同一约定，逐条
 *             对齐 XPainter.c；引用一律按函数名锚定、不写行号）：
 *             - 乘法取整用 x32_mul255 的 (a*b+127)/255 精确四舍五入，
 *               与 XPainter.c 的 painterMul255 逐字节同式，不用
 *               (x*255+127)>>8 变体（±1 口径差，两套近似不可混用）；
 *             - alpha 快捷路径对应 painterComposeColor 的 SourceOver
 *               快捷分支：sa==0 保目标、da==0 得源、sa==255 直写源；
 *             - 不透明目标（RGB32/RGBX8888）：预乘域与直存域合一，
 *               out = srcPrem + dst*(255-sa)/255（rgb565 内核同款，
 *               565 位复制展开换成目标字节直读）；
 *             - 半透明目标（RGBA8888/ARGB32，非预乘存储）：目标读出
 *               值先按其自身 alpha 预乘（dstPrem），预乘域合成
 *               out = srcPrem + dstPrem*(255-sa)/255、
 *               outA = sa + da*(255-sa)/255（painterComposeColor 的
 *               SourceOver 通用分支同式）；写回按目标非预乘存储域把
 *               RGB 以 outA 反预乘（x32_unpremultiply，与
 *               painterUnpremultiply/XImage_unpremultiply8 同式），
 *               alpha 直接存合成后的 outA——与内核所替代的逐像素回退
 *               （painterComposeColor → XImage_setPixel）同一存储域，
 *               避免把预乘字节误存进直色格式；
 *             - 字形覆盖率调制沿用 rgb565 内核口径（等价 Qt BYTE_MUL）：
 *               预乘色各通道同乘覆盖率仍为预乘色，调制后走
 *               source-over；coverage==0 跳过；与 painter 文本路径
 *               painterGlyphAlphaBlend 的两步乘法数学等价、仅舍入次序
 *               不同（XPainter.c 内核字形消费点同款注释口径）；
 *             - storePrem 入参为 XPainter 的 putPixel 已合成完的最终
 *               色（painterRaster_putPixel 内 painterComposeColor 的
 *               输出，非预乘规范域），按目标布局逐字节直存，与
 *               XImage_setPixel 兜底的存储结果逐位一致——rgb565 内核
 *               的 storePrem 同一消费语义（"putPixel 已完成合成，此处
 *               仅做压缩与写入"）；
 *             - 32 位格式 4 字节自然对齐，无 565 的 stride 对齐问题，
 *               行基址按调用方传入的 XImage_bytesPerLine 扫描行推进，
 *               行内像素地址 = 基址 + x*4。
 * @note       模块归属光栅层，随 XPAINTER_ON 裁剪；四张内核表经
 *             XRenderKernel_registerRgb32 挂入 XRenderKernel.c 的
 *             xrenderkernel_registerBuiltins（惰性一次性，后注册可被
 *             平台/加速器变体覆盖）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRenderKernel.h"

#if XPAINTER_ON && (XRENDERKERNEL_RGB32_ON || XRENDERKERNEL_RGBX8888_ON || XRENDERKERNEL_RGBA8888_ON || XRENDERKERNEL_ARGB32_ON)

#include <stddef.h> /* size_t */

/* ========== 公共合成基元（四格式共用；口径见文件头） ========== */

/**
 * @brief 8 位分量相乘并按 255 四舍五入。
 * @note  与 XPainter.c 的 painterMul255 逐字节同式，保证内核合成结果
 *        与 painter ARGB32 路径同口径。
 */
static unsigned x32_mul255(unsigned a, unsigned b)
{
    return (a * b + 127u) / 255u;
}

/**
 * @brief 预乘分量按合成 alpha 还原为直色分量（非预乘存储域写回用）。
 * @note  与 XPainter.c 的 painterUnpremultiply、XImage.c 的
 *        XImage_unpremultiply8 同式（含 alpha 0/255 直通与 255 钳制），
 *        保证写回值与逐像素回退路径的存储域一致。
 */
static unsigned x32_unpremultiply(unsigned value, unsigned alpha)
{
    unsigned result;
    if (alpha == 0u || alpha == 255u) return value;
    result = (value * 255u + alpha / 2u) / alpha;
    return result > 255u ? 255u : result;
}

/* ========== RGB32：[0]=B [1]=G [2]=R [3]=x（不透明目标） ========== */

/**
 * @brief 把规范色（低 24 位 RGB）写入 RGB32 目标像素。
 * @note  x 字节恒写 0xff，对齐 XImage_writePixelValue 的 RGB32 分支
 *        （color | 0xff000000u）的存储结果。
 */
static void rgb32_storePixel(uint8_t* p, uint32_t argb)
{
    p[0] = (uint8_t)argb;         /* B */
    p[1] = (uint8_t)(argb >> 8);  /* G */
    p[2] = (uint8_t)(argb >> 16); /* R */
    p[3] = 0xffu;                 /* x：忽略位，写 0xff 与 setPixel 兜底一致 */
}

/**
 * @brief 预乘 source-over 单像素合成后原位写入 RGB32 目标像素。
 * @note  目标不透明：out = src + dst*(255-sa)/255（rgb565_overPixel
 *        同款算式）。sa==0 保目标、sa==255 免混合直写
 *        （painterComposeColor 的 SourceOver 快捷分支同款）。
 */
static void rgb32_overPixel(uint8_t* p, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned outr;
    unsigned outg;
    unsigned outb;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        rgb32_storePixel(p, srcPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        outr = ((srcPrem >> 16) & 0xffu) + x32_mul255(p[2], invA);
        outg = ((srcPrem >> 8) & 0xffu) + x32_mul255(p[1], invA);
        outb = (srcPrem & 0xffu) + x32_mul255(p[0], invA);
    }
    rgb32_storePixel(p, 0xff000000u | (outr << 16) | (outg << 8) | outb);
}

/** @brief 不透明纯色填充一段 RGB32 目标行（字节拆包提到循环外）。 */
static void rgb32_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                 uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    uint8_t b = (uint8_t)argbPrem;
    uint8_t g = (uint8_t)(argbPrem >> 8);
    uint8_t r = (uint8_t)(argbPrem >> 16);
    int i;
    for (i = 0; i < count; ++i)
    {
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = 0xffu;
        dst += 4;
    }
}

/**
 * @brief 半透明纯色 source-over 填充一段 RGB32 目标行。
 * @note  alpha==255 先查（退化为不透明直写免读改写）、alpha==0 直接
 *        返回（painter SourceOver 快捷分支同款）；src 常量把拆包/
 *        invA 提到循环外，逐像素只做三次乘加。
 */
static void rgb32_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        rgb32_fillSpanOpaque(rowBytes, x, count, argbPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sr = (argbPrem >> 16) & 0xffu;
        unsigned sg = (argbPrem >> 8) & 0xffu;
        unsigned sb = argbPrem & 0xffu;
        for (i = 0; i < count; ++i)
        {
            dst[0] = (uint8_t)(sb + x32_mul255(dst[0], invA));
            dst[1] = (uint8_t)(sg + x32_mul255(dst[1], invA));
            dst[2] = (uint8_t)(sr + x32_mul255(dst[2], invA));
            dst[3] = 0xffu;
            dst += 4;
        }
    }
}

/**
 * @brief 不透明源行拷贝到 RGB32 目标行。
 * @note  srcRow 恒为 alpha==255 的 ARGB32/RGB32 规范行（契约保证），
 *        逐像素按目标字节布局重排拷贝，无混合。
 */
static void rgb32_blitSpan(uint8_t* dstRow, int dstX,
                           const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
        rgb32_storePixel(dst + (size_t)i * 4u, src[i]);
}

/**
 * @brief 预乘 source-over 源行混合到 RGB32 目标行。
 * @note  逐像素先查源 alpha：255 直写免读改写、0 跳过（目标不变），
 *        其余走 rgb32_overPixel 合成（rgb565_blendSpan 同款组织）。
 */
static void rgb32_blendSpan(uint8_t* dstRow, int dstX,
                            const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        if ((s >> 24) == 255u)
            rgb32_storePixel(dst + (size_t)i * 4u, s);
        else
            rgb32_overPixel(dst + (size_t)i * 4u, s);
    }
}

/**
 * @brief 字形灰度 mask 混合到 RGB32 目标行。
 * @note  coverage==0 跳过；调制等价 Qt BYTE_MUL（预乘色各通道乘覆盖
 *        率仍为预乘色，x32_mul255 取整与 painterGlyphAlphaBlend 的
 *        (coverage*a+127)/255 同口径）；调制后走 rgb32_overPixel。
 */
static void rgb32_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                const uint8_t* mask, uint32_t colorPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
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
                ((uint32_t)x32_mul255((colorPrem >> 24) & 0xffu,
                                      coverage) << 24) |
                ((uint32_t)x32_mul255((colorPrem >> 16) & 0xffu,
                                      coverage) << 16) |
                ((uint32_t)x32_mul255((colorPrem >> 8) & 0xffu,
                                      coverage) << 8) |
                (uint32_t)x32_mul255(colorPrem & 0xffu, coverage);
        }
        rgb32_overPixel(dst + (size_t)i * 4u, modulated);
    }
}

/**
 * @brief putPixel 终端存储：已合成单像素写入 RGB32 目标行。
 * @note  入参为 painterRaster_putPixel 里 painterComposeColor 的输出
 *        （合成已完成），此处仅按目标布局落盘；无 alpha 位，x 写
 *        0xff。
 */
static void rgb32_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    rgb32_storePixel(rowBytes + (size_t)x * 4u, argbPrem);
}

/* ========== RGBX8888：[0]=R [1]=G [2]=B [3]=x（不透明目标） ========== */
/* 合成算式与 RGB32 族逐位同式，仅字节序不同；x 字节写 0xff 对齐
 * XImage_writePixelValue 的 RGBX8888 分支。 */

/** @brief 把规范色（低 24 位 RGB）写入 RGBX8888 目标像素。 */
static void rgbx8888_storePixel(uint8_t* p, uint32_t argb)
{
    p[0] = (uint8_t)(argb >> 16); /* R */
    p[1] = (uint8_t)(argb >> 8);  /* G */
    p[2] = (uint8_t)argb;         /* B */
    p[3] = 0xffu;                 /* x：忽略位，写 0xff */
}

/** @brief 预乘 source-over 单像素合成后原位写入（rgb32_overPixel 同式）。 */
static void rgbx8888_overPixel(uint8_t* p, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned outr;
    unsigned outg;
    unsigned outb;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        rgbx8888_storePixel(p, srcPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        outr = ((srcPrem >> 16) & 0xffu) + x32_mul255(p[0], invA);
        outg = ((srcPrem >> 8) & 0xffu) + x32_mul255(p[1], invA);
        outb = (srcPrem & 0xffu) + x32_mul255(p[2], invA);
    }
    rgbx8888_storePixel(p, 0xff000000u | (outr << 16) | (outg << 8) | outb);
}

/** @brief 不透明纯色填充一段 RGBX8888 目标行。 */
static void rgbx8888_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                    uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    uint8_t r = (uint8_t)(argbPrem >> 16);
    uint8_t g = (uint8_t)(argbPrem >> 8);
    uint8_t b = (uint8_t)argbPrem;
    int i;
    for (i = 0; i < count; ++i)
    {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 0xffu;
        dst += 4;
    }
}

/** @brief 半透明纯色 source-over 填充一段 RGBX8888 目标行。 */
static void rgbx8888_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                   uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        rgbx8888_fillSpanOpaque(rowBytes, x, count, argbPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sr = (argbPrem >> 16) & 0xffu;
        unsigned sg = (argbPrem >> 8) & 0xffu;
        unsigned sb = argbPrem & 0xffu;
        for (i = 0; i < count; ++i)
        {
            dst[0] = (uint8_t)(sr + x32_mul255(dst[0], invA));
            dst[1] = (uint8_t)(sg + x32_mul255(dst[1], invA));
            dst[2] = (uint8_t)(sb + x32_mul255(dst[2], invA));
            dst[3] = 0xffu;
            dst += 4;
        }
    }
}

/** @brief 不透明源行拷贝到 RGBX8888 目标行（契约：srcRow alpha 恒 255）。 */
static void rgbx8888_blitSpan(uint8_t* dstRow, int dstX,
                              const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
        rgbx8888_storePixel(dst + (size_t)i * 4u, src[i]);
}

/** @brief 预乘 source-over 源行混合到 RGBX8888 目标行。 */
static void rgbx8888_blendSpan(uint8_t* dstRow, int dstX,
                               const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        if ((s >> 24) == 255u)
            rgbx8888_storePixel(dst + (size_t)i * 4u, s);
        else
            rgbx8888_overPixel(dst + (size_t)i * 4u, s);
    }
}

/** @brief 字形灰度 mask 混合到 RGBX8888 目标行（rgb32_glyphMaskSpan 同式）。 */
static void rgbx8888_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                   const uint8_t* mask, uint32_t colorPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
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
                ((uint32_t)x32_mul255((colorPrem >> 24) & 0xffu,
                                      coverage) << 24) |
                ((uint32_t)x32_mul255((colorPrem >> 16) & 0xffu,
                                      coverage) << 16) |
                ((uint32_t)x32_mul255((colorPrem >> 8) & 0xffu,
                                      coverage) << 8) |
                (uint32_t)x32_mul255(colorPrem & 0xffu, coverage);
        }
        rgbx8888_overPixel(dst + (size_t)i * 4u, modulated);
    }
}

/** @brief putPixel 终端存储：已合成单像素写入 RGBX8888 目标行。 */
static void rgbx8888_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    rgbx8888_storePixel(rowBytes + (size_t)x * 4u, argbPrem);
}

/* ========== RGBA8888：[0]=R [1]=G [2]=B [3]=A（非预乘直存目标） ========== */

/**
 * @brief 把直色域规范色（0xAARRGGBB）写入 RGBA8888 目标像素。
 * @note  非预乘格式直存：RGB 为直色分量、A 为 alpha 本身
 *        （XImage_writePixelValue 的 RGBA8888 分支逐字节同款）。
 */
static void rgba8888_storePixel(uint8_t* p, uint32_t argb)
{
    p[0] = (uint8_t)(argb >> 16); /* R */
    p[1] = (uint8_t)(argb >> 8);  /* G */
    p[2] = (uint8_t)argb;         /* B */
    p[3] = (uint8_t)(argb >> 24); /* A（非预乘直存） */
}

/**
 * @brief 预乘 source-over 单像素合成后原位写入 RGBA8888 目标像素。
 * @note  三段快捷路径（painterComposeColor 的 SourceOver 分支同款）：
 *        sa==0 保目标；sa==255 全覆盖直写源（预乘==直色，alpha 落
 *        0xff）；da==0 得源（合成结果 outA==sa，预乘域结果即
 *        srcPrem，按存储域把源反预乘回直色落盘）。通用分支：目标
 *        直色先按自身 alpha 预乘（dstPrem），预乘域合成
 *        out = srcPrem + dstPrem*(255-sa)/255、
 *        outA = sa + da*(255-sa)/255，写回按非预乘存储域把 RGB 以
 *        outA 反预乘、alpha 直接存 outA（文件头"半透明目标"条）。
 */
static void rgba8888_overPixel(uint8_t* p, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned da;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        p[0] = (uint8_t)(srcPrem >> 16);
        p[1] = (uint8_t)(srcPrem >> 8);
        p[2] = (uint8_t)srcPrem;
        p[3] = 0xffu;
        return;
    }
    da = p[3];
    if (da == 0u)
    {
        p[0] = (uint8_t)x32_unpremultiply((srcPrem >> 16) & 0xffu, sa);
        p[1] = (uint8_t)x32_unpremultiply((srcPrem >> 8) & 0xffu, sa);
        p[2] = (uint8_t)x32_unpremultiply(srcPrem & 0xffu, sa);
        p[3] = (uint8_t)sa;
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned outR =
            ((srcPrem >> 16) & 0xffu) + x32_mul255(x32_mul255(p[0], da), invA);
        unsigned outG =
            ((srcPrem >> 8) & 0xffu) + x32_mul255(x32_mul255(p[1], da), invA);
        unsigned outB =
            (srcPrem & 0xffu) + x32_mul255(x32_mul255(p[2], da), invA);
        unsigned outA = sa + x32_mul255(da, invA);
        p[0] = (uint8_t)x32_unpremultiply(outR, outA);
        p[1] = (uint8_t)x32_unpremultiply(outG, outA);
        p[2] = (uint8_t)x32_unpremultiply(outB, outA);
        p[3] = (uint8_t)outA;
    }
}

/** @brief 不透明纯色填充一段 RGBA8888 目标行（alpha 字节原样落盘）。 */
static void rgba8888_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                    uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    uint8_t r = (uint8_t)(argbPrem >> 16);
    uint8_t g = (uint8_t)(argbPrem >> 8);
    uint8_t b = (uint8_t)argbPrem;
    uint8_t a = (uint8_t)(argbPrem >> 24);
    int i;
    for (i = 0; i < count; ++i)
    {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
        dst += 4;
    }
}

/**
 * @brief 半透明纯色 source-over 填充一段 RGBA8888 目标行。
 * @note  sa 快捷路径同 rgb32_fillSpanBlend；目标 alpha 逐像素可变，
 *        不能像 565/RGB32 那样把整条目标侧提出循环，仅提升源常量；
 *        da==0 时合成结果即预乘源，按存储域反预乘落盘。
 */
static void rgba8888_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                   uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        rgba8888_fillSpanOpaque(rowBytes, x, count, argbPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sr = (argbPrem >> 16) & 0xffu;
        unsigned sg = (argbPrem >> 8) & 0xffu;
        unsigned sb = argbPrem & 0xffu;
        for (i = 0; i < count; ++i)
        {
            unsigned da = dst[3];
            if (da == 0u)
            {
                dst[0] = (uint8_t)x32_unpremultiply(sr, sa);
                dst[1] = (uint8_t)x32_unpremultiply(sg, sa);
                dst[2] = (uint8_t)x32_unpremultiply(sb, sa);
                dst[3] = (uint8_t)sa;
            }
            else
            {
                unsigned outR =
                    sr + x32_mul255(x32_mul255(dst[0], da), invA);
                unsigned outG =
                    sg + x32_mul255(x32_mul255(dst[1], da), invA);
                unsigned outB =
                    sb + x32_mul255(x32_mul255(dst[2], da), invA);
                unsigned outA = sa + x32_mul255(da, invA);
                dst[0] = (uint8_t)x32_unpremultiply(outR, outA);
                dst[1] = (uint8_t)x32_unpremultiply(outG, outA);
                dst[2] = (uint8_t)x32_unpremultiply(outB, outA);
                dst[3] = (uint8_t)outA;
            }
            dst += 4;
        }
    }
}

/**
 * @brief 不透明源行拷贝到 RGBA8888 目标行。
 * @note  契约保证 srcRow alpha 恒 255，alpha 字节落 0xff（不透明
 *        拷贝语义，RGB32 源的忽略位不渗入目标 alpha）。
 */
static void rgba8888_blitSpan(uint8_t* dstRow, int dstX,
                              const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        dst[0] = (uint8_t)(s >> 16);
        dst[1] = (uint8_t)(s >> 8);
        dst[2] = (uint8_t)s;
        dst[3] = 0xffu;
        dst += 4;
    }
}

/** @brief 预乘 source-over 源行混合到 RGBA8888 目标行。 */
static void rgba8888_blendSpan(uint8_t* dstRow, int dstX,
                               const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        if ((s >> 24) == 255u)
            rgba8888_storePixel(dst + (size_t)i * 4u, s);
        else
            rgba8888_overPixel(dst + (size_t)i * 4u, s);
    }
}

/** @brief 字形灰度 mask 混合到 RGBA8888 目标行。 */
static void rgba8888_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                   const uint8_t* mask, uint32_t colorPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
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
                ((uint32_t)x32_mul255((colorPrem >> 24) & 0xffu,
                                      coverage) << 24) |
                ((uint32_t)x32_mul255((colorPrem >> 16) & 0xffu,
                                      coverage) << 16) |
                ((uint32_t)x32_mul255((colorPrem >> 8) & 0xffu,
                                      coverage) << 8) |
                (uint32_t)x32_mul255(colorPrem & 0xffu, coverage);
        }
        rgba8888_overPixel(dst + (size_t)i * 4u, modulated);
    }
}

/** @brief putPixel 终端存储：已合成单像素（直色域）写入 RGBA8888 行。 */
static void rgba8888_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    rgba8888_storePixel(rowBytes + (size_t)x * 4u, argbPrem);
}

/* ========== ARGB32：字节 [0]=B [1]=G [2]=R [3]=A（非预乘直存） ========== */
/* 小端 uint32 视角即 0xAARRGGBB 直色；合成算式与 RGBA8888 族逐位同式，
 * 仅字节序不同。XImage 的 ARGB32 经 XImage_load32/XImage_store32
 * （memcpy）读写在小端下与本字节布局一致。 */

/** @brief 把直色域规范色（0xAARRGGBB）写入 ARGB32 目标像素。 */
static void argb32_storePixel(uint8_t* p, uint32_t argb)
{
    p[0] = (uint8_t)argb;         /* B */
    p[1] = (uint8_t)(argb >> 8);  /* G */
    p[2] = (uint8_t)(argb >> 16); /* R */
    p[3] = (uint8_t)(argb >> 24); /* A（非预乘直存） */
}

/** @brief 预乘 source-over 单像素合成后原位写入（rgba8888_overPixel 同式）。 */
static void argb32_overPixel(uint8_t* p, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned da;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        p[0] = (uint8_t)srcPrem;
        p[1] = (uint8_t)(srcPrem >> 8);
        p[2] = (uint8_t)(srcPrem >> 16);
        p[3] = 0xffu;
        return;
    }
    da = p[3];
    if (da == 0u)
    {
        p[0] = (uint8_t)x32_unpremultiply(srcPrem & 0xffu, sa);
        p[1] = (uint8_t)x32_unpremultiply((srcPrem >> 8) & 0xffu, sa);
        p[2] = (uint8_t)x32_unpremultiply((srcPrem >> 16) & 0xffu, sa);
        p[3] = (uint8_t)sa;
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned outR =
            ((srcPrem >> 16) & 0xffu) + x32_mul255(x32_mul255(p[2], da), invA);
        unsigned outG =
            ((srcPrem >> 8) & 0xffu) + x32_mul255(x32_mul255(p[1], da), invA);
        unsigned outB =
            (srcPrem & 0xffu) + x32_mul255(x32_mul255(p[0], da), invA);
        unsigned outA = sa + x32_mul255(da, invA);
        p[0] = (uint8_t)x32_unpremultiply(outB, outA);
        p[1] = (uint8_t)x32_unpremultiply(outG, outA);
        p[2] = (uint8_t)x32_unpremultiply(outR, outA);
        p[3] = (uint8_t)outA;
    }
}

/** @brief 不透明纯色填充一段 ARGB32 目标行（alpha 字节原样落盘）。 */
static void argb32_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                  uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    uint8_t b = (uint8_t)argbPrem;
    uint8_t g = (uint8_t)(argbPrem >> 8);
    uint8_t r = (uint8_t)(argbPrem >> 16);
    uint8_t a = (uint8_t)(argbPrem >> 24);
    int i;
    for (i = 0; i < count; ++i)
    {
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = a;
        dst += 4;
    }
}

/** @brief 半透明纯色 source-over 填充一段 ARGB32 目标行。 */
static void argb32_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                 uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        argb32_fillSpanOpaque(rowBytes, x, count, argbPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sr = (argbPrem >> 16) & 0xffu;
        unsigned sg = (argbPrem >> 8) & 0xffu;
        unsigned sb = argbPrem & 0xffu;
        for (i = 0; i < count; ++i)
        {
            unsigned da = dst[3];
            if (da == 0u)
            {
                dst[0] = (uint8_t)x32_unpremultiply(sb, sa);
                dst[1] = (uint8_t)x32_unpremultiply(sg, sa);
                dst[2] = (uint8_t)x32_unpremultiply(sr, sa);
                dst[3] = (uint8_t)sa;
            }
            else
            {
                unsigned outR =
                    sr + x32_mul255(x32_mul255(dst[2], da), invA);
                unsigned outG =
                    sg + x32_mul255(x32_mul255(dst[1], da), invA);
                unsigned outB =
                    sb + x32_mul255(x32_mul255(dst[0], da), invA);
                unsigned outA = sa + x32_mul255(da, invA);
                dst[0] = (uint8_t)x32_unpremultiply(outB, outA);
                dst[1] = (uint8_t)x32_unpremultiply(outG, outA);
                dst[2] = (uint8_t)x32_unpremultiply(outR, outA);
                dst[3] = (uint8_t)outA;
            }
            dst += 4;
        }
    }
}

/**
 * @brief 不透明源行拷贝到 ARGB32 目标行。
 * @note  契约保证 srcRow alpha 恒 255，alpha 字节落 0xff（不透明
 *        拷贝语义，RGB32 源的忽略位不渗入目标 alpha）。
 */
static void argb32_blitSpan(uint8_t* dstRow, int dstX,
                            const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        dst[0] = (uint8_t)s;
        dst[1] = (uint8_t)(s >> 8);
        dst[2] = (uint8_t)(s >> 16);
        dst[3] = 0xffu;
        dst += 4;
    }
}

/** @brief 预乘 source-over 源行混合到 ARGB32 目标行。 */
static void argb32_blendSpan(uint8_t* dstRow, int dstX,
                             const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 4u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        if ((s >> 24) == 255u)
            argb32_storePixel(dst + (size_t)i * 4u, s);
        else
            argb32_overPixel(dst + (size_t)i * 4u, s);
    }
}

/** @brief 字形灰度 mask 混合到 ARGB32 目标行。 */
static void argb32_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                 const uint8_t* mask, uint32_t colorPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 4u;
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
                ((uint32_t)x32_mul255((colorPrem >> 24) & 0xffu,
                                      coverage) << 24) |
                ((uint32_t)x32_mul255((colorPrem >> 16) & 0xffu,
                                      coverage) << 16) |
                ((uint32_t)x32_mul255((colorPrem >> 8) & 0xffu,
                                      coverage) << 8) |
                (uint32_t)x32_mul255(colorPrem & 0xffu, coverage);
        }
        argb32_overPixel(dst + (size_t)i * 4u, modulated);
    }
}

/** @brief putPixel 终端存储：已合成单像素（直色域）写入 ARGB32 行。 */
static void argb32_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    argb32_storePixel(rowBytes + (size_t)x * 4u, argbPrem);
}

/* ========== 内核表与注册入口 ========== */

/** @brief RGB32 内核表（六成员全满，顺序即契约声明序）。 */
static const XRenderKernelOps g_rgb32Kernel =
{
    rgb32_fillSpanOpaque,
    rgb32_fillSpanBlend,
    rgb32_blitSpan,
    rgb32_blendSpan,
    rgb32_glyphMaskSpan,
    rgb32_storePrem
};

/** @brief RGBX8888 内核表（六成员全满，顺序即契约声明序）。 */
static const XRenderKernelOps g_rgbx8888Kernel =
{
    rgbx8888_fillSpanOpaque,
    rgbx8888_fillSpanBlend,
    rgbx8888_blitSpan,
    rgbx8888_blendSpan,
    rgbx8888_glyphMaskSpan,
    rgbx8888_storePrem
};

/** @brief RGBA8888 内核表（六成员全满，顺序即契约声明序）。 */
static const XRenderKernelOps g_rgba8888Kernel =
{
    rgba8888_fillSpanOpaque,
    rgba8888_fillSpanBlend,
    rgba8888_blitSpan,
    rgba8888_blendSpan,
    rgba8888_glyphMaskSpan,
    rgba8888_storePrem
};

/** @brief ARGB32 内核表（六成员全满，顺序即契约声明序）。 */
static const XRenderKernelOps g_argb32Kernel =
{
    argb32_fillSpanOpaque,
    argb32_fillSpanBlend,
    argb32_blitSpan,
    argb32_blendSpan,
    argb32_glyphMaskSpan,
    argb32_storePrem
};

/**
 * @brief 把四个 32 位格式内核表注册到对应 XImageFormat 槽位。
 * @note  由 XRenderKernel.c 的惰性注册
 *        （xrenderkernel_registerBuiltins）调用；后注册覆盖先注册，
 *        平台/加速器变体（NEON/DMA2D）可随后覆盖。ARGB32_Premultiplied
 *        等预乘目标不在此注册（契约：预乘规范路径不经内核表）。
 */
void XRenderKernel_registerRgb32(void)
{
#if XRENDERKERNEL_RGB32_ON
    XRenderKernel_register(XImageFormat_RGB32, &g_rgb32Kernel);
#endif
#if XRENDERKERNEL_RGBX8888_ON
    XRenderKernel_register(XImageFormat_RGBX8888, &g_rgbx8888Kernel);
#endif
#if XRENDERKERNEL_RGBA8888_ON
    XRenderKernel_register(XImageFormat_RGBA8888, &g_rgba8888Kernel);
#endif
#if XRENDERKERNEL_ARGB32_ON
    XRenderKernel_register(XImageFormat_ARGB32, &g_argb32Kernel);
#endif
}

#endif /* XPAINTER_ON && (RGB32/RGBX8888/RGBA8888/ARGB32 族开关) */
