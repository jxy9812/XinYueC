/**
 * @file XChar_file.c
 * @brief GBK编码转换的文件读写实现（公共平台）
 *
 * 通过读取 XCHAR.TXT 合并映射文件实现 GBK↔Unicode 转换。
 * 适用于嵌入式平台（如 STM32 + FatFs），无需系统 API。
 *
 * 文件格式（二进制写入）：
 *   头部 32 字节：
 *     "GBKTBL:XXXXXXXX\n"  (16字节，GBK表起始偏移，十六进制)
 *     "UNITBL:XXXXXXXX\n"  (16字节，Unicode表起始偏移，十六进制)
 *   GBK 表：21920 × 14 字节（按 GBK 排序，"0xGBK\t0xUnicode\n"）
 *   Unicode 表：21920 × 14 字节（按 Unicode 排序，"0xUnicode\t0xGBK\n"）
 *
 * 查找方式：纯文件二分，每行14字节可直接计算文件偏移。
 * 只需一个 XFile 单例，内存开销为零。
 *
 * 使用方式：在编译配置中定义 XCHAR_USE_FILE_GBK 宏即可启用此实现。
 */

#ifdef XCHAR_USE_FILE_GBK

#include "XChar.h"
#include "XMemory.h"
#include "XFile.h"
#include "XString.h"
#include <string.h>

/* ========================================================================== */
/*                        配置宏                                              */
/* ========================================================================== */

#ifndef XCHAR_TABLE_PATH
#define XCHAR_TABLE_PATH "XCHAR.TXT"
#endif

/** 头部：两行 × 16字节 = 32字节 */
#define HEADER_BYTES    32

/** 数据行宽："0xNNNN\t0xNNNN\n" = 14字节（二进制写入无 \r） */
#define LINE_BYTES      14

/** 总条目数（每表 21920） */
#define TABLE_ENTRY_COUNT  21920

/* ========================================================================== */
/*                        全局状态                                             */
/* ========================================================================== */

typedef struct {
    XFile* file;            /**< 文件单例 */
    bool file_opened;       /**< 是否已打开 */
    int64_t gbk_offset;     /**< GBK表起始偏移 */
    int64_t uni_offset;     /**< Unicode表起始偏移 */
    int line_bytes;         /**< 检测到的行宽（14或15，兼容 \r\n） */
} XChar_FileGlobal;

static XChar_FileGlobal s_files = { NULL, false, 0, 0, 0 };

/* ========================================================================== */
/*                        内部辅助函数                                         */
/* ========================================================================== */

/**
 * @brief 打开映射文件单例并解析头部
 */
static XFile* get_table_file(void)
{
    if (s_files.file_opened && s_files.file) return s_files.file;

    s_files.file = XFile_create_1();
    if (!s_files.file) return NULL;

    XString* xpath = XString_create_utf8(XCHAR_TABLE_PATH);
    if (!xpath) { XFile_deleteLater(s_files.file); s_files.file = NULL; return NULL; }

    XFile_setFileName(s_files.file, xpath);
    XString_delete_base(xpath);

    /* 二进制模式，确保 seek/position 一致 */
    if (!XFile_open_2(s_files.file, XIODevice_ReadOnly, 0)) {
        XFile_deleteLater(s_files.file); s_files.file = NULL; return NULL;
    }

    s_files.file_opened = true;

    /* 解析头部：读取前32字节 */
    char hdr[36];
    XIODevice_seek_base((XIODevice*)s_files.file, 0);
    int64_t n = XIODevice_read_1((XIODevice*)s_files.file, hdr, HEADER_BYTES);
    if (n < HEADER_BYTES) {
        XFile_deleteLater(s_files.file); s_files.file = NULL;
        s_files.file_opened = false; return NULL;
    }

    /* 解析 "GBKTBL:XXXXXXXX\n" (G=0,B=1,K=2,T=3,B=4,L=5,:=6,hex=7..14,\n=15) */
    unsigned int gbk_off = 0, uni_off = 0;
    if (hdr[6] == ':') {
        const char* p = hdr + 7;
        while (*p && *p != '\n' && *p != '\r') {
            char c = *p++;
            unsigned int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else break;
            gbk_off = (gbk_off << 4) | d;
        }
    }
    /* 解析 "UNITBL:XXXXXXXX\n" (U=0,N=1,I=2,T=3,B=4,L=5,:=6,hex=7..14,\n=15) */
    if (hdr[22] == ':') {
        const char* p = hdr + 23;
        while (*p && *p != '\n' && *p != '\r') {
            char c = *p++;
            unsigned int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else break;
            uni_off = (uni_off << 4) | d;
        }
    }

    s_files.gbk_offset = (int64_t)gbk_off;
    s_files.uni_offset = (int64_t)uni_off;

    /* 检测行宽：读取第一个数据行的前15字节 */
    XIODevice_seek_base((XIODevice*)s_files.file, s_files.gbk_offset);
    {
        char probe[16];
        int64_t pn = XIODevice_read_1((XIODevice*)s_files.file, probe, 15);
        s_files.line_bytes = LINE_BYTES; /* 默认14 */
        {
            int i;
            for (i = 0; i < (int)pn; i++) {
                if (probe[i] == '\n') { s_files.line_bytes = i + 1; break; }
            }
        }
    }

    return s_files.file;
}

