/*****************************************************************************/
/**
 * @file       XImageCodecJpeg.c
 * @brief      XImageCodec JPEG 格式独立实现（解码：SOF0/1/2 与 SOF9/10；
 *             编码：基线 SOF0，固定 4:2:0 输出）。
 * @note       受 XIMAGECODEC_JPEG_ON 开关控制，可通过 XImageCodec_config.h
 *             单独裁剪；扩展能力受 XIMAGECODEC_JPEG_PROGRESSIVE_ON /
 *             XIMAGECODEC_JPEG_ARITHMETIC_ON / XIMAGECODEC_JPEG_12BIT_ON /
 *             XIMAGECODEC_JPEG_CMYK_ON 子开关控制（默认全开，PC 全功能，
 *             嵌入式可裁剪）。
 * @note       解码完整支持：
 *             - SOF0/SOF1 顺序、SOF2 渐进式（DC first/refine、AC first/refine，
 *               任意光谱选择 Ss..Se 的多扫描顺序）；SOF9/SOF10 算术熵编码
 *               （ITU-T T.81 Annex D/F 概率估计表，含 DAC 条件表与重启动）。
 *             - 8 位与 12 位样本精度（量化表 DQT 8/16 位、pq=0/1）。
 *             - 1~4 分量：灰度、YCbCr（1/2/4 水平与垂直抽样因子，4:4:4、
 *               4:2:2、4:2:0、4:4:0 等）、CMYK/YCCK（Adobe APP14 transform）。
 *             - DRI/RSTn 重启动间隔（Huffman 与算术），0xFF00 填充字节。
 *             编码输出 YCbCr 4:2:0、8 位、标准量化表（按质量缩放）与标准
 *             Huffman 表的基线 JPEG（SOF0），可被任意标准解码器还原。
 *             不依赖任何第三方库与数学库：DCT/IDCT 使用编译期常量矩阵，
 *             无网络、无后台 API，可在嵌入式环境直接编译使用。
 *             本实现只通过 XImageCodecInternal_decodeJpeg/encodeJpeg 对外暴露，
 *             统一由 XImageCodec.c 的 XImageCodec_decode/encode 分发。
 */
#include "XImageCodec_config.h"
#include "XImageCodecInternal.h"
#include "XImage.h"
#include "XMemory.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

#if XIMAGECODEC_ON
#if XIMAGECODEC_JPEG_ON

/* ====================================================================== */
/* 常量表                                                                   */
/* ====================================================================== */

