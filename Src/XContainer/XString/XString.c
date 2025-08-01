#include "XString.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "XStringList.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// 内部常量定义
#define UTF8_CACHE_SIZE 1024  // 初始UTF-8缓存大小
#define XSTRING_MIN_CAPACITY 16  // 最小容量（不含结束符）
#define XString_cdata(str) ((const XChar*)XContainerDataPtr(str))
#define XString_copy        XString_create

// 获取可修改的内部XChar数组
XChar* XString_data(XString* str);

//初始化缓存
static void XString_initCache(XString* str);

XString* XString_create(const XString* other)
{
    if (!other) return NULL;

    XString* str = (XString*)XMemory_malloc(sizeof(XString));
    if (!str) return NULL;

    XString_copy_base(str, other);
    return str;
}

// 字符串创建函数
XString* XString_create_utf8(const char* utf8_str) 
{
    return XString_create_with_length_utf8(utf8_str, utf8_str ? strlen(utf8_str) : 0);
}

XString* XString_create_with_length_utf8(const char* utf8_str, size_t len)
{
    XString* str = (XString*)XMemory_malloc(sizeof(XString));
    if (!str) return NULL;
    XString_init(str);
    size_t actual_len = (len == 0 && utf8_str) ? strlen(utf8_str) : len;
    if (utf8_str && actual_len > 0)
    {
        int xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, NULL, 0);
        if (xchar_count > 0) {
            XString_reserve(str, xchar_count);
            xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, XString_data(str), xchar_count + 1);
            str->parent.m_size = xchar_count;
        }
    }
    return str;
}

XString* XString_create_gbk(const char* gbk_str)
{
    if (!gbk_str) return NULL;
    // 计算GBK字符串长度（不含终止符）
    size_t len = 0;
    while (gbk_str[len] != '\0') {
        // GBK字符要么1字节（0x00-0x7F）要么2字节（高字节0x81-0xFE，低字节0x40-0xFE且不等于0x7F）
        if ((uint8_t)gbk_str[len] < 0x80) {
            len++;
        }
        else {
            len += 2; // 跳过双字节字符的第二个字节
        }
    }
    return XString_create_with_length_gbk(gbk_str, len);
}

XString* XString_create_fmt_gbk(const char* format, ...)
{
    if (!format) return NULL;

    // 第一步：使用vsnprintf获取格式化后的GBK字符串长度
    va_list args;
    va_start(args, format);
    int fmt_len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (fmt_len <= 0) return NULL;

    // 第二步：分配缓冲区并格式化GBK字符串
    char* gbk_buf = (char*)XMemory_malloc(fmt_len + 1); // +1 用于终止符
    if (!gbk_buf) return NULL;

    va_start(args, format);
    vsnprintf(gbk_buf, fmt_len + 1, format, args);
    va_end(args);

    // 第三步：通过已实现的函数创建XString
    XString* str = XString_create_gbk(gbk_buf);
    XMemory_free(gbk_buf); // 释放临时缓冲区

    return str;
}

XString* XString_create_with_length_gbk(const char* gbk_str, size_t len)
{
    if (!gbk_str || len == 0) {
        return NULL;
    }

    // 步骤1：创建带空终止符的临时GBK缓冲区（避免原函数越界读取）
    char* temp_gbk = (char*)XMemory_malloc(len + 1); // +1 用于存储'\0'
    if (!temp_gbk) {
        return NULL;
    }
    memcpy(temp_gbk, gbk_str, len); // 复制指定长度的GBK数据
    temp_gbk[len] = '\0'; // 添加空终止符，适配XChar_from_gbk的要求

    // 步骤2：计算转换所需的XChar数量（首次调用获取长度）
    int64_t xchar_count = XChar_from_gbk(temp_gbk, NULL, 0);
    if (xchar_count <= 0) {
        XMemory_free(temp_gbk); // 释放临时缓冲区
        return NULL;
    }

    // 步骤3：创建并初始化XString
    XString* str = (XString*)XMemory_malloc(sizeof(XString));
    if (!str) {
        XMemory_free(temp_gbk);
        return NULL;
    }
    XString_init(str);

    // 步骤4：预留足够空间（包含终止符）
    XString_reserve(str, (size_t)xchar_count);

    // 步骤5：执行实际转换（使用临时缓冲区）
    XChar* data = XString_data(str);
    xchar_count = XChar_from_gbk(temp_gbk, data, (size_t)xchar_count + 1); // +1 预留终止符位置
    XMemory_free(temp_gbk); // 转换完成后释放临时缓冲区

    if (xchar_count <= 0) {
        XString_delete_base(str);
        return NULL;
    }

    // 步骤6：设置长度和终止符
    str->parent.m_size = (size_t)xchar_count;
    data[xchar_count] = (XChar){ 0 };

    XString_deinitCache(str);
    return str;
}

XString* XString_create_fmt_utf8(const char* format, ...) 
{
    if (!format) return NULL;

    va_list args;
    va_start(args, format);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    return XString_create_utf8(buf);
}

// 初始化函数
void XString_init(XString* str)
{
    if (!str) return;
    XContainerObject_init(&str->parent, sizeof(XChar));
    str->m_ref_count = (int*)XMemory_malloc(sizeof(int));
    *str->m_ref_count = 1;
    str->m_is_shared = false;
    str->m_cache = NULL;

    XClassGetVtable((XClass*)str) = XString_class_init();
}