/**
 * @brief 快速解析十六进制值
 */
static bool parse_hex_pair(const char* buf, uint16_t* first, uint16_t* second)
{
    unsigned int v1 = 0, v2 = 0;
    const char* p;

    p = buf + 2; /* 跳过 "0x" */
    while (*p && *p != '\t') {
        char c = *p++;
        unsigned int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else break;
        v1 = (v1 << 4) | d;
    }

    p++; /* 跳过 \t */
    p += 2; /* 跳过 "0x" */
    while (*p && *p != '\n' && *p != '\r') {
        char c = *p++;
        unsigned int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else break;
        v2 = (v2 << 4) | d;
    }

    *first = (uint16_t)v1;
    *second = (uint16_t)v2;
    return true;
}

/**
 * @brief 在指定表中二分查找
 * @param table_offset 表的起始文件偏移
 * @param key 要查找的键值
 * @param out_val 输出对应值
 */
static bool binary_search_table(XFile* file, int64_t table_offset, uint16_t key, uint16_t* out_val)
{
    int lo = 0, hi = TABLE_ENTRY_COUNT - 1;
    char buf[16];

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int64_t offset = table_offset + (int64_t)mid * s_files.line_bytes;

        XIODevice_seek_base((XIODevice*)file, offset);
        int64_t n = XIODevice_read_1((XIODevice*)file, buf, s_files.line_bytes);
        if (n < LINE_BYTES) break;

        uint16_t file_key = 0, file_val = 0;
        parse_hex_pair(buf, &file_key, &file_val);

        if (file_key == key) {
            if (out_val) *out_val = file_val;
            return true;
        } else if (file_key < key) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return false;
}

/* ========================================================================== */
/*                   平台抽象函数实现                                          */
/* ========================================================================== */

static bool file_lookup_by_gbk(uint16_t gbk_code, uint16_t* unicode)
{
    if (gbk_code <= 0x7F) {
        if (unicode) *unicode = gbk_code;
        return true;
    }

    XFile* file = get_table_file();
    if (!file) return false;

    return binary_search_table(file, s_files.gbk_offset, gbk_code, unicode);
}

static bool file_lookup_by_unicode(uint16_t unicode_code, uint16_t* gbk)
{
    if (unicode_code <= 0x7F) {
        if (gbk) *gbk = unicode_code;
        return true;
    }

    XFile* file = get_table_file();
    if (!file) return false;

    return binary_search_table(file, s_files.uni_offset, unicode_code, gbk);
}

/* ========================================================================== */
/*                   辅助函数                                                  */
/* ========================================================================== */

static size_t xchar_input_len_char(const char* data, size_t input_size)
{
    if (input_size == 0) {
        size_t len = 0;
        while (data[len] != '\0') len++;
        return len;
    }
    {
        size_t i;
        for (i = 0; i < input_size; i++) {
            if (data[i] == '\0') return i;
        }
    }
    return input_size;
}

static size_t xchar_input_len(const XChar* data, size_t input_count)
{
    if (input_count == 0) {
        size_t len = 0;
        while (data[len] != 0) len++;
        return len;
    }
    {
        size_t i;
        for (i = 0; i < input_count; i++) {
            if (data[i] == 0) return i;
        }
    }
    return input_count;
}

/* ========================================================================== */
/*                   平台抽象函数（对外接口）                                    */
/* ========================================================================== */

int64_t XCharPlatform_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out)
{
    size_t actual_len, i;
    if (!gbk) return -1;
    actual_len = xchar_input_len_char(gbk, input_size);
    if (actual_len == 0) { if (out && max_out > 0) out[0] = 0; return 0; }

    if (!out || max_out == 0) {
        size_t count = 0;
        for (i = 0; i < actual_len; ) {
            uint16_t gbk_code, unicode;
            if ((uint8_t)gbk[i] >= 0x81 && (uint8_t)gbk[i] <= 0xFE &&
                i + 1 < actual_len &&
                (uint8_t)gbk[i + 1] >= 0x40 && (uint8_t)gbk[i + 1] <= 0xFE) {
                gbk_code = (uint16_t)(((uint8_t)gbk[i] << 8) | (uint8_t)gbk[i + 1]);
                i += 2;
            } else {
                gbk_code = (uint8_t)gbk[i];
                i++;
            }
            if (file_lookup_by_gbk(gbk_code, &unicode)) count++;
        }
        return (int64_t)count;
    }

    {
        size_t out_idx = 0;
        for (i = 0; i < actual_len && out_idx < max_out - 1; ) {
            uint16_t gbk_code, unicode;
            if ((uint8_t)gbk[i] >= 0x81 && (uint8_t)gbk[i] <= 0xFE &&
                i + 1 < actual_len &&
                (uint8_t)gbk[i + 1] >= 0x40 && (uint8_t)gbk[i + 1] <= 0xFE) {
                gbk_code = (uint16_t)(((uint8_t)gbk[i] << 8) | (uint8_t)gbk[i + 1]);
                i += 2;
            } else {
                gbk_code = (uint8_t)gbk[i];
                i++;
            }
            if (file_lookup_by_gbk(gbk_code, &unicode))
                out[out_idx++] = unicode;
            else
                out[out_idx++] = 0xFFFD;
        }
        out[out_idx] = 0;
        return (int64_t)out_idx;
    }
}

