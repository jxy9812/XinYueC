/******************************************************************************
 * @file       XImageBuiltinPlugin.c
 * @brief      XImageCodec 内置图像插件实现。
 * @note       插件把 XImageCodec 的格式发现、解码与编码能力包装成 Qt 风格
 *             图像插件，供 XImagePluginRegistry 统一发现和创建处理器。
 ******************************************************************************/
#include "XImageBuiltinPlugin.h"
#include "XImageCodec.h"
#include "XImageCodecInternal.h"
#include "XImageIOHandler.h"
#include "XContainer.h"
#include "XByteArray.h"
#include "XStringList.h"
#include "XMemory.h"
#include <string.h>

#if XIMAGEIOPLUGIN_ON

static const char* const g_builtinFormats[] =
    /* Qt 公共支持列表只列规范键；JPEG 别名和 PPM raw 子类型仍在
       capabilities/create 中接受，但不作为独立插件键枚举。 */
    { "bmp", "png", "jpg", "jpeg", "jfif", "gif",
      "pbm", "pgm", "ppm",
      "xbm", "xpm", "svg", "svgz",
#if XIMAGECODEC_ICO_ON
      "ico", "cur",
#endif
      "dib" };
static const char* const g_builtinMimeTypes[] =
    { "image/bmp", "image/png", "image/jpeg", "image/jpeg", "image/jpeg",
      "image/gif", "image/x-portable-bitmap", "image/x-portable-graymap",
      "image/x-portable-pixmap",
      "image/x-xbitmap", "image/x-xpixmap", "image/svg+xml",
      "image/svg+xml-compressed",
#if XIMAGECODEC_ICO_ON
      "image/vnd.microsoft.icon", "image/vnd.microsoft.icon",
#endif
      "" };
static const char* const g_builtinNameFilters[] =
    { "*.bmp", "*.png", "*.jpg", "*.jpeg", "*.jfif", "*.gif",
      "*.pbm", "*.pgm", "*.ppm",
      "*.xbm", "*.xpm", "*.svg", "*.svgz",
#if XIMAGECODEC_ICO_ON
      "*.ico", "*.cur",
#endif
      "" };

XCLASS_DEFINE_BEGING(XImageBuiltinHandler)
XCLASS_DEFINE_EXTEND_END(XImageBuiltinHandler, XImageIOHandler)

typedef struct XImageBuiltinHandler
{
    XImageIOHandler m_base; /**< 基类成员；必须是第一个。 */
    XString* m_description; /**< PNG Description 选项；由处理器独占。 */
    XString* m_subType; /**< PPM 子类型选项；由处理器独占。 */
} XImageBuiltinHandler;

XCLASS_DEFINE_BEGING(XImageBuiltinPlugin)
XCLASS_DEFINE_EXTEND_END(XImageBuiltinPlugin, XImageIOPlugin)

typedef struct XImageBuiltinPlugin
{
    XImageIOPlugin m_base;    /**< 基类成员；必须是第一个。 */
    XStringList*   m_keys;    /**< 插件声明的格式键列表。 */
    XStringList*   m_mimes;   /**< 插件声明的 MIME 类型列表。 */
    XStringList*   m_filters; /**< 插件声明的文件名过滤器列表。 */
} XImageBuiltinPlugin;

#if XIMAGECODEC_ON

static XImageCodecFormat builtin_formatFromString(const XString* format)
{
    return XImageCodec_formatFromName_2(XString_toUtf8(format));
}

static uint32_t builtin_capabilityForFormat(const XString* format)
{
    XImageCodecFormat codecFormat;
    uint32_t capability = 0;
    const char* formatUtf8;
    bool svgz;
    if (!format || XContainer_isEmpty_base((const XContainer*)format))
        return 0;
    formatUtf8 = XString_toUtf8(format);
    svgz = formatUtf8 && strcmp(formatUtf8, "svgz") == 0;
    codecFormat = builtin_formatFromString(format);
    if (codecFormat == XImageCodecFormat_Unknown)
        return 0;
    if (XImageCodec_canDecode(codecFormat))
        capability |= (uint32_t)XImageIOPlugin_CanRead;
    /* The embedded SVG encoder emits plain XML; Qt's svgz key is read-only
       here until a gzip writer is added. */
    if (!svgz && XImageCodec_canEncode(codecFormat))
        capability |= (uint32_t)XImageIOPlugin_CanWrite;
    return capability;
}

static XImageCodecFormat builtin_detectWithDevice(XIODevice* device)
{
    XByteArray* header;
    XImageCodecFormat format = XImageCodecFormat_Unknown;
    if (!device) return format;
    /* Qt's SVG handler probes a 4096-byte prefix so an XML declaration,
       comments, or a doctype may precede the root element.  The same bounded
       prefix is safe for the other codecs and avoids rejecting valid SVG
       documents merely because their prologue is longer than 16 bytes. */
    header = XIODevice_peek_3(device, 4096);
    if (header) {
        format = XImageCodec_detect(
            (const uint8_t*)XByteArray_data(header),
            (size_t)XByteArray_size_base((const XContainer*)header));
        XByteArray_delete_base((XClass*)header);
    }
    return format;
}

/* QPpmHandler::canRead() 返回的子类型由 P[1] 决定，而不是由调用方
 * 请求的 pbmraw/pgmraw/ppmraw 别名决定。该辅助函数只窥视两个字节，
 * 不改变设备位置，也不分配临时字符串。 */
static const char* builtin_ppmSubtypeName(XIODevice* device)
{
    XByteArray* bytes;
    const uint8_t* data;
    size_t size;
    const char* subtype = NULL;
    if (!device) return NULL;
    bytes = XIODevice_peek_3(device, 2);
    if (!bytes) return NULL;
    data = (const uint8_t*)XByteArray_data(bytes);
    size = (size_t)XByteArray_size_base((const XContainer*)bytes);
    if (data && size >= 2 && data[0] == 'P') {
        if (data[1] == '1' || data[1] == '4') subtype = "pbm";
        else if (data[1] == '2' || data[1] == '5') subtype = "pgm";
        else if (data[1] == '3' || data[1] == '6') subtype = "ppm";
    }
    XByteArray_delete_base((XClass*)bytes);
    return subtype;
}

