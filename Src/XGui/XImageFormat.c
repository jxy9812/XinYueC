/******************************************************************************
 * @file       XImageFormat.c
 * @brief      XImage 格式枚举实现
 * @author     XinYueC 团队
 * @note       提供像素格式的位深度、Alpha 通道判断等辅助函数
 ******************************************************************************/
#include "XImageFormat.h"
#include <stddef.h>

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
    {XImageFormat_RGB16,                       16,  false, false, "RGB16",                        2},
    {XImageFormat_ARGB8565_Premultiplied,       24,  true,  true,  "ARGB8565_Premultiplied",       4},
    {XImageFormat_RGB666,                      18,  false, false, "RGB666",                       4},
    {XImageFormat_ARGB6666_Premultiplied,       24,  true,  true,  "ARGB6666_Premultiplied",       4},
    {XImageFormat_RGB555,                      16,  false, false, "RGB555",                       2},
    {XImageFormat_ARGB8555_Premultiplied,       24,  true,  true,  "ARGB8555_Premultiplied",       4},
    {XImageFormat_RGB888,                      24,  false, false, "RGB888",                       4},
    {XImageFormat_RGB444,                      12,  false, false, "RGB444",                       2},
    {XImageFormat_ARGB4444_Premultiplied,       16,  true,  true,  "ARGB4444_Premultiplied",       2},
    {XImageFormat_RGBX8888,                    32,  false, false, "RGBX8888",                     4},
    {XImageFormat_RGBA8888,                    32,  true,  false, "RGBA8888",                     4},
    {XImageFormat_RGBA8888_Premultiplied,       32,  true,  true,  "RGBA8888_Premultiplied",       4},
    {XImageFormat_BGR30,                       32,  false, false, "BGR30",                        4},
    {XImageFormat_A2BGR30_Premultiplied,        32,  true,  true,  "A2BGR30_Premultiplied",        4},
    {XImageFormat_RGB30,                       32,  false, false, "RGB30",                        4},
    {XImageFormat_A2RGB30_Premultiplied,        32,  true,  true,  "A2RGB30_Premultiplied",        4},
    {XImageFormat_Alpha8,                       8,  true,  false, "Alpha8",                       4},
    {XImageFormat_Grayscale8,                   8,  false, false, "Grayscale8",                    4},
    {XImageFormat_RGBX64,                      64,  false, false, "RGBX64",                       8},
    {XImageFormat_RGBA64,                      64,  true,  false, "RGBA64",                       8},
    {XImageFormat_RGBA64_Premultiplied,         64,  true,  true,  "RGBA64_Premultiplied",         8},
    {XImageFormat_Grayscale16,                 16,  false, false, "Grayscale16",                  8},
    {XImageFormat_BGR888,                      24,  false, false, "BGR888",                       4},
    {XImageFormat_RGBX16FPx4,                  64,  false, false, "RGBX16FPx4",                   8},
    {XImageFormat_RGBA16FPx4,                  64,  true,  false, "RGBA16FPx4",                   8},
    {XImageFormat_RGBA16FPx4_Premultiplied,     64,  true,  true,  "RGBA16FPx4_Premultiplied",     8},
    {XImageFormat_RGBX32FPx4,                 128,  false, false, "RGBX32FPx4",                  16},
    {XImageFormat_RGBA32FPx4,                 128,  true,  false, "RGBA32FPx4",                  16},
    {XImageFormat_RGBA32FPx4_Premultiplied,    128,  true,  true,  "RGBA32FPx4_Premultiplied",    16},
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

    int bitsPerLine = width * info->bitDepth;
    int alignment = info->alignment;
    // 对齐到 alignment 字节
    int bytesPerLine = (bitsPerLine + 7) / 8;
    // 向上对齐
    int mod = bytesPerLine % alignment;
    if (mod != 0)
        bytesPerLine += alignment - mod;
    return bytesPerLine;
}
