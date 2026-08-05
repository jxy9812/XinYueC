/**
* @file XByteArrayView.c
* @brief 字节数组视图实现文件（对标 Qt 6.8 QByteArrayView）
* @details 实现 XByteArrayView 的所有 API，包括构造、访问、子视图、
*          查找、比较、数值转换等操作。
* @note XByteArrayView 是值类型，不分配堆内存，不涉及虚函数表。
*/
#include "XByteArrayView.h"
#if XByteArray_ON
#include "XByteArray.h"      ///< XByteArray 容器，用于从 XByteArray 构造视图
#include <string.h>          ///< memcmp、memchr、strlen 等内存操作
#include <stdlib.h>          ///< strtol、strtoll、strtoul、strtoull、strtod 等数值转换
#include <ctype.h>           ///< isspace 空白字符判断

/* ============================== 内部辅助函数 ============================== */

/**
* @brief 判断字节是否为空白字符
* @details 空白字符包括：空格(' ')、制表符('\t')、换行符('\n')、回车符('\r')、
*          垂直制表符('\v')、换页符('\f')。
* @param ch 待判断的字节值
* @return true 为空白字符，false 为非空白字符
*/
static bool is_white_space(uint8_t ch)
{
    return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f');
}

/* ============================== 构造与创建 ============================== */

XByteArrayView XByteArrayView_create(void)
{
    XByteArrayView view;
    view.m_data = NULL;
    view.m_size = 0;
    return view;
}

XByteArrayView XByteArrayView_create_data(const uint8_t* data, int64_t len)
{
    XByteArrayView view;
    view.m_data = data;
    view.m_size = (data == NULL) ? 0 : len;
    return view;
}

XByteArrayView XByteArrayView_create_range(const uint8_t* first, const uint8_t* last)
{
    XByteArrayView view;
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

XByteArrayView XByteArrayView_create_cstr(const char* str)
{
    XByteArrayView view;
    if (str == NULL)
    {
        view.m_data = NULL;
        view.m_size = 0;
    }
    else
    {
        view.m_data = (const uint8_t*)str;
        view.m_size = (int64_t)strlen(str);
    }
    return view;
}

XByteArrayView XByteArrayView_create_bytearray(const XByteArray* ba)
{
    XByteArrayView view;
    if (ba == NULL || (XContainer_isEmpty_base((const XContainer*)ba)))
    {
        view.m_data = (ba == NULL) ? NULL : XByteArray_data((XByteArray*)ba);
        view.m_size = (ba == NULL) ? 0 : (int64_t)(XContainer_size_base((const XContainer*)ba));
    }
    else
    {
        view.m_data = XByteArray_data((XByteArray*)ba);
        view.m_size = (int64_t)(XContainer_size_base((const XContainer*)ba));
    }
    return view;
}

/* ============================== 基本访问 ============================== */

const uint8_t* XByteArrayView_data(const XByteArrayView* self)
{
    if (self == NULL)
        return NULL;
    return self->m_data;
}

int64_t XByteArrayView_size(const XByteArrayView* self)
{
    if (self == NULL)
        return 0;
    return self->m_size;
}

bool XByteArrayView_empty(const XByteArrayView* self)
{
    if (self == NULL)
        return true;
    return (self->m_size == 0);
}

bool XByteArrayView_isNull(const XByteArrayView* self)
{
    if (self == NULL)
        return true;
    return (self->m_data == NULL);
}

int64_t XByteArrayView_maxSize(void)
{
    return INT64_MAX - 1;
}

/* ============================== 元素访问 ============================== */

uint8_t XByteArrayView_at(const XByteArrayView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL || n < 0 || n >= self->m_size)
        return 0;
    return self->m_data[n];
}

uint8_t XByteArrayView_front(const XByteArrayView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[0];
}

uint8_t XByteArrayView_back(const XByteArrayView* self)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return 0;
    return self->m_data[self->m_size - 1];
}

/* ============================== 子视图操作 ============================== */

