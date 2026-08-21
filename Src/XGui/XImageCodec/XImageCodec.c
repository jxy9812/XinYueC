/*****************************************************************************/
/**
 * @file       XImageCodec.c
 * @brief      XImageCodec 统一编解码门面：格式注册、识别与分发。
 * @note       各格式实现分别位于 XImageCodecBmp.c / XImageCodecPng.c /
 *              XImageCodecGif.c / XImageCodecJpeg.c / XImageCodecSvg.c，
 *              公共入口统一为 XImageCodec_* 系列 API；上层图像类（XImage、
 *              XPixmap、XImageReader、XImageWriter 等）只调用本模块公共接口。
 *              格式裁剪见 XImageCodec_config.h（总开关 XIMAGECODEC_ON，
 *              各格式 XIMAGECODEC_BMP_ON / PNG_ON / JPEG_ON / GIF_ON / SVG_ON）。
 */
#include "XImageCodec.h"
#include "XImageCodecInternal.h"
#include "XImageCodec_config.h"
#include "XMemory.h"
#include <ctype.h>
#include <string.h>

#if XIMAGECODEC_ON

/* 格式名不区分大小写逐字符比较。 */
static bool codec_is_name(const XString* format, const char* expected)
{
    const char* value = XString_toUtf8(format);
    size_t i;
    if (!value || !value[0]) return false;
    for (i = 0; value[i] && expected[i]; ++i)
        if (tolower((unsigned char)value[i]) !=
            tolower((unsigned char)expected[i]))
            return false;
    return value[i] == '\0' && expected[i] == '\0';
}

XImageCodecFormat XImageCodec_formatFromName(const XString* format)
{
    if (!format || XContainer_isEmpty_base((const XContainer*)format))
        return XImageCodecFormat_Unknown;
#if XIMAGECODEC_BMP_ON
    if (codec_is_name(format, "bmp")) return XImageCodecFormat_Bmp;
#endif
#if XIMAGECODEC_PNG_ON
    if (codec_is_name(format, "png")) return XImageCodecFormat_Png;
#endif
#if XIMAGECODEC_JPEG_ON
    if (codec_is_name(format, "jpg") ||
        codec_is_name(format, "jpeg"))
        return XImageCodecFormat_Jpeg;
#endif
#if XIMAGECODEC_GIF_ON
    if (codec_is_name(format, "gif")) return XImageCodecFormat_Gif;
#endif
#if XIMAGECODEC_SVG_ON
    if (codec_is_name(format, "svg") ||
        codec_is_name(format, "svgz"))
        return XImageCodecFormat_Svg;
#endif
    return XImageCodecFormat_Unknown;
}

XImageCodecFormat XImageCodec_formatFromName_2(const char* format)
{
    XString* value;
    XImageCodecFormat result;
    if (!format || !format[0]) return XImageCodecFormat_Unknown;
    value = XString_create_utf8(format);
    if (!value) return XImageCodecFormat_Unknown;
    result = XImageCodec_formatFromName(value);
    XString_delete_base((XClass*)value);
    return result;
}

XImageCodecFormat XImageCodec_detect(const uint8_t* data, size_t size)
{
    if (!data) return XImageCodecFormat_Unknown;
#if XIMAGECODEC_BMP_ON
    if (size >= 2 && data[0] == 'B' && data[1] == 'M')
        return XImageCodecFormat_Bmp;
#endif
#if XIMAGECODEC_PNG_ON
    if (size >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8))
        return XImageCodecFormat_Png;
#endif
#if XIMAGECODEC_JPEG_ON
    if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
        return XImageCodecFormat_Jpeg;
#endif
#if XIMAGECODEC_GIF_ON
    if (size >= 6 &&
        (!memcmp(data, "GIF87a", 6) || !memcmp(data, "GIF89a", 6)))
        return XImageCodecFormat_Gif;
#endif
#if XIMAGECODEC_SVG_ON
    if (size >= 5 && data[0] == '<' &&
        (data[1] == '?' || data[1] == 's' || data[1] == 'S'))
        return XImageCodecFormat_Svg;
#endif
    return XImageCodecFormat_Unknown;
}

static const char* codec_formatNameUtf8(XImageCodecFormat format)
{
    switch (format) {
        case XImageCodecFormat_Bmp: return "bmp";
        case XImageCodecFormat_Png: return "png";
        case XImageCodecFormat_Jpeg: return "jpeg";
        case XImageCodecFormat_Gif: return "gif";
        case XImageCodecFormat_Svg: return "svg";
        default: return NULL;
    }
}

XString* XImageCodec_formatName(XImageCodecFormat format)
{
    const char* name = codec_formatNameUtf8(format);
    return name ? XString_create_utf8(name) : XString_create();
}

