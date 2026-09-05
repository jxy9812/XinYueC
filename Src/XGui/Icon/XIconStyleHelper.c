/******************************************************************************
 * @file       XIconStyleHelper.c
 * @brief      XIcon 样式态生成内部实现。
 * @details    Disabled 使用 QCommonStyle::generatedIconPixmap() 的灰度映射
 *             到“黑 -> 窗口背景 -> 白”颜色表；Selected 使用 Highlight 色
 *             alpha=0.3 的 SourceAtop 合成。Active/Normal 原样返回。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XIconStyleHelper.h"
#include "XGuiApplication.h"
#include "XPalette.h"
#include "XColor.h"
#include "XImage.h"
#include "XImageFormat.h"

static uint64_t paletteHashDefault(const XPalette* palette)
{
    uint64_t hash = 1469598103934665603ULL;
    int group;
    int role;
    if (!palette) return hash;
    for (group = 0; group < XPaletteColorGroup_NColorGroups; ++group) {
        for (role = 0; role < XPaletteColorRole_NColorRoles; ++role) {
            XColor c = XPalette_color(palette, (XPaletteColorGroup)group,
                                      (XPaletteColorRole)role);
            uint32_t rgba = XColor_rgba(&c);
            hash ^= (uint64_t)rgba + 0x9e3779b97f4a7c15ULL +
                    (hash << 6) + (hash >> 2);
        }
    }
    return hash;
}

uint64_t XIconStyleHelper_paletteCacheKey(void)
{
#if XGUIAPPLICATION_ON && XPALETTE_ON
    XPalette palette = XGuiApplication_palette();
    return paletteHashDefault(&palette);
#else
    return 0;
#endif
}

static int clamp255(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static int qGrayOfPixel(uint32_t argb)
{
    int r = (int)((argb >> 16) & 0xffu);
    int g = (int)((argb >> 8) & 0xffu);
    int b = (int)(argb & 0xffu);
    return (r * 11 + g * 16 + b * 5) >> 5;
}

static void styleDisabled(const XPixmap* base, XPalette* palette, XPixmap* out)
{
    XImage image;
    XColor bg;
    int red;
    int green;
    int blue;
    unsigned char reds[256];
    unsigned char greens[256];
    unsigned char blues[256];
    int intensity;
    int i;
    int x;
    int y;
    int width;
    int height;

    if (!base || !palette || !out) return;
    XImage_init(&image);
    XPixmap_toImage(base, &image);
    if (!XImage_convertToFormatInPlace(&image, XImageFormat_ARGB32, 0)) {
        XImage_deinit_base(&image);
        return;
    }
    width = XImage_width(&image);
    height = XImage_height(&image);
    bg = XPalette_color(palette, XPaletteColorGroup_Disabled,
                        XPaletteColorRole_Window);
    red = XColor_red(&bg);
    green = XColor_green(&bg);
    blue = XColor_blue(&bg);
    for (i = 0; i < 128; ++i) {
        reds[i] = (unsigned char)((red * (i << 1)) >> 8);
        greens[i] = (unsigned char)((green * (i << 1)) >> 8);
        blues[i] = (unsigned char)((blue * (i << 1)) >> 8);
    }
    for (i = 0; i < 128; ++i) {
        reds[i + 128] = (unsigned char)clamp255(red + (i << 1));
        greens[i + 128] = (unsigned char)clamp255(green + (i << 1));
        blues[i + 128] = (unsigned char)clamp255(blue + (i << 1));
    }
    intensity = (77 * red + 150 * green + 28 * blue) / 255;
    {
        const int factor = 191;
        if ((red - factor > green && red - factor > blue) ||
            (green - factor > red && green - factor > blue) ||
            (blue - factor > red && blue - factor > green))
            intensity = clamp255(intensity + 91);
        else if (intensity <= 128)
            intensity -= 51;
    }
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint32_t pixel = XImage_pixel(&image, x, y);
            int gray = qGrayOfPixel(pixel);
            int ci = gray / 3 + (130 - intensity / 3);
            uint32_t color;
            if (ci < 0) ci = 0;
            if (ci > 255) ci = 255;
            color = ((pixel & 0xff000000u)) |
                    ((uint32_t)reds[ci] << 16) |
                    ((uint32_t)greens[ci] << 8) |
                    (uint32_t)blues[ci];
            XImage_setPixel(&image, x, y, color);
        }
    }
    XPixmap_fromImage(&image, 0, out);
    XPixmap_setDevicePixelRatio(out, XPixmap_devicePixelRatio(base));
    XImage_deinit_base(&image);
}

static void styleSelected(const XPixmap* base, XPalette* palette, XPixmap* out)
{
    XImage image;
    XColor highlight;
    int srcAlpha;
    int srcRed;
    int srcGreen;
    int srcBlue;
    int x;
    int y;
    int width;
    int height;

    if (!base || !palette || !out) return;
    XImage_init(&image);
    XPixmap_toImage(base, &image);
    if (!XImage_convertToFormatInPlace(&image,
                                       XImageFormat_ARGB32_Premultiplied, 0)) {
        XImage_deinit_base(&image);
        return;
    }
    width = XImage_width(&image);
    height = XImage_height(&image);
    highlight = XPalette_color(palette, XPaletteColorGroup_Active,
                               XPaletteColorRole_Highlight);
    XColor_setAlphaF(&highlight, 0.3f);
    srcAlpha = XColor_alpha(&highlight);
    srcRed = XColor_red(&highlight);
    srcGreen = XColor_green(&highlight);
    srcBlue = XColor_blue(&highlight);
    srcRed = srcRed * srcAlpha / 255;
    srcGreen = srcGreen * srcAlpha / 255;
    srcBlue = srcBlue * srcAlpha / 255;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint32_t pixel = XImage_pixel(&image, x, y);
            int da = (int)((pixel >> 24) & 0xffu);
            int dr = (int)((pixel >> 16) & 0xffu);
            int dg = (int)((pixel >> 8) & 0xffu);
            int db = (int)(pixel & 0xffu);
            int inv = 255 - srcAlpha;
            int outR = (srcRed * da) / 255 + ((dr * inv) / 255);
            int outG = (srcGreen * da) / 255 + ((dg * inv) / 255);
            int outB = (srcBlue * da) / 255 + ((db * inv) / 255);
            uint32_t color = ((uint32_t)da << 24) |
                    ((uint32_t)clamp255(outR) << 16) |
                    ((uint32_t)clamp255(outG) << 8) |
                    (uint32_t)clamp255(outB);
            XImage_setPixel(&image, x, y, color);
        }
    }
    XPixmap_fromImage(&image, 0, out);
    XPixmap_setDevicePixelRatio(out, XPixmap_devicePixelRatio(base));
    XImage_deinit_base(&image);
}

void XIconStyleHelper_apply(XIconMode mode, const XPixmap* base, XPixmap* out)
{
    if (!out || !base || XPixmap_isNull(base)) return;
    if (mode != XIconMode_Disabled && mode != XIconMode_Selected) {
        XCopy(out, base);
        return;
    }
#if XGUIAPPLICATION_ON && XPALETTE_ON
    XPalette palette = XGuiApplication_palette();
    if (mode == XIconMode_Disabled)
        styleDisabled(base, &palette, out);
    else
        styleSelected(base, &palette, out);
#else
    XCopy(out, base);
#endif
}
