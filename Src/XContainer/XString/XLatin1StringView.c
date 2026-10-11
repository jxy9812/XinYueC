/**
* @file XLatin1StringView.c
* @brief Latin-1 字符串视图实现文件（对标 Qt 6.8 QLatin1StringView）
* @details 实现 XLatin1StringView 的所有 API，包括构造、访问、子视图、
*          查找、比较等操作。
* @note XLatin1StringView 是值类型，不分配堆内存，不涉及虚函数表。
*/
#include "XLatin1StringView.h"
#if XString_ON
#include "XByteArrayView.h"   ///< 字节数组视图，用于从 XByteArrayView 构造
#include "XStringView.h"      ///< 字符串视图，用于跨视图比较
#include "XString.h"          ///< XString 字符串容器，用于 toString
#include "XStringUtils.h"     ///< XStrlen 等字符串原语（外部依赖约束：strlen 用库内 API 替代）
#include <string.h>           ///< memcmp、memchr 等内存原语

/* ============================== 内部辅助函数 ============================== */

/**
* @brief 判断 char 是否为空白字符
* @details 委托库内 XChar_isSpace（外部依赖约束：字符分类/转换必须使用 XChar），
*          Latin-1 字节先按无符号字节提升为 XChar。
* @param ch 待判断的字符
* @return true 为空白字符，false 为非空白字符
*/
static bool is_white_space_char(char ch)
{
    return XChar_isSpace((XChar)(uint8_t)ch);
}

/**
* @brief 比较两个 char 字符（支持大小写敏感性）
* @details 委托库内 XChar_equals（外部依赖约束：字符分类/转换必须使用 XChar），
*          Latin-1 字节先按无符号字节提升为 XChar。
* @param a 第一个字符
* @param b 第二个字符
* @param cs 大小写敏感性（0=不区分，1=区分）
* @return 相等返回 true，否则返回 false
*/
static bool char_equal(char a, char b, int cs)
{
    return XChar_equals((XChar)(uint8_t)a, (XChar)(uint8_t)b,
                        (cs == 1) ? XChar_CaseSensitive : XChar_CaseInsensitive);
}

/**
* @brief 将 char 转小写（辅助函数）
* @details 委托库内 XChar_toLower（外部依赖约束：字符分类/转换必须使用 XChar）。
* @param ch 输入字符
* @return 小写形式
*/
static char char_to_lower(char ch)
{
    return (char)XChar_toLower((XChar)(uint8_t)ch);
}

/* ============================== 构造与创建 ============================== */

XLatin1StringView XLatin1StringView_create(void)
{
    XLatin1StringView view;
    view.m_data = NULL;
    view.m_size = 0;
    return view;
}

XLatin1StringView XLatin1StringView_create_cstr(const char* str)
{
    XLatin1StringView view;
    if (str == NULL)
    {
        view.m_data = NULL;
        view.m_size = 0;
    }
    else
    {
        view.m_data = str;
        view.m_size = (int64_t)XStrlen(str);
    }
    return view;
}

XLatin1StringView XLatin1StringView_create_data(const char* data, int64_t len)
{
    XLatin1StringView view;
    view.m_data = data;
    view.m_size = (data == NULL) ? 0 : len;
    return view;
}

XLatin1StringView XLatin1StringView_create_range(const char* first, const char* last)
{
    XLatin1StringView view;
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

XLatin1StringView XLatin1StringView_create_bytearrayview(const XByteArrayView* bav)
{
    XLatin1StringView view;
    if (bav == NULL)
    {
        view.m_data = NULL;
        view.m_size = 0;
    }
    else
    {
        view.m_data = (const char*)XByteArrayView_data(bav);
        view.m_size = XByteArrayView_size(bav);
    }
    return view;
}

/* ============================== 基本访问 ============================== */

const char* XLatin1StringView_latin1(const XLatin1StringView* self)
{
    if (self == NULL)
        return NULL;
    return self->m_data;
}

const char* XLatin1StringView_data(const XLatin1StringView* self)
{
    if (self == NULL)
        return NULL;
    return self->m_data;
}

const char* XLatin1StringView_constData(const XLatin1StringView* self)
{
    return XLatin1StringView_data(self);
}

int64_t XLatin1StringView_size(const XLatin1StringView* self)
{
    if (self == NULL)
        return 0;
    return self->m_size;
}

bool XLatin1StringView_empty(const XLatin1StringView* self)
{
    if (self == NULL)
        return true;
    return (self->m_size == 0);
}

bool XLatin1StringView_isNull(const XLatin1StringView* self)
{
    if (self == NULL)
        return true;
    return (self->m_data == NULL);
}

/* ============================== 元素访问 ============================== */

char XLatin1StringView_at(const XLatin1StringView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL || n < 0 || n >= self->m_size)
        return 0;
    return self->m_data[n];
}

char XLatin1StringView_front(const XLatin1StringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[0];
}

char XLatin1StringView_back(const XLatin1StringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[self->m_size - 1];
}

/* ============================== 子视图操作 ============================== */

