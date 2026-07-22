/**
* @file XAnyStringView.c
* @brief 任意编码字符串视图实现文件（对标 Qt 6.8 QAnyStringView）
* @details 实现 XAnyStringView 的所有 API，包括构造、访问、子视图、
*          比较、编码转换等操作。
* @note XAnyStringView 是值类型，不分配堆内存，不涉及虚函数表。
*       内部使用 tagged union 存储：{数据指针, 编码类型标记 + 长度}。
*/
#include "XAnyStringView.h"
#if XString_ON
#include "XStringView.h"          ///< UTF-16 字符串视图
#include "XLatin1StringView.h"    ///< Latin-1 字符串视图
#include "XUtf8StringView.h"      ///< UTF-8 字符串视图
#include "XByteArrayView.h"       ///< 字节数组视图
#include "XString.h"              ///< XString 字符串容器，用于 toString
#include <string.h>               ///< memcmp、memchr、strlen 等内存操作
#include <stdlib.h>               ///< 数值转换

/* ============================== 内部辅助函数 ============================== */

/**
* @brief 从 m_size_and_tag 中提取编码类型标记
* @param st m_size_and_tag 值
* @return 编码类型枚举
*/
static XAnyStringView_Encoding extract_tag(size_t st)
{
    size_t tag_bits = st & XAnyStringView_TypeMask;
    if (tag_bits == XAnyStringView_Utf16Flag)
        return XAnyStringView_Utf16;
    if (tag_bits == XAnyStringView_Latin1Flag)
        return XAnyStringView_Latin1;
    return XAnyStringView_Utf8;
}

/**
* @brief 从 m_size_and_tag 中提取长度
* @param st m_size_and_tag 值
* @return 长度值
*/
static int64_t extract_size(size_t st)
{
    return (int64_t)(st & XAnyStringView_SizeMask);
}

/**
* @brief 将编码类型和长度编码为 m_size_and_tag
* @param size 长度
* @param enc  编码类型
* @return 编码后的 m_size_and_tag 值
*/
static size_t encode_size_and_tag(int64_t size, XAnyStringView_Encoding enc)
{
    size_t tag = 0;
    switch (enc)
    {
    case XAnyStringView_Latin1:
        tag = XAnyStringView_Latin1Flag;
        break;
    case XAnyStringView_Utf16:
        tag = XAnyStringView_Utf16Flag;
        break;
    default:
        tag = 0;
        break;
    }
    return ((size_t)size & XAnyStringView_SizeMask) | tag;
}

/**
* @brief 将 XChar 转换为 UTF-8 并写入缓冲区
* @param ch   XChar 字符
* @param buf  输出缓冲区（至少 4 字节）
* @return UTF-8 字节数
*/
static int xchar_to_utf8(XChar ch, char* buf)
{
    if (ch <= 0x7F)
    {
        buf[0] = (char)ch;
        return 1;
    }
    else if (ch <= 0x7FF)
    {
        buf[0] = (char)(0xC0 | (ch >> 6));
        buf[1] = (char)(0x80 | (ch & 0x3F));
        return 2;
    }
    else
    {
        buf[0] = (char)(0xE0 | (ch >> 12));
        buf[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (ch & 0x3F));
        return 3;
    }
}

