/******************************************************************************
 * @file       XRenderKernel_24bit.c
 * @brief      XImageFormat_RGB888 / XImageFormat_BGR888（24 位）目标格式
 *             渲染内核表。
 * @details    沿用 XRenderKernel_rgb565.c 确立的"在目标格式域收尾"组织：
 *             所有输入统一为 0xAARRGGBB 预乘 ARGB32 规范色（XPainter 的
 *             规范色），内核内部完成 source-over 合成与 24 位落盘，外部
 *             零格式分支。合成口径与 rgb565 内核逐条同源（先读 XPainter.c
 *             后确定；引用一律按函数名锚定、不写行号）：
 *             - 除法近似采用 XPainter.c 的 painterMul255 的
 *               (a*b+127)/255 精确四舍五入；
 *             - alpha==0 保目标、alpha==255 直写两条快捷路径对应
 *               painterComposeColor 的 SourceOver 分支的两条 if 快捷；
 *             - 字形覆盖率调制等价 Qt BYTE_MUL：预乘色各通道同乘覆盖率
 *               后仍为预乘色，再走 source-over，与 painter 文本回退
 *               路径 painterGlyphAlphaBlend 的覆盖率缩放同口径。
 *             字节布局（依据 XImage.c 的 XImage_writePixelValue 落盘
 *             分支与读取侧 XImage_readPixelValue 的 888 分支，两侧逐
 *             字节对称，均为 x*3 字节寻址）：
 *             - RGB888：3B/像素，[0]=R [1]=G [2]=B；
 *             - BGR888：3B/像素，[0]=B [1]=G [2]=R（BGR 序）。
 *             24 位格式无 alpha 位：合成结果仅 R/G/B 落盘，alpha 丢弃；
 *             目标像素视为完全不透明（da==255），source-over 退化为
 *             out = src + dst*(255-sa)/255。
 *             本文件核心难点是 3 字节像素：无 uint16/uint32 整写捷径，
 *             一律逐字节写、按字节推进（像素基址 rowBytes+(size_t)x*3）；
 *             不投机对齐整写——行起点/像素基址未必 2/4 字节对齐，整写
 *             会错位越界（对标 Qt qdrawhelper 的 24bpp 逐字节 store）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRenderKernel.h"
#include <stddef.h> /* size_t */

#if XPAINTER_ON && (XRENDERKERNEL_RGB888_ON || XRENDERKERNEL_BGR888_ON)

/* ========== 24 位编解码基元 ========== */

/**
 * @brief 8 位分量相乘并按 255 四舍五入。
 * @note  与 XPainter.c 的 painterMul255 逐字节同式，
 *        保证内核合成结果与 painter ARGB32 路径同口径。
 */
static unsigned rgb24_mul255(unsigned a, unsigned b)
{
    return (a * b + 127u) / 255u;
}

/** @brief 把 R/G/B 分量按 RGB888 字节序落盘：[0]=R [1]=G [2]=B。 */
static void rgb888_store3(uint8_t* p, unsigned r, unsigned g, unsigned b)
{
    p[0] = (uint8_t)r;
    p[1] = (uint8_t)g;
    p[2] = (uint8_t)b;
}

/** @brief 把 R/G/B 分量按 BGR888 字节序落盘：[0]=B [1]=G [2]=R。 */
static void bgr888_store3(uint8_t* p, unsigned r, unsigned g, unsigned b)
{
    p[0] = (uint8_t)b;
    p[1] = (uint8_t)g;
    p[2] = (uint8_t)r;
}

/**
 * @brief 预乘 source-over 单像素合成入 RGB888 目标像素（原位）。
 * @param p 目标像素基址（3 字节）。
 * @param srcPrem 源（预乘 ARGB32）。
 * @note  out = src + dst*(255-sa)/255（painter 同款，见文件头）。目标
 *        不透明：从 3 字节展开 R/G/B（alpha 位不存在，da 视为 255），
 *        ARGB32 精度域合成后压回 3 字节（alpha 丢弃）。源 alpha 为 0
 *        或 255 时走快捷路径（painterComposeColor 的 SourceOver 快捷
 *        分支同款），避免读改写与无效乘法。
 */
