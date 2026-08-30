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
#include <limits.h>
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

#if XIMAGECODEC_SVG_ON
/*
 * 为 SVG 头部探测生成有界 ASCII 视图。
 * Qt 的 QSvgTinyDocument::hasSvgHeader() 使用 QTextStream 读取探测缓冲区，
 * 因而 UTF-8、UTF-16 和 UTF-32 都可以参与同一套 <svg/DOCTYPE 判断；门面
 * 不能直接把 UTF-16/32 的零字节交给 strstr，否则自动 Handler 发现会比显式
 * SVG 解码少支持一组合法输入。这里仅转换头部所需的 ASCII 字符，非 ASCII
 * 标量折叠为 '?'，并限制最多 4096 个输入字节，避免不可信设备的无界扫描。
 */
static size_t codec_svg_prepareProbe(const uint8_t* data, size_t size,
                                     char* out, size_t capacity)
{
    size_t offset = 0;
    size_t unit = 1;
    bool little = true;
    bool encoded = false;
    size_t available;
    size_t count;
    size_t unitIndex = 0;
    size_t used = 0;
    if (!data || !size || !out || capacity < 2) return 0;

    if (size >= 4 && data[0] == 0xff && data[1] == 0xfe &&
        data[2] == 0x00 && data[3] == 0x00) {
        offset = 4; unit = 4; encoded = true;
    } else if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 &&
               data[2] == 0xfe && data[3] == 0xff) {
        offset = 4; unit = 4; little = false; encoded = true;
    } else if (size >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        offset = 2; unit = 2; encoded = true;
    } else if (size >= 2 && data[0] == 0xfe && data[1] == 0xff) {
        offset = 2; unit = 2; little = false; encoded = true;
    } else if (size >= 3 && data[0] == 0xef && data[1] == 0xbb &&
               data[2] == 0xbf) {
        offset = 3;
    } else if (size >= 4 && data[0] == '<' && data[1] == 0x00 &&
               data[2] == 0x00 && data[3] == 0x00) {
        unit = 4; encoded = true;
    } else if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 &&
               data[2] == 0x00 && data[3] == '<') {
        unit = 4; little = false; encoded = true;
    } else if (size >= 2 && data[0] == '<' && data[1] == 0x00) {
        unit = 2; encoded = true;
    } else if (size >= 2 && data[0] == 0x00 && data[1] == '<') {
        unit = 2; little = false; encoded = true;
    }

    if (!encoded) {
        count = size - offset;
        if (count > capacity - 1) count = capacity - 1;
        memcpy(out, data + offset, count);
        out[count] = '\0';
        return count;
    }
    if (offset > size || (size - offset) % unit != 0) return 0;
    available = (size - offset) / unit;
    count = available < capacity - 1 ? available : capacity - 1;
    while (unitIndex < count) {
        size_t pos = offset + unitIndex * unit;
        uint32_t cp;
        if (unit == 2) {
            cp = little ?
                (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) :
                ((uint32_t)data[pos] << 8) | (uint32_t)data[pos + 1];
        } else {
            cp = little ?
                (uint32_t)data[pos] | ((uint32_t)data[pos + 1] << 8) |
                ((uint32_t)data[pos + 2] << 16) |
                ((uint32_t)data[pos + 3] << 24) :
                ((uint32_t)data[pos] << 24) |
                ((uint32_t)data[pos + 1] << 16) |
                ((uint32_t)data[pos + 2] << 8) | (uint32_t)data[pos + 3];
        }
        out[used++] = cp <= 0x7fu ? (char)cp : '?';
        ++unitIndex;
    }
    out[used] = '\0';
    return used;
}
#endif /* XIMAGECODEC_SVG_ON */

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
        codec_is_name(format, "jpeg") ||
        codec_is_name(format, "jfif"))
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
    {
        char probe[4097];
        size_t probeSize = codec_svg_prepareProbe(data, size, probe,
                                                  sizeof(probe));
        size_t pos = 0;
        size_t scan;
        bool prefixed = false;
        if (!probeSize) return XImageCodecFormat_Unknown;
        data = (const uint8_t*)probe;
        size = probeSize;
        /* Accept BOM/ASCII whitespace before an XML declaration or root SVG.
         * This is common for hand-authored files and remains bounded so format
         * probing never scans an untrusted large buffer. */
        if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf)
            pos = 3;
        while (pos < size && (data[pos] == ' ' || data[pos] == '\t' ||
                              data[pos] == '\r' || data[pos] == '\n')) ++pos;
        if (size - pos >= 4 && data[pos] == '<' && data[pos + 1] == 's' &&
            data[pos + 2] == 'v' && data[pos + 3] == 'g')
            return XImageCodecFormat_Svg;
        if (size - pos >= 13 && data[pos] == '<' && data[pos + 1] == '!' &&
            memcmp(data + pos, "<!DOCTYPE svg", 13) == 0)
            return XImageCodecFormat_Svg;
        /* QSvgTinyDocument::hasSvgHeader() accepts an XML declaration or
         * leading comment only when the same bounded prefix also contains
         * an SVG root/doctype.  Do the same instead of accepting every XML
         * document as an image. */
        if (size - pos >= 5 && data[pos] == '<' && data[pos + 1] == '?')
            prefixed = memcmp(data + pos, "<?xml", 5) == 0;
        else if (size - pos >= 4 && data[pos] == '<' && data[pos + 1] == '!' &&
                 data[pos + 2] == '-' && data[pos + 3] == '-')
            prefixed = true;
        if (prefixed) {
            for (scan = pos + 1; scan <= size - 4; ++scan) {
                if (data[scan] == '<' && data[scan + 1] == 's' &&
                    data[scan + 2] == 'v' && data[scan + 3] == 'g')
                    return XImageCodecFormat_Svg;
                if (size - scan >= 13 && data[scan] == '<' &&
                    memcmp(data + scan, "<!DOCTYPE svg", 13) == 0)
                    return XImageCodecFormat_Svg;
            }
        }
    }
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