const char* XString_toUtf8(const XString* str)
{
    if (!str) return NULL;

    // 缓存已存在则直接返回
    if (str->m_cache&& str->m_cache[XStringCache_Utf8]) return str->m_cache[XStringCache_Utf8];

    // 计算所需UTF-8缓冲区大小（包含结束符）
    size_t xchar_len = XString_length_base(str);
    size_t utf8_max_len = xchar_len * 4 + 1;  // 每个Unicode最多4字节+终止符
    char* utf8_buf = (char*)XMemory_malloc(utf8_max_len);
    if (!utf8_buf) return NULL;

    // 调用XChar转换函数（使用内部结束符自动处理）
    int64_t result = XChar_to_utf8(XString_cdata(str), (uint8_t*)utf8_buf, utf8_max_len);
    if (result <= 0) 
    {
        XMemory_free(utf8_buf);
        return NULL;  // 转换失败
    }
    XString_initCache(str);
    // 缓存结果（内部保证线程安全）
    str->m_cache[XStringCache_Utf8] = utf8_buf;
    return utf8_buf;
}

const uint16_t* XString_toUtf16(const XString* str)
{
    if (!str) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Utf16]) {
        return (const uint16_t*)str->m_cache[XStringCache_Utf16];
    }

    // 计算UTF-16所需缓冲区大小（包含终止符）
    size_t xchar_len = XString_length_base(str);
    int64_t utf16_len = XChar_to_utf16(XString_cdata(str), NULL, 0);
    if (utf16_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf16_len * sizeof(uint16_t) + sizeof(uint16_t);
    uint16_t* utf16_buf = (uint16_t*)XMemory_malloc(buf_size);
    if (!utf16_buf) return NULL;

    // 执行转换
    int64_t result = XChar_to_utf16(XString_cdata(str), utf16_buf, utf16_len + 1);
    if (result <= 0) {
        XMemory_free(utf16_buf);
        return NULL;
    }

    // 初始化缓存并存储结果
    XString_initCache((XString*)str);  // 此处强制转换因const语义，实际内部不修改原字符串
    str->m_cache[XStringCache_Utf16] = (char*)utf16_buf;
    return utf16_buf;
}

const uint32_t* XString_toUtf32(const XString* str)
{
    if (!str) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Utf32]) {
        return (const uint32_t*)str->m_cache[XStringCache_Utf32];
    }

    // 计算UTF-32所需缓冲区大小（包含终止符）
    int64_t utf32_len = XChar_to_utf32(XString_cdata(str), NULL, 0);
    if (utf32_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf32_len * sizeof(uint32_t) + sizeof(uint32_t);
    uint32_t* utf32_buf = (uint32_t*)XMemory_malloc(buf_size);
    if (!utf32_buf) return NULL;

    // 执行转换
    int64_t result = XChar_to_utf32(XString_cdata(str), utf32_buf, utf32_len + 1);
    if (result <= 0) {
        XMemory_free(utf32_buf);
        return NULL;
    }

    // 初始化缓存并存储结果
    XString_initCache((XString*)str);
    str->m_cache[XStringCache_Utf32] = (char*)utf32_buf;
    return utf32_buf;
}

const char* XString_toGbk(const XString* str)
{
    if (!str) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Gbk]) {
        return str->m_cache[XStringCache_Gbk];
    }

    // 计算GBK所需缓冲区大小（包含终止符）
    int64_t gbk_len = XChar_to_gbk(XString_cdata(str), NULL, 0);
    if (gbk_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t gbk_max_len = (size_t)gbk_len + 1;
    char* gbk_buf = (char*)XMemory_malloc(gbk_max_len);
    if (!gbk_buf) return NULL;

    // 执行转换
    int64_t result = XChar_to_gbk(XString_cdata(str), gbk_buf, gbk_max_len);
    if (result <= 0) {
        XMemory_free(gbk_buf);
        return NULL;
    }

    // 初始化缓存并存储结果
    XString_initCache((XString*)str);
    str->m_cache[XStringCache_Gbk] = gbk_buf;
    return gbk_buf;
}

const char* XString_toLocal(const XString* str)
{
    if (!str) return NULL;

    // 本地编码缓存已存在则直接返回
    if (str->m_cache && str->m_cache[XStringCache_Local]) return str->m_cache[XStringCache_Local];

#ifdef __linux__
    // Linux下本地编码为UTF-8，直接指向UTF-8缓存
    const char* utf8_str = XString_to_utf8(str);
    if (utf8_str) 
    {
        XString_initCache(str);
        // 共享UTF-8缓存地址
        str->m_cache[XStringCache_Local] = (char*)utf8_str;
    }
    return utf8_str;
#else
    // Windows下需要转换为GBK
    size_t xchar_len = XString_length_base(str);
    // 计算所需本地编码缓冲区大小（包含结束符）
    int64_t local_len = XChar_to_local(XString_cdata(str), NULL, 0);
    if (local_len <= 0) return NULL;

    size_t local_max_len = local_len + 1;  // 加终止符
    char* local_buf = (char*)XMemory_malloc(local_max_len);
    if (!local_buf) return NULL;

    // 执行实际转换
    int64_t result = XChar_to_local(XString_cdata(str), local_buf, local_max_len);
    if (result <= 0) {
        XMemory_free(local_buf);
        return NULL;
    }
    XString_initCache(str);
    // 缓存结果
    str->m_cache[XStringCache_Local] = local_buf;
    return local_buf;
#endif
}

XChar XString_at(const XString* str, size_t index) 
{
    if (!str || index >= XString_length_base(str)) 
        return XChar_from(0);
    return XClassGetVirtualFunc(str, EXString_At, XChar(*)(const XString*, size_t))(str, index);
}

const XChar* XString_unicode(const XString* str)
{
    if (!str) return NULL;
    // 直接XString的内部数据通过XContainerDataPtr访问，返回常量指针确保不被修改
    return (const XChar*)XContainerDataPtr(str);
}

const uint16_t* XString_utf16(const XString* str)
{
    if (!str) return NULL;

    // 利用已有的UTF-16转换缓存
    const uint16_t* utf16_data = XString_toUtf16(str);
    // 由于uint16_t在UTF-16场景下通常为16位，可直接转换为uint16_t指针
    return (const uint16_t*)utf16_data;
}

