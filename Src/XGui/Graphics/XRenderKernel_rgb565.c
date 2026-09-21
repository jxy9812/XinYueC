/******************************************************************************
 * @file       XRenderKernel_rgb565.c
 * @brief      XImageFormat_RGB16（RGB565）目标格式渲染内核（首个消费示例）。
 * @details    对标 Skia blitter 的逐格式 blitRow/blitRect 组织，并采用
 *             LVGL 目标格式内核（lv_draw_sw_blend_to_rgb565 思路）的
 *             "在目标格式域收尾"组织方式：所有输入统一为 0xAARRGGBB
 *             预乘 ARGB32 规范色（XPainter 的规范色），内核内部完成
 *             source-over 合成与 565 压缩，外部零格式分支。
 *             合成口径对齐（先读 XPainter.c 后确定；引用一律按函数名
 *             锚定、不写行号，避免上游演进导致行号漂移失准）：
 *             - 除法近似采用 XPainter.c 的 painterMul255 的
 *               (a*b+127)/255 精确四舍五入，而非 (x*255+127)>>8 变体
 *               ——后者与 painter 存在 ±1 口径差，两套近似不可混用；
 *             - alpha==0 保目标、alpha==255 直写两条快捷路径对应
 *               painterComposeColor 的 SourceOver 分支的两条 if 快捷
 *               （sa==0 返回目标、da==0 返回源）；
 *             - 字形覆盖率调制等价 Qt BYTE_MUL：预乘色各通道同乘覆盖率
 *               后仍为预乘色，再走 source-over，与 painter 文本回退
 *               路径 painterGlyphAlphaBlend 的 (coverage*a+127)/255
 *               调制同口径。
 *             565 打包按本内核契约使用截断式 ((r>>3)<<11)|((g>>2)<<5)|
 *             (b>>3)（对标 Qt qConvertRgb32To16）；注意 XImage_setPixel
 *             走的是四舍五入压缩（XImage.c 的 XImage_compress5/
 *             compress6），两者可在渲染层与直接读写层各自独立存在，
 *             本内核以契约为准。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRenderKernel.h"

#if XPAINTER_ON

/* ========== 565 编解码基元 ========== */

/**
 * @brief 把预乘 ARGB32 规范色压缩为 RGB565（截断式，契约固定公式）。
 * @note  对标 Qt qConvertRgb32To16；预乘色的 RGB 分量在 565（无 alpha
 *        位）目标上无需再做反预乘，直接按位截断。
 *        口径互引：XImage.c 的 XImage_compress5/compress6 为四舍五入
 *        式 ((v*N+127)/255)，本函数为截断式 (v>>N)——同一颜色经两侧
 *        压缩可差 1 LSB；渲染层（本内核）与直接读写层
 *        （XImage_setPixel）两套口径各自独立并存，见文件头契约说明。
 */