/* 标准之字形扫描：jpegZigzag[k] 为第 k 个之字形位置对应的自然块坐标（0..63）。 */
static const uint8_t jpegZigzagNatural[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* 自然顺序系数转置：JPEG T.81 将系数按 (垂直频率*8 + 水平频率) 排布，
 * 而本项目 DCT 矩阵按 (水平频率*8 + 垂直频率) 存放（自洽但方向为转置）。
 * 与标准文件互换数据时必须转置，保证编码输出可被标准解码器正确还原。 */
static int jpegTransposeIndex(int n)
{
    return (n % 8) * 8 + (n / 8);
}

/* 标准亮度量化表（自然顺序，ITU-T T.81 表 K.1）。 */
static const uint8_t jpegQuantLuminance[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
};

/* 标准色度量化表（自然顺序，ITU-T T.81 表 K.2）。 */
static const uint8_t jpegQuantChrominance[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

/* 标准 DC 亮度 Huffman 表（ITU-T T.81 表 K.3）：16 个长度计数 + 符号。 */
static const uint8_t jpegStdDcLumCounts[16] = {
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t jpegStdDcSymbols[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

/* 标准 DC 色度 Huffman 表（表 K.4）。 */
static const uint8_t jpegStdDcChromaCounts[16] = {
    0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
};
static const uint8_t jpegStdDcChromaSymbols[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

/* 标准 AC 亮度 Huffman 表（表 K.5），162 个符号。 */
static const uint8_t jpegStdAcLumCounts[16] = {
    0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d
};
static const uint8_t jpegStdAcLumSymbols[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
    0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
    0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
    0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

/* 标准 AC 色度 Huffman 表（表 K.6），162 个符号。 */
static const uint8_t jpegStdAcChromaCounts[16] = {
    0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77
};
static const uint8_t jpegStdAcChromaSymbols[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
    0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
    0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa
};

/* 8x8 正交 DCT 基矩阵 JPEG_DCT[u][x] = C(u)/2 * cos((2x+1)*u*PI/16)。
 * 同一矩阵正反对称：前向 F = D*f*D^T，逆向 f = D^T*F*D。
 * 编译期常量，避免运行时调用数学库函数（嵌入式可用）。 */
static const float jpegDctBase[8][8] = {
    { 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f },
    { 0.4903926402f, 0.4157348062f, 0.2777851165f, 0.09754516101f, -0.09754516101f, -0.2777851165f, -0.4157348062f, -0.4903926402f },
    { 0.4619397663f, 0.1913417162f, -0.1913417162f, -0.4619397663f, -0.4619397663f, -0.1913417162f, 0.1913417162f, 0.4619397663f },
    { 0.4157348062f, -0.09754516101f, -0.4903926402f, -0.2777851165f, 0.2777851165f, 0.4903926402f, 0.09754516101f, -0.4157348062f },
    { 0.3535533906f, -0.3535533906f, -0.3535533906f, 0.3535533906f, 0.3535533906f, -0.3535533906f, -0.3535533906f, 0.3535533906f },
    { 0.2777851165f, -0.4903926402f, 0.09754516101f, 0.4157348062f, -0.4157348062f, -0.09754516101f, 0.4903926402f, -0.2777851165f },
    { 0.1913417162f, -0.4619397663f, 0.4619397663f, -0.1913417162f, -0.1913417162f, 0.4619397663f, -0.4619397663f, 0.1913417162f },
    { 0.09754516101f, -0.2777851165f, 0.4157348062f, -0.4903926402f, 0.4903926402f, -0.4157348062f, 0.2777851165f, -0.09754516101f }
};

/* ====================================================================== */
/* 位读取器（熵编码数据流，含 0xFF00 填充字节处理）                          */
/* ====================================================================== */

/** JPEG 熵编码位读取器。
 *  位缓冲约定：acc 的 [byteCount-1..0] 位存放尚未消费的位流数据，
 *  紧邻最高有效位（bit byteCount-1）是流中的下一位（MSB 在前）。
 *  消费时仅减少 byteCount，已消费的位随后续左移补字节自然离开 32 位寄存器。 */
typedef struct JpegBitReader
{
    const uint8_t* data;   /**< 输入数据指针 */
    size_t         size;   /**< 输入数据字节数 */
    size_t         pos;    /**< 当前字节位置 */
    uint32_t       acc;    /**< 位累加器（有效位右对齐，下一位在 bit byteCount-1） */
    int            byteCount; /**< 累加器中有效未消费位数（0..23） */
    bool           error;  /**< 出错标志 */
} JpegBitReader;

/* 读取下一个原始字节（跳过 0xFF00 填充），成功返回 true。 */
static bool jpegBitRefill(JpegBitReader* b, int need)
{
    while (b->byteCount < need) {
        uint8_t c, c2;
        if (b->pos >= b->size) { b->error = true; return false; }
        c = b->data[b->pos++];
        if (c == 0xFF) {
            /* 字节填充：0xFF 0x00 表示数据字节 0xFF；
               其余 0xFF 标记不允许出现在熵数据中（RST 在两 MCU 之间处理）。 */
            if (b->pos >= b->size) { b->error = true; return false; }
            c2 = b->data[b->pos++];
            if (c2 != 0x00) { b->error = true; return false; }
            c = 0xFF;
        }
        /* 新的一个字节进入低 8 位，原有效位左移 8 位保持流顺序；
           已消费位位于 bit byteCount 之上，左移后被自然丢弃。
           入读前 byteCount <= 15（need <= 16），故 byteCount <= 23，无溢出。 */
        b->acc = (b->acc << 8) | c;
        b->byteCount += 8;
    }
    return true;
}

/* 读取 1 位，成功返回 true。 */
static bool jpegGetBit(JpegBitReader* b, int* out)
{
    if (b->byteCount < 1) {
        if (!jpegBitRefill(b, 1)) return false;
    }
    *out = (int)((b->acc >> (b->byteCount - 1)) & 1u);
    --b->byteCount;
    return true;
}

/* 窥视 n 位（n <= 16），返回值为 n 位整数（首个流位为最高位），失败返回 -1。 */
static int jpegPeekBits(JpegBitReader* b, int n)
{
    if (n < 0 || n > 16) { b->error = true; return -1; }
    if (b->byteCount < n) {
        if (!jpegBitRefill(b, n)) return -1;
    }
    return (int)((b->acc >> (b->byteCount - n)) & ((1u << n) - 1u));
}

/* 丢弃 n 位（n > 0 且 n <= byteCount）。 */
static void jpegDropBits(JpegBitReader* b, int n)
{
    b->byteCount -= n;
}

/* 读入 n 位裸数值（无符号扩展，用于 EOBRUN 的附加位数，T.81 G.1.4）。 */
static int jpegReadBits(JpegBitReader* b, int n)
{
    int v;
    if (n <= 0) return 0;
    v = jpegPeekBits(b, n);
    if (v < 0) return 0;
    jpegDropBits(b, n);
    return v;
}

/* 对齐到字节边界（丢弃当前字节剩余位）。 */
static void jpegAlignToByte(JpegBitReader* b)
{
    int pad = b->byteCount & 7;
    if (pad) jpegDropBits(b, pad);
    b->acc = 0;
    b->byteCount = 0;
}

/* ====================================================================== */
/* Huffman 表                                                              */
/* ====================================================================== */

/** JPEG Huffman 表（解码用）。 */
typedef struct JpegHuff
{
    uint8_t count[16];   /**< 各码长对应的符号数量（索引为长度-1） */
    uint8_t symbol[256]; /**< 符号序列（按规范排列） */
    int     total;       /**< 符号总数 */
} JpegHuff;

/** JPEG Huffman 表（编码用，含规范码字）。 */
typedef struct JpegHuffEncode
{
    uint16_t code[256];  /**< 符号 -> 规范码字 */
    uint8_t  size[256];  /**< 符号 -> 码长 */
    int      total;
} JpegHuffEncode;

/* 从 DHT 载荷构造解码表（返回消费字节数，失败返回 -1）。 */
static int jpegHuffmanBuildDecode(const uint8_t* p, size_t len, JpegHuff* out)
{
    int idx = 0, total = 0;
    if (!p || !out || len < 17) return -1;
    for (int i = 0; i < 16; ++i) {
        out->count[i] = p[1 + i];
        total += out->count[i];
    }
    if (total > 256 || (size_t)(17 + total) > len) return -1;
    for (int i = 0; i < total; ++i)
        out->symbol[i] = p[17 + i];
    out->total = total;
    return 17 + total;
}

/* 由长度计数与符号构造编码表（规范码字，逐符号生成）。 */
static void jpegHuffmanBuildEncode(const uint8_t counts[16],
                                   const uint8_t* symbols,
                                   int symbolCount,
                                   JpegHuffEncode* out)
{
    uint16_t code = 0;
    int idx = 0;
    memset(out, 0, sizeof(*out));
    for (int len = 1; len <= 16; ++len) {
        for (int i = 0; i < counts[len - 1]; ++i) {
            uint8_t sym = symbols[idx];
            out->code[sym] = code;
            out->size[sym] = (uint8_t)len;
            ++code;
            ++idx;
        }
        code <<= 1;
    }
    out->total = symbolCount;
}

/* 解码一个 Huffman 符号，失败返回 -1。 */
static int jpegDecodeSymbol(JpegBitReader* b, const JpegHuff* t)
{
    int code = 0, first = 0, idx = 0;
    for (int len = 1; len <= 16; ++len) {
        int bit;
        if (!jpegGetBit(b, &bit)) return -1;
        code = (code << 1) | bit;
        if (code - first < t->count[len - 1]) {
            int sym = t->symbol[idx + code - first];
            if (idx + code - first >= t->total) { b->error = true; return -1; }
            return sym;
        }
        idx += t->count[len - 1];
        first = (first + t->count[len - 1]) << 1;
    }
    b->error = true;
    return -1;
}

/* 读入 s 位并做符号扩展（JPEG receive_extend）。 */
static int jpegReceiveExtend(JpegBitReader* b, int s)
{
    int v;
    if (s <= 0) return 0;
    v = jpegPeekBits(b, s);
    if (v < 0) return 0;
    jpegDropBits(b, s);
    if (v < (1 << (s - 1))) v -= (1 << s) - 1;
    return v;
}

/* ====================================================================== */
/* DCT / IDCT（浮点矩阵，编译期常量，无数学库依赖）                          */
/* ====================================================================== */

/* 值钳制到 [0,255]。 */
static uint8_t jpegClamp255(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* 四舍五入到最近整数。 */
static int jpegRound(float v)
{
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

/* 前向 DCT：8x8 块（samples 为已做电平搬移的数据，-128..127）→ F。 */
static void jpegForwardDct(const float samples[64], float f[64])
{
    float tmp[8][8];
    for (int v = 0; v < 8; ++v) {
        for (int x = 0; x < 8; ++x) {
            float s = 0.0f;
            for (int y = 0; y < 8; ++y)
                s += jpegDctBase[v][y] * samples[y * 8 + x];
            tmp[x][v] = s;
        }
    }
    for (int u = 0; u < 8; ++u) {
        for (int v = 0; v < 8; ++v) {
            float s = 0.0f;
            for (int x = 0; x < 8; ++x)
                s += jpegDctBase[u][x] * tmp[x][v];
            f[u * 8 + v] = s;
        }
    }
}

/* 逆 DCT：F → 8x8 样本（+128 电平搬移，输出行优先）。 */
static void jpegInverseDct(const float f[64], uint8_t out[64])
{
    float tmp[8][8];
    float res[8][8];
    for (int x = 0; x < 8; ++x) {
        for (int v = 0; v < 8; ++v) {
            float s = 0.0f;
            for (int u = 0; u < 8; ++u)
                s += jpegDctBase[u][x] * f[u * 8 + v];
            tmp[x][v] = s;
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float s = 0.0f;
            for (int v = 0; v < 8; ++v)
                s += jpegDctBase[v][y] * tmp[x][v];
            res[y][x] = s;
        }
    }
    for (int i = 0; i < 64; ++i)
        out[i] = jpegClamp255(jpegRound(res[i / 8][i % 8] + 128.0f));
}

/* 解码框架（支持基线/渐进/12 位/算术/CMYK）                                */
/* ====================================================================== */

/** 解码帧分量。 */
typedef struct JpegFrameComp
{
    int id;    /**< 分量标识（1=Y, 2=Cb, 3=Cr, 4=C/M/Y/K 等） */
    int h;     /**< 水平抽样因子（1..4） */
    int v;     /**< 垂直抽样因子（1..4） */
    int tq;    /**< 量化表选择（0..3） */
} JpegFrameComp;

/** 解码帧（SOF）信息，支持 SOF0/1/2 与 SOF9/10（算术熵编码）。 */
typedef struct JpegFrame
{
    int width;            /**< 图像宽度（像素） */
    int height;           /**< 图像高度（像素） */
    int precision;        /**< 样本精度（8 或 12） */
    int nf;               /**< 分量数量（1..4） */
    int maxH, maxV;       /**< 最大抽样因子 */
    int progressive;      /**< 渐进式（SOF2/SOF10） */
    int arithmetic;       /**< 算术熵编码（SOF9/SOF10） */
    int adobe;            /**< 找到 Adobe APP14 标记 */
    int adobeTransform;   /**< Adobe APP14 转换字段（0=CMYK,1=YCbCr,2=YCCK） */
    bool parsed;          /**< SOF 是否已解析 */
    JpegFrameComp comp[4]; /**< 分量（最多 4 个） */
} JpegFrame;

/** 解码扫描（SOS）分量。 */
typedef struct JpegScanComp
{
    int frameIdx;  /**< 对应 JpegFrame.comp 索引 */
    int tdc;       /**< DC Huffman/条件表号（0..3） */
    int tac;       /**< AC Huffman/条件表号（0..3） */
} JpegScanComp;

/** 解码扫描（SOS）描述：分量子集 + 光谱选择 + 逐次逼近参数。 */
typedef struct JpegScan
{
    int ns;               /**< 扫描中分量数量 */
    JpegScanComp comp[4]; /**< 扫描分量 */
    int Ss, Se;           /**< 光谱选择（起始/结束之字形位置） */
    int Ah, Al;           /**< 逐次逼近：高位已传位数/本扫描低位位数 */
} JpegScan;

/** 解码样本平面（8 位或 12 位样本；平面为块网格尺寸并含抽样因子）。 */
typedef struct JpegSamplePlane
{
    uint8_t* data;    /**< 平面数据（每样本 byteDepth 字节） */
    int      pw;      /**< 平面宽（像素） */
    int      ph;      /**< 平面高（像素） */
    int      h, v;    /**< 抽样因子 */
    int      byteDepth; /**< 1=8 位，2=12 位 */
} JpegSamplePlane;

/** 渐进式/顺序解码系数平面（每块 64 个量化系数，自然顺序）。 */
typedef struct JpegCoeffPlane
{
    int32_t* data;    /**< 系数数据（blocksW*blocksH*64） */
    int      blocksW; /**< 块列数 */
    int      blocksH; /**< 块行数 */
} JpegCoeffPlane;

/** 算术熵解码器状态（JPEG 规范 Annex D，ITU-T T.81 图 D.2）。 */
typedef struct JpegArith
{
    const uint8_t* data;  /**< 输入数据指针 */
    size_t         size;  /**< 输入数据字节数 */
    size_t         pos;   /**< 当前字节位置 */
    uint32_t       c;     /**< C 寄存器（编码区间基 + 输入位缓冲） */
    uint32_t       a;     /**< A 寄存器（归一化后的区间大小） */
    int            ct;    /**< 位计数（初始化 -16，运行 0..7） */
    int            unreadMarker; /**< 已读到的未处理标记（0=无） */
    uint8_t        dcStats[4][64];  /**< DC 统计区（每条件表 64 bin） */
    uint8_t        acStats[4][256]; /**< AC 统计区（每条件表 256 bin） */
    uint8_t       fixedBin;  /**< 固定概率 0.5 的 bin（索引 113） */
    int            dcContext[4]; /**< DC 条件上下文（按帧分量索引） */
} JpegArith;

/** 当前 JPEG 文件解码上下文。 */
typedef struct JpegCtx
{
    JpegFrame          frame;
    JpegHuff           huff[2][4];
    bool               haveHuff[2][4];
    int                quant[4][64]; /**< 量化表（DQT 8/16 位统一展开） */
    bool               haveQuant[4];
    int                restart;      /**< 重启动间隔（MCU 数），0=无 */
    uint8_t            arithDcL[4];  /**< 算术 DC 条件 L（默认 0） */
    uint8_t            arithDcU[4];  /**< 算术 DC 条件 U（默认 1） */
    uint8_t            arithAcK[4];  /**< 算术 AC 条件 K（默认 5） */
    JpegCoeffPlane     coeff[4];     /**< 渐进式系数平面（仅渐进时分配） */
    JpegSamplePlane    planes[4];    /**< 样本平面（每帧分量一个） */
    int                dcPred[4];    /**< DC 预测值（按帧分量索引停驻） */
    int                eobrun;       /**< 渐进 AC EOBRUN（按扫描持久） */
    JpegBitReader      bits;         /**< Huffman 位读取器 */
    JpegArith          arith;        /**< 算术熵解码器（算术模式使用） */
    int                frBlkW;       /**< 该分量块网格宽（块单元） */
    int                frBlkH;       /**< 该分量块网格高（块单元） */
} JpegCtx;

/* 当前位置读取 16 位大端整数。 */
static int jpegReadBe16(const uint8_t* data, size_t len, size_t* pos)
{
    if (*pos + 2 > len) return -1;
    {
        int v = ((int)data[*pos] << 8) | data[*pos + 1];
        *pos += 2;
        return v;
    }
}

/* 读取下一个标记（可跳过填充/前导字节），失败返回 -1。 */
static int jpegNextMarker(const uint8_t* data, size_t len, size_t* pos)
{
    if (!data || !pos || *pos >= len) return -1;
    while (*pos < len && data[*pos] != 0xFF) ++(*pos);
    while (*pos < len && data[*pos] == 0xFF) ++(*pos);
    if (*pos >= len) return -1;
    return data[(*pos)++];
}

/* 跳过可忽略标记段（返回 false 表示数据不足/段长非法）。 */
static bool jpegSkipMarker(const uint8_t* data, size_t len, size_t* pos)
{
    int segLen;
    if (*pos + 2 > len) return false;
    segLen = jpegReadBe16(data, len, pos);
    if (segLen < 2 || *pos + (size_t)(segLen - 2) > len) return false;
    *pos += (size_t)(segLen - 2);
    return true;
}

/* 解析 DQT，支持 pq=0（8 位）与 pq=1（16 位）；失败返回 false。 */
static bool jpegParseDqt(const uint8_t* data, size_t len, size_t* pos,
                         int quant[4][64], bool haveQuant[4])
{
    int segLen;
    size_t end;
    if (*pos + 2 > len) return false;
    segLen = jpegReadBe16(data, len, pos);
    if (segLen < 2) return false;
    end = *pos + (size_t)(segLen - 2);
    if (end > len) return false;
    while (*pos < end) {
        int pq, tq, precBytes;
        if (end - *pos < 65) return false;
        pq = data[*pos] >> 4;
        tq = data[*pos] & 15;
        ++(*pos);
        if (pq != 0 && pq != 1) return false;
        if (tq > 3) return false;
        precBytes = (pq == 1) ? 2 : 1;
        if (end - *pos < (size_t)precBytes * 64) return false;
        for (int k = 0; k < 64; ++k) {
            int v;
            if (pq == 1)
                v = ((int)data[*pos] << 8) | data[*pos + 1];
            else
                v = data[*pos];
            *pos += (size_t)precBytes;
            /* DQT 值为之字形顺序，转自然顺序存储。 */
            quant[tq][jpegZigzagNatural[k]] = v;
        }
        haveQuant[tq] = true;
    }
    *pos = end;
    return true;
}

/* 解析 DHT（DC/AC 标准或自定义 Huffman 表）；失败返回 false。 */
static bool jpegParseDht(const uint8_t* data, size_t len, size_t* pos,
                         JpegHuff huff[2][4], bool haveHuff[2][4])
{
    int segLen, tc, th, consumed;
    size_t end;
    if (*pos + 2 > len) return false;
    segLen = jpegReadBe16(data, len, pos);
    if (segLen < 2) return false;
    end = *pos + (size_t)(segLen - 2);
    if (end > len) return false;
    while (*pos < end) {
        uint8_t tcTh;
        if (end - *pos < 17) return false;
        tcTh = data[*pos];
        tc = tcTh >> 4;
        th = tcTh & 15;
        if (tc > 1 || th > 3) return false;
        /* jpegHuffmanBuildDecode 以 [class,len16,symbols...] 布局解析 */
        consumed = jpegHuffmanBuildDecode(data + (*pos), end - *pos,
                                          &huff[tc][th]);
        if (consumed < 0) return false;
        haveHuff[tc][th] = true;
        *pos += (size_t)consumed;
    }
    *pos = end;
    return true;
}

/* 解析 DAC（定义算术编码条件表）；失败返回 false。 */
static bool jpegParseDac(const uint8_t* data, size_t len, size_t* pos,
                         uint8_t dcL[4], uint8_t dcU[4], uint8_t acK[4])
{
    int segLen;
    size_t end;
    if (*pos + 2 > len) return false;
    segLen = jpegReadBe16(data, len, pos);
    if (segLen < 2) return false;
    end = *pos + (size_t)(segLen - 2);
    if (end > len) return false;
    while (*pos + 2 <= end) {
        uint8_t index = data[(*pos)++];
        uint8_t val = data[(*pos)++];
        /* T.81 B.1.5：索引高半字节为表类别（0=DC/1=AC），低半字节为表号 0..3 */
        uint8_t cls = (uint8_t)((index >> 4) & 0x0F);
        uint8_t tbl = (uint8_t)(index & 0x0F);
        if (cls > 1 || tbl > 3) return false;
        if (cls == 1) {
            acK[tbl] = val;
        } else {
            dcL[tbl] = (uint8_t)(val & 0x0F);
            dcU[tbl] = (uint8_t)(val >> 4);
            if (dcL[tbl] > dcU[tbl]) return false;
        }
    }
    *pos = end;
    return true;
}

/* 解析 SOF（支持 SOF0/1/2 与 SOF9/10，精度 8/12）；失败返回 false。 */
static bool jpegParseSof(const uint8_t* data, size_t len, size_t* pos,
                         int marker, JpegFrame* frame)
{
    int segLen, precision, height, width, nf;
    size_t end;
    int maxH, maxV, blockSum;
    bool arithmetic = (marker == 0xC9 || marker == 0xCA) ? true : false;
    bool progressive = (marker == 0xC2 || marker == 0xCA) ? true : false;
    if (marker != 0xC0 && marker != 0xC1 && marker != 0xC2 &&
        marker != 0xC9 && marker != 0xCA)
        return false; /* C3/C5/C6/C7/CB/CD/CF（无损/分层）不支持 */
#if !XIMAGECODEC_JPEG_PROGRESSIVE_ON
    if (progressive) return false; /* 渐进式扩展被裁剪 */
#endif
#if !XIMAGECODEC_JPEG_ARITHMETIC_ON
    if (arithmetic) return false;  /* 算术编码扩展被裁剪 */
#endif
    if (*pos + 2 > len) return false;
    segLen = jpegReadBe16(data, len, pos);
    if (segLen < 8) return false;
    end = *pos + (size_t)(segLen - 2);
    if (end > len) return false;
    precision = data[(*pos)++];
#if !XIMAGECODEC_JPEG_12BIT_ON
    if (precision != 8) return false; /* 12 位扩展被裁剪 */
#endif
    height = jpegReadBe16(data, len, pos);
    width = jpegReadBe16(data, len, pos);
    nf = data[(*pos)++];
    if ((precision != 8 && precision != 12) ||
        width <= 0 || height <= 0 || nf < 1 || nf > 4)
        return false;
    memset(frame, 0, sizeof(*frame));
    frame->width = width;
    frame->height = height;
    frame->nf = nf;
    frame->precision = precision;
    frame->progressive = progressive ? 1 : 0;
    frame->arithmetic = arithmetic ? 1 : 0;
    maxH = 1;
    maxV = 1;
    blockSum = 0;
    for (int i = 0; i < nf; ++i) {
        JpegFrameComp* c;
        int hv;
        if (*pos + 3 > end) return false;
        c = &frame->comp[i];
        c->id = data[(*pos)++];
        hv = data[(*pos)++];
        c->h = (hv >> 4) & 15;
        c->v = hv & 15;
        c->tq = data[(*pos)++] & 15;
        if (c->h < 1 || c->h > 4 || c->v < 1 || c->v > 4)
            return false;
        if (c->h > maxH) maxH = c->h;
        if (c->v > maxV) maxV = c->v;
        if (c->h * c->v > 10) return false;
        blockSum += c->h * c->v;
    }
    if (maxH > 4 || maxV > 4 || blockSum > 10) return false;
    frame->maxH = maxH;
    frame->maxV = maxV;
    frame->parsed = true;
    *pos = end;
    return true;
}

/* 解析 SOS（任意光谱选择/逐次逼近参数，用于顺序与渐进式扫描）。 */
static bool jpegParseSos(const uint8_t* data, size_t len, size_t* pos,
                         const JpegFrame* frame, JpegScan* scan)
{
    int segLen, n;
    size_t end;
    if (*pos + 2 > len) return false;
    segLen = jpegReadBe16(data, len, pos);
    if (segLen < 6) return false;
    end = *pos + (size_t)(segLen - 2);
    if (end > len) return false;
    n = data[(*pos)++];
    if (n < 1 || n > 4) return false;
    memset(scan, 0, sizeof(*scan));
    for (int i = 0; i < n; ++i) {
        int id, tab, found = -1;
        if (*pos + 2 > end) return false;
        id = data[(*pos)++];
        tab = data[(*pos)++];
        for (int j = 0; j < frame->nf; ++j) {
            if (frame->comp[j].id == id) { found = j; break; }
        }
        if (found < 0) return false;
        scan->comp[i].frameIdx = found;
        scan->comp[i].tdc = (tab >> 4) & 15;
        scan->comp[i].tac = tab & 15;
        if (scan->comp[i].tdc > 3 || scan->comp[i].tac > 3) return false;
    }
    if (*pos + 3 > end) return false;
    scan->ns = n;
    scan->Ss = data[(*pos)++];
    scan->Se = data[(*pos)++];
    scan->Ah = (data[*pos] >> 4) & 15;
    scan->Al = data[*pos] & 15;
    ++(*pos);
    *pos = end;
    if (scan->Ss > scan->Se || scan->Se > 63 || scan->Ah > 13 ||
        scan->Al > 13)
        return false;
    /* 逐次逼近精化扫描要求 Al = Ah-1（一次只传输一个低位）。 */
    if (scan->Ah != 0 && scan->Ah - 1 != scan->Al) return false;
#if !XIMAGECODEC_JPEG_PROGRESSIVE_ON
    if (frame->progressive) return false;
#endif
    return true;
}

/* ====================================================================== */
/* 逆 DCT（支持 8/12 位精度电平搬移）                                       */
/* ====================================================================== */

/* 逆 DCT：F → 8x8 浮点样本（未做电平搬移）。 */
static void jpegInverseDctF(const float f[64], float res[64])
{
    float tmp[8][8];
    float out[8][8];
    for (int x = 0; x < 8; ++x) {
        for (int v = 0; v < 8; ++v) {
            float s = 0.0f;
            for (int u = 0; u < 8; ++u)
                s += jpegDctBase[u][x] * f[u * 8 + v];
            tmp[x][v] = s;
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float s = 0.0f;
            for (int v = 0; v < 8; ++v)
                s += jpegDctBase[v][y] * tmp[x][v];
            out[y][x] = s;
        }
    }
    for (int i = 0; i < 64; ++i) res[i] = out[i / 8][i % 8];
}

/* 按精度钳制样本（8 位 [0,255] / 12 位 [0,4095]）。 */
static int jpegClampSample(int v, int precision)
{
    int maxSample = precision >= 12 ? 4095 : 255;
    return v < 0 ? 0 : (v > maxSample ? maxSample : v);
}

/* 计算某分量的块网格尺寸。 */
static void jpegFrameBlockGrid(const JpegCtx* ctx, int compIdx,
                               int* blocksW, int* blocksH)
{
    const JpegFrameComp* fc = &ctx->frame.comp[compIdx];
    int mcuW = (ctx->frame.width + 8 * ctx->frame.maxH - 1) /
               (8 * ctx->frame.maxH);
    int mcuH = (ctx->frame.height + 8 * ctx->frame.maxV - 1) /
               (8 * ctx->frame.maxV);
    /* 分量块网格必须与扫描数据中的实际块排布一致：交错扫描按 MCU 推进，
       每个 MCU 含 h*v 个数据块（T.81 B.2.3），因此分量行块数为
       ceil(宽/(8*maxH))*h（而非 ceil(宽*h/(8*maxH))），否则右/下边缘
       不完整 MCU 的块会被误判越界（渐进式尤其明显）。 */
    *blocksW = mcuW * fc->h;
    *blocksH = mcuH * fc->v;
    if (*blocksW < 1) *blocksW = 1;
    if (*blocksH < 1) *blocksH = 1;
}

/* 样本平面读写（8 位为 1 字节，12 位为小端 2 字节）。 */
static void jpegPlaneSet(JpegSamplePlane* pl, int x, int y, int v)
{
    size_t off;
    if (!pl || !pl->data || x < 0 || y < 0 || x >= pl->pw || y >= pl->ph)
        return;
    off = (size_t)y * (size_t)pl->pw + (size_t)x;
    if (pl->byteDepth == 1) {
        pl->data[off] = (uint8_t)v;
    } else {
        pl->data[off * 2] = (uint8_t)(v & 0xFFu);
        pl->data[off * 2 + 1] = (uint8_t)((v >> 8) & 0xFFu);
    }
}

static int jpegPlaneGet(const JpegSamplePlane* pl, int x, int y)
{
    size_t off;
    if (!pl || !pl->data || x < 0 || y < 0 || x >= pl->pw || y >= pl->ph)
        return 0;
    off = (size_t)y * (size_t)pl->pw + (size_t)x;
    if (pl->byteDepth == 1) return pl->data[off];
    return (int)pl->data[off * 2] | ((int)pl->data[off * 2 + 1] << 8);
}

/* 按抽样因子最近邻上采样取样本（坐标为帧像素坐标）。 */
static int jpegPlaneNearest(const JpegSamplePlane* pl, int x, int y,
                            int maxH, int maxV)
{
    int sx = x * pl->h / maxH;
    int sy = y * pl->v / maxV;
    return jpegPlaneGet(pl, sx, sy);
}

/* 将一系数块反量化+逆 DCT 写入样本平面（块坐标 bx/by，块网格单元）。 */
static void jpegBlockToSamples(JpegCtx* ctx, int compIdx,
                               const int32_t coeff[64], int bx, int by)
{
    const JpegFrameComp* fc = &ctx->frame.comp[compIdx];
    const int* quant = ctx->quant[fc->tq];
    JpegSamplePlane* pl;
    float f[64], res[64];
    int shift;
    if (bx < 0 || by < 0) return;
    pl = &ctx->planes[compIdx];
    ctx->frBlkW = pl->pw / 8;
    ctx->frBlkH = pl->ph / 8;
    if (bx >= ctx->frBlkW || by >= ctx->frBlkH) return;
    memset(f, 0, sizeof(f));
    for (int nat = 0; nat < 64; ++nat) {
        int v = coeff[nat];
        if (v) {
            int q = quant ? quant[nat] : 0;
            if (q > 0)
                f[jpegTransposeIndex(nat)] = (float)v * (float)q;
        }
    }
    jpegInverseDctF(f, res);
    shift = ctx->frame.precision >= 12 ? 2048 : 128;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int v = jpegClampSample(jpegRound(res[r * 8 + c]) + shift,
                                    ctx->frame.precision);
            jpegPlaneSet(pl, bx * 8 + c, by * 8 + r, v);
        }
    }
}

/* 渐进式扫描结束后：将所有系数平面反量化/逆 DCT 写入样本平面。 */
static bool jpegProgressiveCommitCoeff(JpegCtx* ctx)
{
    for (int ci = 0; ci < ctx->frame.nf; ++ci) {
        JpegCoeffPlane* cp = &ctx->coeff[ci];
        if (!cp->data) return false;
        for (int by = 0; by < cp->blocksH; ++by) {
            for (int bx = 0; bx < cp->blocksW; ++bx) {
                jpegBlockToSamples(ctx, ci,
                    cp->data + ((size_t)by * (size_t)cp->blocksW + (size_t)bx) * 64u,
                    bx, by);
            }
        }
    }
    return true;
}

/* ====================================================================== */
/* 扫描状态与重启动处理                                                    */
/* ====================================================================== */

/* 重启动标记处理（Huffman 模式）：对齐到字节边界并读 RSTn。 */
static bool jpegHuffRestart(JpegCtx* ctx, int expected)
{
    JpegBitReader* b = &ctx->bits;
    jpegAlignToByte(b);
    while (b->pos < b->size && b->data[b->pos] == 0xFF) ++b->pos;
    if (b->pos >= b->size || b->data[b->pos] != (uint8_t)(0xD0 + expected))
        return false;
    ++b->pos;
    b->error = false;
    return true;
}

/* 重启动标记处理（算术模式）：跳过填充字节并读 RSTn。 */
static bool jpegArithRestart(JpegCtx* ctx, int expected)
{
    JpegArith* e = &ctx->arith;
    int marker;
    if (e->unreadMarker == 0) {
        if (e->pos >= e->size) return false;
        marker = e->data[e->pos++];
        while (marker == 0xFF) {
            if (e->pos >= e->size) return false;
            marker = e->data[e->pos++];
        }
    } else {
        marker = e->unreadMarker;
        e->unreadMarker = 0;
    }
    if (marker != (0xD0 + expected)) return false;
    /* 重初始化算术解码变量（D.2 节：ct=-16 强制填充两个初始字节）。 */
    e->c = 0;
    e->a = 0;
    e->ct = -16;
    return true;
}

/* 扫描/重启动处重置解码状态（DC 预测、EOBRUN、算术统计区）。 */
static void jpegScanReset(JpegCtx* ctx, const JpegScan* sc, bool scanStart)
{
    bool dcScan = (sc->Ss == 0 && sc->Ah == 0);
    bool acPart = (sc->Ss != 0);
    ctx->eobrun = 0;
    for (int i = 0; i < sc->ns; ++i) {
        int frameIdx = sc->comp[i].frameIdx;
        if (dcScan) {
            ctx->dcPred[frameIdx] = 0;
            if (ctx->frame.arithmetic)
                ctx->arith.dcContext[frameIdx] = 0;
        }
        if (ctx->frame.arithmetic) {
            if (!ctx->frame.progressive || dcScan) {
                int tbl = sc->comp[i].tdc;
                if (tbl >= 0 && tbl < 4) {
                    memset(ctx->arith.dcStats[tbl], 0,
                           sizeof(ctx->arith.dcStats[tbl]));
                }
            }
            if (!ctx->frame.progressive || acPart || scanStart) {
                int tbl = sc->comp[i].tac;
                if (tbl >= 0 && tbl < 4) {
                    memset(ctx->arith.acStats[tbl], 0,
                           sizeof(ctx->arith.acStats[tbl]));
                }
            }
        }
    }
}

/* 初始化算术解码器字节流位置（进入扫描熵数据前调用）。 */
static void jpegArithBegin(JpegCtx* ctx)
{
    JpegArith* e = &ctx->arith;
    e->c = 0;
    e->a = 0;
    e->ct = -16;
    e->unreadMarker = 0;
    /* 固定概率 0.5 bin（JPEG 规范第 10.3 节/表 D.2 索引 113）。 */
    e->fixedBin = 113;
}

/* 算术解码器读取下一个原始字节（0xFF00 填充与标记处理）。 */
static int jpegArithByte(JpegCtx* ctx)
{
    JpegArith* e = &ctx->arith;
    int data;
    if (e->pos >= e->size) return 0;
    data = e->data[e->pos++];
    if (data == 0xFF) {
        do {
            if (e->pos >= e->size) return 0;
            data = e->data[e->pos++];
        } while (data == 0xFF);
        if (data == 0) {
            data = 0xFF; /* 丢弃填充零字节 */
        } else {
            /* 算术编码中扫描数据段内出现标记是合法的：
               后续补零数据直至解码完成。 */
            e->unreadMarker = data;
            data = 0;
        }
    }
    return data;
}

/* ====================================================================== */
/* 算术熵解码核心（ITU-T T.81 Annex D.2）                                 */
/* ====================================================================== */

/* ITU-T T.81 表 D.2 算术编码概率估计表（114 项，索引 113 为固定 0.5）。 */
static const uint16_t jpegArithQe[114] = {
    0x5a1d, 0x2586, 0x1114, 0x080b, 0x03d8, 0x01da, 0x00e5, 0x006f, 0x0036, 0x001a, 0x000d, 0x0006,
    0x0003, 0x0001, 0x5a7f, 0x3f25, 0x2cf2, 0x207c, 0x17b9, 0x1182, 0x0cef, 0x09a1, 0x072f, 0x055c,
    0x0406, 0x0303, 0x0240, 0x01b1, 0x0144, 0x00f5, 0x00b7, 0x008a, 0x0068, 0x004e, 0x003b, 0x002c,
    0x5ae1, 0x484c, 0x3a0d, 0x2ef1, 0x261f, 0x1f33, 0x19a8, 0x1518, 0x1177, 0x0e74, 0x0bfb, 0x09f8,
    0x0861, 0x0706, 0x05cd, 0x04de, 0x040f, 0x0363, 0x02d4, 0x025c, 0x01f8, 0x01a4, 0x0160, 0x0125,
    0x00f6, 0x00cb, 0x00ab, 0x008f, 0x5b12, 0x4d04, 0x412c, 0x37d8, 0x2fe8, 0x293c, 0x2379, 0x1edf,
    0x1aa9, 0x174e, 0x1424, 0x119c, 0x0f6b, 0x0d51, 0x0bb6, 0x0a40, 0x5832, 0x4d1c, 0x438e, 0x3bdd,
    0x34ee, 0x2eae, 0x299a, 0x2516, 0x5570, 0x4ca9, 0x44d9, 0x3e22, 0x3824, 0x32b4, 0x2e17, 0x56a8,
    0x4f46, 0x47e5, 0x41cf, 0x3c3d, 0x375e, 0x5231, 0x4c0f, 0x4639, 0x415e, 0x5627, 0x50e7, 0x4b85,
    0x5597, 0x504f, 0x5a10, 0x5522, 0x59eb, 0x5a1d
};
static const uint8_t jpegArithNlps[114] = {
    1, 14, 16, 18, 20, 23, 25, 28, 30, 33, 35, 9,
    10, 12, 15, 36, 38, 39, 40, 42, 43, 45, 46, 48,
    49, 51, 52, 54, 56, 57, 59, 60, 62, 63, 32, 33,
    37, 64, 65, 67, 68, 69, 70, 72, 73, 74, 75, 77,
    78, 79, 48, 50, 50, 51, 52, 53, 54, 55, 56, 57,
    58, 59, 61, 61, 65, 80, 81, 82, 83, 84, 86, 87,
    87, 72, 72, 74, 74, 75, 77, 77, 80, 88, 89, 90,
    91, 92, 93, 86, 88, 95, 96, 97, 99, 99, 93, 95,
    101, 102, 103, 104, 99, 105, 106, 107, 103, 105, 108, 109,
    110, 111, 110, 112, 112, 113
};
static const uint8_t jpegArithNmps[114] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
    13, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 9,
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, 62, 63, 32, 65, 66, 67, 68, 69, 70, 71, 72,
    73, 74, 75, 76, 77, 78, 79, 48, 81, 82, 83, 84,
    85, 86, 87, 71, 89, 90, 91, 92, 93, 94, 86, 96,
    97, 98, 99, 100, 93, 102, 103, 104, 99, 106, 107, 103,
    109, 107, 111, 109, 111, 113
};
static const uint8_t jpegArithSw[114] = {
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 1, 0, 1, 0
};

/* 算术解码核心（图 D.2 归一化、决策与概率估计状态迁移）。 */
static int jpegArithDecode(JpegCtx* ctx, uint8_t* st)
{
    JpegArith* e = &ctx->arith;
    uint32_t qe, temp;
    uint8_t sv, nl, nm;
    int svIndex;
    int data;

    /* 归一化与数据输入（D.2.6 节） */
    while (e->a < 0x8000u) {
        if (--e->ct < 0) {
            if (e->unreadMarker) {
                data = 0; /* 遇到标记后补零数据 */
            } else {
                data = jpegArithByte(ctx);
            }
            e->c = (e->c << 8) | (uint32_t)data;
            if ((e->ct += 8) < 0) {
                if (++e->ct == 0)
                    e->a = 0x8000u;
            }
        }
        e->a <<= 1;
    }

    /* 从紧凑表示读取表 D.2：Qe 值与状态迁移 */
    sv = *st;
    svIndex = sv & 0x7F;
    qe = jpegArithQe[svIndex];
    nl = (uint8_t)(jpegArithNlps[svIndex] |
                   (jpegArithSw[svIndex] ? 0x80u : 0u));
    nm = jpegArithNmps[svIndex];

    /* 解码与估计（D.2.4 / D.2.5 节） */
    temp = e->a - qe;
    e->a = temp;
    if (e->ct >= 0)
        temp <<= e->ct;
    if (e->c >= temp) {
        e->c -= temp;
        /* 条件 LPS 交换 */
        if (e->a < qe) {
            e->a = qe;
            *st = (uint8_t)((sv & 0x80u) ^ nm); /* MPS 之后估计 */
        } else {
            e->a = qe;
            *st = (uint8_t)((sv & 0x80u) ^ nl); /* LPS 之后估计 */
            sv ^= 0x80u;                        /* 交换 LPS/MPS */
        }
    } else if (e->a < 0x8000u) {
        /* 条件 MPS 交换 */
        if (e->a < qe) {
            *st = (uint8_t)((sv & 0x80u) ^ nl); /* LPS 之后估计 */
            sv ^= 0x80u;                        /* 交换 LPS/MPS */
        } else {
            *st = (uint8_t)((sv & 0x80u) ^ nm); /* MPS 之后估计 */
        }
    }

    return sv >> 7;
}

/* ====================================================================== */
/* Huffman 块解码（顺序/渐进 DC/AC）                                       */
/* ====================================================================== */

/* 顺序 Huffman 块解码：DC 差分 + AC 行程。 */
static bool jpegDecodeBlockHuffSequential(JpegCtx* ctx, int frameIdx,
                                          int tdc, int tac, int32_t coeff[64])
{
    JpegBitReader* b = &ctx->bits;
    int s, diff, k;
    if (!ctx->haveHuff[0][tdc] || !ctx->haveHuff[1][tac]) return false;
    s = jpegDecodeSymbol(b, &ctx->huff[0][tdc]);
    if (s < 0) return false;
    diff = jpegReceiveExtend(b, s);
    ctx->dcPred[frameIdx] += diff;
    coeff[0] = ctx->dcPred[frameIdx];
    k = 1;
    while (k < 64) {
        int rs = jpegDecodeSymbol(b, &ctx->huff[1][tac]);
        int r, ss;
        if (rs < 0) return false;
        r = (rs >> 4) & 15;
        ss = rs & 15;
        if (ss == 0) {
            if (r == 15) {
                k += 16;
                if (k > 63) return false; /* ZRL 越界 */
            } else {
                break; /* EOB */
            }
        } else {
            k += r;
            if (k > 63) return false;
            coeff[jpegZigzagNatural[k]] = jpegReceiveExtend(b, ss);
            ++k;
        }
    }
    return !b->error;
}

/* 渐进 Huffman：DC 初扫（含顺序 DC 系数，Ah=0）。 */
static bool jpegDecodeBlockHuffDcFirst(JpegCtx* ctx, int frameIdx,
                                       int tdc, int al, int32_t coeff[64])
{
    JpegBitReader* b = &ctx->bits;
    int s;
    if (!ctx->haveHuff[0][tdc]) return false;
    s = jpegDecodeSymbol(b, &ctx->huff[0][tdc]);
    if (s < 0) return false;
    ctx->dcPred[frameIdx] += jpegReceiveExtend(b, s);
    coeff[0] = ctx->dcPred[frameIdx] << al;
    return !b->error;
}

/* 渐进 Huffman：DC 精化扫描（每次补充 1 位）。 */
static bool jpegDecodeBlockHuffDcRefine(JpegCtx* ctx, int al,
                                        int32_t coeff[64])
{
    int bit;
    if (!jpegGetBit(&ctx->bits, &bit)) return false;
    if (bit && al < 31) coeff[0] |= (1 << al);
    return true;
}

/* 渐进 Huffman：AC 初扫（光谱选择，Ah=0，EOBRUN 跨 MCU 持久）。 */
static bool jpegDecodeBlockHuffAcFirst(JpegCtx* ctx, const JpegScan* sc,
                                       int tac, int32_t coeff[64])
{
    JpegBitReader* b = &ctx->bits;
    int k = sc->Ss;
    if (!ctx->haveHuff[1][tac]) return false;
    if (ctx->eobrun > 0) {
        --ctx->eobrun; /* 零带处理 */
    } else {
        while (k <= sc->Se) {
            int rs = jpegDecodeSymbol(b, &ctx->huff[1][tac]);
            int r, ss;
            if (rs < 0) return false;
            r = (rs >> 4) & 15;
            ss = rs & 15;
            if (ss != 0) {
                k += r;
                if (k > sc->Se) return false;
                coeff[jpegZigzagNatural[k]] =
                    jpegReceiveExtend(b, ss) << sc->Al;
                ++k;
            } else if (r == 15) {
                k += 15; /* ZRL：跳过 15 个零，配合循环前进共 16 */
                ++k;
            } else {
                /* EOBr：段长为 2^r + 附加位（裸位数），当前块占 1 */
                ctx->eobrun = 1 << r;
                if (r) ctx->eobrun += jpegReadBits(b, r);
                --ctx->eobrun;
                break;
            }
        }
    }
    return !b->error;
}

/* 渐进 Huffman：AC 精化（单比特状态机，已非零系数补 ±2^Al）。 */
static bool jpegDecodeBlockHuffAcRefine(JpegCtx* ctx, const JpegScan* sc,
                                        int tac, int32_t coeff[64])
{
    JpegBitReader* b = &ctx->bits;
    int p1 = 1 << sc->Al;
    int m1 = -p1;
    int k = sc->Ss;
    bool haveNew = false;
    int newPos = 0;
    if (!ctx->haveHuff[1][tac]) return false;
    if (ctx->eobrun == 0) {
        for (; k <= sc->Se; ++k) {
            int rs = jpegDecodeSymbol(b, &ctx->huff[1][tac]);
            int r = 0, s = 0, bit;
            if (rs < 0) return false;
            r = (rs >> 4) & 15;
            s = rs & 15;
            if (s != 0) {
                /* 新非零系数的符号位 */
                if (!jpegGetBit(b, &bit)) return false;
                s = bit ? p1 : m1;
            } else if (r != 15) {
                /* EOBr 段：附加位为裸位数 */
                ctx->eobrun = 1 << r;
                if (r) ctx->eobrun += jpegReadBits(b, r);
                break;
            }
            /* 遍历已非零系数以及 r 个仍未出现的系数，追加校正位 */
            do {
                int32_t v = coeff[jpegZigzagNatural[k]];
                if (v != 0) {
                    if (!jpegGetBit(b, &bit)) return false;
                    if (bit && ((v & p1) == 0)) {
                        coeff[jpegZigzagNatural[k]] =
                            v >= 0 ? v + p1 : v - p1;
                    }
                } else {
                    if (--r < 0) break;
                }
                ++k;
            } while (k <= sc->Se);
            if (s != 0) {
                if (k <= sc->Se) {
                    newPos = k;
                    haveNew = true;
                    coeff[jpegZigzagNatural[k]] = s;
                    /* 外层 for 循环的 k++ 跳过该位置（同 libjpeg）*/
                }
            }
        }
    }
    if (ctx->eobrun > 0) {
        /* EOB 段剩余块：对每个已非零系数追加校正位 */
        while (k <= sc->Se) {
            int32_t v = coeff[jpegZigzagNatural[k]];
            int bit;
            if (v != 0) {
                if (!jpegGetBit(b, &bit)) return false;
                if (bit && ((v & p1) == 0)) {
                    coeff[jpegZigzagNatural[k]] = v >= 0 ? v + p1 : v - p1;
                }
            }
            ++k;
        }
        --ctx->eobrun;
    }
    (void)haveNew;
    return !b->error;
}

/* ====================================================================== */
/* 算术编码块解码（顺序/渐进，ITU-T T.81 Annex F）                         */
/* ====================================================================== */

/* 算术 DC 初扫（顺序全带或渐进 DC first）。 */
static bool jpegArithDecodeBlockDcFirst(JpegCtx* ctx, int frameIdx,
                                        int tbl, int al, int32_t coeff[64])
{
    JpegArith* e = &ctx->arith;
    uint8_t* st = e->dcStats[tbl] + e->dcContext[frameIdx];
    int sign = 0, m = 0, v = 0;
    if (jpegArithDecode(ctx, st) == 0) {
        e->dcContext[frameIdx] = 0;
    } else {
        int L = (int)ctx->arithDcL[tbl];
        int U = (int)ctx->arithDcU[tbl];
        sign = jpegArithDecode(ctx, st + 1);
        st += 2 + sign;
        m = jpegArithDecode(ctx, st);
        if (m != 0) {
            st = e->dcStats[tbl] + 20;
            while (jpegArithDecode(ctx, st)) {
                if ((m <<= 1) == 0x8000) return false; /* 幅度溢出 */
                st += 1;
            }
        }
        if (m < ((1 << L) >> 1))
            e->dcContext[frameIdx] = 0;
        else if (m > ((1 << U) >> 1))
            e->dcContext[frameIdx] = 12 + sign * 4;
        else
            e->dcContext[frameIdx] = 4 + sign * 4;
        v = m;
        st += 14;
        while (m >>= 1) {
            if (jpegArithDecode(ctx, st)) v |= m;
        }
        v += 1;
        if (sign) v = -v;
        ctx->dcPred[frameIdx] = (ctx->dcPred[frameIdx] + v) & 0xffff;
    }
    /* DC 预测值按规范 16 位回绕（F.2.4.1），落位时符号扩展为 32 位有符号
       （与 libjpeg 将 16 位 JCOEF 存入 32 位块等价），避免 0xFEAB
       之类的回绕值被当作正数参与反量化。 */
    coeff[0] = (int32_t)((int16_t)((int)ctx->dcPred[frameIdx] << al));
    return true;
}

/* 算术 DC 精化扫描。 */
static bool jpegArithDecodeBlockDcRefine(JpegCtx* ctx, int al,
                                         int32_t coeff[64])
{
    if (jpegArithDecode(ctx, &ctx->arith.fixedBin) && al < 31)
        coeff[0] |= (1 << al);
    return true;
}

/* 算术 AC 初扫。 */
static bool jpegArithDecodeBlockAcFirst(JpegCtx* ctx, const JpegScan* sc,
                                        int tbl, int32_t coeff[64])
{
    JpegArith* e = &ctx->arith;
    int k = sc->Ss;
    while (k <= sc->Se) {
        uint8_t* st = e->acStats[tbl] + 3 * (k - 1);
        int sign, m, v;
        if (jpegArithDecode(ctx, st)) break; /* EOB 标志 */
        while (jpegArithDecode(ctx, st + 1) == 0) {
            st += 3;
            k++;
            if (k > sc->Se) return false; /* 光谱越界 */
        }
        sign = jpegArithDecode(ctx, &e->fixedBin);
        st += 2;
        m = jpegArithDecode(ctx, st);
        if (m != 0) {
            if (jpegArithDecode(ctx, st)) {
                m <<= 1;
                st = e->acStats[tbl] + (k <= ctx->arithAcK[tbl] ? 189 : 217);
                while (jpegArithDecode(ctx, st)) {
                    if ((m <<= 1) == 0x8000) return false;
                    st += 1;
                }
            }
        }
        v = m;
        st += 14;
        while (m >>= 1) {
            if (jpegArithDecode(ctx, st)) v |= m;
        }
        v += 1;
        if (sign) v = -v;
        /* 与 libjpeg 一致：以无符号移位后截断为 16 位有符号（JCOEF）再扩展 32 位 */
        coeff[jpegZigzagNatural[k]] =
            (int32_t)((int16_t)((uint32_t)v << sc->Al));
        k++;
    }
    return true;
}

/* 算术 AC 精化扫描。 */
static bool jpegArithDecodeBlockAcRefine(JpegCtx* ctx, const JpegScan* sc,
                                         int tbl, int32_t coeff[64])
{
    JpegArith* e = &ctx->arith;
    int p1 = 1 << sc->Al;
    int m1 = -p1;
    int k, kex;
    for (kex = sc->Se; kex > 0; --kex) {
        if (coeff[jpegZigzagNatural[kex]]) break;
    }
    for (k = sc->Ss; k <= sc->Se; ++k) {
        uint8_t* st = e->acStats[tbl] + 3 * (k - 1);
        if (k > kex) {
            if (jpegArithDecode(ctx, st)) break; /* EOB 标志 */
        }
        for (;;) {
            int32_t v = coeff[jpegZigzagNatural[k]];
            if (v != 0) {
                if (jpegArithDecode(ctx, st + 2)) {
                    coeff[jpegZigzagNatural[k]] =
                        v < 0 ? v + m1 : v + p1;
                }
                break;
            }
            if (jpegArithDecode(ctx, st + 1)) {
                if (jpegArithDecode(ctx, &e->fixedBin))
                    coeff[jpegZigzagNatural[k]] = m1;
                else
                    coeff[jpegZigzagNatural[k]] = p1;
                break;
            }
            st += 3;
            k++;
            if (k > sc->Se) return false; /* 光谱越界 */
        }
    }
    return true;
}

/* 算术顺序全带块解码（DC + AC，Al=0）。 */
static bool jpegArithDecodeBlockSequential(JpegCtx* ctx, int frameIdx,
                                           int tdc, int tac,
                                           int32_t coeff[64])
{
    int k;
    /* DC 系数 */
    if (!jpegArithDecodeBlockDcFirst(ctx, frameIdx, tdc, 0, coeff))
        return false;
    /* AC 系数 */
    for (k = 1; k <= 63; ++k) {
        uint8_t* st = ctx->arith.acStats[tac] + 3 * (k - 1);
        int sign, m, v;
        if (jpegArithDecode(ctx, st)) break; /* EOB 标志 */
        while (jpegArithDecode(ctx, st + 1) == 0) {
            st += 3;
            k++;
            if (k > 63) return false;
        }
        sign = jpegArithDecode(ctx, &ctx->arith.fixedBin);
        st += 2;
        m = jpegArithDecode(ctx, st);
        if (m != 0) {
            if (jpegArithDecode(ctx, st)) {
                m <<= 1;
                st = ctx->arith.acStats[tac] +
                     (k <= ctx->arithAcK[tac] ? 189 : 217);
                while (jpegArithDecode(ctx, st)) {
                    if ((m <<= 1) == 0x8000) return false;
                    st += 1;
                }
            }
        }
        v = m;
        st += 14;
        while (m >>= 1) {
            if (jpegArithDecode(ctx, st)) v |= m;
        }
        v += 1;
        if (sign) v = -v;
        coeff[jpegZigzagNatural[k]] = v;
    }
    return true;
}
/* ====================================================================== */
/* 扫描（SOS）解码驱动：MCU 网格、块调度与重启动                             */
/* ====================================================================== */

/** @brief 计算扫描的 MCU 网格与每 MCU 数据块数。
 *  @param interleaved  输出：true=交错扫描（ns==nf），false=单分量非交错扫描。
 *  @note  交错扫描按帧 MCU 网格（ceil(w/(8*maxH)) x ceil(h/(8*maxV))）推进，
 *         每个 MCU 依扫描顺序包含各分量 h*v 个数据块；
 *         非交错扫描（ns==1）按分量块网格推进，且每个 MCU 恰含 1 个数据块
 *         （ITU-T T.81 B.2.3，与 libjpeg per_scan_setup 一致）。
 */
static bool jpegScanGrid(const JpegCtx* ctx, const JpegScan* sc,
                         int* mcuW, int* mcuH, int* blocksInMcu,
                         bool* interleaved)
{
    if (sc->ns == ctx->frame.nf) {
        int total = 0;
        *interleaved = true;
        for (int i = 0; i < sc->ns; ++i) {
            const JpegFrameComp* fc =
                &ctx->frame.comp[sc->comp[i].frameIdx];
            total += fc->h * fc->v;
            if (total > 10) return false; /* 每 MCU 数据块上限（规范 A.1.1） */
        }
        *blocksInMcu = total;
        *mcuW = (ctx->frame.width + 8 * ctx->frame.maxH - 1) /
                (8 * ctx->frame.maxH);
        *mcuH = (ctx->frame.height + 8 * ctx->frame.maxV - 1) /
                (8 * ctx->frame.maxV);
    } else if (sc->ns == 1) {
        /* 单分量非交错扫描：数据只覆盖实际分量块网格
           （ceil(宽*h/(8*maxH)) x ceil(高*v/(8*maxV))，无 MCU 边缘填充；
           与 libjpeg 的 width_in_blocks/height_in_blocks 一致），
           而非 MCU 对齐的分配网格。 */
        const JpegFrameComp* fc = &ctx->frame.comp[sc->comp[0].frameIdx];
        *interleaved = false;
        *blocksInMcu = 1;
        *mcuW = (ctx->frame.width * fc->h + 8 * ctx->frame.maxH - 1) /
                (8 * ctx->frame.maxH);
        *mcuH = (ctx->frame.height * fc->v + 8 * ctx->frame.maxV - 1) /
                (8 * ctx->frame.maxV);
        if (*mcuW < 1) *mcuW = 1;
        if (*mcuH < 1) *mcuH = 1;
    } else {
        return false; /* 多分量但非全交错：非法扫描结构 */
    }
    return *mcuW > 0 && *mcuH > 0;
}

/* 单块熵解码派发（按帧/扫描参数选择 Huffman 或算术解码过程）。 */
static bool jpegDecodeOneBlock(JpegCtx* ctx, const JpegScan* sc,
                               int frameIdx, int tdc, int tac,
                               int32_t coeff[64])
{
    bool prog = ctx->frame.progressive ? true : false;
    bool arith = ctx->frame.arithmetic ? true : false;
    if (!arith) {
        if (!prog)
            return jpegDecodeBlockHuffSequential(ctx, frameIdx, tdc, tac,
                                                 coeff);
        if (sc->Ss == 0) {
            if (sc->Ah == 0)
                return jpegDecodeBlockHuffDcFirst(ctx, frameIdx, tdc,
                                                  sc->Al, coeff);
            return jpegDecodeBlockHuffDcRefine(ctx, sc->Al, coeff);
        }
        if (sc->Ah == 0)
            return jpegDecodeBlockHuffAcFirst(ctx, sc, tac, coeff);
        return jpegDecodeBlockHuffAcRefine(ctx, sc, tac, coeff);
    }
    if (!prog)
        return jpegArithDecodeBlockSequential(ctx, frameIdx, tdc, tac,
                                              coeff);
    if (sc->Ss == 0) {
        if (sc->Ah == 0)
            return jpegArithDecodeBlockDcFirst(ctx, frameIdx, tdc,
                                               sc->Al, coeff);
        return jpegArithDecodeBlockDcRefine(ctx, sc->Al, coeff);
    }
    if (sc->Ah == 0)
        return jpegArithDecodeBlockAcFirst(ctx, sc, tac, coeff);
    return jpegArithDecodeBlockAcRefine(ctx, sc, tac, coeff);
}

/* 校验扫描所需表齐备（Huffman/量化；算术扫描无需 Huffman 表）。 */
static bool jpegScanCheckTables(JpegCtx* ctx, const JpegScan* sc)
{
    for (int i = 0; i < sc->ns; ++i) {
        const JpegScanComp* c = &sc->comp[i];
        const JpegFrameComp* fc = &ctx->frame.comp[c->frameIdx];
        if (fc->tq < 0 || fc->tq > 3 || !ctx->haveQuant[fc->tq])
            return false;
        if (!ctx->frame.arithmetic) {
            if (c->tdc < 0 || c->tdc > 3 || c->tac < 0 || c->tac > 3)
                return false;
            /* DC 初扫/顺序扫描需要 DC 表；AC 初扫/顺序需要 AC 表；
               DC/AC 精化扫描只用位流，无需表。 */
            if (sc->Ss == 0 && sc->Ah == 0) {
                if (!ctx->haveHuff[0][c->tdc]) return false;
            }
            if (sc->Ss != 0 && sc->Ah == 0) {
                if (!ctx->haveHuff[1][c->tac]) return false;
            }
            /* 顺序全带扫描需要 DC+AC 表 */
            if (!ctx->frame.progressive) {
                if (!ctx->haveHuff[0][c->tdc] ||
                    !ctx->haveHuff[1][c->tac]) return false;
            }
        }
    }
    return true;
}

/* 渐进式：从系数平面读取某块的 64 个已解系数（精化扫描需基于前序扫描状态）。 */
static bool jpegLoadBlock(const JpegCtx* ctx, int frameIdx, int blockX,
                          int blockY, int32_t coeff[64])
{
    const JpegCoeffPlane* cp = &ctx->coeff[frameIdx];
    if (blockX < 0 || blockY < 0)
        return false;
    if (blockX >= cp->blocksW || blockY >= cp->blocksH)
        return false;
    memcpy(coeff, cp->data + ((size_t)blockY * (size_t)cp->blocksW +
                              (size_t)blockX) * 64u,
           sizeof(int32_t) * 64u);
    return true;
}

/* 将已解出的 64 个系数落位（渐进式写入系数平面；顺序式即时 IDCT 到样本平面）。 */
static bool jpegPlaceBlock(JpegCtx* ctx, int frameIdx, int blockX, int blockY,
                           const int32_t coeff[64])
{
    if (ctx->frame.progressive) {
        JpegCoeffPlane* cp = &ctx->coeff[frameIdx];
        if (blockX < 0 || blockY < 0)
            return false;
        if (blockX >= cp->blocksW || blockY >= cp->blocksH)
            return false;
        memcpy(cp->data + ((size_t)blockY * (size_t)cp->blocksW +
                           (size_t)blockX) * 64u, coeff,
               sizeof(int32_t) * 64u);
        return true;
    }
    jpegBlockToSamples(ctx, frameIdx, coeff, blockX, blockY);
    return true;
}

/* 运行一次扫描的熵解码（扫描入口 pos 指向熵数据起点；完成后 *pos
 * 指向最后一个消费字节之后）。 */
static bool jpegRunScan(JpegCtx* ctx, const JpegScan* sc,
                        const uint8_t* data, size_t size, size_t* pos)
{
    int mcuW, mcuH, blocksInMcu, mcuIndex, rstRemain, rstIndex;
    bool interleaved;
    bool arith = ctx->frame.arithmetic ? true : false;

    if (!jpegScanGrid(ctx, sc, &mcuW, &mcuH, &blocksInMcu, &interleaved))
        return false;
    /* 渐进式扫描结构约束（T.81 G.2）：DC 扫描 Se 必须为 0，
       AC 扫描必须单分量。 */
    if (ctx->frame.progressive) {
        if (sc->Ss == 0) {
            if (sc->Se != 0) return false;
        } else {
            if (sc->ns != 1) return false;
        }
    }
    if (!jpegScanCheckTables(ctx, sc)) return false;

    if (arith) {
        ctx->arith.data = data;
        ctx->arith.size = size;
        ctx->arith.pos = *pos;
        jpegArithBegin(ctx);
    } else {
        memset(&ctx->bits, 0, sizeof(ctx->bits));
        ctx->bits.data = data;
        ctx->bits.size = size;
        ctx->bits.pos = *pos;
    }

    jpegScanReset(ctx, sc, true); /* 扫描起点：复位 DC 预测/统计 */
    rstRemain = ctx->restart;
    rstIndex = 0;
    mcuIndex = 0;

    for (int my = 0; my < mcuH; ++my) {
        for (int mx = 0; mx < mcuW; ++mx) {
            /* 重启动标记在 MCU 边界处理（T.81 B.2.3 F.1.2.3） */
            if (ctx->restart > 0 && rstRemain == 0) {
                bool ok2 = arith ? jpegArithRestart(ctx, rstIndex)
                                 : jpegHuffRestart(ctx, rstIndex);
                if (!ok2) return false;
                jpegScanReset(ctx, sc, false);
                rstRemain = ctx->restart;
                rstIndex = (rstIndex + 1) & 7;
            }
            if (ctx->restart > 0) --rstRemain;

            if (interleaved) {
                bool bad = false;
                for (int ci = 0; ci < sc->ns && !bad; ++ci) {
                    const JpegScanComp* c = &sc->comp[ci];
                    const JpegFrameComp* fc =
                        &ctx->frame.comp[c->frameIdx];
                    for (int vy = 0; vy < fc->v && !bad; ++vy) {
                        for (int hx = 0; hx < fc->h && !bad; ++hx) {
                            int32_t coeff[64];
                            if (ctx->frame.progressive) {
                                if (!jpegLoadBlock(ctx, c->frameIdx,
                                                   mx * fc->h + hx,
                                                   my * fc->v + vy, coeff))
                                    return false;
                            } else {
                                memset(coeff, 0, sizeof(coeff));
                            }
                            if (!jpegDecodeOneBlock(ctx, sc, c->frameIdx,
                                                    c->tdc, c->tac, coeff))
                                return false;
                            if (!jpegPlaceBlock(ctx, c->frameIdx,
                                                mx * fc->h + hx,
                                                my * fc->v + vy, coeff))
                                return false;
                            if ((arith && ctx->arith.ct == -1) ||
                                (!arith && ctx->bits.error))
                                bad = true;
                        }
                    }
                }
                if (bad) return false;
            } else {
                const JpegScanComp* c = &sc->comp[0];
                int32_t coeff[64];
                if (ctx->frame.progressive) {
                    if (!jpegLoadBlock(ctx, c->frameIdx, mx, my, coeff))
                        return false;
                } else {
                    memset(coeff, 0, sizeof(coeff));
                }
                if (!jpegDecodeOneBlock(ctx, sc, c->frameIdx,
                                        c->tdc, c->tac, coeff))
                    return false;
                if (!jpegPlaceBlock(ctx, c->frameIdx, mx, my, coeff))
                    return false;
                if ((arith && ctx->arith.ct == -1) ||
                    (!arith && ctx->bits.error))
                    return false;
            }
            (void)blocksInMcu;
            ++mcuIndex;
        }
    }
    *pos = arith ? ctx->arith.pos : ctx->bits.pos;
    return true;
}

/* 扫描结束后的下一个标记定位：跳过熵编码残余填充字节。
 * 算术解码器遇到标记时已缓存于 unreadMarker（pos 已越过标记）。 */
static int jpegScanNextMarker(JpegCtx* ctx, const uint8_t* data,
                              size_t size, size_t* pos, bool arithmetic)
{
    if (arithmetic && ctx->arith.unreadMarker != 0) {
        int m = ctx->arith.unreadMarker;
        ctx->arith.unreadMarker = 0;
        return m;
    }
    while (*pos < size) {
        uint8_t c = data[*pos];
        if (c == 0xFF) {
            size_t p = *pos + 1;
            while (p < size && data[p] == 0xFF) ++p;
            if (p >= size) return -1;
            if (data[p] != 0x00) { /* 真实标记 */
                *pos = p + 1;
                return data[p];
            }
            *pos = p + 1; /* 0xFF00 填充字节属于熵数据 */
            continue;
        }
        ++(*pos);
    }
    return -1;
}

/* ====================================================================== */
/* 样本/系数平面分配与颜色转换输出                                          */
/* ====================================================================== */

/* 解析 Adobe APP14 段（版本/标志/转换字段）。 */
static bool jpegParseAdobe(const uint8_t* data, size_t size, size_t* pos,
                           JpegCtx* ctx)
{
    int segLen;
    size_t end;
    if (*pos + 2 > size) return false;
    segLen = jpegReadBe16(data, size, pos);
    if (segLen < 2) return false;
    end = *pos + (size_t)(segLen - 2);
    if (end > size) return false;
    if (segLen >= 12) {
        /* 载荷：'A''d''o''b''e' + 版本2 + flags0 2 + flags1 2 + 转换1 */
        if (memcmp(data + *pos, "Adobe", 5) == 0) {
            ctx->frame.adobe = 1;
            ctx->frame.adobeTransform = data[*pos + 11];
        }
    }
    *pos = end;
    return true;
}

/* 分配帧所有分量的样本平面（渐进式额外分配系数平面）。 */
static bool jpegAllocatePlanes(JpegCtx* ctx)
{
    int precision = ctx->frame.precision;
    for (int ci = 0; ci < ctx->frame.nf; ++ci) {
        int blocksW, blocksH, mid;
        JpegSamplePlane* pl = &ctx->planes[ci];
        size_t pw, ph, n;
        jpegFrameBlockGrid(ctx, ci, &blocksW, &blocksH);
        pw = (size_t)blocksW * 8u;
        ph = (size_t)blocksH * 8u;
        if (!pw || !ph || pw > (size_t)INT_MAX || ph > (size_t)INT_MAX)
            return false;
        n = pw * ph;
        if (n > SIZE_MAX / (size_t)(precision >= 12 ? 2u : 1u)) return false;
        n *= (size_t)(precision >= 12 ? 2u : 1u);
        pl->data = (uint8_t*)XMalloc_System(n);
        if (!pl->data) return false;
        pl->pw = (int)pw;
        pl->ph = (int)ph;
        pl->h = ctx->frame.comp[ci].h;
        pl->v = ctx->frame.comp[ci].v;
        pl->byteDepth = (precision >= 12) ? 2 : 1;
        mid = (precision >= 12) ? 2048 : 128;
        for (int y = 0; y < (int)ph; ++y)
            for (int x = 0; x < (int)pw; ++x)
                jpegPlaneSet(pl, x, y, mid);
        if (ctx->frame.progressive) {
            JpegCoeffPlane* cp = &ctx->coeff[ci];
            size_t cn = (size_t)blocksW * (size_t)blocksH;
            if (cn > SIZE_MAX / (64u * sizeof(int32_t))) return false;
            cn *= 64u * sizeof(int32_t);
            cp->data = (int32_t*)XMalloc_System(cn);
            if (!cp->data) return false;
            cp->blocksW = blocksW;
            cp->blocksH = blocksH;
            memset(cp->data, 0, cn); /* 渐进式系数初始为 0 */
        }
    }
    return true;
}

/* 释放样本/系数平面。 */
static void jpegFreePlanes(JpegCtx* ctx)
{
    for (int i = 0; i < 4; ++i) {
        if (ctx->planes[i].data) XFree_System(ctx->planes[i].data);
        if (ctx->coeff[i].data) XFree_System(ctx->coeff[i].data);
        ctx->planes[i].data = NULL;
        ctx->coeff[i].data = NULL;
    }
}

/* 从帧像素坐标取（按最近邻上采样的）分量样本，12 位时右移 4 位转 8 位。 */
static int jpegSampleAt(JpegCtx* ctx, int compIdx, int x, int y)
{
    int v = jpegPlaneNearest(&ctx->planes[compIdx], x, y,
                             ctx->frame.maxH, ctx->frame.maxV);
    if (ctx->frame.precision >= 12) return v >> 4;
    return v;
}

/* 将解码完成的分量平面转换成 ARGB32 输出图像。
 * 支持：1 分量灰度、3 分量 YCbCr、4 分量 CMYK/YCCK（Adobe APP14）。 */
static bool jpegOutputImage(JpegCtx* ctx, XImage* out)
{
    int w = ctx->frame.width;
    int h = ctx->frame.height;
    int nf = ctx->frame.nf;
    int role[4] = { -1, -1, -1, -1 };
    XImage temp;

    if (!out || w <= 0 || h <= 0) return false;
    /* 渐进式：所有扫描结束后才将系数平面反量化/逆 DCT */
    if (ctx->frame.progressive) {
        if (!jpegProgressiveCommitCoeff(ctx)) return false;
    }
    /* 依据分量 id 建立角色映射（Y=1, Cb=2, Cr=3, K/C/M/Y 按文件约定），
       缺失时退化为按顺序。 */
    for (int ci = 0; ci < nf && ci < 4; ++ci) {
        int id = ctx->frame.comp[ci].id;
        if (id >= 1 && id <= 4) role[id - 1] = ci;
    }
    XImage_init_ex(&temp, w, h, XImageFormat_ARGB32);
    if (XImage_isNull(&temp)) return false;

    if (nf == 1) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int g = jpegSampleAt(ctx, 0, x, y);
                XImage_setPixel(&temp, x, y, 0xff000000u |
                                ((uint32_t)g << 16) | ((uint32_t)g << 8) |
                                (uint32_t)g);
            }
        }
    } else if (nf == 3) {
        int iY = role[0] >= 0 ? role[0] : 0;
        int iCb = role[1] >= 0 ? role[1] : 1;
        int iCr = role[2] >= 0 ? role[2] : 2;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int Y = jpegSampleAt(ctx, iY, x, y);
                int Cb = jpegSampleAt(ctx, iCb, x, y) - 128;
                int Cr = jpegSampleAt(ctx, iCr, x, y) - 128;
                int r = jpegClamp255(jpegRound((float)Y + 1.402f * (float)Cr));
                int g = jpegClamp255(jpegRound((float)Y - 0.344136f *
                                               (float)Cb - 0.714136f *
                                               (float)Cr));
                int b = jpegClamp255(jpegRound((float)Y + 1.772f *
                                               (float)Cb));
                XImage_setPixel(&temp, x, y, 0xff000000u |
                                ((uint32_t)r << 16) | ((uint32_t)g << 8) |
                                (uint32_t)b);
            }
        }
    } else if (nf == 4) {
#if XIMAGECODEC_JPEG_CMYK_ON
        bool ycck = (ctx->frame.adobe != 0 && ctx->frame.adobeTransform == 2);
        int i0 = role[0] >= 0 ? role[0] : 0;
        int i1 = role[1] >= 0 ? role[1] : 1;
        int i2 = role[2] >= 0 ? role[2] : 2;
        int i3 = role[3] >= 0 ? role[3] : 3;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int c0 = jpegSampleAt(ctx, i0, x, y);
                int c1 = jpegSampleAt(ctx, i1, x, y);
                int c2 = jpegSampleAt(ctx, i2, x, y);
                int K = jpegSampleAt(ctx, i3, x, y);
                int r, g, b;
                if (ycck) {
                    /* YCCK（Adobe transform=2）：先 YCbCr→RGB，再按 Adobe 约定输出。
                     * YCCK 文件的 K 平面与 CMYK 一样以“反相”形式存储
                     * (255=无黑墨，0=纯黑)，且 YCbCr 表示扣除黑墨后的反相 RGB；
                     * 因此最终 RGB = 反相后 YCbCr→RGB 的结果 * K / 255，
                     * 等价于 libjpeg-turbo 的 YCCK→CMYK→RGB 转换链。 */
                    int Y = c0, Cb = c1 - 128, Cr = c2 - 128;
                    int rp = jpegClamp255(jpegRound((float)Y + 1.402f *
                                                    (float)Cr));
                    int gp = jpegClamp255(jpegRound((float)Y - 0.344136f *
                                                    (float)Cb - 0.714136f *
                                                    (float)Cr));
                    int bp = jpegClamp255(jpegRound((float)Y + 1.772f *
                                                    (float)Cb));
                    r = jpegClamp255(((255 - rp) * K + 127) / 255);
                    g = jpegClamp255(((255 - gp) * K + 127) / 255);
                    b = jpegClamp255(((255 - bp) * K + 127) / 255);
                } else {
                    /* CMYK：Adobe 约定 4 分量 CMYK JPEG 全部按“反相”存储
                     * (255=无油墨，0=100% 油墨)，显示 RGB 取
                     * R=C*K/255、G=M*K/255、B=Y*K/255（四舍五入取整，
                     * 与 libjpeg-turbo djpeg 的 cmyk_to_rgb 一致；
                     * 等价于先反相回油墨量再按 (255-C)*(255-K)/255）。 */
                    r = jpegClamp255((c0 * K + 127) / 255);
                    g = jpegClamp255((c1 * K + 127) / 255);
                    b = jpegClamp255((c2 * K + 127) / 255);
                }
                XImage_setPixel(&temp, x, y, 0xff000000u |
                                ((uint32_t)r << 16) | ((uint32_t)g << 8) |
                                (uint32_t)b);
            }
        }