/* 按 Qt QBmpHandler::option(ImageFormat) 规则从 BMP 信息头推导像素格式。
 * 仅窥视固定头部，不改变设备当前位置，也不触发整幅图像分配。 */
static XImageFormat builtin_bmpImageFormat(XIODevice* device)
{
    XByteArray* bytes;
    const uint8_t* data;
    size_t size;
    uint32_t dibSize;
    uint32_t compression = 0;
    uint32_t alphaMask = 0;
    uint16_t bpp;
    if (!device) return XImageFormat_Invalid;
    /* Qt QBmpHandler accepts the 12-byte legacy Windows/OS2 header as well
       as the modern INFO/V4/V5 headers.  Read enough for the largest fixed
       fields, but classify only after checking the DIB size. */
    bytes = XIODevice_peek_3(device, 70);
    if (!bytes) return XImageFormat_Invalid;
    data = (const uint8_t*)XByteArray_data(bytes);
    size = (size_t)XByteArray_size_base((const XContainer*)bytes);
    if (!data || size < 26 || data[0] != 'B' || data[1] != 'M') {
        XByteArray_delete_base((XClass*)bytes);
        return XImageFormat_Invalid;
    }
    dibSize = (uint32_t)data[14] | ((uint32_t)data[15] << 8) |
              ((uint32_t)data[16] << 16) | ((uint32_t)data[17] << 24);
    if (dibSize == 12u) {
        bpp = (uint16_t)data[24] | ((uint16_t)data[25] << 8);
    } else if (dibSize >= 40u && size >= 34u) {
        bpp = (uint16_t)data[28] | ((uint16_t)data[29] << 8);
        compression = (uint32_t)data[30] | ((uint32_t)data[31] << 8) |
                      ((uint32_t)data[32] << 16) | ((uint32_t)data[33] << 24);
        /* QBmpHandler exposes ARGB32 for a V4/V5 32-bit bitfield image
           carrying a non-zero alpha mask; plain BI_RGB remains RGB32. */
        if (dibSize >= 108u && size >= 70u)
            alphaMask = (uint32_t)data[66] | ((uint32_t)data[67] << 8) |
                        ((uint32_t)data[68] << 16) | ((uint32_t)data[69] << 24);
    } else {
        XByteArray_delete_base((XClass*)bytes);
        return XImageFormat_Invalid;
    }
    XByteArray_delete_base((XClass*)bytes);
    switch (bpp) {
        case 32:
        case 24:
        case 16:
            return (bpp == 32u && (compression == 3u || compression == 4u) &&
                    alphaMask != 0u)
                ? XImageFormat_ARGB32 : XImageFormat_RGB32;
        case 8:
        case 4:
            return XImageFormat_Indexed8;
        default:
            return XImageFormat_Mono;
    }
}

static XImageFormat builtin_imageFormat(XImageIOHandler* self)
{
    XImageCodecFormat format;
    if (!self) return XImageFormat_Invalid;
    format = builtin_formatFromString(XImageIOHandler_format_const(self));
    if (format == XImageCodecFormat_Unknown)
        format = builtin_detectWithDevice(XImageIOHandler_device(self));
    if (format == XImageCodecFormat_Bmp)
        return builtin_bmpImageFormat(XImageIOHandler_device(self));
    if (format == XImageCodecFormat_Dib) {
        XByteArray* bytes = XIODevice_peek_3(XImageIOHandler_device(self), 70);
        const uint8_t* data;
        size_t size;
        uint32_t dibSize;
        uint16_t bpp;
        uint32_t compression = 0;
        uint32_t alphaMask = 0;
        if (!bytes) return XImageFormat_Invalid;
        data = (const uint8_t*)XByteArray_data(bytes);
        size = (size_t)XByteArray_size_base((const XContainer*)bytes);
        if (!data || size < 12u) {
            XByteArray_delete_base((XClass*)bytes);
            return XImageFormat_Invalid;
        }
        dibSize = XImageCodecInternal_readU32LE(data);
        if (dibSize == 12u) bpp = XImageCodecInternal_readU16LE(data + 10u);
        else if ((dibSize == 40u || dibSize == 64u || dibSize == 108u || dibSize == 124u) && size >= 30u) {
            bpp = XImageCodecInternal_readU16LE(data + 14u);
            compression = XImageCodecInternal_readU32LE(data + 16u);
            if (dibSize >= 108u && size >= 56u)
                alphaMask = XImageCodecInternal_readU32LE(data + 52u);
        } else {
            XByteArray_delete_base((XClass*)bytes);
            return XImageFormat_Invalid;
        }
        XByteArray_delete_base((XClass*)bytes);
        if (bpp == 32u && (compression == 3u || compression == 4u) &&
            alphaMask != 0u)
            return XImageFormat_ARGB32;
        if (bpp >= 24u) return XImageFormat_RGB32;
        if (bpp >= 4u) return XImageFormat_Indexed8;
        return XImageFormat_Mono;
    }
    if (format == XImageCodecFormat_Ppm) {
        XByteArray* bytes = XIODevice_peek_3(XImageIOHandler_device(self), 2);
        const uint8_t* data;
        size_t size;
        uint8_t type;
        if (!bytes) return XImageFormat_Invalid;
        data = (const uint8_t*)XByteArray_data(bytes);
        size = (size_t)XByteArray_size_base((const XContainer*)bytes);
        if (!data || size < 2 || data[0] != 'P' ||
            data[1] < '1' || data[1] > '6') {
            XByteArray_delete_base((XClass*)bytes);
            return XImageFormat_Invalid;
        }
        type = data[1];
        XByteArray_delete_base((XClass*)bytes);
        return (type == '1' || type == '4')
            ? XImageFormat_Mono
            : ((type == '2' || type == '5')
                ? XImageFormat_Grayscale8 : XImageFormat_RGB32);
    }
#if XIMAGECODEC_XBM_ON
    if (format == XImageCodecFormat_Xbm)
        return XImageFormat_MonoLSB;
#endif
#if XIMAGECODEC_XPM_ON
    if (format == XImageCodecFormat_Xpm) {
        XByteArray* bytes = XIODevice_peek_3(XImageIOHandler_device(self), 512);
        int width = 0;
        int height = 0;
        if (!bytes) return XImageFormat_Invalid;
        XImageFormat imageFormat = XImageFormat_Invalid;
        if (!XImageCodecInternal_probeXpmImageFormat(
                (const uint8_t*)XByteArray_data(bytes),
                (size_t)XByteArray_size_base((const XContainer*)bytes),
                &width, &height, &imageFormat)) {
            XByteArray_delete_base((XClass*)bytes);
            return XImageFormat_Invalid;
        }
        XByteArray_delete_base((XClass*)bytes);
        /* QXpmHandler reports Indexed8 for <=256 colors and Invalid for
           larger palettes until the complete table has been consumed. */
        return imageFormat;
    }
#endif
#if XIMAGECODEC_ICO_ON
    if (format == XImageCodecFormat_Ico) {
        XByteArray* bytes = XIODevice_peek_3(XImageIOHandler_device(self), 32);
        int width = 0;
        int height = 0;
        uint16_t bitCount = 0;
        if (!bytes) return XImageFormat_Invalid;
        if (!XImageCodecInternal_probeIcoSize(
                (const uint8_t*)XByteArray_data(bytes),
                (size_t)XByteArray_size_base((const XContainer*)bytes),
                &width, &height)) {
            XByteArray_delete_base((XClass*)bytes);
            return XImageFormat_Invalid;
        }
        if (XByteArray_size_base((const XContainer*)bytes) >= 14u)
            bitCount = XImageCodecInternal_readU16LE(
                (const uint8_t*)XByteArray_data(bytes) + 12u);
        XByteArray_delete_base((XClass*)bytes);
        /* Qt's QIcoHandler::option(ImageFormat) reports the directory
           bit-count: 1-bit mono, 24-bit RGB32, 32-bit ARGB32, and indexed
           for the remaining palette depths. */
        if (bitCount == 1u || bitCount == 2u)
            return XImageFormat_Mono;
        if (bitCount == 24u)
            return XImageFormat_RGB32;
        if (bitCount == 32u)
            return XImageFormat_ARGB32;
        return XImageFormat_Indexed8;
    }
#endif
    /* The portable codec facade currently normalizes non-BMP decoded output
       to ARGB32; keep that as the handler's conservative image format. */
    if (format != XImageCodecFormat_Unknown)
        return XImageFormat_ARGB32;
    return XImageFormat_Invalid;
}

