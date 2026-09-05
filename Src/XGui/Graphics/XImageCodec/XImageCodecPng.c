/*****************************************************************************/
/**
 * @file       XImageCodecPng.c
 * @brief      XImageCodec PNG 格式独立实现。
 * @note       受 XIMAGECODEC_PNG_ON 开关控制，可通过 XImageCodec_config.h
 *             单独裁剪；扩展能力分别受下列子开关控制：
 *               - XIMAGECODEC_PNG_PALETTE_ON：调色板（ColorType 3，位深
 *                 1/2/4/8）解码与 Indexed8 图像调色板编码，支持 tRNS 透明；
 *               - XIMAGECODEC_PNG_16BIT_ON：16 位深（灰度/RGB/灰度+Alpha/
 *                 RGBA）解码，输出 QImage 对齐的 Grayscale16/RGBX64/RGBA64；
 *               - XIMAGECODEC_PNG_INTERLACE_ON：Adam7 隔行解码与 1/2/4
 *                 位子采样展开。
 *             关闭上述子开关后，对应特性降级为旧有 8 位顺序解码能力
 *             （遇见不支持的 PNG 明确失败）。
 * @note       IDAT 使用库内 zlib 解压/压缩；块 CRC32 复用 XCrc 的
 *             ISO-HDLC 参数组（与 PNG 规范 CRC-32 完全一致）。
 *              只通过 XImageCodecInternal_decodePng/encodePng 对外暴露，
 *              统一由 XImageCodec.c 的 XImageCodec_decode/encode 分发。
 */
#include "XImageCodec_config.h"
#include "XImageCodecInternal.h"
#include "XMemory.h"
#include "XCrc.h"
#include "zlib.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#if XIMAGECODEC_ON
#if XIMAGECODEC_PNG_ON

/* PNG 色彩类型常量（规范 11.2.2）。 */
#define PNG_CT_GRAY       0u
#define PNG_CT_RGB        2u
#define PNG_CT_PALETTE    3u
#define PNG_CT_GRAY_ALPHA 4u
#define PNG_CT_RGBA       6u

/* 单块 CRC 数据的累计更新（先类型 4 字节，后负载数据）。 */
static uint32_t pngChunkCrc(const char type[4], const uint8_t* data, size_t size)
{
    uint32_t crc = XCrc32_calculate(XCrc32_Algorithm_IsoHdlc,
                                    (const uint8_t*)type, 4);
    if (size) crc = XCrc32_update(XCrc32_Algorithm_IsoHdlc, crc, data, size);
    return crc;
}

/* PNG iCCP 的 profile 数据使用 zlib wrapper 压缩。Qt 交给 libpng 后再
 * 传入 QColorSpace::fromIccProfile()；这里先做有界解压，保留原始 ICC
 * 字节供 XImage 的内部资源侧车保存。 */
#define PNG_MAX_ICC_PROFILE (16u * 1024u * 1024u)
#define PNG_MAX_TEXT_BYTES  (16u * 1024u * 1024u)
#define PNG_MAX_TEXT_ITEMS  1024u

/* 受上限约束地解压 ancillary 文本/ICC 数据，避免压缩炸弹耗尽内存。
 * PNG 文本允许空字符串，因此解压成功但长度为零时也返回 true。 */
static bool pngInflateBounded(const uint8_t* data, size_t size,
                              uint8_t** out, size_t* outSize,
                              size_t maxOutput)
{
    size_t capacity = 4096u;
    uint8_t* buffer = NULL;
    if (!data || !size || !out || !outSize ||
        maxOutput == 0 || size > (size_t)ULONG_MAX)
        return false;
    if (capacity > maxOutput) capacity = maxOutput;
    while (capacity <= maxOutput) {
        uLongf decodedSize;
        int result;
        buffer = (uint8_t*)XMalloc_System(capacity);
        if (!buffer) return false;
        decodedSize = (uLongf)capacity;
        result = uncompress(buffer, &decodedSize, data, (uLong)size);
        if (result == Z_OK) {
            *out = buffer;
            *outSize = (size_t)decodedSize;
            return true;
        }
        XFree_System(buffer);
        buffer = NULL;
        if (result != Z_BUF_ERROR || capacity == maxOutput)
            return false;
        if (capacity > maxOutput / 2u)
            capacity = maxOutput;
        else
            capacity *= 2u;
    }
    return false;
}

static bool pngInflateIccProfile(const uint8_t* data, size_t size,
                                 uint8_t** out, size_t* outSize)
{
    if (!pngInflateBounded(data, size, out, outSize,
                           (size_t)PNG_MAX_ICC_PROFILE))
        return false;
    if (!*outSize) {
        XFree_System(*out);
        *out = NULL;
        return false;
    }
    return true;
}

/* 将一个文本键值副本加入临时列表；XStringList 会深复制元素。 */
static bool pngAppendTextPair(XStringList* keys, XStringList* values,
                              XString* key, XString* value)
{
    size_t index;
    if (!keys || !values || !key || !value) return false;
    index = XStringList_size_base((const XContainer*)keys);
    if (!XStringList_insert_base((XVector*)keys, (int64_t)index, key))
        return false;
    if (!XStringList_insert_base((XVector*)values, (int64_t)index, value)) {
        XStringList_remove_base((XVector*)keys, (int64_t)index, 1);
        return false;
    }
    return true;
}

/* 文本元数据预扫描复用解码器后部定义的块名称/CRC 校验器。 */
static bool pngChunkTypeValid(const uint8_t* type);
static bool pngChunkCrcValid(const uint8_t* type, const uint8_t* data,
                             size_t size, bool required);

/* 解析 tEXt/zTXt/iTXt。无效块按 libpng 的 benign warning 语义忽略，
 * 但结构合法且解压成功的文本会保存为 XImage UTF-8 元数据。 */
static bool pngParseTextChunk(const char type[4], const uint8_t* data,
                              size_t size, XStringList* keys,
                              XStringList* values, size_t* totalBytes)
{
    size_t keyLen = 0, textOffset, textLen;
    uint8_t* inflated = NULL;
    XString* key = NULL;
    XString* value = NULL;
    bool compressed = false, itxt = false;
    if (!type || !data || !size || !keys || !values || !totalBytes)
        return false;
    while (keyLen < size && data[keyLen] != 0) {
        if (keyLen >= 79u) return false;
        ++keyLen;
    }
    if (keyLen == 0 || keyLen >= size) return false;
    textOffset = keyLen + 1u;
    if (!memcmp(type, "tEXt", 4)) {
        textLen = size - textOffset;
    } else if (!memcmp(type, "zTXt", 4)) {
        if (textOffset >= size || data[textOffset] != 0) return false;
        compressed = true;
        textOffset += 1u;
        if (textOffset >= size) return false;
        textLen = size - textOffset;
    } else if (!memcmp(type, "iTXt", 4)) {
        size_t languageEnd, translatedEnd;
        uint8_t compressionFlag, compressionMethod;
        if (textOffset + 2u > size) return false;
        compressionFlag = data[textOffset++];
        compressionMethod = data[textOffset++];
        if (compressionFlag > 1u || compressionMethod != 0u) return false;
        languageEnd = textOffset;
        while (languageEnd < size && data[languageEnd] != 0) ++languageEnd;
        if (languageEnd >= size) return false;
        translatedEnd = languageEnd + 1u;
        while (translatedEnd < size && data[translatedEnd] != 0) ++translatedEnd;
        if (translatedEnd >= size) return false;
        textOffset = translatedEnd + 1u;
        compressed = compressionFlag != 0;
        itxt = true;
        textLen = size - textOffset;
    } else {
        return false;
    }
    if (*totalBytes > (size_t)PNG_MAX_TEXT_BYTES -
                     (textLen > (size_t)PNG_MAX_TEXT_BYTES
                          ? (size_t)PNG_MAX_TEXT_BYTES : textLen))
        return false;
    if (compressed) {
        size_t decodedLen = 0;
        if (!pngInflateBounded(data + textOffset, textLen, &inflated,
                               &decodedLen, (size_t)PNG_MAX_TEXT_BYTES) ||
            decodedLen > (size_t)PNG_MAX_TEXT_BYTES - *totalBytes)
            goto done;
        textOffset = 0;
        textLen = decodedLen;
    }
    key = XString_create_with_length_latin1(data, keyLen);
    value = itxt
        ? XString_create_with_length_utf8(compressed ? (const char*)inflated
                                                     : (const char*)data + textOffset,
                                          textLen)
        : XString_create_with_length_latin1(compressed ? inflated
                                                          : data + textOffset,
                                            textLen);
    if (key && value &&
        XStringList_size_base((const XContainer*)keys) < PNG_MAX_TEXT_ITEMS &&
        pngAppendTextPair(keys, values, key, value)) {
        *totalBytes += textLen;
    }
done:
    if (key) XString_delete_base((XClass*)key);
    if (value) XString_delete_base((XClass*)value);
    if (inflated) XFree_System(inflated);
    return key != NULL && value != NULL;
}

/* 将已解析的文本列表按 QPngHandler::readPngTexts() 格式拼接为
 * Description；后续由 qt_getImageTextFromDescription 等价逻辑再次解析。 */
