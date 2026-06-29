/**
 * @file XChar_file.c
 * @brief GBK编码转换的文件读取实现（二进制格式）
 *
 * 通过读取 XCHAR_COMPACT.BIN 文件实现 GBK↔Unicode 转换。
 * 适用于嵌入式平台（如 STM32 + FatFs），无需系统 API。
 *
 * 二进制格式（XCHAR_COMPACT.BIN）- 由 XChar_generateCodeTable() 生成：
 *   无头部，直接是二进制数据：
 *   GBK 表：N × 4 字节（GBK高、GBK低、Unicode高、Unicode低，按 GBK 排序）
 *   Unicode 表：N × 4 字节（Unicode高、Unicode低、GBK高、GBK低，按 Unicode 排序）
 *
 * 查找方式：纯文件二分，每项4字节。
 * 只需一个 XFile 单例，内存开销为零。
 *
 * 三种模式优先级（从高到低）：
 *   1. XCHAR_USE_CODE_GBK   - 代码模式（静态数组）
 *   2. XCHAR_USE_FILE_GBK   - 文件模式（读取外部文件）
 *   3. XCHAR_USE_SYSTEM_GBK - 系统API模式（调用系统API）
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

#ifndef XCHAR_COMPACT_PATH
#define XCHAR_COMPACT_PATH "XCHAR_COMPACT.BIN"
#endif

/** 二进制格式每项字节数 */
#define BINARY_ENTRY_SIZE  4

/* ========================================================================== */
/*                        全局状态                                             */
/* ========================================================================== */

typedef struct {
    XFile* file;            /**< 文件单例 */
    bool file_opened;       /**< 是否已打开 */
    int64_t gbk_offset;     /**< GBK表起始偏移 */
    int64_t uni_offset;     /**< Unicode表起始偏移 */
    uint32_t entry_count;   /**< 条目数量 */
} XChar_FileGlobal;

static XChar_FileGlobal s_files = { NULL, false, 0, 0, 0 };

/* ========================================================================== */
/*                        内部辅助函数                                         */
/* ========================================================================== */

/**
 * @brief 打开二进制格式文件
 * @return 成功返回文件指针，失败返回NULL
 */
static XFile* open_binary_file(void)
{
    XString* xpath = XString_create_utf8(XCHAR_COMPACT_PATH);
    if (!xpath) return NULL;

    s_files.file = XFile_create_1();
    if (!s_files.file) { XString_delete_base(xpath); return NULL; }

    XFile_setFileName(s_files.file, xpath);
    XString_delete_base(xpath);

    if (!XFile_open_2(s_files.file, XIODevice_ReadOnly, 0)) {
        XFile_deleteLater(s_files.file); s_files.file = NULL;
        return NULL;
    }

    /* 计算文件大小来确定条目数 */
    int64_t file_size = XIODevice_size_base((XIODevice*)s_files.file);
    if (file_size <= 0) {
        XFile_deleteLater(s_files.file); s_files.file = NULL;
        return NULL;
    }

    /* 二进制格式：两个表，每项4字节 */
    uint32_t total_entries = (uint32_t)(file_size / (BINARY_ENTRY_SIZE * 2));
    if (total_entries == 0) {
        XFile_deleteLater(s_files.file); s_files.file = NULL;
        return NULL;
    }

    s_files.entry_count = total_entries;
    s_files.gbk_offset = 0;  /* GBK表在文件开头 */
    s_files.uni_offset = (int64_t)total_entries * BINARY_ENTRY_SIZE;  /* Unicode表紧随其后 */
    s_files.file_opened = true;

    return s_files.file;
}

/**
 * @brief 获取文件单例
 */
static XFile* get_table_file(void)
{
    if (s_files.file_opened && s_files.file) return s_files.file;
    return open_binary_file();
}

/**
 * @brief 在二进制格式表中二分查找
 */
static bool binary_search_table(XFile* file, int64_t table_offset, uint16_t key, uint16_t* out_val)
{
    int lo = 0, hi = (int)s_files.entry_count - 1;
    uint8_t buf[4];

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int64_t offset = table_offset + (int64_t)mid * BINARY_ENTRY_SIZE;

        XIODevice_seek_base((XIODevice*)file, offset);
        int64_t n = XIODevice_read_1((XIODevice*)file, buf, BINARY_ENTRY_SIZE);
        if (n < BINARY_ENTRY_SIZE) break;

        /* 大端序读取键和值 */
        uint16_t file_key = ((uint16_t)buf[0] << 8) | buf[1];
        uint16_t file_val = ((uint16_t)buf[2] << 8) | buf[3];

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