/* 从随机访问 JPEG 设备的头部读取 Qt 风格的 EXIF 方向变换。 */
static XImageIOHandlerTransformation builtin_jpegTransformation(XIODevice* device)
{
    XByteArray* bytes;
    int transformation = 0;
    bool ok;
    if (!device || XIODevice_isSequential(device))
        return XImageIOHandlerTransformation_None;
    bytes = XIODevice_peek_3(device, 262144);
    if (!bytes) return XImageIOHandlerTransformation_None;
    ok = XImageCodecInternal_probeJpegTransformation(
        (const uint8_t*)XByteArray_data(bytes),
        (size_t)XByteArray_size_base((const XContainer*)bytes),
        &transformation);
    XByteArray_delete_base((XClass*)bytes);
    return ok && transformation >= 0 && transformation <= 7
        ? (XImageIOHandlerTransformation)transformation
        : XImageIOHandlerTransformation_None;
}

/* 选项支持必须随具体格式变化。Qt 的 QBmpHandler 只声明 Size 和
 * ImageFormat，而 PNG/JPEG 还声明 Quality；PNG 的 Description、Gamma 和
 * CompressionRatio 由便携处理器实际接入并在读写两侧分别反映。 */
static bool builtin_supportsOption(const XImageIOHandler* self,
                                   XImageIOHandlerOption option)
{
    XIODevice* device;
    XImageCodecFormat format;
    if (!self) return false;
    format = builtin_formatFromString(XImageIOHandler_format_const(self));
    if (format == XImageCodecFormat_Unknown)
        format = builtin_detectWithDevice(XImageIOHandler_device(self));

    /* Qt QGifHandler only exposes Animation for sequential devices; Size is
       available for random-access devices after scanning frame headers.  GIF
       deliberately does not expose ImageFormat, unlike BMP/PNG/JPEG/SVG. */
    if (format == XImageCodecFormat_Gif) {
#if XIMAGECODEC_GIF_ANIM_ON
        if (option == XImageIOHandlerOption_Animation)
            return true;
#endif
        if (option == XImageIOHandlerOption_Size) {
            device = XImageIOHandler_device(self);
            return device && !XIODevice_isSequential(device);
        }
        return false;
    }
    if (format == XImageCodecFormat_Ppm) {
        return option == XImageIOHandlerOption_Size ||
               option == XImageIOHandlerOption_ImageFormat ||
               option == XImageIOHandlerOption_SubType;
    }
    if (format == XImageCodecFormat_Jpeg &&
        option == XImageIOHandlerOption_ImageTransformation) {
        device = XImageIOHandler_device(self);
        return device && XIODevice_isReadable(device);
    }
#if XIMAGECODEC_XBM_ON
    if (format == XImageCodecFormat_Xbm) {
        return option == XImageIOHandlerOption_Name ||
               option == XImageIOHandlerOption_Size ||
               option == XImageIOHandlerOption_ImageFormat;
    }
#endif
#if XIMAGECODEC_XPM_ON
    if (format == XImageCodecFormat_Xpm)
        return option == XImageIOHandlerOption_Name ||
               option == XImageIOHandlerOption_Size ||
               option == XImageIOHandlerOption_ImageFormat;
#endif
    if (option == XImageIOHandlerOption_Size ||
        option == XImageIOHandlerOption_ImageFormat)
        return format != XImageCodecFormat_Unknown;
    if (option == XImageIOHandlerOption_Quality)
        return format == XImageCodecFormat_Png ||
               format == XImageCodecFormat_Jpeg;
    /* QPngHandler forwards Description, Gamma and CompressionRatio to
       libpng; the portable PNG path stores Description and accepts the
       scalar controls where the encoder can represent them. */
    if (format == XImageCodecFormat_Png &&
        (option == XImageIOHandlerOption_Description ||
         option == XImageIOHandlerOption_Gamma ||
         option == XImageIOHandlerOption_CompressionRatio))
        return true;
    return false;
}