static bool pngBuildDescription(const XStringList* keys,
                                const XStringList* values, XString* out)
{
    size_t count;
    size_t i;
    if (!keys || !values || !out) return false;
    count = XStringList_size_base((const XContainer*)keys);
    if (count != XStringList_size_base((const XContainer*)values)) return false;
    for (i = 0; i < count; ++i) {
        const XString* key = (const XString*)XStringList_at_base(
            (XVector*)keys, (int64_t)i);
        const XString* value = (const XString*)XStringList_at_base(
            (XVector*)values, (int64_t)i);
        XString* simplified;
        const char* keyUtf8;
        const char* valueUtf8;
        if (!key || !value) continue;
        simplified = XString_simplified(value);
        if (!simplified) return false;
        keyUtf8 = XString_toUtf8(key);
        valueUtf8 = XString_toUtf8(simplified);
        if (!keyUtf8 || !valueUtf8 ||
            (!XString_isEmpty_base((const XContainer*)out) &&
             !XString_append_utf8(out, "\n\n")) ||
            !XString_append_with_length_utf8(out, keyUtf8,
                                             XString_toUtf8_length(key)) ||
            !XString_append_utf8(out, ": ") ||
            !XString_append_with_length_utf8(out, valueUtf8,
                                             XString_toUtf8_length(simplified))) {
            XString_delete_base((XClass*)simplified);
            return false;
        }
        XString_delete_base((XClass*)simplified);
    }
    return true;
}

bool XImageCodecInternal_extractPngDescription(const uint8_t* data,
                                               size_t size,
                                               XString* out)
{
    size_t pos = 8u;
    XStringList keys;
    XStringList values;
    size_t textBytes = 0;
    bool haveIhdr = false;
    bool haveIdat = false;
    bool ok = false;
    if (!data || size < 8u || !out || memcmp(data, "\x89PNG\r\n\x1a\n", 8))
        return false;
    XStringList_init(&keys);
    XStringList_init(&values);
    while (pos <= size && size - pos >= 12u) {
        uint32_t length = XImageCodecInternal_readU32BE(data + pos);
        const uint8_t* type = data + pos + 4u;
        const uint8_t* chunkData = data + pos + 8u;
        if (length > 0x7fffffffu || length > size - pos - 12u ||
            !pngChunkTypeValid(type))
            goto done;
        if (!haveIhdr && memcmp(type, "IHDR", 4) != 0)
            goto done;
        if (!memcmp(type, "IHDR", 4)) {
            if (haveIhdr || length != 13u ||
                !pngChunkCrcValid(type, chunkData, length, true))
                goto done;
            haveIhdr = true;
        } else if (!memcmp(type, "IDAT", 4)) {
            if (!haveIhdr || !pngChunkCrcValid(type, chunkData, length, true))
                goto done;
            haveIdat = true;
        } else if (!memcmp(type, "tEXt", 4) ||
                   !memcmp(type, "zTXt", 4) ||
                   !memcmp(type, "iTXt", 4)) {
            /* QPngHandler reads both info_ptr and end_info, so ancillary
             * text after IDAT is visible as well as text before IDAT. */
            if (pngChunkCrcValid(type, chunkData, length, true))
                (void)pngParseTextChunk((const char*)type, chunkData, length,
                                         &keys, &values, &textBytes);
        } else if (!memcmp(type, "IEND", 4)) {
            if (!haveIhdr || !haveIdat || length != 0u)
                goto done;
            ok = pngBuildDescription(&keys, &values, out);
            goto done;
        }
        pos += (size_t)length + 12u;
    }
done:
    XStringList_deinit_base((XClass*)&keys);
    XStringList_deinit_base((XClass*)&values);
    return ok;
}

bool XImageCodecInternal_extractPngGamma(const uint8_t* data,
                                         size_t size,
                                         float* out)
{
    size_t pos = 8u;
    bool haveIhdr = false;
    bool haveIdat = false;
    if (!data || size < 8u || !out || memcmp(data, "\x89PNG\r\n\x1a\n", 8))
        return false;
    *out = 0.0f;
    while (pos <= size && size - pos >= 12u) {
        uint32_t length = XImageCodecInternal_readU32BE(data + pos);
        const uint8_t* type = data + pos + 4u;
        const uint8_t* chunkData = data + pos + 8u;
        if (length > 0x7fffffffu || length > size - pos - 12u ||
            !pngChunkTypeValid(type))
            return false;
        if (!haveIhdr && memcmp(type, "IHDR", 4) != 0)
            return false;
        if (!memcmp(type, "IHDR", 4)) {
            if (haveIhdr || length != 13u ||
                !pngChunkCrcValid(type, chunkData, length, true))
                return false;
            haveIhdr = true;
        } else if (!memcmp(type, "IDAT", 4)) {
            if (!haveIhdr || !pngChunkCrcValid(type, chunkData, length, true))
                return false;
            haveIdat = true;
        } else if (!haveIdat && !memcmp(type, "gAMA", 4) && length == 4u &&
                   pngChunkCrcValid(type, chunkData, length, true)) {
            uint32_t raw = XImageCodecInternal_readU32BE(chunkData);
            if (raw != 0u) {
                *out = (float)raw / 100000.0f;
                return *out > 0.0f;
            }
        }
        pos += (size_t)length + 12u;
    }
    return false;
}

/* 解析 iCCP 块的名称、压缩方法及 profile 负载。名称只用于 Qt 描述
 * 回退，当前内部侧车保存原始 profile，不把名称伪装成颜色空间描述。 */
static bool pngParseIccProfile(const uint8_t* data, size_t size,
                               uint8_t** profile, size_t* profileSize)
{
    size_t nameSize = 0;
    if (!data || size < 3u || !profile || !profileSize) return false;
    while (nameSize < size && data[nameSize] != 0) {
        if (nameSize >= 79u) return false;
        ++nameSize;
    }
    if (nameSize == 0 || nameSize >= size || nameSize + 2u >= size ||
        data[nameSize + 1u] != 0)
        return false;
    return pngInflateIccProfile(data + nameSize + 2u,
                                size - nameSize - 2u,
                                profile, profileSize);
}

static bool pngAppendChunk(XByteArray* out, const char type[4],
                           const uint8_t* data, size_t size);

/* 将图像内部保存的 ICC profile 写成 PNG iCCP 块。没有 profile 时返回
 * true 且不追加任何块；这样普通图像的编码路径保持原有字节布局。 */
static bool pngAppendColorProfile(const XImage* image, XByteArray* out)
{
    XByteArray* profile = NULL;
    uint8_t* compressed = NULL;
    uint8_t* payload = NULL;
    const uint8_t* profileData;
    const char* description;
    XColorSpace colorSpace;
    size_t profileSize, nameSize = 0, payloadSize;
    uLongf compressedSize;
    bool ok = true;
    if (!image || !out) return false;
    profile = XByteArray_create();
    if (!profile) return false;
    if (!XImageCodecInternal_copyIccProfile(image, profile)) {
        XByteArray_delete_base((XClass*)profile);
        return false;
    }
    profileSize = XByteArray_size_base((const XContainer*)profile);
    if (profileSize == 0) {
        XByteArray_delete_base((XClass*)profile);
        return true;
    }
    profileData = (const uint8_t*)XByteArray_data(profile);
    colorSpace = XImage_colorSpace(image);
    description = XColorSpace_description(&colorSpace);
    if (!description || !description[0]) description = "Custom";
    while (description[nameSize] && nameSize < 79u) {
        unsigned char ch = (unsigned char)description[nameSize];
        if (ch < 0x20u || ch > 0x7eu) break;
        ++nameSize;
    }
    if (nameSize == 0) {
        description = "Custom";
        nameSize = 6u;
    }
    if (profileSize > (size_t)ULONG_MAX ||
        compressBound((uLong)profileSize) > (uLong)UINT32_MAX -
                           (uLong)(nameSize + 2u)) {
        ok = false;
        goto done;
    }
    compressedSize = compressBound((uLong)profileSize);
    compressed = (uint8_t*)XMalloc_System((size_t)compressedSize);
    payloadSize = nameSize + 2u + (size_t)compressedSize;
    payload = (uint8_t*)XMalloc_System(payloadSize);
    if (!compressed || !payload) {
        ok = false;
        goto done;
    }
    memcpy(payload, description, nameSize);
    payload[nameSize] = 0;
    payload[nameSize + 1u] = 0; /* PNG compression method: zlib */
    if (compress2(compressed, &compressedSize, profileData,
                  (uLong)profileSize, Z_DEFAULT_COMPRESSION) != Z_OK) {
        ok = false;
        goto done;
    }
    memcpy(payload + nameSize + 2u, compressed, (size_t)compressedSize);
    ok = pngAppendChunk(out, "iCCP", payload,
                        nameSize + 2u + (size_t)compressedSize);
done:
    if (compressed) XFree_System(compressed);
    if (payload) XFree_System(payload);
    XByteArray_delete_base((XClass*)profile);
    return ok;
}

/* 判断图像是否带有 ICC profile。Qt 在存在 iCCP 时不再额外写 gAMA；
 * 这里复用图像内部侧车复制接口，仅复制元数据，不触碰像素数据。 */
static bool pngImageHasIccProfile(const XImage* image)
{
    XByteArray* profile;
    bool result;
    if (!image) return false;
    profile = XByteArray_create();
    if (!profile) return false;
    result = XImageCodecInternal_copyIccProfile(image, profile) &&
             XByteArray_size_base((const XContainer*)profile) > 0;
    XByteArray_delete_base((XClass*)profile);
    return result;
}

/* 写出 Qt QPNGImageWriter::setGamma() 使用的 gAMA 块。参数是旧式
 * handler gamma；PNG 文件中保存其倒数乘以 100000 的定点值。 */