XByteArrayView XByteArrayView_first_n(const XByteArrayView* self, int64_t n)
{
    XByteArrayView result;
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

XByteArrayView XByteArrayView_last_n(const XByteArrayView* self, int64_t n)
{
    XByteArrayView result;
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

XByteArrayView XByteArrayView_sliced(const XByteArrayView* self, int64_t pos)
{
    XByteArrayView result;
    if (self == NULL || self->m_data == NULL || pos < 0)
    {
        result.m_data = (self != NULL) ? self->m_data : NULL;
        result.m_size = 0;
        return result;
    }
    if (pos >= self->m_size)
    {
        result.m_data = self->m_data + self->m_size;
        result.m_size = 0;
    }
    else
    {
        result.m_data = self->m_data + pos;
        result.m_size = self->m_size - pos;
    }
    return result;
}

XByteArrayView XByteArrayView_sliced_n(const XByteArrayView* self, int64_t pos, int64_t n)
{
    XByteArrayView result;
    if (self == NULL || self->m_data == NULL || pos < 0 || n < 0)
    {
        result.m_data = (self != NULL) ? self->m_data : NULL;
        result.m_size = 0;
        return result;
    }
    if (pos >= self->m_size)
    {
        result.m_data = self->m_data + self->m_size;
        result.m_size = 0;
    }
    else
    {
        result.m_data = self->m_data + pos;
        int64_t remaining = self->m_size - pos;
        result.m_size = (n > remaining) ? remaining : n;
    }
    return result;
}

void XByteArrayView_slice(XByteArrayView* self, int64_t pos)
{
    if (self == NULL || self->m_data == NULL)
        return;
    if (pos < 0)
        pos = 0;
    if (pos >= self->m_size)
    {
        self->m_data += self->m_size;
        self->m_size = 0;
    }
    else
    {
        self->m_data += pos;
        self->m_size -= pos;
    }
}

void XByteArrayView_slice_n(XByteArrayView* self, int64_t pos, int64_t n)
{
    if (self == NULL || self->m_data == NULL)
        return;
    if (pos < 0)
        pos = 0;
    if (pos >= self->m_size)
    {
        self->m_data += self->m_size;
        self->m_size = 0;
    }
    else
    {
        self->m_data += pos;
        int64_t remaining = self->m_size - pos;
        self->m_size = (n < 0) ? remaining : ((n > remaining) ? remaining : n);
    }
}

XByteArrayView XByteArrayView_chopped(const XByteArrayView* self, int64_t n)
{
    XByteArrayView result;
    if (self == NULL || self->m_data == NULL || n <= 0)
    {
        result.m_data = (self != NULL) ? self->m_data : NULL;
        result.m_size = (self != NULL) ? self->m_size : 0;
        return result;
    }
    if (n >= self->m_size)
    {
        result.m_data = self->m_data;
        result.m_size = 0;
    }
    else
    {
        result.m_data = self->m_data;
        result.m_size = self->m_size - n;
    }
    return result;
}

XByteArrayView XByteArrayView_mid(const XByteArrayView* self, int64_t pos, int64_t n)
{
    if (n == -1)
        return XByteArrayView_sliced(self, pos);
    return XByteArrayView_sliced_n(self, pos, n);
}

void XByteArrayView_truncate(XByteArrayView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL)
        return;
    if (n < 0)
        n = 0;
    self->m_size = (n > self->m_size) ? self->m_size : n;
}

void XByteArrayView_chop(XByteArrayView* self, int64_t n)
{
    if (self == NULL || self->m_data == NULL || n <= 0)
        return;
    self->m_size = (n >= self->m_size) ? 0 : (self->m_size - n);
}

/* ============================== 查找操作 ============================== */

int64_t XByteArrayView_indexOf(const XByteArrayView* self, const XByteArrayView* a, int64_t from)
{
    if (self == NULL || a == NULL || self->m_data == NULL || a->m_data == NULL)
        return -1;
    if (a->m_size == 0)
        return 0;
    if (a->m_size > self->m_size)
        return -1;

    /* 处理负数 from（从末尾偏移） */
    if (from < 0)
    {
        from = self->m_size + from;
        if (from < 0)
            from = 0;
    }
    if (from > self->m_size)
        return -1;

    int64_t searchEnd = self->m_size - a->m_size;
    for (int64_t i = from; i <= searchEnd; i++)
    {
        if (memcmp(self->m_data + i, a->m_data, (size_t)a->m_size) == 0)
            return i;
    }
    return -1;
}

int64_t XByteArrayView_indexOf_char(const XByteArrayView* self, uint8_t ch, int64_t from)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;

    /* 处理负数 from（从末尾偏移） */
    if (from < 0)
    {
        from = self->m_size + from;
        if (from < 0)
            from = 0;
    }
    if (from >= self->m_size)
        return -1;

    const void* found = memchr(self->m_data + from, (int)ch, (size_t)(self->m_size - from));
    if (found == NULL)
        return -1;
    return (int64_t)((const uint8_t*)found - self->m_data);
}

