/******************************************************************************
 * @file       XImageCodecPpm.c
 * @brief      PPM/PBM/PGM（P1-P6）便携图像编解码实现。
 * @note       对齐 Qt 6.8 QPpmHandler 的头部、尺寸探测和像素语义；输入
 *              支持 ASCII 与二进制变体；编码按 Qt 子类型分别输出 PBM P4、
 *              PGM P5 或 PPM P6。所有分配使用项目内存接口，不依赖平台 API。
 ******************************************************************************/
#include "XImageCodecInternal.h"
#include "XMemory.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if XIMAGECODEC_ON && XIMAGECODEC_PPM_ON

typedef struct XImageCodecPpmHeader
{
    int m_type;             /**< PPM 类型数字 1..6。 */
    int m_width;            /**< 图像宽度。 */
    int m_height;           /**< 图像高度。 */
    unsigned int m_maxValue;/**< 灰度/RGB 最大样本值；PBM 固定为 1。 */
    size_t m_dataOffset;    /**< 二进制像素数据起始偏移。 */
} XImageCodecPpmHeader;

/*
 * 等价于 Qt qppmhandler.cpp 的 read_pbm_int()。扫描器只在输入缓冲区上
 * 前进，不复制令牌，因此超长样本仍按 Qt 的 int 溢出规则返回 -1；
 * maxDigits=1 用于 P1，读取一位后立即返回并保留下一位供下一次读取。
 */
static bool ppm_readQtInt(const uint8_t* data, size_t size, size_t* pos,
                          int maxDigits, int* value)
{
    unsigned long long result = 0;
    bool haveDigit = false;
    bool overflow = false;
    if (!data || !pos || !value) return false;

    while (*pos < size) {
        uint8_t valueByte = data[*pos];
        bool digit = valueByte >= (uint8_t)'0' && valueByte <= (uint8_t)'9';
        if (haveDigit) {
            if (digit) {
                unsigned int digitValue = (unsigned int)(valueByte - (uint8_t)'0');
                if (!overflow) {
                    if (result > ((unsigned long long)INT_MAX - digitValue) / 10ULL)
                        overflow = true;
                    else
                        result = result * 10ULL + digitValue;
                }
                ++*pos;
                if (maxDigits > 0 && --maxDigits == 0)
                    break;
                continue;
            }
            if (valueByte == (uint8_t)'#') {
                do {
                    ++*pos;
                } while (*pos < size && data[*pos] != (uint8_t)'\n');
                if (*pos < size) ++*pos;
            } else {
                /* read_pbm_int() consumes one arbitrary non-digit terminator. */
                ++*pos;
            }
            break;
        }

        if (digit) {
            haveDigit = true;
            result = (unsigned long long)(valueByte - (uint8_t)'0');
            ++*pos;
            if (maxDigits > 0 && --maxDigits == 0)
                break;
        } else if (valueByte == (uint8_t)' ' || valueByte == (uint8_t)'\t' ||
                   valueByte == (uint8_t)'\n' || valueByte == (uint8_t)'\v' ||
                   valueByte == (uint8_t)'\f' || valueByte == (uint8_t)'\r') {
            ++*pos;
        } else if (valueByte == (uint8_t)'#') {
            do {
                ++*pos;
            } while (*pos < size && data[*pos] != (uint8_t)'\n');
            if (*pos < size) ++*pos;
        } else {
            ++*pos;
            return false;
        }
    }

    if (!haveDigit) return false;
    *value = overflow ? -1 : (int)result;
    return true;
}

