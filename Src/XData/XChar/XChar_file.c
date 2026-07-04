/**
 * @file XChar_file.c
 * @brief GBK编码转换的文件读取实现（二进制格式）
 *
 * 通过读取 XCHAR_COMPACT.BIN 文件实现 GBK↔Unicode 转换。
 * 适用于嵌入式平台（如 STM32 + FatFs），无需系统 API。
 *
 * 二进制格式（XCHAR_COMPACT.BIN）-
 *   无头部，直接是二进制数据：
 *   GBK 表：N × 4 字节（GBK高、GBK低、Unicode高、Unicode低，按 GBK 排序）
 *   Unicode 表：N × 4 字节（Unicode高、Unicode低、GBK高、GBK低，按 Unicode 排序）
 *
 * 查找方式：纯文件二分，每项4字节。
 *
 * 线程安全：根据 XCHAR_FILE_THREAD_SAFE 配置启用/禁用
 *
 * 配置文件：XChar_conf.h
 */
#include "XChar_conf.h"
#if defined(XCHAR_USE_FILE_GBK) && !defined(XCHAR_USE_CODE_GBK) && !defined(XCHAR_USE_SYSTEM_GBK)
#include "XChar.h"
#include "XMemory.h"
#include "XFile.h"
#include "XString.h"
#include <string.h>

#if XCHAR_FILE_THREAD_SAFE
#include "XHashMap.h"
#include "XReadWriteLock.h"
#include "XThread.h"
#endif

/* ========================================================================== */
/*                        配置宏                                              */
/* ========================================================================== */

/** 二进制格式每项字节数 */
#define BINARY_ENTRY_SIZE  4

/* ========================================================================== */
/*                        线程局部存储结构                                     */
/* ========================================================================== */

#if XCHAR_FILE_THREAD_SAFE

/**
 * @brief 每个线程的文件句柄信息
 */
typedef struct {
    XFile* file;                    /**< 文件句柄（每个线程独立） */
    uint32_t entry_count;           /**< 条目数量（所有线程共享，首次打开时设置） */
    int64_t gbk_offset;             /**< GBK表起始偏移 */
    int64_t uni_offset;             /**< Unicode表起始偏移 */
    uint8_t initialized;            /**< 是否已初始化 */
} XChar_ThreadFile;

/* ========================================================================== */
/*                        全局状态（线程安全模式）                              */
/* ========================================================================== */

/** 全局共享的条目数量（首次打开后设置，所有线程共享） */
static uint32_t g_entry_count = 0;
static int64_t g_gbk_offset = 0;
static int64_t g_uni_offset = 0;
static uint8_t g_global_initialized = 0;

/** 线程局部存储映射：线程ID -> XChar_ThreadFile* */
static XHashMap* g_thread_map = NULL;
static XReadWriteLock* g_lock = NULL;

#else /* !XCHAR_FILE_THREAD_SAFE */

/* ========================================================================== */
/*                        全局状态（单线程模式）                                */
/* ========================================================================== */

typedef struct {
    XFile* file;                    /**< 文件单例 */
    uint32_t file_opened : 1;       /**< 是否已打开 */
    uint32_t entry_count : 31;      /**< 条目数量 */
    int64_t gbk_offset;             /**< GBK表起始偏移 */
    int64_t uni_offset;             /**< Unicode表起始偏移 */
} XChar_FileGlobal;

static XChar_FileGlobal s_files = { NULL, false, 0, 0, 0 };

#endif /* XCHAR_FILE_THREAD_SAFE */

/* ========================================================================== */
/*                        线程安全模式实现                                      */
/* ========================================================================== */

#if XCHAR_FILE_THREAD_SAFE

/**
 * @brief 初始化全局状态
 */
static void init_global_state(void)
{
    if (g_lock == NULL)
    {
        g_lock = XReadWriteLock_create(XLock_NonRecursive);
    }
    if (g_thread_map == NULL)
    {
        g_thread_map = XHashMap_Create(XHandle, XChar_ThreadFile*, size_t_compare);
    }
}

/**
 * @brief 获取当前线程的文件句柄（线程安全）
 * @return 成功返回文件句柄，失败返回NULL
 */