int64_t XByteArrayView_lastIndexOf(const XByteArrayView* self, const XByteArrayView* a)
{
    return XByteArrayView_lastIndexOf_from(self, a, -1);
}

int64_t XByteArrayView_lastIndexOf_from(const XByteArrayView* self, const XByteArrayView* a, int64_t from)
{
    if (self == NULL || a == NULL || self->m_data == NULL || a->m_data == NULL)
        return -1;
    if (a->m_size == 0)
        return (from < 0) ? self->m_size : ((from > self->m_size) ? self->m_size : from);
    if (a->m_size > self->m_size)
        return -1;

    /* 处理 from */
    if (from < 0)
        from = self->m_size - a->m_size;
    else if (from > self->m_size - a->m_size)
        from = self->m_size - a->m_size;

    for (int64_t i = from; i >= 0; i--)
    {
        if (memcmp(self->m_data + i, a->m_data, (size_t)a->m_size) == 0)
            return i;
    }
    return -1;
}

int64_t XByteArrayView_lastIndexOf_char(const XByteArrayView* self, uint8_t ch, int64_t from)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return -1;

    /* 处理 from */
    if (from < 0)
        from = self->m_size - 1;
    else if (from >= self->m_size)
        from = self->m_size - 1;

    for (int64_t i = from; i >= 0; i--)
    {
        if (self->m_data[i] == ch)
            return i;
    }
    return -1;
}

bool XByteArrayView_contains(const XByteArrayView* self, const XByteArrayView* a)
{
    return (XByteArrayView_indexOf(self, a, 0) != -1);
}

bool XByteArrayView_contains_char(const XByteArrayView* self, uint8_t ch)
{
    return (XByteArrayView_indexOf_char(self, ch, 0) != -1);
}

int64_t XByteArrayView_count(const XByteArrayView* self, const XByteArrayView* a)
{
    if (self == NULL || a == NULL || self->m_data == NULL || a->m_data == NULL)
        return 0;
    if (a->m_size == 0)
        return self->m_size + 1;
    if (a->m_size > self->m_size)
        return 0;

    int64_t count = 0;
    int64_t pos = 0;
    while ((pos = XByteArrayView_indexOf(self, a, pos)) != -1)
    {
        count++;
        pos += a->m_size;
    }
    return count;
}

int64_t XByteArrayView_count_char(const XByteArrayView* self, uint8_t ch)
{
    if (self == NULL || self->m_data == NULL)
        return 0;

    int64_t count = 0;
    for (int64_t i = 0; i < self->m_size; i++)
    {
        if (self->m_data[i] == ch)
            count++;
    }
    return count;
}

bool XByteArrayView_startsWith(const XByteArrayView* self, const XByteArrayView* other)
{
    if (self == NULL || other == NULL || self->m_data == NULL || other->m_data == NULL)
        return false;
    if (other->m_size > self->m_size)
        return false;
    if (other->m_size == 0)
        return true;
    return (memcmp(self->m_data, other->m_data, (size_t)other->m_size) == 0);
}

