/******************************************************************************
 * @file       XRenderKernel_gray8.c
 * @brief      XImageFormat_Grayscale8（8 位灰度）目标格式渲染内核
 *             （单色 OLED/灰度屏核心路径）。
 * @details    延续 XRenderKernel_rgb565.c 的组织方式（Skia blitter 的
 *             逐格式内核表 + LVGL blend_to_l8 的"目标格式域收尾"）：
 *             输入统一为 0xAARRGGBB 预乘 ARGB32 规范色（XPainter 的
 *             规范色），内核内部完成 source-over 合成与灰度换算，
 *             外部零格式分支。
 *             灰度换算口径经 XImage.c 核实：已有现成灰度换算
 *             XImage_luma（(299r+587g+114b+500)/1000，
 *             XImage_writePixelValue 的 Grayscale8/Grayscale16 分支
 *             同源），本文件 gray8_luma 逐式对齐（含 +500 四舍
 *             五入项）；其为 XImage.c 私有 static，故在内核内以
 *             同式复刻并以注释锚定函数名。1 字节像素：不透明
 *             填充/半透明不透明快捷路径为纯 memset（对标 LVGL
 *             blend_to_l8 的 L8 填充快路径），混合与字形调制在
 *             8 位灰度域直接做 source-over（OLED/单色屏文本
 *             核心路径）。
 *             合成口径与 rgb565 内核一致：painterMul255 同式精确四舍
 *             五入 (a*b+127)/255；sa==0 保目标、sa==255 直写两条快捷
 *             路径对应 painterComposeColor 的 SourceOver 快捷分支；
 *             字形覆盖率调制等价 Qt BYTE_MUL。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRenderKernel.h"

#if XPAINTER_ON && XRENDERKERNEL_GRAYSCALE8_ON

#include "XMemory.h" /* XMemset（Grayscale8 填充快路径）；对标全库 XMem* 纪律 */

/* ========== 8 位灰度编解码基元 ========== */

/** @brief 8 位分量相乘并按 255 四舍五入（painterMul255 同式）。 */
static unsigned gray8_mul255(unsigned a, unsigned b)
{
    return (a * b + 127u) / 255u;
}

/**
 * @brief 预乘 ARGB32 规范色换算为 8 位灰度。
 * @note  逐式对齐 XImage.c 的 XImage_luma（该函数为 static 私有，此处
 *        以同式复刻并按函数名锚定）：(299r + 587g + 114b + 500)/1000，
 *        含 +500 四舍五入项，与 XImage_writePixelValue 的 Grayscale8
 *        分支（line[x] = XImage_luma(color)）口径一致。输入为预乘色：
 *        灰度权重为正且和为 1，对 RGB 线性映射满足
 *        luma(预乘色) = 预乘(luma)，故单通道域可直接在预乘色上换算，
 *        与 rgb565 内核"无 alpha 位目标免反预乘"的口径同理由。
 */
static uint8_t gray8_luma(uint32_t colorPrem)
{
    unsigned r = (colorPrem >> 16) & 0xffu;
    unsigned g = (colorPrem >> 8) & 0xffu;
    unsigned b = colorPrem & 0xffu;
    return (uint8_t)((299u * r + 587u * g + 114u * b + 500u) / 1000u);
}

/**
 * @brief 预乘 source-over 单像素合成后写入 8 位灰度目标像素。
 * @param dstGray 目标像素（灰度值）。
 * @param srcPrem 源（预乘 ARGB32）。
 * @note  out = srcGray + dstGray*(255-sa)/255：灰度权重线性保证预乘
 *        域单通道合成与 ARGB32 逐通道合成同构（rgb565_overPixel 的
 *        单通道特例）；预乘保证 srcGray <= sa，结果不越界 255 免钳位。
 *        源 alpha 为 0 或 255 时走快捷路径（painterComposeColor 的
 *        SourceOver 快捷分支同款）。
 */
static uint8_t gray8_overPixel(uint8_t dstGray, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    if (sa == 0u) return dstGray;          /* 全透明源：目标不变 */
    if (sa == 255u) return gray8_luma(srcPrem); /* 不透明源：直写 */
    return (uint8_t)(gray8_luma(srcPrem) +
                     gray8_mul255(dstGray, 255u - sa));
}

/* ========== Grayscale8 内核表六个成员 ========== */

/**
 * @brief 不透明纯色填充一段 8 位灰度目标行。
 * @note  对标 LVGL blend_to_l8 的 L8 不透明填充快路径：换算一次灰度，
 *        纯 memset 收尾（1 字节像素的极致快路径）。
 */