/* 对齐 Qt 内置处理器的 Size 选项：尺寸只从设备前缀探测，不消费设备，
 * 也不触发整幅图像解码或分配。各格式处理器均把该元数据查询列为支持项。 */
static bool builtin_imageSize(XImageIOHandler* self, XSize* out)
{
#if XIMAGECODEC_ON
    XByteArray* bytes;
    XImageCodecFormat format;
    bool ok;
    if (!self || !out || !(bytes = XIODevice_peek_3(
            XImageIOHandler_device(self), 262144)))
        return false;
    format = builtin_formatFromString(XImageIOHandler_format_const(self));
    ok = XImageCodec_probeSize(
        (const uint8_t*)XByteArray_data(bytes),
        (size_t)XByteArray_size_base((const XContainer*)bytes),
        format, &out->width, &out->height);
    XByteArray_delete_base((XClass*)bytes);
    return ok;
#else
    (void)self;
    (void)out;
    return false;
#endif
}

static bool builtin_writeBytes(XIODevice* device, const XByteArray* bytes)
{
    const char* data;
    int64_t total;
    int64_t offset = 0;
    if (!device || !bytes) return false;
    data = (const char*)XByteArray_data((XByteArray*)bytes);
    total = (int64_t)XByteArray_size_base((const XContainer*)bytes);
    while (offset < total) {
        int64_t written = XIODevice_write_1(device, data + offset, total - offset);
        if (written <= 0) return false;
        offset += written;
    }
    return XIODevice_flush(device);
}

#if XIMAGECODEC_XBM_ON
/*
 * 对齐 Qt 6.8 QXbmHandler::canRead(QIODevice*)：XBM 没有固定魔数，
 * 处理器必须在保持设备位置不变的前提下解析完整的随机访问设备，
 * 并拒绝正文中的非法十六进制字节。先做尺寸/分配上限检查，避免畸形
 * 文件在能力探测阶段触发不受控的大块分配；随后复用同一解码器验证
 * 首个数据行和截断正文语义。顺序设备由调用方按 Qt 规则提前拒绝。
 */
static bool builtin_validateXbmDevice(XIODevice* device)
{
    int64_t position;
    int64_t totalSize;
    int64_t remaining;
    int width = 0;
    int height = 0;
    XSize imageSize;
    XByteArray* bytes;
    XImage image;
    bool valid;

    if (!device || XIODevice_isSequential(device))
        return false;
    position = XIODevice_pos_base(device);
    totalSize = XIODevice_size_base(device);
    if (position < 0 || totalSize <= position)
        return false;
    remaining = totalSize - position;
    bytes = XIODevice_peek_3(device, remaining);
    if (!bytes)
        return false;
    valid = XImageCodecInternal_probeXbmSize(
        (const uint8_t*)XByteArray_data(bytes),
        (size_t)XByteArray_size_base((const XContainer*)bytes),
        &width, &height);
    imageSize.width = width;
    imageSize.height = height;
    if (valid)
        valid = XImageIOHandler_checkAllocation(&imageSize,
                                                 XImageFormat_MonoLSB);
    XImage_init(&image);
    if (valid)
        valid = XImageCodecInternal_decodeXbm(
            (const uint8_t*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes), &image);
    XImage_deinit_base(&image);
    XByteArray_delete_base((XClass*)bytes);
    return valid;
}
#endif /* XIMAGECODEC_XBM_ON */

static bool VXImageBuiltinHandler_canRead(const XImageIOHandler* self)
{
    XIODevice* device;
    XImageCodecFormat format;
    XImageCodecFormat detected;
    XString* detectedName;
    const XString* requestedName;
    const char* requestedUtf8;
    bool compressedSvg = false;
    if (!self) return false;
    device = XImageIOHandler_device(self);
    if (!device) return false;
    requestedName = XImageIOHandler_format_const(self);
    requestedUtf8 = XString_toUtf8(requestedName);
    format = builtin_formatFromString(requestedName);
    if (format == XImageCodecFormat_Unknown)
        format = builtin_detectWithDevice(device);
    if (format == XImageCodecFormat_Unknown || !XImageCodec_canDecode(format))
        return false;
    /* QXbmHandler rejects sequential devices because probing requires a
       complete parse followed by seeking back to the original position. */
#if XIMAGECODEC_XBM_ON
    if (format == XImageCodecFormat_Xbm && XIODevice_isSequential(device))
        return false;
#endif
    /* Qt treats the explicit BMP format as a hint.  A readable handler still has
     * to validate the device contents before accepting unrelated bytes. */
    detected = builtin_detectWithDevice(device);
    if (format != XImageCodecFormat_Dib && detected != format) return false;
#if XIMAGECODEC_SVG_ON
    if (format == XImageCodecFormat_Svg) {
        XByteArray* svgHeader = XIODevice_peek_3(device, 2);
        const uint8_t* raw = svgHeader
            ? (const uint8_t*)XByteArray_data(svgHeader) : NULL;
        size_t rawSize = svgHeader
            ? (size_t)XByteArray_size_base((const XContainer*)svgHeader) : 0;
        compressedSvg = (requestedUtf8 && strcmp(requestedUtf8, "svgz") == 0) ||
            (raw && rawSize >= 2 && raw[0] == 0x1fu && raw[1] == 0x8bu);
        if (svgHeader) XByteArray_delete_base((XClass*)svgHeader);
    }
#endif
#if XIMAGECODEC_XBM_ON
    if (format == XImageCodecFormat_Xbm && !builtin_validateXbmDevice(device))
        return false;
#endif
    /* QImageIOHandler::canRead() is const but the Qt base class deliberately
       exposes a const setFormat() overload so a handler can publish its
       canonical format after content probing. */
    detectedName = format == XImageCodecFormat_Dib
        ? XString_create_utf8("dib") :
        (compressedSvg ? XString_create_utf8("svgz") :
         XImageCodec_formatName(detected));
    /* QPpmHandler::canRead() publishes the canonical subtype determined by
       P[1] (pbm, pgm, or ppm), even when the caller requested a raw alias. */
    if (format == XImageCodecFormat_Ppm) {
        const char* subtype = builtin_ppmSubtypeName(device);
        if (subtype) {
            XString* canonical = XString_create_utf8(subtype);
            XImageBuiltinHandler* mutableHandler =
                (XImageBuiltinHandler*)self;
            if (canonical) {
                if (detectedName)
                    XString_delete_base((XClass*)detectedName);
                detectedName = canonical;
                if (mutableHandler->m_subType)
                    XString_delete_base((XClass*)mutableHandler->m_subType);
                mutableHandler->m_subType = XString_create_copy(canonical);
            }
        }
    }
    if (detectedName) {
        XImageIOHandler_setFormat_const(self, detectedName);
        XString_delete_base((XClass*)detectedName);
    }
    return true;
}

