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

// 获取可修改的内部XChar数组
XChar* XString_data(XString* str);

//初始化缓存
static void XString_initCache(XString* str);
// ------------------------------
// 虚函数包装（对外接口）
// ------------------------------

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

uint32_t XString_at(const XString* str, size_t index) {
    if (!str || index >= XString_length_base(str)) return 0;
    return XClassGetVirtualFunc(str, EXString_At, uint32_t(*)(const XString*, size_t))(str, index);
}

bool XString_append_base(XString* str, const char* utf8_str) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Append, bool(*)(XString*, const char*))(str, utf8_str);
}

bool XString_assign_base(XString* str, const char* utf8_str)
{
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Assign, bool(*)(XString*, const char*))(str, utf8_str);
}

bool XString_prepend(XString* str, const char* utf8_str) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Prepend, bool(*)(XString*, const char*))(str, utf8_str);
}

bool XString_insert(const XString* str, size_t pos, const char* utf8_str) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Insert, bool(*)(XString*, size_t, const char*))((XString*)str, pos, utf8_str);
}

bool XString_remove(XString* str, size_t pos, size_t len) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Remove, bool (*)(XString*, size_t, size_t))(str, pos, len);
}

bool XString_replace(XString* str, const char* before, const char* after) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Replace, bool(*)(XString*, const char*, const char*))(str, before, after);
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

int64_t XString_index_of(const XString* str, const char* substr, size_t from) {
    if (!str) return -1;
    return XClassGetVirtualFunc(str, EXString_IndexOf, int64_t(*)(const XString*, const char*, size_t))(str, substr, from);
}

int64_t XString_last_index_of(const XString* str, const char* substr, size_t from) {
    if (!str) return -1;
    return XClassGetVirtualFunc(str, EXString_LastIndexOf, int64_t(*)(const XString*, const char*, size_t))(str, substr, from);
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

bool XEquality_XString(const XString* str1, const XString* str2) 
{
    return XString_compare(str1, str2) == 0;
}

bool XString_starts_with(const XString* str, const char* prefix) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_StartsWith, bool(*)(const XString*, const char*))(str, prefix);
}

bool XString_ends_with(const XString* str, const char* suffix) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_EndsWith, bool(*)(const XString*, const char*))(str, suffix);
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

    if (start > end) return XString_create("");  // 全是空白
    return XString_mid(str, start, end - start + 1);
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
    if (!str || n == 0) return XString_create("");
    n = (n > XString_length_base(str)) ? XString_length_base(str) : n;
    return XString_mid(str, 0, n);
}

XString* XString_right(const XString* str, size_t n) {
    if (!str || n == 0) return XString_create("");
    size_t len = XString_length_base(str);
    n = (n > len) ? len : n;
    return XString_mid(str, len - n, n);
}

XString* XString_mid(const XString* str, size_t pos, size_t n)
{
    if (!str || pos >= XString_length_base(str) || n == 0) return XString_create("");

    size_t len = XString_length_base(str);
    size_t actual_len = (pos + n > len) ? (len - pos) : n;
    XString* result = XString_create("");
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

// 辅助函数：查找下一个分隔符位置
static int64_t find_next_delimiter(const XString* str, const char* delimiter, size_t current_pos) {
    if (!str || !delimiter || current_pos > XString_length_base(str)) {
        return -1;
    }
    return XString_index_of(str, delimiter, current_pos);
}

// 按分隔符拆分字符串
XStringList* XString_split(const XString* str, const char* delimiter) {
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
        int64_t delimiter_pos = find_next_delimiter(str, delimiter, current_pos);
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
XStringList* XString_split_limit(const XString* str, const char* delimiter, size_t limit) {
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
        int64_t delimiter_pos = find_next_delimiter(str, delimiter, current_pos);
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
    for (size_t i = 0; i < XStringCache_Size; i++)
    {
        XMemory_free(str->m_cache[i]);
        str->m_cache[i] = NULL;
    }
}

int XPrint(const XString* str)
{
    if (str == NULL)
        return 0;
    printf("%s\n", XString_toLocal(str));
    //// 使用内部结束符，无需额外添加
    //size_t len = XChar_to_local(XString_cdata(str), NULL, 0);
    //char* buff = XMemory_malloc(len + 1);
    //len = XChar_to_local(XString_cdata(str), buff, len + 1);
    //printf("%s\n", buff);
    //XMemory_free(buff);
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
    // 先计算UTF-8长度（不含终止符）
    //size_t len = 0;
    //while (utf8_str[len] != '\0') len++;

    //// 使用write系统调用直接输出（避免stdio缓冲问题）
    //if (len > 0) {
    //    write(STDOUT_FILENO, utf8_str, len);
    //}
    //write(STDOUT_FILENO, "\n", 1);  // 补充换行符
    //return (int)(len + 1);  // 返回总输出字节数（含换行）
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

    wchar_t* wfmt = (wchar_t*)XMemory_malloc(wfmt_len * sizeof(wchar_t));
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
    wchar_t* wbuf = (wchar_t*)XMemory_malloc(wbuf_len * sizeof(wchar_t));
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
