/*****************************************************************************/
/**
 * @file       XImageCodecIco.c
 * @brief      XImageCodec ICO/CUR 图标编解码实现。
 * @note       目录和 DIB 布局依据 Qt 6.8 qicohandler.cpp；门面接口只表示
 *             单幅图像，因此读取目录中的首个条目。支持嵌入 PNG 与常见
 *             1/4/8/24/32 位无压缩 DIB，编码统一写出一个 32 位 DIB 条目。
 */
#include "XImageCodec_config.h"
#include "XImageCodecInternal.h"
#include "XImage.h"
#include "XMemory.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#if XIMAGECODEC_ON && XIMAGECODEC_ICO_ON

/* ICO 文件头固定 6 字节，目录条目固定 16 字节，均为小端序。 */
#define ICO_HEADER_SIZE 6u
#define ICO_ENTRY_SIZE 16u
#define ICO_DIB_HEADER_SIZE 40u

static bool ico_range(size_t size, uint32_t offset, uint32_t length)
{
    size_t begin = (size_t)offset;
    size_t count = (size_t)length;
    return begin <= size && count <= size - begin;
}

static bool ico_readFirstEntry(const uint8_t* data, size_t size,
                               uint8_t* width, uint8_t* height,
                               uint16_t* planes, uint16_t* bitCount,
                               uint32_t* bytesInRes, uint32_t* imageOffset,
                               uint16_t* countOut)
{
    uint16_t type;
    uint16_t count;
    const uint8_t* entry;
    if (!data || size < ICO_HEADER_SIZE + ICO_ENTRY_SIZE || !width ||
        !height || !planes || !bitCount || !bytesInRes || !imageOffset ||
        !countOut)
        return false;
    if (XImageCodecInternal_readU16LE(data) != 0u)
        return false;
    type = XImageCodecInternal_readU16LE(data + 2u);
    count = XImageCodecInternal_readU16LE(data + 4u);
    if (type != 1u && type != 2u)
        return false;
    entry = data + ICO_HEADER_SIZE;
    if (entry[3] != 0u)
        return false;
    *width = entry[0];
    *height = entry[1];
    *planes = XImageCodecInternal_readU16LE(entry + 4u);
    *bitCount = XImageCodecInternal_readU16LE(entry + 6u);
    *bytesInRes = XImageCodecInternal_readU32LE(entry + 8u);
    *imageOffset = XImageCodecInternal_readU32LE(entry + 12u);
    *countOut = count;
    /* Qt ICOReader::canRead() uses these fields to reject random files that
       happen to begin with two zero bytes.  Cursor entries permit the plane
       and bit-count fields to carry hotspot metadata, as Qt does. */
    if (*bytesInRes < ICO_DIB_HEADER_SIZE ||
        (type == 1u && (*planes > 1u || *bitCount > 32u)))
        return false;
    return true;
}

static int ico_dimension(uint8_t value)
{
    return value ? (int)value : 256;
}

/* 从 ICO 调色板扫描行按 MSB 优先取出 1/4/8 位索引。 */
static uint8_t ico_packedIndex(const uint8_t* row, size_t x, int bitCount)
{
    unsigned bitIndex = (unsigned)x * (unsigned)bitCount;
    unsigned shift = 8u - (unsigned)bitCount - (bitIndex & 7u);
    unsigned mask = (1u << (unsigned)bitCount) - 1u;
    return (uint8_t)((row[bitIndex >> 3] >> shift) & mask);
}

bool XImageCodecInternal_probeIcoSize(const uint8_t* data, size_t size,
                                      int* width, int* height)
{
    uint8_t entryWidth;
    uint8_t entryHeight;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t bytesInRes;
    uint32_t imageOffset;
    uint16_t count;
    int w;
    int h;
    if (!width || !height ||
        !ico_readFirstEntry(data, size, &entryWidth, &entryHeight,
                             &planes, &bitCount, &bytesInRes, &imageOffset,
                             &count))
        return false;
    (void)planes;
    (void)bitCount;
    (void)bytesInRes;
    (void)imageOffset;
    (void)count;
    w = ico_dimension(entryWidth);
    h = ico_dimension(entryHeight);
    if (w <= 0 || h <= 0 || w > 256 || h > 256)
        return false;
    *width = w;
    *height = h;
    return true;
}