#else
        return false; /* CMYK 扩展被裁剪 */
#endif
    } else {
        return false; /* 不支持的组件数量 */
    }
    XImage_deinit_base(out);
    out->m_data = temp.m_data;
    temp.m_data = NULL;
    return true;
}

/* ====================================================================== */
/* 顶层 JPEG 解码入口                                                       */
/* ====================================================================== */

/** @brief 解码完整 JPEG 文件到 XImage（统一入口，由 XImageCodec 分发）。
 *  @note  支持 SOF0/1/2（SOF9/10 算术）、8/12 位、1~4 分量、
 *         灰度/YCbCr/CMYK/YCCK、顺序/渐进式、重启间隔；
 *         渐进/12 位/算术/CMYK 均受 XImageCodec_config.h 对应开关裁剪。
 */
bool XImageCodecInternal_decodeJpeg(const uint8_t* data, size_t size,
                                    XImage* out)
{
    JpegCtx ctx;
    size_t pos = 0;
    bool ok = false;
    int pendingMarker = 0;

    if (!data || size < 4 || !out) return false;
    memset(&ctx, 0, sizeof(ctx));
    /* 算术条件表默认（JPEG 规范 F.1.4.2：L=0, U=1, K=5） */
    for (int i = 0; i < 4; ++i) {
        ctx.arithDcL[i] = 0;
        ctx.arithDcU[i] = 1;
        ctx.arithAcK[i] = 5;
    }
    if (jpegNextMarker(data, size, &pos) != 0xD8) return false; /* SOI */

    for (;;) {
        int marker;
        if (pendingMarker != 0) {
            marker = pendingMarker;
            pendingMarker = 0;
        } else {
            marker = jpegNextMarker(data, size, &pos);
            if (marker < 0) goto done;
        }
        if (marker == 0xDA) { /* SOS：解析并执行一段扫描 */
            JpegScan scan;
            if (!ctx.frame.parsed) return false;
            if (!jpegParseSos(data, size, &pos, &ctx.frame, &scan))
                goto done;
            if (!jpegRunScan(&ctx, &scan, data, size, &pos))
                goto done;
            pendingMarker = jpegScanNextMarker(&ctx, data, size, &pos,
                                               ctx.frame.arithmetic != 0);
            if (pendingMarker < 0) goto done;
            continue;
        }
        if (marker == 0xD9) { /* EOI */
            ok = ctx.frame.parsed;
            break;
        }
        if (marker == 0xDB) {
            if (!jpegParseDqt(data, size, &pos, ctx.quant, ctx.haveQuant))
                goto done;
        } else if (marker == 0xC4) {
            if (!jpegParseDht(data, size, &pos, ctx.huff, ctx.haveHuff))
                goto done;
        } else if (marker == 0xCC) {
            if (!jpegParseDac(data, size, &pos, ctx.arithDcL, ctx.arithDcU,
                              ctx.arithAcK))
                goto done;
        } else if (marker == 0xDD) {
            if (pos + 2 > size) goto done;
            pos += 2;
            ctx.restart = jpegReadBe16(data, size, &pos);
            if (ctx.restart < 0) goto done;
        } else if (marker == 0xEE) {
            if (!jpegParseAdobe(data, size, &pos, &ctx))
                goto done;
        } else if (marker >= 0xC0 && marker <= 0xCF &&
                   marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (ctx.frame.parsed) return false; /* 分层 JPEG 多帧不支持 */
            if (!jpegParseSof(data, size, &pos, marker, &ctx.frame))
                goto done;
            if (!jpegAllocatePlanes(&ctx)) goto done;
        } else if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            /* TEM 与 RSTn 为无长度字段的单字节标记 */
        } else {
            if (!jpegSkipMarker(data, size, &pos)) goto done;
        }
    }
    if (!ok) goto done;
    ok = jpegOutputImage(&ctx, out);

