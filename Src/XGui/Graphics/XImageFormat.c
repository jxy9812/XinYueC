/******************************************************************************
 * @file       XImageFormat.c
 * @brief      XImage 格式枚举实现
 * @author     XinYueC 团队
 * @note       提供像素格式的位深度、Alpha 通道判断等辅助函数
 ******************************************************************************/
#include "XImageFormat.h"
#include "CXinYueConfig.h"
#include <stddef.h>
#include <limits.h>

/**
 * @brief      像素格式信息表
 */
typedef struct XImageFormatInfo
{
    XImageFormat format;       /**< 格式枚举 */
    int          bitDepth;     /**< 每像素位数 */
    bool         hasAlpha;     /**< 是否包含 Alpha */
    bool         premultiplied;/**< 是否预乘 */
    const char*  name;         /**< 格式名称 */
    int          alignment;    /**< 行对齐字节数 */
}XImageFormatInfo;

static const XImageFormatInfo g_formatInfo[] = {
    {XImageFormat_Invalid,                      0,  false, false, "Invalid",                      4},
    {XImageFormat_Mono,                         1,  false, false, "Mono",                         4},
    {XImageFormat_MonoLSB,                      1,  false, false, "MonoLSB",                      4},
    {XImageFormat_Indexed8,                     8,  false, false, "Indexed8",                     4},
    {XImageFormat_RGB32,                       32,  false, false, "RGB32",                        4},
    {XImageFormat_ARGB32,                      32,  true,  false, "ARGB32",                       4},
    {XImageFormat_ARGB32_Premultiplied,         32,  true,  true,  "ARGB32_Premultiplied",         4},
    {XImageFormat_RGB16,                       16,  false, false, "RGB16",                        4},
    {XImageFormat_ARGB8565_Premultiplied,       24,  true,  true,  "ARGB8565_Premultiplied",       4},
    {XImageFormat_RGB666,                      24,  false, false, "RGB666",                       4},
    {XImageFormat_ARGB6666_Premultiplied,       24,  true,  true,  "ARGB6666_Premultiplied",       4},
    {XImageFormat_RGB555,                      16,  false, false, "RGB555",                       4},
    {XImageFormat_ARGB8555_Premultiplied,       24,  true,  true,  "ARGB8555_Premultiplied",       4},
    {XImageFormat_RGB888,                      24,  false, false, "RGB888",                       4},
    {XImageFormat_RGB444,                      16,  false, false, "RGB444",                       4},
    {XImageFormat_ARGB4444_Premultiplied,       16,  true,  true,  "ARGB4444_Premultiplied",       4},
    {XImageFormat_RGBX8888,                    32,  false, false, "RGBX8888",                     4},
    {XImageFormat_RGBA8888,                    32,  true,  false, "RGBA8888",                     4},
    {XImageFormat_RGBA8888_Premultiplied,       32,  true,  true,  "RGBA8888_Premultiplied",       4},
    {XImageFormat_BGR30,                       32,  false, false, "BGR30",                        4},
    {XImageFormat_A2BGR30_Premultiplied,        32,  true,  true,  "A2BGR30_Premultiplied",        4},
    {XImageFormat_RGB30,                       32,  false, false, "RGB30",                        4},
    {XImageFormat_A2RGB30_Premultiplied,        32,  true,  true,  "A2RGB30_Premultiplied",        4},
    /* Qt qimage.cpp:6229-6241 describes Alpha8 as a premultiplied
       alpha-only layout; keep the fast format query consistent with
       XImageFormat_toPixelFormat(). */
    {XImageFormat_Alpha8,                       8,  true,  true,  "Alpha8",                       4},
    {XImageFormat_Grayscale8,                   8,  false, false, "Grayscale8",                    4},
    {XImageFormat_RGBX64,                      64,  false, false, "RGBX64",                       4},
    {XImageFormat_RGBA64,                      64,  true,  false, "RGBA64",                       4},
    {XImageFormat_RGBA64_Premultiplied,         64,  true,  true,  "RGBA64_Premultiplied",         4},
    {XImageFormat_Grayscale16,                 16,  false, false, "Grayscale16",                  4},
    {XImageFormat_BGR888,                      24,  false, false, "BGR888",                       4},
    {XImageFormat_RGBX16FPx4,                  64,  false, false, "RGBX16FPx4",                   4},
    {XImageFormat_RGBA16FPx4,                  64,  true,  false, "RGBA16FPx4",                   4},
    {XImageFormat_RGBA16FPx4_Premultiplied,     64,  true,  true,  "RGBA16FPx4_Premultiplied",     4},
    {XImageFormat_RGBX32FPx4,                 128,  false, false, "RGBX32FPx4",                   4},
    {XImageFormat_RGBA32FPx4,                 128,  true,  false, "RGBA32FPx4",                   4},
    {XImageFormat_RGBA32FPx4_Premultiplied,    128,  true,  true,  "RGBA32FPx4_Premultiplied",     4},
    {XImageFormat_CMYK8888,                    32,  false, false, "CMYK8888",                     4},
};