static bool VXImageBuiltinHandler_read(XImageIOHandler* self, XImage* image)
{
    XIODevice* device;
    XImageCodecFormat format;
    XByteArray* bytes;
    bool ok = false;
    if (!self || !image || !(device = XImageIOHandler_device(self)))
        return false;
    format = builtin_formatFromString(XImageIOHandler_format_const(self));
    bytes = XIODevice_readAll_3(device);
    if (!bytes) return false;
    if (format == XImageCodecFormat_Unknown)
        format = XImageCodec_detect(
            (const uint8_t*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes));
    /* 便携编解码器在 read() 内部直接建立 XImage，无法像 Qt 的
       QImageIOHandler 子类那样调用 allocateImage()。压缩字节已经读入后，
       先从同一缓冲区取得尺寸，再按不低于 32 位的有效深度执行检查，
       随后才进入像素解码；这样不重复窥视设备，也保留顺序设备语义。 */
    if (format != XImageCodecFormat_Unknown && XImageCodec_canDecode(format)) {
        XSize imageSize;
        imageSize.width = 0;
        imageSize.height = 0;
        if (XImageCodec_probeSize(
                (const uint8_t*)XByteArray_data(bytes),
                (size_t)XByteArray_size_base((const XContainer*)bytes),
                format, &imageSize.width, &imageSize.height) &&
            !XImageIOHandler_checkAllocation(&imageSize,
                                              XImageFormat_ARGB32)) {
            XByteArray_delete_base((XClass*)bytes);
            return false;
        }
        ok = XImageCodec_decode((const uint8_t*)XByteArray_data(bytes),
                                (size_t)XByteArray_size_base((const XContainer*)bytes),
                                format, image);
    }
    XByteArray_delete_base((XClass*)bytes);
    return ok;
}

static bool VXImageBuiltinHandler_write(XImageIOHandler* self, const XImage* image)
{
    XIODevice* device;
    XImageCodecFormat format;
    const char* formatName;
#if XIMAGECODEC_XBM_ON
    const char* xbmName = "image";
#endif
    XByteArray* bytes;
    XImage described;
    bool describedInitialized = false;
    const XImage* source = image;
    int quality = -1;
    int compression = -1;
    float gamma = 0.0f;
    XImageIOHandlerOptionValue value;
    bool ok = false;
    bool encoded = false;
    if (!self || !image || !(device = XImageIOHandler_device(self)))
        return false;
    format = XImageCodec_formatFromName_2(XImageIOHandler_format_2(self));
    if (format == XImageCodecFormat_Unknown || !XImageCodec_canEncode(format))
        return false;
    formatName = XImageIOHandler_format_2(self);
    /* QImageWriter applies SubType immediately before write(); the PPM
       handler writes that selected subtype rather than the generic format
       key.  The option value is borrowed from XImageWriterPrivate and is
       valid for the duration of this call. */
    if (format == XImageCodecFormat_Ppm) {
        XImageIOHandlerOptionValue subtypeValue;
        memset(&subtypeValue, 0, sizeof(subtypeValue));
        if (XImageIOHandler_optionValue(self,
                                        XImageIOHandlerOption_SubType,
                                        &subtypeValue) &&
            subtypeValue.string &&
            !XContainer_isEmpty_base((const XContainer*)subtypeValue.string))
            formatName = XString_toUtf8(subtypeValue.string);
    }
    bytes = XByteArray_create();
    if (!bytes) return false;
    memset(&value, 0, sizeof(value));
    if (XImageIOHandler_optionValue(self, XImageIOHandlerOption_Quality, &value))
        quality = value.integer;
    memset(&value, 0, sizeof(value));
    if (XImageIOHandler_optionValue(self,
                                    XImageIOHandlerOption_CompressionRatio,
                                    &value))
        compression = value.integer;
    memset(&value, 0, sizeof(value));
    if (XImageIOHandler_optionValue(self, XImageIOHandlerOption_Gamma, &value))
        gamma = value.real;
    if (format == XImageCodecFormat_Png) {
        XImageIOHandlerOptionValue descriptionValue;
        memset(&descriptionValue, 0, sizeof(descriptionValue));
        if (XImageIOHandler_option_base(self,
                                        XImageIOHandlerOption_Description,
                                        &descriptionValue) &&
            descriptionValue.string &&
            !XContainer_isEmpty_base((const XContainer*)descriptionValue.string)) {
            XImage_init(&described);
            XCopy(&described, image);
            if (!described.m_data ||
                !XImage_applyTextDescription(&described, descriptionValue.string)) {
                XImage_deinit_base(&described);
                XByteArray_delete_base((XClass*)bytes);
                return false;
            }
            source = &described;
            describedInitialized = true;
        }
    }
#if XIMAGECODEC_XBM_ON
    memset(&value, 0, sizeof(value));
    if (format == XImageCodecFormat_Xbm &&
        XImageIOHandler_optionValue(self, XImageIOHandlerOption_Name, &value) &&
        value.string && XString_toUtf8(value.string) && XString_toUtf8(value.string)[0])
        xbmName = XString_toUtf8(value.string);
#endif
#if XIMAGECODEC_PPM_ON
    if (format == XImageCodecFormat_Ppm)
        encoded = XImageCodecInternal_encodePpmSubtype(image, formatName, bytes);
    else
#endif
#if XIMAGECODEC_XBM_ON
    if (format == XImageCodecFormat_Xbm)
        encoded = XImageCodecInternal_encodeXbmNamed(image, xbmName, bytes);
    else
#endif
#if XIMAGECODEC_XPM_ON
    if (format == XImageCodecFormat_Xpm)
        encoded = XImageCodecInternal_encodeXpmNamed(image, formatName, bytes);
    else
#endif
    if (format == XImageCodecFormat_Png)
        encoded = XImageCodecInternal_encodePngOptions(
            source, quality, compression, gamma, NULL, bytes);
    else
        encoded = XImageCodec_encode(source, format, quality, bytes);
    if (encoded)
        ok = builtin_writeBytes(device, bytes);
    XByteArray_delete_base((XClass*)bytes);
    if (describedInitialized)
        XImage_deinit_base(&described);
    return ok;
}