bool XString_append_utf8(XString* str, const char* utf8_str) 
{
    if (!str || !utf8_str) return false;

    // 先计算需要转换的XChar数量（不含终止符）
    int64_t xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    size_t current_size = XString_length_base(str);
    size_t new_size = current_size + (size_t)xchar_count;

    // 预留足够空间（包含终止符）
    XString_reserve(str, new_size);

    // 直接转换到目标缓冲区
    XChar* data = XString_data(str);
    int64_t result = XChar_from_utf8(
        (const uint8_t*)utf8_str,
        data + current_size,  // 直接写到当前字符串末尾
        (size_t)xchar_count + 1  // 包含终止符的空间（实际不会覆盖原终止符）
    );

    if (result <= 0) {
        // 转换失败时恢复原长度和终止符
        XContainerSize(str) = current_size;
        data[current_size].code = 0;
        return false;
    }

    // 更新字符串长度和终止符
    XContainerSize(str) = new_size;
    data[new_size].code = 0;

    XString_deinitCache(str);
    return true;
}

bool XString_assign_utf8(XString* str, const char* utf8_str)
{
    if (!str) return false;

    XString_clear_base(str);
    if (!utf8_str || *utf8_str == '\0') return true;

    // 先计算需要转换的XChar数量（不含终止符）
    int64_t xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    // 预留足够空间（包含终止符）
    XString_reserve(str, (size_t)xchar_count);

    // 直接转换到目标缓冲区
    XChar* data = XString_data(str);
    int64_t result = XChar_from_utf8(
        (const uint8_t*)utf8_str,
        data,
        (size_t)xchar_count + 1  // 包含终止符的空间
    );

    if (result <= 0) {
        // 转换失败时保持清空状态
        XContainerSize(str) = 0;
        data[0].code = 0;
        return false;
    }

    // 设置长度和终止符
    XContainerSize(str) = (size_t)xchar_count;
    data[xchar_count].code = 0;

    XString_deinitCache(str);
    return true;
}

bool XString_prepend_utf8(XString* str, const char* utf8_str) 
{
    if (!str || !utf8_str) return false;

    XString* original = XString_copy(str);
    if (!original) return false;

    XString_clear_base(str);
    if (!XString_append_utf8(str, utf8_str)) {
        XString_delete_base(original);
        return false;
    }

    bool success = XString_append_utf8(str, XString_toUtf8(original));
    XString_delete_base(original);
    return success;
}

bool XString_insert_utf8(const XString* str, size_t pos, const char* utf8_str) 
{
    if (!str || !utf8_str || pos > XString_length_base(str)) return false;

    XString* insert_str = XString_create_utf8(utf8_str);
    if (!insert_str) return false;
    size_t insert_len = XString_length_base(insert_str);
    if (insert_len == 0) {
        XString_delete_base(insert_str);
        return true;
    }

    XString_detach(str);
    size_t original_size = XString_length_base(str);
    size_t new_size = original_size + insert_len;

    XString_reserve(str, new_size);
    XChar* data = XString_data(str);

    memmove(data + pos + insert_len, data + pos, (original_size - pos) * sizeof(XChar));
    memcpy(data + pos, XString_cdata(insert_str), insert_len * sizeof(XChar));

    XContainerSize(str) = new_size;
    XString_data(str)[new_size].code = 0;

    XString_deinitCache(str);

    XString_delete_base(insert_str);
    return true;
}

bool XString_remove(XString* str, size_t pos, size_t len) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Remove, bool (*)(XString*, size_t, size_t))(str, pos, len);
}

bool XString_replace_utf8(XString* str, const char* before, const char* after, XCharCaseSensitivity cs)
{
    if (!str || !before || !after) return false;

    XString* before_str = XString_create_utf8(before);
    XString* after_str = XString_create_utf8(after);
    if (!before_str || !after_str) {
        XString_delete_base(before_str);
        XString_delete_base(after_str);
        return false;
    }

    size_t before_len = XString_length_base(before_str);
    size_t after_len = XString_length_base(after_str);
    if (before_len == 0) {
        XString_delete_base(before_str);
        XString_delete_base(after_str);
        return false;
    }

    int64_t pos = XString_index_of_utf8(str, before, 0, cs);
    while (pos != -1) {
        if (!XString_remove(str, (size_t)pos, before_len)) break;
        if (!XString_insert_utf8(str, (size_t)pos, after)) break;
        pos = XString_index_of_utf8(str, before, (size_t)pos + after_len, cs);
    }

    XString_delete_base(before_str);
    XString_delete_base(after_str);
    return true;
}

// 尾插单个XChar
bool XString_push_back_base(XString* str, XChar ch) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_PushBack, bool(*)(XString*, XChar))(str, ch);
}

// 尾删一个字符
bool XString_pop_back_base(XString* str) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_PopBack, bool(*)(XString*))(str);
}

// 头插单个XChar
bool XString_push_front_base(XString* str, XChar ch) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_PushFront, bool(*)(XString*, XChar))(str, ch);
}

// 头删一个字符
bool XString_pop_front_base(XString* str) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_PopFront, bool(*)(XString*))(str);
}
//查找算法
// 辅助函数：计算KMP前缀表
static void compute_prefix(const XChar* pattern, size_t m, int* prefix, XCharCaseSensitivity cs) {
    prefix[0] = 0;
    int len = 0;  // 当前最长前缀后缀长度

    for (size_t i = 1; i < m; ) {
        if (XChar_equals(&pattern[i], &pattern[len], cs)) {
            len++;
            prefix[i] = len;
            i++;
        }
        else {
            if (len != 0) {
                len = prefix[len - 1];  // 回溯到上一个可能的前缀长度
            }
            else {
                prefix[i] = 0;
                i++;
            }
        }
    }
}