done:
    jpegFreePlanes(&ctx);
    return ok;
}
/* 编码器：位写出器（MSB 在前，0xFF 后自动补 0x00）                          */
/* ====================================================================== */

/** JPEG 熵编码位写出器。 */
typedef struct JpegBitWriter
{
    uint8_t* out;    /**< 已写出的熵编码字节 */
    size_t   len;    /**< 有效字节数 */
    size_t   cap;    /**< 分配容量 */
    uint32_t acc;    /**< 待写位累加器（MSB 在前） */
    int      nbits;  /**< 累加器中的有效位数（0..7） */
    bool     error;  /**< 出错标志 */
} JpegBitWriter;

/* 确保还能容纳 extra 字节，必要时扩容；失败返回 false。 */
static bool jpegBitWriterReserve(JpegBitWriter* w, size_t extra)
{
    size_t need;
    if (extra > SIZE_MAX - w->len) { w->error = true; return false; }
    need = w->len + extra;
    if (need > w->cap) {
        size_t cap = w->cap ? w->cap : 256;
        uint8_t* p;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        p = (uint8_t*)XMalloc_System(cap);
        if (!p) { w->error = true; return false; }
        if (w->out) { memcpy(p, w->out, w->len); XFree_System(w->out); }
        w->out = p;
        w->cap = cap;
    }
    return true;
}

