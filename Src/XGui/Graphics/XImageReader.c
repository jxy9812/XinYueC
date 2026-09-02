/******************************************************************************
 * @file       XImageReader.c
 * @brief      XImageReader 图像读取器实现（对标 Qt 6.8 QImageReader）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImageReader.h"
#include "XImageCodec.h"
#include "XImagePluginRegistry.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XVector.h"
#include "XStringList.h"
#include "XFile.h"
#include <string.h>

/* C 字符串兼容重载没有调用方缓冲区；上限覆盖 Qt 插件常见格式键，
 * 同时避免原先 16 字节缓存对合法长键的静默截断。 */
#define XIMAGE_READER_FORMAT_BUFFER_SIZE 256
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>

static int g_imageReaderAllocationLimitMb = 256;
static bool g_imageReaderAllocationEnvResolved = false;
static int g_imageReaderAllocationEnvLimitMb = -1;

/*
 * Qt 6.8 仅在首次读取 allocationLimit() 时解析 QT_IMAGEIO_MAXALLOC；
 * 解析采用 base=0 的整数规则（含 0x/0b 前缀和首尾空白），非法、溢出或负值（除 -0）
 * 均视为未设置，随后使用运行时 setter 保存的值。
 */
static bool XImageReader_asciiSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f' || value == '\v';
}

static int XImageReader_digitValue(char value)
{
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'z') return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'Z') return (int)(value - 'A') + 10;
    return -1;
}

static bool XImageReader_parseAllocationEnvironment(const char* text, int* value)
{
    const char* cursor;
    const char* end;
    unsigned long long magnitude = 0;
    unsigned long long limit;
    int base = 0;
    bool negative = false;
    bool hasDigit = false;

    size_t maxLength;
    unsigned int bitCount = 0;
    unsigned int bitProbe = UINT_MAX;

    /* qEnvironmentVariableIntValue() 先按
       (numeric_limits<uint>::digits + 2) / 3 + sign + "0" 前缀
       限制字符串长度（qtenvironmentvariables.cpp:196-222）。用 UINT_MAX
       计算 value bits，避免把 32 位实现的魔数误用于其它 C99 平台。 */
    do {
        ++bitCount;
        bitProbe >>= 1;
    } while (bitProbe != 0);
    maxLength = (size_t)((bitCount + 2u) / 3u + 2u);
    if (!text || !value || strlen(text) > maxLength) return false;
    cursor = text;
    end = text + strlen(text);
    while (cursor < end && XImageReader_asciiSpace(*cursor)) ++cursor;
    if (cursor < end && (*cursor == '+' || *cursor == '-')) {
        negative = *cursor == '-';
        ++cursor;
    }
    if (cursor >= end) return false;

    if (*cursor == '0') {
        if (cursor + 1 < end && (cursor[1] == 'b' || cursor[1] == 'B')) {
            base = 2;
            cursor += 2;
        } else if (cursor + 1 < end && (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        } else {
            base = 8;
        }
    } else {
        base = 10;
    }
    limit = negative ? (unsigned long long)INT_MAX + 1ULL
                     : (unsigned long long)INT_MAX;
    while (cursor < end) {
        int digit = XImageReader_digitValue(*cursor);
        if (digit < 0 || digit >= base) break;
        hasDigit = true;
        if (magnitude > (limit - (unsigned)digit) / (unsigned)base)
            return false;
        magnitude = magnitude * (unsigned)base + (unsigned)digit;
        ++cursor;
    }
    if (!hasDigit) return false;
    while (cursor < end && XImageReader_asciiSpace(*cursor)) ++cursor;
    /* Qt 的有符号转换允许 -0，其他负值才会被 allocationLimit() 忽略。 */
    if (cursor != end || (negative && magnitude != 0)) return false;
    /* Casting INT_MAX + 1 to int is implementation-defined in C.  Qt's
       integer helper accepts INT_MIN, so spell out that endpoint and keep
       the remaining negation within the representable range. */
    if (negative) {
        if (magnitude == (unsigned long long)INT_MAX + 1ULL)
            *value = INT_MIN;
        else
            *value = -(int)magnitude;
    } else {
        *value = (int)magnitude;
    }
    return true;
}

static int XImageReader_effectiveAllocationLimit(void)
{
    if (!g_imageReaderAllocationEnvResolved)
    {
        const char* text = getenv("QT_IMAGEIO_MAXALLOC");
        int parsed = -1;
        if (XImageReader_parseAllocationEnvironment(text, &parsed))
            g_imageReaderAllocationEnvLimitMb = parsed;
        g_imageReaderAllocationEnvResolved = true;
    }
    return g_imageReaderAllocationEnvLimitMb >= 0
        ? g_imageReaderAllocationEnvLimitMb : g_imageReaderAllocationLimitMb;
}

/* Keep discovery aligned with the independent XImageCodec registry. */
/* Qt JPEG 插件公开 jpg、jpeg、jfif 三个格式键；保持读取器在插件
   裁剪关闭时也能发现同一组别名，MIME 列表随后按值去重。 */
static const char* const g_imageReaderFormats[] =
    { "bmp", "png", "jpg", "jpeg", "jfif", "gif",
      "pbm", "pgm", "ppm", "xbm", "xpm", "svg",
      "svgz",
#if XIMAGECODEC_ICO_ON
      "ico", "cur",
#endif
    };
static const char* const g_imageReaderMimeTypes[] =
    { "image/bmp", "image/png", "image/jpeg", "image/jpeg", "image/jpeg",
      "image/gif", "image/x-portable-bitmap", "image/x-portable-graymap",
      "image/x-portable-pixmap",
      "image/x-xbitmap", "image/x-xpixmap", "image/svg+xml",
      "image/svg+xml-compressed",
#if XIMAGECODEC_ICO_ON
      "image/vnd.microsoft.icon", "image/vnd.microsoft.icon",
#endif
    };

static XStringList* XImageReader_makeStringList(const char* const* values, size_t count)
{
    XStringList* result = XStringList_create();
    if (!result) return NULL;
    for (size_t i = 0; i < count; ++i) {
        XStringList_push_back_utf8(result, values[i]);
    }
    return result;
}

static void XImageReader_makeStringList_prefix(XStringList* result, const char* const* values, size_t count)
{
    size_t i;
    if (!result || !values) return;
    for (i = 0; i < count; ++i) {
        if (values[i] && values[i][0] &&
            !XStringList_contains_utf8(result, values[i], XChar_CaseSensitive))
            XStringList_push_back_utf8(result, values[i]);
    }
}

static XStringList* XImageReader_supportedFormats(void)
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result) return NULL;
#if XIMAGECODEC_ON
    for (i = 0; i < sizeof(g_imageReaderFormats) / sizeof(g_imageReaderFormats[0]); ++i)
        if (XImageCodec_canDecode(XImageCodec_formatFromName_2(g_imageReaderFormats[i])))
            XStringList_push_back_utf8(result, g_imageReaderFormats[i]);
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_supportedImageFormats(true);
        int64_t n;
        int64_t j;
        if (plugin) {
            n = XStringList_size_base((XContainer*)plugin);
            for (j = 0; j < n; ++j) {
                XString* item = (XString*)XStringList_at_base((XVector*)plugin, j);
                const char* value = XString_toUtf8(item);
                if (value && value[0] &&
                    !XStringList_contains_utf8(result, value, XChar_CaseSensitive))
                    XStringList_push_back_utf8(result, value);
            }
            XStringList_delete_base((XClass*)plugin);
        }
    }
#endif
    /* Qt QImageReader::supportedImageFormats() sorts and removes duplicates. */
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList_removeDuplicates(result);
    return result;
}

static bool XImageReader_mimeEquals(const char* mimeType, const char* expected)
{
    /* Qt QImageReaderWriterHelpers 按 QByteArray 值精确匹配 MIME；
       仅格式名本身按大小写不敏感处理。 */
    return mimeType && expected && strcmp(mimeType, expected) == 0;
}

static bool XImageReader_mimeIsBmp(const char* mimeType)
{
    return XImageReader_mimeEquals(mimeType, "image/bmp");
}

static const char* XImageReader_detectSignature(const unsigned char* data, size_t size)
{
#if XIMAGECODEC_ON
    XImageCodecFormat format = XImageCodec_detect(data, size);
    if (format == XImageCodecFormat_Ppm && size >= 2) {
        if (data[1] == '1' || data[1] == '4') return "pbm";
        if (data[1] == '2' || data[1] == '5') return "pgm";
        return "ppm";
    }
#if XIMAGECODEC_SVG_ON
    /* Qt QSvgIOHandler distinguishes gzip-wrapped SVG (svgz) from plain SVG
       although both share the same internal renderer. */
    if (format == XImageCodecFormat_Svg && size >= 2 &&
        data[0] == 0x1fu && data[1] == 0x8bu)
        return "svgz";
#endif
    return XImageCodec_formatName_2(format);
#else
    (void)data;
    (void)size;
    return NULL;
#endif
}

static bool XImageReader_isSupportedFormat(const char* format)
{
    if (!format || !format[0]) return true;
#if XIMAGECODEC_ON
    if (XImageCodec_canDecode(XImageCodec_formatFromName_2(format))) return true;
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XString* value = XString_create_utf8(format);
        bool supported = value && XImagePluginRegistry_supportsReadFormat(value);
        if (value) XString_delete_base((XClass*)value);
        if (supported) return true;
    }
#else
    (void)format;
#endif
    return false;
}

/* 裁剪掉插件发现后，canRead() 仍须让显式格式处理器确认设备内容。
 * 这里用独立 codec 枚举比较格式别名（如 jpg/jpeg、pbm/pgm/ppm），
 * 与 Qt QImageIOHandler::canRead() 的“格式与签名同时满足”语义一致。 */
static bool XImageReader_explicitFormatMatchesContent(
    const XString* requested, const char* detected)
{
    if (!requested || XContainer_isEmpty_base((const XContainer*)requested))
        return true;
    if (!detected || !detected[0]) return false;
#if XIMAGECODEC_ON
    {
        XImageCodecFormat expected = XImageCodec_formatFromName(requested);
        XImageCodecFormat actual = XImageCodec_formatFromName_2(detected);
        return expected != XImageCodecFormat_Unknown && expected == actual;
    }
#else
    /* 无 codec 时 imageFormatDevice() 不会返回有效签名；保留字符串
       比较仅用于防止未来裁剪实现提供签名时破坏显式格式语义。 */
    {
        const char* value = XString_toUtf8(requested);
        return value && strcmp(value, detected) == 0;
    }
#endif
}

/* SVG 在插件裁剪构建中保留直接 codec 读取能力，但静态
 * imageFormatDevice() 按 Qt 处理器边界返回空字符串。对显式格式的
 * canRead() 需要再做一次有界窥视，避免把该合法路径误判为不可读。 */
static bool XImageReader_explicitFormatMatchesDevice(
    const XString* requested, XIODevice* device)
{
#if XIMAGECODEC_ON
    XByteArray* bytes;
    XImageCodecFormat expected;
    XImageCodecFormat actual;
    if (!requested || !device) return false;
    bytes = XIODevice_peek_3(device, 4096);
    if (!bytes) return false;
    expected = XImageCodec_formatFromName(requested);
    actual = XImageCodec_detect(
        (const uint8_t*)XByteArray_data(bytes),
        (size_t)XByteArray_size_base((const XContainer*)bytes));
    XByteArray_delete_base((XClass*)bytes);
    return expected != XImageCodecFormat_Unknown && expected == actual;
#else
    (void)requested;
    (void)device;
    return false;
#endif
}

static XImageIOHandler* XImageReader_ensureHandler(XImageReader* self);

/**
 * @brief      XImageReader 私有数据
 */