// 辅助函数：KMP正向搜索
static int64_t kmp_search(const XChar* text, size_t n,
    const XChar* pattern, size_t m,
    const int* prefix, XCharCaseSensitivity cs,
    size_t from) {
    if (m == 0) return from <= n ? (int64_t)from : -1;  // 空模式串处理
    if (from >= n || m > n) return -1;  // 起始位置越界或模式串更长

    int i = (int)from;  // 主串索引
    int j = 0;          // 模式串索引

    while (i < (int)n) {
        if (XChar_equals(&text[i], &pattern[j], cs)) {
            i++;
            j++;
            if (j == (int)m) {
                return i - j;  // 找到匹配，返回起始位置
            }
        }
        else {
            if (j != 0) {
                j = prefix[j - 1];  // 利用前缀表回溯
            }
            else {
                i++;
            }
        }
    }
    return -1;  // 未找到匹配
}


int64_t XString_index_of_utf8(const XString* str, const char* substr, size_t from, XCharCaseSensitivity cs)
{
    if (!str || !substr) return -1;

    // 将substr转换为XString以便获取XChar数组
    XString* substr_str = XString_create_utf8(substr);
    if (!substr_str) return -1;

    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr_str);

    // 处理空模式串
    if (substr_len == 0) {
        XString_delete_base(substr_str);
        return (from <= str_len) ? (int64_t)from : -1;
    }

    // 模式串长于主串时直接返回-1
    if (substr_len > str_len) {
        XString_delete_base(substr_str);
        return -1;
    }

    const XChar* text = XString_cdata(str);       // 主串XChar数组
    const XChar* pattern = XString_cdata(substr_str);  // 模式串XChar数组

    // 分配并计算前缀表
    int* prefix = (int*)XMemory_malloc(substr_len * sizeof(int));
    if (!prefix) {
        XString_delete_base(substr_str);
        return -1;
    }
    compute_prefix(pattern, substr_len, prefix, cs);

    // 执行KMP搜索
    int64_t result = kmp_search(text, str_len, pattern, substr_len, prefix, cs, from);

    // 清理资源
    XMemory_free(prefix);
    XString_delete_base(substr_str);
    return result;
}
// 辅助函数：KMP反向搜索（用于last_index_of）
static int64_t kmp_reverse_search(const XChar* text, size_t n,
    const XChar* pattern, size_t m,
    XCharCaseSensitivity cs, size_t from) {
    if (m == 0 || m > n) return -1;

    // 计算最大可搜索起始位置（确保模式串能完全匹配）
    size_t max_start = (from >= n) ? (n - m) :
        (from >= m - 1) ? (from - m + 1) : 0;
    if (max_start > n - m) max_start = n - m;

    // 反转主串和模式串（为了复用正向KMP逻辑）
    XChar* rev_text = (XChar*)XMemory_malloc(n * sizeof(XChar));
    XChar* rev_pattern = (XChar*)XMemory_malloc(m * sizeof(XChar));
    if (!rev_text || !rev_pattern) {
        XMemory_free(rev_text);
        XMemory_free(rev_pattern);
        return -1;
    }

    // 填充反转后的数组
    for (size_t i = 0; i < n; i++) {
        rev_text[i] = text[n - 1 - i];
    }
    for (size_t i = 0; i < m; i++) {
        rev_pattern[i] = pattern[m - 1 - i];
    }

    // 计算反转模式串的前缀表
    int* prefix = (int*)XMemory_malloc(m * sizeof(int));
    if (!prefix) {
        XMemory_free(rev_text);
        XMemory_free(rev_pattern);
        return -1;
    }
    compute_prefix(rev_pattern, m, prefix, cs);

    // 搜索反转后的字符串（从0开始，因为已通过max_start限制范围）
    int64_t rev_pos = kmp_search(rev_text, n, rev_pattern, m, prefix, cs, 0);

    // 清理资源
    XMemory_free(prefix);
    XMemory_free(rev_text);
    XMemory_free(rev_pattern);

    // 转换为原字符串中的位置并检查是否在合法范围内
    if (rev_pos != -1) {
        int64_t original_pos = (n - m) - rev_pos;
        return (original_pos >= 0 && (size_t)original_pos <= max_start) ? original_pos : -1;
    }
    return -1;
}

int64_t XString_index_of(const XString* str, const XString* substr, size_t from, XCharCaseSensitivity cs)
{
    if (!str || !substr) return -1;

    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr);

    // 处理空模式串
    if (substr_len == 0) {
        return (from <= str_len) ? (int64_t)from : -1;
    }

    // 模式串长于主串时直接返回-1
    if (substr_len > str_len) {
        return -1;
    }

    const XChar* text = XString_cdata(str);       // 主串XChar数组
    const XChar* pattern = XString_cdata(substr);  // 模式串XChar数组

    // 分配并计算前缀表
    int* prefix = (int*)XMemory_malloc(substr_len * sizeof(int));
    if (!prefix) {
        return -1;
    }
    compute_prefix(pattern, substr_len, prefix, cs);

    // 执行KMP搜索
    int64_t result = kmp_search(text, str_len, pattern, substr_len, prefix, cs, from);

    // 清理资源
    XMemory_free(prefix);
    return result;
}