static XChar_ThreadFile* get_thread_file(void)
{
    /* 初始化全局状态 */
    init_global_state();
    
    XHandle thread_id = XThread_currentThreadId();
    
    /* 先用读锁查找 */
    XReadWriteLock_lockForRead(g_lock);
    XChar_ThreadFile** lptr = XHashMap_value_base(g_thread_map, &thread_id);
    XChar_ThreadFile* tf = lptr ? *lptr : NULL;
    XReadWriteLock_unlock(g_lock);
    
    if (tf && tf->initialized)
    {
        return tf;
    }
    
    /* 需要创建新的线程文件句柄 */
    XReadWriteLock_lockForWrite(g_lock);
    
    /* 双重检查 */
    lptr = XHashMap_value_base(g_thread_map, &thread_id);
    tf = lptr ? *lptr : NULL;
    
    if (tf == NULL)
    {
        /* 创建新的线程文件结构 */
        tf = (XChar_ThreadFile*)XMalloc_System(sizeof(XChar_ThreadFile));
        if (tf)
        {
            memset(tf, 0, sizeof(XChar_ThreadFile));
            XMapBase_insert_base(g_thread_map, &thread_id, &tf);
        }
    }
    
    XReadWriteLock_unlock(g_lock);
    
    return tf;
}

/**
 * @brief 打开文件并初始化线程局部数据
 * @param tf 线程文件结构
 * @return 成功返回true
 */
static bool open_file_for_thread(XChar_ThreadFile* tf)
{
    if (tf->initialized && tf->file)
    {
        return true;
    }
    
    XString* xpath = XString_create_utf8(XCHAR_COMPACT_PATH);
    if (!xpath) return false;
    
    tf->file = XFile_create();
    if (!tf->file)
    {
        XString_delete_base(xpath);
        return false;
    }
    
    XFile_setFileName(tf->file, xpath);
    XString_delete_base(xpath);
    
    if (!XFile_open_2(tf->file, XIODevice_ReadOnly, 0))
    {
        XFile_deleteLater(tf->file);
        tf->file = NULL;
        return false;
    }
    
    /* 计算文件大小来确定条目数 */
    int64_t file_size = XIODevice_size_base((XIODevice*)tf->file);
    if (file_size <= 0)
    {
        XFile_deleteLater(tf->file);
        tf->file = NULL;
        return false;
    }
    
    /* 二进制格式：两个表，每项4字节 */
    uint32_t total_entries = (uint32_t)(file_size / (BINARY_ENTRY_SIZE * 2));
    if (total_entries == 0)
    {
        XFile_deleteLater(tf->file);
        tf->file = NULL;
        return false;
    }
    
    /* 设置全局偏移（所有线程共享） */
    if (!g_global_initialized)
    {
        g_entry_count = total_entries;
        g_gbk_offset = 0;  /* GBK表在文件开头 */
        g_uni_offset = (int64_t)total_entries * BINARY_ENTRY_SIZE;  /* Unicode表紧随其后 */
        g_global_initialized = 1;
    }
    
    tf->entry_count = g_entry_count;
    tf->gbk_offset = g_gbk_offset;
    tf->uni_offset = g_uni_offset;
    tf->initialized = 1;
    
    return true;
}

/**
 * @brief 获取已打开的文件（确保文件已打开）
 * @return 成功返回XFile指针，失败返回NULL
 */
static XFile* get_opened_file(void)
{
    XChar_ThreadFile* tf = get_thread_file();
    if (!tf) return NULL;
    
    if (tf->initialized && tf->file)
    {
        return tf->file;
    }
    
    if (open_file_for_thread(tf))
    {
        return tf->file;
    }
    
    return NULL;
}

#else /* !XCHAR_FILE_THREAD_SAFE */

/* ========================================================================== */
/*                        单线程模式实现                                        */
/* ========================================================================== */

/**
 * @brief 打开二进制格式文件
 * @return 成功返回文件指针，失败返回NULL
 */
