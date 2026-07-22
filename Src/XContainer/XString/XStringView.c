/**
* @file XStringView.c
* @brief 字符串视图实现文件（对标 Qt 6.8 QStringView）
* @details 实现 XStringView 的所有 API，包括构造、访问、子视图、
*          查找、比较、数值转换等操作。
* @note XStringView 是值类型，不分配堆内存，不涉及虚函数表。
*/
#include "XStringView.h"
#if XString_ON
#include "XString.h"           ///< XString 字符串容器，用于从 XString 构造视图和 toString
#include <string.h>            ///< memcmp、memchr 等内存操作
#include <stdlib.h>            ///< strtol、strtoll、strtoul、strtoull、strtod 等数值转换
#include <wchar.h>             ///< wmemchr 宽字符查找
#include <stdio.h>
#include <limits.h>             ///< snprintf 格式化

/* ============================== 内部辅助函数 ============================== */

/**
* @brief 判断 XChar 是否为空白字符
* @details 空白字符包括：空格(U+0020)、制表符(U+0009)、换行符(U+000A)、
*          回车符(U+000D)、垂直制表符(U+000B)、换页符(U+000C)。
* @param ch 待判断的 XChar 字符
* @return true 为空白字符，false 为非空白字符
*/
static bool is_white_space_xchar(XChar ch)
{
    return (ch == 0x0020 || ch == 0x0009 || ch == 0x000A ||
            ch == 0x000D || ch == 0x000B || ch == 0x000C);
}