static bool pngAppendGamma(const XImage* image, XByteArray* out, float gamma)
{
    uint8_t payload[4];
    float fileGamma;
    uint32_t encoded;
    if (!image || !out || !(gamma > 0.0f) || gamma != gamma ||
        pngImageHasIccProfile(image))
        return true;
    fileGamma = 1.0f / gamma;
    if (!(fileGamma > 0.0f) || fileGamma != fileGamma ||
        fileGamma > 42949.67295f)
        return false;
    encoded = (uint32_t)(fileGamma * 100000.0f + 0.5f);
    if (encoded == 0u) return false;
    XImageCodecInternal_writeU32BE(payload, encoded);
    return pngAppendChunk(out, "gAMA", payload, sizeof(payload));
}

/* 写出 Qt QImage 使用的物理元数据。pHYs 的单位固定为米；oFFs 的单位
 * 固定为像素。没有设置对应元数据时不追加块，以保持普通 PNG 的布局。 */
static bool pngAppendPhysicalMetadata(const XImage* image, XByteArray* out)
{
    uint8_t payload[9];
    XPoint offset;
    int dpmX, dpmY;
    if (!image || !out) return false;
    XImage_offset(image, &offset);
    if (offset.x != 0 || offset.y != 0) {
        XImageCodecInternal_writeU32BE(payload, (uint32_t)(int32_t)offset.x);
        XImageCodecInternal_writeU32BE(payload + 4, (uint32_t)(int32_t)offset.y);
        payload[8] = 0; /* PNG_OFFSET_PIXEL */
        if (!pngAppendChunk(out, "oFFs", payload, sizeof(payload)))
            return false;
    }
    dpmX = XImage_dotsPerMeterX(image);
    dpmY = XImage_dotsPerMeterY(image);
    if (dpmX > 0 || dpmY > 0) {
        XImageCodecInternal_writeU32BE(payload, (uint32_t)(dpmX > 0 ? dpmX : 0));
        XImageCodecInternal_writeU32BE(payload + 4, (uint32_t)(dpmY > 0 ? dpmY : 0));
        payload[8] = 1; /* PNG_RESOLUTION_METER */
        if (!pngAppendChunk(out, "pHYs", payload, sizeof(payload)))
            return false;
    }
    return true;
}

/* 写一个完整的 PNG chunk。 */
static bool pngAppendChunk(XByteArray* out, const char type[4],
                           const uint8_t* data, size_t size)
{
    uint8_t header[8], crcData[4];
    if (size > UINT32_MAX) return false;
    XImageCodecInternal_writeU32BE(header, (uint32_t)size);
    memcpy(header + 4, type, 4);
    if (!XImageCodecInternal_appendBytes(out, header, sizeof(header)))
        return false;
    if (size && !XImageCodecInternal_appendBytes(out, data, size))
        return false;
    XImageCodecInternal_writeU32BE(crcData, pngChunkCrc(type, data, size));
    return XImageCodecInternal_appendBytes(out, crcData, sizeof(crcData));
}

/* 依据 Qt qpnghandler.cpp::set_text 的阈值和编码选择写出一个文本块。
 * 关键词按 PNG 规范限制为 1..79 个 Latin-1 字节；XImage 的 UTF-8 键若
 * 含非 ASCII 字节则跳过，避免产生 libpng 会拒绝的非法关键字。 */
static bool pngAppendTextChunk(XByteArray* out, const char* key,
                               size_t keyLen, const char* value,
                               size_t valueLen)
{
    bool needsItxt = false;
    bool compressText;
    uint8_t* compressed = NULL;
    uint8_t* payload = NULL;
    size_t prefix, payloadSize;
    uLongf compressedSize = 0;
    const char* chunkType;
    size_t i;
    if (!out || !key || !value || keyLen == 0 || keyLen > 79u ||
        valueLen > (size_t)PNG_MAX_TEXT_BYTES)
        return false;
    for (i = 0; i < keyLen; ++i) {
        unsigned char ch = (unsigned char)key[i];
        if (ch == 0 || ch > 0x7fu) return true; /* 非法键直接忽略 */
    }
    for (i = 0; i < valueLen; ++i) {
        unsigned char ch = (unsigned char)value[i];
        if (ch >= 0x80u || (ch < 0x20u && ch != '\n')) {
            needsItxt = true;
            break;
        }
    }
    compressText = valueLen >= 40u;
    if (needsItxt) {
        /* keyword\0, compression flag/method, language="UTF-8"\0,
         * translated keyword (same key)\0, text. */
        prefix = keyLen + 1u + 2u + 6u + keyLen + 1u;
        if (compressText) {
            if (compressBound((uLong)valueLen) > (uLong)SIZE_MAX - prefix)
                return false;
            compressedSize = compressBound((uLong)valueLen);
            compressed = (uint8_t*)XMalloc_System((size_t)compressedSize);
            if (!compressed || compress2(compressed, &compressedSize,
                                         (const uint8_t*)value,
                                         (uLong)valueLen,
                                         Z_DEFAULT_COMPRESSION) != Z_OK)
                goto done;
            payloadSize = prefix + (size_t)compressedSize;
        } else {
            if (valueLen > SIZE_MAX - prefix) return false;
            payloadSize = prefix + valueLen;
        }
        chunkType = "iTXt";
    } else {
        /* tEXt: keyword\0 + Latin-1 text；zTXt: keyword\0 + method +
         * zlib stream。这里 value 已确认是 ASCII，等价于 Latin-1。 */
        prefix = keyLen + 1u + (compressText ? 2u : 0u);
        if (compressText) {
            if (compressBound((uLong)valueLen) > (uLong)SIZE_MAX - prefix)
                return false;
            compressedSize = compressBound((uLong)valueLen);
            compressed = (uint8_t*)XMalloc_System((size_t)compressedSize);
            if (!compressed || compress2(compressed, &compressedSize,
                                         (const uint8_t*)value,
                                         (uLong)valueLen,
                                         Z_DEFAULT_COMPRESSION) != Z_OK)
                goto done;
            payloadSize = prefix + (size_t)compressedSize;
            chunkType = "zTXt";
        } else {
            if (valueLen > SIZE_MAX - prefix) return false;
            payloadSize = prefix + valueLen;
            chunkType = "tEXt";
        }
    }
    if (payloadSize > UINT32_MAX) goto done;
    payload = (uint8_t*)XMalloc_System(payloadSize ? payloadSize : 1u);
    if (!payload) goto done;
    memcpy(payload, key, keyLen);
    payload[keyLen] = 0;
    if (!strcmp(chunkType, "iTXt")) {
        size_t at = keyLen + 1u;
        payload[at++] = compressText ? 1u : 0u;
        payload[at++] = 0u;
        memcpy(payload + at, "UTF-8", 5u);
        at += 5u;
        payload[at++] = 0u;
        memcpy(payload + at, key, keyLen);
        at += keyLen;
        payload[at++] = 0u;
        if (compressText)
            memcpy(payload + at, compressed, (size_t)compressedSize);
        else if (valueLen)
            memcpy(payload + at, value, valueLen);
    } else if (!strcmp(chunkType, "zTXt")) {
        size_t at = keyLen + 1u;
        payload[at++] = 0u;
        memcpy(payload + at, compressed, (size_t)compressedSize);
    } else if (valueLen) {
        memcpy(payload + keyLen + 1u, value, valueLen);
    }
    {
        bool ok = pngAppendChunk(out, chunkType, payload, payloadSize);
        if (payload) XFree_System(payload);
        if (compressed) XFree_System(compressed);
        return ok;
    }
done:
    if (payload) XFree_System(payload);
    if (compressed) XFree_System(compressed);
    return false;
}

/* 将 XImage 文本元数据逐项写出。Qt 的 QMap 已按键排序；XImage_setText
 * 同样维护大小写敏感升序，因此直接按索引遍历即可保持稳定顺序。 */
static bool pngAppendTexts(const XImage* image, XByteArray* out)
{
    int count, i;
    if (!image || !out) return false;
    count = XImage_textCount(image);
    for (i = 0; i < count; ++i) {
        const XString* key = XImage_textKey_const(image, i);
        const XString* value;
        const char* keyUtf8;
        const char* valueUtf8;
        /* qt_getImageText() intentionally excludes QImage's empty key when
           constructing PNG text chunks; an empty writer key is represented
           by the Description pseudo-key instead. */
        if (!key || XString_isEmpty_base((const XContainer*)key)) continue;
        value = XImage_text_const(image, key);
        if (!value) continue;
        keyUtf8 = XString_toUtf8(key);
        valueUtf8 = XString_toUtf8(value);
        if (!keyUtf8 || !valueUtf8) continue;
        if (!pngAppendTextChunk(out, keyUtf8,
                                XString_toUtf8_length(key), valueUtf8,
                                XString_toUtf8_length(value)))
            return false;
    }
    return true;
}

/* 对已解压的扫描行做 PNG 反滤波（Filter 0..4）。
 * bitsPerPixel 用于子 8 位深图像：扫描行按位打包，行长不是
 * width*bytesPerPixel，而是 (width*bitsPerPixel+7)/8。 */