static bool ppm_parseHeader(const uint8_t* data, size_t size,
                            XImageCodecPpmHeader* header)
{
    int value;
    size_t pos = 3u;
    if (!data || !header || size < 3u || data[0] != (uint8_t)'P' ||
        data[1] < (uint8_t)'1' || data[1] > (uint8_t)'6' ||
        !(data[2] == (uint8_t)' ' || data[2] == (uint8_t)'\t' ||
          data[2] == (uint8_t)'\n' || data[2] == (uint8_t)'\v' ||
          data[2] == (uint8_t)'\f' || data[2] == (uint8_t)'\r'))
        return false;
    header->m_type = data[1] - (uint8_t)'0';
    if (!ppm_readQtInt(data, size, &pos, -1, &value) || value <= 0)
        return false;
    header->m_width = value;
    if (!ppm_readQtInt(data, size, &pos, -1, &value) || value <= 0)
        return false;
    header->m_height = value;
    /* QPpmHandler rejects dimensions outside the signed 15-bit range before
     * allocating the destination image.  Keep the same observable boundary;
     * XImage's own allocation checks still protect the total byte count. */
    if (header->m_width > 32767 || header->m_height > 32767)
        return false;
    if (header->m_type == 1 || header->m_type == 4) {
        header->m_maxValue = 1;
    } else {
        if (!ppm_readQtInt(data, size, &pos, -1, &value) || value <= 0 ||
            value > 65535)
            return false;
        header->m_maxValue = (unsigned int)value;
    }
    /* read_pbm_int() already consumed the first terminator.  For CRLF this
     * leaves LF as the first raw sample, exactly as Qt's QIODevice path does. */
    header->m_dataOffset = pos;
    return true;
}

static bool ppm_readBinarySample(const uint8_t* data, size_t size, size_t* pos,
                                 unsigned int maxValue, unsigned int* sample)
{
    if (!data || !pos || !sample || *pos >= size) return false;
    if (maxValue < 256u) {
        *sample = data[(*pos)++];
    } else {
        if (*pos + 1 >= size) return false;
        *sample = ((unsigned int)data[*pos] << 8) |
                  (unsigned int)data[*pos + 1];
        *pos += 2;
    }
    /* Qt's raw path does not reject a byte/word above maxval; it applies the
     * same integer conversion and lets the destination component truncate.
     * Keep the argument in the signature because the byte width depends on
     * maxval, but deliberately do not use it as a range validator. */
    (void)maxValue;
    return true;
}

static bool ppm_readAsciiSample(const uint8_t* data, size_t size, size_t* pos,
                                unsigned int maxValue, unsigned int* sample)
{
    int value;
    if (!ppm_readQtInt(data, size, pos, -1, &value)) return false;
    /* read_pbm_int() accepts values outside maxval; scaling or qRgb() then
     * reproduces Qt's arithmetic and narrowing behavior.  On int overflow Qt
     * returns -1 while keeping ok=true; UINT_MAX has the same low-byte and
     * low-quint16 effects in the unsigned pipeline below. */
    (void)maxValue;
    *sample = value < 0 ? UINT_MAX : (unsigned int)value;
    return true;
}

/* ASCII PBM (P1) passes maxDigits=1 to Qt's read_pbm_int().  Consequently
 * "10" is two successive bit samples rather than one decimal sample. */
static bool ppm_readAsciiBit(const uint8_t* data, size_t size, size_t* pos,
                             unsigned int* sample)
{
    int value;
    if (!ppm_readQtInt(data, size, pos, 1, &value)) return false;
    *sample = (unsigned int)value & 1u;
    return true;
}

static uint8_t ppm_scaleGray(unsigned int sample, unsigned int maxValue)
{
    /* QPpmHandler writes read_pbm_int() directly for maxval 255, so an
     * oversized ASCII sample is narrowed to its low byte rather than scaled.
     * For every other ASCII maxval Qt first narrows the int to quint16.  Raw
     * samples already have the corresponding byte/word width. */
    if (maxValue == 255u)
        return (uint8_t)sample;
    return (uint8_t)(((uint64_t)(sample & 0xffffu) * 255u) / maxValue);
}

static uint8_t ppm_scaleColor(unsigned int sample, unsigned int maxValue)
{
    uint64_t expanded;
    /* For maxval 255 Qt uses qRgb(), which masks each component to 8 bits;
     * non-255 values go through scale_pbm_color(quint16, ...). */
    if (maxValue == 255u)
        return (uint8_t)sample;
    expanded = ((uint64_t)(sample & 0xffffu) * 65535u) / maxValue;
    /* QRgba64::toArgb32() uses div_257 rounding after the 16-bit expansion. */
    expanded += 128u;
    return (uint8_t)((expanded - (expanded >> 8)) >> 8);
}