static void rgb888_overPixel3(uint8_t* p, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned invA;
    if (sa == 0u) return;                       /* 全透明源：目标不变 */
    if (sa == 255u)                             /* 不透明源：免混合直写 */
    {
        rgb888_store3(p, (srcPrem >> 16) & 0xffu,
                      (srcPrem >> 8) & 0xffu, srcPrem & 0xffu);
        return;
    }
    invA = 255u - sa;
    {
        unsigned dr = p[0];
        unsigned dg = p[1];
        unsigned db = p[2];
        rgb888_store3(p,
                      ((srcPrem >> 16) & 0xffu) + rgb24_mul255(dr, invA),
                      ((srcPrem >> 8) & 0xffu) + rgb24_mul255(dg, invA),
                      (srcPrem & 0xffu) + rgb24_mul255(db, invA));
    }
}

/**
 * @brief 预乘 source-over 单像素合成入 BGR888 目标像素（原位）。
 * @note  与 rgb888_overPixel3 同式，仅字节序相反：[0]=B [1]=G [2]=R
 *        （各通道在各自槽位原位更新，展开/收尾按 BGR 序取放）。
 */
static void bgr888_overPixel3(uint8_t* p, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned invA;
    if (sa == 0u) return;                       /* 全透明源：目标不变 */
    if (sa == 255u)                             /* 不透明源：免混合直写 */
    {
        bgr888_store3(p, (srcPrem >> 16) & 0xffu,
                      (srcPrem >> 8) & 0xffu, srcPrem & 0xffu);
        return;
    }
    invA = 255u - sa;
    {
        unsigned db = p[0];
        unsigned dg = p[1];
        unsigned dr = p[2];
        bgr888_store3(p,
                      ((srcPrem >> 16) & 0xffu) + rgb24_mul255(dr, invA),
                      ((srcPrem >> 8) & 0xffu) + rgb24_mul255(dg, invA),
                      (srcPrem & 0xffu) + rgb24_mul255(db, invA));
    }
}

/* ========== RGB888 内核表六个成员 ========== */

/**
 * @brief 不透明纯色填充一段 RGB888 目标行。
 * @note  颜色拆包提到循环外，逐像素仅 3 次字节写；基址推进按字节
 *        （rowBytes+(size_t)x*3），无整写捷径（见文件头）。
 */
static void rgb888_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                  uint32_t argbPrem)
{
    uint8_t* p = rowBytes + (size_t)x * 3u;
    unsigned sr = (argbPrem >> 16) & 0xffu;
    unsigned sg = (argbPrem >> 8) & 0xffu;
    unsigned sb = argbPrem & 0xffu;
    int i;
    for (i = 0; i < count; ++i)
    {
        rgb888_store3(p, sr, sg, sb);
        p += 3;
    }
}

/**
 * @brief 半透明纯色 source-over 填充一段 RGB888 目标行。
 * @note  alpha==255 退化为 rgb888_fillSpanOpaque（免读改写，契约第 3
 *        条）；alpha==0 对应 painter 的 SourceOver 快捷分支
 *        （painterComposeColor）直接返回。src 常量把 invA/拆包提到
 *        循环外，逐像素只做三次乘加与字节写。
 */
static void rgb888_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                 uint32_t argbPrem)
{
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        rgb888_fillSpanOpaque(rowBytes, x, count, argbPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sr = (argbPrem >> 16) & 0xffu;
        unsigned sg = (argbPrem >> 8) & 0xffu;
        unsigned sb = argbPrem & 0xffu;
        uint8_t* p = rowBytes + (size_t)x * 3u;
        for (i = 0; i < count; ++i)
        {
            p[0] = (uint8_t)(sr + rgb24_mul255(p[0], invA));
            p[1] = (uint8_t)(sg + rgb24_mul255(p[1], invA));
            p[2] = (uint8_t)(sb + rgb24_mul255(p[2], invA));
            p += 3;
        }
    }
}

/**
 * @brief 不透明源行拷贝到 RGB888 目标行。
 * @note  srcRow 恒为 alpha==255 的 ARGB32/RGB32 规范行（契约保证），
 *        逐像素拆包拷贝（alpha 位丢弃），无混合。
 */
static void rgb888_blitSpan(uint8_t* dstRow, int dstX,
                            const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 3u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        rgb888_store3(dst, (src[i] >> 16) & 0xffu,
                      (src[i] >> 8) & 0xffu, src[i] & 0xffu);
        dst += 3;
    }
}

