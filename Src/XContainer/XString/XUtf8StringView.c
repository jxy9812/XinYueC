/**
* @file XUtf8StringView.c
* @brief UTF-8 字符串视图实现文件（对标 Qt 6.8 QUtf8StringView）
* @details 实现 XUtf8StringView 的所有 API，包括构造、访问、子视图、
*          查找、比较、数值转换等操作。
* @note XUtf8StringView 是值类型，不分配堆内存，不涉及虚函数表。
*/
#include "XUtf8StringView.h"
#if XString_ON
#include "XByteArrayView.h"   ///< 字节数组视图，用于从 XByteArrayView 构造
#include "XStringView.h"      ///< 字符串视图，用于跨视图比较
#include "XString.h"          ///< XString 字符串容器，用于 toString
#include "XStringUtils.h"     ///< XStrlen 等字符串原语（外部依赖约束：strlen 用库内 API 替代）
#include <string.h>           ///< memcmp、memcpy 等内存原语（白名单）

/* ============================== 内部辅助函数 ============================== */

/**
* @brief 判断 char 是否为空白字符
* @details 委托库内 XChar_isSpace（外部依赖约束：字符分类/转换必须使用 XChar），
*          字节先按无符号提升为 XChar。
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
*          大小写折叠覆盖完整 Unicode，不再手写 ASCII 范围处理。
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

/**
* @brief 检查 UTF-8 序列的连续字节是否有效
* @param data 数据指针
* @param pos  当前位置
* @param remaining 剩余字节数
* @param expected 期望的连续字节数
* @return 有效返回 true，否则返回 false
*/
static bool check_utf8_continuation(const char* data, int64_t pos, int64_t remaining, int expected)
{
    if (remaining < expected)
        return false;
    int i;
    for (i = 0; i < expected; i++)
    {
        if ((data[pos + 1 + i] & 0xC0) != 0x80)
            return false;
    }
    return true;
}

/* ============================== 构造与创建 ============================== */

XUtf8StringView XUtf8StringView_create(void)
{
    XUtf8StringView view;
    view.m_data = NULL;
    view.m_size = 0;
    return view;
}