int64_t XString_last_index_of(const XString* str, const XString* substr, size_t from, XCharCaseSensitivity cs)
{
    if (!str || !substr) return -1;

    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr);

    // 处理空模式串
    if (substr_len == 0) {
        return (from < str_len) ? (int64_t)from : (int64_t)str_len;
    }

    // 模式串长于主串时直接返回-1
    if (substr_len > str_len) {
        return -1;
    }

    const XChar* text = XString_cdata(str);
    const XChar* pattern = XString_cdata(substr);

    // 计算最大可搜索起始位置（确保模式串能完全匹配）
    size_t max_start = (from >= str_len) ? (str_len - substr_len) :
        (from >= substr_len - 1) ? (from - substr_len + 1) : 0;
    if (max_start > str_len - substr_len) {
        max_start = str_len - substr_len;
    }

    // 反转主串和模式串（为了复用KMP正向搜索逻辑）
    XChar* rev_text = (XChar*)XMemory_malloc(str_len * sizeof(XChar));
    XChar* rev_pattern = (XChar*)XMemory_malloc(substr_len * sizeof(XChar));
    if (!rev_text || !rev_pattern) {
        XMemory_free(rev_text);
        XMemory_free(rev_pattern);
        return -1;
    }

    // 填充反转后的数组
    for (size_t i = 0; i < str_len; i++) {
        rev_text[i] = text[str_len - 1 - i];
    }
    for (size_t i = 0; i < substr_len; i++) {
        rev_pattern[i] = pattern[substr_len - 1 - i];
    }

    // 计算反转模式串的前缀表
    int* prefix = (int*)XMemory_malloc(substr_len * sizeof(int));
    if (!prefix) {
        XMemory_free(rev_text);
        XMemory_free(rev_pattern);
        return -1;
    }
    compute_prefix(rev_pattern, substr_len, prefix, cs);

    // 搜索反转后的字符串（从0开始，因为已通过max_start限制范围）
    int64_t rev_pos = kmp_search(rev_text, str_len, rev_pattern, substr_len, prefix, cs, 0);

    // 清理资源
    XMemory_free(prefix);
    XMemory_free(rev_text);
    XMemory_free(rev_pattern);

    // 转换为原字符串中的位置并检查是否在合法范围内
    if (rev_pos != -1) {
        int64_t original_pos = (str_len - substr_len) - rev_pos;
        return (original_pos >= 0 && (size_t)original_pos <= max_start) ? original_pos : -1;
    }

    return -1;
}

int64_t XString_last_index_of_utf8(const XString* str, const char* substr, size_t from, XCharCaseSensitivity cs)
{
    if (!str || !substr) return -1;

    // 将substr转换为XString以便获取XChar数组
    XString* substr_str = XString_create_utf8(substr);
    if (!substr_str) return -1;

    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr_str);

    // 处理空模式串
    if (substr_len == 0) {
        XString_delete_base(substr_str);
        return (from < str_len) ? (int64_t)from : (int64_t)str_len;
    }

    // 模式串长于主串时直接返回-1
    if (substr_len > str_len) {
        XString_delete_base(substr_str);
        return -1;
    }

    const XChar* text = XString_cdata(str);
    const XChar* pattern = XString_cdata(substr_str);

    // 执行反向KMP搜索
    int64_t result = kmp_reverse_search(text, str_len, pattern, substr_len, cs, from);

    XString_delete_base(substr_str);
    return result;
}

int XString_compare(const XString* str1, const XString* str2) 
{
    if (!str1 && !str2) 
        return 0;
    if (!str1)
        return -1;
    if (!str2)
        return 1;

    size_t min_len = XString_length_base(str1) < XString_length_base(str2)
        ? XString_length_base(str1)
        : XString_length_base(str2);
    const XChar* data1 = XString_cdata(str1);
    const XChar* data2 = XString_cdata(str2);

    for (size_t i = 0; i < min_len; i++) {
        if (data1[i].code < data2[i].code) return -1;
        if (data1[i].code > data2[i].code) return 1;
    }

    return (XString_length_base(str1) < XString_length_base(str2)) ? -1 :
        (XString_length_base(str1) > XString_length_base(str2)) ? 1 : 0;
}

bool XString_equals(const XString* str1, const XString* str2, XCharCaseSensitivity cs)
{
    // 空指针处理：两者都为空则相等，仅有一个为空则不等
    if (!str1 && !str2) return true;
    if (!str1 || !str2) return false;

    // 长度不同直接不等（快速失败）
    size_t len1 = XString_length_base(str1);
    size_t len2 = XString_length_base(str2);
    if (len1 != len2) return false;

    // 获取字符数据指针
    const XChar* data1 = XString_cdata(str1);
    const XChar* data2 = XString_cdata(str2);

    // 逐个字符比较
    for (size_t i = 0; i < len1; i++) {
        if (!XChar_equals(&data1[i], &data2[i], cs)) {
            return false;
        }
    }

    return true;
}

bool XEquality_XString(const XString* str1, const XString* str2) 
{
    return XString_compare(str1, str2) == 0;
}

bool XString_starts_with_utf8(const XString* str, const char* prefix, XCharCaseSensitivity cs) 
{
    if (!str || !prefix) return false;

    XString* prefix_str = XString_create_utf8(prefix);
    if (!prefix_str) return false;

    bool result = (XString_length_base(prefix_str) <= XString_length_base(str)) &&
        (XString_index_of_utf8(str, prefix, 0,cs) == 0);
    XString_delete_base(prefix_str);
    return result;
}

bool XString_ends_with_utf8(const XString* str, const char* suffix, XCharCaseSensitivity cs)
{
    if (!str || !suffix) return false;

    XString* suffix_str = XString_create_utf8(suffix);
    if (!suffix_str) return false;

    size_t str_len = XString_length_base(str);
    size_t suffix_len = XString_length_base(suffix_str);
    bool result = (suffix_len <= str_len) &&
        (XString_last_index_of_utf8(str, suffix, str_len - suffix_len,cs) == (int64_t)(str_len - suffix_len));

    XString_delete_base(suffix_str);
    return result;
}

XString* XString_toLower(const XString* str) {
    if (!str) return NULL;

    XString* result = XString_copy(str);
    XString_detach(result);  // 确保可修改

    XChar* data = XString_data(result);
    for (size_t i = 0; i < XString_length_base(result); i++) {
        data[i] = XChar_to_lower(&data[i]);
    }
    data[XString_length_base(result)] = (XChar){ 0 };  // 更新结束符

    XString_deinitCache(result);
    return result;
}