static bool ico_decodeDib(const uint8_t* payload, size_t payloadSize,
                          uint8_t entryWidth, uint8_t entryHeight,
                          uint16_t entryBitCount, XImage* out)
{
    uint32_t headerSize;
    int32_t dibWidth;
    int32_t dibHeight;
    uint16_t planes;
    uint16_t bitCount;
    uint16_t count;
    uint32_t compression;
    int width;
    int height;
    size_t colorStride;
    size_t colorBytes;
    size_t maskStride;
    size_t maskBytes;
    size_t paletteEntries = 0;
    size_t paletteBytes = 0;
    size_t pixelOffset;
    uint32_t colorsUsed;
    XImage temp;
    XImageFormat outputFormat;
    if (!payload || !out || payloadSize < ICO_DIB_HEADER_SIZE)
        return false;
    headerSize = XImageCodecInternal_readU32LE(payload);
    if (headerSize != ICO_DIB_HEADER_SIZE)
        return false;
    dibWidth = (int32_t)XImageCodecInternal_readU32LE(payload + 4u);
    dibHeight = (int32_t)XImageCodecInternal_readU32LE(payload + 8u);
    planes = XImageCodecInternal_readU16LE(payload + 12u);
    bitCount = XImageCodecInternal_readU16LE(payload + 14u);
    compression = XImageCodecInternal_readU32LE(payload + 16u);
    if (dibWidth <= 0 || dibHeight <= 0 || planes != 1u ||
        compression != 0u)
        return false;
    width = entryWidth ? (int)entryWidth : dibWidth;
    height = entryHeight ? (int)entryHeight : dibHeight / 2;
    if (width <= 0 || height <= 0 || width > 256 || height > 256 ||
        dibWidth < width || dibHeight < height * 2)
        return false;
    if (bitCount == 0u)
        bitCount = entryBitCount;
    if (bitCount != 1u && bitCount != 4u && bitCount != 8u &&
        bitCount != 16u &&
        bitCount != 24u && bitCount != 32u)
        return false;
    /* Qt's ICO reader deliberately rejects 16-bit DIB color data. */
    if (bitCount == 16u)
        return false;
    colorsUsed = XImageCodecInternal_readU32LE(payload + 32u);
    if (bitCount <= 8u) {
        paletteEntries = colorsUsed ? (size_t)colorsUsed : ((size_t)1u << bitCount);
        if (paletteEntries == 0u || paletteEntries > 256u ||
            paletteEntries > (SIZE_MAX - ICO_DIB_HEADER_SIZE) / 4u)
            return false;
        paletteBytes = paletteEntries * 4u;
    }
    pixelOffset = ICO_DIB_HEADER_SIZE + paletteBytes;
    colorStride = (((size_t)width * (size_t)bitCount) + 31u) / 32u * 4u;
    if (colorStride > SIZE_MAX / (size_t)height)
        return false;
    colorBytes = colorStride * (size_t)height;
    maskStride = (((size_t)width) + 31u) / 32u * 4u;
    if (maskStride > SIZE_MAX / (size_t)height)
        return false;
    maskBytes = maskStride * (size_t)height;
    if (pixelOffset > payloadSize ||
        colorBytes > payloadSize - pixelOffset ||
        maskBytes > payloadSize - pixelOffset - colorBytes)
        return false;

    /* The AND mask can make 24-bit pixels transparent, so retain an alpha
       capable destination even though the XOR payload itself is opaque. */
    outputFormat = XImageFormat_ARGB32;
    XImage_init(&temp);
    XImage_init_ex(&temp, width, height, outputFormat);
    if (XImage_isNull(&temp))
        return false;
    for (int fileY = 0; fileY < height; ++fileY) {
        int destY = height - 1 - fileY;
            const uint8_t* row = payload + pixelOffset +
                              colorStride * (size_t)fileY;
        for (int x = 0; x < width; ++x) {
            uint32_t color;
            if (bitCount <= 8u) {
                uint8_t index = (uint8_t)(ico_packedIndex(row, (size_t)x,
                                                          (int)bitCount));
                const uint8_t* palette = payload + ICO_DIB_HEADER_SIZE;
                if ((size_t)index >= paletteEntries) {
                    XImage_deinit_base(&temp);
                    return false;
                }
                color = 0xff000000u | ((uint32_t)palette[(size_t)index * 4u + 2u] << 16) |
                        ((uint32_t)palette[(size_t)index * 4u + 1u] << 8) |
                        (uint32_t)palette[(size_t)index * 4u];
            } else {
                const uint8_t* pixel = row + (size_t)x * (size_t)bitCount / 8u;
                color = 0xff000000u | ((uint32_t)pixel[2] << 16) |
                         ((uint32_t)pixel[1] << 8) | (uint32_t)pixel[0];
            }
            if (bitCount == 32u) {
                const uint8_t* pixel = row + (size_t)x * 4u;
                color = ((uint32_t)pixel[3] << 24) |
                        ((uint32_t)pixel[2] << 16) |
                        ((uint32_t)pixel[1] << 8) | (uint32_t)pixel[0];
            }
            XImage_setPixel(&temp, x, destY, color);
        }
    }
    /* Qt applies the AND mask to 1/4/8/16/24-bit entries.  A 32-bit DIB
       already carries alpha bytes and therefore keeps them verbatim. */
    if (bitCount != 32u) {
        size_t maskOffset = pixelOffset + colorBytes;
        for (int fileY = 0; fileY < height; ++fileY) {
            int destY = height - 1 - fileY;
            const uint8_t* row = payload + maskOffset +
                                 maskStride * (size_t)fileY;
            for (int x = 0; x < width; ++x) {
                if (row[(unsigned)x >> 3] & (uint8_t)(0x80u >> (x & 7))) {
                    uint32_t color = XImage_pixel(&temp, x, destY) & 0x00ffffffu;
                    XImage_setPixel(&temp, x, destY, color);
                }
            }
        }
    }
    /* Qt keeps a 32-bit ICO payload in an alpha-capable image and applies
       the AND mask for 24-bit payloads. */
    XImage_deinit_base(out);
    out->m_data = temp.m_data;
    temp.m_data = NULL;
    XImage_deinit_base(&temp);
    return true;
}