static XFile* open_binary_file(void)
{
    XString* xpath = XString_create_utf8(XCHAR_COMPACT_PATH);
    if (!xpath) return NULL;

    s_files.file = XFile_create();
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
static XFile* get_opened_file(void)
{
    if (s_files.file_opened && s_files.file) return s_files.file;
    return open_binary_file();
}

#endif /* XCHAR_FILE_THREAD_SAFE */

/* ========================================================================== */
/*                        内部辅助函数                                         */
/* ========================================================================== */

/**
 * @brief 在二进制格式表中二分查找
 */
static bool binary_search_table(XFile* file, int64_t table_offset, uint16_t key, uint16_t* out_val)
{
#if XCHAR_FILE_THREAD_SAFE
    int lo = 0, hi = (int)g_entry_count - 1;
#else
    int lo = 0, hi = (int)s_files.entry_count - 1;
#endif
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

    XFile* file = get_opened_file();
    if (!file) return false;

#if XCHAR_FILE_THREAD_SAFE
    return binary_search_table(file, g_gbk_offset, gbk_code, unicode);
#else
    return binary_search_table(file, s_files.gbk_offset, gbk_code, unicode);
#endif
}

static bool file_lookup_by_unicode(uint16_t unicode_code, uint16_t* gbk)
{
    if (unicode_code <= 0x7F) {
        if (gbk) *gbk = unicode_code;
        return true;
    }

    XFile* file = get_opened_file();
    if (!file) return false;

#if XCHAR_FILE_THREAD_SAFE
    return binary_search_table(file, g_uni_offset, unicode_code, gbk);
#else
    return binary_search_table(file, s_files.uni_offset, unicode_code, gbk);
#endif
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

/* ========================================================================== */
/*                   清理函数                                                  */
/* ========================================================================== */

#if XCHAR_FILE_THREAD_SAFE

/**
 * @brief 清理当前线程的文件句柄
 * @note 线程退出前可调用此函数释放资源
 */
void XCharPlatform_cleanupThread(void)
{
    if (!g_lock || !g_thread_map) return;
    
    XHandle thread_id = XThread_currentThreadId();
    
    XReadWriteLock_lockForWrite(g_lock);
    XChar_ThreadFile** lptr = XHashMap_value_base(g_thread_map, &thread_id);
    XChar_ThreadFile* tf = lptr ? *lptr : NULL;
    
    if (tf)
    {
        if (tf->file)
        {
            XFile_deleteLater(tf->file);
            tf->file = NULL;
        }
        XFree_System(tf);
        XMapBase_remove_base(g_thread_map, &thread_id);
    }
    
    XReadWriteLock_unlock(g_lock);
}

/**
 * @brief 清理所有线程的文件句柄
 * @note 程序退出前可调用此函数释放所有资源
 */
void XCharPlatform_cleanupAll(void)
{
    if (!g_lock || !g_thread_map) return;
    
    XReadWriteLock_lockForWrite(g_lock);
    
    /* 遍历所有线程文件并清理 */
    for_each_iterator(g_thread_map, XHashMap, it)
    {
        XPair* pair = XHashMap_iterator_data(&it);
        if (pair)
        {
            XChar_ThreadFile* tf = *(XChar_ThreadFile**)XPair_second(pair);
            if (tf)
            {
                if (tf->file)
                {
                    XFile_deleteLater(tf->file);
                }
                XFree_System(tf);
            }
        }
    }
    
    XMapBase_clear_base(g_thread_map);
    XHashMap_delete_base(g_thread_map);
    g_thread_map = NULL;
    
    XReadWriteLock_unlock(g_lock);
    XReadWriteLock_delete(g_lock);
    g_lock = NULL;
    
    g_global_initialized = 0;
    g_entry_count = 0;
}

#else /* !XCHAR_FILE_THREAD_SAFE */

/**
 * @brief 清理文件句柄
 * @note 程序退出前可调用此函数释放资源
 */
void XCharPlatform_cleanup(void)
{
    if (s_files.file)
    {
        XFile_deleteLater(s_files.file);
        s_files.file = NULL;
    }
    s_files.file_opened = false;
    s_files.entry_count = 0;
}

#endif /* XCHAR_FILE_THREAD_SAFE */

#endif /* XCHAR_USE_FILE_GBK */