XString* XString_toUpper(const XString* str) {
    if (!str) return NULL;

    XString* result = XString_copy(str);
    XString_detach(result);  // 确保可修改

    XChar* data = XString_data(result);
    for (size_t i = 0; i < XString_length_base(result); i++) {
        data[i] = XChar_to_upper(&data[i]);
    }
    data[XString_length_base(result)] = (XChar){ 0 };  // 更新结束符

    XString_deinitCache(result);
    return result;
}

XString* XString_trimmed(const XString* str) {
    if (!str || XString_isEmpty_base(str)) return XString_copy(str);

    const XChar* data = XContainerDataPtr(str);
    size_t start = 0;
    size_t end = XString_length_base(str) - 1;

    // 跳过前导空白
    while (start <= end && XChar_is_space(&data[start])) start++;
    // 跳过后导空白
    while (end >= start && XChar_is_space(&data[end])) end--;

    if (start > end) return XString_create_utf8("");  // 全是空白
    return XString_mid(str, start, end - start + 1);
}

unsigned short XString_toUShort(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    unsigned long val = strtoul(utf8, &endptr, base);

    // 额外检查是否在unsigned short范围内
    if (endptr != utf8 && *endptr == '\0' && val <= USHRT_MAX) {
        if (ok) *ok = true;
        return (unsigned short)val;
    }
    return 0;
}

unsigned int XString_toUInt(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    unsigned long val = strtoul(utf8, &endptr, base);

    // 检查转换有效性：必须有有效字符、完全转换且值在unsigned int范围内
    if (endptr != utf8 && *endptr == '\0' && val <= UINT_MAX) {
        if (ok) *ok = true;
        return (unsigned int)val;
    }
    return 0;
}

short XString_toShort(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    long val = strtol(utf8, &endptr, base);

    // 检查转换有效性：必须有有效字符、完全转换且值在short范围内
    if (endptr != utf8 && *endptr == '\0' && val >= SHRT_MIN && val <= SHRT_MAX) {
        if (ok) *ok = true;
        return (short)val;
    }
    return 0;
}

int XString_toInt(const XString* str, bool* ok, int base) {
    if (!str || !ok) return 0;
    *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    long val = strtol(utf8, &endptr, base);

    // 检查转换是否有效
    if (endptr != utf8 && *endptr == '\0' && val >= INT_MIN && val <= INT_MAX) {
        *ok = true;
        return (int)val;
    }
    return 0;
}

long XString_toLong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    long val = strtol(utf8, &endptr, base);

    // 检查转换有效性：必须有有效字符且完全转换，且值在long范围内
    if (endptr != utf8 && *endptr == '\0') {
        if (ok) *ok = true;
        return val;
    }
    return 0;
}

long long XString_toLongLong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    long long val = strtoll(utf8, &endptr, base);

    // 检查转换有效性：必须有有效字符且完全转换
    if (endptr != utf8 && *endptr == '\0') {
        if (ok) *ok = true;
        return val;
    }
    return 0;
}

unsigned long XString_toULong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    unsigned long val = strtoul(utf8, &endptr, base);

    // 检查转换有效性：必须有有效字符且完全转换
    if (endptr != utf8 && *endptr == '\0') {
        if (ok) *ok = true;
        return val;
    }
    return 0;
}

unsigned long long XString_toULongLong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    unsigned long long val = strtoull(utf8, &endptr, base);

    if (endptr != utf8 && *endptr == '\0') {
        if (ok) *ok = true;
        return val;
    }
    return 0;
}

float XString_toFloat(const XString* str, bool* ok)
{
    if (!str) {
        if (ok) *ok = false;
        return 0.0f;
    }
    if (ok) *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    float val = strtof(utf8, &endptr);

    // 检查转换有效性：必须有有效字符且完全转换
    if (endptr != utf8 && *endptr == '\0') {
        if (ok) *ok = true;
        return val;
    }
    return 0.0f;
}

double XString_toDouble(const XString* str, bool* ok) {
    if (!str || !ok) return 0.0;
    *ok = false;

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    double val = strtod(utf8, &endptr);

    if (endptr != utf8 && *endptr == '\0') {
        *ok = true;
        return val;
    }
    return 0.0;
}

XString* XString_left(const XString* str, size_t n) {
    if (!str || n == 0) return XString_create_utf8("");
    n = (n > XString_length_base(str)) ? XString_length_base(str) : n;
    return XString_mid(str, 0, n);
}

XString* XString_right(const XString* str, size_t n) {
    if (!str || n == 0) return XString_create_utf8("");
    size_t len = XString_length_base(str);
    n = (n > len) ? len : n;
    return XString_mid(str, len - n, n);
}

XString* XString_mid(const XString* str, size_t pos, size_t n)
{
    if (!str || pos >= XString_length_base(str) || n == 0) return XString_create_utf8("");

    size_t len = XString_length_base(str);
    size_t actual_len = (pos + n > len) ? (len - pos) : n;
    XString* result = XString_create_utf8("");
    if (!result) return NULL;

    XString_detach(result);
    XString_reserve(result, actual_len);  // 自动预留结束符空间

    const XChar* src = XString_cdata(str) + pos;
    XChar* dest = XString_data(result);
    memcpy(dest, src, actual_len * sizeof(XChar));
    result->parent.m_size = actual_len;
    dest[actual_len] = (XChar){ 0 };  // 设置结束符

    return result;
}