static void gray8_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                 uint32_t argbPrem)
{
    XMemset(rowBytes + x, (int)gray8_luma(argbPrem), (size_t)count);
}

/**
 * @brief 半透明纯色 source-over 填充一段 8 位灰度目标行。
 * @note  alpha==255 退化为 memset 直写免读改写；alpha==0 直接返回
 *        （painter 的 SourceOver 快捷分支同款）。sg/invA 提到循环外，
 *        逐像素只做一次 8 位乘加。
 */
static void gray8_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                uint32_t argbPrem)
{
    uint8_t* dst = rowBytes + x;
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        XMemset(dst, (int)gray8_luma(argbPrem), (size_t)count);
        return;
    }
    {
        unsigned invA = 255u - sa;
        unsigned sg = gray8_luma(argbPrem);
        for (i = 0; i < count; ++i)
        {
            dst[i] = (uint8_t)(sg + gray8_mul255(dst[i], invA));
        }
    }
}

/**
 * @brief 不透明源行拷贝到 8 位灰度目标行。
 * @note  srcRow 为 ARGB32 规范行而目标 1 字节/像素，无整行 memcpy
 *        快路径可言，逐像素灰度换算写入（填充路径才有 memset 快路径，
 *        见 gray8_fillSpanOpaque）。
 */
static void gray8_blitSpan(uint8_t* dstRow, int dstX,
                           const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + dstX;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i) dst[i] = gray8_luma(src[i]);
}

/**
 * @brief 预乘 source-over 源行混合到 8 位灰度目标行。
 * @note  逐像素先查源 alpha：255 直写免读改写、0 跳过
 *        （painterComposeColor 的 SourceOver 快捷分支同款），其余在
 *        8 位灰度域走 gray8_overPixel 合成。
 */
static void gray8_blendSpan(uint8_t* dstRow, int dstX,
                            const uint32_t* srcRow, int srcX, int count)
{
    uint8_t* dst = dstRow + dstX;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i)
    {
        uint32_t s = src[i];
        unsigned sa = (s >> 24) & 0xffu;
        if (sa == 255u)
        {
            dst[i] = gray8_luma(s);
        }
        else if (sa != 0u)
        {
            dst[i] = gray8_overPixel(dst[i], s);
        }
    }
}

/**
 * @brief 字形灰度 mask 混合到 8 位灰度目标行（OLED/单色屏文本核心路径）。
 * @note  rgb565_glyphMaskSpan 的单通道版：覆盖率调制等价 Qt BYTE_MUL
 *        （预乘色各通道乘覆盖率仍为预乘色），再在 8 位灰度域做
 *        source-over。coverage==0 跳过；调制用 gray8_mul255，与 painter


 *        （(coverage*a+127)/255）同口径；coverage==255 且色不透明时
 *        退化为灰度直写。
 */
static void gray8_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
                                const uint8_t* mask, uint32_t colorPrem)
{
    uint8_t* dst = rowBytes + x;
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
                ((uint32_t)gray8_mul255((colorPrem >> 24) & 0xffu,
                                        coverage) << 24) |
                ((uint32_t)gray8_mul255((colorPrem >> 16) & 0xffu,
                                        coverage) << 16) |
                ((uint32_t)gray8_mul255((colorPrem >> 8) & 0xffu,
                                        coverage) << 8) |
                (uint32_t)gray8_mul255(colorPrem & 0xffu, coverage);
        }
        dst[i] = gray8_overPixel(dst[i], modulated);
    }
}

/**
 * @brief putPixel 终端存储：预乘 ARGB32 单像素写入 8 位灰度目标行。
 * @note  XPainter putPixel 已完成合成，此处仅做灰度换算与 8 位写。
 */
static void gray8_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    rowBytes[x] = gray8_luma(argbPrem);
}

/* ========== 注册入口 ========== */

/** @brief Grayscale8 内核表（六个槽位全满，无 NULL；顺序即契约声明序）。 */
static const XRenderKernelOps g_gray8Kernel =
{
    gray8_fillSpanOpaque,
    gray8_fillSpanBlend,
    gray8_blitSpan,
    gray8_blendSpan,
    gray8_glyphMaskSpan,
    gray8_storePrem
};

/**
 * @brief 把 Grayscale8 内核表注册到对应槽位。
 * @note  由 XRenderKernel.c 的惰性注册（xrenderkernel_registerBuiltins）
 *        调用；后注册覆盖先注册，平台/加速器变体可随后覆盖。
 */
void XRenderKernel_registerGray8(void)
{
    XRenderKernel_register(XImageFormat_Grayscale8, &g_gray8Kernel);
}

#endif /* XPAINTER_ON && XRENDERKERNEL_GRAYSCALE8_ON */