typedef struct XImageReaderPrivate
{
    XIODevice*  m_device;            /**< IO 设备 */
    XString*    m_fileName;          /**< 文件名（UTF-8） */
    XString*    m_format;            /**< 格式字符串（UTF-8） */
    bool        m_autoDetectFormat;  /**< 是否自动检测格式 */
    bool        m_decideFromContent; /**< 是否从内容决定格式 */
    int         m_clipX;             /**< 裁剪矩形 X */
    int         m_clipY;             /**< 裁剪矩形 Y */
    int         m_clipW;             /**< 裁剪矩形宽度 */
    int         m_clipH;             /**< 裁剪矩形高度 */
    bool        m_hasClipRect;       /**< 是否有裁剪矩形 */
    int         m_scaledW;           /**< 缩放宽度 */
    int         m_scaledH;           /**< 缩放高度 */
    bool        m_hasScaledSize;     /**< 是否有缩放尺寸 */
    int         m_quality;           /**< 质量参数 */
    int         m_scaledClipX;       /**< 缩放裁剪矩形 X */
    int         m_scaledClipY;       /**< 缩放裁剪矩形 Y */
    int         m_scaledClipW;       /**< 缩放裁剪矩形宽度 */
    int         m_scaledClipH;       /**< 缩放裁剪矩形高度 */
    bool        m_hasScaledClipRect; /**< 是否有缩放裁剪矩形 */
    bool        m_autoTransform;     /**< 是否自动变换 */
    XImageReaderError m_error;       /**< 错误码 */
    XString*    m_errorString;       /**< 错误描述 */
    XImageIOHandler* m_handler;      /**< 内部 IO 处理器 */
    XIODevice*      m_fileDevice;   /**< 内部文件设备；由读取器拥有，供插件处理器使用 */
    XString*        m_detectedFormat; /**< 自动探测到的格式缓存；由读取器拥有 */
    XStringList     m_textKeys;      /**< 处理器描述解析出的文本键列表 */
    XStringList     m_textValues;    /**< 与文本键按索引对应的文本值列表 */
    XString         m_textCache;     /**< UTF-8 兼容重载的最近一次文本缓存 */
    bool            m_textLoaded;    /**< 是否已尝试从处理器读取描述文本 */
    int         m_sizeW;             /**< 已探测图像宽度 */
    int         m_sizeH;             /**< 已探测图像高度 */
    bool        m_hasSize;           /**< 是否已探测到图像尺寸 */
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    XImageCodecAnimation* m_animation; /**< GIF 动画缓存；由读取器拥有。 */
    int         m_currentImageNumber; /**< 当前帧编号。 */
    bool        m_imageJumpPending; /**< 是否已定位到下一次 read() 的目标帧。 */
    XByteArray* m_sourceBytes;        /**< 设备数据缓存，避免多次消费设备。 */
#endif
}XImageReaderPrivate;

static void XImageReader_clearText(XImageReaderPrivate* data)
{
    if (!data) return;
    XStringList_clear_base((XContainer*)&data->m_textKeys);
    XStringList_clear_base((XContainer*)&data->m_textValues);
    data->m_textLoaded = false;
}

static void XImageReader_clearDetectedFormat(XImageReaderPrivate* data)
{
    if (!data) return;
    if (data->m_detectedFormat)
        XString_delete_base((XClass*)data->m_detectedFormat);
    data->m_detectedFormat = NULL;
}

/* 将 Description 中的一个键值写入有序列表；重复键按 QMap 规则覆盖旧值。 */
static void XImageReader_storeText(XImageReaderPrivate* data,
                                   const XString* key,
                                   const XString* value)
{
    int count;
    int insertIndex = 0;
    int i;
    if (!data || !key || !value ||
        XString_isEmpty_base((const XContainer*)key))
        return;
    count = (int)XStringList_size_base((const XContainer*)&data->m_textKeys);
    for (i = 0; i < count; ++i) {
        XString* item = (XString*)XStringList_at_base(
            (XVector*)&data->m_textKeys, i);
        if (!item) continue;
        if (XString_equals(item, key, XChar_CaseSensitive)) {
            XString* target = (XString*)XStringList_at_base(
                (XVector*)&data->m_textValues, i);
            if (target)
                XString_copy_base((XClass*)target, (const XClass*)value);
            return;
        }
        if (XString_compare(item, key) > 0) {
            insertIndex = i;
            break;
        }
        insertIndex = i + 1;
    }
    if (!XStringList_insert_base((XVector*)&data->m_textKeys,
                                 insertIndex, (void*)key))
        return;
    if (!XStringList_insert_base((XVector*)&data->m_textValues,
                                 insertIndex, (void*)value)) {
        XStringList_remove_base((XVector*)&data->m_textKeys,
                                insertIndex, 1);
    }
}

/* 对齐 qt_getImageTextFromDescription：键值由空行分隔，值采用 simplified；
   普通键保留原始空白，只用 trimmed 结果判断是否为空。 */
static void XImageReader_loadText(XImageReader* self)
{
    XImageReaderPrivate* data;
    XImageIOHandlerOptionValue optionValue;
    XString* description;
    XStringList* pairs;
    int64_t count;
    int64_t i;
    if (!self || !(data = self->m_data) || data->m_textLoaded)
        return;
    data->m_textLoaded = true;
    if (!XImageReader_ensureHandler(self))
        return;
    if (!XImageIOHandler_supportsOption_base(data->m_handler,
                                             XImageIOHandlerOption_Description))
        return;
    memset(&optionValue, 0, sizeof(optionValue));
    if (!XImageIOHandler_option_base(data->m_handler,
                                     XImageIOHandlerOption_Description,
                                     &optionValue) || !optionValue.string)
        return;
    description = XString_create_copy(optionValue.string);
    if (!description)
        return;
    pairs = XString_split_utf8(description, "\n\n", XChar_CaseSensitive);
    XString_delete_base((XClass*)description);
    if (!pairs)
        return;
    count = (int64_t)XStringList_size_base((const XContainer*)pairs);
    for (i = 0; i < count; ++i) {
        XString* pair = (XString*)XStringList_at_base((XVector*)pairs, i);
        XString* simplified;
        int64_t colonIndex;
        int64_t spaceIndex;
        XString* key = NULL;
        XString* value = NULL;
        if (!pair) continue;
        simplified = XString_simplified(pair);
        if (!simplified || XString_isEmpty_base((const XContainer*)simplified)) {
            if (simplified) XString_delete_base((XClass*)simplified);
            continue;
        }
        /* QStringView::indexOf() 以 UTF-16 代码单元计数；不能用 UTF-8
           字节偏移切割，否则非 ASCII 键会在冒号处产生错误边界。 */
        colonIndex = XString_indexOf_char(pair, (XChar)':', 0,
                                          XChar_CaseSensitive);
        spaceIndex = XString_indexOf_char(pair, (XChar)' ', 0,
                                          XChar_CaseSensitive);
        /* Qt 的 QStringView::indexOf(u' ') 在未找到时返回 -1，
           因此“没有空格”同样满足 indexOf(' ') < 冒号位置。 */
        if (colonIndex >= 0 && spaceIndex < colonIndex) {
            key = XString_create_utf8("Description");
            value = simplified;
            simplified = NULL;
        } else {
            /* Qt 的 left(-1) 返回整个字符串；因此无冒号的非空段也
               保留为键，并使用 mid(1) 作为对应值。 */
            key = XString_left(pair, colonIndex >= 0
                                      ? (size_t)colonIndex : SIZE_MAX);
            if (key) {
                XString* keyCheck = XString_simplified(key);
                if (!keyCheck || XString_isEmpty_base((const XContainer*)keyCheck)) {
                    if (keyCheck) XString_delete_base((XClass*)keyCheck);
                    XString_delete_base((XClass*)key);
                    key = NULL;
                } else {
                    XString_delete_base((XClass*)keyCheck);
                }
            }
            /* qt_getImageTextFromDescription 使用 pair.mid(index + 2)，
               即冒号及其后一个字符一起跳过，再执行 simplified；无冒号
               时 index 为 -1，等价于从第二个 UTF-16 字符开始。 */
            {
                size_t valuePos = colonIndex >= 0
                    ? (size_t)colonIndex + 2u : 1u;
                size_t pairLength = XString_length_base(
                    (const XContainer*)pair);
                value = XString_mid(pair, valuePos,
                                    valuePos < pairLength
                                        ? pairLength - valuePos : 0u);
            }
            if (value) {
                XString* normalized = XString_simplified(value);
                XString_delete_base((XClass*)value);
                value = normalized;
            }
        }
        if (key && value)
            XImageReader_storeText(data, key, value);
        if (key) XString_delete_base((XClass*)key);
        if (value) XString_delete_base((XClass*)value);
        if (simplified) XString_delete_base((XClass*)simplified);
    }
    XStringList_delete_base((XClass*)pairs);
}