static bool pngUnfilter(uint8_t* rows, int width, int height,
                        size_t stride, size_t bytesPerPixel,
                        int bitsPerPixel)
{
    size_t row, x;
    size_t expected;
    if (!rows || !width || !height || bytesPerPixel == 0 ||
        bitsPerPixel <= 0)
        return false;
    expected = bitsPerPixel < 8
        ? ((size_t)width * (size_t)bitsPerPixel + 7u) / 8u + 1u
        : (size_t)width * bytesPerPixel + 1u;
    if (stride != expected)
        return false;
    for (row = 0; row < (size_t)height; ++row) {
        uint8_t* current = rows + row * stride;
        const uint8_t* prior = row ? current - stride : NULL;
        uint8_t filter = current[0];
        for (x = 0; x < stride - 1; ++x) {
            uint8_t left = x >= bytesPerPixel
                ? current[1 + x - bytesPerPixel] : 0;
            uint8_t up = prior ? prior[1 + x] : 0;
            uint8_t upLeft = prior && x >= bytesPerPixel
                ? prior[1 + x - bytesPerPixel] : 0;
            uint8_t predictor;
            switch (filter) {
                case 0: predictor = 0; break;
                case 1: predictor = left; break;
                case 2: predictor = up; break;
                case 3: predictor =
                    (uint8_t)(((unsigned)left + up) / 2u); break;
                case 4: {
                    int p = (int)left + (int)up - (int)upLeft;
                    int pa = p > (int)left ? p - (int)left : (int)left - p;
                    int pb = p > (int)up ? p - (int)up : (int)up - p;
                    int pc = p > (int)upLeft ? p - (int)upLeft
                                             : (int)upLeft - p;
                    predictor = pa <= pb && pa <= pc
                        ? left : (pb <= pc ? up : upLeft);
                    break;
                }
                default: return false;
            }
            current[1 + x] = (uint8_t)(current[1 + x] + predictor);
        }
    }
    return true;
}

#if XIMAGECODEC_PNG_INTERLACE_ON
/* Adam7 隔行参数（规范 8.2.2）：每遍的起点 (x,y) 与步长。 */
static const int pngAdam7StartX[7] = {0, 4, 0, 2, 0, 1, 0};
static const int pngAdam7StartY[7] = {0, 0, 4, 0, 2, 0, 1};
static const int pngAdam7StepX[7]  = {8, 8, 4, 4, 2, 2, 1};
static const int pngAdam7StepY[7]  = {8, 8, 8, 4, 4, 2, 2};
#endif /* XIMAGECODEC_PNG_INTERLACE_ON */

/* 由采样深计算单样本字节数（16 -> 2，其余 1）。 */
static size_t pngSampleBytes(int bitDepth)
{
    return bitDepth > 8 ? (size_t)2 : (size_t)1;
}

/* 计算一条扫描行的数据字节数（不含滤波字节），子字节深按位打包。 */
static size_t pngRowBytes(int width, int bitDepth, int channels)
{
    int bitsPerPixel = bitDepth * channels;
    if (width <= 0 || bitDepth <= 0 || channels <= 0)
        return 0;
    if (bitsPerPixel < 8) {
        if ((size_t)width > (SIZE_MAX - 7u) / (size_t)bitsPerPixel)
            return 0;
        return ((size_t)width * (size_t)bitsPerPixel + 7u) / 8u;
    }
    if ((size_t)width > SIZE_MAX / pngSampleBytes(bitDepth) /
                        (size_t)channels)
        return 0;
    return (size_t)width * pngSampleBytes(bitDepth) * (size_t)channels;
}

/* 判断 PNG chunk 名称是否符合 libpng 的四字母约束。
 * qpnghandler.cpp 通过 libpng 的 png_read_chunk_header() 读取所有块；
 * libpng 在 pngrutil.c:153-170 会拒绝非 ASCII 字母以及第三个字节为小写
 * 的名称。这里保持相同的保留位检查，避免把畸形名称误当作可忽略扩展。
 */
static bool pngChunkTypeValid(const uint8_t* type)
{
    int i;
    if (!type) return false;
    for (i = 0; i < 4; ++i) {
        if (!((type[i] >= (uint8_t)'A' && type[i] <= (uint8_t)'Z') ||
              (type[i] >= (uint8_t)'a' && type[i] <= (uint8_t)'z')))
            return false;
    }
    return type[2] >= (uint8_t)'A' && type[2] <= (uint8_t)'Z';
}

/* PNG 的关键块 CRC 错误会使 Qt 使用的 libpng 直接报错；可选块的 CRC
 * 错误在默认配置下只是警告，因此这里只对关键块执行硬失败校验。PLTE
 * 对灰度图是可忽略的辅助块，保持 libpng 对其 CRC 的宽松处理。 */
static bool pngChunkCrcValid(const uint8_t* type, const uint8_t* data,
                             size_t size, bool required)
{
    uint32_t stored;
    if (!required) return true;
    stored = XImageCodecInternal_readU32BE(data + size);
    return pngChunkCrc((const char*)type, data, size) == stored;
}

/* 从打包位中取出第 px 个样本（子 8 位深，MSB 优先）。 */
static int pngPackedSample(const uint8_t* row, int px, int bitDepth)
{
    int bitIndex = px * bitDepth;
    int byteIndex = bitIndex >> 3;
    int shift = 8 - bitDepth - (bitIndex & 7);
    int mask = (1 << bitDepth) - 1;
    return (row[byteIndex] >> shift) & mask;
}

