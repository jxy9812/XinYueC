/******************************************************************************
 * @file       XImageFormat.c
 * @brief      XImage 格式枚举实现
 * @author     XinYueC 团队
 * @note       提供像素格式的位深度、Alpha 通道判断等辅助函数
 ******************************************************************************/
#include "XImageFormat.h"
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
    {XImageFormat_Alpha8,                       8,  true,  false, "Alpha8",                       4},
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
    if (!info || width <= 0)
        return 0;

    /* QImage-owned buffers use 32-bit aligned scanlines. Keep the
       calculation wide so hostile dimensions cannot wrap to a small size. */
    const int64_t bitsPerLine = (int64_t)width * info->bitDepth;
    const int64_t bytesPerLine = ((bitsPerLine + 31) / 32) * 4;
    return bytesPerLine > 0 && bytesPerLine <= INT_MAX ? (int)bytesPerLine : 0;
}

XPixelFormat XImageFormat_pixelFormat(XImageFormat format)
{
    XPixelFormat result = { XPixelFormatModel_Invalid, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, false };
    switch (format)
    {
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
            result.m_model = XPixelFormatModel_Mono;
            result.m_channelCount = 1;
            result.m_redSize = 1;
            break;
        case XImageFormat_Indexed8:
            result.m_model = XPixelFormatModel_Indexed;
            result.m_channelCount = 1;
            break;
        case XImageFormat_Alpha8:
            result.m_model = XPixelFormatModel_Gray;
            result.m_channelCount = 1;
            result.m_alphaSize = 8;
            break;
        case XImageFormat_Grayscale8:
            result.m_model = XPixelFormatModel_Gray;
            result.m_channelCount = 1;
            result.m_redSize = 8;
            break;
        case XImageFormat_Grayscale16:
            result.m_model = XPixelFormatModel_Gray;
            result.m_channelCount = 1;
            result.m_redSize = 16;
            break;
        case XImageFormat_RGB32:
        case XImageFormat_ARGB32:
        case XImageFormat_ARGB32_Premultiplied:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 8;
            result.m_alphaSize = format == XImageFormat_RGB32 ? 0 : 8;
            result.m_premultiplied = format == XImageFormat_ARGB32_Premultiplied;
            result.m_byteOrdered = true;
            break;
        case XImageFormat_RGB16:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 3;
            result.m_redSize = result.m_blueSize = 5;
            result.m_greenSize = 6;
            break;
        case XImageFormat_RGB555:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 3;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 5;
            break;
        case XImageFormat_RGB444:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 3;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 4;
            break;
        case XImageFormat_ARGB4444_Premultiplied:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = result.m_alphaSize = 4;
            result.m_premultiplied = true;
            break;
        case XImageFormat_RGB888:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 3;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 8;
            break;
        case XImageFormat_BGR888:
            result.m_model = XPixelFormatModel_BGR;
            result.m_channelCount = 3;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 8;
            break;
        case XImageFormat_RGBX8888:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 8;
            break;
        case XImageFormat_RGBA8888:
        case XImageFormat_RGBA8888_Premultiplied:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = result.m_alphaSize = 8;
            result.m_premultiplied = format == XImageFormat_RGBA8888_Premultiplied;
            break;
        case XImageFormat_RGBX64:
        case XImageFormat_RGBA64:
        case XImageFormat_RGBA64_Premultiplied:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 16;
            result.m_alphaSize = format == XImageFormat_RGBX64 ? 0 : 16;
            result.m_premultiplied = format == XImageFormat_RGBA64_Premultiplied;
            break;
        case XImageFormat_BGR30:
        case XImageFormat_RGB30:
            result.m_model = format == XImageFormat_BGR30 ? XPixelFormatModel_BGR : XPixelFormatModel_RGB;
            result.m_channelCount = 3;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 10;
            break;
        case XImageFormat_A2BGR30_Premultiplied:
        case XImageFormat_A2RGB30_Premultiplied:
            result.m_model = format == XImageFormat_A2BGR30_Premultiplied ? XPixelFormatModel_BGR : XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 10;
            result.m_alphaSize = 2;
            result.m_premultiplied = true;
            break;
        case XImageFormat_RGBX16FPx4:
        case XImageFormat_RGBA16FPx4:
        case XImageFormat_RGBA16FPx4_Premultiplied:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 16;
            result.m_alphaSize = format == XImageFormat_RGBX16FPx4 ? 0 : 16;
            result.m_premultiplied = format == XImageFormat_RGBA16FPx4_Premultiplied;
            break;
        case XImageFormat_RGBX32FPx4:
        case XImageFormat_RGBA32FPx4:
        case XImageFormat_RGBA32FPx4_Premultiplied:
            result.m_model = XPixelFormatModel_RGB;
            result.m_channelCount = 4;
            result.m_redSize = result.m_greenSize = result.m_blueSize = 32;
            result.m_alphaSize = format == XImageFormat_RGBX32FPx4 ? 0 : 32;
            result.m_premultiplied = format == XImageFormat_RGBA32FPx4_Premultiplied;
            break;
        case XImageFormat_CMYK8888:
            result.m_model = XPixelFormatModel_CMYK;
            result.m_channelCount = 4;
            result.m_cyanSize = result.m_magentaSize = result.m_yellowSize = result.m_blackSize = 8;
            break;
        default:
            if (format >= XImageFormat_RGB666 && format <= XImageFormat_ARGB8555_Premultiplied)
            {
                result.m_model = XPixelFormatModel_RGB;
                result.m_channelCount = 3;
                result.m_redSize = result.m_greenSize = result.m_blueSize = 6;
                result.m_alphaSize = (format == XImageFormat_ARGB6666_Premultiplied) ? 6 :
                    (format == XImageFormat_ARGB8555_Premultiplied ? 8 : 0);
                result.m_premultiplied = result.m_alphaSize != 0;
            }
            break;
    }
    return result;
}