XLatin1StringView XLatin1StringView_first_n(const XLatin1StringView* self, int64_t n)
{
    XLatin1StringView result;
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

XLatin1StringView XLatin1StringView_last_n(const XLatin1StringView* self, int64_t n)
{
    XLatin1StringView result;
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

XLatin1StringView XLatin1StringView_sliced(const XLatin1StringView* self, int64_t pos)
{
    XLatin1StringView result;
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

XLatin1StringView XLatin1StringView_sliced_2(const XLatin1StringView* self, int64_t pos, int64_t n)
{
    XLatin1StringView result;
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

XLatin1StringView XLatin1StringView_chopped(const XLatin1StringView* self, int64_t n)
{
    XLatin1StringView result;
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

void XLatin1StringView_truncate(XLatin1StringView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL)
        return;
    if (n < 0)
        n = 0;
    if (n < self->m_size)
        self->m_size = n;
}

void XLatin1StringView_chop(XLatin1StringView* self, int64_t n)
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

int64_t XLatin1StringView_indexOf_char(const XLatin1StringView* self, char ch, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;

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
        if (char_equal(self->m_data[i], ch, cs))
            return i;
    }
    return -1;
}

int64_t XLatin1StringView_indexOf(const XLatin1StringView* self, const XLatin1StringView* substr, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;
    if (substr == NULL || substr->m_data == NULL || substr->m_size <= 0)
        return -1;
    if (substr->m_size > self->m_size)
        return -1;

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
            if (!char_equal(self->m_data[i + j], substr->m_data[j], cs))
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

int64_t XLatin1StringView_lastIndexOf_char(const XLatin1StringView* self, char ch, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;

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
        if (char_equal(self->m_data[i], ch, cs))
            return i;
    }
    return -1;
}

int64_t XLatin1StringView_lastIndexOf(const XLatin1StringView* self, const XLatin1StringView* substr, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;
    if (substr == NULL || substr->m_data == NULL || substr->m_size <= 0)
        return -1;
    if (substr->m_size > self->m_size)
        return -1;

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
            if (!char_equal(self->m_data[i + j], substr->m_data[j], cs))
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

bool XLatin1StringView_contains_char(const XLatin1StringView* self, char ch, int cs)
{
    return (XLatin1StringView_indexOf_char(self, ch, 0, cs) >= 0);
}

bool XLatin1StringView_contains(const XLatin1StringView* self, const XLatin1StringView* substr, int cs)
{
    return (XLatin1StringView_indexOf(self, substr, 0, cs) >= 0);
}

int64_t XLatin1StringView_count_char(const XLatin1StringView* self, char ch, int cs)
{
    if (self == NULL || self->m_data == NULL)
        return 0;

    int64_t count = 0;
    int64_t i;
    for (i = 0; i < self->m_size; i++)
    {
        if (char_equal(self->m_data[i], ch, cs))
            count++;
    }
    return count;
}

int64_t XLatin1StringView_count(const XLatin1StringView* self, const XLatin1StringView* substr, int cs)
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
        int64_t found = XLatin1StringView_indexOf(self, substr, pos, cs);
        if (found < 0)
            break;
        count++;
        pos = found + substr->m_size;
    }
    return count;
}

bool XLatin1StringView_startsWith_char(const XLatin1StringView* self, char ch, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return char_equal(self->m_data[0], ch, cs);
}

bool XLatin1StringView_startsWith(const XLatin1StringView* self, const XLatin1StringView* prefix, int cs)
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
        if (!char_equal(self->m_data[i], prefix->m_data[i], cs))
            return false;
    }
    return true;
}

bool XLatin1StringView_endsWith_char(const XLatin1StringView* self, char ch, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return char_equal(self->m_data[self->m_size - 1], ch, cs);
}

bool XLatin1StringView_endsWith(const XLatin1StringView* self, const XLatin1StringView* suffix, int cs)
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
        if (!char_equal(self->m_data[offset + i], suffix->m_data[i], cs))
            return false;
    }
    return true;
}

/* ============================== 比较操作 ============================== */

bool XLatin1StringView_equal(const XLatin1StringView* self, const XLatin1StringView* other, int cs)
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
        return (memcmp(self->m_data, other->m_data, (size_t)self->m_size) == 0);
    }

    /* CaseInsensitive */
    int64_t i;
    for (i = 0; i < self->m_size; i++)
    {
        if (!char_equal(self->m_data[i], other->m_data[i], cs))
            return false;
    }
    return true;
}

int XLatin1StringView_compare(const XLatin1StringView* self, const XLatin1StringView* other, int cs)
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
        char a = self->m_data[i];
        char b = other->m_data[i];
        if (cs == 0) /* CaseInsensitive */
        {
            a = char_to_lower(a);
            b = char_to_lower(b);
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

XLatin1StringView XLatin1StringView_trimmed(const XLatin1StringView* self)
{
    XLatin1StringView result;
    if (self == NULL || self->m_data == NULL)
    {
        result.m_data = NULL;
        result.m_size = 0;
        return result;
    }

    int64_t start = 0;
    int64_t end = self->m_size - 1;

    while (start <= end && is_white_space_char(self->m_data[start]))
        start++;
    while (end >= start && is_white_space_char(self->m_data[end]))
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

XString* XLatin1StringView_toString(const XLatin1StringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return XString_create();

    /* Latin-1 每个字符直接映射到 Unicode 码点 0-255，
       转换统一经库内 XString_create_with_length_latin1（内部走
       XChar_fromLatin1Stream），不再手写 Latin-1→UTF-8 编码。 */
    return XString_create_with_length_latin1((const uint8_t*)self->m_data,
                                             (size_t)self->m_size);
}

#endif // XString_ON