bool XImageCodecInternal_decodeIco(const uint8_t* data, size_t size, XImage* out)
{
    uint8_t entryWidth;
    uint8_t entryHeight;
    uint16_t planes;
    uint16_t bitCount;
    uint16_t count;
    uint32_t bytesInRes;
    uint32_t imageOffset;
    const uint8_t* payload;
    if (!out ||
        !ico_readFirstEntry(data, size, &entryWidth, &entryHeight,
                             &planes, &bitCount, &bytesInRes, &imageOffset,
                             &count))
        return false;
    (void)planes;
    /* Qt's read() returns no image for an empty directory even though
       canRead() only inspects the first entry bytes. */
    if (count == 0u)
        return false;
    /* Header probing follows Qt's canRead() and does not require the whole
       resource in the initial device peek; decoding must enforce bounds. */
    if (!ico_range(size, imageOffset, bytesInRes))
        return false;
    payload = data + imageOffset;
#if XIMAGECODEC_PNG_ON
    if (bytesInRes >= 8u &&
        memcmp(payload, "\x89PNG\r\n\x1a\n", 8u) == 0)
        return XImageCodecInternal_decodePng(payload, (size_t)bytesInRes, out);
#endif
    return ico_decodeDib(payload, (size_t)bytesInRes, entryWidth, entryHeight,
                         bitCount, out);
}

