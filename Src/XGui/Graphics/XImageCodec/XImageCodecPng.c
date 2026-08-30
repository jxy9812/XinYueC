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
    int paletteCount = 0, trnsCount = 0;
    uint16_t trnsGray = 0, trnsR = 0, trnsG = 0, trnsB = 0;
    bool haveTrnsGray = false, haveTrnsRgb = false;
    bool haveIHDR = false, havePLTE = false, haveIDAT = false,
         haveIEND = false, haveTrns = false, idatEnded = false;
    XImage temp;
    bool ok = false;

    if (!data || size < 33 || !out || memcmp(data, "\x89PNG\r\n\x1a\n", 8))
        return false;
    memset(palette, 0, sizeof(palette));
    memset(trnsAlpha, 0, sizeof(trnsAlpha));

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