/* 写一个原始字节，0xFF 后自动追加 0x00（熵数据字节填充）。 */
static bool jpegBitWriterPutByte(JpegBitWriter* w, uint8_t c)
{
    if (!jpegBitWriterReserve(w, c == 0xFF ? 2u : 1u)) return false;
    w->out[w->len++] = c;
    if (c == 0xFF) w->out[w->len++] = 0x00;
    return true;
}

/* 写 1 位。 */
static bool jpegBitWriterPutBit(JpegBitWriter* w, int bit)
{
    w->acc = (w->acc << 1) | (uint32_t)(bit & 1);
    if (++w->nbits == 8) {
        uint8_t c = (uint8_t)(w->acc & 0xFFu);
        w->acc >>= 8;
        w->nbits = 0;
        return jpegBitWriterPutByte(w, c);
    }
    return true;
}

/* 写 count 位（MSB 在前），count 不超过 16。 */
static bool jpegBitWriterPutBits(JpegBitWriter* w, unsigned value, int count)
{
    for (int i = count - 1; i >= 0; --i)
        if (!jpegBitWriterPutBit(w, (int)((value >> i) & 1u))) return false;
    return true;
}

/* 以补 1 的方式对齐到字节边界。 */
static bool jpegBitWriterAlign(JpegBitWriter* w)
{
    while (w->nbits > 0)
        if (!jpegBitWriterPutBit(w, 1)) return false;
    return true;
}