bool XImageCodecInternal_encodeIco(const XImage* image, XByteArray* out)
{
    XImage scaled;
    const XImage* source = image;
    bool hasScaled = false;
    int width;
    int height;
    size_t colorStride;
    size_t colorBytes;
    size_t maskStride;
    size_t maskBytes;
    size_t imageOffset = ICO_HEADER_SIZE + ICO_ENTRY_SIZE;
    size_t total;
    uint8_t* bytes;
    if (!image || !out || XImage_isNull(image))
        return false;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0)
        return false;
    if (width > 256 || height > 256) {
        XImage_init(&scaled);
        XImage_scaled(image, 256, 256, 1u, 1u, &scaled);
        if (XImage_isNull(&scaled))
            return false;
        source = &scaled;
        width = XImage_width(source);
        height = XImage_height(source);
        hasScaled = true;
    }
    colorStride = (size_t)width * 4u;
    colorBytes = colorStride * (size_t)height;
    maskStride = (((size_t)width) + 31u) / 32u * 4u;
    maskBytes = maskStride * (size_t)height;
    if (colorBytes > SIZE_MAX - maskBytes - imageOffset - ICO_DIB_HEADER_SIZE) {
        if (hasScaled) XImage_deinit_base(&scaled);
        return false;
    }
    total = imageOffset + ICO_DIB_HEADER_SIZE + colorBytes + maskBytes;
    if (!XByteArray_resize_base((XVector*)out, total)) {
        if (hasScaled) XImage_deinit_base(&scaled);
        return false;
    }
    bytes = XByteArray_data(out);
    if (!bytes) {
        if (hasScaled) XImage_deinit_base(&scaled);
        return false;
    }
    memset(bytes, 0, total);
    XImageCodecInternal_writeU16LE(bytes, 0u);
    XImageCodecInternal_writeU16LE(bytes + 2u, 1u);
    XImageCodecInternal_writeU16LE(bytes + 4u, 1u);
    bytes[6] = width == 256 ? 0u : (uint8_t)width;
    bytes[7] = height == 256 ? 0u : (uint8_t)height;
    bytes[8] = 0u;
    bytes[9] = 0u;
    XImageCodecInternal_writeU16LE(bytes + 10u, 1u);
    XImageCodecInternal_writeU16LE(bytes + 12u, 32u);
    XImageCodecInternal_writeU32LE(bytes + 14u,
                                   (uint32_t)(ICO_DIB_HEADER_SIZE +
                                              colorBytes + maskBytes));
    /* ICONDIR stores the resource start, not the DIB pixel start. */
    XImageCodecInternal_writeU32LE(bytes + 18u, (uint32_t)imageOffset);
    {
        uint8_t* dib = bytes + imageOffset;
        XImageCodecInternal_writeU32LE(dib, ICO_DIB_HEADER_SIZE);
        XImageCodecInternal_writeU32LE(dib + 4u, (uint32_t)width);
        XImageCodecInternal_writeU32LE(dib + 8u, (uint32_t)height * 2u);
        XImageCodecInternal_writeU16LE(dib + 12u, 1u);
        XImageCodecInternal_writeU16LE(dib + 14u, 32u);
        XImageCodecInternal_writeU32LE(dib + 16u, 0u);
        XImageCodecInternal_writeU32LE(dib + 20u,
                                       (uint32_t)(colorBytes + maskBytes));
        XImageCodecInternal_writeU32LE(dib + 24u, 0u);
        XImageCodecInternal_writeU32LE(dib + 28u, 0u);
        XImageCodecInternal_writeU32LE(dib + 32u, 0u);
        XImageCodecInternal_writeU32LE(dib + 36u, 0u);
        for (int fileY = 0; fileY < height; ++fileY) {
            int sourceY = height - 1 - fileY;
            uint8_t* row = dib + ICO_DIB_HEADER_SIZE +
                           colorStride * (size_t)fileY;
            uint8_t* mask = dib + ICO_DIB_HEADER_SIZE + colorBytes +
                            maskStride * (size_t)fileY;
            memset(mask, 0xff, maskStride);
            for (int x = 0; x < width; ++x) {
                uint32_t color = XImage_pixel(source, x, sourceY);
                uint8_t* pixel = row + (size_t)x * 4u;
                pixel[0] = (uint8_t)color;
                pixel[1] = (uint8_t)(color >> 8);
                pixel[2] = (uint8_t)(color >> 16);
                pixel[3] = (uint8_t)(color >> 24);
                if ((color >> 24) != 0u)
                    mask[(unsigned)x >> 3] &= (uint8_t)~(0x80u >> (x & 7));
            }
        }
    }
    if (hasScaled) XImage_deinit_base(&scaled);
    return true;
}

#endif /* XIMAGECODEC_ON && XIMAGECODEC_ICO_ON */