XUtf8StringView XUtf8StringView_create_cstr(const char* str)
{
    XUtf8StringView view;
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

XUtf8StringView XUtf8StringView_create_data(const char* data, int64_t len)
{
    XUtf8StringView view;
    view.m_data = data;
    view.m_size = (data == NULL) ? 0 : len;
    return view;
}

XUtf8StringView XUtf8StringView_create_range(const char* first, const char* last)
{
    XUtf8StringView view;
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

XUtf8StringView XUtf8StringView_create_bytearrayview(const XByteArrayView* bav)
{
    XUtf8StringView view;
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

const char* XUtf8StringView_data(const XUtf8StringView* self)
{
    if (self == NULL)
        return NULL;
    return self->m_data;
}

const char* XUtf8StringView_utf8(const XUtf8StringView* self)
{
    return XUtf8StringView_data(self);
}

const char* XUtf8StringView_constData(const XUtf8StringView* self)
{
    return XUtf8StringView_data(self);
}

int64_t XUtf8StringView_size(const XUtf8StringView* self)
{
    if (self == NULL)
        return 0;
    return self->m_size;
}

bool XUtf8StringView_empty(const XUtf8StringView* self)
{
    if (self == NULL)
        return true;
    return (self->m_size == 0);
}

bool XUtf8StringView_isNull(const XUtf8StringView* self)
{
    if (self == NULL)
        return true;
    return (self->m_data == NULL);
}

int64_t XUtf8StringView_length(const XUtf8StringView* self)
{
    return XUtf8StringView_size(self);
}

/* ============================== 元素访问 ============================== */

char XUtf8StringView_at(const XUtf8StringView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL || n < 0 || n >= self->m_size)
        return 0;
    return self->m_data[n];
}

char XUtf8StringView_front(const XUtf8StringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[0];
}

char XUtf8StringView_back(const XUtf8StringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[self->m_size - 1];
}

/* ============================== 子视图操作 ============================== */

XUtf8StringView XUtf8StringView_first_n(const XUtf8StringView* self, int64_t n)
{
    XUtf8StringView result;
    if (self == NULL || self->m_data == NULL || n <= 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size = 0;
        return result;
    }
    if (n > self->m_size)
        n = self->m_size;
    result.m_data = self->m_data;
    result.m_size = n;
    return result;
}

XUtf8StringView XUtf8StringView_last_n(const XUtf8StringView* self, int64_t n)
{
    XUtf8StringView result;
    if (self == NULL || self->m_data == NULL || n <= 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size = 0;
        return result;
    }
    if (n > self->m_size)
        n = self->m_size;
    result.m_data = self->m_data + (self->m_size - n);
    result.m_size = n;
    return result;
}

XUtf8StringView XUtf8StringView_sliced(const XUtf8StringView* self, int64_t pos)
{
    XUtf8StringView result;
    if (self == NULL || self->m_data == NULL || pos < 0 || pos > self->m_size)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size = 0;
        return result;
    }
    result.m_data = self->m_data + pos;
    result.m_size = self->m_size - pos;
    return result;
}

XUtf8StringView XUtf8StringView_sliced_2(const XUtf8StringView* self, int64_t pos, int64_t n)
{
    XUtf8StringView result;
    if (self == NULL || self->m_data == NULL || pos < 0 || pos > self->m_size || n < 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size = 0;
        return result;
    }
    if (pos + n > self->m_size)
        n = self->m_size - pos;
    result.m_data = self->m_data + pos;
    result.m_size = n;
    return result;
}

XUtf8StringView XUtf8StringView_chopped(const XUtf8StringView* self, int64_t n)
{
    XUtf8StringView result;
    if (self == NULL || self->m_data == NULL || n < 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size = 0;
        return result;
    }
    int64_t new_size = self->m_size - n;
    if (new_size < 0)
        new_size = 0;
    result.m_data = self->m_data;
    result.m_size = new_size;
    return result;
}

/* ============================== 原地修改 ============================== */

void XUtf8StringView_truncate(XUtf8StringView* self, int64_t n)
{
    if (self == NULL)
        return;
    if (n < 0)
        n = 0;
    if (n > self->m_size)
        n = self->m_size;
    self->m_size = n;
}

void XUtf8StringView_chop(XUtf8StringView* self, int64_t n)
{
    if (self == NULL)
        return;
    if (n < 0)
        n = 0;
    if (n > self->m_size)
        self->m_size = 0;
    else
        self->m_size -= n;
}

/* ============================== 查找操作 ============================== */

int64_t XUtf8StringView_indexOf_char(const XUtf8StringView* self, char ch, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;
    if (from < 0)
        from = 0;
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

int64_t XUtf8StringView_indexOf(const XUtf8StringView* self, const XUtf8StringView* sub, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || sub == NULL || sub->m_data == NULL)
        return -1;
    if (sub->m_size == 0)
        return 0;
    if (sub->m_size > self->m_size)
        return -1;
    if (from < 0)
        from = 0;
    if (from >= self->m_size)
        return -1;

    int64_t end = self->m_size - sub->m_size;
    int64_t i;
    for (i = from; i <= end; i++)
    {
        int64_t j;
        bool match = true;
        for (j = 0; j < sub->m_size; j++)
        {
            if (!char_equal(self->m_data[i + j], sub->m_data[j], cs))
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

int64_t XUtf8StringView_lastIndexOf_char(const XUtf8StringView* self, char ch, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;
    if (from < 0 || from >= self->m_size)
        from = self->m_size - 1;

    int64_t i;
    for (i = from; i >= 0; i--)
    {
        if (char_equal(self->m_data[i], ch, cs))
            return i;
    }
    return -1;
}

int64_t XUtf8StringView_lastIndexOf(const XUtf8StringView* self, const XUtf8StringView* sub, int64_t from, int cs)
{
    if (self == NULL || self->m_data == NULL || sub == NULL || sub->m_data == NULL)
        return -1;
    if (sub->m_size == 0)
        return (self->m_size > 0) ? self->m_size : 0;
    if (sub->m_size > self->m_size)
        return -1;
    if (from < 0 || from >= self->m_size)
        from = self->m_size - sub->m_size;
    if (from > self->m_size - sub->m_size)
        from = self->m_size - sub->m_size;

    int64_t i;
    for (i = from; i >= 0; i--)
    {
        int64_t j;
        bool match = true;
        for (j = 0; j < sub->m_size; j++)
        {
            if (!char_equal(self->m_data[i + j], sub->m_data[j], cs))
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

bool XUtf8StringView_contains_char(const XUtf8StringView* self, char ch, int cs)
{
    return (XUtf8StringView_indexOf_char(self, ch, 0, cs) >= 0);
}

bool XUtf8StringView_contains(const XUtf8StringView* self, const XUtf8StringView* sub, int cs)
{
    return (XUtf8StringView_indexOf(self, sub, 0, cs) >= 0);
}

int64_t XUtf8StringView_count_char(const XUtf8StringView* self, char ch, int cs)
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

int64_t XUtf8StringView_count(const XUtf8StringView* self, const XUtf8StringView* sub, int cs)
{
    if (self == NULL || self->m_data == NULL || sub == NULL || sub->m_data == NULL || sub->m_size == 0)
        return 0;
    int64_t count = 0;
    int64_t pos = 0;
    while ((pos = XUtf8StringView_indexOf(self, sub, pos, cs)) >= 0)
    {
        count++;
        pos += sub->m_size;
    }
    return count;
}

bool XUtf8StringView_startsWith_char(const XUtf8StringView* self, char ch, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return char_equal(self->m_data[0], ch, cs);
}

bool XUtf8StringView_startsWith(const XUtf8StringView* self, const XUtf8StringView* prefix, int cs)
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

bool XUtf8StringView_endsWith_char(const XUtf8StringView* self, char ch, int cs)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return char_equal(self->m_data[self->m_size - 1], ch, cs);
}

bool XUtf8StringView_endsWith(const XUtf8StringView* self, const XUtf8StringView* suffix, int cs)
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

bool XUtf8StringView_equal(const XUtf8StringView* self, const XUtf8StringView* other, int cs)
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

int XUtf8StringView_compare(const XUtf8StringView* self, const XUtf8StringView* other, int cs)
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

XUtf8StringView XUtf8StringView_trimmed(const XUtf8StringView* self)
{
    XUtf8StringView result;
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

XString* XUtf8StringView_toString(const XUtf8StringView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return XString_create();

    /* 直接使用 UTF-8 数据创建 XString */
    XString* result = XString_create();
    if (result == NULL)
        return NULL;
    XString_append_with_length_utf8(result, self->m_data, (size_t)self->m_size);
    return result;
}

/* ============================== 编码检测 ============================== */

bool XUtf8StringView_isValidUtf8(const XUtf8StringView* self)
{
    if (self == NULL || self->m_data == NULL)
        return true;
    if (self->m_size == 0)
        return true;

    int64_t i;
    for (i = 0; i < self->m_size; i++)
    {
        unsigned char c = (unsigned char)self->m_data[i];
        int64_t remaining = self->m_size - i - 1;

        if (c <= 0x7F)
        {
            /* 单字节: 0xxxxxxx */
            continue;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            /* 双字节: 110xxxxx 10xxxxxx */
            if (!check_utf8_continuation(self->m_data, i, remaining, 1))
                return false;
            /* 检查是否过编码（overlong） */
            if ((c & 0x1E) == 0)
                return false;
            i += 1;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            /* 三字节: 1110xxxx 10xxxxxx 10xxxxxx */
            if (!check_utf8_continuation(self->m_data, i, remaining, 2))
                return false;
            /* 检查是否过编码 */
            if ((c & 0x0F) == 0 && (self->m_data[i + 1] & 0x20) == 0)
                return false;
            i += 2;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            /* 四字节: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            if (!check_utf8_continuation(self->m_data, i, remaining, 3))
                return false;
            /* 检查是否过编码 */
            if ((c & 0x07) == 0 && (self->m_data[i + 1] & 0x30) == 0)
                return false;
            i += 3;
        }
        else
        {
            /* 无效的起始字节 */
            return false;
        }
    }
    return true;
}

/* ============================== 数值转换 ============================== */

/**
* @brief 将视图内容转换为以 0 终止的 XChar（UTF-16）临时数组
* @details 委托库内 XChar_fromUtf8Stream（外部依赖约束：编码转换必须使用
*          XChar），供数值转换经 XChar_to* 系列库内解析，不再依赖 strtol 族。
* @param self 视图指针
* @param buf 输出缓冲区（由调用者分配，XChar 元素数需 > size）
* @param buf_size 缓冲区容量（XChar 元素数）
* @return 缓冲区指针，失败返回 NULL
*/
static XChar* view_to_xchar(const XUtf8StringView* self, XChar* buf, size_t buf_size)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return NULL;
    if ((size_t)self->m_size >= buf_size)
        return NULL;
    int64_t count = XChar_fromUtf8Stream((const uint8_t*)self->m_data,
                                         (size_t)self->m_size, buf, buf_size);
    if (count < 0)
        return NULL;
    return buf;
}

short XUtf8StringView_toShort(const XUtf8StringView* self, bool* ok, int base)
{
    long val = XUtf8StringView_toLong(self, ok, base);
    if (ok != NULL && *ok)
    {
        if (val < -32768 || val > 32767)
        {
            *ok = false;
            return 0;
        }
    }
    return (short)val;
}

unsigned short XUtf8StringView_toUShort(const XUtf8StringView* self, bool* ok, int base)
{
    unsigned long val = XUtf8StringView_toULong(self, ok, base);
    if (ok != NULL && *ok)
    {
        if (val > 65535)
        {
            *ok = false;
            return 0;
        }
    }
    return (unsigned short)val;
}

int XUtf8StringView_toInt(const XUtf8StringView* self, bool* ok, int base)
{
    long val = XUtf8StringView_toLong(self, ok, base);
    if (ok != NULL && *ok)
    {
        if (val < -2147483647L - 1 || val > 2147483647L)
        {
            *ok = false;
            return 0;
        }
    }
    return (int)val;
}

unsigned int XUtf8StringView_toUInt(const XUtf8StringView* self, bool* ok, int base)
{
    unsigned long val = XUtf8StringView_toULong(self, ok, base);
    if (ok != NULL && *ok)
    {
        if (val > 4294967295UL)
        {
            *ok = false;
            return 0;
        }
    }
    return (unsigned int)val;
}

long XUtf8StringView_toLong(const XUtf8StringView* self, bool* ok, int base)
{
    if (ok != NULL)
        *ok = false;
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;

    XChar buf[128];
    if (!view_to_xchar(self, buf, sizeof(buf) / sizeof(buf[0])))
        return 0;

    /* 委托库内 XChar_toLong（外部依赖约束：数值解析必须使用库内 API），
       内部含溢出检测；input_count 传 0 按缓冲区终止符自动检测。 */
    bool parsed = false;
    long val = XChar_toLong(buf, 0, base, &parsed);
    if (ok != NULL)
        *ok = parsed;
    return val;
}

unsigned long XUtf8StringView_toULong(const XUtf8StringView* self, bool* ok, int base)
{
    if (ok != NULL)
        *ok = false;
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;

    XChar buf[128];
    if (!view_to_xchar(self, buf, sizeof(buf) / sizeof(buf[0])))
        return 0;

    /* 委托库内 XChar_toULong（外部依赖约束：数值解析必须使用库内 API），
       内部含溢出检测；库内无 char* 版 strtoul 替代，统一走 XChar 路径。 */
    bool parsed = false;
    unsigned long val = XChar_toULong(buf, 0, base, &parsed);
    if (ok != NULL)
        *ok = parsed;
    return val;
}

int64_t XUtf8StringView_toLongLong(const XUtf8StringView* self, bool* ok, int base)
{
    if (ok != NULL)
        *ok = false;
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;

    XChar buf[128];
    if (!view_to_xchar(self, buf, sizeof(buf) / sizeof(buf[0])))
        return 0;

    /* 委托库内 XChar_toLongLong（外部依赖约束：数值解析必须使用库内 API）。 */
    bool parsed = false;
    int64_t val = (int64_t)XChar_toLongLong(buf, 0, base, &parsed);
    if (ok != NULL)
        *ok = parsed;
    return val;
}

uint64_t XUtf8StringView_toULongLong(const XUtf8StringView* self, bool* ok, int base)
{
    if (ok != NULL)
        *ok = false;
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;

    XChar buf[128];
    if (!view_to_xchar(self, buf, sizeof(buf) / sizeof(buf[0])))
        return 0;

    /* 委托库内 XChar_toULongLong（外部依赖约束：数值解析必须使用库内 API）。 */
    bool parsed = false;
    uint64_t val = (uint64_t)XChar_toULongLong(buf, 0, base, &parsed);
    if (ok != NULL)
        *ok = parsed;
    return val;
}

float XUtf8StringView_toFloat(const XUtf8StringView* self, bool* ok)
{
    double val = XUtf8StringView_toDouble(self, ok);
    return (float)val;
}

double XUtf8StringView_toDouble(const XUtf8StringView* self, bool* ok)
{
    if (ok != NULL)
        *ok = false;
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0.0;

    XChar buf[128];
    if (!view_to_xchar(self, buf, sizeof(buf) / sizeof(buf[0])))
        return 0.0;

    /* 委托库内 XChar_toDouble（外部依赖约束：数值解析必须使用库内 API）。 */
    bool parsed = false;
    double val = XChar_toDouble(buf, 0, &parsed);
    if (ok != NULL)
        *ok = parsed;
    return val;
}

#endif // XString_ON