/* 释放位写出器的内部缓冲区。 */
static void jpegBitWriterFree(JpegBitWriter* w)
{
    if (w->out) { XFree_System(w->out); w->out = NULL; }
    w->len = w->cap = 0;
    w->acc = 0;
    w->nbits = 0;
    w->error = false;
}

/* ====================================================================== */
/* 编码器：标记段与量化表                                                    */
/* ====================================================================== */

/* 向 XByteArray 追加 2 字节大端整数。 */
static bool jpegAppendBe16(XByteArray* out, uint16_t value)
{
    uint8_t b[2];
    b[0] = (uint8_t)(value >> 8);
    b[1] = (uint8_t)value;
    return XImageCodecInternal_appendBytes(out, b, 2);
}

/* 追加一个标记段：FF code + 长度(2+载荷) + 载荷。 */
static bool jpegAppendSegment(XByteArray* out, uint8_t code,
                              const uint8_t* payload, size_t payloadLen)
{
    static const uint8_t headMarker = 0xFF;
    if (payloadLen > 65535u - 2u) return false;
    if (!XImageCodecInternal_appendBytes(out, &headMarker, 1)) return false;
    if (!XImageCodecInternal_appendBytes(out, &code, 1)) return false;
    if (!jpegAppendBe16(out, (uint16_t)(payloadLen + 2u))) return false;
    return XImageCodecInternal_appendBytes(out, payload, payloadLen);
}