#define XIMAGEFORMAT_COUNT (sizeof(g_formatInfo) / sizeof(g_formatInfo[0]))

/**
 * @brief      获取格式信息
 * @param format 像素格式
 * @return 格式信息指针，无效格式返回 NULL
 */
static const XImageFormatInfo* XImageFormat_info(XImageFormat format)
{
    if (format < 0 || format >= XImageFormat_NImageFormats)
        return NULL;
    for (int i = 0; i < XIMAGEFORMAT_COUNT; i++)
    {
        if (g_formatInfo[i].format == format)
            return &g_formatInfo[i];
    }
    return NULL;
}

int XImageFormat_bitDepth(XImageFormat format)
{
    const XImageFormatInfo* info = XImageFormat_info(format);
    return info ? info->bitDepth : 0;
}

bool XImageFormat_hasAlpha(XImageFormat format)
{
    const XImageFormatInfo* info = XImageFormat_info(format);
    return info ? info->hasAlpha : false;
}

bool XImageFormat_isPremultiplied(XImageFormat format)
{
    const XImageFormatInfo* info = XImageFormat_info(format);
    return info ? info->premultiplied : false;
}

int XImageFormat_bytesPerLineAlignment(XImageFormat format)
{
    const XImageFormatInfo* info = XImageFormat_info(format);
    return info ? info->alignment : 4;
}

int XImageFormat_bytesPerLine(int width, XImageFormat format)
{
    const XImageFormatInfo* info = XImageFormat_info(format);
    if (!info || info->bitDepth <= 0 || width <= 0)
        return 0;

    /* Qt 的 calculateImageParameters() 在计算字节数后仍保留一个
       32 位宽度上限：width 不能超过 (INT_MAX - 31) / depth。这个
       看似比最终 bytesPerLine 更严格的检查用于禁止后续以 int 计算
       width * depth 时发生溢出；不能只依赖 64 位临时值来放宽它。 */
    if ((int64_t)width > ((int64_t)INT_MAX - 31) / info->bitDepth)
        return 0;

    /* QImage 自有缓冲区使用 32 位对齐的扫描行。使用宽临时值避免
       位数相乘或加上对齐余量时先在 int 中回绕成小尺寸。 */
    const int64_t bitsPerLine = (int64_t)width * info->bitDepth;
    const int64_t bytesPerLine = ((bitsPerLine + 31) / 32) * 4;
    return bytesPerLine > 0 && bytesPerLine <= INT_MAX ? (int)bytesPerLine : 0;
}