static uint32_t XImageReader_readLe32(const unsigned char* data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t XImageReader_readLe16(const unsigned char* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t XImageReader_readBe32(const unsigned char* data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool XImageReader_probeBmpSize(const unsigned char* data, size_t size,
                                      int* width, int* height)
{
    int32_t signedWidth;
    int32_t signedHeight;
    if (!data || size < 26 || data[0] != 'B' || data[1] != 'M' || !width || !height)
        return false;
    signedWidth = (int32_t)XImageReader_readLe32(data + 18);
    signedHeight = (int32_t)XImageReader_readLe32(data + 22);
    if (signedWidth <= 0 || signedHeight == 0 || signedWidth > INT_MAX)
        return false;
    if (signedHeight == INT32_MIN)
        return false;
    signedHeight = signedHeight < 0 ? -signedHeight : signedHeight;
    if (signedHeight > INT_MAX)
        return false;
    *width = (int)signedWidth;
    *height = (int)signedHeight;
    return true;
}

static bool XImageReader_probeSize(XImageReader* self)
{
    enum { XIMAGE_READER_PROBE_MAX = 262144 };
    unsigned char header[XIMAGE_READER_PROBE_MAX];
    size_t size = 0;
    XFile* fileObject;
    XByteArray* bytes;
    if (!self || !self->m_data) return false;
    if (self->m_data->m_hasSize) return true;
    if (self->m_data->m_sourceBytes) {
        size = XByteArray_size_base((const XContainer*)self->m_data->m_sourceBytes);
        if (size > sizeof(header)) size = sizeof(header);
        memcpy(header, XByteArray_data(self->m_data->m_sourceBytes), size);
    } else if (self->m_data->m_fileName) {
        fileObject = XFile_create_2(self->m_data->m_fileName);
        if (!fileObject || !XIODevice_open_base((XIODevice*)fileObject, XIODevice_ReadOnly)) { if (fileObject) XClass_delete_base((XClass*)fileObject); return false; }
        bytes = XIODevice_readAll_3((XIODevice*)fileObject); XIODevice_close_base((XIODevice*)fileObject); XClass_delete_base((XClass*)fileObject);
        if (!bytes) return false; size = XByteArray_size_base((const XContainer*)bytes); if (size > sizeof(header)) size = sizeof(header); if (size) memcpy(header, XByteArray_data(bytes), size); XByteArray_delete_base((XClass*)bytes);
    } else if (self->m_data->m_device) {
        bytes = XIODevice_peek_3(self->m_data->m_device, (int64_t)sizeof(header));
        if (!bytes) return false;
        size = (size_t)XByteArray_size_base((const XContainer*)bytes);
        if (size > sizeof(header)) size = sizeof(header);
        if (size) memcpy(header, XByteArray_data(bytes), size);
        XByteArray_delete_base((XClass*)bytes);
    } else {
        return false;
    }
#if XIMAGECODEC_ON
    if (size && !XImageCodec_probeSize(header, size, XImageCodecFormat_Unknown,
                                       &self->m_data->m_sizeW,
                                       &self->m_data->m_sizeH))
        return false;
#else
    if (!XImageReader_probeBmpSize(header, size, &self->m_data->m_sizeW, &self->m_data->m_sizeH)) return false;
#endif
    self->m_data->m_hasSize = true;
    return true;
}

static void XImageReader_setError(XImageReader* self, XImageReaderError error, const char* message)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_errorString) XString_delete_base((XClass*)self->m_data->m_errorString);
    self->m_data->m_errorString = NULL;
    self->m_data->m_error = error;
    if (message) self->m_data->m_errorString = XString_create_utf8(message);
}

static void XImageReader_releaseHandler(XImageReaderPrivate* data)
{
    if (!data) return;
    if (data->m_handler) XImageIOHandler_delete_base(data->m_handler);
    data->m_handler = NULL;
    /* 文件名构造的设备归读取器所有；重建处理器时只关闭并复用它。 */
    if (data->m_fileDevice && XIODevice_isOpen(data->m_fileDevice))
        XIODevice_close_base(data->m_fileDevice);
    XImageReader_clearDetectedFormat(data);
    XImageReader_clearText(data);
}

static void XImageReader_releaseOwnedDevice(XImageReaderPrivate* data)
{
    if (!data || !data->m_fileDevice) return;
    if (XIODevice_isOpen(data->m_fileDevice))
        XIODevice_close_base(data->m_fileDevice);
    if (data->m_device == data->m_fileDevice)
        data->m_device = NULL;
    XClass_delete_base((XClass*)data->m_fileDevice);
    data->m_fileDevice = NULL;
}

/*
 * QFileInfo::suffix() 对应的轻量级文件名后缀提取。后缀仅用于自动格式
 * 探测；显式 setFormat() 或 decideFormatFromContent() 时由调用方忽略。
 */
static XString* XImageReader_fileSuffix(const XString* fileName)
{
    const char* utf8;
    const char* base;
    const char* dot;
    XString* suffix;
    XString* lower;
    if (!fileName || XContainer_isEmpty_base((const XContainer*)fileName))
        return NULL;
    utf8 = XString_toUtf8(fileName);
    if (!utf8) return NULL;
    /* QFileInfo::suffix() only examines the final path component.  A dot in
       a parent directory must never become the image format suffix. */
    base = strrchr(utf8, '/');
    {
        const char* backslash = strrchr(utf8, '\\');
        if (backslash && (!base || backslash > base)) base = backslash;
    }
    base = base ? base + 1 : utf8;
    dot = strrchr(base, '.');
    if (!dot || !dot[1]) return NULL;
    suffix = XString_create_utf8(dot + 1);
    if (!suffix) return NULL;
    lower = XString_toLower(suffix);
    XString_delete_base((XClass*)suffix);
    return lower;
}

/*
 * 对齐 Qt QImageReader::read() 成功后的高 DPI 文件名约定：
 * QFileInfo(fileName).baseName().right(3) 为 @2x..@9x 时设置 DPR。
 * 环境变量 QT_HIGHDPI_DISABLE_2X_IMAGE_LOADING 非空时禁用该行为，
 * 与 Qt 的一次性 disableNxImageLoading 初始化语义一致。
 */
static float XImageReader_fileDevicePixelRatio(const XString* fileName)
{
    const char* utf8;
    const char* base;
    const char* dot;
    size_t length;
    static int disableNxImageLoading = -1;
    if (!fileName || XContainer_isEmpty_base((const XContainer*)fileName))
        return 1.0f;
    /* Qt uses a function-local static initialized once, so later environment
       changes do not alter the reader behavior in the same process. */
    if (disableNxImageLoading < 0) {
        const char* disable = getenv("QT_HIGHDPI_DISABLE_2X_IMAGE_LOADING");
        disableNxImageLoading = disable && disable[0] ? 1 : 0;
    }
    if (disableNxImageLoading) return 0.0f;
    utf8 = XString_toUtf8(fileName);
    if (!utf8) return 0.0f;
    base = strrchr(utf8, '/');
    {
        const char* backslash = strrchr(utf8, '\\');
        if (backslash && (!base || backslash > base)) base = backslash;
    }
    base = base ? base + 1 : utf8;
    dot = strrchr(base, '.');
    length = dot ? (size_t)(dot - base) : strlen(base);
    if (length < 3 || base[length - 3] != '@' ||
        base[length - 1] != 'x' || base[length - 2] < '2' ||
        base[length - 2] > '9')
        return 0.0f;
    return (float)(base[length - 2] - '0');
}

static XIODevice* XImageReader_ensureOwnedDevice(XImageReaderPrivate* data)
{
    XFile* file;
    if (!data || !data->m_fileName ||
        XContainer_isEmpty_base((const XContainer*)data->m_fileName))
        return NULL;
    if (data->m_fileDevice) return data->m_fileDevice;
    file = XFile_create_2(data->m_fileName);
    if (!file) return NULL;
    data->m_fileDevice = (XIODevice*)file;
    data->m_device = data->m_fileDevice;
    return data->m_fileDevice;
}

/*
 * 对齐 QImageReaderPrivate::initHandler() 的文件扩展名回退：当
 * setFileName() 对应的原路径打不开且启用自动探测时，按支持格式逐个
 * 尝试“原路径.扩展名”。成功后 QFile/fileName() 应暴露实际打开的路径，
 * 读取器内部配置的文件名也同步为该路径。列表由项目固定容量注册表
 * 生成，所有临时字符串均走 XString 项目内存接口。
 */
static bool XImageReader_tryDefaultExtensions(XImageReaderPrivate* data)
{
    XFile* file;
    XStringList* extensions;
    XString* original;
    int64_t count;
    int64_t preferred = -1;
    int64_t pass;
    if (!data || !data->m_autoDetectFormat ||
        !(file = (XFile*)data->m_fileDevice) ||
        !data->m_fileName || XContainer_isEmpty_base((const XContainer*)data->m_fileName))
        return false;
    extensions = XImageReader_supportedFormats();
    if (!extensions) return false;
    original = XString_create_copy(data->m_fileName);
    if (!original) {
        XStringList_delete_base((XClass*)extensions);
        return false;
    }
    count = (int64_t)XStringList_size_base((const XContainer*)extensions);
    /* Qt 将显式格式对应的后缀换到首位，以减少无谓的文件探测。 */
    if (data->m_format &&
        !XContainer_isEmpty_base((const XContainer*)data->m_format)) {
        for (pass = 0; pass < count; ++pass) {
            XString* item = (XString*)XStringList_at_base(
                (XVector*)extensions, pass);
            if (item && XString_equals(item, data->m_format,
                                       XChar_CaseInsensitive)) {
                preferred = pass;
                break;
            }
        }
    }
    for (pass = 0; pass < count; ++pass) {
        int64_t index = preferred >= 0
            ? (pass == 0 ? preferred : (pass <= preferred ? pass - 1 : pass))
            : pass;
        XString* extension;
        XString* candidate;
        extension = (XString*)XStringList_at_base((XVector*)extensions, index);
        if (!extension || XContainer_isEmpty_base((const XContainer*)extension))
            continue;
        candidate = XString_create_copy(original);
        if (!candidate || !XString_append_utf8(candidate, ".") ||
            !XString_append(candidate, extension)) {
            if (candidate) XString_delete_base((XClass*)candidate);
            continue;
        }
        XFile_setFileName(file, candidate);
        if (XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
            XString_delete_base((XClass*)data->m_fileName);
            data->m_fileName = candidate;
            XString_delete_base((XClass*)original);
            XStringList_delete_base((XClass*)extensions);
            return true;
        }
        XString_delete_base((XClass*)candidate);
    }
    /* 所有候选都失败时恢复 QFile 的原始名称，保持 fileName() 契约。 */
    XFile_setFileName(file, original);
    XString_delete_base((XClass*)original);
    XStringList_delete_base((XClass*)extensions);
    return false;
}

static XImageIOHandler* XImageReader_ensureHandler(XImageReader* self)
{
    XImageReaderPrivate* data;
    const XString* handlerFormat;
    XString* suffixFormat = NULL;
    XIODevice* device;
    bool hasExplicitFormat;
    bool skipOwnedOpen;
    if (!self || !(data = self->m_data)) return NULL;
    if (data->m_handler) return data->m_handler;
    /* decideFormatFromContent 只改变处理器筛选时是否忽略格式和扩展名；
       autoDetectImageFormat 是独立状态，内容模式仍通过空格式交给插件。 */
    hasExplicitFormat = !data->m_decideFromContent && data->m_format &&
        !XContainer_isEmpty_base((const XContainer*)data->m_format);
    skipOwnedOpen = !data->m_autoDetectFormat && !data->m_decideFromContent &&
        (!data->m_format ||
         XContainer_isEmpty_base((const XContainer*)data->m_format));
    handlerFormat = hasExplicitFormat ? data->m_format : NULL;
    if (!handlerFormat && data->m_autoDetectFormat && !data->m_decideFromContent &&
        data->m_fileName)
        suffixFormat = XImageReader_fileSuffix(data->m_fileName);
    if (!handlerFormat) handlerFormat = suffixFormat;
    device = data->m_device ? data->m_device : XImageReader_ensureOwnedDevice(data);
    if (!device) {
        XImageReader_setError(self, XImageReaderError_DeviceError,
                              "Invalid device");
        return NULL;
    }
    if (!XIODevice_isOpen(device)) {
        /* Qt 的 QFile（即 deleteDevice=true）在关闭自动探测、没有格式且
           未启用内容决策时不会尝试打开文件；处理器前置检查随后报告
           UnsupportedFormatError。显式格式或内容决策仍需先打开设备，
           自动探测开启时还执行原路径打开与候选后缀回退。外部 QIODevice
           则始终按 QImageReader::initHandler() 规则尝试打开，失败立即
           报告 DeviceError。 */
        if (data->m_fileDevice) {
            if (!skipOwnedOpen &&
                !XIODevice_open_base(device, XIODevice_ReadOnly)) {
                if (!data->m_autoDetectFormat ||
                    !XImageReader_tryDefaultExtensions(data)) {
                    XImageReader_setError(self, XImageReaderError_FileNotFoundError,
                                          "File not found");
                    if (suffixFormat)
                        XString_delete_base((XClass*)suffixFormat);
                    return NULL;
                }
                /* tryDefaultExtensions() 已经打开候选文件。 */
                /* QFileInfo::suffix() 必须依据最终成功打开的候选名
                   重新计算，保持后缀处理器优先级。 */
                if (!hasExplicitFormat && !data->m_decideFromContent &&
                    data->m_autoDetectFormat && data->m_fileName) {
                    if (suffixFormat)
                        XString_delete_base((XClass*)suffixFormat);
                    suffixFormat = XImageReader_fileSuffix(data->m_fileName);
                    handlerFormat = suffixFormat;
                }
            }
        } else if (!XIODevice_open_base(device, XIODevice_ReadOnly)) {
            XImageReader_setError(self, XImageReaderError_DeviceError,
                                  "Invalid device");
            if (suffixFormat)
                XString_delete_base((XClass*)suffixFormat);
            return NULL;
        }
    }
    /* QImageReader 在关闭自动探测且未指定格式时不会创建处理器；
       该前置条件位于设备有效性/打开检查之后，因而无设备时仍应报告
       DeviceError，而不是被空格式条件抢先改写成 UnsupportedFormatError。
       显式空 XString 与 Qt 的空 QByteArray 状态相同，
       decideFormatFromContent 不能覆盖此拒绝。 */
    if (!data->m_autoDetectFormat &&
        (!data->m_format ||
         XContainer_isEmpty_base((const XContainer*)data->m_format))) {
        XImageReader_setError((XImageReader*)self,
                              XImageReaderError_UnsupportedFormatError,
                              "Unsupported image format");
        if (suffixFormat)
            XString_delete_base((XClass*)suffixFormat);
        return NULL;
    }
#if XIMAGEIOPLUGIN_ON
    if (device) {
        /* 文件名后缀存在且允许自动探测时，Qt 先单独尝试
           suffixPluginIndex，再在后续插件阶段跳过该插件；不能把两步
           合并成普通格式遍历，否则首个后缀工厂失败时会漏掉后续同键插件。 */
        data->m_handler = suffixFormat && data->m_autoDetectFormat &&
            !data->m_decideFromContent
            ? XImagePluginRegistry_createReadHandlerSuffix(device,
                                                            suffixFormat)
            : XImagePluginRegistry_createReadHandlerEx(
                  device, handlerFormat, data->m_autoDetectFormat,
                  data->m_decideFromContent);
        /* Qt 在文件后缀命中插件后仍调用处理器 canRead()；错误后缀时
           销毁该处理器并继续按内容探测。保存设备位置，避免自定义插件
           的 canRead() 消费窥视数据影响后续处理。 */
        if (data->m_handler && suffixFormat) {
            int64_t position = XIODevice_isSequential(device)
                ? -1 : XIODevice_pos_base(device);
            bool canRead = XImageIOHandler_canRead_base(data->m_handler);
            if (position >= 0)
                (void)XIODevice_seek_base(device, position);
            if (!canRead) {
                XImageIOHandler_delete_base(data->m_handler);
                data->m_handler = NULL;
            }
        }
        if (data->m_handler) {
            if (suffixFormat) XString_delete_base((XClass*)suffixFormat);
            return data->m_handler;
        }
        /* 仅自动探测模式会产生 suffixFormat；此时与 Qt 一样，后缀
           处理器拒绝内容后改用空格式进行插件/内置处理器探测。 */
        if (suffixFormat && data->m_autoDetectFormat &&
            !data->m_decideFromContent) {
            data->m_handler = XImagePluginRegistry_createReadHandlerContentFallback(
                device, suffixFormat);
            if (data->m_handler) {
                XString_delete_base((XClass*)suffixFormat);
                return data->m_handler;
            }
        }
    }
#else
    (void)self;
    (void)device;
#endif
    if (suffixFormat) XString_delete_base((XClass*)suffixFormat);
    /* Qt QImageReaderPrivate::initHandler() 只要处理器工厂最终返回
       nullptr，就统一报告 UnsupportedFormatError。设备打开失败等更早
       产生的 DeviceError/FileNotFoundError 必须保持不变，因此仅覆盖
       尚未设置错误的状态。 */
    if (!data->m_handler && data->m_error == XImageReaderError_UnknownError)
        XImageReader_setError((XImageReader*)self,
                              XImageReaderError_UnsupportedFormatError,
                              "Unsupported image format");
    return NULL;
}

static void VXImageReader_deinit(XImageReader* self)
{
    if (ISNULL(self, "XImageReader")) return;
    if (self->m_data)
    {
        if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
        if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
        if (self->m_data->m_errorString) XString_delete_base((XClass*)self->m_data->m_errorString);
        XImageReader_releaseHandler(self->m_data);
        XImageReader_releaseOwnedDevice(self->m_data);
        XStringList_deinit_base((XClass*)&self->m_data->m_textKeys);
        XStringList_deinit_base((XClass*)&self->m_data->m_textValues);
        XString_deinit_base((XClass*)&self->m_data->m_textCache);
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
        if (self->m_data->m_animation) XImageCodecAnimation_delete(self->m_data->m_animation);
        if (self->m_data->m_sourceBytes) XByteArray_delete_base((XClass*)self->m_data->m_sourceBytes);
#endif
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
}

#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
static void XImageReader_clearAnimation(XImageReaderPrivate* data)
{
    if (!data) return;
    if (data->m_animation) XImageCodecAnimation_delete(data->m_animation);
    data->m_animation = NULL;
    if (data->m_sourceBytes) XByteArray_delete_base((XClass*)data->m_sourceBytes);
    data->m_sourceBytes = NULL;
    data->m_currentImageNumber = -1;
    data->m_imageJumpPending = false;
}

static bool XImageReader_prepareAnimation(XImageReader* self)
{
    XImageReaderPrivate* data;
    XByteArray* bytes = NULL;
    XFile* file = NULL;
    const uint8_t* raw;
    size_t size;
    if (!self || !(data = self->m_data) || data->m_animation) return data && data->m_animation;
    if (data->m_format && !XContainer_isEmpty_base((const XContainer*)data->m_format) &&
        XImageCodec_formatFromName(data->m_format) != XImageCodecFormat_Gif)
        return false;
    if (!data->m_fileName && data->m_device &&
        (!data->m_format || XImageCodec_formatFromName(data->m_format) == XImageCodecFormat_Gif)) {
        XByteArray* head = XIODevice_peek_3(data->m_device, 8);
        bool isGif = head && XImageCodec_detect((const uint8_t*)XByteArray_data(head),
                                                 XByteArray_size_base((const XContainer*)head)) == XImageCodecFormat_Gif;
        if (head) XByteArray_delete_base((XClass*)head);
        if (!isGif) return false;
    }
    if (data->m_fileName) {
        file = XFile_create_2(data->m_fileName);
        if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
            if (file) XClass_delete_base((XClass*)file);
            return false;
        }
        bytes = XIODevice_readAll_3((XIODevice*)file);
        XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
    } else if (data->m_device) {
        bytes = XIODevice_readAll_3(data->m_device);
    }
    if (!bytes) return false;
    raw = (const uint8_t*)XByteArray_data(bytes);
    size = XByteArray_size_base((const XContainer*)bytes);
    if (XImageCodec_detect(raw, size) != XImageCodecFormat_Gif) {
        XByteArray_delete_base((XClass*)bytes);
        return false;
    }
    data->m_animation = XImageCodec_decodeAnimation(raw, size);
    if (!data->m_animation) {
        XByteArray_delete_base((XClass*)bytes);
        return false;
    }
    data->m_sourceBytes = bytes;
    /* QGifHandler::frameNumber 在首次 read() 前为 -1；read() 成功后
       才递增到首帧编号。 */
    data->m_currentImageNumber = -1;
    data->m_imageJumpPending = false;
    return true;
}
#endif

XVtable* XImageReader_class_init()
{
    XVTABLE_INIT_DEFAULT(XImageReader)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImageReader_deinit);
    return XVTABLE_DEFAULT;
}