/**
* @brief 从 UTF-8 数据中提取一个 Unicode 码点
* @param data    UTF-8 数据指针
* @param pos     当前位置（输入输出，会更新）
* @param size    数据总长度
* @return Unicode 码点，无效返回 0
*/
static uint32_t utf8_to_codepoint(const char* data, int64_t* pos, int64_t size)
{
    if (*pos >= size)
        return 0;
    unsigned char c = (unsigned char)data[*pos];
    uint32_t cp;
    int extra;
    if (c <= 0x7F)
    {
        cp = c;
        extra = 0;
    }
    else if ((c & 0xE0) == 0xC0)
    {
        cp = c & 0x1F;
        extra = 1;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        cp = c & 0x0F;
        extra = 2;
    }
    else if ((c & 0xF8) == 0xF0)
    {
        cp = c & 0x07;
        extra = 3;
    }
    else
    {
        return 0;
    }
    int i;
    for (i = 0; i < extra; i++)
    {
        if (*pos + 1 + i >= size)
            return 0;
        unsigned char cont = (unsigned char)data[*pos + 1 + i];
        if ((cont & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (cont & 0x3F);
    }
    *pos += extra;
    return cp;
}

/**
* @brief 比较两个字符（ASCII 范围，支持大小写敏感性）
*/
static bool char_equal(char a, char b, int cs)
{
    if (cs == 1)
        return a == b;
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

/**
* @brief 比较两个 XChar（支持大小写敏感性）
*/
static bool xchar_equal(XChar a, XChar b, int cs)
{
    if (cs == 1)
        return a == b;
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    return a == b;
}

/**
* @brief 将 char 转小写
*/
static char char_to_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (char)(ch + 32);
    return ch;
}

/**
* @brief 将 XChar 转小写
*/
static XChar xchar_to_lower(XChar ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return (XChar)(ch + 32);
    return ch;
}

/* ============================== 构造与创建 ============================== */

XAnyStringView XAnyStringView_create(void)
{
    XAnyStringView view;
    view.m_data = NULL;
    view.m_size_and_tag = 0;
    return view;
}

XAnyStringView XAnyStringView_create_stringview(const XStringView* sv)
{
    XAnyStringView view;
    if (sv == NULL || XStringView_isNull(sv))
    {
        view.m_data = NULL;
        view.m_size_and_tag = 0;
        return view;
    }
    view.m_data_utf16 = XStringView_data(sv);
    view.m_size_and_tag = encode_size_and_tag(XStringView_size(sv), XAnyStringView_Utf16);
    return view;
}

XAnyStringView XAnyStringView_create_latin1(const XLatin1StringView* lv)
{
    XAnyStringView view;
    if (lv == NULL || XLatin1StringView_isNull(lv))
    {
        view.m_data = NULL;
        view.m_size_and_tag = 0;
        return view;
    }
    view.m_data_latin1 = XLatin1StringView_data(lv);
    view.m_size_and_tag = encode_size_and_tag(XLatin1StringView_size(lv), XAnyStringView_Latin1);
    return view;
}

XAnyStringView XAnyStringView_create_utf8view(const XUtf8StringView* uv)
{
    XAnyStringView view;
    if (uv == NULL || XUtf8StringView_isNull(uv))
    {
        view.m_data = NULL;
        view.m_size_and_tag = 0;
        return view;
    }
    view.m_data_utf8 = XUtf8StringView_data(uv);
    view.m_size_and_tag = encode_size_and_tag(XUtf8StringView_size(uv), XAnyStringView_Utf8);
    return view;
}

XAnyStringView XAnyStringView_create_bytearrayview(const XByteArrayView* bav)
{
    XAnyStringView view;
    if (bav == NULL)
    {
        view.m_data = NULL;
        view.m_size_and_tag = 0;
        return view;
    }
    view.m_data_utf8 = (const char*)XByteArrayView_data(bav);
    view.m_size_and_tag = encode_size_and_tag(XByteArrayView_size(bav), XAnyStringView_Utf8);
    return view;
}

XAnyStringView XAnyStringView_create_string(const XString* str)
{
    XAnyStringView view;
    if (str == NULL)
    {
        view.m_data = NULL;
        view.m_size_and_tag = 0;
        return view;
    }
    /* XString 内部是 UTF-16 编码 */
    view.m_data_utf16 = XString_data(str);
    view.m_size_and_tag = encode_size_and_tag((int64_t)XString_size(str), XAnyStringView_Utf16);
    return view;
}

XAnyStringView XAnyStringView_create_cstr(const char* str)
{
    XAnyStringView view;
    if (str == NULL)
    {
        view.m_data = NULL;
        view.m_size_and_tag = 0;
        return view;
    }
    view.m_data_utf8 = str;
    view.m_size_and_tag = encode_size_and_tag((int64_t)strlen(str), XAnyStringView_Utf8);
    return view;
}

XAnyStringView XAnyStringView_create_utf8(const char* data, int64_t len)
{
    XAnyStringView view;
    view.m_data_utf8 = data;
    view.m_size_and_tag = (data == NULL) ? 0 : encode_size_and_tag(len, XAnyStringView_Utf8);
    return view;
}

XAnyStringView XAnyStringView_create_utf16(const XChar* data, int64_t len)
{
    XAnyStringView view;
    view.m_data_utf16 = data;
    view.m_size_and_tag = (data == NULL) ? 0 : encode_size_and_tag(len, XAnyStringView_Utf16);
    return view;
}

XAnyStringView XAnyStringView_create_latin1_data(const char* data, int64_t len)
{
    XAnyStringView view;
    view.m_data_latin1 = data;
    view.m_size_and_tag = (data == NULL) ? 0 : encode_size_and_tag(len, XAnyStringView_Latin1);
    return view;
}

/* ============================== 基本访问 ============================== */

const void* XAnyStringView_data(const XAnyStringView* self)
{
    if (self == NULL)
        return NULL;
    return self->m_data;
}

int64_t XAnyStringView_size(const XAnyStringView* self)
{
    if (self == NULL)
        return 0;
    return extract_size(self->m_size_and_tag);
}

bool XAnyStringView_empty(const XAnyStringView* self)
{
    if (self == NULL)
        return true;
    return (extract_size(self->m_size_and_tag) == 0);
}

bool XAnyStringView_isNull(const XAnyStringView* self)
{
    if (self == NULL)
        return true;
    return (self->m_data == NULL);
}

int64_t XAnyStringView_length(const XAnyStringView* self)
{
    return XAnyStringView_size(self);
}

XAnyStringView_Encoding XAnyStringView_encoding(const XAnyStringView* self)
{
    if (self == NULL)
        return XAnyStringView_Utf8;
    return extract_tag(self->m_size_and_tag);
}

bool XAnyStringView_isUtf16(const XAnyStringView* self)
{
    return (XAnyStringView_encoding(self) == XAnyStringView_Utf16);
}

bool XAnyStringView_isUtf8(const XAnyStringView* self)
{
    return (XAnyStringView_encoding(self) == XAnyStringView_Utf8);
}

bool XAnyStringView_isLatin1(const XAnyStringView* self)
{
    return (XAnyStringView_encoding(self) == XAnyStringView_Latin1);
}

size_t XAnyStringView_charSize(const XAnyStringView* self)
{
    if (self == NULL)
        return 1;
    return (extract_tag(self->m_size_and_tag) == XAnyStringView_Utf16) ? 2 : 1;
}

int64_t XAnyStringView_size_bytes(const XAnyStringView* self)
{
    if (self == NULL)
        return 0;
    return XAnyStringView_size(self) * (int64_t)XAnyStringView_charSize(self);
}

/* ============================== 元素访问 ============================== */

XChar XAnyStringView_front(const XAnyStringView* self)
{
    if (self == NULL || self->m_data == NULL)
        return 0;
    int64_t sz = XAnyStringView_size(self);
    if (sz <= 0)
        return 0;

    XAnyStringView_Encoding enc = XAnyStringView_encoding(self);
    switch (enc)
    {
    case XAnyStringView_Utf16:
        return self->m_data_utf16[0];
    case XAnyStringView_Latin1:
        return (XChar)(unsigned char)self->m_data_latin1[0];
    default: /* Utf8 */
    {
        /* 获取第一个 UTF-8 字符的码点 */
        int64_t pos = 0;
        uint32_t cp = utf8_to_codepoint(self->m_data_utf8, &pos, sz);
        return (XChar)(cp > 0xFFFF ? 0xFFFD : cp);
    }
    }
}

XChar XAnyStringView_back(const XAnyStringView* self)
{
    if (self == NULL || self->m_data == NULL)
        return 0;
    int64_t sz = XAnyStringView_size(self);
    if (sz <= 0)
        return 0;

    XAnyStringView_Encoding enc = XAnyStringView_encoding(self);
    switch (enc)
    {
    case XAnyStringView_Utf16:
        return self->m_data_utf16[sz - 1];
    case XAnyStringView_Latin1:
        return (XChar)(unsigned char)self->m_data_latin1[sz - 1];
    default: /* Utf8 */
    {
        /* 从末尾向前查找最后一个 UTF-8 字符的起始位置 */
        int64_t i;
        for (i = sz - 1; i >= 0; i--)
        {
            if ((self->m_data_utf8[i] & 0xC0) != 0x80)
            {
                int64_t pos = i;
                uint32_t cp = utf8_to_codepoint(self->m_data_utf8, &pos, sz);
                return (XChar)(cp > 0xFFFF ? 0xFFFD : cp);
            }
        }
        return 0;
    }
    }
}

/* ============================== 子视图操作 ============================== */

/**
* @brief 内部：前进数据指针（按编码类型调整字节偏移）
* @param self 输入视图
* @param delta 前进的字符数
* @return 新的数据指针值
*/
static const void* advance_data(const XAnyStringView* self, int64_t delta)
{
    XAnyStringView_Encoding enc = XAnyStringView_encoding(self);
    switch (enc)
    {
    case XAnyStringView_Utf16:
        return (const void*)(self->m_data_utf16 + delta);
    case XAnyStringView_Latin1:
        return (const void*)(self->m_data_latin1 + delta);
    default: /* Utf8 */
        return (const void*)(self->m_data_utf8 + delta);
    }
}

XAnyStringView XAnyStringView_first_n(const XAnyStringView* self, int64_t n)
{
    XAnyStringView result;
    if (self == NULL || self->m_data == NULL || n <= 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size_and_tag = (self == NULL) ? 0 : (self->m_size_and_tag & XAnyStringView_TypeMask);
        return result;
    }
    int64_t sz = XAnyStringView_size(self);
    if (n > sz)
        n = sz;
    result.m_data = self->m_data;
    result.m_size_and_tag = encode_size_and_tag(n, XAnyStringView_encoding(self));
    return result;
}

XAnyStringView XAnyStringView_last_n(const XAnyStringView* self, int64_t n)
{
    XAnyStringView result;
    if (self == NULL || self->m_data == NULL || n <= 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size_and_tag = (self == NULL) ? 0 : (self->m_size_and_tag & XAnyStringView_TypeMask);
        return result;
    }
    int64_t sz = XAnyStringView_size(self);
    if (n > sz)
        n = sz;
    result.m_data = advance_data(self, sz - n);
    result.m_size_and_tag = encode_size_and_tag(n, XAnyStringView_encoding(self));
    return result;
}

XAnyStringView XAnyStringView_sliced(const XAnyStringView* self, int64_t pos)
{
    XAnyStringView result;
    if (self == NULL || self->m_data == NULL || pos < 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size_and_tag = (self == NULL) ? 0 : (self->m_size_and_tag & XAnyStringView_TypeMask);
        return result;
    }
    int64_t sz = XAnyStringView_size(self);
    if (pos > sz)
        pos = sz;
    result.m_data = advance_data(self, pos);
    result.m_size_and_tag = encode_size_and_tag(sz - pos, XAnyStringView_encoding(self));
    return result;
}

XAnyStringView XAnyStringView_sliced_2(const XAnyStringView* self, int64_t pos, int64_t n)
{
    XAnyStringView result;
    if (self == NULL || self->m_data == NULL || pos < 0 || n < 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size_and_tag = (self == NULL) ? 0 : (self->m_size_and_tag & XAnyStringView_TypeMask);
        return result;
    }
    int64_t sz = XAnyStringView_size(self);
    if (pos > sz)
        pos = sz;
    if (pos + n > sz)
        n = sz - pos;
    result.m_data = advance_data(self, pos);
    result.m_size_and_tag = encode_size_and_tag(n, XAnyStringView_encoding(self));
    return result;
}

XAnyStringView XAnyStringView_chopped(const XAnyStringView* self, int64_t n)
{
    XAnyStringView result;
    if (self == NULL || self->m_data == NULL || n < 0)
    {
        result.m_data = (self == NULL) ? NULL : self->m_data;
        result.m_size_and_tag = (self == NULL) ? 0 : (self->m_size_and_tag & XAnyStringView_TypeMask);
        return result;
    }
    int64_t sz = XAnyStringView_size(self);
    int64_t new_size = sz - n;
    if (new_size < 0)
        new_size = 0;
    result.m_data = self->m_data;
    result.m_size_and_tag = encode_size_and_tag(new_size, XAnyStringView_encoding(self));
    return result;
}

/* ============================== 原地修改 ============================== */

void XAnyStringView_truncate(XAnyStringView* self, int64_t n)
{
    if (self == NULL)
        return;
    if (n < 0)
        n = 0;
    int64_t sz = XAnyStringView_size(self);
    if (n > sz)
        n = sz;
    self->m_size_and_tag = encode_size_and_tag(n, XAnyStringView_encoding(self));
}

void XAnyStringView_chop(XAnyStringView* self, int64_t n)
{
    if (self == NULL)
        return;
    if (n < 0)
        n = 0;
    int64_t sz = XAnyStringView_size(self);
    int64_t new_size = sz - n;
    if (new_size < 0)
        new_size = 0;
    self->m_size_and_tag = encode_size_and_tag(new_size, XAnyStringView_encoding(self));
}

/* ============================== 比较操作 ============================== */

/**
* @brief 内部：从 XAnyStringView 获取第 idx 个字符的 XChar 值
* @param self 视图指针
* @param idx  索引
* @return XChar 值
*/
static XChar anyview_at(const XAnyStringView* self, int64_t idx)
{
    XAnyStringView_Encoding enc = XAnyStringView_encoding(self);
    switch (enc)
    {
    case XAnyStringView_Utf16:
        return self->m_data_utf16[idx];
    case XAnyStringView_Latin1:
        return (XChar)(unsigned char)self->m_data_latin1[idx];
    default: /* Utf8 */
    {
        int64_t pos = 0;
        int64_t sz = XAnyStringView_size(self);
        int64_t i;
        for (i = 0; i <= idx && pos < sz; i++)
        {
            uint32_t cp = utf8_to_codepoint(self->m_data_utf8, &pos, sz);
            if (i == idx)
                return (XChar)(cp > 0xFFFF ? 0xFFFD : cp);
            pos++; /* 跳过当前字符 */
        }
        return 0;
    }
    }
}

bool XAnyStringView_equal(const XAnyStringView* self, const XAnyStringView* other)
{
    if (self == NULL || other == NULL)
        return (self == other);
    int64_t sz1 = XAnyStringView_size(self);
    int64_t sz2 = XAnyStringView_size(other);
    if (sz1 != sz2)
        return false;
    if (self->m_data == NULL && other->m_data == NULL)
        return true;
    if (self->m_data == NULL || other->m_data == NULL)
        return false;
    if (sz1 == 0)
        return true;

    /* 如果编码相同且都是单字节编码，可以用 memcmp */
    XAnyStringView_Encoding enc1 = XAnyStringView_encoding(self);
    XAnyStringView_Encoding enc2 = XAnyStringView_encoding(other);
    if (enc1 == enc2 && enc1 != XAnyStringView_Utf16)
    {
        const char* d1 = (enc1 == XAnyStringView_Latin1) ? self->m_data_latin1 : self->m_data_utf8;
        const char* d2 = (enc2 == XAnyStringView_Latin1) ? other->m_data_latin1 : other->m_data_utf8;
        return (memcmp(d1, d2, (size_t)sz1) == 0);
    }

    /* 逐字符比较 */
    int64_t i;
    for (i = 0; i < sz1; i++)
    {
        XChar c1 = anyview_at(self, i);
        XChar c2 = anyview_at(other, i);
        if (c1 != c2)
            return false;
    }
    return true;
}

int XAnyStringView_compare(const XAnyStringView* self, const XAnyStringView* other, int cs)
{
    if (self == NULL && other == NULL)
        return 0;
    if (self == NULL)
        return -1;
    if (other == NULL)
        return 1;

    int64_t sz1 = XAnyStringView_size(self);
    int64_t sz2 = XAnyStringView_size(other);
    int64_t min_len = (sz1 < sz2) ? sz1 : sz2;

    int64_t i;
    for (i = 0; i < min_len; i++)
    {
        XChar a = anyview_at(self, i);
        XChar b = anyview_at(other, i);
        if (cs == 0) /* CaseInsensitive */
        {
            a = xchar_to_lower(a);
            b = xchar_to_lower(b);
        }
        if (a != b)
            return (a < b) ? -1 : 1;
    }
    if (sz1 < sz2)
        return -1;
    if (sz1 > sz2)
        return 1;
    return 0;
}

/* ============================== 编码转换 ============================== */

XString* XAnyStringView_toString(const XAnyStringView* self)
{
    if (self == NULL || self->m_data == NULL)
        return XString_create();

    int64_t sz = XAnyStringView_size(self);
    if (sz <= 0)
        return XString_create();

    XAnyStringView_Encoding enc = XAnyStringView_encoding(self);
    switch (enc)
    {
    case XAnyStringView_Utf16:
    {
        /* 直接使用 UTF-16 数据创建 XString */
        XString* result = XString_create();
        if (result == NULL)
            return NULL;
        int64_t i;
        for (i = 0; i < sz; i++)
        {
            char utf8_buf[4];
            int utf8_len = xchar_to_utf8(self->m_data_utf16[i], utf8_buf);
            XString_append_with_length_utf8(result, utf8_buf, (size_t)utf8_len);
        }
        return result;
    }
    case XAnyStringView_Latin1:
    {
        /* Latin-1 每个字节直接映射到 Unicode 码点 0-255 */
        XString* result = XString_create();
        if (result == NULL)
            return NULL;
        int64_t i;
        for (i = 0; i < sz; i++)
        {
            unsigned char cp = (unsigned char)self->m_data_latin1[i];
            char utf8_buf[4];
            int utf8_len;
            if (cp <= 0x7F)
            {
                utf8_buf[0] = (char)cp;
                utf8_len = 1;
            }
            else
            {
                utf8_buf[0] = (char)(0xC0 | (cp >> 6));
                utf8_buf[1] = (char)(0x80 | (cp & 0x3F));
                utf8_len = 2;
            }
            XString_append_with_length_utf8(result, utf8_buf, (size_t)utf8_len);
        }
        return result;
    }
    default: /* Utf8 */
    {
        XString* result = XString_create();
        if (result == NULL)
            return NULL;
        XString_append_with_length_utf8(result, self->m_data_utf8, (size_t)sz);
        return result;
    }
    }
}

#endif // XString_ON
