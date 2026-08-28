/*****************************************************************************/
/**
 * @file       XImageCodecBmp.c
 * @brief      XImageCodec BMP 格式独立实现（Windows 位图）。
 * @note       受 XIMAGECODEC_BMP_ON 及其扩展开关控制，可通过
 *              XImageCodec_config.h 单独裁剪：
 *               基础（BMP_ON）          ：DIB>=40、24/32 位 BI_RGB 无压缩；
 *               索引色（BMP_INDEXED_ON）：1/2/4/8 位调色板、16 位
 *                                        RGB555/565/BITFIELDS 解码；
 *               RLE（BMP_RLE_ON）       ：RLE4/RLE8 行程编码解码；
 *               V4/V5（BMP_V45_ON）     ：BITMAPV4/V5 头、32 位 BITFIELDS
 *                                        与 Alpha 掩码解码。
 * @note       编码输出 24/32 位 BI_RGB（依图像是否含 Alpha 通道）。
 *              只通过 XImageCodecInternal_decodeBmp/encodeBmp 对外暴露，
 *              统一由 XImageCodec.c 的 XImageCodec_decode/encode 分发。
 */
#include "XImageCodec_config.h"
#include "XImageCodecInternal.h"
#include "XImage.h"
#include "XMemory.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#if XIMAGECODEC_ON
#if XIMAGECODEC_BMP_ON

/* 取第 i 个调色板条目（文件内 BGR/BGRA 字节序展开为 ARGB 颜色）。 */
static uint32_t bmpPaletteEntry(const uint8_t* p, size_t entryBytes)
{
    (void)entryBytes;
    return ((uint32_t)255u << 24) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) |
           (uint32_t)p[0];
}

/* 从扫描行按位取出第 x 个索引（MSB 优先）。 */
static uint8_t bmpPackedIndex(const uint8_t* row, size_t x, int bpp)
{
    unsigned bitIndex = (unsigned)x * (unsigned)bpp;
    unsigned shift = 8u - (unsigned)bpp - (bitIndex & 7u);
    unsigned mask = (1u << (unsigned)bpp) - 1u;
    return (uint8_t)((row[bitIndex >> 3] >> shift) & mask);
}

/* 把位掩码中的字段值展开为 8 位分量。 */
static uint8_t bmpMaskTo8(uint32_t value, uint32_t mask)
{
    uint32_t shift, bits;
    if (!mask) return 0;
    shift = 0;
    while (shift < 32 && !((mask >> shift) & 1u)) ++shift;
    bits = mask >> shift;
    while (bits > 1 && !(bits & 1u)) bits >>= 1;
    {
        uint32_t max = bits;
        uint32_t v = ((uint32_t)value & mask) >> shift;
        return (uint8_t)((v * 255u + max / 2u) / max);
    }
}

/* RLE8/RLE4 解压到 width*height 的索引缓冲区。 */
/* Qt 对 RLE 行程/绝对模式按当前行余量钳制，行程跨行时不写入下一行。 */
static int bmpRleRowRemaining(int x, int width)
{
    return x >= width ? 0 : width - x;
}

static bool bmpRleAbsRead(const uint8_t* src, size_t pos, int i, int bpp,
                          int* outIndex)
{
    if (bpp == 8) {
        *outIndex = src[pos + (size_t)i];
        return true;
    }
    {
        uint8_t byte = src[pos + (size_t)(i / 2)];
        *outIndex = (i & 1) ? (byte & 0xf) : ((byte >> 4) & 0xf);
        return true;
    }
}