bool XByteArrayView_startsWith_char(const XByteArrayView* self, uint8_t c)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return (self->m_data[0] == c);
}

bool XByteArrayView_endsWith(const XByteArrayView* self, const XByteArrayView* other)
{
    if (self == NULL || other == NULL || self->m_data == NULL || other->m_data == NULL)
        return false;
    if (other->m_size > self->m_size)
        return false;
    if (other->m_size == 0)
        return true;
    return (memcmp(self->m_data + (self->m_size - other->m_size),
                   other->m_data, (size_t)other->m_size) == 0);
}

bool XByteArrayView_endsWith_char(const XByteArrayView* self, uint8_t c)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return false;
    return (self->m_data[self->m_size - 1] == c);
}

/* ============================== 比较操作 ============================== */

int32_t XByteArrayView_compare(const XByteArrayView* self, const XByteArrayView* a, int cs)
{
    if (self == NULL && a == NULL)
        return 0;
    if (self == NULL)
        return -1;
    if (a == NULL)
        return 1;

    int64_t minSize = (self->m_size < a->m_size) ? self->m_size : a->m_size;
    int cmp;

    if (cs == 0)
    {
        /* 大小写不敏感比较（ASCII 范围） */
        for (int64_t i = 0; i < minSize; i++)
        {
            uint8_t l = (uint8_t)tolower((int)self->m_data[i]);
            uint8_t r = (uint8_t)tolower((int)a->m_data[i]);
            if (l != r)
                return (l < r) ? -1 : 1;
        }
    }
    else
    {
        /* 大小写敏感比较 */
        cmp = minSize > 0 ? memcmp(self->m_data, a->m_data, (size_t)minSize) : 0;
        if (cmp != 0)
            return (cmp < 0) ? -1 : 1;
    }

    /* 长度不同时，较长的视为更大 */
    if (self->m_size != a->m_size)
        return (self->m_size < a->m_size) ? -1 : 1;

    return 0;
}

bool XByteArrayView_equal(const XByteArrayView* lhs, const XByteArrayView* rhs)
{
    if (lhs == NULL && rhs == NULL)
        return true;
    if (lhs == NULL || rhs == NULL)
        return false;
    if (lhs->m_size != rhs->m_size)
        return false;
    if (lhs->m_size == 0)
        return true;
    return (memcmp(lhs->m_data, rhs->m_data, (size_t)lhs->m_size) == 0);
}

/* ============================== 裁剪操作 ============================== */

XByteArrayView XByteArrayView_trimmed(const XByteArrayView* self)
{
    XByteArrayView result;
    if (self == NULL || self->m_data == NULL)
    {
        result.m_data = (self != NULL) ? self->m_data : NULL;
        result.m_size = 0;
        return result;
    }

    const uint8_t* start = self->m_data;
    const uint8_t* end = self->m_data + self->m_size;

    /* 去除前导空白 */
    while (start < end && is_white_space(*start))
        start++;

    /* 去除尾部空白 */
    while (end > start && is_white_space(*(end - 1)))
        end--;

    result.m_data = start;
    result.m_size = (int64_t)(end - start);
    return result;
}

/* ============================== 数值转换 ============================== */

/**
* @brief 内部辅助：将视图转换为 NULL 终止的临时字符串
* @details 由于标准 C 库的 strtol 等函数需要 NULL 终止的字符串，
*          此函数在栈上创建临时缓冲区并拷贝数据。
*          注意：缓冲区最大 256 字节，超长时返回 NULL。
* @param self 视图指针
* @param buf  输出缓冲区
* @param bufsize 缓冲区大小
* @return 指向缓冲区的指针，失败返回 NULL
*/
static const char* view_to_cstr(const XByteArrayView* self, char* buf, size_t bufsize)
{
    if (self == NULL || self->m_data == NULL || self->m_size <= 0)
        return NULL;
    if ((size_t)self->m_size >= bufsize)
        return NULL;
    memcpy(buf, self->m_data, (size_t)self->m_size);
    buf[self->m_size] = '\0';
    return buf;
}

