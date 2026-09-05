/******************************************************************************
 * @file       XImageCodecXbm.c
 * @brief      XBM（X11 Bitmap）图像编解码实现，对齐 Qt 6.8 QXbmHandler。
 * @note       XBM 是 C 源码形式的单色位图：头部由两个 #define 行组成，
 *             像素数组由 0xHH 十六进制字节组成。实现严格使用 MonoLSB
 *             存储、白色索引 0/黑色索引 1 和项目 XMalloc_System 内存接口，
 *             不调用平台 API，所有扫描都限制在输入缓冲区边界内。
 ******************************************************************************/
#include "XImageCodecInternal.h"
#include "XImageCodec_config.h"
#include "XMemory.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if XIMAGECODEC_ON && XIMAGECODEC_XBM_ON

typedef struct XImageCodecXbmHeader
{
    int m_width;
    int m_height;
    size_t m_bodyOffset;
} XImageCodecXbmHeader;

static bool xbm_space(unsigned char c)
{
    return c == (unsigned char)' ' || c == (unsigned char)'\t';
}

static bool xbm_identifier(unsigned char c)
{
    return (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
           (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
           (c >= (unsigned char)'0' && c <= (unsigned char)'9') ||
           c == (unsigned char)'_' || c == (unsigned char)'.';
}

static int xbm_hex(unsigned char c)
{
    if (c >= (unsigned char)'0' && c <= (unsigned char)'9') return (int)(c - '0');
    if (c >= (unsigned char)'a' && c <= (unsigned char)'f') return (int)(c - 'a') + 10;
    if (c >= (unsigned char)'A' && c <= (unsigned char)'F') return (int)(c - 'A') + 10;
    return -1;
}

/* 解析一行 #define，语义对应 qxbmhandler.cpp:57-76 的 parseDefine。 */
static bool xbm_parseDefine(const uint8_t* line, size_t length, int* value)
{
    size_t pos = 0;
    unsigned long long magnitude = 0;
    bool negative = false;
    bool hasDigit = false;
    if (!line || !value || length < 7 || memcmp(line, "#define", 7) != 0)
        return false;
    pos = 7;
    while (pos < length && xbm_space(line[pos])) ++pos;
    while (pos < length && xbm_identifier(line[pos])) ++pos;
    while (pos < length && xbm_space(line[pos])) ++pos;
    if (pos < length && (line[pos] == (uint8_t)'+' || line[pos] == (uint8_t)'-')) {
        negative = line[pos] == (uint8_t)'-';
        ++pos;
    }
    while (pos < length && line[pos] >= (uint8_t)'0' && line[pos] <= (uint8_t)'9') {
        unsigned digit = (unsigned)(line[pos] - (uint8_t)'0');
        hasDigit = true;
        if (magnitude > ((unsigned long long)INT_MAX - digit) / 10ULL)
            return false;
        magnitude = magnitude * 10ULL + digit;
        ++pos;
    }
    while (pos < length && xbm_space(line[pos])) ++pos;
    if (!hasDigit || pos != length || negative || magnitude == 0 || magnitude > 32767ULL)
        return false;
    *value = (int)magnitude;
    return true;
}

static bool xbm_readLine(const uint8_t* data, size_t size, size_t* pos,
                         const uint8_t** line, size_t* length, size_t* consumed)
{
    size_t start;
    size_t end;
    if (!data || !pos || !line || !length || !consumed || *pos >= size)
        return false;
    start = *pos;
    end = start;
    while (end < size && data[end] != (uint8_t)'\n') ++end;
    *line = data + start;
    *length = end - start;
    *consumed = end < size ? end - start + 1u : end - start;
    *pos = end < size ? end + 1u : size;
    return true;
}

static bool xbm_parseHeader(const uint8_t* data, size_t size,
                            XImageCodecXbmHeader* header)
{
    size_t pos = 0;
    size_t totalRead = 0;
    const uint8_t* line;
    size_t length;
    size_t consumed;
    int width = 0;
    int height = 0;
    bool foundDefine = false;
    if (!data || !header || !size) return false;
    /* Qt limits each readLine() to 300 bytes and the header to 4096 bytes. */
    while (pos < size) {
        if (!xbm_readLine(data, size, &pos, &line, &length, &consumed)) return false;
        if (length >= 299u) return false;
        totalRead += consumed;
        if (totalRead >= 4096u) return false;
        if (length && line[0] == (uint8_t)'#') {
            foundDefine = xbm_parseDefine(line, length, &width);
            break;
        }
    }
    if (!foundDefine || pos >= size) return false;
    if (!xbm_readLine(data, size, &pos, &line, &length, &consumed) ||
        length >= 299u || !xbm_parseDefine(line, length, &height))
        return false;
    totalRead += consumed;
    if (totalRead >= 4096u || width <= 0 || width > 32767 ||
        height <= 0 || height > 32767)
        return false;
    header->m_width = width;
    header->m_height = height;
    header->m_bodyOffset = pos;
    return true;
}

static bool xbm_findToken(const uint8_t* data, size_t size, size_t start,
                          size_t* token)
{
    size_t i;
    if (!data || !token || start >= size) return false;
    for (i = start; i + 1u < size; ++i) {
        if (data[i] == (uint8_t)'0' && data[i + 1u] == (uint8_t)'x') {
            *token = i;
            return true;
        }
    }
    return false;
}

static uint8_t xbm_gray(uint32_t color)
{
    unsigned red = (color >> 16) & 0xffu;
    unsigned green = (color >> 8) & 0xffu;
    unsigned blue = color & 0xffu;
    return (uint8_t)((red * 11u + green * 16u + blue * 5u) >> 5);
}

bool XImageCodecInternal_probeXbmSize(const uint8_t* data, size_t size,
                                      int* width, int* height)
{
    XImageCodecXbmHeader header;
    if (!width || !height || !xbm_parseHeader(data, size, &header)) return false;
    *width = header.m_width;
    *height = header.m_height;
    return true;
}

bool XImageCodecInternal_decodeXbm(const uint8_t* data, size_t size, XImage* out)
{
    XImageCodecXbmHeader header;
    XImage image;
    size_t token;
    size_t cursor;
    size_t rowBytes;
    size_t expected;
    size_t written = 0;
    if (!out || !xbm_parseHeader(data, size, &header) ||
        !xbm_findToken(data, size, header.m_bodyOffset, &token))
        return false;
    rowBytes = ((size_t)header.m_width + 7u) / 8u;
    if (rowBytes > SIZE_MAX / (size_t)header.m_height)
        return false;
    expected = rowBytes * (size_t)header.m_height;
    XImage_init_ex(&image, header.m_width, header.m_height, XImageFormat_MonoLSB);
    if (XImage_isNull(&image)) {
        XImage_deinit_base(&image);
        return false;
    }
    XImage_fill(&image, 0u);
    XImage_setColorCount(&image, 2);
    XImage_setColor(&image, 0, 0xffffffffu);
    XImage_setColor(&image, 1, 0xff000000u);

    /* qxbmhandler.cpp:121-145 scans 0x tokens and intentionally accepts a
       truncated tail; invalid two-digit tokens remain hard errors. */
    cursor = token;
    while (written < expected && cursor < size) {
        size_t current;
        if (!xbm_findToken(data, size, cursor, &current)) break;
        if (current + 3u >= size) break;
        {
            int high = xbm_hex(data[current + 2u]);
            int low = xbm_hex(data[current + 3u]);
            if (high < 0 || low < 0) {
                XImage_deinit_base(&image);
                return false;
            }
            {
                int y = (int)(written / rowBytes);
                size_t x = written % rowBytes;
                uint8_t* line = XImage_scanLine(&image, y);
                if (!line) {
                    XImage_deinit_base(&image);
                    return false;
                }
                line[x] = (uint8_t)((high << 4) | low);
            }
            ++written;
            cursor = current + 4u;
        }
    }
    XImage_move_base(out, &image);
    return true;
}

static void xbm_makeName(const char* name, char* out, size_t capacity)
{
    const char* begin;
    const char* dot;
    size_t used = 0;
    if (!out || capacity < 2u) return;
    out[0] = '\0';
    begin = name && name[0] ? name : "image";
    {
        const char* slash = strrchr(begin, '/');
        const char* backslash = strrchr(begin, '\\');
        if (backslash && (!slash || backslash > slash)) slash = backslash;
        if (slash) begin = slash + 1;
    }
    dot = strrchr(begin, '.');
    while (*begin && begin != dot && used + 1u < capacity) {
        unsigned char c = (unsigned char)*begin++;
        if ((c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
            (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
            (c >= (unsigned char)'0' && c <= (unsigned char)'9') || c == '_')
            out[used++] = (char)c;
        else
            out[used++] = '_';
    }
    if (!used) out[used++] = 'i';
    if (out[0] >= '0' && out[0] <= '9' && used + 1u < capacity) {
        memmove(out + 1, out, used);
        out[0] = '_';
        ++used;
    }
    out[used] = '\0';
}

bool XImageCodecInternal_encodeXbmNamed(const XImage* image, const char* name,
                                        XByteArray* out)
{
    XImage mono;
    char identifier[128];
    char header[256];
    char token[32];
    int width;
    int height;
    int rowBytes;
    int y;
    int i;
    int count = 0;
    bool invert;
    if (!image || !out || XImage_isNull(image)) return false;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0 || width > 32767 || height > 32767)
        return false;
    XImage_init(&mono);
    XImage_convertToFormat(image, XImageFormat_MonoLSB, 0u, &mono);
    if (XImage_isNull(&mono)) return false;
    xbm_makeName(name, identifier, sizeof(identifier));
    invert = xbm_gray(XImage_color(&mono, 0)) < xbm_gray(XImage_color(&mono, 1));
    if (snprintf(header, sizeof(header),
                 "#define %s_width %d\n#define %s_height %d\n"
                 "static char %s_bits[] = {\n ",
                 identifier, width, identifier, height, identifier) <= 0 ||
        !XImageCodecInternal_appendBytes(out, header, strlen(header))) {
        XImage_deinit_base(&mono);
        return false;
    }
    rowBytes = (width + 7) / 8;
    for (y = 0; y < height; ++y) {
        const uint8_t* line = XImage_constScanLine(&mono, y);
        if (!line) {
            XImage_deinit_base(&mono);
            return false;
        }
        for (i = 0; i < rowBytes; ++i) {
            uint8_t value = line[i];
            bool last = y == height - 1 && i == rowBytes - 1;
            if (invert) value = (uint8_t)~value;
            if (snprintf(token, sizeof(token), "0x%02x%s", value, last ? "" : ",") <= 0 ||
                !XImageCodecInternal_appendBytes(out, token, strlen(token))) {
                XImage_deinit_base(&mono);
                return false;
            }
            if (!last && ++count == 15) {
                if (!XImageCodecInternal_appendBytes(out, "\n ", 2u)) {
                    XImage_deinit_base(&mono);
                    return false;
                }
                count = 0;
            }
        }
    }
    if (!XImageCodecInternal_appendBytes(out, " };\n", 4u)) {
        XImage_deinit_base(&mono);
        return false;
    }
    XImage_deinit_base(&mono);
    return true;
}

#endif /* XIMAGECODEC_ON && XIMAGECODEC_XBM_ON */