static bool bmpRleDecode(const uint8_t* src, size_t size, int bpp,
                         int width, int height, uint8_t* indices)
{
    size_t pos = 0;
    int x = 0, y = 0;
    while (pos + 2 <= size) {
        uint8_t count = src[pos], value = src[pos + 1];
        pos += 2;
        if (count > 0) {
            /* 编码模式：8 位为单字节索引，4 位为高低半字节轮流 */
            int max = (y >= height) ? 0 : bmpRleRowRemaining(x, width);
            int n = (int)count;
            if (n > max) n = max;
            for (int i = 0; i < n; ++i) {
                int idx = value;
                if (bpp == 4) {
                    int hi = (value >> 4) & 0xf, lo = value & 0xf;
                    idx = (i & 1) ? lo : hi;
                }
                indices[(size_t)y * width + (size_t)x] = (uint8_t)idx;
                ++x;
            }
        } else {
            switch (value) {
                case 0: x = 0; ++y; break;          /* 行尾 */
                case 1: return true;                /* 图尾 */
                case 2: {                           /* 增量 */
                    uint8_t dx, dy;
                    if (pos + 2 > size) return false;
                    dx = src[pos]; dy = src[pos + 1];
                    pos += 2;
                    x += dx; y += dy;
                    if (x >= width) x = width - 1;
                    if (y >= height) y = height - 1;
                    break;
                }
                default: {                          /* 绝对模式 */
                    int n = value;
                    int max = (y >= height) ? 0 : bmpRleRowRemaining(x, width);
                    size_t need = bpp == 8
                        ? (size_t)n + ((size_t)n & 1u)   /* 8 位逐字节，奇数补齐 */
                        : (size_t)((n + 1) / 2);         /* 4 位每字节两个像素 */
                    if (pos + need > size) return false;
                    if (n > max) n = max;
                    for (int i = 0; i < n; ++i) {
                        int idx;
                        if (!bmpRleAbsRead(src, pos, i, bpp, &idx))
                            return false;
                        indices[(size_t)y * width + (size_t)x] = (uint8_t)idx;
                        ++x;
                    }
                    pos += need;
                    break;
                }
            }
        }
    }
    return true; /* 允许文件以非 1 命令结束 */
}