int16_t XByteArrayView_toShort(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    long val = strtol(str, &endptr, base);
    if (endptr == str || val < INT16_MIN || val > INT16_MAX)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (int16_t)val;
}

uint16_t XByteArrayView_toUShort(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    unsigned long val = strtoul(str, &endptr, base);
    if (endptr == str || val > UINT16_MAX)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (uint16_t)val;
}

int32_t XByteArrayView_toInt(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    long val = strtol(str, &endptr, base);
    if (endptr == str || val < INT32_MIN || val > INT32_MAX)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (int32_t)val;
}

uint32_t XByteArrayView_toUInt(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
    if (str == NULL)
    {
        if (ok) *ok = false;
        return 0;
    }
    char* endptr = NULL;
    unsigned long val = strtoul(str, &endptr, base);
    if (endptr == str || val > UINT32_MAX)
    {
        if (ok) *ok = false;
        return 0;
    }
    if (ok) *ok = true;
    return (uint32_t)val;
}

long XByteArrayView_toLong(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
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

unsigned long XByteArrayView_toULong(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
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

int64_t XByteArrayView_toLongLong(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
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

uint64_t XByteArrayView_toULongLong(const XByteArrayView* self, bool* ok, int base)
{
    char buf[64];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
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

float XByteArrayView_toFloat(const XByteArrayView* self, bool* ok)
{
    char buf[256];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
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

double XByteArrayView_toDouble(const XByteArrayView* self, bool* ok)
{
    char buf[256];
    const char* str = view_to_cstr(self, buf, sizeof(buf));
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

/* ============================== 编码检测 ============================== */

/**
* @brief 内部辅助：检查 UTF-8 编码有效性
* @details 检查字节序列是否为有效的 UTF-8 编码。
*          遵循 RFC 3629 规范。
* @param data 数据指针
* @param size 数据长度
* @return true 为有效 UTF-8，false 为无效
*/
static bool is_valid_utf8_internal(const uint8_t* data, int64_t size)
{
    if (data == NULL || size <= 0)
        return true;

    for (int64_t i = 0; i < size; )
    {
        uint8_t lead = data[i];

        if (lead <= 0x7F)
        {
            /* 单字节序列：0xxxxxxx */
            i++;
        }
        else if (lead >= 0xC2 && lead <= 0xDF)
        {
            /* 双字节序列：110xxxxx 10xxxxxx */
            if (i + 1 >= size) return false;
            if ((data[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            /* 三字节序列：1110xxxx 10xxxxxx 10xxxxxx */
            if (i + 2 >= size) return false;
            if ((data[i + 1] & 0xC0) != 0x80) return false;
            if ((data[i + 2] & 0xC0) != 0x80) return false;
            /* 检查过长的编码 */
            if (lead == 0xE0 && data[i + 1] < 0xA0) return false;
            /* 检查 U+FFFE 和 U+FFFF */
            if (lead == 0xEF && data[i + 1] == 0xBF &&
                (data[i + 2] == 0xBE || data[i + 2] == 0xBF))
                return false;
            i += 3;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            /* 四字节序列：11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            if (i + 3 >= size) return false;
            if ((data[i + 1] & 0xC0) != 0x80) return false;
            if ((data[i + 2] & 0xC0) != 0x80) return false;
            if ((data[i + 3] & 0xC0) != 0x80) return false;
            /* 检查过长的编码 */
            if (lead == 0xF0 && data[i + 1] < 0x90) return false;
            if (lead == 0xF4 && data[i + 1] > 0x8F) return false;
            i += 4;
        }
        else
        {
            /* 无效的前导字节（0x80-0xBF 或 0xF5-0xFF） */
            return false;
        }
    }
    return true;
}

bool XByteArrayView_isValidUtf8(const XByteArrayView* self)
{
    if (self == NULL || self->m_data == NULL)
        return true;
    return is_valid_utf8_internal(self->m_data, self->m_size);
}

#endif // XByteArray_ON