static XPixelFormat XPixelFormat_make(XPixelFormatModel model,
                                      uint8_t first, uint8_t second,
                                      uint8_t third, uint8_t fourth,
                                      uint8_t fifth, uint8_t alpha,
                                      XPixelFormatAlphaUsage alphaUsage,
                                      XPixelFormatAlphaPosition alphaPosition,
                                      XPixelFormatAlphaPremultiplied premultiplied,
                                      XPixelFormatTypeInterpretation type)
{
    XPixelFormat result = { 0 };
    const XPixelFormatByteOrder sourceByteOrder =
        XPixelFormatByteOrder_CurrentSystemEndian;
    result.m_model = model;
    result.m_redSize = first;
    result.m_greenSize = second;
    result.m_blueSize = third;
    result.m_alphaSize = alpha;
    result.m_fourthSize = fourth;
    result.m_fifthSize = fifth;
    result.m_premultiplied = premultiplied == XPixelFormatAlpha_Premultiplied;
    result.m_alphaUsage = alphaUsage;
    result.m_alphaPosition = alphaPosition;
    result.m_typeInterpretation = type;
    /* QPixelFormat receives CurrentSystemEndian in the static table, then
       resolves it in its constructor before byteOrder() exposes the value. */
#if IS_BIG_ENDIAN
    result.m_byteOrder = sourceByteOrder == XPixelFormatByteOrder_CurrentSystemEndian
        ? XPixelFormatByteOrder_BigEndian : sourceByteOrder;
#else
    result.m_byteOrder = sourceByteOrder == XPixelFormatByteOrder_CurrentSystemEndian
        ? XPixelFormatByteOrder_LittleEndian : sourceByteOrder;
#endif
    result.m_channelCount = (uint8_t)((first != 0) + (second != 0) +
                                      (third != 0) + (fourth != 0) +
                                      (fifth != 0) + (alpha != 0));
    if (model == XPixelFormatModel_CMYK)
    {
        result.m_cyanSize = first;
        result.m_magentaSize = second;
        result.m_yellowSize = third;
        result.m_blackSize = fourth;
    }
    return result;
}

bool XPixelFormat_equals(const XPixelFormat* left, const XPixelFormat* right)
{
    if (!left || !right) return left == right;
    return left->m_model == right->m_model &&
           left->m_redSize == right->m_redSize &&
           left->m_greenSize == right->m_greenSize &&
           left->m_blueSize == right->m_blueSize &&
           left->m_alphaSize == right->m_alphaSize &&
           left->m_cyanSize == right->m_cyanSize &&
           left->m_magentaSize == right->m_magentaSize &&
           left->m_yellowSize == right->m_yellowSize &&
           left->m_blackSize == right->m_blackSize &&
           left->m_fourthSize == right->m_fourthSize &&
           left->m_fifthSize == right->m_fifthSize &&
           left->m_channelCount == right->m_channelCount &&
           left->m_premultiplied == right->m_premultiplied &&
           left->m_alphaUsage == right->m_alphaUsage &&
           left->m_alphaPosition == right->m_alphaPosition &&
           left->m_typeInterpretation == right->m_typeInterpretation &&
           left->m_byteOrder == right->m_byteOrder &&
           left->m_subEnum == right->m_subEnum;
}

