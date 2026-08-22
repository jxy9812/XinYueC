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
    if (bitsPerPixel < 8)
        return ((size_t)width * (size_t)bitsPerPixel + 7u) / 8u;
    return (size_t)width * pngSampleBytes(bitDepth) * (size_t)channels;
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
    int paletteCount = 0, trnsCount = 0;
    uint16_t trnsGray = 0, trnsR = 0, trnsG = 0, trnsB = 0;
    bool haveTrnsGray = false, haveTrnsRgb = false;
    XImage temp;
    bool ok = false;

    if (!data || size < 33 || !out || memcmp(data, "\x89PNG\r\n\x1a\n", 8))
        return false;
    memset(palette, 0, sizeof(palette));
    memset(trnsAlpha, 0, sizeof(trnsAlpha));

    while (pos + 12 <= size) {
        uint32_t length = XImageCodecInternal_readU32BE(data + pos);
        const uint8_t* type = data + pos + 4;
        if (length > size - pos - 12) goto fail;
        if (!memcmp(type, "IHDR", 4) && length >= 13) {
            width = XImageCodecInternal_readU32BE(data + pos + 8);
            height = XImageCodecInternal_readU32BE(data + pos + 12);
            bitDepth = data[pos + 16]; colorType = data[pos + 17];
            compression = data[pos + 18]; filter = data[pos + 19];
            interlace = data[pos + 20];
        } else if (!memcmp(type, "PLTE", 4) && length <= 768u &&
                   (length % 3u) == 0u) {
            const uint8_t* p = data + pos + 8;
            paletteCount = (int)(length / 3u);
            for (int i = 0; i < paletteCount; ++i) {
                palette[i] = 0xff000000u |
                    ((uint32_t)p[i * 3] << 16) |
                    ((uint32_t)p[i * 3 + 1] << 8) |
                    (uint32_t)p[i * 3 + 2];
            }
        } else if (!memcmp(type, "tRNS", 4)) {
            const uint8_t* p = data + pos + 8;
            if (colorType == PNG_CT_PALETTE) {
                trnsCount = (int)length;
                if (trnsCount > 256) goto fail;
                for (int i = 0; i < trnsCount; ++i)
                    trnsAlpha[i] = p[i];
            } else if (colorType == PNG_CT_GRAY && length >= 2u) {
                trnsGray = (uint16_t)XImageCodecInternal_readU16BE(p);
                haveTrnsGray = true;
            } else if (colorType == PNG_CT_RGB && length >= 6u) {
                trnsR = (uint16_t)XImageCodecInternal_readU16BE(p);
                trnsG = (uint16_t)XImageCodecInternal_readU16BE(p + 2);
                trnsB = (uint16_t)XImageCodecInternal_readU16BE(p + 4);
                haveTrnsRgb = true;
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t* next = (uint8_t*)XRealloc_System(idat, idatSize + length);
            if (!next) goto fail;
            idat = next;
            memcpy(idat + idatSize, data + pos + 8, length);
            idatSize += length;
        } else if (!memcmp(type, "IEND", 4)) break;
        pos += (size_t)length + 12;
    }

    /* 头字段校验 */
    if (!width || !height || width > INT_MAX || height > INT_MAX ||
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
                    size_t stride = pngRowBytes((int)pw, bitDepth, channels) + 1;
                    if (stride < 1 ||
                        ph > (size_t)UINT_MAX / stride) goto fail;
                    passData[i] = stride * ph;
                }
            }
        } else