XImageReader* XImageReader_create_ex(XMemoryType memory)
{
    XImageReader* self = (XImageReader*)XMemory_malloc(sizeof(XImageReader), memory);
    if (!self) return NULL;
    XImageReader_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

void XImageReader_init(XImageReader* self)
{
    if (ISNULL(self, "XImageReader")) return;
    memset(self, 0, sizeof(XImageReader));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XImageReader);
    self->m_data = (XImageReaderPrivate*)XMalloc_System(sizeof(XImageReaderPrivate));
    if (self->m_data)
    {
        memset(self->m_data, 0, sizeof(XImageReaderPrivate));
        XStringList_init(&self->m_data->m_textKeys);
        XStringList_init(&self->m_data->m_textValues);
        XString_init(&self->m_data->m_textCache);
        self->m_data->m_autoDetectFormat = true;
        /* QSize 默认值为 (-1,-1)，表示未请求缩放。 */
        self->m_data->m_scaledW = -1;
        self->m_data->m_scaledH = -1;
        self->m_data->m_quality = -1;
        self->m_data->m_error = XImageReaderError_UnknownError;
        self->m_data->m_errorString = XString_create();
    }
}

void XImageReader_init_device(XImageReader* self, XIODevice* device, const XString* format)
{
    XImageReader_init(self);
    if (self->m_data)
    {
        self->m_data->m_device = device;
        if (format && !XContainer_isEmpty_base((const XContainer*)format))
            self->m_data->m_format = XString_create_copy(format);
    }
}

void XImageReader_init_device_2(XImageReader* self, XIODevice* device, const char* format)
{
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImageReader_init_device(self, device, formatString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImageReader_init_file(XImageReader* self, const XString* fileName, const XString* format)
{
    XImageReader_init(self);
    if (self->m_data)
    {
        if (fileName) self->m_data->m_fileName = XString_create_copy(fileName);
        if (format && !XContainer_isEmpty_base((const XContainer*)format))
            self->m_data->m_format = XString_create_copy(format);
        if (self->m_data->m_fileName)
            (void)XImageReader_ensureOwnedDevice(self->m_data);
    }
}

void XImageReader_init_file_2(XImageReader* self, const char* fileName, const char* format)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImageReader_init_file(self, fileNameString, formatString);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImageReader_setFormat(XImageReader* self, const XString* format)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_format) XString_delete_base((XClass*)self->m_data->m_format);
    self->m_data->m_format = format ? XString_create_copy(format) : NULL;
}

