#include "XString.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
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
//#define XString_copy        XString_create_copy

// -------------------------- 内部机制辅助函数 --------------------------

/**
 * @brief 分离共享数据（Copy-On-Write 机制）
 * @details 当字符串数据被共享时，复制一份独立数据供修改，避免影响其他对象
 * @param str XString 对象指针
 */
void XString_detach(XString* str);

/**
 * @brief 释放所有编码缓存
 * @param str XString 对象指针
 */
void XString_deinitCache(XString* str);

// 获取可修改的内部XChar数组
XChar* XString_data(XString* str);
// 辅助函数：计算KMP前缀表
static void compute_prefix(const XChar* pattern, size_t m, int* prefix, XCharCaseSensitivity cs);
// 辅助函数：KMP反向搜索（用于last_index_of）
static int64_t kmp_reverse_search(const XChar* text, size_t n, const XChar* pattern, size_t m,
    XCharCaseSensitivity cs, size_t start_idx);
// 辅助函数：KMP正向搜索
static int64_t kmp_search(const XChar* text, size_t n,const XChar* pattern, size_t m,const int* prefix, XCharCaseSensitivity cs,size_t from);

//初始化缓存
static void XString_initCache(XString* str);

XString* XString_create()
{
    return XString_create_utf8(NULL);
}

XString* XString_create_copy(const XString* other)
{
    if (other == NULL)
        return NULL;
    XString* str = (XString*)XMemory_malloc(sizeof(XString));
    if (!str) return NULL;
    memset(str, 0, sizeof(XString));

    XString_copy_base(str, other);

    return str;
}
XString* XString_create_move(XString* other)
{
    if (other==NULL)
        return NULL;
    XString* str= XString_create();
    XString_move_base(str,other);
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
        int xchar_count = XChar_from_utf8_stream((const uint8_t*)utf8_str,len, NULL, 0);
        if (xchar_count > 0) {
            XString_reserve(str, xchar_count);
            xchar_count = XChar_from_utf8_stream((const uint8_t*)utf8_str,len, XString_data(str), xchar_count + 1);
            str->parent.m_size = xchar_count;
        }
    }
    Set_Class_MemoryFree(str, XFree);
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
    int64_t xchar_count = XChar_from_gbk_stream(temp_gbk, len, NULL, 0);
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
    xchar_count = XChar_from_gbk_stream(temp_gbk, len, data, (size_t)xchar_count + 1); // +1 预留终止符位置
    XMemory_free(temp_gbk); // 转换完成后释放临时缓冲区

    if (xchar_count <= 0) {
        XString_delete_base(str);
        return NULL;
    }

    // 步骤6：设置长度和终止符
    str->parent.m_size = (size_t)xchar_count;
    data[xchar_count] = (XChar){ 0 };

    XString_deinitCache(str);
    Set_Class_MemoryFree(str, XFree);
    return str;
}

XString* XString_create_fmt_utf8(const char* format, ...) 
{
    if (!format) return NULL;

    va_list args, args_copy;
    va_start(args, format);
    va_copy(args_copy, args);  // 复制参数列表用于二次调用

    // 第一次调用：获取所需缓冲区大小（不写入数据）
    int buf_size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);  // 释放复制的参数列表

    if (buf_size < 0) {
        va_end(args);    // 释放原始参数列表
        return NULL;     // 格式化失败
    }

    // 分配缓冲区（+1 用于存储终止符'\0'）
    char* buf = (char*)XMemory_malloc(buf_size + 1);
    if (!buf) {
        va_end(args);
        return NULL;     // 内存分配失败
    }

    // 第二次调用：实际写入格式化后的字符串
    vsnprintf(buf, buf_size + 1, format, args);
    va_end(args);  // 释放原始参数列表

    // 创建UTF-8编码的XString并释放临时缓冲区
    XString* str = XString_create_utf8(buf);
    XMemory_free(buf);

    return str;
}

// 初始化函数
void XString_init(XString* str)
{
    if (!str) return;
    XContainerObject_init(str, sizeof(XChar));
    // 初始化原子引用计数（初始值为1）
    str->m_ref_count = (XAtomic_int32_t*)XMemory_malloc(sizeof(XAtomic_int32_t));
    if (str->m_ref_count) 
    {
        XAtomic_store_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 使用原子存储初始化
    }
    str->m_cache = NULL;

    XClassGetVtable((XClass*)str) = XString_class_init();
}

const char* XString_toUtf8(const XString* str)
{
    if (!str) return NULL;

    // 缓存已存在则直接返回
    if (str->m_cache&& str->m_cache[XStringCache_Utf8].m_data) return str->m_cache[XStringCache_Utf8].m_data;

    // 计算所需UTF-8缓冲区大小（不包含结束符）
    int64_t utf8_len = XChar_to_utf8_stream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (utf8_len <= 0) return NULL;
    
    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf8_len * sizeof(uint8_t) + sizeof(uint8_t);
    uint16_t* utf8_buf = (uint16_t*)XMemory_malloc(buf_size);
    if (!utf8_buf) return NULL;

    // 调用XChar转换函数（使用内部结束符自动处理）
    int64_t result = XChar_to_utf8_stream(XString_cdata(str), XString_length_base(str), (uint8_t*)utf8_buf, utf8_len+1);
    if (result <= 0) 
    {
        XMemory_free(utf8_buf);
        return NULL;  // 转换失败
    }
    XString_initCache(str);
    // 缓存结果（内部保证线程安全）
    str->m_cache[XStringCache_Utf8].m_data = utf8_buf;
    str->m_cache[XStringCache_Utf8].m_length = result;
    return utf8_buf;
}

size_t XString_toUtf8_length(const XString* str)
{
    if (!str)
        return 0;
    if (XString_toUtf8(str))
        return str->m_cache[XStringCache_Utf8].m_length;
    return 0;
}