/**
 * @brief 预乘 source-over 源行混合到 RGB888 目标行。
 * @note  逐像素先查源 alpha（契约第 3 条）：255 直写免读改写（对标
 *        Skia blitRow_s32a_opaque 的不透明快捷分支）、0 跳过
 *        （painterComposeColor 的 SourceOver 快捷分支同款，由
 *        rgb888_overPixel3 内部快捷处理），其余原位合成。
 */
static void rgb888_blendSpan(uint8_t* dstRow, int dstX,
                             const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 3u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        uint8_t* p = dst + (size_t)i * 3u;
        if ((s >> 24) == 255u)
        {
            rgb888_store3(p, (s >> 16) & 0xffu, (s >> 8) & 0xffu,
                          s & 0xffu);
        }
        else
        {
            rgb888_overPixel3(p, s);
        }
    }
}

/**
 * @brief 字形灰度 mask 混合到 RGB888 目标行。
 * @note  覆盖率调制等价 Qt BYTE_MUL：预乘色各通道乘覆盖率仍为预乘色，
 *        再 source-over 入目标。coverage==0 跳过（契约第 4 条）；
 *        调制用 rgb24_mul255，与 painter 文本回退路径
 *        painterGlyphAlphaBlend 的覆盖率缩放同口径；调制后经
 *        rgb888_overPixel3 落盘（源不透明/全透明快捷分支在其内部）。
 */
static void rgb888_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                 const uint8_t* mask, uint32_t colorPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 3u;
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
                ((uint32_t)rgb24_mul255((colorPrem >> 24) & 0xffu,
                                        coverage) << 24) |
                ((uint32_t)rgb24_mul255((colorPrem >> 16) & 0xffu,
                                        coverage) << 16) |
                ((uint32_t)rgb24_mul255((colorPrem >> 8) & 0xffu,
                                        coverage) << 8) |
                (uint32_t)rgb24_mul255(colorPrem & 0xffu, coverage);
        }
        rgb888_overPixel3(dst + (size_t)i * 3u, modulated);
    }
}

/**
 * @brief putPixel 终端存储：预乘 ARGB32 单像素写入 RGB888 目标行。
 * @note  XPainter putPixel 已完成合成，此处仅做拆包与 3 字节写；
 *        RGB888 无 alpha 位，源 alpha 位丢弃。
 */
static void rgb888_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    rgb888_store3(rowBytes + (size_t)x * 3u, (argbPrem >> 16) & 0xffu,
                  (argbPrem >> 8) & 0xffu, argbPrem & 0xffu);
}

/* ========== BGR888 内核表六个成员 ========== */

/**
 * @brief 不透明纯色填充一段 BGR888 目标行。
 * @note  与 rgb888_fillSpanOpaque 同式，字节序 [0]=B [1]=G [2]=R。
 */
static void bgr888_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                  uint32_t argbPrem)
{
    uint8_t* p = rowBytes + (size_t)x * 3u;
    unsigned sr = (argbPrem >> 16) & 0xffu;
    unsigned sg = (argbPrem >> 8) & 0xffu;
    unsigned sb = argbPrem & 0xffu;
    int i;
    for (i = 0; i < count; ++i)
    {
        bgr888_store3(p, sr, sg, sb);
        p += 3;
    }
}

/**
 * @brief 半透明纯色 source-over 填充一段 BGR888 目标行。
 * @note  快捷路径与循环外提同 rgb888_fillSpanBlend；各通道在各自
 *        BGR 槽位原位更新（[0]=B [1]=G [2]=R）。
 */
static void bgr888_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                 uint32_t argbPrem)
{
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        bgr888_fillSpanOpaque(rowBytes, x, count, argbPrem);
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sr = (argbPrem >> 16) & 0xffu;
        unsigned sg = (argbPrem >> 8) & 0xffu;
        unsigned sb = argbPrem & 0xffu;
        uint8_t* p = rowBytes + (size_t)x * 3u;
        for (i = 0; i < count; ++i)
        {
            p[0] = (uint8_t)(sb + rgb24_mul255(p[0], invA));
            p[1] = (uint8_t)(sg + rgb24_mul255(p[1], invA));
            p[2] = (uint8_t)(sr + rgb24_mul255(p[2], invA));
            p += 3;
        }
    }
}