bool XImageCodecInternal_probePpmSize(const uint8_t* data, size_t size,
                                      int* width, int* height)
{
    XImageCodecPpmHeader header;
    if (!width || !height || !ppm_parseHeader(data, size, &header)) return false;
    *width = header.m_width;
    *height = header.m_height;
    return true;
}

bool XImageCodecInternal_decodePpm(const uint8_t* data, size_t size, XImage* out)
{
    XImageCodecPpmHeader header;
    XImage temp;
    XImageFormat outputFormat;
    size_t pos;
    int x;
    int y;
    bool ascii;
    if (!out || !ppm_parseHeader(data, size, &header)) return false;
    outputFormat = (header.m_type == 1 || header.m_type == 4)
        ? XImageFormat_Mono
        : ((header.m_type == 2 || header.m_type == 5)
            ? XImageFormat_Grayscale8 : XImageFormat_RGB32);
    XImage_init_ex(&temp, header.m_width, header.m_height, outputFormat);
    if (XImage_isNull(&temp)) {
        XImage_deinit_base(&temp);
        return false;
    }
    ascii = header.m_type <= 3;
    pos = header.m_dataOffset;
    if (header.m_type == 4) {
        size_t rowBytes = ((size_t)header.m_width + 7u) / 8u;
        if (rowBytes > SIZE_MAX / (size_t)header.m_height ||
            size - pos < rowBytes * (size_t)header.m_height) {
            XImage_deinit_base(&temp);
            return false;
        }
        for (y = 0; y < header.m_height; ++y) {
            const uint8_t* row = data + pos + rowBytes * (size_t)y;
            for (x = 0; x < header.m_width; ++x) {
                bool black = (row[(unsigned)x >> 3] &
                              (uint8_t)(0x80u >> ((unsigned)x & 7u))) != 0;
                /* PBM stores 1 for black and 0 for white.  Keep the native
                 * Mono bit and install Qt's white/black palette below. */
                XImage_setPixel(&temp, x, y, black ? 1u : 0u);
            }
        }
    } else {
        for (y = 0; y < header.m_height; ++y) {
            for (x = 0; x < header.m_width; ++x) {
                unsigned int first;
                unsigned int green;
                unsigned int blue;
                bool ok = header.m_type == 1
                    ? ppm_readAsciiBit(data, size, &pos, &first)
                    : (ascii
                        ? ppm_readAsciiSample(data, size, &pos, header.m_maxValue, &first)
                        : ppm_readBinarySample(data, size, &pos, header.m_maxValue, &first));
                if (!ok) {
                    XImage_deinit_base(&temp);
                    return false;
                }
                if (header.m_type == 1) {
                    XImage_setPixel(&temp, x, y, first ? 1u : 0u);
                } else if (header.m_type == 2 || header.m_type == 5) {
                    uint8_t gray = ppm_scaleGray(first, header.m_maxValue);
                    XImage_setPixel(&temp, x, y, 0xff000000u |
                                    ((uint32_t)gray << 16) |
                                    ((uint32_t)gray << 8) | gray);
                } else {
                    bool greenOk = ascii
                        ? ppm_readAsciiSample(data, size, &pos, header.m_maxValue, &green)
                        : ppm_readBinarySample(data, size, &pos, header.m_maxValue, &green);
                    bool blueOk = greenOk && (ascii
                        ? ppm_readAsciiSample(data, size, &pos, header.m_maxValue, &blue)
                        : ppm_readBinarySample(data, size, &pos, header.m_maxValue, &blue));
                    if (!greenOk || !blueOk) {
                        XImage_deinit_base(&temp);
                        return false;
                    }
                    XImage_setPixel(&temp, x, y, 0xff000000u |
                                    ((uint32_t)ppm_scaleColor(first, header.m_maxValue) << 16) |
                                    ((uint32_t)ppm_scaleColor(green, header.m_maxValue) << 8) |
                                    ppm_scaleColor(blue, header.m_maxValue));
                }
            }
        }
    }
    if (outputFormat == XImageFormat_Mono) {
        /* QPpmHandler::read_pbm_body() exposes Format_Mono with index 0
         * white and index 1 black, matching PBM's on-disk convention. */
        XImage_setColorCount(&temp, 2);
        XImage_setColor(&temp, 0, 0xffffffffu);
        XImage_setColor(&temp, 1, 0xff000000u);
    }
    XImage_move_base(out, &temp);
    return true;
}