int64_t XCharPlatform_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk)
{
    size_t actual_count, i;
    if (!ch) return -1;
    actual_count = xchar_input_len(ch, input_count);
    if (actual_count == 0) { if (gbk && max_gbk > 0) gbk[0] = '\0'; return 0; }

    if (!gbk || max_gbk == 0) {
        size_t byte_count = 0;
        for (i = 0; i < actual_count; i++) {
            uint16_t gbk_code;
            if (ch[i] <= 0x7F) byte_count += 1;
            else if (file_lookup_by_unicode(ch[i], &gbk_code))
                byte_count += (gbk_code > 0xFF) ? 2 : 1;
        }
        return (int64_t)byte_count;
    }

    {
        size_t gbk_idx = 0;
        for (i = 0; i < actual_count; i++) {
            uint16_t gbk_code;
            if (ch[i] <= 0x7F) {
                if (gbk_idx < max_gbk - 1) gbk[gbk_idx++] = (char)ch[i];
            } else if (file_lookup_by_unicode(ch[i], &gbk_code)) {
                if (gbk_code > 0xFF) {
                    if (gbk_idx + 2 < max_gbk) {
                        gbk[gbk_idx++] = (char)((gbk_code >> 8) & 0xFF);
                        gbk[gbk_idx++] = (char)(gbk_code & 0xFF);
                    }
                } else {
                    if (gbk_idx < max_gbk - 1) gbk[gbk_idx++] = (char)gbk_code;
                }
            }
        }
        gbk[gbk_idx] = '\0';
        return (int64_t)gbk_idx;
    }
}

int64_t XCharPlatform_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len)
{
    size_t utf8_len; int64_t xchar_count, actual, result;
    XChar* xchars;
    if (!utf8_str) return -1;
    utf8_len = xchar_input_len_char(utf8_str, input_size);

    xchar_count = XChar_fromUtf8Stream((const uint8_t*)utf8_str, utf8_len, NULL, 0);
    if (xchar_count <= 0) return xchar_count;

    xchars = (XChar*)XMalloc_System((size_t)(xchar_count + 1) * sizeof(XChar));
    if (!xchars) return -1;

    actual = XChar_fromUtf8Stream((const uint8_t*)utf8_str, utf8_len, xchars, (size_t)xchar_count + 1);
    if (actual <= 0) { XFree_System(xchars); return -1; }
    xchars[actual] = 0;

    result = XCharPlatform_toGbkStream(xchars, (size_t)actual, gbk_buf, max_len);
    XFree_System(xchars);
    return result;
}

int64_t XCharPlatform_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len)
{
    size_t gbk_len; int64_t xchar_count, actual, utf8_len, result;
    XChar* xchars;
    if (!gbk_str) return -1;
    gbk_len = xchar_input_len_char(gbk_str, input_size);

    xchar_count = XCharPlatform_fromGbkStream(gbk_str, gbk_len, NULL, 0);
    if (xchar_count <= 0) return xchar_count;

    xchars = (XChar*)XMalloc_System((size_t)(xchar_count + 1) * sizeof(XChar));
    if (!xchars) return -1;

    actual = XCharPlatform_fromGbkStream(gbk_str, gbk_len, xchars, (size_t)xchar_count + 1);
    if (actual <= 0) { XFree_System(xchars); return -1; }
    xchars[actual] = 0;

    utf8_len = XChar_toUtf8Stream(xchars, (size_t)actual, NULL, 0);
    if (utf8_len <= 0) { XFree_System(xchars); return utf8_len; }
    if (!utf8_buf || max_len == 0) { XFree_System(xchars); return utf8_len; }
    if (max_len < (size_t)utf8_len + 1) { XFree_System(xchars); return -1; }

    result = XChar_toUtf8Stream(xchars, (size_t)actual, (uint8_t*)utf8_buf, max_len);
    XFree_System(xchars);
    return result;
}

#endif /* XCHAR_USE_FILE_GBK */