static void VXImageBuiltinHandler_setOption(XImageIOHandler* self,
                                             XImageIOHandlerOption option,
                                             const void* value)
{
    if (builtin_supportsOption(self, option) &&
        (option == XImageIOHandlerOption_Quality ||
         option == XImageIOHandlerOption_Name ||
         option == XImageIOHandlerOption_Gamma ||
         option == XImageIOHandlerOption_CompressionRatio))
        XImageIOHandler_storeOptionValue(self, option, value);
    else if (builtin_supportsOption(self, option) &&
             option == XImageIOHandlerOption_SubType && value) {
        XImageBuiltinHandler* handler = (XImageBuiltinHandler*)self;
        const XImageIOHandlerOptionValue* optionValue =
            (const XImageIOHandlerOptionValue*)value;
        XString* replacement = optionValue->string
            ? XString_toLower(optionValue->string) : NULL;
        /* 先复制再释放旧值，允许调用方把 option() 返回的借用字符串
           原样传回 setOption()，避免自引用时产生悬空读取。 */
        if (optionValue->string && !replacement)
            return;
        if (handler->m_subType)
            XString_delete_base((XClass*)handler->m_subType);
        handler->m_subType = replacement;
    }
    else if (builtin_supportsOption(self, option) &&
             option == XImageIOHandlerOption_Description && value) {
        XImageBuiltinHandler* handler = (XImageBuiltinHandler*)self;
        const XImageIOHandlerOptionValue* optionValue =
            (const XImageIOHandlerOptionValue*)value;
        XString* replacement = optionValue->string
            ? XString_create_copy(optionValue->string) : NULL;
        if (optionValue->string && !replacement)
            return;
        if (handler->m_description)
            XString_delete_base((XClass*)handler->m_description);
        handler->m_description = replacement;
    }
}

static bool VXImageBuiltinHandler_option(const XImageIOHandler* self,
                                         XImageIOHandlerOption option,
                                         void* out)
{
    XImageIOHandlerOptionValue* value = (XImageIOHandlerOptionValue*)out;
#if XIMAGECODEC_XBM_ON
    if (option == XImageIOHandlerOption_Name && value &&
        builtin_supportsOption(self, option)) {
        XImageIOHandlerOptionValue stored;
        memset(&stored, 0, sizeof(stored));
        if (!XImageIOHandler_optionValue(self, option, &stored)) return false;
        value->string = stored.string;
        return value->string != NULL;
    }
#endif
    if (option == XImageIOHandlerOption_Size && value)
        return builtin_imageSize((XImageIOHandler*)self, &value->size);
    if (option == XImageIOHandlerOption_Quality && value &&
        builtin_supportsOption(self, option))
        return XImageIOHandler_optionValue(self, option, value);
    if (option == XImageIOHandlerOption_Description && value &&
        builtin_supportsOption(self, option)) {
        const XImageBuiltinHandler* handler = (const XImageBuiltinHandler*)self;
        value->string = handler->m_description;
        return true;
    }
    if ((option == XImageIOHandlerOption_Gamma ||
         option == XImageIOHandlerOption_CompressionRatio) && value &&
        builtin_supportsOption(self, option))
        return XImageIOHandler_optionValue(self, option, value);
    /* Qt QGifHandler::option(Animation) returns true whenever the handler
       advertises Animation, including before the first frame is read. */
    if (option == XImageIOHandlerOption_Animation && value &&
        builtin_supportsOption(self, option)) {
        value->boolean = true;
        return true;
    }
    if (option == XImageIOHandlerOption_SubType && value &&
        builtin_supportsOption(self, option)) {
        const XImageBuiltinHandler* handler =
            (const XImageBuiltinHandler*)self;
        value->string = handler->m_subType
            ? handler->m_subType : XImageIOHandler_format_const(self);
        return value->string && !XContainer_isEmpty_base(
            (const XContainer*)value->string);
    }
    if (option == XImageIOHandlerOption_ImageTransformation && value &&
        builtin_supportsOption(self, option)) {
        value->transformation = builtin_jpegTransformation(
            XImageIOHandler_device(self));
        return true;
    }
    if (option != XImageIOHandlerOption_ImageFormat || !value)
        return false;
    value->format = builtin_imageFormat((XImageIOHandler*)self);
    return value->format != XImageFormat_Invalid;
}

static bool VXImageBuiltinHandler_supportsOption(const XImageIOHandler* self,
                                                 XImageIOHandlerOption option)
{
    return builtin_supportsOption(self, option);
}

static void VXImageBuiltinHandler_deinit(XImageIOHandler* self)
{
    XImageBuiltinHandler* handler;
    if (!self) return;
    handler = (XImageBuiltinHandler*)self;
    if (handler->m_description)
        XString_delete_base((XClass*)handler->m_description);
    if (handler->m_subType)
        XString_delete_base((XClass*)handler->m_subType);
    handler->m_description = NULL;
    handler->m_subType = NULL;
    XClass_Deinit_Parent(XImageIOHandler, self);
}

static XVtable* XImageBuiltinHandler_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XImageBuiltinHandler)
    XVTABLE_INHERIT_XCLASS(XImageIOHandler);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageBuiltinHandler_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_CanRead, VXImageBuiltinHandler_canRead);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Read, VXImageBuiltinHandler_read);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Write, VXImageBuiltinHandler_write);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Option, VXImageBuiltinHandler_option);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SetOption, VXImageBuiltinHandler_setOption);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SupportsOption, VXImageBuiltinHandler_supportsOption);
    return XVTABLE_DEFAULT;
}