/**
 * @brief 不透明源行拷贝到 BGR888 目标行。
 * @note  逐像素拆包按 BGR 序落盘（alpha 位丢弃），无混合。
 */
static void bgr888_blitSpan(uint8_t* dstRow, int dstX,
                            const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 3u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        bgr888_store3(dst, (src[i] >> 16) & 0xffu,
                      (src[i] >> 8) & 0xffu, src[i] & 0xffu);
        dst += 3;
    }
}

/**
 * @brief 预乘 source-over 源行混合到 BGR888 目标行。
 * @note  快捷分支结构同 rgb888_blendSpan，合成经 bgr888_overPixel3。
 */
static void bgr888_blendSpan(uint8_t* dstRow, int dstX,
                             const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + (size_t)dstX * 3u;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        uint8_t* p = dst + (size_t)i * 3u;
        if ((s >> 24) == 255u)
        {
            bgr888_store3(p, (s >> 16) & 0xffu, (s >> 8) & 0xffu,
                          s & 0xffu);
        }
        else
        {
            bgr888_overPixel3(p, s);
        }
    }
}

/**
 * @brief 字形灰度 mask 混合到 BGR888 目标行。
 * @note  调制口径同 rgb888_glyphMaskSpan（painterGlyphAlphaBlend 同
 *        源），合成经 bgr888_overPixel3。
 */
static void bgr888_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                 const uint8_t* mask, uint32_t colorPrem)
{
    uint8_t* dst = rowBytes + (size_t)x * 3u;
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
                ((uint32_t)rgb24_mul255((colorPrem >> 24) & 0xffu,
                                        coverage) << 24) |
                ((uint32_t)rgb24_mul255((colorPrem >> 16) & 0xffu,
                                        coverage) << 16) |
                ((uint32_t)rgb24_mul255((colorPrem >> 8) & 0xffu,
                                        coverage) << 8) |
                (uint32_t)rgb24_mul255(colorPrem & 0xffu, coverage);
        }
        bgr888_overPixel3(dst + (size_t)i * 3u, modulated);
    }
}

/**
 * @brief putPixel 终端存储：预乘 ARGB32 单像素写入 BGR888 目标行。
 * @note  仅做拆包与 3 字节写；无 alpha 位，源 alpha 位丢弃。
 */
static void bgr888_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    bgr888_store3(rowBytes + (size_t)x * 3u, (argbPrem >> 16) & 0xffu,
                  (argbPrem >> 8) & 0xffu, argbPrem & 0xffu);
}

/* ========== 注册入口 ========== */

/** @brief RGB888 内核表（六槽位全满，无 NULL；顺序即契约声明序）。 */
static const XRenderKernelOps g_rgb888Kernel =
{
    rgb888_fillSpanOpaque,
    rgb888_fillSpanBlend,
    rgb888_blitSpan,
    rgb888_blendSpan,
    rgb888_glyphMaskSpan,
    rgb888_storePrem
};

/** @brief BGR888 内核表（六槽位全满，无 NULL；顺序即契约声明序）。 */
static const XRenderKernelOps g_bgr888Kernel =
{
    bgr888_fillSpanOpaque,
    bgr888_fillSpanBlend,
    bgr888_blitSpan,
    bgr888_blendSpan,
    bgr888_glyphMaskSpan,
    bgr888_storePrem
};

/**
 * @brief 把 RGB888/BGR888 两张内核表注册到对应格式槽位。
 * @note  由 XRenderKernel.c 的惰性注册（xrenderkernel_registerBuiltins）
 *        调用；后注册覆盖先注册，平台/加速器变体（NEON/DMA2D）可随后
 *        覆盖。
 */
void XRenderKernel_registerRgb888(void)
{
#if XRENDERKERNEL_RGB888_ON
    XRenderKernel_register(XImageFormat_RGB888, &g_rgb888Kernel);
#endif
#if XRENDERKERNEL_BGR888_ON
    XRenderKernel_register(XImageFormat_BGR888, &g_bgr888Kernel);
#endif
}

#endif /* XPAINTER_ON && (XRENDERKERNEL_RGB888_ON || XRENDERKERNEL_BGR888_ON) */