/**
* @brief 比较两个 XChar 字符（支持大小写敏感性）
* @param a 第一个字符
* @param b 第二个字符
* @param cs 大小写敏感性（0=不区分，1=区分）
* @return 相等返回 true，否则返回 false
*/
static bool xchar_equal(XChar a, XChar b, int cs)
{
    if (cs == 1) /* XChar_CaseSensitive */
        return a == b;
    /* XChar_CaseInsensitive: 统一转小写后比较 */
    /* 使用 XChar_toLower 如果可用，否则用简单的 ASCII 范围处理 */
    /* 简单处理 ASCII 范围，完整 Unicode 处理依赖 XChar 库 */
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

/**
* @brief 将 XChar 转小写（辅助函数）
* @param ch 输入字符
* @return 小写形式
*/
static XChar xchar_to_lower(XChar ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (XChar)(ch + 32);
    return ch;
}

/**
* @brief 将 UTF-16 视图转换为 UTF-8 C 字符串（用于数值转换）
* @details 将视图内容转换为 NULL 终止的 UTF-8 字符串存储在缓冲区中。
* @param self 视图指针
* @param buf 输出缓冲区
* @param buf_size 缓冲区大小
* @return 转换后的字符串指针，失败返回 NULL
*/
static const char* view_to_utf8(const XStringView* self, char* buf, size_t buf_size)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return NULL;

    /* 对于 ASCII 字符可以直接拷贝 */
    size_t i;
    size_t out_pos = 0;
    for (i = 0; i < (size_t)self->m_size && out_pos + 4 < buf_size; i++)
    {
        uint32_t cp = self->m_data[i];
        if (cp <= 0x7F)
        {
            buf[out_pos++] = (char)cp;
        }
        else if (cp <= 0x7FF)
        {
            buf[out_pos++] = (char)(0xC0 | (cp >> 6));
            buf[out_pos++] = (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            buf[out_pos++] = (char)(0xE0 | (cp >> 12));
            buf[out_pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[out_pos++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    buf[out_pos] = '\0';
    return buf;
}

/* ============================== 构造与创建 ============================== */

XStringView XStringView_create(void)
{
    XStringView view;
    view.m_data = NULL;
    view.m_size = 0;
    return view;
}

XStringView XStringView_create_data(const XChar* data, int64_t len)
{
    XStringView view;
    view.m_data = data;
    view.m_size = (data == NULL) ? 0 : len;
    return view;
}

XStringView XStringView_create_range(const XChar* first, const XChar* last)
{
    XStringView view;
    if (first == NULL || last == NULL || last < first)
    {
        view.m_data = NULL;
        view.m_size = 0;
    }
    else
    {
        view.m_data = first;
        view.m_size = (int64_t)(last - first);
    }
    return view;
}

XStringView XStringView_create_cstr(const XChar* str)
{
    XStringView view;
    if (str == NULL)
    {
        view.m_data = NULL;
        view.m_size = 0;
    }
    else
    {
        /* 计算 XChar 数组长度（直到遇到 0） */
        int64_t len = 0;
        while (str[len] != 0)
            len++;
        view.m_data = str;
        view.m_size = len;
    }
    return view;
}

XStringView XStringView_create_utf16(const uint16_t* data, int64_t len)
{
    XStringView view;
    view.m_data = (const XChar*)data;
    view.m_size = (data == NULL) ? 0 : len;
    return view;
}

XStringView XStringView_create_string(const XString* str)
{
    XStringView view;
    if (str == NULL)
    {
        view.m_data = NULL;
        view.m_size = 0;
    }
    else
    {
        view.m_data = XString_unicode(str);
        view.m_size = (int64_t)XContainer_size_base((const XContainer*)str);
    }
    return view;
}

/* ============================== 基本访问 ============================== */

const XChar* XStringView_data(const XStringView* self)
{
    if (self == NULL)
        return NULL;
    return self->m_data;
}

const XChar* XStringView_constData(const XStringView* self)
{
    return XStringView_data(self);
}

const uint16_t* XStringView_utf16(const XStringView* self)
{
    if (self == NULL)
        return NULL;
    return (const uint16_t*)self->m_data;
}

int64_t XStringView_size(const XStringView* self)
{
    if (self == NULL)
        return 0;
    return self->m_size;
}

bool XStringView_empty(const XStringView* self)
{
    if (self == NULL)
        return true;
    return (self->m_size == 0);
}

bool XStringView_isNull(const XStringView* self)
{
    if (self == NULL)
        return true;
    return (self->m_data == NULL);
}

int64_t XStringView_maxSize(void)
{
    return INT64_MAX - 1;
}

/* ============================== 元素访问 ============================== */

XChar XStringView_at(const XStringView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL || n < 0 || n >= self->m_size)
        return 0;
    return self->m_data[n];
}

XChar XStringView_front(const XStringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[0];
}

XChar XStringView_back(const XStringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[self->m_size - 1];
}

/* ============================== 子视图操作 ============================== */

XStringView XStringView_first_n(const XStringView* self, int64_t n)
{
    XStringView result;
    if (self == NULL || self->m_data == NULL || n <= 0)
    {
        result.m_data = (self != NULL) ? self->m_data : NULL;
        result.m_size = 0;
        return result;
    }
    result.m_data = self->m_data;
    result.m_size = (n > self->m_size) ? self->m_size : n;
    return result;
}

XStringView XStringView_last_n(const XStringView* self, int64_t n)
{
    XStringView result;
    if (self == NULL || self->m_data == NULL || n <= 0)
    {
        result.m_data = (self != NULL) ? self->m_data : NULL;
        result.m_size = 0;
        return result;
    }
    if (n >= self->m_size)
    {
        result.m_data = self->m_data;
        result.m_size = self->m_size;
    }
    else
    {
        result.m_data = self->m_data + (self->m_size - n);
        result.m_size = n;
    }
    return result;
}

XStringView XStringView_sliced(const XStringView* self, int64_t pos)
{
    XStringView result;
    if (self == NULL || self->m_data == NULL)
    {
        result.m_data = NULL;
        result.m_size = 0;
        return result;
    }
    if (pos < 0)
        pos = 0;
    if (pos > self->m_size)
        pos = self->m_size;
    result.m_data = self->m_data + pos;
    result.m_size = self->m_size - pos;
    return result;
}

XStringView XStringView_sliced_2(const XStringView* self, int64_t pos, int64_t n)
{
    XStringView result;
    if (self == NULL || self->m_data == NULL)
    {
        result.m_data = NULL;
        result.m_size = 0;
        return result;
    }
    if (pos < 0)
        pos = 0;
    if (pos > self->m_size)
        pos = self->m_size;
    if (n < 0)
        n = 0;
    if (pos + n > self->m_size)
        n = self->m_size - pos;
    result.m_data = self->m_data + pos;
    result.m_size = n;
    return result;
}

XStringView XStringView_chopped(const XStringView* self, int64_t n)
{
    XStringView result;
    if (self == NULL || self->m_data == NULL)
    {
        result.m_data = NULL;
        result.m_size = 0;
        return result;
    }
    if (n < 0)
        n = 0;
    if (n > self->m_size)
        n = self->m_size;
    result.m_data = self->m_data;
    result.m_size = self->m_size - n;
    return result;
}

/* ============================== 原地修改操作 ============================== */

void XStringView_truncate(XStringView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL)
        return;
    if (n < 0)
        n = 0;
    if (n < self->m_size)
        self->m_size = n;
}

void XStringView_chop(XStringView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL)
        return;
    if (n < 0)
        n = 0;
    if (n > self->m_size)
        n = self->m_size;
    self->m_size -= n;
}

/* ============================== 查找操作 ============================== */

int64_t XStringView_indexOf_char(const XStringView* self, XChar ch, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;

    /* 处理负数 from */
    if (from < 0)
    {
        from = from + self->m_size;
        if (from < 0)
            from = 0;
    }

    if (from >= self->m_size)
        return -1;

    int64_t i;
    for (i = from; i < self->m_size; i++)
    {
        if (xchar_equal(self->m_data[i], ch, cs))
            return i;
    }
    return -1;
}

int64_t XStringView_indexOf(const XStringView* self, const XStringView* substr, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;
    if (substr == NULL || substr->m_data == NULL || substr->m_size <= 0)
        return -1;
    if (substr->m_size > self->m_size)
        return -1;

    /* 处理负数 from */
    if (from < 0)
    {
        from = from + self->m_size;
        if (from < 0)
            from = 0;
    }

    if (from >= self->m_size)
        return -1;

    int64_t max_start = self->m_size - substr->m_size;
    int64_t i, j;
    for (i = from; i <= max_start; i++)
    {
        bool match = true;
        for (j = 0; j < substr->m_size; j++)
        {
            if (!xchar_equal(self->m_data[i + j], substr->m_data[j], cs))
            {
                match = false;
                break;
            }
        }
        if (match)
            return i;
    }
    return -1;
}

int64_t XStringView_lastIndexOf_char(const XStringView* self, XChar ch, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;

    /* 处理负数 from */
    if (from < 0)
    {
        from = from + self->m_size;
        if (from < 0)
            return -1;
    }

    if (from >= self->m_size)
        from = self->m_size - 1;

    int64_t i;
    for (i = from; i >= 0; i--)
    {
        if (xchar_equal(self->m_data[i], ch, cs))
            return i;
    }
    return -1;
}

int64_t XStringView_lastIndexOf(const XStringView* self, const XStringView* substr, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;
    if (substr == NULL || substr->m_data == NULL || substr->m_size <= 0)
        return -1;
    if (substr->m_size > self->m_size)
        return -1;

    /* 处理负数 from */
    if (from < 0)
    {
        from = from + self->m_size;
        if (from < 0)
            return -1;
    }

    if (from >= self->m_size)
        from = self->m_size - 1;

    int64_t max_start = self->m_size - substr->m_size;
    if (from > max_start)
        from = max_start;

    int64_t i, j;
    for (i = from; i >= 0; i--)
    {
        bool match = true;
        for (j = 0; j < substr->m_size; j++)
        {
            if (!xchar_equal(self->m_data[i + j], substr->m_data[j], cs))
            {
                match = false;
                break;
            }
        }
        if (match)
            return i;
    }
    return -1;
}

bool XStringView_contains_char(const XStringView* self, XChar ch, int cs)
{
    return (XStringView_indexOf_char(self, ch, 0, cs) >= 0);
}

bool XStringView_contains(const XStringView* self, const XStringView* substr, int cs)
{
    return (XStringView_indexOf(self, substr, 0, cs) >= 0);
}

int64_t XStringView_count_char(const XStringView* self, XChar ch, int cs)
{
    if (self == NULL || self->m_data == NULL)
        return 0;

    int64_t count = 0;
    int64_t i;
    for (i = 0; i < self->m_size; i++)
    {
        if (xchar_equal(self->m_data[i], ch, cs))
            count++;
    }
    return count;
}

int64_t XStringView_count(const XStringView* self, const XStringView* substr, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    if (substr == NULL || substr->m_data == NULL || substr->m_size <= 0)
        return 0;
    if (substr->m_size > self->m_size)
        return 0;

    int64_t count = 0;
    int64_t pos = 0;
    int64_t max_start = self->m_size - substr->m_size;

    while (pos <= max_start)
    {
        int64_t found = XStringView_indexOf(self, substr, pos, cs);
        if (found < 0)
            break;
        count++;
        pos = found + substr->m_size;
    }
    return count;
}

bool XStringView_startsWith_char(const XStringView* self, XChar ch, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return xchar_equal(self->m_data[0], ch, cs);
}

bool XStringView_startsWith(const XStringView* self, const XStringView* prefix, int cs)
{
    if (self == NULL || self->m_data == NULL)
        return false;
    if (prefix == NULL || prefix->m_data == NULL)
        return false;
    if (prefix->m_size > self->m_size)
        return false;

    int64_t i;
    for (i = 0; i < prefix->m_size; i++)
    {
        if (!xchar_equal(self->m_data[i], prefix->m_data[i], cs))
            return false;
    }
    return true;
}

bool XStringView_endsWith_char(const XStringView* self, XChar ch, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return xchar_equal(self->m_data[self->m_size - 1], ch, cs);
}

bool XStringView_endsWith(const XStringView* self, const XStringView* suffix, int cs)
{
    if (self == NULL || self->m_data == NULL)
        return false;
    if (suffix == NULL || suffix->m_data == NULL)
        return false;
    if (suffix->m_size > self->m_size)
        return false;

    int64_t offset = self->m_size - suffix->m_size;
    int64_t i;
    for (i = 0; i < suffix->m_size; i++)
    {
        if (!xchar_equal(self->m_data[offset + i], suffix->m_data[i], cs))
            return false;
    }
    return true;
}

/* ============================== 比较操作 ============================== */

bool XStringView_equal(const XStringView* self, const XStringView* other, int cs)
{
    if (self == NULL || other == NULL)
        return (self == other);
    if (self->m_size != other->m_size)
        return false;
    if (self->m_data == NULL && other->m_data == NULL)
        return true;
    if (self->m_data == NULL || other->m_data == NULL)
        return false;
    if (self->m_size == 0)
        return true;

    if (cs == 1) /* CaseSensitive */
    {
        return (memcmp(self->m_data, other->m_data,
                       (size_t)self->m_size * sizeof(XChar)) == 0);
    }

    /* CaseInsensitive */
    int64_t i;
    for (i = 0; i < self->m_size; i++)
    {
        if (!xchar_equal(self->m_data[i], other->m_data[i], cs))
            return false;
    }
    return true;
}

int XStringView_compare(const XStringView* self, const XStringView* other, int cs)
{
    if (self == NULL && other == NULL)
        return 0;
    if (self == NULL)
        return -1;
    if (other == NULL)
        return 1;

    int64_t min_len = (self->m_size < other->m_size) ? self->m_size : other->m_size;
    int64_t i;
    for (i = 0; i < min_len; i++)
    {
        XChar a = self->m_data[i];
        XChar b = other->m_data[i];
        if (cs == 0) /* CaseInsensitive */
        {
            a = xchar_to_lower(a);
            b = xchar_to_lower(b);
        }
        if (a != b)
            return (a < b) ? -1 : 1;
    }
    if (self->m_size < other->m_size)
        return -1;
    if (self->m_size > other->m_size)
        return 1;
    return 0;
}

/* ============================== 修剪操作 ============================== */

XStringView XStringView_trimmed(const XStringView* self)
{
    XStringView result;
    if (self == NULL || self->m_data == NULL)
    {
        result.m_data = NULL;
        result.m_size = 0;
        return result;
    }

    int64_t start = 0;
    int64_t end = self->m_size - 1;

    while (start <= end && is_white_space_xchar(self->m_data[start]))
        start++;
    while (end >= start && is_white_space_xchar(self->m_data[end]))
        end--;

    if (start > end)
    {
        result.m_data = self->m_data;
        result.m_size = 0;
    }
    else
    {
        result.m_data = self->m_data + start;
        result.m_size = end - start + 1;
    }
    return result;
}

/* ============================== 编码转换 ============================== */

XString* XStringView_toString(const XStringView* self)
{
    if (self == NULL || self->m_data == NULL)
        return XString_create();
    return XString_create_with_length_utf16((const uint16_t*)self->m_data,
                                            (size_t)self->m_size);
}

/* ============================== 数值转换 ============================== */

short XStringView_toShort(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    long val = strtol(str, &endptr, base);
    if (endptr == str || val < SHRT_MIN || val > SHRT_MAX)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (short)val;
}

unsigned short XStringView_toUShort(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    unsigned long val = strtoul(str, &endptr, base);
    if (endptr == str || val > USHRT_MAX)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (unsigned short)val;
}

int XStringView_toInt(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    long val = strtol(str, &endptr, base);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (int)val;
}

unsigned int XStringView_toUInt(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    unsigned long val = strtoul(str, &endptr, base);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (unsigned int)val;
}

long XStringView_toLong(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    long val = strtol(str, &endptr, base);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return val;
}

unsigned long XStringView_toULong(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    unsigned long val = strtoul(str, &endptr, base);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return val;
}

int64_t XStringView_toLongLong(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    int64_t val = (int64_t)strtoll(str, &endptr, base);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return val;
}

uint64_t XStringView_toULongLong(const XStringView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    uint64_t val = (uint64_t)strtoull(str, &endptr, base);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return val;
}

float XStringView_toFloat(const XStringView* self, bool* ok)
{
    char buf[256];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0.0f;
    }
    char* endptr = NULL;
    float val = (float)strtof(str, &endptr);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0.0f;
    }
    if (ok) *ok = true;
    return val;
}

double XStringView_toDouble(const XStringView* self, bool* ok)
{
    char buf[256];
    const char* str = view_to_utf8(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0.0;
    }
    char* endptr = NULL;
    double val = strtod(str, &endptr);
    if (endptr == str)
    {
        if (ok) *ok = false;
        return 0.0;
    }
    if (ok) *ok = true;
    return val;
}

#endif // XString_ON