#endif
        {
            passCount = 1;
            passData[0] = (pngRowBytes((int)width, bitDepth, channels) + 1) *
                          (size_t)height;
        }
        for (int i = 0; i < passCount; ++i)
            rawSize += passData[i];
        if (rawSize == 0 || rawSize > UINT_MAX) goto fail;
    }

    raw = (uint8_t*)XMalloc_System(rawSize);
    if (!raw) goto fail;
    {
        uLongf inflated = (uLongf)rawSize;
        if (uncompress(raw, &inflated, idat, (uLong)idatSize) != Z_OK ||
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
                size_t stride = pngRowBytes((int)pw, bitDepth, channels) + 1;
                size_t passBytes = stride * ph;
                const uint8_t* passRaw = raw + offset;
                uint16_t* dst = samples;
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
        bool hasAlpha = false;
        for (int i = 0; i < paletteCount; ++i)
            if (trnsCount > i && trnsAlpha[i] < 255u) { hasAlpha = true; break; }
        if (hasAlpha) {
            XImage_init_ex(&temp, (int)width, (int)height, XImageFormat_ARGB32);
            if (XImage_isNull(&temp)) goto fail;
            for (int y = 0; y < (int)height; ++y) {
                for (int x = 0; x < (int)width; ++x) {
                    int idx = samples[(size_t)y * width + x];
                    uint32_t c = idx < paletteCount ? palette[idx] : 0u;
                    uint8_t a = (trnsCount > idx) ? trnsAlpha[idx] : 255u;
                    XImage_setPixel(&temp, x, y, (c & 0x00ffffffu) |
                        ((uint32_t)a << 24));
                }
            }
        } else {
            XImage_init_ex(&temp, (int)width, (int)height,
                           XImageFormat_Indexed8);
            if (XImage_isNull(&temp)) goto fail;
            XImage_setColorTable(&temp, palette, paletteCount);
            for (int y = 0; y < (int)height; ++y) {
                for (int x = 0; x < (int)width; ++x) {
                    int idx = samples[(size_t)y * width + x];
                    XImage_setPixel(&temp, x, y, (uint32_t)idx);
                }
            }
        }
    } else
#endif
#if XIMAGECODEC_PNG_16BIT_ON
    if (bitDepth == 16) {
        XImageFormat fmt = colorType == PNG_CT_GRAY
            ? XImageFormat_Grayscale16
            : XImageFormat_RGBA64;
        if (colorType == PNG_CT_RGB) fmt = XImageFormat_RGBX64;
        XImage_init_ex(&temp, (int)width, (int)height, fmt);
        if (XImage_isNull(&temp)) goto fail;
        for (int y = 0; y < (int)height; ++y) {
            uint8_t* row = XImage_scanLine(&temp, y);
            const uint16_t* src = samples + (size_t)y * width * (size_t)channels;
            if (channels == 1) {
                for (int x = 0; x < (int)width; ++x) {
                    uint16_t v = src[x];
                    memcpy(row + (size_t)x * 2, &v, 2);
                }
            } else {
                for (int x = 0; x < (int)width; ++x) {
                    uint16_t tmp[4];
                    int c;
                    for (c = 0; c < (int)channels; ++c) tmp[c] = src[x * channels + c];
                    if (colorType == PNG_CT_GRAY_ALPHA) {
                        /* 灰度+Alpha：RGB 三通道复制灰度值，Alpha 通道保留。 */
                        uint16_t gray = tmp[0], alpha = tmp[1];
                        tmp[1] = tmp[2] = gray;
                        tmp[3] = alpha;
                    }
                    if (colorType == PNG_CT_RGB) tmp[3] = 0xffffu;
                    for (c = 0; c < 4; ++c)
                        memcpy(row + (size_t)x * 8 + (size_t)c * 2, &tmp[c], 2);
                }
            }
        }
    } else
#endif
    {
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
    XImage_deinit_base(out);
    out->m_data = temp.m_data;
    temp.m_data = NULL;
    ok = true;

fail:
    if (samples) XFree_System(samples);
    if (raw) XFree_System(raw);
    if (idat) XFree_System(idat);
    return ok;
}

#if XIMAGECODEC_PNG_PALETTE_ON
/* 将 Indexed8 图像编码为调色板 PNG（ColorType 3 + PLTE + tRNS）。 */
static bool pngEncodeIndexed(const XImage* image, XByteArray* out)
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
                                 (uLong)rawSize, Z_DEFAULT_COMPRESSION) != Z_OK)
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

bool XImageCodecInternal_encodePng(const XImage* image, XByteArray* out)
{
    uint8_t ihdr[13];
    uint8_t* raw = NULL;
    uint8_t* compressed = NULL;
    size_t stride, rawSize;
    uLongf compressedSize;
    int width, height;
    bool ok = false;
    if (!image || !out || XImage_isNull(image)) return false;
#if XIMAGECODEC_PNG_PALETTE_ON
    if (XImage_format(image) == XImageFormat_Indexed8) {
        return pngEncodeIndexed(image, out);
    }
#endif
    width = XImage_width(image); height = XImage_height(image);
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
            uint32_t c = XImage_pixel(image, x, y);
            row[1 + x * 4] = (uint8_t)(c >> 16);
            row[2 + x * 4] = (uint8_t)(c >> 8);
            row[3 + x * 4] = (uint8_t)c;
            row[4 + x * 4] = (uint8_t)(c >> 24);
        }
    }
    compressedSize = compressBound((uLong)rawSize);
    compressed = (uint8_t*)XMalloc_System((size_t)compressedSize);
    if (!compressed || compress2(compressed, &compressedSize, raw,
                                 (uLong)rawSize, Z_DEFAULT_COMPRESSION) != Z_OK)
        goto done;
    if (!XByteArray_resize_base((XVector*)out, 0)) goto done;
    memset(ihdr, 0, sizeof(ihdr));
    XImageCodecInternal_writeU32BE(ihdr, (uint32_t)width);
    XImageCodecInternal_writeU32BE(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8; ihdr[9] = 6;
    if (!XImageCodecInternal_appendBytes(out, "\x89PNG\r\n\x1a\n", 8) ||
        !pngAppendChunk(out, "IHDR", ihdr, sizeof(ihdr)) ||
        !pngAppendChunk(out, "IDAT", compressed, (size_t)compressedSize) ||
        !pngAppendChunk(out, "IEND", NULL, 0))
        goto done;
    ok = true;
done:
    if (raw) XFree_System(raw);
    if (compressed) XFree_System(compressed);
    return ok;
}

#endif /* XIMAGECODEC_PNG_ON */
#endif /* XIMAGECODEC_ON */