const uint16_t* XString_toUtf16(const XString* str)
{
    if (!str) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Utf16].m_data) 
    {
        return (const uint16_t*)str->m_cache[XStringCache_Utf16].m_data;
    }

    // 计算UTF-16所需缓冲区大小（包含终止符）
    int64_t utf16_len = XChar_to_utf16_stream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (utf16_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf16_len * sizeof(uint16_t) + sizeof(uint16_t);
    uint16_t* utf16_buf = (uint16_t*)XMemory_malloc(buf_size);
    if (!utf16_buf) return NULL;

    // 执行转换
    int64_t result = XChar_to_utf16_stream(XString_cdata(str), XString_length_base(str), utf16_buf, utf16_len + 1);
    if (result <= 0) {
        XMemory_free(utf16_buf);
        return NULL;
    }

    // 初始化缓存并存储结果
    XString_initCache((XString*)str);  // 此处强制转换因const语义，实际内部不修改原字符串
    str->m_cache[XStringCache_Utf16].m_data = (char*)utf16_buf;
    str->m_cache[XStringCache_Utf16].m_length = result;
    return utf16_buf;
}

size_t XString_toUtf16_length(const XString* str)
{
    if (!str)
        return 0;
    if (XString_toUtf16(str))
        return str->m_cache[XStringCache_Utf16].m_length;
    return 0;
}

const uint32_t* XString_toUtf32(const XString* str)
{
    if (!str) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Utf32].m_data) 
    {
        return (const uint32_t*)str->m_cache[XStringCache_Utf32].m_data;
    }

    // 计算UTF-32所需缓冲区大小（包含终止符）
    int64_t utf32_len = XChar_to_utf32_stream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (utf32_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf32_len * sizeof(uint32_t) + sizeof(uint32_t);
    uint32_t* utf32_buf = (uint32_t*)XMemory_malloc(buf_size);
    if (!utf32_buf) return NULL;

    // 执行转换
    int64_t result = XChar_to_utf32_stream(XString_cdata(str), XString_length_base(str), utf32_buf, utf32_len + 1);
    if (result <= 0) {
        XMemory_free(utf32_buf);
        return NULL;
    }

    // 初始化缓存并存储结果
    XString_initCache((XString*)str);
    str->m_cache[XStringCache_Utf32].m_data = (char*)utf32_buf;
    str->m_cache[XStringCache_Utf32].m_length = result;
    return utf32_buf;
}

size_t XString_toUtf32_length(const XString* str)
{
    if (!str)
        return 0;
    if (XString_toUtf32(str))
        return str->m_cache[XStringCache_Utf32].m_length;
    return 0;
}

const char* XString_toGbk(const XString* str)
{
    if (!str) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Gbk].m_data) {
        return str->m_cache[XStringCache_Gbk].m_data;
    }

    // 计算GBK所需缓冲区大小（包含终止符）
    int64_t gbk_len = XChar_to_gbk_stream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (gbk_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t gbk_max_len = (size_t)gbk_len + 1;
    char* gbk_buf = (char*)XMemory_malloc(gbk_max_len);
    if (!gbk_buf) return NULL;

    // 执行转换
    int64_t result = XChar_to_gbk_stream(XString_cdata(str), XString_length_base(str), gbk_buf, gbk_max_len);
    if (result <= 0) {
        XMemory_free(gbk_buf);
        return NULL;
    }

    // 初始化缓存并存储结果
    XString_initCache((XString*)str);
    str->m_cache[XStringCache_Gbk].m_data = gbk_buf;
    return gbk_buf;
}

size_t XString_toGbk_length(const XString* str)
{
    if (!str) 
        return 0;
    if (XString_toGbk(str)) 
        return str->m_cache[XStringCache_Gbk].m_length;
    return 0;
}

const char* XString_toLocal(const XString* str)
{
    if (!str) return NULL;

    // 本地编码缓存已存在则直接返回
    if (str->m_cache && str->m_cache[XStringCache_Local].m_data) 
        return str->m_cache[XStringCache_Local].m_data;
    XString_initCache(str);
#ifdef _WIN32
    // Windows下需要转换为GBK
    str->m_cache[XStringCache_Local].m_data = XString_toGbk(str);
    str->m_cache[XStringCache_Local].m_length = XString_toGbk_length(str);
#else
    // Linux下本地编码为UTF-8，直接指向UTF-8缓存
    // 共享UTF-8缓存地址
    str->m_cache[XStringCache_Local].m_data = XString_toUtf8(str);
    str->m_cache[XStringCache_Local].m_length = XString_toUtf8_length(str);
#endif
 return str->m_cache[XStringCache_Local].m_data;
}

size_t XString_toUtfLocal_length(const XString* str)
{
    if (!str)
        return 0;
    if (XString_toLocal(str))
        return str->m_cache[XStringCache_Local].m_length;
    return 0;
}

XChar XString_at(const XString* str, size_t index) 
{
    if (!str || index >= XString_length_base(str)) 
        return 0;
    return XClassGetVirtualFunc(str, EXString_At, XChar(*)(const XString*, size_t))(str, index);
}

XChar XString_front(const XString* str)
{
    // 检查字符串指针有效性及是否为空
    if (!str || XString_isEmpty_base(str)) 
    {
        return 0; // 返回空字符（code为0的XChar）
    }

    // 获取字符串的首个字符（索引为0）
    return XString_at(str, 0);
}