static uint16_t rgb565_pack(uint32_t argbPrem)
{
    unsigned r = (argbPrem >> 16) & 0xffu;
    unsigned g = (argbPrem >> 8) & 0xffu;
    unsigned b = argbPrem & 0xffu;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/** @brief 5 位通道展开为 8 位（低位位复制，对标 Skia SkR16 replicates）。 */
static unsigned rgb565_expand5(unsigned value)
{
    return (value << 3) | (value >> 2);
}

/** @brief 6 位通道展开为 8 位（低位位复制；绿通道专用）。 */
static unsigned rgb565_expand6(unsigned value)
{
    return (value << 2) | (value >> 4);
}

/**
 * @brief 8 位分量相乘并按 255 四舍五入。
 * @note  与 XPainter.c 的 painterMul255 逐字节同式，
 *        保证内核合成结果与 painter ARGB32 路径同口径。
 */
static unsigned rgb565_mul255(unsigned a, unsigned b)
{
    return (a * b + 127u) / 255u;
}

/**
 * @brief 预乘 source-over 单像素合成后写入 565 目标像素。
 * @param dstPixel 目标像素（565）。
 * @param srcPrem  源（预乘 ARGB32）。
 * @note  out = src + dst*(255-sa)/255（painter 同款，见文件头）。
 *        目标为不透明 565：先位复制展开到 8 位合成域，压缩回 565 收尾
 *        （LVGL blend_to_rgb565 的"目标格式域收尾"思路，但合成在
 *        ARGB32 精度域完成以保证与 painter 口径一致）。源 alpha 为 0
 *        或 255 时走快捷路径（painterComposeColor 的 SourceOver 快捷
 *        分支同款），避免读改写与无效乘法。
 */
static uint16_t rgb565_overPixel(uint16_t dstPixel, uint32_t srcPrem)
{
    unsigned sa = (srcPrem >> 24) & 0xffu;
    unsigned invA;
    unsigned dr;
    unsigned dg;
    unsigned db;
    if (sa == 0u) return dstPixel;      /* 全透明源：目标不变 */
    if (sa == 255u) return rgb565_pack(srcPrem); /* 不透明源：免混合直写 */
    invA = 255u - sa;
    dr = rgb565_expand5((dstPixel >> 11) & 0x1fu);
    dg = rgb565_expand6((dstPixel >> 5) & 0x3fu);
    db = rgb565_expand5(dstPixel & 0x1fu);
    {
        unsigned or = ((srcPrem >> 16) & 0xffu) + rgb565_mul255(dr, invA);
        unsigned og = ((srcPrem >> 8) & 0xffu) + rgb565_mul255(dg, invA);
        unsigned ob = (srcPrem & 0xffu) + rgb565_mul255(db, invA);
        return (uint16_t)(((or >> 3) << 11) | ((og >> 2) << 5) | (ob >> 3));
    }
}

/* ========== 内核表六个成员 ========== */

/**
 * @brief 不透明纯色填充一段 565 目标行。
 * @note  对标 Skia 的 565 blitH 宽字节填充与 LVGL fill 操作：打包一次，
 *        循环只做 16 位写。
 */
static void rgb565_fillSpanOpaque(uint8_t* rowBytes, int x, int count,
                                  uint32_t argbPrem)
{
    uint16_t* dst = (uint16_t*)(void*)rowBytes + x;
    uint16_t packed = rgb565_pack(argbPrem);
    int i;
    for (i = 0; i < count; ++i) dst[i] = packed;
}

/**
 * @brief 半透明纯色 source-over 填充一段 565 目标行。
 * @note  alpha==255 先查（契约第 3 条）：退化为不透明直写，免读改写；
 *        alpha==0 对应 painter 的 SourceOver 快捷分支
 *        （painterComposeColor）直接返回。src 常量把拆包/invA 提到
 *        循环外，逐像素只做 565 展开、三次乘加与压缩。
 */
static void rgb565_fillSpanBlend(uint8_t* rowBytes, int x, int count,
                                 uint32_t argbPrem)
{
    uint16_t* dst = (uint16_t*)(void*)rowBytes + x;
    unsigned sa = (argbPrem >> 24) & 0xffu;
    int i;
    if (sa == 0u) return;
    if (sa == 255u)
    {
        uint16_t packed = rgb565_pack(argbPrem);
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
            unsigned dr = rgb565_expand5((d >> 11) & 0x1fu);
            unsigned dg = rgb565_expand6((d >> 5) & 0x3fu);
            unsigned db = rgb565_expand5(d & 0x1fu);
            unsigned or = sr + rgb565_mul255(dr, invA);
            unsigned og = sg + rgb565_mul255(dg, invA);
            unsigned ob = sb + rgb565_mul255(db, invA);
            dst[i] = (uint16_t)(((or >> 3) << 11) | ((og >> 2) << 5) |
                                (ob >> 3));
        }
    }
}