static XImageBuiltinHandler* XImageBuiltinHandler_create(void)
{
    XImageBuiltinHandler* self = (XImageBuiltinHandler*)XMemory_malloc(
        sizeof(XImageBuiltinHandler), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XImageIOHandler_init(&self->m_base);
    XClassSetVtable(self, XImageBuiltinHandler);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

/* 读取 PNG 文本描述供 QImageReader::textKeys()/text() 使用。仅对可读
 * 设备执行窥探，写设备不会把已有缓冲区误当作输入；解码器内部有固定
 * 文本上限，避免元数据查询导致无界分配。 */
static void builtin_loadPngDescription(XImageBuiltinHandler* handler)
{
#if XIMAGECODEC_PNG_ON
    XIODevice* device;
    int64_t totalSize;
    XByteArray* bytes;
    XString* description;
    if (!handler || !handler->m_base.m_data ||
        XImageCodec_formatFromName_2(XString_toUtf8(
            XImageIOHandler_format_const(&handler->m_base))) != XImageCodecFormat_Png)
        return;
    device = XImageIOHandler_device(&handler->m_base);
    if (!device || !XIODevice_isReadable(device)) return;
    totalSize = XIODevice_size_base(device);
    if (totalSize <= 0 || totalSize > (int64_t)(16u * 1024u * 1024u)) return;
    bytes = XIODevice_peek_3(device, totalSize);
    if (!bytes) return;
    description = XString_create();
    if (description && XImageCodecInternal_extractPngDescription(
            (const uint8_t*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes), description) &&
        !XString_isEmpty_base((const XContainer*)description))
        handler->m_description = description;
    else if (description)
        XString_delete_base((XClass*)description);
    XByteArray_delete_base((XClass*)bytes);
#else
    (void)handler;
#endif
}

/* 读取 PNG 头部 gAMA，供 QImageReader 在 read() 前查询 Gamma。显式
 * setOption(Gamma) 会写入基类选项并在 option() 中优先返回。 */
static void builtin_loadPngGamma(XImageBuiltinHandler* handler)
{
#if XIMAGECODEC_PNG_ON
    XIODevice* device;
    int64_t totalSize;
    XByteArray* bytes;
    float gamma = 0.0f;
    XImageIOHandlerOptionValue value;
    if (!handler || !handler->m_base.m_data ||
        XImageCodec_formatFromName_2(XString_toUtf8(
            XImageIOHandler_format_const(&handler->m_base))) != XImageCodecFormat_Png)
        return;
    device = XImageIOHandler_device(&handler->m_base);
    if (!device || !XIODevice_isReadable(device) || XIODevice_isSequential(device))
        return;
    totalSize = XIODevice_size_base(device);
    if (totalSize <= 0 || totalSize > (int64_t)(16u * 1024u * 1024u)) return;
    bytes = XIODevice_peek_3(device, totalSize);
    if (!bytes) return;
    if (XImageCodecInternal_extractPngGamma(
            (const uint8_t*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes), &gamma)) {
        memset(&value, 0, sizeof(value));
        value.real = gamma;
        XImageIOHandler_storeOptionValue(&handler->m_base,
                                          XImageIOHandlerOption_Gamma, &value);
    }
    XByteArray_delete_base((XClass*)bytes);
#else
    (void)handler;
#endif
}

static uint32_t VXImageBuiltinPlugin_capabilities(const XImageIOPlugin* self,
                                                  XIODevice* device,
                                                  const XString* format)
{
    uint32_t capability = builtin_capabilityForFormat(format);
    XImageCodecFormat requested;
    XImageCodecFormat detected;
    (void)self;
    if (format && !XContainer_isEmpty_base((const XContainer*)format)) {
        if (!device) return capability;
        requested = builtin_formatFromString(format);
        if (requested == XImageCodecFormat_Unknown)
            return 0;
        detected = builtin_detectWithDevice(device);
        /* Writing does not inspect the output device.  Reading does, as
         * required by QImageIOPlugin::capabilities(). */
        if (requested != XImageCodecFormat_Dib && detected != requested)
            capability &= ~(uint32_t)XImageIOPlugin_CanRead;
        return capability;
    }
    if (!device) return capability;
    detected = builtin_detectWithDevice(device);
    return (detected != XImageCodecFormat_Unknown &&
            XImageCodec_canDecode(detected))
        ? (uint32_t)XImageIOPlugin_CanRead : 0;
}

static XImageIOHandler* VXImageBuiltinPlugin_create(const XImageIOPlugin* self,
                                                    XIODevice* device,
                                                    const XString* format)
{
    XImageBuiltinHandler* handler;
    XImageCodecFormat detected;
    XString* detectedName;
    (void)self;
    handler = XImageBuiltinHandler_create();
    if (handler) {
        XImageIOHandler_setDevice(&handler->m_base, device);
        /* QImageIOPlugin::create() must return a fully initialized handler;
           registry callers may add the same format again, but direct callers
           must also be able to write through the returned object. */
        if (format && !XContainer_isEmpty_base((const XContainer*)format)) {
            XImageIOHandler_setFormat(&handler->m_base, format);
            /* Qt's built-in QPpmHandler receives setOption(SubType,
               testFormat) at construction time, including raw aliases. */
            if (builtin_formatFromString(format) == XImageCodecFormat_Ppm)
                handler->m_subType = XString_toLower(format);
        } else if (device) {
            /* Qt's content-probe path returns a handler whose format() is the
               format accepted by canRead().  Keep that result visible to
               QImageReader::imageFormat() even when SVG/XML has a long
               prologue that is not contained in the caller's short peek. */
            detected = builtin_detectWithDevice(device);
            if (detected != XImageCodecFormat_Unknown) {
                detectedName = XImageCodec_formatName(detected);
                if (detected == XImageCodecFormat_Svg) {
                    XByteArray* svgHeader = XIODevice_peek_3(device, 2);
                    const uint8_t* raw = svgHeader
                        ? (const uint8_t*)XByteArray_data(svgHeader) : NULL;
                    size_t rawSize = svgHeader
                        ? (size_t)XByteArray_size_base((const XContainer*)svgHeader) : 0;
                    if (raw && rawSize >= 2 && raw[0] == 0x1fu && raw[1] == 0x8bu) {
                        XString* compressedName = XString_create_utf8("svgz");
                        if (compressedName) {
                            if (detectedName)
                                XString_delete_base((XClass*)detectedName);
                            detectedName = compressedName;
                        }
                    }
                    if (svgHeader) XByteArray_delete_base((XClass*)svgHeader);
                }
                if (detected == XImageCodecFormat_Ppm) {
                    XByteArray* head = XIODevice_peek_3(device, 2);
                    const uint8_t* raw = head
                        ? (const uint8_t*)XByteArray_data(head) : NULL;
                    size_t rawSize = head
                        ? (size_t)XByteArray_size_base((const XContainer*)head) : 0;
                    if (raw && rawSize >= 2) {
                        const char* subtype = (raw[1] == '1' || raw[1] == '4')
                            ? "pbm" : ((raw[1] == '2' || raw[1] == '5')
                                ? "pgm" : "ppm");
                        XString* subtypeString = XString_create_utf8(subtype);
                        if (subtypeString) {
                            if (detectedName)
                                XString_delete_base((XClass*)detectedName);
                            detectedName = subtypeString;
                        }
                    }
                    if (head) XByteArray_delete_base((XClass*)head);
                }
                if (detectedName) {
                    XImageIOHandler_setFormat(&handler->m_base, detectedName);
                    if (detected == XImageCodecFormat_Ppm) {
                        if (handler->m_subType)
                            XString_delete_base((XClass*)handler->m_subType);
                        handler->m_subType = XString_create_copy(detectedName);
                    }
                    XString_delete_base((XClass*)detectedName);
                }
            }
        }
        builtin_loadPngDescription(handler);
        builtin_loadPngGamma(handler);
    }
    return (XImageIOHandler*)handler;
}

static XStringList* VXImageBuiltinPlugin_keys(const XImageIOPlugin* self)
{
    return self ? ((XImageBuiltinPlugin*)self)->m_keys : NULL;
}

static XStringList* VXImageBuiltinPlugin_mimeTypes(const XImageIOPlugin* self)
{
    return self ? ((XImageBuiltinPlugin*)self)->m_mimes : NULL;
}

static XStringList* VXImageBuiltinPlugin_nameFilters(const XImageIOPlugin* self)
{
    return self ? ((XImageBuiltinPlugin*)self)->m_filters : NULL;
}

static void VXImageBuiltinPlugin_deinit(XImageIOPlugin* self)
{
    XImageBuiltinPlugin* plugin;
    if (!self) return;
    plugin = (XImageBuiltinPlugin*)self;
    if (plugin->m_keys) XStringList_delete_base((XClass*)plugin->m_keys);
    if (plugin->m_mimes) XStringList_delete_base((XClass*)plugin->m_mimes);
    if (plugin->m_filters) XStringList_delete_base((XClass*)plugin->m_filters);
    plugin->m_keys = NULL;
    plugin->m_mimes = NULL;
    plugin->m_filters = NULL;
    XClass_Deinit_Parent(XImageIOPlugin, self);
}

static XVtable* XImageBuiltinPlugin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XImageBuiltinPlugin)
    XVTABLE_INHERIT_XCLASS(XImageIOPlugin);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageBuiltinPlugin_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Capabilities, VXImageBuiltinPlugin_capabilities);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Create, VXImageBuiltinPlugin_create);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Keys, VXImageBuiltinPlugin_keys);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_MimeTypes, VXImageBuiltinPlugin_mimeTypes);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_NameFilters, VXImageBuiltinPlugin_nameFilters);
    return XVTABLE_DEFAULT;
}