XChar XString_back(const XString* str)
{
    // 检查字符串是否有效且非空
    if (!str || XString_isEmpty_base(str)) 
    {
        return 0; // 返回空字符
    }

    // 获取字符串长度（不含终止符）
    size_t len = XString_length_base(str);
    // 返回最后一个字符（索引为长度-1）
    return XString_at(str, len - 1);
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

bool XString_append(XString* str, const XString* app_str)
{
    if (!str || !app_str) return false;

    // 获取待追加字符串的长度
    size_t app_len = XString_length_base(app_str);
    if (app_len == 0) return true; // 空字符串直接返回成功

    XString_detach(str); // 确保当前字符串可修改
    size_t current_size = XString_length_base(str);
    size_t new_size = current_size + app_len;

    // 预留足够空间（包含终止符）
    XString_reserve(str, new_size);

    // 获取源数据和目标缓冲区指针
    const XChar* app_data = XString_cdata(app_str);
    XChar* dest_data = XString_data(str) + current_size;

    // 复制数据
    memcpy(dest_data, app_data, app_len * sizeof(XChar));

    // 更新长度和终止符
    XContainerSize(str) = new_size;
    XString_data(str)[new_size] = (XChar){ 0 };

    // 清除缓存（内容已修改）
    XString_deinitCache(str);
    return true;
}

bool XString_append_utf8(XString* str, const char* utf8_str)
{
    if (!str || !utf8_str) return false;

    // 先计算需要转换的XChar数量（不含终止符）
    int64_t xchar_count = XChar_from_utf8_stream((const uint8_t*)utf8_str,0, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    size_t current_size = XString_length_base(str);
    size_t new_size = current_size + (size_t)xchar_count;

    // 预留足够空间（包含终止符）
    XString_reserve(str, new_size);

    // 直接转换到目标缓冲区
    XChar* data = XString_data(str);
    int64_t result = XChar_from_utf8_stream(
        (const uint8_t*)utf8_str,
        0,
        data + current_size,  // 直接写到当前字符串末尾
        (size_t)xchar_count + 1  // 包含终止符的空间（实际不会覆盖原终止符）
    );

    if (result <= 0) {
        // 转换失败时恢复原长度和终止符
        XContainerSize(str) = current_size;
        data[current_size]=0;
        return false;
    }

    // 更新字符串长度和终止符
    XContainerSize(str) = new_size;
    data[new_size]=0;

    XString_deinitCache(str);
    return true;
}

bool XString_append_with_length_utf8(XString* str, const char* utf8_str, size_t len)
{
    if (!str || !utf8_str) return false;

    // 先计算需要转换的XChar数量（不含终止符）
    int64_t xchar_count = XChar_from_utf8_stream((const uint8_t*)utf8_str, len, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    size_t current_size = XString_length_base(str);
    size_t new_size = current_size + (size_t)xchar_count;

    // 预留足够空间（包含终止符）
    XString_reserve(str, new_size);

    // 直接转换到目标缓冲区
    XChar* data = XString_data(str);
    int64_t result = XChar_from_utf8_stream(
        (const uint8_t*)utf8_str,
        len,
        data + current_size,  // 直接写到当前字符串末尾
        (size_t)xchar_count + 1  // 包含终止符的空间（实际不会覆盖原终止符）
    );

    if (result <= 0) {
        // 转换失败时恢复原长度和终止符
        XContainerSize(str) = current_size;
        data[current_size] = 0;
        return false;
    }

    // 更新字符串长度和终止符
    XContainerSize(str) = new_size;
    data[new_size] = 0;

    XString_deinitCache(str);
    return true;
}

bool XString_append_char(XString* str, XChar ch)
{
    if (!str) return false;

    XString_detach(str);
    size_t new_size = XString_length_base(str) + 1;
    XString_reserve(str, new_size);

    XChar* data = XString_data(str);
    data[XString_length_base(str)] = ch;

    XContainerSize(str) = new_size;
    XString_data(str)[new_size] = 0;

    XString_deinitCache(str);
    return true;
}

bool XString_assign(XString* str, const XString* ass_str)
{
    if (!str || !ass_str) return false;

    // 如果是自我赋值，直接返回成功
    if (str == ass_str) return true;

    // 清空目标字符串
    XString_clear_base(str);

    // 如果源字符串为空，直接返回成功
    if (XString_isEmpty_base(ass_str)) return true;

    // 分离共享数据，确保可修改
    XString_detach(str);

    // 获取源字符串长度和数据
    size_t ass_len = XString_length_base(ass_str);
    const XChar* ass_data = XString_cdata(ass_str);

    // 预留足够空间（包含终止符）
    if (!XString_reserve(str, ass_len)) 
    {
        return false; // 内存分配失败
    }

    // 复制数据到目标字符串
    XChar* dest_data = XString_data(str);
    memcpy(dest_data, ass_data, ass_len * sizeof(XChar));

    // 更新长度和终止符
    XContainerSize(str) = ass_len;
    dest_data[ass_len]=0;

    // 清除缓存（内容已修改）
    XString_deinitCache(str);
    return true;
}

bool XString_assign_utf8(XString* str, const char* utf8_str)
{
    return XString_assign_with_length_utf8(str,utf8_str,0);
}

bool XString_assign_with_length_utf8(XString* str, const char* utf8_str, size_t len)
{
    if (!str) return false;

    XString_clear_base(str);
    if (!utf8_str || *utf8_str == '\0') return true;

    // 先计算需要转换的XChar数量（不含终止符）
    int64_t xchar_count = XChar_from_utf8_stream((const uint8_t*)utf8_str, len, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    // 预留足够空间（包含终止符）
    XString_reserve(str, (size_t)xchar_count);

    // 直接转换到目标缓冲区
    XChar* data = XString_data(str);
    int64_t result = XChar_from_utf8_stream(
        (const uint8_t*)utf8_str,
        len,
        data,
        (size_t)xchar_count + 1  // 包含终止符的空间
    );

    if (result <= 0) {
        // 转换失败时保持清空状态
        XContainerSize(str) = 0;
        data[0] = 0;
        return false;
    }

    // 设置长度和终止符
    XContainerSize(str) = (size_t)xchar_count;
    data[xchar_count] = 0;

    XString_deinitCache(str);
    return true;
}

bool XString_assign_fmt_utf8(XString* str, const char* utf8_format, ...)
{
    // 参数校验：目标字符串或格式字符串为空则直接返回失败
    if (!str || !utf8_format) {
        return false;
    }

    // 第一步：计算格式化字符串所需的缓冲区大小
    va_list args;
    va_start(args, utf8_format);
    // 使用vsnprintf计算所需长度（第一个参数为NULL时仅返回长度）
    int fmt_len = vsnprintf(NULL, 0, utf8_format, args);
    va_end(args);

    // 格式化失败或结果为空时，清空目标字符串并返回成功
    if (fmt_len <= 0) {
        XString_clear_base(str);
        return true;
    }

    // 第二步：分配临时缓冲区并执行格式化
    // +1 用于存储字符串终止符'\0'
    char* utf8_buf = (char*)XMemory_malloc(fmt_len + 1);
    if (!utf8_buf) {
        return false; // 内存分配失败
    }

    // 重新初始化可变参数列表并执行实际格式化
    va_start(args, utf8_format);
    vsnprintf(utf8_buf, fmt_len + 1, utf8_format, args);
    va_end(args);

    // 第三步：将格式化后的UTF-8字符串赋值给目标XString
    // 利用已实现的XString_assign_utf8完成实际赋值（包含编码转换）
    bool success = XString_assign_utf8(str, utf8_buf);

    // 释放临时缓冲区（无论成功与否都需要释放）
    XMemory_free(utf8_buf);

    return success;
}

bool XString_prepend(XString* str, const XString* pre_str)
{
    if (!str || !pre_str) return false;

    // 待前置字符串为空时直接返回成功
    size_t pre_len = XString_length_base(pre_str);
    if (pre_len == 0) return true;

    // 如果原字符串为空，直接赋值
    if (XString_isEmpty_base(str))
    {
        return XString_assign(str, pre_str);
    }

    XString_detach(str);

    // 计算新长度并预留空间
    size_t current_len = XString_length_base(str);
    size_t new_len = current_len + pre_len;
    if (!XString_reserve(str, new_len)) 
    {
        return false; // 内存分配失败
    }

    // 获取数据指针
    XChar* data = XString_data(str);
    const XChar* pre_data = XString_cdata(pre_str);

    // 移动原有数据，为前置内容腾出空间
    memmove(data + pre_len, data, current_len * sizeof(XChar));

    // 复制前置内容到开头
    memcpy(data, pre_data, pre_len * sizeof(XChar));

    // 更新长度和终止符
    XContainerSize(str) = new_len;
    data[new_len]=0;

    // 清除缓存（内容已修改）
    XString_deinitCache(str);
    return true;
}

bool XString_prepend_utf8(XString* str, const char* utf8_str) 
{
    if (!str || !utf8_str) return false;

    XString* original = XString_create_copy(str);
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

bool XString_insert(XString* str, size_t pos, const XString* in_str)
{
    if (!str || !in_str) return false;

    // 待插入字符串为空时直接返回成功
    size_t in_len = XString_length_base(in_str);
    if (in_len == 0) return true;

    // 处理插入位置（确保不超过原字符串长度）
    size_t original_len = XString_length_base(str);
    pos = (pos > original_len) ? original_len : pos;

    // 分离共享数据，确保可修改
    XString_detach(str);

    // 计算新长度并预留足够空间（包含终止符）
    size_t new_len = original_len + in_len;
    if (!XString_reserve(str, new_len)) {
        return false; // 内存分配失败
    }

    // 获取数据指针
    XChar* data = XString_data(str);
    const XChar* in_data = XString_cdata(in_str);

    // 移动原有数据，为插入内容腾出空间
    if (original_len > pos) {
        memmove(data + pos + in_len, data + pos, (original_len - pos) * sizeof(XChar));
    }

    // 复制插入内容到指定位置
    memcpy(data + pos, in_data, in_len * sizeof(XChar));

    // 更新长度和终止符
    XContainerSize(str) = new_len;
    data[new_len]=0;

    // 清除缓存（内容已修改）
    XString_deinitCache(str);
    return true;
}

bool XString_insert_utf8(XString* str, size_t pos, const char* utf8_str) 
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
    XString_data(str)[new_size]=0;

    XString_deinitCache(str);

    XString_delete_base(insert_str);
    return true;
}

bool XString_remove_base(XString* str, size_t pos, size_t len) {
    if (!str) return false;
    return XClassGetVirtualFunc(str, EXString_Remove, bool (*)(XString*, size_t, size_t))(str, pos, len);
}

void XString_erase_base(XString* str, const XString_iterator* it, XString_iterator* next)
{
    if (!str||!it) return ;
    XClassGetVirtualFunc(str, EXString_Erase, bool (*)(XString*, const XString_iterator*,XString_iterator*))(str, it, next);
}

bool XString_replace(XString* str, const XString* before, const XString* after, XCharCaseSensitivity cs)
{
    if (!str || !before || !after) return false;

    size_t before_len = XString_length_base(before);
    if (before_len == 0) return false; // 不处理空替换串

    size_t after_len = XString_length_base(after);
    const XChar* before_data = XString_cdata(before);
    const XChar* text = XString_cdata(str);
    size_t text_len = XString_length_base(str);

    // 预计算前缀表（KMP算法）
    int* prefix = (int*)XMemory_malloc(before_len * sizeof(int));
    if (!prefix) return false;
    compute_prefix(before_data, before_len, prefix, cs);

    // 分离共享数据，确保可修改
    XString_detach(str);

    int64_t pos = 0;
    bool replaced = false;

    // 循环查找并替换所有匹配
    while (pos != -1) {
        // 在当前位置继续查找
        pos = kmp_search(text, text_len, before_data, before_len, prefix, cs, (size_t)pos);
        if (pos == -1) break;

        // 执行替换操作
        // 1. 移除原子串
        if (!XString_remove_base(str, (size_t)pos, before_len)) 
        {
            XMemory_free(prefix);
            return false;
        }

        // 2. 插入新子串
        if (!XString_insert(str, (size_t)pos, after)) 
        {
            XMemory_free(prefix);
            return false;
        }

        replaced = true;
        // 更新文本数据和长度（替换后字符串已变化）
        text = XString_cdata(str);
        text_len = XString_length_base(str);
        // 计算下一次查找起始位置
        pos += after_len;
    }

    XMemory_free(prefix);
    return replaced; // 即使没有替换也返回true（操作成功但无匹配）
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
        if (!XString_remove_base(str, (size_t)pos, before_len)) break;
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
void compute_prefix(const XChar* pattern, size_t m, int* prefix, XCharCaseSensitivity cs) 
{
    prefix[0] = 0;
    int len = 0;  // 当前最长前缀后缀长度

    for (size_t i = 1; i < m; ) {
        if (XChar_equals(pattern[i],pattern[len], cs)) {
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
int64_t kmp_search(const XChar* text, size_t n,
    const XChar* pattern, size_t m,
    const int* prefix, XCharCaseSensitivity cs,
    size_t from)
{
    if (m == 0) return from <= n ? (int64_t)from : -1;  // 空模式串处理
    if (from >= n || m > n) return -1;  // 起始位置越界或模式串更长

    int i = (int)from;  // 主串索引
    int j = 0;          // 模式串索引

    while (i < (int)n) {
        if (XChar_equals(text[i], pattern[j], cs)) {
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
int64_t kmp_reverse_search(const XChar* text, size_t n, const XChar* pattern, size_t m,
    XCharCaseSensitivity cs, size_t start_idx)
{
    // 计算前缀表（正向匹配用，与子串方向一致）
    int* prefix = (int*)XMemory_malloc(m * sizeof(int));
    if (!prefix) return -1;
    compute_prefix(pattern, m, prefix, cs);

    int i = (int)start_idx;  // 主串当前起始位置（从start_idx开始）
    int j = 0;               // 模式串匹配进度（从0开始，正向匹配）

    // 从start_idx向前搜索，直到主串起始位置>=0
    while (i >= 0) {
        // 检查当前主串位置(i+j)与模式串位置j是否匹配
        if (XChar_equals(text[i + j], pattern[j], cs)) {
            j++;
            if (j == (int)m) {
                // 找到完整匹配，返回起始索引i
                XMemory_free(prefix);
                return i;
            }
        }
        else {
            if (j > 0) {
                // 利用前缀表回溯模式串（减少重复比较）
                j = prefix[j - 1];
            }
            else {
                // j=0时不匹配，主串起始位置向前移一位
                i--;
            }
        }
    }

    // 未找到匹配
    XMemory_free(prefix);
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
    // 空指针检查
    if (!str || !substr) return -1;

    // 获取字符串长度
    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr);

    // 子串为空的特殊处理（返回from对应的实际索引）
    if (substr_len == 0) {
        // from=0对应最后一个字符的下一个位置（插入点），与标准库行为一致
        size_t pos = (from >= str_len) ? 0 : (str_len - from);
        return (int64_t)pos;
    }

    // 子串长于主串时无匹配可能
    if (substr_len > str_len) {
        return -1;
    }

    // 计算子串可匹配的最大起始索引（主串中能放下子串的最左位置）
    size_t max_start = str_len - substr_len;

    // 将from（尾部偏移）转换为主串实际索引（头部偏移）
    // from=0 → 主串最后一个字符位置（str_len-1），但需确保子串能放下
    size_t start_idx;
    if (from >= str_len) {
        // from超出主串长度，从主串最左侧可匹配位置开始
        start_idx = max_start;
    }
    else {
        // 从尾部偏移from对应的位置开始，不超过max_start
        start_idx = (str_len - 1 - from);
        if (start_idx > max_start) {
            start_idx = max_start; // 确保子串能完整放下
        }
    }

    // 获取字符数据指针
    const XChar* text = XString_cdata(str);
    const XChar* pattern = XString_cdata(substr);

    // 调用修正后的KMP反向搜索
    return kmp_reverse_search(text, str_len, pattern, substr_len, cs, start_idx);
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

    // 计算子串可匹配的最大起始索引（主串中能放下子串的最左位置）
    size_t max_start = str_len - substr_len;

    // 将from（尾部偏移）转换为主串实际索引（头部偏移）
    // from=0 → 主串最后一个字符位置（str_len-1），但需确保子串能放下
    size_t start_idx;
    if (from >= str_len) {
        // from超出主串长度，从主串最左侧可匹配位置开始
        start_idx = max_start;
    }
    else {
        // 从尾部偏移from对应的位置开始，不超过max_start
        start_idx = (str_len - 1 - from);
        if (start_idx > max_start) {
            start_idx = max_start; // 确保子串能完整放下
        }
    }

    const XChar* text = XString_cdata(str);
    const XChar* pattern = XString_cdata(substr_str);

    // 执行反向KMP搜索（使用转换后的start_idx）
    int64_t result = kmp_reverse_search(text, str_len, pattern, substr_len, cs, start_idx);

    XString_delete_base(substr_str);
    return result;
}

bool XString_contains(const XString* str, const XString* substr, XCharCaseSensitivity cs)
{
    if (!str || !substr) return false;

    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr);

    // 子串为空时，任何字符串都视为包含空串
    if (substr_len == 0) return true;
    // 子串长于源字符串时，不可能包含
    if (substr_len > str_len) return false;

    const XChar* text = XString_cdata(str);       // 源字符串的XChar数组
    const XChar* pattern = XString_cdata(substr); // 子串的XChar数组

    // 分配KMP算法所需的前缀表
    int* prefix = (int*)XMemory_malloc(substr_len * sizeof(int));
    if (!prefix) return false;

    // 计算前缀表（用于KMP搜索）
    compute_prefix(pattern, substr_len, prefix, cs);

    // 执行KMP搜索，从位置0开始查找
    int64_t found_pos = kmp_search(text, str_len, pattern, substr_len, prefix, cs, 0);

    // 释放前缀表内存
    XMemory_free(prefix);

    // 找到匹配位置则返回true
    return found_pos != -1;
}

bool XString_contains_utf8(const XString* str, const char* utf8_substr, XCharCaseSensitivity cs)
{
    if (!utf8_substr) return false;

    // 将UTF-8子串转换为XString以便统一处理
    XString* substr = XString_create_utf8(utf8_substr);
    if (!substr) return false;

    // 调用核心实现
    bool result = XString_contains(str, substr, cs);

    // 释放临时创建的XString
    XString_delete_base(substr);
    return result;
}

int32_t XString_compare(const XString* str1, const XString* str2) 
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
        if (data1[i] < data2[i]) return -1;
        if (data1[i] > data2[i]) return 1;
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
        if (!XChar_equals(data1[i], data2[i], cs)) {
            return false;
        }
    }

    return true;
}

bool XString_starts_with(const XString* str, const XString* prefix, XCharCaseSensitivity cs) {
    if (!str || !prefix) return false;

    size_t str_len = XString_length_base(str);
    size_t prefix_len = XString_length_base(prefix);

    // 前缀长度大于字符串长度时直接返回false
    if (prefix_len > str_len) return false;

    // 前缀为空字符串时默认匹配成功
    if (prefix_len == 0) return true;

    const XChar* str_data = XString_cdata(str);
    const XChar* prefix_data = XString_cdata(prefix);

    // 逐个字符比较前缀
    for (size_t i = 0; i < prefix_len; i++) {
        if (!XChar_equals(str_data[i], prefix_data[i], cs)) {
            return false;
        }
    }

    return true;
}
const bool XLess_XString(const XString* str1, const XString* str2)
{
    return XString_compare(str1, str2) <0;
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

bool XString_ends_with(const XString* str, const XString* suffix, XCharCaseSensitivity cs) {
    if (!str || !suffix) return false;

    size_t str_len = XString_length_base(str);
    size_t suffix_len = XString_length_base(suffix);

    // 后缀长度大于字符串长度时直接返回false
    if (suffix_len > str_len) return false;

    // 空后缀默认匹配任何字符串的后缀
    if (suffix_len == 0) return true;

    const XChar* str_data = XString_cdata(str);
    const XChar* suffix_data = XString_cdata(suffix);

    // 从字符串末尾开始比较后缀长度的字符
    size_t start_pos = str_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        if (!XChar_equals(str_data[start_pos + i], suffix_data[i], cs)) {
            return false;
        }
    }

    return true;
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

bool XString_isLower(const XString* str)
{
    if (!str || XString_isEmpty_base(str)) {
        return false; // 空字符串返回false
    }

    size_t len = XString_length_base(str);
    const XChar* chars = XString_cdata(str);
    bool has_letter = false; // 标记是否存在可大小写转换的字母

    for (size_t i = 0; i < len; ++i) {
        const XChar ch = chars[i];
        if (XChar_is_letter(ch)) { // 仅检查字母字符
            has_letter = true;
            if (!XChar_is_lower(ch)) { // 发现大写字母则直接返回false
                return false;
            }
        }
        // 忽略非字母字符（数字、符号等不影响结果）
    }

    // 必须至少包含一个字母字符且全为小写
    return has_letter;
}

bool XString_isUpper(const XString* str)
{
    if (!str || XString_isEmpty_base(str)) {
        return false; // 空字符串返回false
    }

    size_t len = XString_length_base(str);
    const XChar* chars = XString_cdata(str);
    bool has_letter = false; // 标记是否存在可大小写转换的字母

    for (size_t i = 0; i < len; ++i) {
        const XChar ch = chars[i];
        // 跳过代理字符（已在XChar层级处理完整字符）
        if (XChar_is_surrogate(ch)) {
            continue;
        }
        if (XChar_is_letter(ch)) { // 仅检查字母字符
            has_letter = true;
            if (!XChar_is_upper(ch)) { // 发现小写字母则直接返回false
                return false;
            }
        }
        // 忽略非字母字符（数字、符号等不影响结果）
    }

    // 必须至少包含一个字母字符且全为大写
    return has_letter;
}

bool XString_isNull(const XString* str)
{
    // 指针为NULL直接判定为null
    if (!str) 
    {
        return true;
    }

    // 检查内部数据指针是否未初始化（根据XContainerObject结构特性）
    // 结合XString_init逻辑，未初始化的对象其数据指针可能为NULL
    if (XContainerDataPtr(str) == NULL) {
        return true;
    }

    // 检查引用计数是否异常（未初始化的对象可能无引用计数）
    if (str->m_ref_count == NULL) {
        return true;
    }

    return false;
}

bool XString_isValidUtf16(const XString* str)
{
    if (!str || XString_isNull(str)) {
        return false; // 空指针或未初始化字符串视为无效
    }

    size_t len = XString_length_base(str);
    const XChar* chars = XString_cdata(str);

    for (size_t i = 0; i < len; ++i) {
        const XChar* ch = &chars[i];
        // 检查高代理字符
        if (XChar_is_high_surrogate(ch)) {
            // 高代理后面必须跟一个低代理，且不能是最后一个字符
            if (i + 1 >= len || !XChar_is_low_surrogate(chars[i + 1])) {
                return false;
            }
            i++; // 跳过下一个低代理，避免重复检查
        }
        // 检查孤立的低代理字符（前面没有高代理）
        else if (XChar_is_low_surrogate(ch)) {
            return false;
        }
        // 其他字符无需特殊处理（包括基本平面字符）
    }

    return true;
}

bool XString_isRightToLeft(const XString* str)
{
    if (!str || XString_isEmpty_base(str)) {
        return false; // 空字符串或无效指针返回false
    }

    size_t len = XString_length_base(str);
    const XChar* chars = XString_cdata(str);

    // 遍历字符串中的每个字符，检查是否包含强RTL（从右到左）字符
    for (size_t i = 0; i < len; ++i) {
        const XChar* ch = &chars[i];
        uint16_t code = ch;

        // 检查是否属于强RTL字符范围（参考Unicode标准）
        // 希伯来语及相关：0x0590-0x05FF
        // 阿拉伯语及相关：0x0600-0x06FF
        // 叙利亚语：0x0700-0x074F
        // Thaana（马尔代夫语）：0x0780-0x07BF
        // 阿拉伯语呈现形式：0xFB1D-0xFDFF、0xFE70-0xFEFF
        if ((code >= 0x0590 && code <= 0x05FF) ||
            (code >= 0x0600 && code <= 0x06FF) ||
            (code >= 0x0700 && code <= 0x074F) ||
            (code >= 0x0780 && code <= 0x07BF) ||
            (code >= 0xFB1D && code <= 0xFDFF) ||
            (code >= 0xFE70 && code <= 0xFEFF)) {
            return true; // 发现RTL字符，立即返回true
        }
    }

    return false; // 未发现任何RTL字符
}

XString* XString_toLower(const XString* str) {
    if (!str) return NULL;

    XString* result = XString_create_copy(str);
    XString_detach(result);  // 确保可修改

    XChar* data = XString_data(result);
    for (size_t i = 0; i < XString_length_base(result); i++) {
        data[i] = XChar_to_lower(data[i]);
    }
    data[XString_length_base(result)] = (XChar){ 0 };  // 更新结束符

    XString_deinitCache(result);
    return result;
}

XString* XString_toUpper(const XString* str) {
    if (!str) return NULL;

    XString* result = XString_create_copy(str);
    XString_detach(result);  // 确保可修改

    XChar* data = XString_data(result);
    for (size_t i = 0; i < XString_length_base(result); i++) {
        data[i] = XChar_to_upper(data[i]);
    }
    data[XString_length_base(result)] = (XChar){ 0 };  // 更新结束符

    XString_deinitCache(result);
    return result;
}

XString* XString_trimmed(const XString* str) {
    if (!str || XString_isEmpty_base(str)) return XString_create_copy(str);

    const XChar* data = XContainerDataPtr(str);
    size_t start = 0;
    size_t end = XString_length_base(str) - 1;

    // 跳过前导空白
    while (start <= end && XChar_is_space(data[start])) start++;
    // 跳过后导空白
    while (end >= start && XChar_is_space(data[end])) end--;

    if (start > end) return XString_create();  // 全是空白
    return XString_mid(str, start, end - start + 1);
}

unsigned short XString_toUShort(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    // 无需提前设置ok为false，后续成功时再赋值

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    unsigned long val = strtoul(utf8, &endptr, base);

    // 额外检查转换有效性：必须有有效字符、完全转换且值在unsigned short范围内
    const bool success = (endptr != utf8 && *endptr == '\0' && val <= USHRT_MAX);
    if (ok) *ok = success;
    return success ? (unsigned short)val : 0;
}

unsigned int XString_toUInt(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }

    const char* utf8 = XString_toUtf8(str);
    char* endptr;
    unsigned long val = strtoul(utf8, &endptr, base);

    const bool success = (endptr != utf8 && *endptr == '\0' && val <= UINT_MAX);
    if (ok) *ok = success;
    return success ? (unsigned int)val : 0;
}

short XString_toShort(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    return XChar_to_short_stream(XContainerDataPtr(str),XContainerSize(str),base,ok);
}

int XString_toInt(const XString* str, bool* ok, int base) {
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    return XChar_to_int_stream(XContainerDataPtr(str), XContainerSize(str), base, ok);
}

long XString_toLong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    return XChar_to_long_stream(XContainerDataPtr(str), XContainerSize(str), base, ok);
}

long long XString_toLongLong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    return XChar_to_longlong_stream(XContainerDataPtr(str), XContainerSize(str), base, ok);
}

unsigned long XString_toULong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    return XChar_to_ulong_stream(XContainerDataPtr(str), XContainerSize(str), base, ok);
}

unsigned long long XString_toULongLong(const XString* str, bool* ok, int base)
{
    if (!str) {
        if (ok) *ok = false;
        return 0;
    }
    return XChar_to_ulonglong_stream(XContainerDataPtr(str), XContainerSize(str), base, ok);
}

float XString_toFloat(const XString* str, bool* ok)
{
    if (!str) {
        if (ok) *ok = false;
        return 0.0f;
    }
    return XChar_to_float_stream(XContainerDataPtr(str), XContainerSize(str), ok);
}

double XString_toDouble(const XString* str, bool* ok) {
    if (!str) {
        if (ok) *ok = false;
        return 0.0;
    }
    return XChar_to_double_stream(XContainerDataPtr(str), XContainerSize(str), ok);
}

bool XString_setNum_int(XString* str, int n, int base)
{
    if (!str || base < 2 || base > 36) {  // 补充str空指针检查
        return false;
    }
    int64_t len=XChar_from_int_stream(n,base,NULL,0,true);
    if (len == -1)
        return false;
    XString_resize(str,len);
    return XChar_from_int_stream(n, base, XString_data(str), len+1, true)!=-1;
}

bool XString_setNum_uInt(XString* str, unsigned int n, int base)
{
    if (!str || base < 2 || base > 36) {  // 补充str空指针检查
        return false;
    }
    int64_t len = XChar_from_uint_stream(n, base, NULL, 0, true);
    if (len == -1)
        return false;
    XString_resize(str, len);
    return XChar_from_uint_stream(n, base, XString_data(str), len + 1, true) != -1;
}

bool XString_setNum_long(XString* str, long n, int base)
{
    if (!str || base < 2 || base > 36) {  // 补充str空指针检查
        return false;
    }
    int64_t len = XChar_from_long_stream(n, base, NULL, 0, true);
    if (len == -1)
        return false;
    XString_resize(str, len);
    return XChar_from_long_stream(n, base, XString_data(str), len + 1, true) != -1;
}

bool XString_setNum_uLong(XString* str, unsigned long n, int base)
{
    if (!str || base < 2 || base > 36) {  // 补充str空指针检查
        return false;
    }
    int64_t len = XChar_from_ulong_stream(n, base, NULL, 0, true);
    if (len == -1)
        return false;
    XString_resize(str, len);
    return XChar_from_ulong_stream(n, base, XString_data(str), len + 1, true) != -1;
}

bool XString_setNum_llong(XString* str, long long n, int base)
{
    if (!str || base < 2 || base > 36) {  // 补充str空指针检查
        return false;
    }
    int64_t len = XChar_from_ulonglong_stream(n, base, NULL, 0, true);
    if (len == -1)
        return false;
    XString_resize(str, len);
    return XChar_from_ulonglong_stream(n, base, XString_data(str), len + 1, true) != -1;
}

bool XString_setNum_uLLong(XString* str, unsigned long long n, int base)
{
    if (!str || base < 2 || base > 36) {  // 补充str空指针检查
        return false;
    }
    int64_t len = XChar_from_ulonglong_stream(n, base, NULL, 0, true);
    if (len == -1)
        return false;
    XString_resize(str, len);
    return XChar_from_ulonglong_stream(n, base, XString_data(str), len + 1, true) != -1;
}

bool XString_setNum_float(XString* str, float f, char format, int precision)
{
    if (!str ) { 
        return false;
    }
    int64_t len = XChar_from_float_stream((double)f,format,NULL,0,precision);
    if (len == -1)
        return false;
    XString_resize(str, len);
    return XChar_from_float_stream((double)f, format, XString_data(str), len + 1, precision) != -1;
    // 转换为double处理，避免精度损失
    return XString_setNum_double(str, (double)f, format, precision);
}

bool XString_setNum_double(XString* str, double d, char format, int precision)
{
    if (!str) {
        return false;
    }
    int64_t len = XChar_from_double_stream(d, format, NULL, 0, precision);
    if (len == -1)
        return false;
    XString_resize(str, len);
    return XChar_from_double_stream(d, format, XString_data(str), len + 1, precision) != -1;
}

XString* XString_left(const XString* str, size_t n)
{
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
bool XString_reserve(XString* str, size_t capacity)
{
    if (!str || capacity <= XContainerCapacity(str)) 
        return false;

    //XString_detach(str);  // 确保可修改
    // 新容量，且不小于最小容量
    size_t new_capacity = (capacity < XSTRING_MIN_CAPACITY) ? XSTRING_MIN_CAPACITY : capacity;
    XChar* new_data = NULL;
    // 实际需要容量= 新容量 + 1（结束符）
    if (XString_cdata(str) == NULL || XAtomic_load_int32(str->m_ref_count, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        new_data = XMemory_malloc((new_capacity + 1) * sizeof(XChar));
        if (XString_cdata(str))
        {
            memcpy(new_data, XString_cdata(str),(XString_length_base(str)+1)*sizeof(XChar));
        }
        if (XAtomic_load_int32(str->m_ref_count, XAtomic_MemoryOrder_Relaxed) > 1)
        {
            // 原子减少原引用计数（若减到0，原数据会被其他持有者释放）
            int32_t old_ref = XAtomic_fetch_sub_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);
            // 为新数据创建独立的引用计数（初始化为1）
            XAtomic_int32_t* new_ref_count = (XAtomic_int32_t*)XMemory_malloc(sizeof(XAtomic_int32_t));
            if (!new_ref_count) {
                XMemory_free(new_data);
                // 恢复原引用计数（拷贝失败需回滚）
                XAtomic_fetch_add_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);
                return;
            }
            XAtomic_store_int32(new_ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子初始化新计数
            str->m_ref_count = new_ref_count;
        }
    }
    else
    {
        new_data = (XChar*)XMemory_realloc(XString_data(str), (new_capacity + 1) * sizeof(XChar));
    }
    
    if (new_data) 
    {
        XContainerDataPtr(str)=new_data;
        XContainerCapacity(str)=new_capacity;
        // 设置结束符（当前有效长度位置）
        new_data[XString_length_base(str)] = 0;
    }
    return new_data != NULL;
}

void XString_resize(XString* str, size_t size)
{
    if (!str) return;

    // 如果当前长度与目标长度相同，无需操作
    size_t current_size = XString_length_base(str);
    if (current_size == size) {
        return;
    }

    XString_detach(str); // 确保拥有独立数据，避免影响共享实例

    if (size > current_size) {
        // 扩展字符串：预留空间并填充空字符
        XString_reserve(str, size);
        XChar* data = XString_data(str);
        // 从当前末尾到新长度的位置填充空字符
        for (size_t i = current_size; i < size; ++i) {
            data[i] = XChar_from(0); // 空字符初始化
        }
    }
    else {
        // 缩短字符串：直接调整长度并设置新的终止符
        // （无需修改原有数据，仅调整长度标记）
    }

    // 更新长度并设置终止符（无论扩展还是缩短）
    XContainerSize(str) = size;
    XString_data(str)[size]=0; // 确保终止符存在

    XString_deinitCache(str); // 失效缓存，因为内容已改变
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

//拷贝字符串后返回
char* XStrdup(char* str)
{
    size_t len = strlen(str);
    if (!len)return NULL;
    char* s = XMemory_malloc(len + 1);
    memcpy(s, str, len + 1);
    return s;
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
    if (!str/*|| !str->m_is_shared*/) return;

    // 1. 空字符串无需处理（无数据可共享）
    if (!XContainerDataPtr(str)) {
       // str->m_is_shared = false;
        return;
    }

    // 2. 无共享引用时，直接标记为可修改，无需拷贝
    //    （引用计数为1说明没有其他持有者）
    if (XAtomic_load_int32(str->m_ref_count, XAtomic_MemoryOrder_Relaxed) == 1) {
       // str->m_is_shared = false;
        return;
    }

    // 3. 存在共享引用，必须拷贝数据（真正的写时复制点）
    size_t curr_size = XContainerSize(str);
    size_t curr_capacity = XContainerCapacity(str);

    // 分配新的缓冲区（保留原有容量，避免后续频繁分配）
    XChar* new_data = (XChar*)XMemory_malloc((curr_capacity + 1) * sizeof(XChar));
    if (!new_data) return; // 内存分配失败（可根据需求添加错误处理）

    // 复制现有数据（包含终止符）
    memcpy(new_data, XContainerDataPtr(str), (curr_size + 1) * sizeof(XChar));

    // 原子减少原引用计数（若减到0，原数据会被其他持有者释放）
    int32_t old_ref = XAtomic_fetch_sub_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);
    // 为新数据创建独立的引用计数（初始化为1）
    XAtomic_int32_t* new_ref_count = (XAtomic_int32_t*)XMemory_malloc(sizeof(XAtomic_int32_t));
    if (!new_ref_count) {
        XMemory_free(new_data);
        // 恢复原引用计数（拷贝失败需回滚）
        XAtomic_fetch_add_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);
        return;
    }
    XAtomic_store_int32(new_ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子初始化新计数
    // 替换数据指针，标记为非共享
    XContainerDataPtr(str) = new_data;
    str->m_ref_count = new_ref_count;

    // 缓存失效（数据已变更）
    XString_deinitCache(str);
}

void XString_deinitCache(XString* str)
{
    if (str == NULL || str->m_cache == NULL)
        return;
    char* local = str->m_cache[0].m_data;
    for (size_t i = 1; i < XStringCache_Size; i++)
    {
        if (str->m_cache[i].m_data == NULL)
            continue;
        XMemory_free(str->m_cache[i].m_data);
        if (str->m_cache[i].m_data == local)
            local = NULL;
        str->m_cache[i].m_data = NULL;
        str->m_cache[i].m_length = 0;
    }
    if (local != NULL)
    {
        XMemory_free(local);
    }
    str->m_cache[0].m_data = NULL;
    str->m_cache[0].m_length = 0;
}

void XString_initCache(XString* str)
{
    if (str == NULL || str->m_cache != NULL)
        return;
    str->m_cache=XMemory_calloc(XStringCache_Size,sizeof(XStringCache));
}