/**
 * @brief 不透明源行拷贝到 565 目标行。
 * @note  对标 Skia 的 S32_D16 blitRow：srcRow 恒为 alpha==255 的
 *        ARGB32/RGB32 规范行（契约保证），逐像素打包拷贝，无混合。
 */
static void rgb565_blitSpan(uint8_t* dstRow, int dstX,
                            const uint32_t* srcRow, int srcX, int count)
{
    uint16_t* dst = (uint16_t*)(void*)dstRow + dstX;
    const uint32_t* src = srcRow + srcX;
    int i;
    for (i = 0; i < count; ++i) dst[i] = rgb565_pack(src[i]);
}

/**
 * @brief 预乘 source-over 源行混合到 565 目标行。
 * @note  逐像素先查源 alpha（契约第 3 条）：255 直写免读改写（对标
 *        Skia blitRow_s32a_opaque 的不透明快捷分支）、0 跳过
 *        （painterComposeColor 的 SourceOver 快捷分支同款），其余走
 *        rgb565_overPixel 合成。
 */
static void rgb565_blendSpan(uint8_t* dstRow, int dstX,
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
            dst[i] = rgb565_pack(s);
        }
        else
        {
            dst[i] = rgb565_overPixel(dst[i], s);
        }
    }
}

/**
 * @brief 字形灰度 mask 混合到 565 目标行。
 * @note  覆盖率调制等价 Qt BYTE_MUL：预乘色各通道乘覆盖率仍为预乘色，
 *        再 source-over 入目标。coverage==0 跳过（契约第 4 条）；
 *        调制用 rgb565_mul255，与 painter 文本回退路径
 *        painterGlyphAlphaBlend 的覆盖率缩放
 *        （(coverage*a+127)/255）同口径；
 *        调制后源不透明时直写，半透明走 rgb565_overPixel。
 */
static void rgb565_glyphMaskSpan(uint8_t* rowBytes, int x, int count,
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
                ((uint32_t)rgb565_mul255((colorPrem >> 24) & 0xffu,
                                         coverage) << 24) |
                ((uint32_t)rgb565_mul255((colorPrem >> 16) & 0xffu,
                                         coverage) << 16) |
                ((uint32_t)rgb565_mul255((colorPrem >> 8) & 0xffu,
                                         coverage) << 8) |
                (uint32_t)rgb565_mul255(colorPrem & 0xffu, coverage);
        }
        dst[i] = rgb565_overPixel(dst[i], modulated);
    }
}

/**
 * @brief putPixel 终端存储：预乘 ARGB32 单像素写入 565 目标行。
 * @note  XPainter putPixel 已完成合成，此处仅做 565 压缩与 16 位写；
 *        565 无 alpha 位，源 alpha 位丢弃。
 */
static void rgb565_storePrem(uint8_t* rowBytes, int x, uint32_t argbPrem)
{
    ((uint16_t*)(void*)rowBytes)[x] = rgb565_pack(argbPrem);
}

/* ========== 注册入口 ========== */

/** @brief RGB16 内核表（七个槽位全满，无 NULL；顺序即契约声明序）。 */
static const XRenderKernelOps g_rgb565Kernel =
{
    rgb565_fillSpanOpaque,
    rgb565_fillSpanBlend,
    rgb565_blitSpan,
    rgb565_blendSpan,
    rgb565_glyphMaskSpan,
    rgb565_storePrem
};

/**
 * @brief 把 RGB565 内核表注册到 XImageFormat_RGB16 槽位。
 * @note  由 XRenderKernel.c 的惰性注册（XRenderKernel.c:27）调用；
 *        后注册覆盖先注册，平台/加速器变体（NEON/DMA2D）可随后覆盖。
 */
void XRenderKernel_registerRgb565(void)
{
    XRenderKernel_register(XImageFormat_RGB16, &g_rgb565Kernel);
}

#endif /* XPAINTER_ON */
