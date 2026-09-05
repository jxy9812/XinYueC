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

/* 计算 Qt qbmphandler.cpp::calc_shift() 使用的最低有效位位置。 */
static uint32_t bmpMaskShift(uint32_t mask)
{
    uint32_t result = 0;
    while ((mask >= 0x100u) || (!(mask & 1u) && mask)) {
        ++result;
        mask >>= 1;
    }
    return result;
}

/* 计算 Qt qbmphandler.cpp::calc_scale() 使用的位复制缩放量。 */
static uint32_t bmpMaskScale(uint32_t lowMask)
{
    uint32_t result = 8;
    while (lowMask && result) {
        --result;
        lowMask >>= 1;
    }
    return result;
}

/* 把位掩码中的字段值展开为 8 位分量。
 * Qt 不使用四舍五入除法，而是用 apply_scale() 重复高位填充低位。
 * 例如 5 位值 3 的结果是 24 而不是 25；保留这个细节可避免
 * RGB555/RGB565 夹具在少数通道值上与 Qt 不一致。 */
static uint8_t bmpMaskTo8(uint32_t value, uint32_t mask)
{
    uint32_t shift;
    uint32_t scale;
    uint32_t filled;
    uint32_t result;
    if (!mask) return 0;
    shift = bmpMaskShift(mask);
    scale = bmpMaskScale(mask >> shift);
    result = (value & mask) >> shift;
    if (!(scale & 7u))
        return (uint8_t)result;
    filled = 8u - scale;
    result <<= scale;
    do {
        result |= result >> filled;
        filled <<= 1;
    } while (filled < 8u);
    return (uint8_t)result;
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
                    int encoded = value;
                    int max = (y >= height) ? 0 : bmpRleRowRemaining(x, width);
                    int n = encoded;
                    if (n > max) n = max;
                    /* Qt qbmphandler.cpp:449-453/514-519 先把绝对块
                     * 的计数钳到当前行余量，再读取钳制后的 payload。
                     * 例如宽度为 2 而命令计数为 4 时，Qt 只要求两个
                     * 像素的字节；按原始计数检查会错误拒绝同样的流。 */
                    size_t need = bpp == 8
                        ? (size_t)n + ((size_t)n & 1u)   /* 8 位逐字节，奇数补齐 */
                        : (size_t)((n + 1) / 2);         /* 4 位每字节两个像素 */
                    /* RLE4 绝对模式的像素数据按字对齐。Qt 的条件
                     * `(((n & 3) + 1) & 2) == 2` 等价于余数为 1 或 2；
                     * 这里的 n 已是 Qt 钳制后的计数，其中值 2 本身
                     * 是 Delta 控制码，实际绝对块从 3 开始，但 6、10
                     * 等合法块仍必须消费一个填充字节，否则下一条命令
                     * 的首字节会被误读为当前流的一部分。 */
                    if (bpp == 4 && ((n & 3) == 1 || (n & 3) == 2))
                        ++need;
                    if (pos + need > size) return false;
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
    int32_t pelsPerMeterX = 0, pelsPerMeterY = 0;
    uint32_t width32;
    int width, height;
    uint16_t planes, bpp;
    uint32_t maskR = 0, maskG = 0, maskB = 0, maskA = 0;
    uint32_t rgbAlphaMask = 0;
    bool bottomUp, hasMasks = false, hasV45 = false;
    bool coreHeader;
    size_t palettePos, dataStart, pixelPos;
    size_t paletteEntries = 0, paletteEntryBytes = 3;
    uint32_t palette[256];
    uint8_t* indexBuffer = NULL;
    XImage temp;
    bool tempInitialized = false;
    bool ok = false;

    if (!data || size < 26 || !out || data[0] != 'B' || data[1] != 'M')
        return false;
    offset = XImageCodecInternal_readU32LE(data + 10);
    dib = XImageCodecInternal_readU32LE(data + 14);
    /* Qt qbmphandler.cpp:77-102 仅把 biSize==12 作为 OS/2 Core
       Header。其它未知长度虽同样走旧式宽高兼容解析，但 read_dib_body
       仍按实际 biSize 定位调色板，且按非 12 字节头读取四字节条目；
       不能把所有未知值都折叠成 12，否则会从错误位置读取颜色表。 */
    coreHeader = dib == 12;
    if (dib == 12) {
        int16_t oldWidth;
        int16_t oldHeight;
        if (size < 26) return false;
        /* Qt 的 BMP_INFOHDR 解析将 OS/2 core header 的宽高读入
         * qint16；保留符号才能正确拒绝负宽度并支持倒行序负高度。 */
        oldWidth = (int16_t)XImageCodecInternal_readU16LE(data + 18);
        oldHeight = (int16_t)XImageCodecInternal_readU16LE(data + 20);
        width32 = (uint32_t)(int32_t)oldWidth;
        signedHeight = (int32_t)oldHeight;
        planes = XImageCodecInternal_readU16LE(data + 22);
        bpp = XImageCodecInternal_readU16LE(data + 24);
    } else if (dib == 40 || dib == 64 || dib == 108 || dib == 124) {
        if (size < 14u + dib) return false;
        width32 = XImageCodecInternal_readU32LE(data + 18);
        signedHeight = (int32_t)XImageCodecInternal_readU32LE(data + 22);
        planes = XImageCodecInternal_readU16LE(data + 26);
        bpp = XImageCodecInternal_readU16LE(data + 28);
        compression = XImageCodecInternal_readU32LE(data + 30);
        pelsPerMeterX = (int32_t)XImageCodecInternal_readU32LE(data + 38);
        pelsPerMeterY = (int32_t)XImageCodecInternal_readU32LE(data + 42);
        clrUsed = XImageCodecInternal_readU32LE(data + 46);
        if (dib == 108 || dib == 124)
            hasV45 = true;
        if (hasV45)
            rgbAlphaMask = XImageCodecInternal_readU32LE(data + 14u + 52u);
    } else {
        /* Qt qbmphandler.cpp:77-102 对未知 DIB 长度走旧 Windows
         * 兼容分支：只读取有符号 16 位宽高、平面和位深，并把压缩、
         * 分辨率及调色板数量清零。不能把任意 >=40 的长度当作现代
         * 头，否则畸形文件中偏移 30/46 的随机字节会改变解码路径。 */
        int16_t oldWidth = (int16_t)XImageCodecInternal_readU16LE(data + 18);
        int16_t oldHeight = (int16_t)XImageCodecInternal_readU16LE(data + 20);
        width32 = (uint32_t)(int32_t)oldWidth;
        signedHeight = (int32_t)oldHeight;
        planes = XImageCodecInternal_readU16LE(data + 22);
        bpp = XImageCodecInternal_readU16LE(data + 24);
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
        /* Qt qbmphandler.cpp:331-338 不把零掩码视为格式错误：
         * calc_shift/calc_scale 会令对应通道解码为 0。这里只检查
         * 掩码数据块完整存在，保留 Qt 对畸形但可读 BITFIELDS 的行为。 */
    }

    /* 调色板（仅索引色）：位于 DIB 头（及可选掩码）之后。 */
    palettePos = 14 + (size_t)dib;
    /* Qt qbmphandler.cpp:255-264：BI_BITFIELDS（压缩值 3）只读取
     * RGB 三个掩码；即使像素为 32 位也不会读取第四个 Alpha 掩码。
     * 第四个掩码仅属于未被 Qt read_dib_infoheader() 接受的
     * BI_ALPHABITFIELDS（压缩值 4）。 */
    if (hasMasks && !hasV45) palettePos += 12u;
    /* Qt qbmphandler.cpp:306 对除 12 字节 Core Header 外的索引 BMP
     * 读取四字节调色板条目；第 4 字节只被消费，不参与 qRgb()。
     * 40 字节 Windows INFOHEADER 同样优先使用四字节条目。项目早期
     * 已内嵌的 RLE/索引夹具曾按三字节条目生成，且像素偏移恰好落在
     * palettePos + 3*n；为保持这些嵌入式资源可用，仅在像素偏移明确
     * 容不下四字节而能容下三字节时回退。这样标准资产仍完全走 Qt
     * 的四字节路径，回退也不会通过“文件剩余长度”猜测而误读截断
     * 的标准 BMP。对应 Qt 的标准依据仍是 qbmphandler.cpp:303-313。 */
    paletteEntryBytes = coreHeader ? 3u : 4u;
    if (bpp <= 8) {
        size_t needEntries;
        if (clrUsed > 256u) return false;
        needEntries = clrUsed ? (size_t)clrUsed : ((size_t)1u << bpp);
        if (needEntries == 0 || needEntries > 256u) return false;
        if (dib == 40 && offset < palettePos + needEntries * 4u &&
            offset == palettePos + needEntries * 3u)
            paletteEntryBytes = 3u;
        for (size_t i = 0; i < needEntries; ++i) {
            if (palettePos + (i + 1) * paletteEntryBytes > size)
                return false;
        }
        dataStart = palettePos + needEntries * paletteEntryBytes;
        paletteEntries = needEntries;
        for (size_t i = 0; i < needEntries; ++i) {
            const uint8_t* p = data + palettePos + i * paletteEntryBytes;
            uint32_t c = bmpPaletteEntry(p, paletteEntryBytes);
            /* Qt qbmphandler.cpp:307-313 以 qRgb(rgb[2], rgb[1], rgb[0])
             * 设置颜色表，第四个保留字节（通常写为零）始终被忽略。 */
            palette[i] = c;
        }
    } else {
        dataStart = palettePos;
    }

    /* 像素数据区域检查。RLE 流长度与 stride*height 无关，只需保证流
     * 起始偏移落在实际像素数据之前，剩余边界由 bmpRleDecode 逐步校验。
     * Qt qbmphandler.cpp:212-214 在进入逐行读取前先用 atEnd() 拒绝
     * 没有任何像素字节的设备（头部末尾正好等于文件尾）；但如果头部
     * 后仍有尾字节，offset==size 会在 Qt 中 seek 到文件尾并保留零填充
     * 图像，不能把这两种情况混为一谈。
     * Qt
     * qbmphandler.cpp:373-377/532-535/548-552 对未压缩像素行
     * 读取不足时退出当前行循环但仍返回已分配图像；因此这里不能再以
     * stride*height 要求整个文件完整。每行循环会单独检查剩余字节并
     * 安全停止，偏移落在文件尾之后时同样保留零填充结果。 */
    /* Qt qbmphandler.cpp:365-367 只有在 bfOffBits 大于已经读取的
     * 调色板/掩码位置时才 seek；偏移过小会继续从当前游标读取，不能
     * 回读文件头。初始游标到达文件尾时则由 212-214 拒绝；若游标
     * 仍有尾字节而 bfOffBits 指向文件尾之后，Qt 分配零填充图像并让
     * 后续读行失败后成功返回，这里保留该边界行为。 */
    if (dataStart >= size) return false;
    pixelPos = (size_t)offset;
    if (pixelPos < dataStart) pixelPos = dataStart;

    /* 索引色：先解码到索引缓冲区。 */
    if (bpp <= 8) {
        bool rle = compression == 1 || compression == 2;
        size_t stride = (((size_t)width * (size_t)bpp) + 31u) / 32u * 4u;
        size_t count = (size_t)width * (size_t)height;
        XImageFormat fmt = bpp == 1 ? XImageFormat_Mono :
            XImageFormat_Indexed8;
        indexBuffer = (uint8_t*)XMalloc_System(count);
        if (!indexBuffer) return false;
        /* 未覆盖到的剩余像素补 0：RLE 流允许不显式铺满整行，未写区域按索引 0 处理。 */
        memset(indexBuffer, 0, count);
        if (rle) {
            if (pixelPos < size &&
                !bmpRleDecode(data + pixelPos, size - pixelPos, bpp, width,
                              height, indexBuffer))
                goto bmp_done;
        } else {
            for (int fileY = 0; fileY < height; ++fileY) {
                int destY = bottomUp ? height - 1 - fileY : fileY;
                size_t rowOffset;
                const uint8_t* src;
                if ((size_t)fileY > (SIZE_MAX - pixelPos) / stride)
                    break;
                rowOffset = pixelPos + stride * (size_t)fileY;
                if (rowOffset > size || stride > size - rowOffset)
                    break;
                src = data + rowOffset;
                for (int x = 0; x < width; ++x)
                    indexBuffer[(size_t)destY * width + x] =
                        bmpPackedIndex(src, (size_t)x, bpp);
            }
        }
        tempInitialized = true;
        XImage_init_ex(&temp, width, height, fmt);
        if (XImage_isNull(&temp)) goto bmp_done;
        if (fmt == XImageFormat_Indexed8 || fmt == XImageFormat_Mono)
            XImage_setColorTable(&temp, palette, (int)paletteEntries);
        /* The BMP reader preserves raw indices even when biClrUsed is
         * smaller than an RLE payload index.  Qt's private decoder writes the
         * scanline bytes directly; routing through public QImage::setPixel()
         * would (correctly for callers) reject those indices. */
        {
            uint8_t* tempBits = XImage_bits(&temp);
            int tempStride = XImage_bytesPerLine(&temp);
            if (!tempBits || tempStride <= 0) goto bmp_done;
        for (int y = 0; y < height; ++y) {
            /* 未压缩索引色在填充阶段已把文件行序翻转为逻辑行序；
             * RLE 解出的行序仍是文件行序（bottom-up 文件首行是图像底行），
             * 因此仅 RLE 路径在此再翻转一次，避免“双重翻转”。 */
            int srcRow = (rle && bottomUp) ? height - 1 - y : y;
            for (int x = 0; x < width; ++x) {
                uint8_t idx = indexBuffer[(size_t)srcRow * width + x];
                if (fmt == XImageFormat_Indexed8 || fmt == XImageFormat_Mono) {
                    /* Qt 的 QImage 在 Indexed8/Mono 路径保存文件中的原始
                     * 索引，即使该索引超出 biClrUsed；只有随后调用
                     * QImage::pixel() 取颜色时，越界表项才表现为 0 色。
                     * 因此这里不能像 RGB 转换路径那样预先把索引钳为 0。 */
                    if (fmt == XImageFormat_Indexed8)
                        tempBits[(size_t)y * (size_t)tempStride + (size_t)x] = idx;
                    else {
                        unsigned mask = 0x80u >> ((unsigned)x & 7u);
                        uint8_t* byte = tempBits + (size_t)y * (size_t)tempStride + ((unsigned)x >> 3);
                        if (idx & 1u) *byte |= (uint8_t)mask;
                        else *byte &= (uint8_t)~mask;
                    }
                } else {
                    /* 该分支目前仅保留给未来的索引到 RGB 转换格式；
                     * 转换成颜色时才按 Qt 的越界表项语义返回 0。 */
                    XImage_setPixel(&temp, x, y,
                        idx < paletteEntries ? palette[idx] : 0u);
                }
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
        tempInitialized = true;
        XImage_init_ex(&temp, width, height, fmt);
        if (XImage_isNull(&temp)) goto bmp_done;
        for (int fileY = 0; fileY < height; ++fileY) {
            int destY = bottomUp ? height - 1 - fileY : fileY;
            size_t rowOffset;
            const uint8_t* src;
            if ((size_t)fileY > (SIZE_MAX - pixelPos) / stride)
                break;
            rowOffset = pixelPos + stride * (size_t)fileY;
            if (rowOffset > size || stride > size - rowOffset)
                break;
            src = data + rowOffset;
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
                        /* Qt qbmphandler.cpp:345-353 使用 5-5-5 掩码；
                         * 通过 bmpMaskTo8() 保留 apply_scale() 的位复制
                         * 规则，而不是看似等价但在少数值上不同的除法。 */
                        r = bmpMaskTo8(v, 0x7c00u);
                        g = bmpMaskTo8(v, 0x03e0u);
                        b = bmpMaskTo8(v, 0x001fu);
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
                    /* Qt 对 BI_BITFIELDS 始终按声明掩码解码；即使 RGB
                     * 掩码全为零，也会把三个通道解为 0，而不是退回
                     * 到普通 BI_RGB 的 0x00RRGGBB 解释。 */
                    if (hasMasks) {
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
                XImage_setPixel(&temp, x, destY, c);
            }
        }
    }
    /* Qt qbmphandler.cpp:355-356 直接把 BMP 的每米点数写入
     * QImage；传入 0 时 XImage 保留其默认分辨率，效果与 Qt
     * 新建图像的默认 3937 dots/meter 一致。 */
    XImage_setDotsPerMeterX(&temp, pelsPerMeterX);
    XImage_setDotsPerMeterY(&temp, pelsPerMeterY);
    XMove(out, &temp);
    ok = true;

bmp_done:
    if (!ok && tempInitialized) XImage_deinit_base(&temp);
    if (indexBuffer) XFree_System(indexBuffer);
    return ok;
}

/*
 * Qt 的 QBmpHandler::DibFormat 不读取 14 字节文件头。现有 BMP 解码器
 * 已完整覆盖 DIB 信息头、调色板、掩码和像素转换，因此这里仅构造一个
 * 临时的、边界受检的 BMP 文件头，把 bfOffBits 设为 Qt DIB 分支计算的
 * 像素起点，再复用同一实现。DIB 不支持自动魔数探测，只能由显式格式
 * 名称进入本函数。
 */
bool XImageCodecInternal_decodeDib(const uint8_t* data, size_t size, XImage* out)
{
    uint32_t dib;
    uint16_t bpp = 0;
    uint32_t compression = 0;
    uint32_t sizeImage = 0;
    uint32_t clrUsed = 0;
    size_t pixelOffset;
    size_t total;
    uint8_t* wrapped;
    bool ok;
    if (!data || !out || size < 12u || size > (size_t)UINT32_MAX - 14u)
        return false;
    dib = XImageCodecInternal_readU32LE(data);
    if (dib == 12u) {
        if (size < 12u) return false;
        bpp = XImageCodecInternal_readU16LE(data + 10u);
    } else if (dib == 40u || dib == 64u || dib == 108u || dib == 124u) {
        if (size < (size_t)dib) return false;
        bpp = XImageCodecInternal_readU16LE(data + 14u);
        compression = XImageCodecInternal_readU32LE(data + 16u);
        sizeImage = XImageCodecInternal_readU32LE(data + 20u);
        clrUsed = XImageCodecInternal_readU32LE(data + 32u);
    } else {
        /* decodeBmp() preserves Qt's old-header compatibility branch; for
         * offset calculation an unknown header has no optional fields. */
        pixelOffset = (size_t)dib;
        goto wrap;
    }

    /* qbmphandler.cpp:757-778: a non-zero biSizeImage that fits the
     * available DIB buffer takes precedence over masks and palettes. */
    if (sizeImage > 0u && (size_t)sizeImage < size)
        pixelOffset = size - (size_t)sizeImage;
    else {
        pixelOffset = (size_t)dib;
        if ((bpp == 16u || bpp == 32u) && compression == 3u)
            pixelOffset += 12u;
        else if ((bpp == 16u || bpp == 32u) && compression == 4u)
            pixelOffset += 16u;
        if (bpp <= 8u) {
            size_t entries = clrUsed ? (size_t)clrUsed : ((size_t)1u << bpp);
            if (entries == 0u || entries > 256u ||
                entries > (SIZE_MAX - pixelOffset) / (dib == 12u ? 3u : 4u))
                return false;
            pixelOffset += entries * (dib == 12u ? 3u : 4u);
        }
    }

wrap:
    if (pixelOffset > size || pixelOffset > (size_t)UINT32_MAX - 14u)
        return false;
    total = size + 14u;
    wrapped = (uint8_t*)XMalloc_System(total);
    if (!wrapped) return false;
    memset(wrapped, 0, 14u);
    XImageCodecInternal_writeU16LE(wrapped, 0x4d42u);
    XImageCodecInternal_writeU32LE(wrapped + 2u, (uint32_t)total);
    XImageCodecInternal_writeU32LE(wrapped + 10u,
                                   (uint32_t)(pixelOffset + 14u));
    memcpy(wrapped + 14u, data, size);
    ok = XImageCodecInternal_decodeBmp(wrapped, total, out);
    XFree_System(wrapped);
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
    /* Qt QBmpHandler::write_dib() stores dots-per-meter in the INFOHEADER
     * and uses 2834 (72 DPI) only when the source has no explicit value. */
    XImageCodecInternal_writeU32LE(header + 38,
                                   XImage_dotsPerMeterX(image) ?
                                       (uint32_t)XImage_dotsPerMeterX(image) : 2834u);
    XImageCodecInternal_writeU32LE(header + 42,
                                   XImage_dotsPerMeterY(image) ?
                                       (uint32_t)XImage_dotsPerMeterY(image) : 2834u);
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

bool XImageCodecInternal_encodeDib(const XImage* image, XByteArray* out)
{
    const XImage* source = image;
    XImage converted;
    XImageFormat format;
    int width;
    int height;
    int depth;
    int colorCount;
    int bitsPerLine;
    size_t bpl;
    size_t bplBmp;
    size_t colorBytes;
    size_t imageBytes;
    size_t total;
    int nbits;
    bool hasConverted = false;
    uint8_t* dst;
    int y;
    if (!image || !out || XImage_isNull(image)) return false;

    /* Qt QBmpHandler::write() normalizes unsupported source formats before
     * computing the DIB layout. Keep the conversion in project-owned XImage
     * APIs so no codec path needs a second pixel representation. */
    format = XImage_format(image);
    if (format == XImageFormat_MonoLSB ||
        format == XImageFormat_Alpha8 || format == XImageFormat_Grayscale8 ||
        (format != XImageFormat_Mono && format != XImageFormat_Indexed8 &&
         format != XImageFormat_RGB32 && format != XImageFormat_ARGB32)) {
        XImage_init(&converted);
        if (format == XImageFormat_MonoLSB)
            XImage_convertToFormat(image, XImageFormat_Mono, 0u, &converted);
        else if (format == XImageFormat_Alpha8 ||
                 format == XImageFormat_Grayscale8)
            XImage_convertToFormat(image, XImageFormat_Indexed8, 0u, &converted);
        else
            XImage_convertToFormat(image,
                                   XImage_hasAlphaChannel(image) ?
                                       XImageFormat_ARGB32 : XImageFormat_RGB32,
                                   0u, &converted);
        if (XImage_isNull(&converted)) {
            XImage_deinit_base(&converted);
            return false;
        }
        source = &converted;
        hasConverted = true;
    }

    width = XImage_width(source);
    height = XImage_height(source);
    depth = XImage_depth(source);
    colorCount = XImage_colorCount(source);
    if (width <= 0 || height <= 0 || depth <= 0 || depth > 32 ||
        colorCount < 0 || colorCount > 256) {
        if (hasConverted) XImage_deinit_base(&converted);
        return false;
    }
    if ((size_t)width > (SIZE_MAX - 31u) / (size_t)depth) {
        if (hasConverted) XImage_deinit_base(&converted);
        return false;
    }
    if (((size_t)width * (size_t)depth + 31u) / 32u > (size_t)INT_MAX) {
        if (hasConverted) XImage_deinit_base(&converted);
        return false;
    }
    bitsPerLine = (int)(((size_t)width * (size_t)depth + 31u) / 32u);
    if ((size_t)bitsPerLine > SIZE_MAX / 4u) {
        if (hasConverted) XImage_deinit_base(&converted);
        return false;
    }
    bpl = (size_t)bitsPerLine * 4u;
    bplBmp = bpl;
    nbits = depth;
    if (depth == 8 && colorCount <= 16) {
        if (bpl > SIZE_MAX - 1u || (bpl + 1u) / 2u > SIZE_MAX - 3u) {
            if (hasConverted) XImage_deinit_base(&converted);
            return false;
        }
        bplBmp = ((bpl + 1u) / 2u + 3u) & ~((size_t)3u);
        nbits = 4;
    } else if (depth == 32) {
        if ((size_t)width > (SIZE_MAX - 31u) / 24u) {
            if (hasConverted) XImage_deinit_base(&converted);
            return false;
        }
        bplBmp = (((size_t)width * 24u + 31u) / 32u) * 4u;
        nbits = 24;
    }
    if ((size_t)height > SIZE_MAX / bplBmp) {
        if (hasConverted) XImage_deinit_base(&converted);
        return false;
    }
    imageBytes = bplBmp * (size_t)height;
    colorBytes = depth == 32 ? 0u : (size_t)colorCount * 4u;
    if (imageBytes > UINT32_MAX || imageBytes > SIZE_MAX - 40u ||
        colorBytes > SIZE_MAX - 40u - imageBytes) {
        if (hasConverted) XImage_deinit_base(&converted);
        return false;
    }
    total = 40u + colorBytes + imageBytes;
    if (!XByteArray_resize_base((XVector*)out, total)) {
        if (hasConverted) XImage_deinit_base(&converted);
        return false;
    }
    dst = XByteArray_data(out);
    memset(dst, 0, total);
    XImageCodecInternal_writeU32LE(dst, 40u);
    XImageCodecInternal_writeU32LE(dst + 4u, (uint32_t)width);
    XImageCodecInternal_writeU32LE(dst + 8u, (uint32_t)height);
    XImageCodecInternal_writeU16LE(dst + 12u, 1u);
    XImageCodecInternal_writeU16LE(dst + 14u, (uint16_t)nbits);
    XImageCodecInternal_writeU32LE(dst + 16u, 0u);
    XImageCodecInternal_writeU32LE(dst + 20u, (uint32_t)imageBytes);
    XImageCodecInternal_writeU32LE(dst + 24u,
                                   XImage_dotsPerMeterX(source) ?
                                       (uint32_t)XImage_dotsPerMeterX(source) : 2834u);
    XImageCodecInternal_writeU32LE(dst + 28u,
                                   XImage_dotsPerMeterY(source) ?
                                       (uint32_t)XImage_dotsPerMeterY(source) : 2834u);
    XImageCodecInternal_writeU32LE(dst + 32u, (uint32_t)colorCount);
    XImageCodecInternal_writeU32LE(dst + 36u, (uint32_t)colorCount);
    if (depth != 32) {
        int i;
        for (i = 0; i < colorCount; ++i) {
            uint32_t color = XImage_color(source, i);
            uint8_t* entry = dst + 40u + (size_t)i * 4u;
            entry[0] = (uint8_t)color;
            entry[1] = (uint8_t)(color >> 8);
            entry[2] = (uint8_t)(color >> 16);
        }
    }
    if (nbits == 1 || nbits == 8) {
        int sourceBpl = XImage_bytesPerLine(source);
        if (sourceBpl < 0 || (size_t)sourceBpl < bpl) {
            if (hasConverted) XImage_deinit_base(&converted);
            return false;
        }
        for (y = height - 1; y >= 0; --y) {
            const uint8_t* pixels = XImage_constScanLine(source, y);
            if (!pixels) {
                if (hasConverted) XImage_deinit_base(&converted);
                return false;
            }
            memcpy(dst + 40u + colorBytes + (size_t)(height - 1 - y) * bplBmp,
                   pixels, bpl);
        }
    } else {
        for (y = height - 1; y >= 0; --y) {
            uint8_t* row = dst + 40u + colorBytes +
                           (size_t)(height - 1 - y) * bplBmp;
            int x;
            if (nbits == 4) {
                const uint8_t* pixels = XImage_constScanLine(source, y);
                if (!pixels || XImage_bytesPerLine(source) < width) {
                    if (hasConverted) XImage_deinit_base(&converted);
                    return false;
                }
                for (x = 0; x < width / 2; ++x)
                    row[x] = (uint8_t)((pixels[x * 2] << 4) |
                                       (pixels[x * 2 + 1] & 0x0fu));
                if (width & 1) row[width / 2] = (uint8_t)(
                    pixels[width - 1] << 4);
            } else {
                for (x = 0; x < width; ++x) {
                    uint32_t color = XImage_pixel(source, x, y);
                    row[x * 3] = (uint8_t)color;
                    row[x * 3 + 1] = (uint8_t)(color >> 8);
                    row[x * 3 + 2] = (uint8_t)(color >> 16);
                }
            }
        }
    }
    if (hasConverted) XImage_deinit_base(&converted);
    return true;
}

#endif /* XIMAGECODEC_BMP_ON */
#endif /* XIMAGECODEC_ON */