/* 按质量缩放标准量化表（quality 已收敛到 1..100）。 */
static void jpegMakeQuantTable(const uint8_t base[64], int scale,
                               uint8_t outQ[64])
{
    for (int i = 0; i < 64; ++i) {
        int v = ((int)base[i] * scale + 50) / 100;
        if (v < 1) v = 1;
        if (v > 255) v = 255;
        outQ[i] = (uint8_t)v;
    }
}

/* 追加 DQT 段（第 tableId 张 8 位量化表，之字形顺序）。 */
static bool jpegAppendDqt(XByteArray* out, const uint8_t quantNat[64],
                          int tableId)
{
    uint8_t body[65];
    body[0] = (uint8_t)tableId;
    for (int k = 0; k < 64; ++k)
        body[1 + k] = quantNat[jpegZigzagNatural[k]];
    return jpegAppendSegment(out, 0xDB, body, sizeof(body));
}

/* 追加 DHT 段（一张表：tc=0/1 DC/AC，th 为表号）。 */
static bool jpegAppendDht(XByteArray* out, int tc, int th,
                          const uint8_t* counts, const uint8_t* symbols)
{
    uint8_t body[1 + 16 + 162];
    int total = 0;
    size_t n = 0;
    body[n++] = (uint8_t)((tc << 4) | th);
    for (int i = 0; i < 16; ++i) {
        body[n++] = counts[i];
        total += counts[i];
    }
    if (total > 162) return false;
    for (int i = 0; i < total; ++i) body[n++] = symbols[i];
    return jpegAppendSegment(out, 0xC4, body, n);
}

/* 追加 APP0（JFIF 1.1）段。 */
static bool jpegAppendApp0(XByteArray* out)
{
    static const uint8_t body[14] = {
        'J', 'F', 'I', 'F', 0,    /* 标识 */
        1, 1,                     /* 版本号 1.1 */
        0,                        /* 单位：无 */
        0, 1,                     /* X 方向像素密度 */
        0, 1,                     /* Y 方向像素密度 */
        0, 0                      /* 缩略图宽高 */
    };
    return jpegAppendSegment(out, 0xE0, body, sizeof(body));
}

/* 追加 SOF0 段：8 位精度的 YCbCr 4:2:0 基线顺序帧。 */
static bool jpegAppendSof0(XByteArray* out, int width, int height)
{
    uint8_t body[15];
    size_t n = 0;
    body[n++] = 8;                                /* 精度 */
    body[n++] = (uint8_t)(height >> 8);           /* 高度 */
    body[n++] = (uint8_t)height;
    body[n++] = (uint8_t)(width >> 8);            /* 宽度 */
    body[n++] = (uint8_t)width;
    body[n++] = 3;                                /* 分量数 */
    body[n++] = 1; body[n++] = 0x22; body[n++] = 0; /* Y: 2x2 抽样, 表0 */
    body[n++] = 2; body[n++] = 0x11; body[n++] = 1; /* Cb: 1x1 抽样, 表1 */
    body[n++] = 3; body[n++] = 0x11; body[n++] = 1; /* Cr: 1x1 抽样, 表1 */
    return jpegAppendSegment(out, 0xC0, body, n);
}