static bool ppm_subtypeIs(const char* subtype, const char* expected)
{
    size_t i;
    if (!subtype || !expected) return false;
    for (i = 0; i < 3; ++i) {
        if (!subtype[i] || !expected[i] ||
            tolower((unsigned char)subtype[i]) !=
                tolower((unsigned char)expected[i]))
            return false;
    }
    return true;
}

static uint8_t ppm_luma(uint32_t color)
{
    unsigned red = (color >> 16) & 0xffu;
    unsigned green = (color >> 8) & 0xffu;
    unsigned blue = color & 0xffu;
    return (uint8_t)((299u * red + 587u * green + 114u * blue + 500u) / 1000u);
}

bool XImageCodecInternal_encodePpmSubtype(const XImage* image,
                                          const char* subtype,
                                          XByteArray* out)
{
    char header[64];
    int width;
    int height;
    size_t rowBytes;
    size_t total;
    uint8_t* pixels;
    int x;
    int y;
    char type;
    if (!image || !out || XImage_isNull(image) || !subtype) return false;
    if (ppm_subtypeIs(subtype, "pbm")) type = '4';
    else if (ppm_subtypeIs(subtype, "pgm")) type = '5';
    else if (ppm_subtypeIs(subtype, "ppm")) type = '6';
    else return false;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height)
        return false;
    rowBytes = type == '4' ? ((size_t)width + 7u) / 8u
                           : (type == '5' ? (size_t)width : (size_t)width * 3u);
    if (rowBytes > SIZE_MAX / (size_t)height) return false;
    total = rowBytes * (size_t)height;
    pixels = (uint8_t*)XMalloc_System(total ? total : 1u);
    if (!pixels) return false;
    memset(pixels, 0, total);
    if (type == '4') {
        for (y = 0; y < height; ++y) {
            for (x = 0; x < width; ++x) {
                uint32_t color = XImage_pixel(image, x, y);
                /* PBM uses one for black and zero for white. */
                if (ppm_luma(color) < 128u)
                    pixels[(size_t)y * rowBytes + ((size_t)x >> 3)] |=
                        (uint8_t)(0x80u >> ((unsigned)x & 7u));
            }
        }
    } else if (type == '5') {
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x)
                pixels[(size_t)y * rowBytes + (size_t)x] =
                    ppm_luma(XImage_pixel(image, x, y));
    } else {
        for (y = 0; y < height; ++y) {
            for (x = 0; x < width; ++x) {
                uint32_t color = XImage_pixel(image, x, y);
                size_t index = (size_t)y * rowBytes + (size_t)x * 3u;
                pixels[index] = (uint8_t)(color >> 16);
                pixels[index + 1] = (uint8_t)(color >> 8);
                pixels[index + 2] = (uint8_t)color;
            }
        }
    }
    if ((type == '4'
             ? snprintf(header, sizeof(header), "P4\n%d %d\n", width, height)
             : snprintf(header, sizeof(header), "P%c\n%d %d\n255\n", type,
                        width, height)) <= 0 ||
        !XImageCodecInternal_appendBytes(out, header, strlen(header)) ||
        !XImageCodecInternal_appendBytes(out, pixels, total)) {
        XFree_System(pixels);
        return false;
    }
    XFree_System(pixels);
    return true;
}

bool XImageCodecInternal_encodePpm(const XImage* image, XByteArray* out)
{
    /* The format-only facade has no subtype argument; Qt's ordinary PPM
     * default is the RGB P6 variant.  Handler callers use the subtype-aware
     * entry point above for pbm/pgm aliases. */
    return XImageCodecInternal_encodePpmSubtype(image, "ppm", out);
}

#endif /* XIMAGECODEC_ON && XIMAGECODEC_PPM_ON */