// 预分配空间（额外+1存储结束符）
void XString_reserve(XString* str, size_t capacity)
{
    if (!str || capacity <= XContainerCapacity(&str->parent)) return;

    XString_detach(str);  // 确保可修改
    // 新容量 = 实际需要容量 + 1（结束符），且不小于最小容量
    size_t new_capacity = (capacity < XSTRING_MIN_CAPACITY) ? XSTRING_MIN_CAPACITY : capacity;
    new_capacity += 1;  // 预留结束符位置
    XChar* new_data = (XChar*)XMemory_realloc(XString_data(str), new_capacity * sizeof(XChar));
    if (new_data) 
    {
        str->parent.m_data = new_data;
        str->parent.m_capacity = new_capacity;
        // 设置结束符（当前有效长度位置）
        new_data[XString_length_base(str)] = (XChar){ 0 };
    }
}

void XString_truncate(XString* str, size_t position)
{
    if (!str) return;

    // 位置超出当前长度时无需操作
    size_t current_len = XString_length_base(str);
    if (position >= current_len) return;

    XString_detach(str); // 确保可修改（Copy-On-Write机制）

    // 截断长度（保留 position 个字符）
    str->parent.m_size = position;
    // 更新终止符
    XString_data(str)[position] = (XChar){ 0 };

    // 清除缓存（截断后缓存内容失效）
    XString_deinitCache(str);
}

// 辅助函数：查找下一个分隔符位置
static int64_t find_next_delimiter(const XString* str, const char* delimiter, size_t current_pos, XCharCaseSensitivity cs)
{
    if (!str || !delimiter || current_pos > XString_length_base(str)) {
        return -1;
    }
    return XString_index_of_utf8(str, delimiter, current_pos,cs);
}

// 按分隔符拆分字符串
XStringList* XString_split_utf8(const XString* str, const char* delimiter, XCharCaseSensitivity cs) 
{
    if (!str || !delimiter) {
        return NULL;
    }

    XStringList* result = XStringList_create();
    if (!result) {
        return NULL;
    }

    // 处理空字符串
    if (XString_isEmpty_base(str)) {
        return result;
    }

    size_t current_pos = 0;
    const size_t str_len = XString_length_base(str);
    const size_t delimiter_len = strlen(delimiter);

    // 空分隔符特殊处理（按字符拆分）
    if (delimiter_len == 0) {
        for (size_t i = 0; i < str_len; i++) 
        {
            XString* substr = XString_mid(str, i, 1);
            if (substr)
            {
                XStringList_push_back_move_base(result, substr);
            }
            XString_delete_base(substr);
        }
        return result;
    }

    // 正常分隔符拆分
    while (current_pos <= str_len) {
        int64_t delimiter_pos = find_next_delimiter(str, delimiter, current_pos,cs);
        size_t end_pos = (delimiter_pos == -1) ? str_len : (size_t)delimiter_pos;

        // 提取子串（跳过空内容）
        if (end_pos > current_pos) 
        {
            XString* substr = XString_mid(str, current_pos, end_pos - current_pos);
            if (substr)
            {
                XStringList_push_back_move_base(result, substr);
            }
            XString_delete_base(substr);
        }

        // 终止条件：未找到分隔符
        if (delimiter_pos == -1) {
            break;
        }

        current_pos = end_pos + delimiter_len;
    }

    return result;
}

// 按分隔符拆分字符串（限制最大拆分次数）
XStringList* XString_split_limit_utf8(const XString* str, const char* delimiter, size_t limit, XCharCaseSensitivity cs)
{
    if (!str || !delimiter || limit == 0) {
        return NULL;
    }

    XStringList* result = XStringList_create();
    if (!result) {
        return NULL;
    }

    // 处理空字符串
    if (XString_isEmpty_base(str)) {
        return result;
    }

    size_t current_pos = 0;
    const size_t str_len = XString_length_base(str);
    const size_t delimiter_len = strlen(delimiter);
    size_t split_count = 0;

    // 空分隔符特殊处理（按字符拆分，限制次数）
    if (delimiter_len == 0) {
        const size_t actual_limit = (limit > str_len) ? str_len : limit;
        for (size_t i = 0; i < actual_limit; i++) {
            XString* substr = XString_mid(str, i, 1);
            if (substr) {
                XStringList_push_back_move_base(result, substr);
                split_count++;
            }
        }
        return result;
    }

    // 有限制的分隔符拆分
    while (current_pos <= str_len && split_count < limit) {
        int64_t delimiter_pos = find_next_delimiter(str, delimiter, current_pos,cs);
        size_t end_pos = (delimiter_pos == -1) ? str_len : (size_t)delimiter_pos;

        // 提取子串（跳过空内容）
        if (end_pos > current_pos) {
            XString* substr = XString_mid(str, current_pos, end_pos - current_pos);
            if (substr) {
                XStringList_push_back_move_base(result, substr);
                split_count++;
            }
        }

        // 达到最大拆分次数，终止循环
        if (split_count >= limit) {
            break;
        }

        // 终止条件：未找到分隔符
        if (delimiter_pos == -1) {
            break;
        }

        current_pos = end_pos + delimiter_len;
    }

    // 若还有剩余内容且未达限制，添加最后一个元素
    if (current_pos < str_len && split_count < limit) {
        XString* substr = XString_mid(str, current_pos, str_len - current_pos);
        if (substr) {
            XStringList_push_back_move_base(result, substr);
        }
    }

    return result;
}

// 获取可修改的内部XChar数组
XChar* XString_data(XString* str)
{
    if (!str) return NULL;
    XString_detach(str);  // 修改前确保分离
    return (XChar*)XContainerDataPtr(str);
}