/* 追加 SOS 段：单扫描交错顺序，Ss=0、Se=63、AhAl=0。 */
static bool jpegAppendSos(XByteArray* out)
{
    uint8_t body[10];
    size_t n = 0;
    body[n++] = 3;                                /* 本扫描分量数 */
    body[n++] = 1; body[n++] = 0x00;              /* Y: DC 表0, AC 表0 */
    body[n++] = 2; body[n++] = 0x11;              /* Cb: DC 表1, AC 表1 */
    body[n++] = 3; body[n++] = 0x11;              /* Cr: DC 表1, AC 表1 */
    body[n++] = 0;                                /* Ss */
    body[n++] = 63;                               /* Se */
    body[n++] = 0;                                /* Ah/Al */
    return jpegAppendSegment(out, 0xDA, body, n);
}

/* ====================================================================== */
/* 编码器：DCT 系数熵编码                                                    */
/* ====================================================================== */

/* RGB -> YCbCr（JFIF BT.601 全范围）。 */
static void jpegRgbToYuv(int r, int g, int b, int* y, int* cb, int* cr)
{
    *y = jpegClamp255(jpegRound(0.299f * (float)r + 0.587f * (float)g +
                                0.114f * (float)b));
    *cb = jpegClamp255(jpegRound(128.0f - 0.168736f * (float)r -
                                 0.331264f * (float)g + 0.5f * (float)b));
    *cr = jpegClamp255(jpegRound(128.0f + 0.5f * (float)r -
                                 0.418688f * (float)g - 0.081312f * (float)b));
}

/* 符号幅度类别（数值所需的位数，JPEG category）。 */
static int jpegEncodeCategory(int value)
{
    unsigned int u = value < 0 ? (0u - (unsigned int)value) : (unsigned int)value;
    int n = 0;
    while (u) { ++n; u >>= 1; }
    return n;
}

/* 写出符号幅度位（负数按 JPEG receive_extend 反变换编码）。 */
static bool jpegEncodeMagnitude(JpegBitWriter* w, int value, int category)
{
    unsigned int bits;
    if (category <= 0) return true;
    if (value >= 0) bits = (unsigned int)value;
    else bits = (unsigned int)(value + (1 << category) - 1);
    return jpegBitWriterPutBits(w, bits, category);
}

/* 编码一个 8x8 块：DCT -> 量化 -> 之字形 -> DC 差分/AC 游程 Huffman。 */
static bool jpegEncodeBlock(JpegBitWriter* w,
                            const JpegHuffEncode* dcTable,
                            const JpegHuffEncode* acTable,
                            const uint8_t* quant,
                            const uint8_t samples[64],
                            int* dcPred)
{
    float shifted[64];
    float f[64];
    int coeff[64];
    int diff, category, run = 0;

    for (int i = 0; i < 64; ++i)
        shifted[i] = (float)samples[i] - 128.0f; /* 电平搬移 -128 */
    jpegForwardDct(shifted, f);
    for (int z = 0; z < 64; ++z) {
        int nat = jpegZigzagNatural[z];
        int q = quant[nat];
        coeff[z] = q > 0 ? jpegRound(f[jpegTransposeIndex(nat)] / (float)q) : 0;
    }

    diff = coeff[0] - *dcPred;
    *dcPred = coeff[0];
    category = jpegEncodeCategory(diff);
    if (!jpegBitWriterPutBits(w, dcTable->code[category],
                              dcTable->size[category]))
        return false;
    if (!jpegEncodeMagnitude(w, diff, category)) return false;

    for (int k = 1; k < 64; ++k) {
        int cv;
        int rs;
        if (coeff[k] == 0) { ++run; continue; }
        cv = coeff[k];
        while (run >= 16) {
            /* ZRL：16 个连续零 + 后续非零系数 */
            if (!jpegBitWriterPutBits(w, acTable->code[0xF0],
                                      acTable->size[0xF0]))
                return false;
            run -= 16;
        }
        category = jpegEncodeCategory(cv);
        rs = (run << 4) | category;
        if (rs > 0xFF) { w->error = true; return false; }
        if (!jpegBitWriterPutBits(w, acTable->code[rs],
                                  acTable->size[rs]))
            return false;
        if (!jpegEncodeMagnitude(w, cv, category)) return false;
        run = 0;
    }
    /* 末尾零：EOB（0x00）。若最后非零系数恰在 63 位则不发送。 */
    if (run > 0) {
        if (!jpegBitWriterPutBits(w, acTable->code[0x00],
                                  acTable->size[0x00]))
            return false;
    }
    return true;
}

/* ====================================================================== */
/* 编码器入口                                                                 */
/* ====================================================================== */

/* 将 XImage 编码为基线 JPEG（YCbCr 4:2:0，质量 1..100，<=0 取 75）。 */
bool XImageCodecInternal_encodeJpeg(const XImage* image, int quality,
                                    XByteArray* out)
{
    int width, height, mcuW, mcuH, scale;
    uint8_t qLum[64], qChr[64];
    uint8_t* yPlane = NULL;
    uint8_t* cbPlane = NULL;
    uint8_t* crPlane = NULL;
    JpegHuffEncode dcLum, dcChr, acLum, acChr;
    JpegBitWriter w;
    int dcPred[3] = { 0, 0, 0 };
    size_t ySize = 0, cSize = 0;
    bool ok = false;

    if (!image || !out || XImage_isNull(image)) return false;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return false;
    if (quality <= 0) quality = 75;   /* 默认质量 */
    if (quality > 100) quality = 100;
    scale = quality < 50 ? (5000 / quality) : (200 - 2 * quality);
    jpegMakeQuantTable(jpegQuantLuminance, scale, qLum);
    jpegMakeQuantTable(jpegQuantChrominance, scale, qChr);

    mcuW = (width + 15) / 16;
    mcuH = (height + 15) / 16;
    if ((size_t)mcuW * 2u > SIZE_MAX / ((size_t)mcuH * 2u)) return false;
    if ((size_t)mcuW * 2u > SIZE_MAX / 64u) return false;
    ySize = (size_t)mcuW * 2u * (size_t)mcuH * 2u * 64u;
    cSize = (size_t)mcuW * (size_t)mcuH * 64u;
    if (ySize == 0 || cSize == 0) return false;
    yPlane = (uint8_t*)XMalloc_System(ySize);
    cbPlane = (uint8_t*)XMalloc_System(cSize);
    crPlane = (uint8_t*)XMalloc_System(cSize);
    if (!yPlane || !cbPlane || !crPlane) goto done;

    jpegHuffmanBuildEncode(jpegStdDcLumCounts, jpegStdDcSymbols, 12, &dcLum);
    jpegHuffmanBuildEncode(jpegStdDcChromaCounts, jpegStdDcChromaSymbols, 12, &dcChr);
    jpegHuffmanBuildEncode(jpegStdAcLumCounts, jpegStdAcLumSymbols, 162, &acLum);
    jpegHuffmanBuildEncode(jpegStdAcChromaCounts, jpegStdAcChromaSymbols, 162, &acChr);

    /* 亮度 8x8 块平面（补齐到 16 像素网格，越界像素复制边缘）。 */
    for (int by = 0; by < mcuH * 2; ++by) {
        for (int bx = 0; bx < mcuW * 2; ++bx) {
            uint8_t* dst = yPlane +
                ((size_t)by * (size_t)mcuW * 2u + (size_t)bx) * 64u;
            for (int dy = 0; dy < 8; ++dy) {
                int py = by * 8 + dy;
                if (py >= height) py = height - 1;
                for (int dx = 0; dx < 8; ++dx) {
                    int px = bx * 8 + dx;
                    uint32_t c;
                    int r, g, b, yv, cb, cr;
                    if (px >= width) px = width - 1;
                    c = XImage_pixel(image, px, py);
                    r = (int)((c >> 16) & 0xffu);
                    g = (int)((c >> 8) & 0xffu);
                    b = (int)(c & 0xffu);
                    jpegRgbToYuv(r, g, b, &yv, &cb, &cr);
                    dst[dy * 8 + dx] = (uint8_t)yv;
                }
            }
        }
    }

    /* 色度 8x8 块平面（4:2:0，2x2 像素平均）。 */
    for (int by = 0; by < mcuH; ++by) {
        for (int bx = 0; bx < mcuW; ++bx) {
            uint8_t* dCb = cbPlane + ((size_t)by * (size_t)mcuW + (size_t)bx) * 64u;
            uint8_t* dCr = crPlane + ((size_t)by * (size_t)mcuW + (size_t)bx) * 64u;
            for (int dy = 0; dy < 8; ++dy) {
                for (int dx = 0; dx < 8; ++dx) {
                    int sCb = 0, sCr = 0;
                    for (int oy = 0; oy < 2; ++oy) {
                        for (int ox = 0; ox < 2; ++ox) {
                            int px = bx * 16 + dx * 2 + ox;
                            int py = by * 16 + dy * 2 + oy;
                            uint32_t c;
                            int r, g, b, yv, cbv, crv;
                            if (px >= width) px = width - 1;
                            if (py >= height) py = height - 1;
                            c = XImage_pixel(image, px, py);
                            r = (int)((c >> 16) & 0xffu);
                            g = (int)((c >> 8) & 0xffu);
                            b = (int)(c & 0xffu);
                            jpegRgbToYuv(r, g, b, &yv, &cbv, &crv);
                            sCb += cbv;
                            sCr += crv;
                        }
                    }
                    dCb[dy * 8 + dx] = (uint8_t)((sCb + 2) / 4);
                    dCr[dy * 8 + dx] = (uint8_t)((sCr + 2) / 4);
                }
            }
        }
    }

    memset(&w, 0, sizeof(w));
    if (!XByteArray_resize_base((XVector*)out, 0)) goto done;
    {
        static const uint8_t soi[2] = { 0xFF, 0xD8 };
        if (!XImageCodecInternal_appendBytes(out, soi, 2)) goto done;
    }
    if (!jpegAppendApp0(out)) goto done;
    if (!jpegAppendDqt(out, qLum, 0)) goto done;
    if (!jpegAppendDqt(out, qChr, 1)) goto done;
    if (!jpegAppendSof0(out, width, height)) goto done;
    if (!jpegAppendDht(out, 0, 0, jpegStdDcLumCounts, jpegStdDcSymbols)) goto done;
    if (!jpegAppendDht(out, 0, 1, jpegStdDcChromaCounts, jpegStdDcChromaSymbols)) goto done;
    if (!jpegAppendDht(out, 1, 0, jpegStdAcLumCounts, jpegStdAcLumSymbols)) goto done;
    if (!jpegAppendDht(out, 1, 1, jpegStdAcChromaCounts, jpegStdAcChromaSymbols)) goto done;
    if (!jpegAppendSos(out)) goto done;

    /* MCU：4 个亮度块 + 2 个色度块（先 Y，再 Cb、Cr）。 */
    for (int my = 0; my < mcuH; ++my) {
        for (int mx = 0; mx < mcuW; ++mx) {
            for (int b = 0; b < 2; ++b) {
                for (int a = 0; a < 2; ++a) {
                    const uint8_t* src = yPlane +
                        ((size_t)(my * 2 + b) * (size_t)mcuW * 2u +
                         (size_t)(mx * 2 + a)) * 64u;
                    if (!jpegEncodeBlock(&w, &dcLum, &acLum, qLum,
                                         src, &dcPred[0]))
                        goto done;
                }
            }
            {
                const uint8_t* src = cbPlane +
                    ((size_t)my * (size_t)mcuW + (size_t)mx) * 64u;
                if (!jpegEncodeBlock(&w, &dcChr, &acChr, qChr,
                                     src, &dcPred[1]))
                    goto done;
            }
            {
                const uint8_t* src = crPlane +
                    ((size_t)my * (size_t)mcuW + (size_t)mx) * 64u;
                if (!jpegEncodeBlock(&w, &dcChr, &acChr, qChr,
                                     src, &dcPred[2]))
                    goto done;
            }
        }
    }
    if (w.error) goto done;
    if (!jpegBitWriterAlign(&w)) goto done;
    if (!XImageCodecInternal_appendBytes(out, w.out, w.len)) goto done;
    {
        static const uint8_t eoi[2] = { 0xFF, 0xD9 };
        if (!XImageCodecInternal_appendBytes(out, eoi, 2)) goto done;
    }
    ok = true;

done:
    jpegBitWriterFree(&w);
    if (yPlane) XFree_System(yPlane);
    if (cbPlane) XFree_System(cbPlane);
    if (crPlane) XFree_System(crPlane);
    if (!ok) XByteArray_resize_base((XVector*)out, 0); /* 失败不留半截数据 */
    return ok;
}

#endif /* XIMAGECODEC_JPEG_ON */
#endif /* XIMAGECODEC_ON */