bool XImageCodecInternal_decodeBmp(const uint8_t* data, size_t size, XImage* out)
{
    uint32_t offset, dib, compression = 0, clrUsed = 0;
    int32_t signedHeight;
    uint32_t width32;
    int width, height;
    uint16_t planes, bpp;
    uint32_t maskR = 0, maskG = 0, maskB = 0, maskA = 0;
    uint32_t rgbAlphaMask = 0;
    bool bottomUp, hasMasks = false, hasV45 = false;
    size_t palettePos, paletteEntries = 0, paletteEntryBytes = 3;
    uint32_t palette[256];
    bool paletteAlpha = false;
    uint8_t* indexBuffer = NULL;
    XImage temp;
    bool ok = false;

    if (!data || size < 26 || !out || data[0] != 'B' || data[1] != 'M')
        return false;
    offset = XImageCodecInternal_readU32LE(data + 10);
    dib = XImageCodecInternal_readU32LE(data + 14);
    if (dib == 12) {
        if (size < 26) return false;
        width32 = XImageCodecInternal_readU16LE(data + 18);
        signedHeight = XImageCodecInternal_readU16LE(data + 20);
        planes = XImageCodecInternal_readU16LE(data + 22);
        bpp = XImageCodecInternal_readU16LE(data + 24);
    } else {
        if (dib < 40 || size < 14u + dib) return false;
        width32 = XImageCodecInternal_readU32LE(data + 18);
        signedHeight = (int32_t)XImageCodecInternal_readU32LE(data + 22);
        planes = XImageCodecInternal_readU16LE(data + 26);
        bpp = XImageCodecInternal_readU16LE(data + 28);
        if (dib >= 40)
            compression = XImageCodecInternal_readU32LE(data + 30);
        if (dib >= 40)
            clrUsed = XImageCodecInternal_readU32LE(data + 46);
        if (dib == 52 || dib == 56 || dib == 108 || dib == 124)
            hasV45 = true;
        if (dib >= 108)
            rgbAlphaMask = XImageCodecInternal_readU32LE(data + 14u + 52u);
    }
    if (width32 == 0 || width32 > (uint32_t)INT_MAX ||
        signedHeight == 0 || signedHeight == INT32_MIN)
        return false;
    if (planes != 1) return false;
    width = (int)width32;
    bottomUp = signedHeight > 0;
    height = signedHeight < 0 ? -(int)signedHeight : (int)signedHeight;
    if ((uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height >
        16384ull * 16384ull)
        return false;

    /* 位深与压缩方式裁剪。 */
    switch (bpp) {
        case 24:
        case 32:
            break;
        case 1:
        case 4:
        case 8:
        case 16:
#if XIMAGECODEC_BMP_INDEXED_ON
            break;
#else
            return false;
#endif
        default:
            return false;
    }
#if !XIMAGECODEC_BMP_RLE_ON
    if (compression == 1 || compression == 2) return false;
#endif
#if !XIMAGECODEC_BMP_V45_ON
    if (compression == 3) return false;
#endif
    switch (compression) {
        case 0: break;                  /* BI_RGB */
        case 1:                         /* BI_RLE8 */
            if (bpp != 8) return false;
            break;
        case 2:                         /* BI_RLE4 */
            if (bpp != 4) return false;
            break;
        case 3:                         /* BI_BITFIELDS */
            if (bpp != 16 && bpp != 32) return false;
            hasMasks = true;
            break;
        default:
            return false;
    }

    /* 掩码位置：V4/V5 的 RGBA 掩码固定位于 DIB 头偏移 40..55；
     * INFOHEADER 的 BITFIELDS 掩码紧随 DIB 头（规范固定 3 个掩码共 12 字节）。 */
    if (hasMasks) {
        const uint8_t* mp;
        if (hasV45) {
            mp = data + 14u + 40u;
            if (dib < 56u || size < 14u + 40u + 16u) return false;
            maskR = XImageCodecInternal_readU32LE(mp);
            maskG = XImageCodecInternal_readU32LE(mp + 4);
            maskB = XImageCodecInternal_readU32LE(mp + 8);
            if (bpp == 32) maskA = XImageCodecInternal_readU32LE(mp + 12);
        } else {
            mp = data + 14u + dib;
            if (size < 14u + dib + 12u) return false;
            maskR = XImageCodecInternal_readU32LE(mp);
            maskG = XImageCodecInternal_readU32LE(mp + 4);
            maskB = XImageCodecInternal_readU32LE(mp + 8);
        }
        if (!maskR || !maskG || !maskB) return false;
    }

    /* 调色板（仅索引色）：位于 DIB 头（及可选掩码）之后。 */
    palettePos = 14 + (dib == 12 ? 12u : (size_t)dib);
    if (hasMasks && !hasV45) palettePos += (size_t)(bpp == 32 ? 16 : 12);
    paletteEntryBytes = (dib >= 108) ? 4u : 3u;
    if (bpp <= 8) {
        size_t needEntries;
        if (clrUsed > 256u) return false;
        needEntries = clrUsed ? (size_t)clrUsed : ((size_t)1u << bpp);
        if (needEntries == 0 || needEntries > 256u) return false;
        for (size_t i = 0; i < needEntries; ++i) {
            if (palettePos + (i + 1) * paletteEntryBytes > size)
                return false;
        }
        paletteEntries = needEntries;
        for (size_t i = 0; i < needEntries; ++i) {
            const uint8_t* p = data + palettePos + i * paletteEntryBytes;
            uint32_t c = bmpPaletteEntry(p, paletteEntryBytes);
            if (paletteEntryBytes == 4) {
                uint8_t a = p[3];
                if (a != 255u) paletteAlpha = true;
                c = ((uint32_t)a << 24) | (c & 0x00ffffffu);
            }
            palette[i] = c;
        }
    }

    /* 像素数据区域整体越界检查。RLE 流长度与 stride*height 无关，
     * 只需保证流起始偏移合法，剩余边界由 bmpRleDecode 逐步校验。 */
    {
        bool rleComp = compression == 1 || compression == 2;
        if (rleComp) {
            if ((uint64_t)offset > size) return false;
        } else {
            size_t stride = (((size_t)width * bpp) + 31u) / 32u * 4u;
            size_t minRowBytes = bpp >= 8
                ? (size_t)width * ((size_t)bpp / 8u)
                : (((size_t)width * (size_t)bpp) + 7u) / 8u;
            if (stride < minRowBytes) return false;
            if ((uint64_t)offset + stride * (uint64_t)height > size)
                return false;
        }
    }

    /* 索引色：先解码到索引缓冲区。 */
    if (bpp <= 8) {
        bool rle = compression == 1 || compression == 2;
        size_t stride = (((size_t)width * (size_t)bpp) + 31u) / 32u * 4u;
        size_t count = (size_t)width * (size_t)height;
        XImageFormat fmt = bpp == 1 ? XImageFormat_Mono :
            (paletteAlpha ? XImageFormat_ARGB32 : XImageFormat_Indexed8);
        indexBuffer = (uint8_t*)XMalloc_System(count);
        if (!indexBuffer) return false;
        /* 未覆盖到的剩余像素补 0：RLE 流允许不显式铺满整行，未写区域按索引 0 处理。 */
        memset(indexBuffer, 0, count);
        if (rle) {
            if (!bmpRleDecode(data + offset, size - offset, bpp, width,
                              height, indexBuffer))
                goto bmp_done;
        } else {
            for (int y = 0; y < height; ++y) {
                int srcY = bottomUp ? height - 1 - y : y;
                const uint8_t* src =
                    data + offset + stride * (size_t)srcY;
                for (int x = 0; x < width; ++x)
                    indexBuffer[(size_t)y * width + x] =
                        bmpPackedIndex(src, (size_t)x, bpp);
            }
        }
        XImage_init_ex(&temp, width, height, fmt);
        if (XImage_isNull(&temp)) goto bmp_done;
        if (fmt == XImageFormat_Indexed8 || fmt == XImageFormat_Mono)
            XImage_setColorTable(&temp, palette, (int)paletteEntries);
        for (int y = 0; y < height; ++y) {
            /* 未压缩索引色在填充阶段已把文件行序翻转为逻辑行序；
             * RLE 解出的行序仍是文件行序（bottom-up 文件首行是图像底行），
             * 因此仅 RLE 路径在此再翻转一次，避免“双重翻转”。 */
            int srcRow = (rle && bottomUp) ? height - 1 - y : y;
            for (int x = 0; x < width; ++x) {
                uint8_t idx = indexBuffer[(size_t)srcRow * width + x];
                if (idx >= paletteEntries) idx = 0;
                if (fmt == XImageFormat_Indexed8 || fmt == XImageFormat_Mono) {
                    XImage_setPixel(&temp, x, y, (uint32_t)idx);
                } else {
                    XImage_setPixel(&temp, x, y,
                        idx < paletteEntries ? palette[idx] : 0u);
                }
            }
        }
        /* Qt 的 1 位 BMP 约定颜色表中较亮颜色对应索引 0；若文件
         * 颜色表顺序相反，同时翻转存储位和颜色表，保持 QImage::pixel
         * 的可观察颜色与 Qt 一致。 */
        if (fmt == XImageFormat_Mono && paletteEntries == 2) {
            uint32_t c0 = palette[0], c1 = palette[1];
            unsigned y;
            unsigned x;
            unsigned l0 = (299u * ((c0 >> 16) & 255u) +
                           587u * ((c0 >> 8) & 255u) +
                           114u * (c0 & 255u) + 500u) / 1000u;
            unsigned l1 = (299u * ((c1 >> 16) & 255u) +
                           587u * ((c1 >> 8) & 255u) +
                           114u * (c1 & 255u) + 500u) / 1000u;
            if (l0 < l1) {
                for (y = 0; y < (unsigned)height; ++y)
                    for (x = 0; x < (unsigned)width; ++x)
                        XImage_setPixel(&temp, (int)x, (int)y,
                                        1u - (uint32_t)XImage_pixelIndex(&temp,
                                                                          (int)x,
                                                                          (int)y));
                {
                    uint32_t swapped[2] = {c1, c0};
                    XImage_setColorTable(&temp, swapped, 2);
                }
            }
        }
    } else {
        size_t stride = (((size_t)width * (size_t)bpp) + 31u) / 32u * 4u;
        XImageFormat fmt = bpp == 32 ? XImageFormat_ARGB32
                                     : XImageFormat_RGB888;
        XImage_init_ex(&temp, width, height, fmt);
        if (XImage_isNull(&temp)) goto bmp_done;
        for (int y = 0; y < height; ++y) {
            int srcY = bottomUp ? height - 1 - y : y;
            const uint8_t* src = data + offset + stride * (size_t)srcY;
            for (int x = 0; x < width; ++x) {
                uint32_t c;
                if (bpp == 16) {
                    uint32_t v = XImageCodecInternal_readU16LE(
                        src + (size_t)x * 2u);
                    uint8_t r, g, b, a = 255;
                    if (hasMasks) {
                        r = bmpMaskTo8(v, maskR);
                        g = bmpMaskTo8(v, maskG);
                        b = bmpMaskTo8(v, maskB);
                    } else {
                        /* 缺省 5-5-5（高位未使用） */
                        r = (uint8_t)(((v >> 10) & 0x1fu) * 255 / 31);
                        g = (uint8_t)(((v >> 5) & 0x1fu) * 255 / 31);
                        b = (uint8_t)((v & 0x1fu) * 255 / 31);
                    }
                    c = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                        ((uint32_t)g << 8) | b;
                } else if (bpp == 24) {
                    uint8_t bl = src[x * 3], g = src[x * 3 + 1],
                            r = src[x * 3 + 2];
                    c = 0xff000000u | ((uint32_t)r << 16) |
                        ((uint32_t)g << 8) | bl;
                } else {
                    uint32_t v = XImageCodecInternal_readU32LE(
                        src + (size_t)x * 4u);
                    uint8_t r, g, b, a;
                    if (hasMasks && (maskR | maskG | maskB)) {
                        r = bmpMaskTo8(v, maskR);
                        g = bmpMaskTo8(v, maskG);
                        b = bmpMaskTo8(v, maskB);
                        a = maskA ? bmpMaskTo8(v, maskA) : 255;
                    } else {
                        r = (uint8_t)((v >> 16) & 255u);
                        g = (uint8_t)((v >> 8) & 255u);
                        b = (uint8_t)(v & 255u);
                        /* Qt treats plain BI_RGB 32-bit pixels as opaque;
                         * only a V4/V5 header carrying 0xff000000 in the
                         * alpha-mask field enables the high byte. */
                        a = (compression == 0 && rgbAlphaMask == 0xff000000u)
                            ? (uint8_t)(v >> 24) : 255u;
                    }
                    c = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                        ((uint32_t)g << 8) | b;
                }
                XImage_setPixel(&temp, x, y, c);
            }
        }
    }
    XImage_deinit_base(out);
    out->m_data = temp.m_data;
    temp.m_data = NULL;
    ok = true;

bmp_done:
    if (indexBuffer) XFree_System(indexBuffer);
    return ok;
}

bool XImageCodecInternal_encodeBmp(const XImage* image, XByteArray* out)
{
    int width, height;
    bool withAlpha;
    int bytesPerPixel;
    uint16_t bpp;
    size_t rowBytes, imageBytes, total, headerSize;
    uint8_t header[126] = {0};
    if (!image || !out || XImage_isNull(image)) return false;
    width = XImage_width(image); height = XImage_height(image);
    if (width <= 0 || height <= 0 || (size_t)width > (SIZE_MAX - 3) / 3)
        return false;
    withAlpha = XImage_hasAlphaChannel(image);
    bytesPerPixel = withAlpha ? 4 : 3;
    bpp = (uint16_t)(bytesPerPixel * 8);
    rowBytes = ((size_t)width * (size_t)bytesPerPixel + 3u) & ~((size_t)3u);
    if ((size_t)height > SIZE_MAX / rowBytes) return false;
    imageBytes = rowBytes * (size_t)height;
    /* Qt's legacy BI_RGB writer drops alpha from 32-bit images.  XinYueC
     * keeps the existing lossless round-trip contract by emitting a valid
     * V4 BITFIELDS header whenever the source has an alpha channel; plain
     * 24-bit images retain the compact 40-byte INFOHEADER form. */
    headerSize = withAlpha ? 122u : 54u;
    total = headerSize + imageBytes;
    if (total > UINT32_MAX ||
        !XByteArray_resize_base((XVector*)out, total))
        return false;
    memset(XByteArray_data(out), 0, total);
    XImageCodecInternal_writeU16LE(header, 0x4d42u);
    XImageCodecInternal_writeU32LE(header + 2, (uint32_t)total);
    XImageCodecInternal_writeU32LE(header + 10, (uint32_t)headerSize);
    XImageCodecInternal_writeU32LE(header + 14, withAlpha ? 108u : 40u);
    XImageCodecInternal_writeU32LE(header + 18, (uint32_t)width);
    XImageCodecInternal_writeU32LE(header + 22, (uint32_t)height);
    XImageCodecInternal_writeU16LE(header + 26, 1u);
    XImageCodecInternal_writeU16LE(header + 28, bpp);
    XImageCodecInternal_writeU32LE(header + 30, withAlpha ? 3u : 0u);
    XImageCodecInternal_writeU32LE(header + 34, (uint32_t)imageBytes);
    if (withAlpha) {
        XImageCodecInternal_writeU32LE(header + 54, 0x00ff0000u);
        XImageCodecInternal_writeU32LE(header + 58, 0x0000ff00u);
        XImageCodecInternal_writeU32LE(header + 62, 0x000000ffu);
        XImageCodecInternal_writeU32LE(header + 66, 0xff000000u);
    }
    memcpy(XByteArray_data(out), header, headerSize);
    for (int y = 0; y < height; ++y) {
        uint8_t* dst =
            XByteArray_data(out) + headerSize +
            rowBytes * (size_t)(height - 1 - y);
        for (int x = 0; x < width; ++x) {
            uint32_t c = XImage_pixel(image, x, y);
            dst[x * bytesPerPixel] = (uint8_t)c;
            dst[x * bytesPerPixel + 1] = (uint8_t)(c >> 8);
            dst[x * bytesPerPixel + 2] = (uint8_t)(c >> 16);
            if (withAlpha) dst[x * 4 + 3] = (uint8_t)(c >> 24);
        }
    }
    return true;
}

#endif /* XIMAGECODEC_BMP_ON */
#endif /* XIMAGECODEC_ON */