/* JPEG SOF 尺寸探测：轻量扫描段标记，不解码像素数据。 */
static bool codec_probeJpegSize(const uint8_t* data, size_t size,
                                int* width, int* height)
{
    size_t pos;
    int marker;
    int precedence;
    if (!data || size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
        !width || !height)
        return false;
    pos = 2;
    /* 跳过 SOI 后可能出现若干 0xff 填充字节。 */
    while (pos < size && data[pos] == 0xff) ++pos;
    if (pos >= size) return false;
    marker = data[pos++];
    if (marker == 0xd9 || marker == 0x00) return false;
    /* 标记拥有段长：0xD8(S0I)、0x01(TEM)、D0..D7(重启) 无段长。 */
    precedence = marker == 0xd8 || marker == 0x01 ||
                 (marker >= 0xd0 && marker <= 0xd7);
    if (!precedence) {
        uint16_t len;
        size_t segLen;
        if (pos + 2 > size) return false;
        len = XImageCodecInternal_readU16BE(data + pos);
        segLen = (size_t)len;
        if (segLen < 2 || pos + segLen > size) return false;
        pos += segLen;
    }
    while (pos + 1 < size) {
        while (pos < size && data[pos] != 0xff) ++pos;
        if (pos + 1 >= size) return false;
        if (data[pos + 1] == 0x00) { /* 字节填充，继续查找后续 0xff。 */
            pos += 2;
            continue;
        }
        ++pos;
        if (data[pos] == 0xff) continue; /* 连续 0xff，继续收缩标记。 */
        marker = data[pos++];
        if (marker == 0x00) continue;
        if (marker >= 0xd0 && marker <= 0xd7) continue; /* 重启标记。 */
        if (marker == 0xda || marker == 0xd9) return false;
        if (pos + 2 > size) return false;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 &&
            marker != 0xc8 && marker != 0xcc) {
            uint16_t segLen2 = XImageCodecInternal_readU16BE(data + pos);
            uint32_t h, w;
            size_t body;
            if (segLen2 < 5 || pos + (size_t)segLen2 > size) return false;
            body = pos + 2;
            h = (uint32_t)XImageCodecInternal_readU16BE(data + body + 1);
            w = (uint32_t)XImageCodecInternal_readU16BE(data + body + 3);
            if (h == 0 || w == 0 || h > (uint32_t)INT_MAX ||
                w > (uint32_t)INT_MAX)
                return false;
            *height = (int)h;
            *width = (int)w;
            return true;
        }
        {
            uint16_t len2 = XImageCodecInternal_readU16BE(data + pos);
            if (len2 < 2 || pos + (size_t)len2 > size) return false;
            pos += (size_t)len2;
        }
    }
    return false;
}

bool XImageCodec_probeSize(const uint8_t* data, size_t size,
                           XImageCodecFormat format,
                           int* width, int* height)
{
    if (!data || !width || !height) return false;
    if (format == XImageCodecFormat_Unknown)
        format = XImageCodec_detect(data, size);
    switch (format) {
#if XIMAGECODEC_BMP_ON
        case XImageCodecFormat_Bmp: {
            int32_t signedWidth;
            int32_t signedHeight;
            if (size < 26 || data[0] != 'B' || data[1] != 'M') return false;
            signedWidth = (int32_t)XImageCodecInternal_readU32LE(data + 18);
            signedHeight = (int32_t)XImageCodecInternal_readU32LE(data + 22);
            if (signedWidth <= 0 || signedHeight == 0 ||
                signedWidth > INT_MAX || signedHeight == INT32_MIN)
                return false;
            if (signedHeight < 0) signedHeight = -signedHeight;
            *width = (int)signedWidth;
            *height = (int)signedHeight;
            return true;
        }
#endif
#if XIMAGECODEC_PNG_ON
        case XImageCodecFormat_Png: {
            uint32_t w, h;
            if (size < 24 ||
                memcmp(data, "\x89PNG\r\n\x1a\n", 8) != 0) return false;
            w = XImageCodecInternal_readU32BE(data + 16);
            h = XImageCodecInternal_readU32BE(data + 20);
            if (w == 0 || h == 0 || w > (uint32_t)INT_MAX ||
                h > (uint32_t)INT_MAX)
                return false;
            *width = (int)w;
            *height = (int)h;
            return true;
        }
#endif
#if XIMAGECODEC_GIF_ON
        case XImageCodecFormat_Gif: {
            uint16_t w, h;
            if (size < 10 ||
                (memcmp(data, "GIF87a", 6) != 0 &&
                 memcmp(data, "GIF89a", 6) != 0)) return false;
            w = XImageCodecInternal_readU16LE(data + 6);
            h = XImageCodecInternal_readU16LE(data + 8);
            if (w == 0 || h == 0) return false;
            *width = (int)w;
            *height = (int)h;
            return true;
        }
#endif
#if XIMAGECODEC_JPEG_ON
        case XImageCodecFormat_Jpeg:
            return codec_probeJpegSize(data, size, width, height);
#endif
#if XIMAGECODEC_SVG_ON
        case XImageCodecFormat_Svg:
            return XImageCodecInternal_probeSvgSize(data, size, width, height);
#endif
        default:
            return false;
    }
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