XPixelFormat XImageFormat_toPixelFormat(XImageFormat format)
{
    switch (format)
    {
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
            return XPixelFormat_make(XPixelFormatModel_Indexed, 1, 0, 0, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_Indexed8:
            return XPixelFormat_make(XPixelFormatModel_Indexed, 8, 0, 0, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_RGB32:
            return XPixelFormat_make(XPixelFormatModel_RGB, 8, 8, 8, 0, 0, 8,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_ARGB32:
            return XPixelFormat_make(XPixelFormatModel_RGB, 8, 8, 8, 0, 0, 8,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_ARGB32_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 8, 8, 8, 0, 0, 8,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_RGB16:
            return XPixelFormat_make(XPixelFormatModel_RGB, 5, 6, 5, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_ARGB8565_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 5, 6, 5, 0, 0, 8,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_RGB666:
            return XPixelFormat_make(XPixelFormatModel_RGB, 6, 6, 6, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_ARGB6666_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 6, 6, 6, 0, 0, 6,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_RGB555:
            return XPixelFormat_make(XPixelFormatModel_RGB, 5, 5, 5, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_ARGB8555_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 5, 5, 5, 0, 0, 8,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_RGB888:
            return XPixelFormat_make(XPixelFormatModel_RGB, 8, 8, 8, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_RGB444:
            return XPixelFormat_make(XPixelFormatModel_RGB, 4, 4, 4, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_ARGB4444_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 4, 4, 4, 0, 0, 4,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_RGBX8888:
            return XPixelFormat_make(XPixelFormatModel_RGB, 8, 8, 8, 0, 0, 8,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_RGBA8888:
            return XPixelFormat_make(XPixelFormatModel_RGB, 8, 8, 8, 0, 0, 8,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_RGBA8888_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 8, 8, 8, 0, 0, 8,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_BGR30:
            return XPixelFormat_make(XPixelFormatModel_BGR, 10, 10, 10, 0, 0, 2,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_A2BGR30_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_BGR, 10, 10, 10, 0, 0, 2,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_RGB30:
            return XPixelFormat_make(XPixelFormatModel_RGB, 10, 10, 10, 0, 0, 2,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_A2RGB30_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 10, 10, 10, 0, 0, 2,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedInteger);
        case XImageFormat_Alpha8:
            return XPixelFormat_make(XPixelFormatModel_Alpha, 0, 0, 0, 0, 0, 8,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtBeginning,
                                     /* Qt qimage.cpp:6229-6241 marks the
                                        alpha-only layout as Premultiplied;
                                        retain that descriptor even though
                                        no RGB channel exists to multiply. */
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_Grayscale8:
            return XPixelFormat_make(XPixelFormatModel_Gray, 8, 0, 0, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_RGBX64:
            return XPixelFormat_make(XPixelFormatModel_RGB, 16, 16, 16, 0, 0, 16,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_RGBA64:
            return XPixelFormat_make(XPixelFormatModel_RGB, 16, 16, 16, 0, 0, 16,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_RGBA64_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 16, 16, 16, 0, 0, 16,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_Grayscale16:
            return XPixelFormat_make(XPixelFormatModel_Gray, 16, 0, 0, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedShort);
        case XImageFormat_BGR888:
            return XPixelFormat_make(XPixelFormatModel_BGR, 8, 8, 8, 0, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedByte);
        case XImageFormat_RGBX16FPx4:
            return XPixelFormat_make(XPixelFormatModel_RGB, 16, 16, 16, 0, 0, 16,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_FloatingPoint);
        case XImageFormat_RGBA16FPx4:
            return XPixelFormat_make(XPixelFormatModel_RGB, 16, 16, 16, 0, 0, 16,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_FloatingPoint);
        case XImageFormat_RGBA16FPx4_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 16, 16, 16, 0, 0, 16,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_FloatingPoint);
        case XImageFormat_RGBX32FPx4:
            return XPixelFormat_make(XPixelFormatModel_RGB, 32, 32, 32, 0, 0, 32,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_FloatingPoint);
        case XImageFormat_RGBA32FPx4:
            return XPixelFormat_make(XPixelFormatModel_RGB, 32, 32, 32, 0, 0, 32,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_FloatingPoint);
        case XImageFormat_RGBA32FPx4_Premultiplied:
            return XPixelFormat_make(XPixelFormatModel_RGB, 32, 32, 32, 0, 0, 32,
                                     XPixelFormatAlpha_Uses,
                                     XPixelFormatAlpha_AtEnd,
                                     XPixelFormatAlpha_Premultiplied,
                                     XPixelFormatType_FloatingPoint);
        case XImageFormat_CMYK8888:
            return XPixelFormat_make(XPixelFormatModel_CMYK, 8, 8, 8, 8, 0, 0,
                                     XPixelFormatAlpha_Ignores,
                                     XPixelFormatAlpha_AtBeginning,
                                     XPixelFormatAlpha_NotPremultiplied,
                                     XPixelFormatType_UnsignedInteger);
        default:
            return (XPixelFormat){ 0 };
    }
}

XPixelFormat XImageFormat_pixelFormat(XImageFormat format)
{
    XPixelFormat result = XImageFormat_toPixelFormat(format);
    /* 保留旧版字段的历史含义；新代码应使用 m_byteOrder。 */
    result.m_byteOrdered = format == XImageFormat_RGB32 ||
                           format == XImageFormat_ARGB32 ||
                           format == XImageFormat_ARGB32_Premultiplied;
    return result;
}

XImageFormat XImageFormat_toImageFormat(XPixelFormat format)
{
    int i;
    XPixelFormat candidate;
    for (i = 0; i < XImageFormat_NImageFormats; ++i)
    {
        candidate = XImageFormat_toPixelFormat((XImageFormat)i);
        if (XPixelFormat_equals(&format, &candidate))
            return (XImageFormat)i;
    }
    return XImageFormat_Invalid;
}