/* 解码 PNG 数据到 XImage（支持扩展特性，见文件头注释）。 */
bool XImageCodecInternal_decodePng(const uint8_t* data, size_t size, XImage* out)
{
    size_t pos = 8, idatSize = 0, rawSize = 0, sampleBytes;
    uint8_t* idat = NULL;
    uint8_t* raw = NULL;
    uint16_t* samples = NULL;
    uint32_t width = 0, height = 0;
    uint8_t bitDepth = 0, colorType = 0, compression = 0, filter = 0,
            interlace = 0;
    int channels = 0, maxValue = 0;
    uint32_t palette[256];
    uint8_t trnsAlpha[256];
    uint8_t* iccProfile = NULL;
    size_t iccProfileSize = 0;
    int paletteCount = 0, trnsCount = 0;
    uint16_t trnsGray = 0, trnsR = 0, trnsG = 0, trnsB = 0;
    bool haveTrnsGray = false, haveTrnsRgb = false;
    bool haveIHDR = false, havePLTE = false, haveIDAT = false,
         haveIEND = false, haveTrns = false, idatEnded = false;
    bool haveIcc = false, haveSrgb = false, haveGamma = false,
         haveChrm = false;
    bool havePhys = false, haveOffset = false;
    uint32_t gammaRaw = 0;
    uint32_t pixelsPerMeterX = 0, pixelsPerMeterY = 0;
    int32_t offsetX = 0, offsetY = 0;
    XColorSpacePrimariesData chrm;
    XStringList textKeys;
    XStringList textValues;
    size_t textBytes = 0;
    XImage temp;
    bool tempInitialized = false;
    bool ok = false;

    if (!data || size < 33 || !out || memcmp(data, "\x89PNG\r\n\x1a\n", 8))
        return false;
    memset(palette, 0, sizeof(palette));
    memset(trnsAlpha, 0, sizeof(trnsAlpha));
    memset(&chrm, 0, sizeof(chrm));
    XStringList_init(&textKeys);
    XStringList_init(&textValues);

    while (pos <= size && size - pos >= 12u) {
        uint32_t length = XImageCodecInternal_readU32BE(data + pos);
        const uint8_t* type = data + pos + 4;
        const uint8_t* chunkData;
        bool isCritical;
        /* PNG 的 chunk 长度字段虽然占 32 位，但最高位必须为零；
         * libpng 的 png_read_chunk_header() 在 pngrutil.c:207-215
         * 会先拒绝 31 位以上长度，避免后续跳过/分配发生溢出。 */
        if (length > 0x7fffffffu) goto fail;
        if (length > size - pos - 12) goto fail;
        chunkData = data + pos + 8;
        isCritical = type[0] >= (uint8_t)'A' && type[0] <= (uint8_t)'Z';
        if (!pngChunkTypeValid(type)) goto fail;
        /* IHDR、IDAT 以及调色板图中的 PLTE 是关键数据，CRC 错误须
         * 失败。libpng 的 png_handle_IEND() 明确以
         * png_crc_finish_critical(..., 1) 按可选块处理 IEND，
         * 因而 IEND 的 CRC 只产生警告；非调色板图的 PLTE 同理。依据
         * pngrutil.c:1041-1044、1082-1107。 */
        if (isCritical && memcmp(type, "IEND", 4) != 0 &&
            (memcmp(type, "PLTE", 4) != 0 || colorType == PNG_CT_PALETTE) &&
            !pngChunkCrcValid(type, chunkData, length, true))
            goto fail;
        if (!haveIHDR && memcmp(type, "IHDR", 4) != 0) goto fail;
        /* libpng 在遇到 IDAT 以外的块后进入 PNG_AFTER_IDAT 状态；其后
         * 再出现 IDAT 会在 png_read_end 中被报告 benign warning 并丢弃。
         * 保持该状态，复现 pngread.c:699-744 的流式块顺序处理。 */
        if (haveIDAT && memcmp(type, "IDAT", 4) != 0)
            idatEnded = true;
        if (!memcmp(type, "IHDR", 4)) {
            /* libpng 的 CDIHDR 规则（pngrutil.c:3069-3075）要求 IHDR
             * 只能出现一次且长度必须精确为 13 字节。 */
            if (haveIHDR || length != 13u) goto fail;
            width = XImageCodecInternal_readU32BE(data + pos + 8);
            height = XImageCodecInternal_readU32BE(data + pos + 12);
            bitDepth = data[pos + 16]; colorType = data[pos + 17];
            compression = data[pos + 18]; filter = data[pos + 19];
            interlace = data[pos + 20];
            haveIHDR = true;
        } else if (!memcmp(type, "PLTE", 4)) {
            /* png_handle_PLTE() 对非调色板图把 PLTE 当作可选块：灰度
             * 图直接忽略；RGB/RGBA 图只有首个、位置正确且长度合法的
             * PLTE 才会保存，重复、越界或位置不对仅发 benign warning
             * 并继续（pngrutil.c:987-1017、1073-1084）。这些图像的
             * 实际像素不依赖 PLTE，故对被忽略的块跳过其负载即可。 */
            if ((colorType & 2u) == 0u ||
                (colorType != PNG_CT_PALETTE &&
                 (havePLTE || haveIDAT || haveTrns || length > 768u ||
                  (length % 3u) != 0u))) {
                /* RGB/RGBA 的空 PLTE 会进入 png_set_PLTE(0) 并触发
                 * png_error("Invalid palette")，而灰度图的同一块在
                 * png_handle_PLTE() 中仅被忽略；保持两者差异。 */
                if ((colorType == PNG_CT_RGB || colorType == PNG_CT_RGBA) &&
                    length == 0u)
                    goto fail;
                pos += (size_t)length + 12;
                continue;
            }
            /* 调色板图的 PLTE 是关键数据，重复、越界或出现在 IDAT
             * 之后会走 png_chunk_error（pngrutil.c:992-1002、1073-1077）。 */
            if (havePLTE || haveIDAT || length == 0u || length > 768u ||
                (length % 3u) != 0u)
                goto fail;
            const uint8_t* p = data + pos + 8;
            paletteCount = (int)(length / 3u);
            {
                int maxPalette = bitDepth <= 8 ? 1 << bitDepth : 256;
                if (paletteCount > maxPalette)
                    paletteCount = maxPalette;
            }
            for (int i = 0; i < paletteCount; ++i) {
                palette[i] = 0xff000000u |
                    ((uint32_t)p[i * 3] << 16) |
                    ((uint32_t)p[i * 3 + 1] << 8) |
                    (uint32_t)p[i * 3 + 2];
            }
            havePLTE = true;
        } else if (!memcmp(type, "tRNS", 4)) {
            /* tRNS 是 ancillary。libpng 对重复、位置、长度和 CRC
             * 异常均走 benign warning（pngrutil.c:1705-1771），读取仍
             * 可继续；只有完整且位置正确的块才改变透明语义。 */
            if (haveTrns || haveIDAT ||
                !pngChunkCrcValid(type, chunkData, length, true)) {
                pos += (size_t)length + 12;
                continue;
            }
            const uint8_t* p = data + pos + 8;
            if (colorType == PNG_CT_PALETTE) {
                /* png_handle_tRNS (libpng:1739-1753) 要求先有 PLTE，且
                 * 透明表非空、长度不超过有效调色板项数。 */
                if (!havePLTE || length == 0u || length > (uint32_t)paletteCount) {
                    pos += (size_t)length + 12;
                    continue;
                }
                trnsCount = (int)length;
                for (int i = 0; i < trnsCount; ++i)
                    trnsAlpha[i] = p[i];
            } else if (colorType == PNG_CT_GRAY && length == 2u) {
                trnsGray = (uint16_t)XImageCodecInternal_readU16BE(p);
                haveTrnsGray = true;
            } else if (colorType == PNG_CT_RGB && length == 6u) {
                trnsR = (uint16_t)XImageCodecInternal_readU16BE(p);
                trnsG = (uint16_t)XImageCodecInternal_readU16BE(p + 2);
                trnsB = (uint16_t)XImageCodecInternal_readU16BE(p + 4);
                haveTrnsRgb = true;
            } else if (colorType != PNG_CT_GRAY && colorType != PNG_CT_RGB) {
                pos += (size_t)length + 12;
                continue;
            } else {
                /* 灰度/RGB 的长度不精确时，libpng 只警告并忽略。 */
                pos += (size_t)length + 12;
                continue;
            }
            haveTrns = true;
        } else if (!memcmp(type, "iCCP", 4)) {
            /* png_handle_iCCP() 只接受首个 profile；重复块发出警告并
             * 保留先出现的数据。压缩失败按无效 profile 忽略，像 Qt
             * fromIccProfile() 返回无效 QColorSpace 时仍继续读图。 */
            if (!haveIcc && pngParseIccProfile(chunkData, length,
                                               &iccProfile, &iccProfileSize))
                haveIcc = true;
        } else if (!memcmp(type, "sRGB", 4)) {
            /* sRGB 的 rendering intent 不影响 QImage 色彩空间，但值必须
             * 在 PNG 规范 0..3 范围内；libpng 对非法值只报告错误并忽略。 */
            if (!haveSrgb && length == 1u && chunkData[0] <= 3u)
                haveSrgb = true;
        } else if (!memcmp(type, "gAMA", 4)) {
            /* gAMA 用 1e5 倍定点保存文件 gamma，Qt 传入的是其倒数。 */
            if (!haveGamma && length == 4u) {
                uint32_t rawGamma = XImageCodecInternal_readU32BE(chunkData);
                if (rawGamma != 0u) {
                    gammaRaw = rawGamma;
                    haveGamma = true;
                }
            }
        } else if (!memcmp(type, "cHRM", 4)) {
            /* cHRM 八个 1e5 倍定点坐标按 Qt 的 QPointF 传入；最终
             * 有效性由 XColorSpace_create_custom() 的范围校验负责。 */
            if (!haveChrm && length == 32u) {
                chrm.m_whitePoint.x = (float)XImageCodecInternal_readU32BE(chunkData) / 100000.0f;
                chrm.m_whitePoint.y = (float)XImageCodecInternal_readU32BE(chunkData + 4) / 100000.0f;
                chrm.m_redPoint.x = (float)XImageCodecInternal_readU32BE(chunkData + 8) / 100000.0f;
                chrm.m_redPoint.y = (float)XImageCodecInternal_readU32BE(chunkData + 12) / 100000.0f;
                chrm.m_greenPoint.x = (float)XImageCodecInternal_readU32BE(chunkData + 16) / 100000.0f;
                chrm.m_greenPoint.y = (float)XImageCodecInternal_readU32BE(chunkData + 20) / 100000.0f;
                chrm.m_bluePoint.x = (float)XImageCodecInternal_readU32BE(chunkData + 24) / 100000.0f;
                chrm.m_bluePoint.y = (float)XImageCodecInternal_readU32BE(chunkData + 28) / 100000.0f;
                haveChrm = true;
            }
        } else if (!memcmp(type, "pHYs", 4)) {
            /* qpnghandler.cpp 通过 png_get_x/y_pixels_per_meter() 读取
             * 米制分辨率；非米制 pHYs 不改变 QImage 的 DPM。 */
            if (!havePhys && length == 9u &&
                pngChunkCrcValid(type, chunkData, length, true) &&
                chunkData[8] == 1u) {
                pixelsPerMeterX = XImageCodecInternal_readU32BE(chunkData);
                pixelsPerMeterY = XImageCodecInternal_readU32BE(chunkData + 4);
                havePhys = true;
            }
        } else if (!memcmp(type, "oFFs", 4)) {
            /* Qt 仅在 unit_type == PNG_OFFSET_PIXEL 时设置 offset；负值
             * 通过有符号 32 位 TIFF/PNG 字段原样保留。 */
            if (!haveOffset && length == 9u &&
                pngChunkCrcValid(type, chunkData, length, true) &&
                chunkData[8] == 0u) {
                offsetX = (int32_t)XImageCodecInternal_readU32BE(chunkData);
                offsetY = (int32_t)XImageCodecInternal_readU32BE(chunkData + 4);
                haveOffset = true;
            }
        } else if (!memcmp(type, "tEXt", 4) ||
                   !memcmp(type, "zTXt", 4) ||
                   !memcmp(type, "iTXt", 4)) {
            /* Qt 通过 libpng 在 info/end 两阶段读取文本；坏 CRC 的
             * ancillary 块只告警并忽略，合法重复键由 XImage_setText
             * 在转移阶段按最后一次出现的值覆盖。 */
            if (pngChunkCrcValid(type, chunkData, length, true))
                (void)pngParseTextChunk((const char*)type, chunkData,
                                        length, &textKeys, &textValues,
                                        &textBytes);
        } else if (!memcmp(type, "IDAT", 4)) {
            if (!haveIHDR ||
                (colorType == PNG_CT_PALETTE && !havePLTE))
                goto fail;
            if (!idatEnded) {
                if (length > SIZE_MAX - idatSize) goto fail;
                uint8_t* next = (uint8_t*)XRealloc_System(idat, idatSize + length);
                if (!next) goto fail;
                idat = next;
                memcpy(idat + idatSize, data + pos + 8, length);
                idatSize += length;
                haveIDAT = true;
            }
        } else if (!memcmp(type, "IEND", 4)) {
            if (!haveIHDR || !haveIDAT) goto fail;
            /* png_handle_IEND() 将非零长度和 CRC 作为 benign warning，
             * 仍设置 PNG_HAVE_IEND 并结束读取（pngrutil.c:1097-1109）。 */
            haveIEND = true;
            pos += 12u;
            break;
        } else if (isCritical) {
            /* 未知关键块正是 libpng:2980-2981 的硬错误条件；不能像
             * ancillary 扩展块一样静默跳过。 */
            goto fail;
        }
        pos += (size_t)length + 12;
    }

    /* 头字段校验 */
    if (!haveIHDR || !haveIDAT || !haveIEND || !width || !height ||
        width > INT_MAX || height > INT_MAX ||
        compression != 0 || filter != 0 || !idatSize ||
        (interlace != 0 && interlace != 1))
        goto fail;

    /* 色彩类型/位深合法性（规范 11.2.2 表 11.1）。 */
    switch (colorType) {
        case PNG_CT_GRAY:
            if (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 ||
                bitDepth == 8 || bitDepth == 16)
                channels = 1;
            break;
        case PNG_CT_RGB:
            if (bitDepth == 8 || bitDepth == 16) channels = 3;
            break;
        case PNG_CT_PALETTE:
            if (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 ||
                bitDepth == 8)
                channels = 1;
            break;
        case PNG_CT_GRAY_ALPHA:
            if (bitDepth == 8 || bitDepth == 16) channels = 2;
            break;
        case PNG_CT_RGBA:
            if (bitDepth == 8 || bitDepth == 16) channels = 4;
            break;
        default:
            break;
    }
    if (channels <= 0) goto fail;

#if XIMAGECODEC_PNG_PALETTE_ON
    if (colorType == PNG_CT_PALETTE && paletteCount <= 0) goto fail;
#else
    if (colorType == PNG_CT_PALETTE) goto fail;
#endif
#if XIMAGECODEC_PNG_16BIT_ON
    if (bitDepth == 16 && colorType == PNG_CT_PALETTE) goto fail;
#else
    if (bitDepth == 16) goto fail;
#endif
#if XIMAGECODEC_PNG_INTERLACE_ON
    (void)interlace;
#else
    if (interlace != 0) goto fail;
#endif

    sampleBytes = pngSampleBytes(bitDepth);
    maxValue = bitDepth == 16 ? 65535 : (1 << bitDepth) - 1;

    /* 计算每个 pass 的原始扫描流大小并一次性分配。 */
    {
        size_t passData[7] = {0};
        int passCount;
#if XIMAGECODEC_PNG_INTERLACE_ON
        if (interlace == 1) {
            passCount = 7;
            for (int i = 0; i < 7; ++i) {
                size_t pw, ph;
                pw = (size_t)pngAdam7StepX[i];
                ph = (size_t)pngAdam7StepY[i];
                pw = width > (uint32_t)pngAdam7StartX[i]
                    ? ((size_t)width - (size_t)pngAdam7StartX[i] + pw - 1) / pw : 0;
                ph = height > (uint32_t)pngAdam7StartY[i]
                    ? ((size_t)height - (size_t)pngAdam7StartY[i] + ph - 1) / ph : 0;
                if (pw && ph) {
                    size_t rowBytes = pngRowBytes((int)pw, bitDepth, channels);
                    size_t stride;
                    if (!rowBytes || rowBytes == SIZE_MAX) goto fail;
                    stride = rowBytes + 1u;
                    if ((uintmax_t)stride * (uintmax_t)ph >
                        (uintmax_t)UINT_MAX) goto fail;
                    passData[i] = stride * ph;
                }
            }
        } else
#endif
        {
            size_t rowBytes = pngRowBytes((int)width, bitDepth, channels);
            passCount = 1;
            if (!rowBytes || rowBytes == SIZE_MAX ||
                (uintmax_t)(rowBytes + 1u) * (uintmax_t)height >
                    (uintmax_t)UINT_MAX)
                goto fail;
            passData[0] = (rowBytes + 1u) * (size_t)height;
        }
        for (int i = 0; i < passCount; ++i) {
            if (passData[i] > SIZE_MAX - rawSize) goto fail;
            rawSize += passData[i];
        }
        if (rawSize == 0 || rawSize > UINT_MAX) goto fail;
    }

    raw = (uint8_t*)XMalloc_System(rawSize);
    if (!raw) goto fail;
    {
        uLongf inflated = (uLongf)rawSize;
        if ((uintmax_t)idatSize > (uintmax_t)ULONG_MAX ||
            uncompress(raw, &inflated, idat, (uLong)idatSize) != Z_OK ||
            inflated != rawSize)
            goto fail;
    }

    if ((size_t)width > (size_t)INT_MAX / (size_t)height / (size_t)channels ||
        (size_t)width * height * (size_t)channels >
            (size_t)SIZE_MAX / sizeof(uint16_t))
        goto fail;
    samples = (uint16_t*)XMalloc_System(
        (size_t)width * (size_t)height * (size_t)channels * sizeof(uint16_t));
    if (!samples) goto fail;

    /* 逐 pass：反滤波 -> 展开到最终坐标。 */
    {
        size_t offset = 0;
        int passCount;
        int startX[7], startY[7], stepX[7], stepY[7];
#if XIMAGECODEC_PNG_INTERLACE_ON
        if (interlace == 1) {
            passCount = 7;
            memcpy(startX, pngAdam7StartX, sizeof(startX));
            memcpy(startY, pngAdam7StartY, sizeof(startY));
            memcpy(stepX, pngAdam7StepX, sizeof(stepX));
            memcpy(stepY, pngAdam7StepY, sizeof(stepY));
        } else
#endif
        {
            passCount = 1;
            startX[0] = 0; startY[0] = 0; stepX[0] = 1; stepY[0] = 1;
        }
        for (int pass = 0; pass < passCount; ++pass) {
            size_t pw, ph;
            if (interlace == 0) { pw = width; ph = height; }
#if XIMAGECODEC_PNG_INTERLACE_ON
            else {
                pw = (size_t)pngAdam7StepX[pass];
                ph = (size_t)pngAdam7StepY[pass];
                pw = width > (uint32_t)startX[pass]
                    ? ((size_t)width - (size_t)startX[pass] + pw - 1) / pw : 0;
                ph = height > (uint32_t)startY[pass]
                    ? ((size_t)height - (size_t)startY[pass] + ph - 1) / ph : 0;
            }
#endif
            if (pw == 0 || ph == 0) continue;
            {
                size_t rowBytes = pngRowBytes((int)pw, bitDepth, channels);
                size_t stride;
                size_t passBytes;
                const uint8_t* passRaw = raw + offset;
                uint16_t* dst = samples;
                if (!rowBytes || rowBytes == SIZE_MAX) goto fail;
                stride = rowBytes + 1u;
                if ((uintmax_t)stride * (uintmax_t)ph >
                    (uintmax_t)SIZE_MAX)
                    goto fail;
                passBytes = stride * ph;
                if (!pngUnfilter((uint8_t*)passRaw, (int)pw, (int)ph,
                                 stride, channels * sampleBytes,
                                 bitDepth * channels))
                    goto fail;
                for (size_t py = 0; py < ph; ++py) {
                    const uint8_t* src = passRaw + py * stride + 1;
                    for (size_t pxx = 0; pxx < pw; ++pxx) {
                        size_t fx = (size_t)startX[pass] + pxx * (size_t)stepX[pass];
                        size_t fy = (size_t)startY[pass] + py * (size_t)stepY[pass];
                        uint16_t* pixel = dst + (fy * (size_t)width + fx) * (size_t)channels;
                        if (bitDepth < 8) {
                            int s = pngPackedSample(src, (int)pxx, bitDepth);
                            /* 调色板存索引；灰度存原采样值（输出阶段统一展开）。
                               注意不可在此预展开，否则 ARGB32 输出会按位深二次展开。 */
                            pixel[0] = (uint16_t)s;

                        } else if (bitDepth == 8) {
                            for (int c = 0; c < channels; ++c)
                                pixel[c] = src[pxx * (size_t)channels + (size_t)c];
                        } else {
                            for (int c = 0; c < channels; ++c) {
                                const uint8_t* sp =
                                    src + pxx * (size_t)channels * 2 + (size_t)c * 2;
                                pixel[c] = (uint16_t)(((uint16_t)sp[0] << 8) | sp[1]);
                            }
                        }
                    }
                }
                offset += passBytes;
            }
        }
        if (offset != rawSize) goto fail;
    }

    /* 输出图像。 */
#if XIMAGECODEC_PNG_PALETTE_ON
    if (colorType == PNG_CT_PALETTE) {
        uint32_t indexedColors[256];
        int i;
        /* Qt qpnghandler.cpp:258-286 对调色板 PNG 始终保留索引存储：
         * 位深为 1 时使用 Format_Mono，其余位深使用 Format_Indexed8。
         * tRNS 只更新颜色表中的 Alpha，不会把图像改成 ARGB32；这样
         * image.colorCount()/pixelIndex() 与 Qt 一致，透明调色板项仍可
         * 通过 color() 观察到。 */
        for (i = 0; i < paletteCount; ++i) {
            uint8_t alpha = trnsCount > i ? trnsAlpha[i] : 255u;
            indexedColors[i] = (palette[i] & 0x00ffffffu) |
                               ((uint32_t)alpha << 24);
        }
        if (bitDepth == 1) {
            uint32_t monoColors[2] = {0xff000000u, 0xff000000u};
            tempInitialized = true;
            XImage_init_ex(&temp, (int)width, (int)height,
                           XImageFormat_Mono);
            if (XImage_isNull(&temp)) goto fail;
            /* Qt 为 Format_Mono 始终建立两个颜色表槽位；畸形输入若仅
             * 提供一个 PLTE 项，第二项仍保留默认黑色而不是缩短表。 */
            for (i = 0; i < paletteCount && i < 2; ++i)
                monoColors[i] = indexedColors[i];
            XImage_setColorTable(&temp, monoColors, 2);
            for (int y = 0; y < (int)height; ++y)
                for (int x = 0; x < (int)width; ++x)
                    XImage_setPixel(&temp, x, y,
                                    (uint32_t)samples[(size_t)y * width + x]);
        } else {
            tempInitialized = true;
            XImage_init_ex(&temp, (int)width, (int)height,
                           XImageFormat_Indexed8);
            if (XImage_isNull(&temp)) goto fail;
            XImage_setColorTable(&temp, indexedColors, paletteCount);
            for (int y = 0; y < (int)height; ++y) {
                for (int x = 0; x < (int)width; ++x) {
                    int idx = samples[(size_t)y * width + x];
                    XImage_setPixel(&temp, x, y, (uint32_t)idx);
                }
            }
        }
    } else
#endif
    if (colorType == PNG_CT_GRAY && bitDepth == 1) {
        /* Qt qpnghandler.cpp:203-218 先调用 png_set_invert_mono()，
         * 再以 Format_Mono 分配，并约定颜色表索引 0 为白、索引 1
         * 为黑。samples 保存的是反滤波后的 PNG 原始样本，所以写入
         * XImage 前必须反转 0/1，才能与 libpng 的位反转结果相同。
         * 灰度 tRNS 的样本值来自 qpnghandler.cpp:211-218，透明度只
         * 作用于对应颜色表项，不改变像素索引。 */
        tempInitialized = true;
        XImage_init_ex(&temp, (int)width, (int)height, XImageFormat_Mono);
        if (XImage_isNull(&temp)) goto fail;
        {
            uint32_t monoColors[2] = {0xffffffffu, 0xff000000u};
            if (haveTrnsGray && trnsGray < 2u)
                monoColors[trnsGray] = 0x00000000u;
            XImage_setColorTable(&temp, monoColors, 2);
        }
        for (int y = 0; y < (int)height; ++y)
            for (int x = 0; x < (int)width; ++x)
                XImage_setPixel(&temp, x, y,
                                1u - (uint32_t)samples[(size_t)y * width + x]);
    } else
#if XIMAGECODEC_PNG_16BIT_ON
    if (bitDepth == 16) {
        bool hasTransparentColor = (colorType == PNG_CT_GRAY &&
                                    haveTrnsGray) ||
                                   (colorType == PNG_CT_RGB && haveTrnsRgb);
        XImageFormat fmt = colorType == PNG_CT_GRAY && !hasTransparentColor
            ? XImageFormat_Grayscale16
            : XImageFormat_RGBA64;
        if (colorType == PNG_CT_RGB && !hasTransparentColor)
            fmt = XImageFormat_RGBX64;
        tempInitialized = true;
        XImage_init_ex(&temp, (int)width, (int)height, fmt);
        if (XImage_isNull(&temp)) goto fail;
        for (int y = 0; y < (int)height; ++y) {
            uint8_t* row = XImage_scanLine(&temp, y);
            const uint16_t* src = samples + (size_t)y * width * (size_t)channels;
            if (channels == 1 && !hasTransparentColor) {
                for (int x = 0; x < (int)width; ++x) {
                    uint16_t v = src[x];
                    memcpy(row + (size_t)x * 2, &v, 2);
                }
            } else {
                for (int x = 0; x < (int)width; ++x) {
                    uint16_t tmp[4];
                    int c;
                    for (c = 0; c < (int)channels; ++c)
                        tmp[c] = src[x * channels + c];
                    if (colorType == PNG_CT_GRAY && hasTransparentColor) {
                        /* 与 qpnghandler.cpp:220-239 相同，16 位灰度
                         * tRNS 必须扩展成 RGB+A，而非丢弃透明样本。 */
                        uint16_t gray = tmp[0];
                        tmp[0] = tmp[1] = tmp[2] = gray;
                        tmp[3] = gray == trnsGray ? 0u : 0xffffu;
                    } else if (colorType == PNG_CT_RGB && hasTransparentColor) {
                        /* qpnghandler.cpp:228-239 对 RGB tRNS 采用
                         * RGBA64；透明色比较的是未缩放的 16 位样本。 */
                        tmp[3] = tmp[0] == trnsR && tmp[1] == trnsG &&
                                 tmp[2] == trnsB ? 0u : 0xffffu;
                    } else if (colorType == PNG_CT_GRAY_ALPHA) {
                        /* 灰度+Alpha：RGB 三通道复制灰度值，Alpha 通道保留。 */
                        uint16_t gray = tmp[0], alpha = tmp[1];
                        tmp[1] = tmp[2] = gray;
                        tmp[3] = alpha;
                    }
                    if (colorType == PNG_CT_RGB && !hasTransparentColor)
                        tmp[3] = 0xffffu;
                    for (c = 0; c < 4; ++c)
                        memcpy(row + (size_t)x * 8 + (size_t)c * 2, &tmp[c], 2);
                }
            }
        }
    } else
#endif
    {
        tempInitialized = true;
        XImage_init_ex(&temp, (int)width, (int)height, XImageFormat_ARGB32);
        if (XImage_isNull(&temp)) goto fail;
        for (int y = 0; y < (int)height; ++y) {
            const uint16_t* src = samples + (size_t)y * width * (size_t)channels;
            for (int x = 0; x < (int)width; ++x) {
                uint32_t c;
                uint8_t r, g, b, a = 255;
                int isTransparent = 0;
                if (colorType == PNG_CT_GRAY) {
                    uint16_t v = src[x];
                    if (haveTrnsGray && v == trnsGray) isTransparent = 1;
                    r = g = b = (uint8_t)((v * 255u + maxValue / 2u) / (uint32_t)maxValue);
                } else if (colorType == PNG_CT_RGB) {
                    r = (uint8_t)((src[x * 3] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                    g = (uint8_t)((src[x * 3 + 1] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                    b = (uint8_t)((src[x * 3 + 2] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                    if (haveTrnsRgb && src[x * 3] == trnsR &&
                        src[x * 3 + 1] == trnsG && src[x * 3 + 2] == trnsB)
                        isTransparent = 1;
                } else if (colorType == PNG_CT_GRAY_ALPHA) {
                    uint16_t v = src[x * 2];
                    r = g = b = (uint8_t)((v * 255u + maxValue / 2u) / (uint32_t)maxValue);
                    a = (uint8_t)((src[x * 2 + 1] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                } else {
                    r = (uint8_t)((src[x * 4] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                    g = (uint8_t)((src[x * 4 + 1] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                    b = (uint8_t)((src[x * 4 + 2] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                    a = (uint8_t)((src[x * 4 + 3] * 255u + maxValue / 2u) / (uint32_t)maxValue);
                }
                c = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                    ((uint32_t)g << 8) | b;
                if (isTransparent) c &= 0x00ffffffu;
                XImage_setPixel(&temp, x, y, c);
            }
        }
    }
    /* Qt 的色彩空间优先级为 iCCP > sRGB > gAMA+cHRM。iCCP 的语义
     * 解析由后续 profile 解析器负责；当前先确保原始资源不丢失。 */
    if (!haveIcc && haveSrgb) {
        XImage_setColorSpace(&temp, XColorSpace_sRgb());
    } else if (!haveIcc && haveGamma) {
        XColorSpace colorSpace;
        float gamma = 100000.0f / (float)gammaRaw;
        if (haveChrm)
            colorSpace = XColorSpace_create_custom(&chrm,
                                                   XColorSpaceTransfer_Gamma,
                                                   gamma);
        else
            colorSpace = XColorSpace_create_ex(XColorSpacePrimaries_SRgb,
                                                XColorSpaceTransfer_Gamma,
                                                gamma);
        if (XColorSpace_isValid(&colorSpace))
            XImage_setColorSpace(&temp, colorSpace);
    }
    if (haveIcc) {
        XImageColorProfileSpec profileSpec;
        memset(&profileSpec, 0, sizeof(profileSpec));
        profileSpec.m_iccData = iccProfile;
        profileSpec.m_iccSize = iccProfileSize;
        (void)XImageCodecInternal_setColorProfile(&temp, &profileSpec);
    }
    if (havePhys) {
        /* XImage 使用 int，与 Qt QImage 的公开 API 相同；超出 int 范围
         * 的 PNG 值无法表达时按上限截断，避免实现定义的窄化溢出。 */
        XImage_setDotsPerMeterX(&temp,
                                pixelsPerMeterX > (uint32_t)INT_MAX
                                    ? INT_MAX : (int)pixelsPerMeterX);
        XImage_setDotsPerMeterY(&temp,
                                pixelsPerMeterY > (uint32_t)INT_MAX
                                    ? INT_MAX : (int)pixelsPerMeterY);
    }
    if (haveOffset) {
        XPoint imageOffset;
        imageOffset.x = (int)offsetX;
        imageOffset.y = (int)offsetY;
        XImage_setOffset(&temp, &imageOffset);
    }
    {
        size_t textCount = XStringList_size_base((const XContainer*)&textKeys);
        size_t i;
        for (i = 0; i < textCount; ++i) {
            const XString* key = (const XString*)XStringList_at_base(
                (XVector*)&textKeys, (int64_t)i);
            const XString* value = (const XString*)XStringList_at_base(
                (XVector*)&textValues, (int64_t)i);
            if (key && value) XImage_setText(&temp, key, value);
        }
    }
    XStringList_deinit_base((XClass*)&textKeys);
    XStringList_deinit_base((XClass*)&textValues);
    if (iccProfile) {
        XFree_System(iccProfile);
        iccProfile = NULL;
    }
    XImage_move_base(out, &temp);
    ok = true;

fail:
    if (!ok && tempInitialized) XImage_deinit_base(&temp);
    XStringList_deinit_base((XClass*)&textKeys);
    XStringList_deinit_base((XClass*)&textValues);
    if (iccProfile) XFree_System(iccProfile);
    if (samples) XFree_System(samples);
    if (raw) XFree_System(raw);
    if (idat) XFree_System(idat);
    return ok;
}

#if XIMAGECODEC_PNG_PALETTE_ON
/* 将 Indexed8 图像编码为调色板 PNG（ColorType 3 + PLTE + tRNS）。 */
static bool pngEncodeIndexed(const XImage* image, int compressionLevel,
                             float gamma, XByteArray* out)
{
    uint8_t ihdr[13];
    uint8_t* raw = NULL;
    uint8_t* compressed = NULL;
    uint8_t* plte = NULL;
    uint8_t* trns = NULL;
    uLongf compressedSize;
    int width, height, colorCount, trnsLen = 0;
    size_t rawSize;
    bool ok = false;

    width = XImage_width(image);
    height = XImage_height(image);
    colorCount = XImage_colorCount(image);
    if (width <= 0 || height <= 0 || colorCount <= 0 || colorCount > 256)
        return false;

    {
        uint32_t colors[256];
        int got = XImage_colorTable(image, colors, 256);
        if (got != colorCount) return false;
        plte = (uint8_t*)XMalloc_System((size_t)colorCount * 3u);
        trns = trnsLen == 0 ? (uint8_t*)XMalloc_System((size_t)colorCount) : trns;
        if (!plte || !trns) goto done;
        for (int i = 0; i < colorCount; ++i) {
            plte[i * 3] = (uint8_t)(colors[i] >> 16);
            plte[i * 3 + 1] = (uint8_t)(colors[i] >> 8);
            plte[i * 3 + 2] = (uint8_t)colors[i];
            trns[i] = (uint8_t)(colors[i] >> 24);
            if (trns[i] < 255u) trnsLen = i + 1;
        }
    }

    rawSize = (size_t)width + 1;
    if ((size_t)height > SIZE_MAX / rawSize) goto done;
    rawSize *= (size_t)height;
    raw = (uint8_t*)XMalloc_System(rawSize);
    if (!raw || rawSize > UINT_MAX) goto done;
    for (int y = 0; y < height; ++y) {
        uint8_t* row = raw + (size_t)y * ((size_t)width + 1);
        row[0] = 0;
        for (int x = 0; x < width; ++x) {
            int idx = XImage_pixelIndex(image, x, y);
            if (idx < 0) idx = 0;
            if (idx >= 256) idx = 0;
            row[1 + x] = (uint8_t)idx;
        }
    }
    compressedSize = compressBound((uLong)rawSize);
    compressed = (uint8_t*)XMalloc_System((size_t)compressedSize);
    if (!compressed || compress2(compressed, &compressedSize, raw,
                                 (uLong)rawSize, compressionLevel) != Z_OK)
        goto done;
    if (!XByteArray_resize_base((XVector*)out, 0)) goto done;
    memset(ihdr, 0, sizeof(ihdr));
    XImageCodecInternal_writeU32BE(ihdr, (uint32_t)width);
    XImageCodecInternal_writeU32BE(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8; ihdr[9] = 3;
    if (!XImageCodecInternal_appendBytes(out, "\x89PNG\r\n\x1a\n", 8) ||
        !pngAppendChunk(out, "IHDR", ihdr, sizeof(ihdr)) ||
        !pngAppendChunk(out, "PLTE", plte, (size_t)colorCount * 3u))
        goto done;
    if (trnsLen > 0 && !pngAppendChunk(out, "tRNS", trns, (size_t)trnsLen))
        goto done;
    if (!pngAppendColorProfile(image, out)) goto done;
    if (!pngAppendGamma(image, out, gamma)) goto done;
    if (!pngAppendPhysicalMetadata(image, out)) goto done;
    if (!pngAppendTexts(image, out)) goto done;
    if (!pngAppendChunk(out, "IDAT", compressed, (size_t)compressedSize) ||
        !pngAppendChunk(out, "IEND", NULL, 0))
        goto done;
    ok = true;
done:
    if (raw) XFree_System(raw);
    if (compressed) XFree_System(compressed);
    if (plte) XFree_System(plte);
    if (trns) XFree_System(trns);
    return ok;
}
#endif /* XIMAGECODEC_PNG_PALETTE_ON */

static int pngCompressionLevel(int quality, int compression)
{
    int mapped = compression;
    if (mapped >= 0) {
        if (mapped > 100) mapped = 100;
    } else if (quality >= 0) {
        mapped = 100 - (quality > 100 ? 100 : quality);
    }
    if (mapped >= 0)
        return (mapped * 9) / 91;
    return Z_DEFAULT_COMPRESSION;
}

bool XImageCodecInternal_encodePngOptions(const XImage* image,
                                          int quality, int compression,
                                          float gamma,
                                          const XString* description,
                                          XByteArray* out)
{
    uint8_t ihdr[13];
    uint8_t* raw = NULL;
    uint8_t* compressed = NULL;
    size_t stride, rawSize;
    uLongf compressedSize;
    int width, height;
    bool ok = false;
    XImage decorated;
    const XImage* source = image;
    bool decoratedInitialized = false;
    int compressionLevel;
    if (!image || !out || XImage_isNull(image)) return false;
    if (description &&
        !XString_isEmpty_base((const XContainer*)description)) {
        XImage_init(&decorated);
        XImage_copy_base(&decorated, image);
        if (!decorated.m_data ||
            !XImage_applyTextDescription(&decorated, description)) {
            XImage_deinit_base(&decorated);
            return false;
        }
        source = &decorated;
        decoratedInitialized = true;
    }
    compressionLevel = pngCompressionLevel(quality, compression);
#if XIMAGECODEC_PNG_PALETTE_ON
    if (XImage_format(source) == XImageFormat_Indexed8) {
        bool indexedOk = pngEncodeIndexed(source, compressionLevel, gamma, out);
        if (decoratedInitialized) XImage_deinit_base(&decorated);
        return indexedOk;
    }
#endif
    width = XImage_width(source); height = XImage_height(source);
    if (width <= 0 || height <= 0 ||
        (size_t)width > (SIZE_MAX - 1) / 4 ||
        (size_t)height > SIZE_MAX / ((size_t)width * 4 + 1))
        return false;
    stride = (size_t)width * 4 + 1;
    rawSize = stride * (size_t)height;
    raw = (uint8_t*)XMalloc_System(rawSize);
    if (!raw || rawSize > UINT_MAX) goto done;
    for (int y = 0; y < height; ++y) {
        uint8_t* row = raw + (size_t)y * stride;
        row[0] = 0;
        for (int x = 0; x < width; ++x) {
            uint32_t c = XImage_pixel(source, x, y);
            row[1 + x * 4] = (uint8_t)(c >> 16);
            row[2 + x * 4] = (uint8_t)(c >> 8);
            row[3 + x * 4] = (uint8_t)c;
            row[4 + x * 4] = (uint8_t)(c >> 24);
        }
    }
    compressedSize = compressBound((uLong)rawSize);
    compressed = (uint8_t*)XMalloc_System((size_t)compressedSize);
    if (!compressed || compress2(compressed, &compressedSize, raw,
                                 (uLong)rawSize, compressionLevel) != Z_OK)
        goto done;
    if (!XByteArray_resize_base((XVector*)out, 0)) goto done;
    memset(ihdr, 0, sizeof(ihdr));
    XImageCodecInternal_writeU32BE(ihdr, (uint32_t)width);
    XImageCodecInternal_writeU32BE(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8; ihdr[9] = 6;
    if (!XImageCodecInternal_appendBytes(out, "\x89PNG\r\n\x1a\n", 8) ||
        !pngAppendChunk(out, "IHDR", ihdr, sizeof(ihdr)) ||
        !pngAppendColorProfile(source, out) ||
        !pngAppendGamma(source, out, gamma) ||
        !pngAppendPhysicalMetadata(source, out) ||
        !pngAppendTexts(source, out) ||
        !pngAppendChunk(out, "IDAT", compressed, (size_t)compressedSize) ||
        !pngAppendChunk(out, "IEND", NULL, 0))
        goto done;
    ok = true;
done:
    if (raw) XFree_System(raw);
    if (compressed) XFree_System(compressed);
    if (decoratedInitialized) XImage_deinit_base(&decorated);
    return ok;
}

bool XImageCodecInternal_encodePng(const XImage* image, XByteArray* out)
{
    return XImageCodecInternal_encodePngOptions(image, -1, -1, 0.0f,
                                                NULL, out);
}

bool XImageCodecInternal_encodePngDescription(const XImage* image,
                                              const XString* description,
                                              XByteArray* out)
{
    return XImageCodecInternal_encodePngOptions(image, -1, -1, 0.0f,
                                                description, out);
}

#endif /* XIMAGECODEC_PNG_ON */
#endif /* XIMAGECODEC_ON */