void XImageReader_setFormat_2(XImageReader* self, const char* format)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    XImageReader_setFormat(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageReader_format_const(const XImageReader* self)
{
    XImageReaderPrivate* data;
    XIODevice* device;
    const XString* handlerFormat;
    if (!self || !(data = self->m_data)) return NULL;
    if (data->m_format &&
        !XContainer_isEmpty_base((const XContainer*)data->m_format))
        return data->m_format;
    XImageReader_ensureHandler((XImageReader*)self);
    if (data->m_handler) {
        /* QImageReader::format() 只有在处理器确认 canRead() 后才返回
           处理器格式；处理器存在但无法读取时必须返回空格式。
           即使 canRead() 成功但处理器格式为空，也必须返回空格式，不能
           再用设备签名探测结果替代。Qt 6.8 qimagereader.cpp:636-645
           直接返回 handler->format()，不再进入第二种探测路径。 */
        if (!XImageIOHandler_canRead_base(data->m_handler))
            return NULL;
        handlerFormat = XImageIOHandler_format_const(data->m_handler);
        if (handlerFormat &&
            !XContainer_isEmpty_base((const XContainer*)handlerFormat))
            return handlerFormat;
        return NULL;
    }
#if XIMAGEIOPLUGIN_ON
    /* Qt QImageReader::format() 在处理器初始化失败时直接返回空格式；
       只有裁剪掉插件处理器后，便携层才允许用裸数据签名作为兼容探测。
       若插件已启用仍回落到 imageFormatDevice()，会把处理器工厂拒绝的
       数据误报为格式，偏离 qimagereader.cpp:636-645。 */
    return NULL;
#else
    device = data->m_device ? data->m_device : data->m_fileDevice;
    if (!device) return NULL;
    if (!data->m_detectedFormat)
        data->m_detectedFormat = XImageReader_imageFormatDevice(device);
    return data->m_detectedFormat &&
           !XContainer_isEmpty_base((const XContainer*)data->m_detectedFormat)
        ? data->m_detectedFormat : NULL;
#endif
}

XString* XImageReader_format(const XImageReader* self)
{
    const XString* value = XImageReader_format_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageReader_format_2(const XImageReader* self)
{ return XString_toUtf8(XImageReader_format_const(self)); }

void XImageReader_setAutoDetectImageFormat(XImageReader* self, bool enabled)
{ if (self && self->m_data) self->m_data->m_autoDetectFormat = enabled; }

bool XImageReader_autoDetectImageFormat(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_autoDetectFormat : true; }

void XImageReader_setDecideFormatFromContent(XImageReader* self, bool enabled)
{
    if (!self || !self->m_data) return;
    self->m_data->m_decideFromContent = enabled;
}

bool XImageReader_decideFormatFromContent(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_decideFromContent : false; }

void XImageReader_setDevice(XImageReader* self, XIODevice* device)
{
    if (!self || !self->m_data) return;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    XImageReader_clearAnimation(self->m_data);
#endif
    XImageReader_releaseHandler(self->m_data);
    XImageReader_releaseOwnedDevice(self->m_data);
    /* QImageReader::setDevice() 清空处理器解析出的文本；同时丢弃依赖旧
       设备内容的格式/尺寸缓存，避免下一次 format()/size() 复用旧探测结果。 */
    XImageReader_clearDetectedFormat(self->m_data);
    XImageReader_clearText(self->m_data);
    self->m_data->m_device = device;
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = NULL;
    self->m_data->m_hasSize = false;
}

XIODevice* XImageReader_device(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_device : NULL; }

void XImageReader_setFileName(XImageReader* self, const XString* fileName)
{
    if (!self || !self->m_data) return;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    XImageReader_clearAnimation(self->m_data);
#endif
    XImageReader_releaseHandler(self->m_data);
    XImageReader_releaseOwnedDevice(self->m_data);
    /* 新文件名对应新设备，所有内容派生状态都必须失效。 */
    XImageReader_clearDetectedFormat(self->m_data);
    XImageReader_clearText(self->m_data);
    if (self->m_data->m_fileName) XString_delete_base((XClass*)self->m_data->m_fileName);
    self->m_data->m_fileName = fileName ? XString_create_copy(fileName) : NULL;
    self->m_data->m_device = NULL;
    if (self->m_data->m_fileName)
        (void)XImageReader_ensureOwnedDevice(self->m_data);
    self->m_data->m_hasSize = false;
}

void XImageReader_setFileName_2(XImageReader* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    XImageReader_setFileName(self, value);
    if (value) XString_delete_base((XClass*)value);
}

const XString* XImageReader_fileName_const(const XImageReader* self)
{
    XIODevice* device;
    if (!self || !self->m_data) return NULL;
    if (self->m_data->m_fileName) return self->m_data->m_fileName;

    /* Qt QImageReader::fileName() 仅对 QFile 设备返回设备文件名；
       其它 QIODevice 即使内部携带自定义名称也必须保持空字符串语义。 */
    device = self->m_data->m_device;
    if (device && XClassGetVtable(device) == XFile_class_init())
        return XFileDevice_fileName_base((XFileDevice*)device);
    return NULL;
}

XString* XImageReader_fileName(const XImageReader* self)
{
    const XString* value = XImageReader_fileName_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImageReader_fileName_2(const XImageReader* self)
{ return XString_toUtf8(XImageReader_fileName_const(self)); }

void XImageReader_size(const XImageReader* self, XSize* out)
{
    if (!out) return;
    out->width = 0;
    out->height = 0;
    if (!self || !self->m_data) return;
    XImageReader_ensureHandler((XImageReader*)self);
    if (self->m_data->m_handler) {
        /* Qt 只在处理器声明支持 Size 时查询该选项；处理器已经存在但
           不支持时不能再用通用探测替代，否则会改变自定义插件语义。 */
        if (XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                                 XImageIOHandlerOption_Size))
            (void)XImageIOHandler_option_base(self->m_data->m_handler,
                                              XImageIOHandlerOption_Size, out);
        return;
    }
#if !XIMAGEIOPLUGIN_ON
    /* 插件发现开启时，Qt 的 supportsOption(Size) 在处理器创建失败后
       直接返回 false；不能绕过处理器再按裸文件头推导尺寸。插件裁剪
       关闭时保留 XImageCodec 的嵌入式读取路径。 */
    if (XImageReader_probeSize((XImageReader*)self)) {
        out->width = self->m_data->m_sizeW;
        out->height = self->m_data->m_sizeH;
    }
#endif
}

XImageFormat XImageReader_imageFormatValue(const XImageReader* self)
{
    XImageIOHandlerOptionValue value;
    if (!self || !self->m_data ||
        !XImageReader_ensureHandler((XImageReader*)self) ||
        !XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                             XImageIOHandlerOption_ImageFormat))
        return XImageFormat_Invalid;
    memset(&value, 0, sizeof(value));
    if (!XImageIOHandler_option_base(self->m_data->m_handler,
                                     XImageIOHandlerOption_ImageFormat,
                                     &value))
        return XImageFormat_Invalid;
    return value.format;
}

XStringList* XImageReader_textKeys(const XImageReader* self)
{
    XImageReaderPrivate* data;
    XImageReader_loadText((XImageReader*)self);
    data = (self) ? self->m_data : NULL;
    return data ? XStringList_create_copy(&data->m_textKeys)
                : XImageReader_makeStringList(NULL, 0);
}
XString* XImageReader_text(const XImageReader* self, const XString* key)
{
    XImageReaderPrivate* data;
    int64_t count;
    int64_t i;
    XImageReader_loadText((XImageReader*)self);
    data = (self) ? self->m_data : NULL;
    if (!data || !key) return XString_create();
    count = (int64_t)XStringList_size_base((const XContainer*)&data->m_textKeys);
    for (i = 0; i < count; ++i) {
        const XString* item = (const XString*)XStringList_at_base(
            (XVector*)&data->m_textKeys, i);
        if (item && XString_equals(item, key, XChar_CaseSensitive)) {
            const XString* value = (const XString*)XStringList_at_base(
                (XVector*)&data->m_textValues, i);
            return value ? XString_create_copy(value) : XString_create();
        }
    }
    return XString_create();
}

const char* XImageReader_text_2(const XImageReader* self, const char* key)
{
    XString* keyString;
    XString* value;
    const char* utf8;
    static const char empty[] = "";
    /* Qt 的 QString 重载无法传入空指针；C 兼容重载将空键映射为空值，
       与 text() 的缺失键结果保持一致。 */
    if (!key) return empty;
    keyString = XString_create_utf8(key);
    if (!keyString) return NULL;
    value = XImageReader_text(self, keyString);
    XString_delete_base((XClass*)keyString);
    if (!value) return empty;
    utf8 = XString_toUtf8(value);
    if (!utf8 || !utf8[0]) {
        XString_delete_base((XClass*)value);
        return empty;
    }
    /* 兼容重载返回读取器内部缓存；下一次调用会覆盖该缓存。 */
    if (self && self->m_data) {
        XString_copy_base((XClass*)&self->m_data->m_textCache,
                          (const XClass*)value);
        XString_delete_base((XClass*)value);
        return XString_toUtf8(&self->m_data->m_textCache);
    }
    XString_delete_base((XClass*)value);
    return empty;
}

void XImageReader_setClipRect(XImageReader* self, const XRect* rect)
{
    if (!self || !self->m_data || !rect) return;
    self->m_data->m_clipX = rect->x;
    self->m_data->m_clipY = rect->y;
    self->m_data->m_clipW = rect->width;
    self->m_data->m_clipH = rect->height;
    self->m_data->m_hasClipRect = true;
}

void XImageReader_clipRect(const XImageReader* self, XRect* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_clipX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_clipY : 0;
        out->width = (self && self->m_data) ? self->m_data->m_clipW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_clipH : 0;
    }
}

void XImageReader_setScaledSize(XImageReader* self, const XSize* size)
{
    if (!self || !self->m_data || !size) return;
    self->m_data->m_scaledW = size->width;
    self->m_data->m_scaledH = size->height;
    self->m_data->m_hasScaledSize = true;
}

void XImageReader_scaledSize(const XImageReader* self, XSize* out)
{
    if (out)
    {
        out->width = (self && self->m_data) ? self->m_data->m_scaledW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_scaledH : 0;
    }
}

void XImageReader_setQuality(XImageReader* self, int quality)
{ if (self && self->m_data) self->m_data->m_quality = quality; }

int XImageReader_quality(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_quality : -1; }

void XImageReader_setScaledClipRect(XImageReader* self, const XRect* rect)
{
    if (!self || !self->m_data || !rect) return;
    self->m_data->m_scaledClipX = rect->x;
    self->m_data->m_scaledClipY = rect->y;
    self->m_data->m_scaledClipW = rect->width;
    self->m_data->m_scaledClipH = rect->height;
    self->m_data->m_hasScaledClipRect = true;
}

void XImageReader_scaledClipRect(const XImageReader* self, XRect* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_scaledClipX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_scaledClipY : 0;
        out->width = (self && self->m_data) ? self->m_data->m_scaledClipW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_scaledClipH : 0;
    }
}

void XImageReader_setBackgroundColor(XImageReader* self, uint32_t color)
{
    XImageIOHandlerOptionValue value;
    if (!self || !self->m_data || !XImageReader_ensureHandler(self) ||
        !XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                             XImageIOHandlerOption_BackgroundColor))
        return;
    memset(&value, 0, sizeof(value));
    value.color = color;
    XImageIOHandler_setOption_base(self->m_data->m_handler,
                                   XImageIOHandlerOption_BackgroundColor,
                                   &value);
}

uint32_t XImageReader_backgroundColor(const XImageReader* self)
{
    XImageIOHandlerOptionValue value;
    if (!self || !self->m_data || !XImageReader_ensureHandler((XImageReader*)self) ||
        !XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                             XImageIOHandlerOption_BackgroundColor))
        return 0;
    memset(&value, 0, sizeof(value));
    return XImageIOHandler_option_base(self->m_data->m_handler,
                                       XImageIOHandlerOption_BackgroundColor,
                                       &value)
        ? value.color : 0;
}

bool XImageReader_supportsAnimation(const XImageReader* self)
{
    XImageIOHandlerOptionValue value;
    XImageIOHandler* handler;
    if (!self || !self->m_data) return false;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (!self->m_data->m_format ||
        XContainer_isEmpty_base((const XContainer*)self->m_data->m_format) ||
        XImageCodec_formatFromName(self->m_data->m_format) == XImageCodecFormat_Gif) {
        if (XImageReader_prepareAnimation((XImageReader*)self))
            return true;
    }
#endif
    /* Qt QImageReader::supportsAnimation() 透传处理器的 Animation 选项，
       因此非 GIF 的插件格式也必须经过这一条通用路径。 */
    handler = XImageReader_ensureHandler((XImageReader*)self);
    if (!handler || !XImageIOHandler_supportsOption_base(
            handler, XImageIOHandlerOption_Animation))
        return false;
    memset(&value, 0, sizeof(value));
    return XImageIOHandler_option_base(handler,
                                       XImageIOHandlerOption_Animation,
                                       &value) && value.boolean;
}

/* 按 Qt qt_imageTransform 的顺序应用处理器方向元数据：先镜像，再旋转。 */
static void XImageReader_applyAutoTransform(
    XImage* image, XImageIOHandlerTransformation transformation)
{
    XImageTransform matrix;
    XImage rotated;
    int width;
    int height;
    bool rotate90;
    bool rotate270;
    if (!image || XImage_isNull(image) ||
        transformation == XImageIOHandlerTransformation_None)
        return;
    rotate90 = transformation == XImageIOHandlerTransformation_Rotate90 ||
               transformation == XImageIOHandlerTransformation_MirrorAndRotate90 ||
               transformation == XImageIOHandlerTransformation_FlipAndRotate90;
    rotate270 = transformation == XImageIOHandlerTransformation_Rotate270;
    if (!rotate90 && !rotate270) {
        XImage_mirroredInPlace(
            image,
            transformation == XImageIOHandlerTransformation_Mirror ||
                transformation == XImageIOHandlerTransformation_Rotate180,
            transformation == XImageIOHandlerTransformation_Flip ||
                transformation == XImageIOHandlerTransformation_Rotate180);
        return;
    }
    if (rotate90 && transformation != XImageIOHandlerTransformation_Rotate90)
        XImage_mirroredInPlace(
            image,
            transformation == XImageIOHandlerTransformation_MirrorAndRotate90,
            transformation == XImageIOHandlerTransformation_FlipAndRotate90);
    width = XImage_width(image);
    height = XImage_height(image);
    memset(&matrix, 0, sizeof(matrix));
    matrix.m11 = 0.0f;
    matrix.m22 = 0.0f;
    matrix.m33 = 1.0f;
    if (rotate270) {
        matrix.m12 = -1.0f;
        matrix.m21 = 1.0f;
        matrix.dy = (float)width;
    } else {
        matrix.m12 = 1.0f;
        matrix.m21 = -1.0f;
        matrix.dx = (float)height;
    }
    XImage_init(&rotated);
    XImage_transformed(image, &matrix, 0, &rotated);
    if (!XImage_isNull(&rotated)) {
        XImage_deinit_base(image);
        XImage_move_base(image, &rotated);
    }
    XImage_deinit_base(&rotated);
}

XImageIOHandlerTransformation XImageReader_transformation(const XImageReader* self)
{
    XImageIOHandlerOptionValue value;
    if (!self || !self->m_data ||
        !XImageReader_ensureHandler((XImageReader*)self) ||
        !XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                             XImageIOHandlerOption_ImageTransformation))
        return XImageIOHandlerTransformation_None;
    memset(&value, 0, sizeof(value));
    return XImageIOHandler_option_base(self->m_data->m_handler,
                                       XImageIOHandlerOption_ImageTransformation,
                                       &value)
        ? value.transformation : XImageIOHandlerTransformation_None;
}

void XImageReader_setAutoTransform(XImageReader* self, bool enabled)
{ if (self && self->m_data) self->m_data->m_autoTransform = enabled; }

bool XImageReader_autoTransform(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_autoTransform : false; }

XString* XImageReader_subType(const XImageReader* self)
{
    const XString* value = XImageReader_subType_const(self);
    return value ? XString_create_copy(value) : XString_create();
}

const XString* XImageReader_subType_const(const XImageReader* self)
{
    XImageIOHandlerOptionValue value;
    if (!self || !self->m_data ||
        !XImageReader_ensureHandler((XImageReader*)self) ||
        !XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                             XImageIOHandlerOption_SubType))
        return NULL;
    memset(&value, 0, sizeof(value));
    if (!XImageIOHandler_option_base(self->m_data->m_handler,
                                     XImageIOHandlerOption_SubType, &value) ||
        !value.string || XContainer_isEmpty_base((const XContainer*)value.string))
        return NULL;
    return value.string;
}