const char* XImageCodec_formatName_2(XImageCodecFormat format)
{
    return codec_formatNameUtf8(format);
}

bool XImageCodec_canDecode(XImageCodecFormat format)
{
#if XIMAGECODEC_BMP_ON
    if (format == XImageCodecFormat_Bmp) return true;
#endif
#if XIMAGECODEC_PNG_ON
    if (format == XImageCodecFormat_Png) return true;
#endif
#if XIMAGECODEC_JPEG_ON
    if (format == XImageCodecFormat_Jpeg) return true;
#endif
#if XIMAGECODEC_GIF_ON
    if (format == XImageCodecFormat_Gif) return true;
#endif
#if XIMAGECODEC_SVG_ON
    if (format == XImageCodecFormat_Svg) return true;
#endif
    return false;
}

bool XImageCodec_canEncode(XImageCodecFormat format)
{
#if XIMAGECODEC_BMP_ON
    if (format == XImageCodecFormat_Bmp) return true;
#endif
#if XIMAGECODEC_PNG_ON
    if (format == XImageCodecFormat_Png) return true;
#endif
#if XIMAGECODEC_JPEG_ON
    if (format == XImageCodecFormat_Jpeg) return true;
#endif
#if XIMAGECODEC_GIF_ON
    if (format == XImageCodecFormat_Gif) return true;
#endif
#if XIMAGECODEC_SVG_ON
    if (format == XImageCodecFormat_Svg) return true;
#endif
    return false;
}

bool XImageCodec_decode(const uint8_t* data, size_t size,
                        XImageCodecFormat format, XImage* out)
{
    if (format == XImageCodecFormat_Unknown)
        format = XImageCodec_detect(data, size);
    if (!data || !out) return false;
    switch (format) {
#if XIMAGECODEC_BMP_ON
        case XImageCodecFormat_Bmp:
            return XImageCodecInternal_decodeBmp(data, size, out);
#endif
#if XIMAGECODEC_PNG_ON
        case XImageCodecFormat_Png:
            return XImageCodecInternal_decodePng(data, size, out);
#endif
#if XIMAGECODEC_GIF_ON
        case XImageCodecFormat_Gif:
            return XImageCodecInternal_decodeGif(data, size, out);
#endif
#if XIMAGECODEC_JPEG_ON
        case XImageCodecFormat_Jpeg:
            return XImageCodecInternal_decodeJpeg(data, size, out);
#endif
#if XIMAGECODEC_SVG_ON
        case XImageCodecFormat_Svg:
            return XImageCodecInternal_decodeSvg(data, size, out);
#endif
        default:
            return false;
    }
}

bool XImageCodec_encode(const XImage* image, XImageCodecFormat format,
                        int quality, XByteArray* out)
{
    (void)quality;
    if (!image || !out) return false;
    switch (format) {
#if XIMAGECODEC_BMP_ON
        case XImageCodecFormat_Bmp:
            return XImageCodecInternal_encodeBmp(image, out);
#endif
#if XIMAGECODEC_PNG_ON
        case XImageCodecFormat_Png:
            return XImageCodecInternal_encodePng(image, out);
#endif
#if XIMAGECODEC_GIF_ON
        case XImageCodecFormat_Gif:
            return XImageCodecInternal_encodeGif(image, out);
#endif
#if XIMAGECODEC_JPEG_ON
        case XImageCodecFormat_Jpeg:
            return XImageCodecInternal_encodeJpeg(image, quality, out);
#endif
#if XIMAGECODEC_SVG_ON
        case XImageCodecFormat_Svg:
            return XImageCodecInternal_encodeSvg(image, out);
#endif
        default:
            return false;
    }
}

#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
XImageCodecAnimation* XImageCodec_decodeAnimation(const uint8_t* data, size_t size)
{
    XImageCodecAnimation* animation;
    if (!data || !size || XImageCodec_detect(data, size) != XImageCodecFormat_Gif)
        return NULL;
    animation = (XImageCodecAnimation*)XMalloc_System(sizeof(XImageCodecAnimation));
    if (!animation) return NULL;
    if (!XImageCodecInternal_decodeGifFrames(data, size, animation)) {
        XFree_System(animation);
        return NULL;
    }
    return animation;
}

void XImageCodecAnimation_delete(XImageCodecAnimation* animation)
{
    if (!animation) return;
    if (animation->frames) {
        for (int i = 0; i < animation->frameCount; ++i)
            XImage_deinit_base(&animation->frames[i].image);
        XFree_System(animation->frames);
    }
    XFree_System(animation);
}
#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */

#endif /* XIMAGECODEC_ON */