// 分离共享数据（Copy-On-Write机制）
void XString_detach(XString* str)
{
    if (!str || !str->m_is_shared || *str->m_ref_count <= 1) return;

    // 引用计数大于1，需要复制数据（包含结束符）
    *str->m_ref_count -= 1;
    size_t size = XString_length_base(str);
    size_t capacity = XContainerCapacity(str);

    // 新容量保留原容量（已包含结束符），若不足则扩展
    size_t new_capacity = (capacity > size) ? capacity : size + 1;
    XChar* new_data = (XChar*)XMemory_malloc(new_capacity * sizeof(XChar));
    memcpy(new_data, XContainerDataPtr(str), size * sizeof(XChar));  // 复制有效数据
    new_data[size] = (XChar){ 0 };  // 设置结束符

    // 重置引用计数和共享标记
    str->m_ref_count = (int*)XMemory_malloc(sizeof(int));
    *str->m_ref_count = 1;
    str->m_is_shared = false;
    str->parent.m_data = new_data;
    str->parent.m_capacity = new_capacity;
}

void XString_deinitCache(XString* str)
{
    if (str == NULL || str->m_cache == NULL)
        return;
    char* local = str->m_cache[0];
    for (size_t i = 1; i < XStringCache_Size; i++)
    {
        XMemory_free(str->m_cache[i]);
        if (str->m_cache[i] == local)
            local = NULL;
        str->m_cache[i] = NULL;
    }
    if (local != NULL)
    {
        XMemory_free(local);
        str->m_cache[0] = NULL;
    }
}

int XPrint(const XString* str)
{
    if (str == NULL)
        return 0;
    printf("%s\n", XString_toLocal(str));
    return XString_length_base(str);
}

int XPrint_utf8(const char* utf8_str) 
{
    if (!utf8_str) return 0;  // 空指针安全处理

#ifdef _WIN32
    // Windows平台：UTF-8 -> GBK 转换后输出
    // 1. 计算所需GBK缓冲区大小
    int64_t gbk_len = XUTF8_to_gbk(utf8_str, NULL, 0);
    if (gbk_len <= 0) return 0;  // 转换失败

    // 2. 分配GBK缓冲区（+1用于终止符）
    char* gbk_buf = (char*)XMemory_malloc(gbk_len + 1);
    if (!gbk_buf) return 0;

    // 3. 执行UTF-8到GBK的转换
    if (XUTF8_to_gbk(utf8_str, gbk_buf, gbk_len + 1) <= 0) {
        XMemory_free(gbk_buf);
        return 0;
    }

    // 4. 输出GBK字符串并释放资源
    int result = printf("%s\n", gbk_buf);
    XMemory_free(gbk_buf);
    return result;

#else
    // Linux平台：直接输出UTF-8（系统默认支持）
    return printf("%s", utf8_str);
#endif
}

int XPrint_utf8_fmt(const char* format, ...)
{
    if (!format) return 0;  // 空格式字符串安全处理

    va_list args;
    va_start(args, format);

#ifdef _WIN32

    // Windows平台：UTF-8格式化 -> 宽字符格式化 -> GBK输出
    // 步骤1：先将UTF-8格式字符串转换为宽字符
    int wfmt_len = MultiByteToWideChar(CP_UTF8, 0, format, -1, NULL, 0);
    if (wfmt_len <= 0) {
        va_end(args);
        return 0;
    }

    uint16_t* wfmt = (uint16_t*)XMemory_malloc(wfmt_len * sizeof(uint16_t));
    if (!wfmt) {
        va_end(args);
        return 0;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, format, -1, wfmt, wfmt_len) <= 0) {
        XMemory_free(wfmt);
        va_end(args);
        return 0;
    }

    // 步骤2：使用宽字符vswprintf格式化内容
    int wbuf_len = _vscwprintf(wfmt, args) + 1;  // +1 包含终止符
    uint16_t* wbuf = (uint16_t*)XMemory_malloc(wbuf_len * sizeof(uint16_t));
    if (!wbuf) {
        XMemory_free(wfmt);
        va_end(args);
        return 0;
    }

    vswprintf(wbuf, wbuf_len, wfmt, args);
    XMemory_free(wfmt);  // 释放格式字符串缓冲区

    // 步骤3：将宽字符结果转换为GBK
    int gbk_len = WideCharToMultiByte(CP_ACP, 0, wbuf, -1, NULL, 0, NULL, NULL);
    if (gbk_len <= 0) {
        XMemory_free(wbuf);
        va_end(args);
        return 0;
    }

    char* gbk_buf = (char*)XMemory_malloc(gbk_len);
    if (!gbk_buf) {
        XMemory_free(wbuf);
        va_end(args);
        return 0;
    }

    WideCharToMultiByte(CP_ACP, 0, wbuf, -1, gbk_buf, gbk_len, NULL, NULL);
    XMemory_free(wbuf);  // 释放宽字符缓冲区

    // 步骤4：打印GBK字符串并清理
    int result = printf("%s", gbk_buf);  // 补充换行符
    XMemory_free(gbk_buf);
    va_end(args);
    return result;

#else
    // Linux平台：直接使用UTF-8格式化输出（不自动动添加换行符）
     // 步骤1：精确计算格式化式化所需缓冲区大小（包含终止符）
    int buf_len = vsnprintf(NULL, 0, format, args) + 1;  // +1 仅包含终止符
    char* buf = (char*)XMemory_malloc(buf_len);
    if (!buf) {
        va_end(args);
        return 0;
    }

    // 步骤2：执行格式化（直接写入缓冲区，不含需额外外处理换行）
    int written = vsnprintf(buf, buf_len, format, args);
    if (written < 0) {
        XMemory_free(buf);
        va_end(args);
        return 0;
    }

    // 步骤3：输出结果（直接打印格式化后的内容）
    int result = printf("%s", buf);

    // 清理资源
    XMemory_free(buf);
    va_end(args);
    return result;
#endif
}

void XString_initCache(XString* str)
{
    if (str == NULL || str->m_cache != NULL)
        return;
    str->m_cache=XMemory_calloc(XStringCache_Size,sizeof(char*));
}