const char* XImageReader_subType_2(const XImageReader* self)
{
    return XString_toUtf8(XImageReader_subType_const(self));
}
XStringList* XImageReader_supportedSubTypes(const XImageReader* self)
{
    XImageIOHandlerOptionValue value;
    XStringList* result;
    if (!self || !self->m_data ||
        !XImageReader_ensureHandler((XImageReader*)self) ||
        !XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                             XImageIOHandlerOption_SupportedSubTypes))
        return XImageReader_makeStringList(NULL, 0);
    memset(&value, 0, sizeof(value));
    if (!XImageIOHandler_option_base(self->m_data->m_handler,
                                     XImageIOHandlerOption_SupportedSubTypes,
                                     &value) || !value.stringList)
        return XImageReader_makeStringList(NULL, 0);
    result = XStringList_create_copy(value.stringList);
    return result ? result : XImageReader_makeStringList(NULL, 0);
}

bool XImageReader_canRead(const XImageReader* self)
{
    if (!self || !self->m_data) return false;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    /* 动画缓存建立后，设备可能已被 readAll() 消费到末尾；此时缓存本身
       就是 QGifHandler 尚可读取帧的状态，不能再用设备位置重复探测。 */
    if (self->m_data->m_animation) {
        if (self->m_data->m_imageJumpPending)
            return self->m_data->m_currentImageNumber >= 0 &&
                   self->m_data->m_currentImageNumber <
                       self->m_data->m_animation->frameCount;
        return self->m_data->m_currentImageNumber + 1 <
               self->m_data->m_animation->frameCount;
    }
#endif
    if (XImageReader_ensureHandler((XImageReader*)self))
        return XImageIOHandler_canRead_base(self->m_data->m_handler);
#if XIMAGEIOPLUGIN_ON
    /* 与 QImageReader::canRead() 一致：插件发现开启时，处理器创建失败
       即表示当前设备没有可读处理器，不能仅凭后缀或签名报告 true。 */
    return false;
#else
    /* 裁剪掉 XImageIOPlugin 后，内置 XImageCodec 仍由 read()/load() 提供
       兼容路径；此时保留签名探测，避免关闭插件发现使内置能力不可见。 */
    if (!self->m_data->m_decideFromContent && self->m_data->m_format &&
        !XImageReader_isSupportedFormat(XString_toUtf8(self->m_data->m_format))) {
        /* Qt 6.8 的 initHandler() 对显式未知格式统一设置
           UnsupportedFormatError；插件裁剪不能只返回 false 而遗留
           UnknownError，否则 canRead() 与 read() 的错误状态不一致。 */
        if (self->m_data->m_error == XImageReaderError_UnknownError)
            XImageReader_setError((XImageReader*)self,
                                  XImageReaderError_UnsupportedFormatError,
                                  "Unsupported image format");
        return false;
    }
    if (self->m_data->m_device) {
        const char* detected;
        if (!self->m_data->m_autoDetectFormat &&
            (!self->m_data->m_format ||
             XContainer_isEmpty_base((const XContainer*)self->m_data->m_format)))
            return false;
        detected = XImageReader_imageFormatDevice_2(self->m_data->m_device);
        if (!self->m_data->m_decideFromContent &&
            self->m_data->m_format &&
            !XContainer_isEmpty_base((const XContainer*)self->m_data->m_format) &&
            !XImageReader_explicitFormatMatchesContent(
                self->m_data->m_format, detected) &&
            !XImageReader_explicitFormatMatchesDevice(
                self->m_data->m_format, self->m_data->m_device))
            return false;
        return XImageReader_isSupportedFormat(detected);
    }
    if (!self->m_data->m_fileName) return false;
    if (!self->m_data->m_autoDetectFormat &&
        (!self->m_data->m_format ||
         XContainer_isEmpty_base((const XContainer*)self->m_data->m_format)))
        return false;
    {
        const char* detected = XImageReader_imageFormat_2(
            XString_toUtf8(self->m_data->m_fileName));
        if (!self->m_data->m_decideFromContent &&
            self->m_data->m_format &&
            !XContainer_isEmpty_base((const XContainer*)self->m_data->m_format) &&
            !XImageReader_explicitFormatMatchesContent(
                self->m_data->m_format, detected))
            return false;
        return XImageReader_isSupportedFormat(detected);
    }
#endif
}

bool XImageReader_read(XImageReader* self, XImage* out)
{
    bool loadedByHandler = false;
    bool animationPrepared = false;
    bool supportScaledSize = false;
    bool supportClipRect = false;
    bool supportScaledClipRect = false;
    bool clipRectNull;
    bool scaledClipRectNull;
    bool clipRectValid;
    bool scaledClipRectValid;
    bool scaledSizeValid;
    XSize requestedSize;
    XSize scaledSize;
    XImageIOHandlerOptionValue optionValue;
    if (!self || !self->m_data || !out) return false;
    if (!self->m_data->m_autoDetectFormat &&
        !self->m_data->m_decideFromContent &&
        (!self->m_data->m_format ||
         XContainer_isEmpty_base((const XContainer*)self->m_data->m_format))) {
        XImageReader_setError(self, XImageReaderError_UnsupportedFormatError,
                              "Unsupported image format");
        return false;
    }
    /* Qt QImageReader::read() 先初始化处理器，再只在单边缩放时查询
       Size；普通读取不能因通用流程而触发处理器的 Size 选项副作用。
       插件裁剪关闭时没有处理器可供 allocateImage() 预检，因此仍查询
       内置编解码器可探测的原始尺寸。 */
#if XIMAGEIOPLUGIN_ON
    if (!XImageReader_ensureHandler(self)) {
        if (self->m_data->m_error == XImageReaderError_UnknownError)
            XImageReader_setError(self, XImageReaderError_UnsupportedFormatError,
                                  "Unsupported image format");
        return false;
    }
#else
    (void)XImageReader_ensureHandler(self);
#endif
    requestedSize.width = 0;
    requestedSize.height = 0;
    scaledSize.width = self->m_data->m_scaledW;
    scaledSize.height = self->m_data->m_scaledH;
    if ((scaledSize.width <= 0 && scaledSize.height > 0) ||
        (scaledSize.height <= 0 && scaledSize.width > 0) ||
        (!XIMAGEIOPLUGIN_ON && !self->m_data->m_handler))
        XImageReader_size(self, &requestedSize);
    {
        int allocationLimitMb = XImageReader_effectiveAllocationLimit();
        if (allocationLimitMb > 0 && requestedSize.width > 0 &&
            requestedSize.height > 0) {
            uint64_t pixels = (uint64_t)(unsigned)requestedSize.width *
                              (uint64_t)(unsigned)requestedSize.height;
            uint64_t limit = (uint64_t)(unsigned)allocationLimitMb *
                             1024u * 1024u;
            if (pixels > limit / 4u) {
                XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                      "Image exceeds the configured allocation limit");
                return false;
            }
        }
    }
    if ((scaledSize.width <= 0 && scaledSize.height > 0) ||
        (scaledSize.height <= 0 && scaledSize.width > 0)) {
        /* Qt 在只给出一个维度时按原始尺寸保持宽高比。 */
        if (requestedSize.width > 0 && requestedSize.height > 0) {
            if (scaledSize.width <= 0)
                scaledSize.width = (int)((double)requestedSize.width *
                                          (double)scaledSize.height /
                                          (double)requestedSize.height + 0.5);
            else
                scaledSize.height = (int)((double)requestedSize.height *
                                           (double)scaledSize.width /
                                           (double)requestedSize.width + 0.5);
        }
    }
    /* QRect::isNull() 仅由宽高同时为零决定，未设置的默认矩形也为空；
       QRect::isValid() 要求宽高均为正值，零宽或零高虽非 null 仍无效。 */
    clipRectNull = !self->m_data->m_hasClipRect ||
        (self->m_data->m_clipW == 0 && self->m_data->m_clipH == 0);
    scaledClipRectNull = !self->m_data->m_hasScaledClipRect ||
        (self->m_data->m_scaledClipW == 0 && self->m_data->m_scaledClipH == 0);
    clipRectValid = self->m_data->m_hasClipRect &&
        self->m_data->m_clipW > 0 && self->m_data->m_clipH > 0;
    scaledClipRectValid = self->m_data->m_hasScaledClipRect &&
        self->m_data->m_scaledClipW > 0 && self->m_data->m_scaledClipH > 0;
    scaledSizeValid = self->m_data->m_hasScaledSize &&
        scaledSize.width >= 0 && scaledSize.height >= 0;
    if (self->m_data->m_handler) {
        supportScaledSize = scaledSizeValid &&
            XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                                XImageIOHandlerOption_ScaledSize);
        supportClipRect = !clipRectNull &&
            XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                                XImageIOHandlerOption_ClipRect);
        supportScaledClipRect = !scaledClipRectNull &&
            XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                                XImageIOHandlerOption_ScaledClipRect);
        memset(&optionValue, 0, sizeof(optionValue));
        if (supportScaledSize && (supportClipRect || clipRectNull)) {
            optionValue.size = scaledSize;
            XImageIOHandler_setOption_base(self->m_data->m_handler,
                                           XImageIOHandlerOption_ScaledSize,
                                           &optionValue);
        }
        if (supportClipRect) {
            optionValue.rect.x = self->m_data->m_clipX;
            optionValue.rect.y = self->m_data->m_clipY;
            optionValue.rect.width = self->m_data->m_clipW;
            optionValue.rect.height = self->m_data->m_clipH;
            XImageIOHandler_setOption_base(self->m_data->m_handler,
                                           XImageIOHandlerOption_ClipRect,
                                           &optionValue);
        }
        if (supportScaledClipRect) {
            optionValue.rect.x = self->m_data->m_scaledClipX;
            optionValue.rect.y = self->m_data->m_scaledClipY;
            optionValue.rect.width = self->m_data->m_scaledClipW;
            optionValue.rect.height = self->m_data->m_scaledClipH;
            XImageIOHandler_setOption_base(self->m_data->m_handler,
                                           XImageIOHandlerOption_ScaledClipRect,
                                           &optionValue);
        }
        if (XImageIOHandler_supportsOption_base(self->m_data->m_handler,
                                                XImageIOHandlerOption_Quality)) {
            optionValue.integer = self->m_data->m_quality;
            XImageIOHandler_setOption_base(self->m_data->m_handler,
                                           XImageIOHandlerOption_Quality,
                                           &optionValue);
        }
    }
    /* Qt QImageReader::read(QImage*) 直接把调用方图像交给处理器；读取
       失败时若处理器未触碰输出，原图仍应保留。处理器/编解码器负责在
       成功分配时替换或复用输出，不能由读取器预先清空。 */
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    animationPrepared = XImageReader_prepareAnimation(self);
    if (animationPrepared) {
        XImageCodecAnimation* animation = self->m_data->m_animation;
        int frame = self->m_data->m_imageJumpPending
            ? self->m_data->m_currentImageNumber
            : self->m_data->m_currentImageNumber + 1;
        if (frame < 0) frame = 0;
        if (animation && frame >= 0 && frame < animation->frameCount) {
            XImage_copy_base(out, &animation->frames[frame].image);
            loadedByHandler = !XImage_isNull(out);
            if (loadedByHandler) {
                self->m_data->m_currentImageNumber = frame;
                self->m_data->m_imageJumpPending = false;
            }
        }
    }
#endif
    /* 已成功建立 GIF 动画缓存时，读完最后一帧必须像 QGifHandler 一样
       直接返回失败，不能再退回普通单帧处理器重复首帧。Qt 在 handler
       的 read() 返回失败时会统一设置 InvalidDataError（对应
       qimagereader.cpp:1211-1218），因此动画缓存耗尽也要保留同样的
       可观察错误状态。 */
    if (animationPrepared && !loadedByHandler) {
        XImageReader_setError(self, XImageReaderError_InvalidDataError,
                              "Unable to read image data");
        return false;
    }
    if (!loadedByHandler)
        XImageReader_ensureHandler(self);
    if (!loadedByHandler && self->m_data->m_handler)
    {
        loadedByHandler = XImageIOHandler_read_base(self->m_data->m_handler, out);
        if (!loadedByHandler) {
            XImageReader_setError(self, XImageReaderError_InvalidDataError, "Unable to read image data");
            return false;
        }
    }