static XImageBuiltinPlugin g_builtinPlugin;

static XImageBuiltinPlugin* XImageBuiltinPlugin_ensure(void)
{
    int64_t i;
    if (g_builtinPlugin.m_keys) return &g_builtinPlugin;
    memset(&g_builtinPlugin, 0, sizeof(g_builtinPlugin));
    XImageIOPlugin_init(&g_builtinPlugin.m_base);
    g_builtinPlugin.m_keys = XStringList_create();
    g_builtinPlugin.m_mimes = XStringList_create();
    g_builtinPlugin.m_filters = XStringList_create();
    if (!g_builtinPlugin.m_keys || !g_builtinPlugin.m_mimes ||
        !g_builtinPlugin.m_filters) {
        if (g_builtinPlugin.m_keys)
            XStringList_delete_base((XClass*)g_builtinPlugin.m_keys);
        if (g_builtinPlugin.m_mimes)
            XStringList_delete_base((XClass*)g_builtinPlugin.m_mimes);
        if (g_builtinPlugin.m_filters)
            XStringList_delete_base((XClass*)g_builtinPlugin.m_filters);
        memset(&g_builtinPlugin, 0, sizeof(g_builtinPlugin));
        return NULL;
    }
    /* 三个元数据数组必须保持一一对应；使用数组长度避免新增别名时
       静默遗漏末尾格式。 */
    for (i = 0; i < (int64_t)(sizeof(g_builtinFormats) /
                              sizeof(g_builtinFormats[0])); ++i) {
        XStringList_push_back_utf8(g_builtinPlugin.m_keys, g_builtinFormats[i]);
        XStringList_push_back_utf8(g_builtinPlugin.m_mimes, g_builtinMimeTypes[i]);
        XStringList_push_back_utf8(g_builtinPlugin.m_filters, g_builtinNameFilters[i]);
    }
    XClassSetVtable(&g_builtinPlugin, XImageBuiltinPlugin);
    return &g_builtinPlugin;
}

#endif /* XIMAGECODEC_ON */

XImageIOPlugin* XImageBuiltinPlugin_instance(void)
{
#if XIMAGECODEC_ON
    return (XImageIOPlugin*)XImageBuiltinPlugin_ensure();
#else
    return NULL;
#endif
}

#else /* XIMAGEIOPLUGIN_ON */

XImageIOPlugin* XImageBuiltinPlugin_instance(void)
{
    return NULL;
}

#endif /* XIMAGEIOPLUGIN_ON */