#if XIMAGEIOPLUGIN_ON
    /* Qt QImageReader::read() returns immediately when initHandler() could
       not create a handler; it never falls through to QImage::load().  The
       direct codec path below is reserved for the plugin-cropped build. */
    if (!loadedByHandler) {
        if (!self->m_data->m_handler &&
            self->m_data->m_error == XImageReaderError_UnknownError)
            XImageReader_setError(self, XImageReaderError_UnsupportedFormatError,
                                  "Unsupported image format");
        return false;
    }
#else
    if (!loadedByHandler && self->m_data->m_fileName)
    {
        /* 无显式格式时，Qt 仅在关闭自动探测的严格模式下拒绝；自动探测
           开启则允许内置解码器按内容识别，显式未知格式仍立即报错。 */
        if (!self->m_data->m_decideFromContent &&
            ((!self->m_data->m_autoDetectFormat && !self->m_data->m_format) ||
             (self->m_data->m_format &&
              !XImageReader_isSupportedFormat(
                  XString_toUtf8(self->m_data->m_format))))) {
            XImageReader_setError(self, XImageReaderError_UnsupportedFormatError,
                                  "The requested image format has no built-in decoder");
            return false;
        }
        {
            XImage loaded;
            bool ok;
            XImage_init(&loaded);
            ok = XImage_load_2(&loaded,
                               XString_toUtf8(self->m_data->m_fileName),
                               self->m_data->m_decideFromContent ? NULL :
                               XString_toUtf8(self->m_data->m_format));
            if (ok)
                XImage_move_base(out, &loaded);
            XImage_deinit_base(&loaded);
            if (!ok) {
            if (!XFile_exists_static(self->m_data->m_fileName))
                XImageReader_setError(self, XImageReaderError_FileNotFoundError,
                                      "Image file could not be opened");
            else {
                XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                      "Image data is invalid or unsupported");
            }
            return false;
            }
        }
    }
    else if (!loadedByHandler && self->m_data->m_device) {
        XByteArray* bytes = XIODevice_readAll_3(self->m_data->m_device);
        int64_t size = bytes ? (int64_t)XByteArray_size_base((const XContainer*)bytes) : 0;
        XImage loaded;
        bool ok;
        XImage_init(&loaded);
        ok = bytes && size <= INT_MAX &&
             XImage_loadFromData_2(&loaded,
                                   (const uint8_t*)XByteArray_data(bytes),
                                   (int)size,
                                   self->m_data->m_decideFromContent ? NULL :
                                   XString_toUtf8(self->m_data->m_format));
        if (ok)
            XImage_move_base(out, &loaded);
        XImage_deinit_base(&loaded);
        if (bytes) XByteArray_delete_base((XClass*)bytes);
        if (!ok) {
            XImageReader_setError(self, XImageReaderError_InvalidDataError,
                                  "Image data from the device is invalid or unsupported");
            return false;
        }
    } else if (!loadedByHandler) {
        XImageReader_setError(self, XImageReaderError_DeviceError, "No image source is set");
        return false;
    }
#endif

    /* 对处理器不支持的选项提供 Qt 兼容的软件回退，执行顺序为
       clip -> scale -> scaledClip；处理器已支持的步骤不重复执行。 */
    if (supportClipRect) {
        if (supportScaledSize) {
            if (!supportScaledClipRect && !scaledClipRectNull) {
                XImage clipped;
                XRect clipRect = { self->m_data->m_scaledClipX,
                                   self->m_data->m_scaledClipY,
                                   self->m_data->m_scaledClipW,
                                   self->m_data->m_scaledClipH };
                XImage_init(&clipped);
                XImage_copyRect(out, &clipRect, &clipped);
                XImage_deinit_base(out);
                XImage_move_base(out, &clipped);
                XImage_deinit_base(&clipped);
            }
        } else if (!supportScaledClipRect) {
            if (scaledSizeValid) {
                XImage scaled;
                XImage_init(&scaled);
                XImage_scaled(out, scaledSize.width, scaledSize.height, 0, 0, &scaled);
                XImage_deinit_base(out);
                XImage_move_base(out, &scaled);
                XImage_deinit_base(&scaled);
            }
            if (scaledClipRectValid) {
                XImage clipped;
                XRect clipRect = { self->m_data->m_scaledClipX,
                                   self->m_data->m_scaledClipY,
                                   self->m_data->m_scaledClipW,
                                   self->m_data->m_scaledClipH };
                XImage_init(&clipped);
                XImage_copyRect(out, &clipRect, &clipped);
                XImage_deinit_base(out);
                XImage_move_base(out, &clipped);
                XImage_deinit_base(&clipped);
            }
        }
    } else if (supportScaledSize && clipRectNull) {
        if (!supportScaledClipRect && scaledClipRectValid) {
            XImage clipped;
            XRect clipRect = { self->m_data->m_scaledClipX,
                               self->m_data->m_scaledClipY,
                               self->m_data->m_scaledClipW,
                               self->m_data->m_scaledClipH };
            XImage_init(&clipped);
            XImage_copyRect(out, &clipRect, &clipped);
            XImage_deinit_base(out);
            XImage_move_base(out, &clipped);
            XImage_deinit_base(&clipped);
        }
    } else if (!supportScaledClipRect) {
        if (!clipRectNull && clipRectValid) {
            XImage clipped;
            XRect clipRect = { self->m_data->m_clipX,
                               self->m_data->m_clipY,
                               self->m_data->m_clipW,
                               self->m_data->m_clipH };
            XImage_init(&clipped);
            XImage_copyRect(out, &clipRect, &clipped);
            XImage_deinit_base(out);
            XImage_move_base(out, &clipped);
            XImage_deinit_base(&clipped);
        }
        if (scaledSizeValid) {
            XImage scaled;
            XImage_init(&scaled);
            XImage_scaled(out, scaledSize.width, scaledSize.height, 0, 0, &scaled);
            XImage_deinit_base(out);
            XImage_move_base(out, &scaled);
            XImage_deinit_base(&scaled);
        }
        if (scaledClipRectValid) {
            XImage clipped;
            XRect clipRect = { self->m_data->m_scaledClipX,
                               self->m_data->m_scaledClipY,
                               self->m_data->m_scaledClipW,
                               self->m_data->m_scaledClipH };
            XImage_init(&clipped);
            XImage_copyRect(out, &clipRect, &clipped);
            XImage_deinit_base(out);
            XImage_move_base(out, &clipped);
            XImage_deinit_base(&clipped);
        }
    }
    /* 成功读取后按文件名设置高 DPI 设备像素比；设备输入没有文件名时
       保持图像原有 DPR，和 Qt 的 QFileInfo(filename) 路径一致。 */
    if (self->m_data->m_fileName) {
        const float ratio = XImageReader_fileDevicePixelRatio(
            self->m_data->m_fileName);
        /* 无 @Nx 后缀时保留处理器设置的 DPR；Qt 只在匹配后缀时写入。 */
        if (ratio > 1.0f)
            XImage_setDevicePixelRatio(out, ratio);
    }
    if (self->m_data->m_autoTransform)
        XImageReader_applyAutoTransform(out, XImageReader_transformation(self));
    /* Qt 返回处理器/解码器的成功标志，而不是依据输出图像是否为空
       再次推断；处理器若返回 true 但留下空图，调用方仍观察到成功。 */
    return true;
}

bool XImageReader_jumpToNextImage(XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (XImageReader_prepareAnimation(self) && self->m_data->m_animation &&
        self->m_data->m_currentImageNumber + 1 < self->m_data->m_animation->frameCount) {
        ++self->m_data->m_currentImageNumber;
        self->m_data->m_imageJumpPending = true;
        return true;
    }
#endif
    /* 非 GIF 或动画缓存构建失败时，遵循 QImageIOHandler 的通用虚函数语义。 */
    if (!self || !self->m_data || !XImageReader_ensureHandler(self))
        return false;
    return XImageIOHandler_jumpToNextImage_base(self->m_data->m_handler);
}
bool XImageReader_jumpToImage(XImageReader* self, int imageNumber)
{
    if (imageNumber < 0) return false;
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (XImageReader_prepareAnimation(self) && self->m_data->m_animation) {
        if (imageNumber >= self->m_data->m_animation->frameCount)
            return false;
        self->m_data->m_currentImageNumber = imageNumber;
        self->m_data->m_imageJumpPending = true;
        return true;
    }
#endif
    /* 非 GIF 或动画缓存构建失败时，委托处理器而非伪造单帧成功。 */
    if (!self || !self->m_data || !XImageReader_ensureHandler(self))
        return false;
    return XImageIOHandler_jumpToImage_base(self->m_data->m_handler,
                                            imageNumber);
}
int XImageReader_loopCount(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (self && self->m_data && XImageReader_prepareAnimation((XImageReader*)self) &&
        self->m_data->m_animation)
        return self->m_data->m_animation->loopCount;
#endif
    if (!self || !self->m_data || !XImageReader_ensureHandler((XImageReader*)self))
        return -1;
    return XImageIOHandler_loopCount_base(self->m_data->m_handler);
}
int XImageReader_imageCount(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (self && self->m_data && XImageReader_prepareAnimation((XImageReader*)self) && self->m_data->m_animation)
        return self->m_data->m_animation->frameCount;
#endif
    if (!self || !self->m_data || !XImageReader_ensureHandler((XImageReader*)self))
        return -1;
    return XImageIOHandler_imageCount_base(self->m_data->m_handler);
}
int XImageReader_nextImageDelay(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (self && self->m_data && XImageReader_prepareAnimation((XImageReader*)self) && self->m_data->m_animation) {
        int frame = self->m_data->m_currentImageNumber;
        if (frame >= 0 && frame < self->m_data->m_animation->frameCount)
            return self->m_data->m_animation->frames[frame].delayMs;
        /* QGifHandler 构造时 nextDelay 为 100；首次 read() 前尚无当前帧，
           因而应返回该默认值，而不是因设备已缓存消费而回落到失败路径。 */
        return 100;
    }
#endif
    if (!self || !self->m_data || !XImageReader_ensureHandler((XImageReader*)self))
        return -1;
    return XImageIOHandler_nextImageDelay_base(self->m_data->m_handler);
}
int XImageReader_currentImageNumber(const XImageReader* self)
{
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    /* QGifHandler 在首次 read() 前的 frameNumber 为 -1。主动准备 GIF
       缓存后再返回状态，避免尚未建立缓存时落到基类默认值 0。 */
    if (self && self->m_data &&
        XImageReader_prepareAnimation((XImageReader*)self) &&
        self->m_data->m_animation)
        return self->m_data->m_currentImageNumber;
#endif
    if (!self || !self->m_data || !XImageReader_ensureHandler((XImageReader*)self))
        return -1;
    return XImageIOHandler_currentImageNumber_base(self->m_data->m_handler);
}

void XImageReader_currentImageRect(const XImageReader* self, XRect* out)
{
    if (!out) return;
    memset(out, 0, sizeof(XRect));
#if XIMAGECODEC_ON && XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    if (self && self->m_data && XImageReader_prepareAnimation((XImageReader*)self) &&
        self->m_data->m_animation) {
        int frame = self->m_data->m_currentImageNumber;
        if (frame >= 0 && frame < self->m_data->m_animation->frameCount) {
            const XImageCodecFrame* value = &self->m_data->m_animation->frames[frame];
            out->x = value->left;
            out->y = value->top;
            out->width = value->width;
            out->height = value->height;
            return;
        }
    }
#endif
    if (!self || !self->m_data || !XImageReader_ensureHandler((XImageReader*)self))
        return;
    XImageIOHandler_currentImageRect_base(self->m_data->m_handler, out);
}

XImageReaderError XImageReader_error(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_error : XImageReaderError_UnknownError; }

XString* XImageReader_errorString(const XImageReader* self)
{
    const XString* value = XImageReader_errorString_const(self);
    if (!value || XContainer_isEmpty_base((const XContainer*)value))
        return XString_create_utf8("Unknown error");
    return XString_create_copy(value);
}

const XString* XImageReader_errorString_const(const XImageReader* self)
{ return (self && self->m_data) ? self->m_data->m_errorString : NULL; }

const char* XImageReader_errorString_2(const XImageReader* self)
{
    const XString* value = XImageReader_errorString_const(self);
    static const char unknown[] = "Unknown error";
    const char* utf8;
    if (!value || XContainer_isEmpty_base((const XContainer*)value))
        return unknown;
    utf8 = XString_toUtf8(value);
    return utf8 && utf8[0] ? utf8 : unknown;
}

bool XImageReader_supportsOption(const XImageReader* self, XImageIOHandlerOption option)
{
    XImageIOHandler* handler;
    if (!self || !self->m_data) return false;
    /* Qt 6.8 supportsOption() 先初始化处理器，再完全透传处理器能力。 */
    handler = XImageReader_ensureHandler((XImageReader*)self);
    return handler && XImageIOHandler_supportsOption_base(handler, option);
}

XString* XImageReader_imageFormat(const XString* fileName)
{
    XFile* file;
    XString* result;
    if (!fileName) return XString_create();
    file = XFile_create_2(fileName); if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) { if (file) XClass_delete_base((XClass*)file); return XString_create(); }
    result = XImageReader_imageFormatDevice((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    return result;
}

const char* XImageReader_imageFormat_2(const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    XString* result = XImageReader_imageFormat(value);
    const char* utf8 = XString_toUtf8(result);
    static char format[XIMAGE_READER_FORMAT_BUFFER_SIZE];
    if (utf8) {
        strncpy(format, utf8, sizeof(format) - 1u);
        format[sizeof(format) - 1u] = '\0';
    } else {
        format[0] = '\0';
    }
    if (result) XString_delete_base((XClass*)result);
    if (value) XString_delete_base((XClass*)value);
    return format[0] ? format : NULL;
}

XString* XImageReader_imageFormatDevice(XIODevice* device)
{
    XImageIOHandler* handler;
    bool handlerCanRead = false;
    bool handlerCanReadChecked = false;
    XByteArray* bytes;
    const char* result = NULL;
    XString* suffix = NULL;
    const XString* fileName = NULL;
    if (!device) return XString_create();
    /* Qt QImageReader::imageFormat(QIODevice*) 先创建空格式处理器，再
       调用其 canRead()，只有处理器确认内容可读时才接受其 format()；不能
       仅凭插件 capabilities() 宣称支持某个格式。Qt 6.8
       qimagereader.cpp:1458-1467 中，只要 handler 已创建就不会再回落到
       设备签名探测，因此 canRead() 成功但 format() 为空仍须返回空格式，
       不能把其它格式的签名误报为该处理器格式。 */
#if XIMAGEIOPLUGIN_ON
    /* QImageReader::imageFormat(QIODevice*) 对 QFile 设备同样执行文件后缀
       优先阶段；普通 QIODevice 没有文件名时才直接进入内容探测。使用
       XFile 的虚表身份限定该行为，避免把任意自定义 FileDevice 的名称
       误当作 QFile。 */
    if (XClassGetVtable(device) == XFile_class_init()) {
        fileName = XFileDevice_fileName_base((XFileDevice*)device);
        if (fileName)
            suffix = XImageReader_fileSuffix(fileName);
    }
    handler = suffix
        ? XImagePluginRegistry_createReadHandlerSuffix(device, suffix)
        : XImagePluginRegistry_createReadHandlerEx(device, NULL, true, false);
    /* 后缀处理器必须先确认内容；拒绝或工厂返回 NULL 后，由注册表只做
       一次内容回退，并跳过同一首个外部插件。此前两个相邻分支在“已创建
       但 canRead() 为假”的失败路径会重复调用该回退，可能让有副作用的
       插件被创建两次，偏离 Qt 6.8 的单次遍历语义。 */
    if (suffix) {
        if (handler) {
            int64_t pos = XIODevice_isSequential(device) ? -1
                : XIODevice_pos_base(device);
            handlerCanRead = XImageIOHandler_canRead_base(handler);
            handlerCanReadChecked = true;
            if (pos >= 0)
                (void)XIODevice_seek_base(device, pos);
            if (!handlerCanRead) {
                XImageIOHandler_delete_base(handler);
                handler = NULL;
            }
        }
        if (!handler) {
            handler = XImagePluginRegistry_createReadHandlerContentFallback(
                device, suffix);
            /* 后缀处理器被拒绝后，内容回退产生的是新处理器；它仍需在
               下面按 Qt 静态查询流程执行一次 canRead()。 */
            handlerCanReadChecked = false;
        }
    }
    if (suffix)
        XString_delete_base((XClass*)suffix);
    /* QImageReader::imageFormat(QIODevice*) 的 createReadHandlerHelper()
       在外部插件阶段之后仍会继续尝试内置处理器；通用 Ex 入口为了保持
       首个内容插件 create() 失败时的停止语义而不会执行该内置阶段，故在
       此专用静态查询路径补做一次无后缀内容回退。 */
    if (handler) {
        if (!handlerCanReadChecked)
            handlerCanRead = XImageIOHandler_canRead_base(handler);
        if (handlerCanRead) {
            const XString* handlerFormat =
                XImageIOHandler_format_const(handler);
            if (handlerFormat &&
                !XContainer_isEmpty_base((const XContainer*)handlerFormat)) {
                XString* format = XString_create_copy(handlerFormat);
                XImageIOHandler_delete_base(handler);
                if (format) return format;
            }
        }
        XImageIOHandler_delete_base(handler);
        return XString_create();
    }
#else
    (void)handler;
#endif
    /* QSvgTinyDocument::hasSvgHeader()窥视最多 4096 字节；静态查询也
       必须允许 XML 声明、注释与根节点之间存在较长前缀。其它格式的
       处理器只读取自身所需的短头部，不会因扩大窥视窗口改变结果。 */
    bytes = XIODevice_peek_3(device, 4096);
    if (bytes) {
        result = XImageReader_detectSignature(
            (const unsigned char*)XByteArray_data(bytes),
            (size_t)XByteArray_size_base((const XContainer*)bytes));
        XByteArray_delete_base((XClass*)bytes);
    }
#if !XIMAGEIOPLUGIN_ON && XIMAGECODEC_SVG_ON
    /* SVG 仅通过 Qt SVG 图像处理器提供公共 imageFormat() 结果；裁剪掉
       XImageIOPlugin 后虽然 codec facade 仍可直接读写 SVG，但没有处理器
       可返回格式名，不能在静态查询中伪造支持。 */
    if (result && (strcmp(result, "svg") == 0 || strcmp(result, "svgz") == 0))
        result = NULL;
#endif
    return result ? XString_create_utf8(result) : XString_create();
}

const char* XImageReader_imageFormatDevice_2(XIODevice* device)
{
    XString* value;
    const char* utf8;
    static char format[XIMAGE_READER_FORMAT_BUFFER_SIZE];
    value = XImageReader_imageFormatDevice(device);
    utf8 = XString_toUtf8(value);
    if (utf8) {
        strncpy(format, utf8, sizeof(format) - 1);
        format[sizeof(format) - 1] = '\0';
    } else {
        format[0] = '\0';
    }
    if (value) XString_delete_base((XClass*)value);
    return format[0] ? format : NULL;
}

XStringList* XImageReader_supportedImageFormats()
{
    return XImageReader_supportedFormats();
}

XStringList* XImageReader_supportedMimeTypes()
{
    XStringList* result = XStringList_create();
    size_t i;
    if (!result) return NULL;
#if XIMAGECODEC_ON
    for (i = 0; i < sizeof(g_imageReaderFormats) / sizeof(g_imageReaderFormats[0]); ++i)
        if (XImageCodec_canDecode(XImageCodec_formatFromName_2(g_imageReaderFormats[i])))
            XStringList_push_back_utf8(result, g_imageReaderMimeTypes[i]);
#endif
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_supportedMimeTypes(true);
        int64_t n;
        int64_t j;
        if (plugin) {
            n = XStringList_size_base((XContainer*)plugin);
            for (j = 0; j < n; ++j) {
                XString* item = (XString*)XStringList_at_base((XVector*)plugin, j);
                const char* value = XString_toUtf8(item);
                if (value && value[0] &&
                    !XStringList_contains_utf8(result, value, XChar_CaseSensitive))
                    XStringList_push_back_utf8(result, value);
            }
            XStringList_delete_base((XClass*)plugin);
        }
    }
#endif
    /* Qt QImageReader::supportedMimeTypes() 按字节序排序并去重。 */
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList_removeDuplicates(result);
    return result;
}

XStringList* XImageReader_imageFormatsForMimeType(const XString* mimeType)
{
    const char* mime = XString_toUtf8(mimeType);
    XStringList* result = XImageReader_makeStringList(NULL, 0);
    if (!result) return NULL;
    if (mime) {
        if (XImageReader_mimeEquals(mime, "image/bmp") && XImageReader_isSupportedFormat("bmp")) { const char* value[] = {"bmp"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/png") && XImageReader_isSupportedFormat("png")) { const char* value[] = {"png"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/gif") && XImageReader_isSupportedFormat("gif")) { const char* value[] = {"gif"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/x-portable-bitmap") && XImageReader_isSupportedFormat("pbm")) { const char* value[] = {"pbm"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/x-portable-graymap") && XImageReader_isSupportedFormat("pgm")) { const char* value[] = {"pgm"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/x-portable-pixmap") && XImageReader_isSupportedFormat("ppm")) { const char* value[] = {"ppm"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/x-xbitmap") && XImageReader_isSupportedFormat("xbm")) { const char* value[] = {"xbm"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/x-xpixmap") && XImageReader_isSupportedFormat("xpm")) { const char* value[] = {"xpm"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/jpeg") && XImageReader_isSupportedFormat("jpeg")) {
            const char* value[] = {"jpg", "jpeg", "jfif"};
            XImageReader_makeStringList_prefix(result, value, 3);
        }
        if (XImageReader_mimeEquals(mime, "image/svg+xml") && XImageReader_isSupportedFormat("svg")) { const char* value[] = {"svg"}; XImageReader_makeStringList_prefix(result, value, 1); }
        if (XImageReader_mimeEquals(mime, "image/svg+xml-compressed") && XImageReader_isSupportedFormat("svgz")) { const char* value[] = {"svgz"}; XImageReader_makeStringList_prefix(result, value, 1); }
#if XIMAGECODEC_ICO_ON
        if (XImageReader_mimeEquals(mime, "image/vnd.microsoft.icon") && XImageReader_isSupportedFormat("ico")) { const char* value[] = {"ico", "cur"}; XImageReader_makeStringList_prefix(result, value, 2); }
#endif
    }
#if XIMAGEIOPLUGIN_ON
    {
        XStringList* plugin = XImagePluginRegistry_imageFormatsForMimeType(mimeType, true);
        int64_t n;
        int64_t j;
        if (plugin) {
            n = XStringList_size_base((XContainer*)plugin);
            for (j = 0; j < n; ++j) {
                XString* item = (XString*)XStringList_at_base((XVector*)plugin, j);
                const char* value = XString_toUtf8(item);
                if (value && value[0] &&
                    !XStringList_contains_utf8(result, value, XChar_CaseSensitive))
                    XStringList_push_back_utf8(result, value);
            }
            XStringList_delete_base((XClass*)plugin);
        }
    }
#endif
    /* Qt 6.8 QImageReaderWriterHelpers::imageFormatsForMimeType() preserves
       the built-in table/plugin metadata order.  Do not sort this result:
       JPEG's declared Qt plugin order is jpg, jpeg, jfif.  The append helper
       already removes duplicate keys while retaining their first position. */
    return result;
}

XStringList* XImageReader_imageFormatsForMimeType_2(const char* mimeType)
{
    XString* value = mimeType ? XString_create_utf8(mimeType) : NULL;
    XStringList* result = XImageReader_imageFormatsForMimeType(value);
    if (value) XString_delete_base((XClass*)value);
    return result;
}
int XImageReader_allocationLimit() { return XImageReader_effectiveAllocationLimit(); }
void XImageReader_setAllocationLimit(int mbLimit)
{
    /* Qt 6.8 ignores negative values; zero explicitly disables the check. */
    if (mbLimit >= 0)
        g_imageReaderAllocationLimitMb = mbLimit;
}
