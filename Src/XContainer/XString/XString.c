#include "XString.h"
#include "XVariant.h"
#include "XVariantTypeOps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include "XStringList.h"
#include "XStringView.h"
#include "XCryptographic.h"

XVARIANT_TYPE_OPS_DEFINE(XString, sizeof(XString), XString_copy_base,
	XString_move_base, XString_clear_base, XString_deinit_base,
	XString_compare, "XString");

XVariant* XString_toVariant(const XString* str)
{
	XVariant* var;
	if (!str)
		return NULL;
	var = XVariant_create(NULL, sizeof(XString), XVariantType_String);
	if (!var)
		return NULL;
	XString_init((XString*)XVariant_data(var));
	XString_copy_base(XVariant_data(var), str);
	return var;
}

XVariant* XString_toVariant_move(XString* str)
{
	XVariant* var;
	if (!str)
		return NULL;
	var = XVariant_create(NULL, sizeof(XString), XVariantType_String);
	if (!var)
		return NULL;
	XString_init((XString*)XVariant_data(var));
	XString_move_base(XVariant_data(var), str);
	return var;
}

XVariant* XString_toVariant_ref(XString* str)
{
	XVariant* var;
	if (!str)
		return NULL;
	var = XVariant_create(NULL, 0, XVariantType_String);
	if (!var)
		return NULL;
	XVariant_setDataRef(var, str, sizeof(XString), XVariantType_String);
	return var;
}

XVariant* XString_toVariant_utf8(const char* utf8)
{
	XVariant* var;
	if (!utf8)
		return NULL;
	var = XVariant_create(NULL, sizeof(XString), XVariantType_String);
	if (!var)
		return NULL;
	XString_init((XString*)XVariant_data(var));
	XString_assign_utf8(XVariant_data(var), utf8);
	return var;
}

XString* XString_fromVariant(const XVariant* var)
{
	return XString_create_copy(XString_fromVariant_ref(var));
}

const XString* XString_fromVariant_const(const XVariant* var)
{
	return (const XString*)XVariant_toRef(var, XVariantType_String);
}

XString* XString_fromVariant_ref(const XVariant* var)
{
	return (XString*)XString_fromVariant_const(var);
}

static bool XString_prepareVariant(XVariant* var)
{
	if (!var)
		return false;
	if (var->m_type != XVariantType_String)
	{
		XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XString));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XString);
		XString_init((XString*)var->m_data);
		var->m_type = XVariantType_String;
	}
	else if (!var->m_data || var->m_dataSize != sizeof(XString))
	{
		if (var->m_data)
			XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XString));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XString);
		XString_init((XString*)var->m_data);
	}
	return true;
}

void XString_setVariant(XVariant* var, const XString* str)
{
	if (!str || !XString_prepareVariant(var))
		return;
	XString_copy_base(XVariant_data(var), str);
}

void XString_setVariant_move(XVariant* var, XString* str)
{
	if (!str || !XString_prepareVariant(var))
		return;
	XString_move_base(XVariant_data(var), str);
}

void XString_setVariant_ref(XVariant* var, XString* str)
{
	if (!var || !str)
		return;
	XVariant_setDataRef(var, str, sizeof(XString), XVariantType_String);
}

void XString_setVariant_utf8(XVariant* var, const char* utf8)
{
	if (!utf8 || !XString_prepareVariant(var))
		return;
	XString_assign_utf8(XVariant_data(var), utf8);
}
#if XRegularExpression_ON
#include "XCryptographic.h"
#include "XRegularExpression.h"
#endif

#if XRegularExpression_ON

static XString* XString_regularExpression_substring(const XString* str, size_t pos, size_t length)
{
    if (length == 0) return XString_create();
    return XString_mid(str, pos, length);
}

static bool XString_append_match_replacement(XString* result, const XString* after,
                                              const XRegularExpressionMatch* match)
{
    if (!result || !after || !match) return false;
    const XChar* data = XString_unicode(after);
    size_t length = XString_size_base(after);
    int captureCount = XRegularExpressionMatch_lastCapturedIndex(match);
    for (size_t i = 0; i < length; ++i) {
        if (data && data[i] == '\\' && i + 1 < length &&
                data[i + 1] >= '0' && data[i + 1] <= '9') {
            int index = (int)(data[i + 1] - '0');
            size_t consumed = 0;
            if (index > 0 && index <= captureCount) {
                consumed = 1;
                if (i + 2 < length && data[i + 2] >= '0' && data[i + 2] <= '9') {
                    int twoDigitIndex = index * 10 + (int)(data[i + 2] - '0');
                    if (twoDigitIndex <= captureCount) {
                        index = twoDigitIndex;
                        consumed = 2;
                    }
                }
            }
            if (consumed) {
                XString* captured = XRegularExpressionMatch_captured(match, index);
                if (!captured) return false;
                if (!XString_append(result, captured)) {
                    XString_delete_base(captured);
                    return false;
                }
                XString_delete_base(captured);
                i += consumed;
                continue;
            }
        }
        if (!XString_append_char(result, data ? data[i] : 0)) {
            return false;
        }
    }
    return true;
}

int64_t XString_indexOf_regularExpression(const XString* str,
                                          const XRegularExpression* expression,
                                          int64_t from,
                                          XRegularExpressionMatch* match)
{
    if (!str || !expression) return -1;
    XRegularExpressionMatch* result = XRegularExpression_match(
            expression, str, from, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    if (!result) return -1;
    bool hasMatch = XRegularExpressionMatch_hasMatch(result);
    int64_t position = hasMatch ?
            XRegularExpressionMatch_capturedStart(result, 0) : -1;
    if (match && hasMatch) XRegularExpressionMatch_copy_base(match, result);
    XRegularExpressionMatch_delete_base(result);
    return position;
}

int64_t XString_lastIndexOf_regularExpression(const XString* str,
                                               const XRegularExpression* expression,
                                               int64_t from,
                                               XRegularExpressionMatch* match)
{
    if (!str || !expression) return -1;
    int64_t length = (int64_t)XString_size_base(str);
    int64_t endPosition;
    if (from < 0) {
        endPosition = length + from + 1;
    } else if (from >= length) {
        endPosition = length + 1;
    } else {
        endPosition = from + 1;
    }
    XRegularExpressionMatchIterator* iterator = XRegularExpression_globalMatch(
            expression, str, 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    if (!iterator) return -1;

    int64_t resultPosition = -1;
    XRegularExpressionMatch* last = NULL;
    while (XRegularExpressionMatchIterator_hasNext(iterator)) {
        XRegularExpressionMatch* current = XRegularExpressionMatchIterator_next(iterator);
        if (!current) break;
        int64_t position = XRegularExpressionMatch_capturedStart(current, 0);
        if (position < 0 || position >= endPosition) {
            XRegularExpressionMatch_delete_base(current);
            break;
        }
        if (last) XRegularExpressionMatch_delete_base(last);
        last = current;
        resultPosition = position;
    }
    if (match && last) XRegularExpressionMatch_copy_base(match, last);
    if (last) XRegularExpressionMatch_delete_base(last);
    XRegularExpressionMatchIterator_delete_base(iterator);
    return resultPosition;
}

bool XString_contains_regularExpression(const XString* str,
                                        const XRegularExpression* expression)
{
    return XString_contains_regularExpression_2(str, expression, NULL);
}

bool XString_contains_regularExpression_2(const XString* str,
                                          const XRegularExpression* expression,
                                          XRegularExpressionMatch* match)
{
    return XString_indexOf_regularExpression(str, expression, 0, match) >= 0;
}

size_t XString_count_regularExpression(const XString* str,
                                       const XRegularExpression* expression)
{
    if (!str || !expression) return 0;
    size_t count = 0;
    int64_t index = -1;
    int64_t length = (int64_t)XString_size_base(str);
    const XChar* data = XString_unicode(str);
    while (index <= length - 1) {
        XRegularExpressionMatch* match = XRegularExpression_match(
                expression, str, index + 1, XRegularExpression_NormalMatch,
                XRegularExpression_NoMatchOption);
        if (!match) break;
        if (!XRegularExpressionMatch_hasMatch(match)) {
            XRegularExpressionMatch_delete_base(match);
            break;
        }
        ++count;
        index = XRegularExpressionMatch_capturedStart(match, 0);
        if (index >= 0 && index < length && data && XChar_isHighSurrogate(data[index]))
            ++index;
        XRegularExpressionMatch_delete_base(match);
    }
    return count;
}

bool XString_replace_regularExpression(XString* str,
                                       const XRegularExpression* expression,
                                       const XString* after)
{
    if (!str || !expression || !after) return false;
    XRegularExpressionMatchIterator* iterator = XRegularExpression_globalMatch(
            expression, str, 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    if (!iterator) return false;
    XString result;
    XString_init(&result);
    size_t cursor = 0;
    bool replaced = false;
    while (XRegularExpressionMatchIterator_hasNext(iterator)) {
        XRegularExpressionMatch* match = XRegularExpressionMatchIterator_next(iterator);
        if (!match) break;
        int64_t start = XRegularExpressionMatch_capturedStart(match, 0);
        int64_t end = XRegularExpressionMatch_capturedEnd(match, 0);
        if (start < 0 || end < start || (size_t)start < cursor) {
            XRegularExpressionMatch_delete_base(match);
            XString_deinit_base(&result);
            XRegularExpressionMatchIterator_delete_base(iterator);
            return false;
        }
        XString* prefix = XString_regularExpression_substring(str, cursor, (size_t)start - cursor);
        bool ok = prefix && XString_append(&result, prefix);
        if (prefix) XString_delete_base(prefix);
        if (ok) ok = XString_append_match_replacement(&result, after, match);
        if (!ok) {
            XRegularExpressionMatch_delete_base(match);
            XString_deinit_base(&result);
            XRegularExpressionMatchIterator_delete_base(iterator);
            return false;
        }
        cursor = (size_t)end;
        replaced = true;
        XRegularExpressionMatch_delete_base(match);
    }
    XRegularExpressionMatchIterator_delete_base(iterator);
    if (!replaced) {
        XString_deinit_base(&result);
        return true;
    }

    XString* suffix = XString_regularExpression_substring(
            str, cursor, XString_size_base(str) - cursor);
    if (!suffix || !XString_append(&result, suffix)) {
        if (suffix) XString_delete_base(suffix);
        XString_deinit_base(&result);
        return false;
    }
    XString_delete_base(suffix);
    XString_move_base(str, &result);
    XString_deinit_base(&result);
    return true;
}

bool XString_remove_regularExpression(XString* str,
                                      const XRegularExpression* expression)
{
    if (!str || !expression) return false;
    XString* empty = XString_create();
    if (!empty) return false;
    bool result = XString_replace_regularExpression(str, expression, empty);
    XString_delete_base(empty);
    return result;
}

XStringList* XString_split_regularExpression(const XString* str,
                                             const XRegularExpression* separator,
                                             bool keepEmptyParts)
{
    if (!str || !separator) return NULL;
    XStringList* result = XStringList_create();
    if (!result) return NULL;
    if (!XRegularExpression_isValid(separator)) return result;
    XRegularExpressionMatchIterator* iterator = XRegularExpression_globalMatch(
            separator, str, 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    if (!iterator) {
        XStringList_delete_base(result);
        return NULL;
    }

    size_t cursor = 0;
    while (XRegularExpressionMatchIterator_hasNext(iterator)) {
        XRegularExpressionMatch* match = XRegularExpressionMatchIterator_next(iterator);
        if (!match) break;
        int64_t start = XRegularExpressionMatch_capturedStart(match, 0);
        int64_t end = XRegularExpressionMatch_capturedEnd(match, 0);
        if (start < 0 || end < start || (size_t)start < cursor) {
            XRegularExpressionMatch_delete_base(match);
            XRegularExpressionMatchIterator_delete_base(iterator);
            XStringList_delete_base(result);
            return NULL;
        }
        XString* part = XString_regularExpression_substring(str, cursor, (size_t)start - cursor);
        if (part && (keepEmptyParts || !XString_isEmpty_base(part)))
            XStringList_push_back_base(result, part);
        if (part) XString_delete_base(part);
        cursor = (size_t)end;
        XRegularExpressionMatch_delete_base(match);
    }
    XRegularExpressionMatchIterator_delete_base(iterator);

    XString* tail = XString_regularExpression_substring(
            str, cursor, XString_size_base(str) - cursor);
    if (tail && (keepEmptyParts || !XString_isEmpty_base(tail)))
        XStringList_push_back_base(result, tail);
    if (tail) XString_delete_base(tail);
    return result;
}

#endif /* XRegularExpression_ON */

// 内部常量定义
#define UTF8_CACHE_SIZE 1024  // 初始UTF-8缓存大小
#define XSTRING_MIN_CAPACITY 16  // 最小容量（不含结束符）
#define XString_cdata(str) ((const XChar*)XContainerDataAddr(str))

// ==================== 内部公共宏：消除重复模式 ====================

// setNum系列宏：整数类型统一实现
#define DEFINE_SETNUM_INT(FuncName, FromFunc, IntType) \
bool FuncName(XString* str, IntType n, int base) { \
    if (!str || base < 2 || base > 36) return false; \
    int64_t len = FromFunc(n, base, NULL, 0, true); \
    if (len == -1) return false; \
    XString_resize(str, len); \
    return FromFunc(n, base, XString_data(str), len + 1, true) != -1; \
}

// setNum浮点类型统一实现
#define DEFINE_SETNUM_FLOAT(FuncName, FromFunc, FloatType) \
bool FuncName(XString* str, FloatType n, char format, int precision) { \
    if (!str) return false; \
    int64_t len = FromFunc(n, format, NULL, 0, precision); \
    if (len == -1) return false; \
    XString_resize(str, len); \
    return FromFunc(n, format, XString_data(str), len + 1, precision) != -1; \
}

// number系列宏：基于setNum创建新XString
#define DEFINE_NUMBER(FuncName, SetNumFunc, NumType) \
XString* FuncName(NumType n, int base) { \
    XString* str = XString_create(); \
    if (!str) return NULL; \
    if (!SetNumFunc(str, n, base)) { XString_delete_base(str); return NULL; } \
    return str; \
}

// number浮点系列宏
#define DEFINE_NUMBER_FLOAT(FuncName, SetNumFunc, FloatType) \
XString* FuncName(FloatType n, char format, int precision) { \
    XString* str = XString_create(); \
    if (!str) return NULL; \
    if (!SetNumFunc(str, n, format, precision)) { XString_delete_base(str); return NULL; } \
    return str; \
}

// 数值转换系列宏：XString -> 数值
#define DEFINE_TO_NUM(FuncName, RetType, ToFunc, DefaultVal) \
RetType FuncName(const XString* str, bool* ok, int base) { \
    if (!str) { if (ok) *ok = false; return DefaultVal; } \
    return ToFunc(XString_cdata(str), XContainerSize(str), base, ok); \
}

// toXxx_length缓存长度查询宏
#define DEFINE_TO_LENGTH(FuncName, ToFunc, CacheType) \
size_t FuncName(const XString* str) { \
    if (!str || XString_isEmpty_base(str)) return 0; \
    if (ToFunc(str)) return str->m_cache[CacheType].m_length; \
    return 0; \
}

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
// 辅助函数：计算KMP前缀表
static void compute_prefix(const XChar* pattern, size_t m, int* prefix, XChar_CaseSensitivity cs);
// 辅助函数：KMP反向搜索（用于last_index_of）
static int64_t kmp_reverse_search(const XChar* text, size_t n, const XChar* pattern, size_t m,
    XChar_CaseSensitivity cs, size_t start_idx);
// 辅助函数：KMP正向搜索
static int64_t kmp_search(const XChar* text, size_t n,const XChar* pattern, size_t m,const int* prefix, XChar_CaseSensitivity cs,size_t from);

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
    XString* str = (XString*)XMemory_malloc(sizeof(XString),
        XContainer_memory_type((const XContainer*)other));
    if (!str) return NULL;
    memset(str, 0, sizeof(XString));

    XString_copy_base(str, other);
    Set_Class_IsHeap(str, true);

    return str;
}
XString* XString_create_move(XString* other)
{
    if (other==NULL)
        return NULL;
    XString* str = (XString*)XMemory_malloc(sizeof(XString),
        XContainer_memory_type((const XContainer*)other));
    if (!str) return NULL;
    memset(str, 0, sizeof(XString));
    XString_move_base(str,other);
    Set_Class_IsHeap(str, true);
    return str;
}
// 字符串创建函数
XString* XString_create_utf8(const char* utf8_str) 
{
    return XString_create_with_length_utf8(utf8_str, utf8_str ? strlen(utf8_str) : 0);
}

XString* XString_create_with_length_utf8(const char* utf8_str, size_t len)
{
    XString* str = (XString*)XClass_Malloc(XString);
    if (!str) return NULL;
    XString_init(str);
    size_t actual_len = (len == 0 && utf8_str) ? strlen(utf8_str) : len;
    if (utf8_str && actual_len > 0)
    {
        int xchar_count = XChar_fromUtf8Stream((const uint8_t*)utf8_str,len, NULL, 0);
        if (xchar_count > 0) {
            XString_reserve(str, xchar_count);
            xchar_count = XChar_fromUtf8Stream((const uint8_t*)utf8_str,len, XString_data(str), xchar_count + 1);
            str->parent.m_size = xchar_count;
        }
    }
    Set_Class_IsHeap(str, true);
    return str;
}

XString* XString_create_utf16(const uint16_t* utf16_str)
{
    if (!utf16_str) return NULL;
    // 计算 UTF-16 字符串长度
    size_t len = 0;
    while (utf16_str[len] != 0) {
        len++;
    }
    return XString_create_with_length_utf16(utf16_str, len);
}

XString* XString_create_with_length_utf16(const uint16_t* utf16_str, size_t len)
{
    if (!utf16_str || len == 0) {
        return NULL;
    }

    // 步骤1：计算转换所需的XChar数量（首次调用获取长度）
    int64_t xchar_count = XChar_fromUtf16Stream(utf16_str, len, NULL, 0);
    if (xchar_count <= 0) {
        return NULL;
    }

    // 步骤2：创建并初始化XString
    XString* str = (XString*)XClass_Malloc(XString);
    if (!str) {
        return NULL;
    }
    XString_init(str);

    // 步骤3：预留足够空间（包含终止符）
    XString_reserve(str, (size_t)xchar_count);

    // 步骤4：执行实际转换
    XChar* data = XString_data(str);
    xchar_count = XChar_fromUtf16Stream(utf16_str, len, data, (size_t)xchar_count + 1);

    if (xchar_count <= 0) {
        XString_delete_base(str);
        return NULL;
    }

    // 步骤5：设置长度和终止符（与Qt QString一致，内部保留尾部0）
    str->parent.m_size = (size_t)xchar_count;
    data[xchar_count] = 0;

    XString_deinitCache(str);
    Set_Class_IsHeap(str, true);
    return str;
}

// 从Latin-1字符串创建XString（对齐Qt QString::fromLatin1）
XString* XString_create_latin1(const char* latin1_str)
{
    if (!latin1_str) return NULL;
    return XString_create_with_length_latin1((const uint8_t*)latin1_str, strlen(latin1_str));
}

XString* XString_create_with_length_latin1(const uint8_t* latin1, size_t len)
{
    if (!latin1 || len == 0) return XString_create();
    int64_t xchar_count = XChar_fromLatin1Stream(latin1, len, NULL, 0);
    if (xchar_count <= 0) return XString_create();
    XString* str = (XString*)XClass_Malloc(XString);
    if (!str) return NULL;
    XString_init(str);
    XString_reserve(str, (size_t)xchar_count);
    XChar* data = XString_data(str);
    xchar_count = XChar_fromLatin1Stream(latin1, len, data, (size_t)xchar_count + 1);
    str->parent.m_size = (size_t)xchar_count;
    data[xchar_count] = 0;
    XString_deinitCache(str);
    Set_Class_IsHeap(str, true);
    return str;
}

// 从UTF-32(UCS-4)字符串创建XString（对齐Qt QString::fromUcs4）
XString* XString_create_utf32(const uint32_t* ucs4)
{
    if (!ucs4) return NULL;
    size_t len = 0;
    while (ucs4[len] != 0) len++;
    return XString_create_with_length_utf32(ucs4, len);
}

XString* XString_create_with_length_utf32(const uint32_t* ucs4, size_t len)
{
    if (!ucs4 || len == 0) return XString_create();
    int64_t xchar_count = XChar_fromUtf32Stream(ucs4, len, NULL, 0);
    if (xchar_count <= 0) return XString_create();
    XString* str = (XString*)XClass_Malloc(XString);
    if (!str) return NULL;
    XString_init(str);
    XString_reserve(str, (size_t)xchar_count);
    XChar* data = XString_data(str);
    xchar_count = XChar_fromUtf32Stream(ucs4, len, data, (size_t)xchar_count + 1);
    str->parent.m_size = (size_t)xchar_count;
    data[xchar_count] = 0;
    XString_deinitCache(str);
    Set_Class_IsHeap(str, true);
    return str;
}

// 从本地8位编码字符串创建XString（对齐Qt QString::fromLocal8Bit）
XString* XString_create_local(const char* local_str)
{
    if (!local_str) return NULL;
#ifdef _WIN32
    return XString_create_gbk(local_str);
#else
    return XString_create_utf8(local_str);
#endif
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
    XMemory* default_memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
    char* gbk_buf = default_memory && default_memory->malloc
        ? (char*)default_memory->malloc(fmt_len + 1) : NULL; // +1 用于终止符
    if (!gbk_buf) return NULL;

    va_start(args, format);
    vsnprintf(gbk_buf, fmt_len + 1, format, args);
    va_end(args);

    // 第三步：通过已实现的函数创建XString
    XString* str = XString_create_gbk(gbk_buf);
    if (default_memory && default_memory->free)
        default_memory->free(gbk_buf); // 释放临时缓冲区

    return str;
}

XString* XString_create_with_length_gbk(const char* gbk_str, size_t len)
{
    if (!gbk_str || len == 0) {
        return NULL;
    }

    // 步骤1：创建带空终止符的临时GBK缓冲区（避免原函数越界读取）
    XMemory* default_memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
    char* temp_gbk = default_memory && default_memory->malloc
        ? (char*)default_memory->malloc(len + 1) : NULL; // +1 用于存储'\0'
    if (!temp_gbk) {
        return NULL;
    }
    memcpy(temp_gbk, gbk_str, len); // 复制指定长度的GBK数据
    temp_gbk[len] = '\0'; // 添加空终止符，适配XChar_from_gbk的要求

    // 步骤2：计算转换所需的XChar数量（首次调用获取长度）
    int64_t xchar_count = XChar_fromGbkStream(temp_gbk, len, NULL, 0);
    if (xchar_count <= 0) {
        if (default_memory && default_memory->free)
            default_memory->free(temp_gbk); // 释放临时缓冲区
        return NULL;
    }

    // 步骤3：创建并初始化XString
    XString* str = (XString*)XClass_Malloc(XString);
    if (!str) {
        if (default_memory && default_memory->free)
            default_memory->free(temp_gbk);
        return NULL;
    }
    XString_init(str);

    // 步骤4：预留足够空间（包含终止符）
    XString_reserve(str, (size_t)xchar_count);

    // 步骤5：执行实际转换（使用临时缓冲区）
    XChar* data = XString_data(str);
    xchar_count = XChar_fromGbkStream(temp_gbk, len, data, (size_t)xchar_count + 1); // +1 预留终止符位置
    if (default_memory && default_memory->free)
        default_memory->free(temp_gbk); // 转换完成后释放临时缓冲区

    if (xchar_count <= 0) {
        XString_delete_base(str);
        return NULL;
    }

    // 步骤6：设置长度和终止符（与Qt QString一致，内部保留尾部0）
    str->parent.m_size = (size_t)xchar_count;
    data[xchar_count] = 0;

    XString_deinitCache(str);
    Set_Class_IsHeap(str, true);
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
    XMemory* default_memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
    char* buf = default_memory && default_memory->malloc
        ? (char*)default_memory->malloc(buf_size + 1) : NULL;
    if (!buf) {
        va_end(args);
        return NULL;     // 内存分配失败
    }

    // 第二次调用：实际写入格式化后的字符串
    vsnprintf(buf, buf_size + 1, format, args);
    va_end(args);  // 释放原始参数列表

    // 创建UTF-8编码的XString并释放临时缓冲区
    XString* str = XString_create_utf8(buf);
    if (default_memory && default_memory->free)
        default_memory->free(buf);

    return str;
}

// 初始化函数
void XString_init(XString* str)
{
    if (!str) return;
    XContainer_init(str, sizeof(XChar),true);

    //// 创建初始数据缓冲区
    //void* data = XMalloc_System(sizeof(XChar) * (XSTRING_MIN_CAPACITY + 1));
    //if (data)
    //{
    //    memset(data, 0, sizeof(XChar) * (XSTRING_MIN_CAPACITY + 1));
    //    XSharedData* sd = XSharedData_create(data);
    //    if (sd)
    //        XContainerSetDataPtr(str, sd);
    //    else
    //    {
    //        XFree_System(data);
    //    }
    //}
    //XContainerCapacity(str) = XSTRING_MIN_CAPACITY;
    XContainerSetDataPtr(str, NULL);
    str->m_cache = NULL;
    XClassSetVtable(str,XString);
}

const char* XString_toUtf8(const XString* str)
{
    if (!str || XString_isEmpty_base(str)) return NULL;

    // 缓存已存在则直接返回
    if (str->m_cache&& str->m_cache[XStringCache_Utf8].m_data) return str->m_cache[XStringCache_Utf8].m_data;

    // 计算所需UTF-8缓冲区大小（不包含结束符）
    int64_t utf8_len = XChar_toUtf8Stream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (utf8_len <= 0) return NULL;
    
    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf8_len * sizeof(uint8_t) + sizeof(uint8_t);
    uint16_t* utf8_buf = (uint16_t*)XContainer_malloc((const XContainer*)str, buf_size);
    if (!utf8_buf) return NULL;

    // 调用XChar转换函数（使用内部结束符自动处理）
    int64_t result = XChar_toUtf8Stream(XString_cdata(str), XString_length_base(str), (uint8_t*)utf8_buf, utf8_len+1);
    if (result <= 0) 
    {
        XContainer_free((const XContainer*)str, utf8_buf);
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
    if (!str || XString_isEmpty_base(str))
        return 0;
    if (XString_toUtf8(str))
        return str->m_cache[XStringCache_Utf8].m_length;
    return 0;
}

const uint16_t* XString_toUtf16(const XString* str)
{
    if (!str || XString_isEmpty_base(str)) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Utf16].m_data) 
    {
        return (const uint16_t*)str->m_cache[XStringCache_Utf16].m_data;
    }

    // 计算UTF-16所需缓冲区大小（包含终止符）
    int64_t utf16_len = XChar_toUtf16Stream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (utf16_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf16_len * sizeof(uint16_t) + sizeof(uint16_t);
    uint16_t* utf16_buf = (uint16_t*)XContainer_malloc((const XContainer*)str, buf_size);
    if (!utf16_buf) return NULL;

    // 执行转换
    int64_t result = XChar_toUtf16Stream(XString_cdata(str), XString_length_base(str), utf16_buf, utf16_len + 1);
    if (result <= 0) {
        XContainer_free((const XContainer*)str, utf16_buf);
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
    if (!str || XString_isEmpty_base(str)) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Utf32].m_data) 
    {
        return (const uint32_t*)str->m_cache[XStringCache_Utf32].m_data;
    }

    // 计算UTF-32所需缓冲区大小（包含终止符）
    int64_t utf32_len = XChar_toUtf32Stream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (utf32_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t buf_size = (size_t)utf32_len * sizeof(uint32_t) + sizeof(uint32_t);
    uint32_t* utf32_buf = (uint32_t*)XContainer_malloc((const XContainer*)str, buf_size);
    if (!utf32_buf) return NULL;

    // 执行转换
    int64_t result = XChar_toUtf32Stream(XString_cdata(str), XString_length_base(str), utf32_buf, utf32_len + 1);
    if (result <= 0) {
        XContainer_free((const XContainer*)str, utf32_buf);
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
    if (!str || XString_isEmpty_base(str))
        return 0;
    if (XString_toUtf32(str))
        return str->m_cache[XStringCache_Utf32].m_length;
    return 0;
}

const char* XString_toGbk(const XString* str)
{
    if (!str||XString_isEmpty_base(str)) return NULL;

    // 检查缓存是否存在
    if (str->m_cache && str->m_cache[XStringCache_Gbk].m_data) {
        return str->m_cache[XStringCache_Gbk].m_data;
    }

    // 计算GBK所需缓冲区大小（包含终止符）
    int64_t gbk_len = XChar_toGbkStream(XString_cdata(str), XString_length_base(str), NULL, 0);
    if (gbk_len <= 0) return NULL;

    // 分配缓冲区（+1用于终止符）
    size_t gbk_max_len = (size_t)gbk_len + 1;
    char* gbk_buf = (char*)XContainer_malloc((const XContainer*)str, gbk_max_len);
    if (!gbk_buf) return NULL;

    // 执行转换
    int64_t result = XChar_toGbkStream(XString_cdata(str), XString_length_base(str), gbk_buf, gbk_max_len);
    if (result <= 0) {
        XContainer_free((const XContainer*)str, gbk_buf);
        return NULL;
    }

    // 初始化缓存并存储结果
    XString_initCache((XString*)str);
    str->m_cache[XStringCache_Gbk].m_data = gbk_buf;
    str->m_cache[XStringCache_Gbk].m_length = gbk_max_len;
    return gbk_buf;
}

size_t XString_toGbk_length(const XString* str)
{
    if (!str || XString_isEmpty_base(str))
        return 0;
    if (XString_toGbk(str)) 
        return str->m_cache[XStringCache_Gbk].m_length;
    return 0;
}

const char* XString_toLocal(const XString* str)
{
    if (!str || XString_isEmpty_base(str)) return NULL;

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

size_t XString_toLocal_length(const XString* str)
{
    if (!str || XString_isEmpty_base(str))
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
    if (!str || !XContainerDataPtr(str)) return NULL;
    return XString_cdata(str);
}

const uint16_t* XString_utf16(const XString* str)
{
    if (!str || !XContainerDataPtr(str)) return NULL;
    return (const uint16_t*)XString_cdata(str);
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
    XString_data(str)[new_size] = 0;

    // 清除缓存（内容已修改）
    XString_deinitCache(str);
    return true;
}

// 内部公共函数：追加UTF-8字符串（len=0表示使用strlen自动计算长度）
static bool XString_append_utf8_internal(XString* str, const char* utf8_str, size_t len)
{
    if (!str || !utf8_str) return false;

    // 先计算需要转换的XChar数量（不含终止符）
    int64_t xchar_count = XChar_fromUtf8Stream((const uint8_t*)utf8_str, len, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    size_t current_size = XString_length_base(str);
    size_t new_size = current_size + (size_t)xchar_count;

    // 预留足够空间（包含终止符）
    XString_reserve(str, new_size);

    // 直接转换到目标缓冲区
    XChar* data = XString_data(str);
    int64_t result = XChar_fromUtf8Stream(
        (const uint8_t*)utf8_str,
        len,
        data + current_size,
        (size_t)xchar_count + 1
    );

    if (result <= 0) {
        XContainerSize(str) = current_size;
        data[current_size] = 0;
        return false;
    }

    XContainerSize(str) = new_size;
    data[new_size] = 0;

    XString_deinitCache(str);
    return true;
}

bool XString_append_utf8(XString* str, const char* utf8_str)
{
    return XString_append_utf8_internal(str, utf8_str, 0);
}

bool XString_append_with_length_utf8(XString* str, const char* utf8_str, size_t len)
{
    return XString_append_utf8_internal(str, utf8_str, len);
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
    int64_t xchar_count = XChar_fromUtf8Stream((const uint8_t*)utf8_str, len, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    // 预留足够空间（包含终止符）
    XString_reserve(str, (size_t)xchar_count);

    // 直接转换到目标缓冲区
    XChar* data = XString_data(str);
    int64_t result = XChar_fromUtf8Stream(
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
    char* utf8_buf = (char*)XContainer_malloc(str, fmt_len + 1);
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
    XContainer_free(str, utf8_buf);

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

bool XString_replace(XString* str, const XString* before, const XString* after, XChar_CaseSensitivity cs)
{
    if (!str || !before || !after) return false;

    size_t before_len = XString_length_base(before);
    if (before_len == 0) return false; // 不处理空替换串

    size_t after_len = XString_length_base(after);
    const XChar* before_data = XString_cdata(before);
    const XChar* text = XString_cdata(str);
    size_t text_len = XString_length_base(str);

    // 预计算前缀表（KMP算法）
    int* prefix = (int*)XContainer_malloc(str, before_len * sizeof(int));
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
            XContainer_free(str, prefix);
            return false;
        }

        // 2. 插入新子串
        if (!XString_insert(str, (size_t)pos, after)) 
        {
            XContainer_free(str, prefix);
            return false;
        }

        replaced = true;
        // 更新文本数据和长度（替换后字符串已变化）
        text = XString_cdata(str);
        text_len = XString_length_base(str);
        // 计算下一次查找起始位置
        pos += after_len;
    }

    XContainer_free(str, prefix);
    return replaced; // 即使没有替换也返回true（操作成功但无匹配）
}

bool XString_replace_utf8(XString* str, const char* before, const char* after, XChar_CaseSensitivity cs)
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

    int64_t pos = XString_indexOf_utf8(str, before, 0, cs);
    while (pos != -1) {
        if (!XString_remove_base(str, (size_t)pos, before_len)) break;
        if (!XString_insert_utf8(str, (size_t)pos, after)) break;
        pos = XString_indexOf_utf8(str, before, (size_t)pos + after_len, cs);
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
void compute_prefix(const XChar* pattern, size_t m, int* prefix, XChar_CaseSensitivity cs) 
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
    const int* prefix, XChar_CaseSensitivity cs,
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


int64_t XString_indexOf_utf8(const XString* str, const char* substr, size_t from, XChar_CaseSensitivity cs)
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
    int* prefix = (int*)XContainer_malloc(str, substr_len * sizeof(int));
    if (!prefix) {
        XString_delete_base(substr_str);
        return -1;
    }
    compute_prefix(pattern, substr_len, prefix, cs);

    // 执行KMP搜索
    int64_t result = kmp_search(text, str_len, pattern, substr_len, prefix, cs, from);

    // 清理资源
    XContainer_free(str, prefix);
    XString_delete_base(substr_str);
    return result;
}
// 辅助函数：KMP反向搜索（用于last_index_of）
int64_t kmp_reverse_search(const XChar* text, size_t n, const XChar* pattern, size_t m,
    XChar_CaseSensitivity cs, size_t start_idx)
{
    if (!text || !pattern || m == 0 || n < m) return -1;
    size_t max_start = n - m;
    if (start_idx > max_start) start_idx = max_start;

    /* 反向扫描候选起点，避免失配回退时停留在同一位置。 */
    for (size_t i = start_idx + 1; i-- > 0;) {
        bool match = true;
        for (size_t j = 0; j < m; ++j) {
            if (!XChar_equals(text[i + j], pattern[j], cs)) {
                match = false;
                break;
            }
        }
        if (match) return (int64_t)i;
    }
    return -1;
}

int64_t XString_indexOf(const XString* str, const XString* substr, size_t from, XChar_CaseSensitivity cs)
{
    /* 委托给 XStringView_indexOf */
    XStringView sv = XStringView_create_string(str);
    XStringView subv = XStringView_create_string(substr);
    return XStringView_indexOf(&sv, &subv, (int64_t)from, (int)cs);
}

int64_t XString_lastIndexOf(const XString* str, const XString* substr, size_t from, XChar_CaseSensitivity cs)
{
    /* 委托给 XStringView_lastIndexOf */
    XStringView sv = XStringView_create_string(str);
    XStringView subv = XStringView_create_string(substr);
    return XStringView_lastIndexOf(&sv, &subv, (int64_t)from, (int)cs);
}

int64_t XString_lastIndexOf_utf8(const XString* str, const char* substr, size_t from, XChar_CaseSensitivity cs)
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

bool XString_contains(const XString* str, const XString* substr, XChar_CaseSensitivity cs)
{
    /* 委托给 XStringView_contains */
    XStringView sv = XStringView_create_string(str);
    XStringView subv = XStringView_create_string(substr);
    return XStringView_contains(&sv, &subv, (int)cs);
}

bool XString_contains_utf8(const XString* str, const char* utf8_substr, XChar_CaseSensitivity cs)
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
    /* 委托给 XStringView_compare（大小写敏感） */
    XStringView v1 = XStringView_create_string(str1);
    XStringView v2 = XStringView_create_string(str2);
    return (int32_t)XStringView_compare(&v1, &v2, 1);
}

uint64_t XString_hash(const void* key, size_t keyTypeSize)
{
    const XString* str;
    const XChar* data;
    size_t len;
    (void)keyTypeSize;  /* 保持 XHashFunc 回调签名一致，哈希不依赖类型大小 */

    if (!key)
        return 0;
    str = (const XString*)key;
    len = XString_length_base(str);
    /* 空串内容唯一确定，直接返回固定哈希；data 为 NULL 也安全。 */
    if (len == 0)
        return XCryptographicHash_value(NULL, 0, XCryptographicHash_XxHash64);
    data = XString_unicode(str);
    /* 以 UTF-16 字符数组字节为输入，与 XString_equals 的逐字符比较语义一致：
     * 内容相等的字符串必然得到相同哈希值（与对象内部缓冲区地址无关）。 */
    return XCryptographicHash_value(data, len * sizeof(XChar),
                                    XCryptographicHash_XxHash64);
}

bool XString_equals(const XString* str1, const XString* str2, XChar_CaseSensitivity cs)
{
    // 空指针处理：两者都为空则相等，仅有一个为空则不等
    if (!str1 && !str2) return true;
    if (!str1 || !str2) return false;

    // 长度不同直接不等（快速失败）
    size_t len1 = XString_length_base(str1);
    size_t len2 = XString_length_base(str2);
    if (len1 != len2) return false;

    /* 空字符串可以不持有数据缓冲区；长度相同即表示二者相等。 */
    if (len1 == 0) return true;

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

bool XString_startsWith(const XString* str, const XString* prefix, XChar_CaseSensitivity cs) {
    /* 委托给 XStringView_startsWith */
    XStringView sv = XStringView_create_string(str);
    XStringView pv = XStringView_create_string(prefix);
    return XStringView_startsWith(&sv, &pv, (int)cs);
}
const bool XLess_XString(const XString* str1, const XString* str2)
{
    return XString_compare(str1, str2) <0;
}

bool XString_startsWith_utf8(const XString* str, const char* prefix, XChar_CaseSensitivity cs) 
{
    if (!str || !prefix) return false;

    XString* prefix_str = XString_create_utf8(prefix);
    if (!prefix_str) return false;

    bool result = (XString_length_base(prefix_str) <= XString_length_base(str)) &&
        (XString_indexOf_utf8(str, prefix, 0,cs) == 0);
    XString_delete_base(prefix_str);
    return result;
}

bool XString_endsWith(const XString* str, const XString* suffix, XChar_CaseSensitivity cs) {
    /* 委托给 XStringView_endsWith */
    XStringView sv = XStringView_create_string(str);
    XStringView suv = XStringView_create_string(suffix);
    return XStringView_endsWith(&sv, &suv, (int)cs);
}

bool XString_endsWith_utf8(const XString* str, const char* suffix, XChar_CaseSensitivity cs)
{
    if (!str || !suffix) return false;

    XString* suffix_str = XString_create_utf8(suffix);
    if (!suffix_str) return false;

    size_t str_len = XString_length_base(str);
    size_t suffix_len = XString_length_base(suffix_str);
    bool result = (suffix_len <= str_len) &&
        (XString_lastIndexOf_utf8(str, suffix, 0, cs) == (int64_t)(str_len - suffix_len));

    XString_delete_base(suffix_str);
    return result;
}

bool XString_equals_utf8(const XString* str, const char* utf8_str, XChar_CaseSensitivity cs)
{
    if (!str || !utf8_str) return false;

    XString* tmp = XString_create_utf8(utf8_str);
    if (!tmp) return false;

    bool result = XString_equals(str, tmp, cs);
    XString_delete_base(tmp);
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
        if (XChar_isLetter(ch)) { // 仅检查字母字符
            has_letter = true;
            if (!XChar_isLower(ch)) { // 发现大写字母则直接返回false
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
        if (XChar_isSurrogate(ch)) {
            continue;
        }
        if (XChar_isLetter(ch)) { // 仅检查字母字符
            has_letter = true;
            if (!XChar_isUpper(ch)) { // 发现小写字母则直接返回false
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

     // 检查共享数据是否异常（未初始化的对象可能无共享数据）
    if ((XSharedData*)XContainerDataPtr(str) == NULL) {
        return true;
    }
    // 检查内部数据指针是否未初始化（根据XContainer结构特性）
    // 结合XString_init逻辑，未初始化的对象其数据指针可能为NULL
    if (XString_cdata(str) == NULL) {
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
        if (XChar_isHighSurrogate(ch)) {
            // 高代理后面必须跟一个低代理，且不能是最后一个字符
            if (i + 1 >= len || !XChar_isLowSurrogate(chars[i + 1])) {
                return false;
            }
            i++; // 跳过下一个低代理，避免重复检查
        }
        // 检查孤立的低代理字符（前面没有高代理）
        else if (XChar_isLowSurrogate(ch)) {
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
        data[i] = XChar_toLower(data[i]);
    }
    data[XString_length_base(result)] = 0;  // 更新结束符

    XString_deinitCache(result);
    return result;
}

XString* XString_toUpper(const XString* str) {
    if (!str) return NULL;

    XString* result = XString_create_copy(str);
    XString_detach(result);  // 确保可修改

    XChar* data = XString_data(result);
    for (size_t i = 0; i < XString_length_base(result); i++) {
        data[i] = XChar_toUpper(data[i]);
    }
    data[XString_length_base(result)] = 0;  // 更新结束符

    XString_deinitCache(result);
    return result;
}

XString* XString_trimmed(const XString* str) {
    /* 委托给 XStringView_trimmed + XStringView_toString */
    XStringView view = XStringView_create_string(str);
    XStringView trimmed = XStringView_trimmed(&view);
    return XStringView_toString(&trimmed);
}

unsigned short XString_toUShort(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toUShort */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toUShort(&sv, ok, base);
}

unsigned int XString_toUInt(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toUInt */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toUInt(&sv, ok, base);
}

short XString_toShort(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toShort */
    XStringView sv = XStringView_create_string(str);
    return (short)XStringView_toShort(&sv, ok, base);
}
int XString_toInt(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toInt */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toInt(&sv, ok, base);
}
long XString_toLong(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toLong */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toLong(&sv, ok, base);
}
long long XString_toLongLong(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toLongLong */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toLongLong(&sv, ok, base);
}
unsigned long XString_toULong(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toULong */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toULong(&sv, ok, base);
}
unsigned long long XString_toULongLong(const XString* str, bool* ok, int base)
{
    /* 委托给 XStringView_toULongLong */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toULongLong(&sv, ok, base);
}

float XString_toFloat(const XString* str, bool* ok)
{
    /* 委托给 XStringView_toFloat */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toFloat(&sv, ok);
}

double XString_toDouble(const XString* str, bool* ok) {
    /* 委托给 XStringView_toDouble */
    XStringView sv = XStringView_create_string(str);
    return XStringView_toDouble(&sv, ok);
}

DEFINE_SETNUM_INT(XString_setNum_int, XChar_fromInt, int)
DEFINE_SETNUM_INT(XString_setNum_uInt, XChar_fromUInt, unsigned int)
DEFINE_SETNUM_INT(XString_setNum_long, XChar_fromLong, long)
DEFINE_SETNUM_INT(XString_setNum_uLong, XChar_fromULong, unsigned long)
DEFINE_SETNUM_INT(XString_setNum_llong, XChar_fromLongLong, long long)
DEFINE_SETNUM_INT(XString_setNum_uLLong, XChar_fromULongLong, unsigned long long)
DEFINE_SETNUM_FLOAT(XString_setNum_double, XChar_fromDouble, double)
DEFINE_SETNUM_FLOAT(XString_setNum_float, XChar_fromFloat, float)

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
    /* 委托给 XStringView_sliced_2 + XStringView_toString */
    XStringView view = XStringView_create_string(str);
    XStringView sub = XStringView_sliced_2(&view, (int64_t)pos, (int64_t)n);
    return XStringView_toString(&sub);
}

// 预分配空间（额外+1存储结束符）
bool XString_reserve(XString* str, size_t capacity)
{
    if (!str)
        return false;

    // 容量足够，无需操作，返回 true
    if (capacity <= XContainerCapacity(str))
        return true;

    // 新容量不小于最小容量
    size_t new_capacity = (capacity < XSTRING_MIN_CAPACITY) ? XSTRING_MIN_CAPACITY : capacity;

    // 如果还没有共享数据块，创建新的
    if (!(XSharedData*)XContainerDataPtr(str))
    {
        size_t bytes = sizeof(XChar) * (new_capacity + 1);
        XSharedData* sd = XSharedData_create_ex(NULL, bytes, XContainer_memory(str));
        if (sd)
        {
            memset(sd->data, 0, bytes);
            XContainerSetDataPtr(str, sd);
        }
        else
        {
            return false;
        }
    }
    // 已有共享块，且容量不足
    else
    {
        size_t bytes = (new_capacity + 1) * sizeof(XChar);
        XSharedData* newShared = XSharedData_create_ex(NULL, bytes, XContainer_memory(str));
        if (!newShared)
            return false;

        // 拷贝现有数据（包含结束符）
        if (XString_cdata(str))
        {
            memcpy(newShared->data, XString_cdata(str), (XString_length_base(str) + 1) * sizeof(XChar));
        }
        else
        {
            memset(newShared->data, 0, bytes);
        }

        XSharedData_release((XSharedData*)XContainerDataPtr(str), XContainer_memory(str));
        XContainerSetDataPtr(str, newShared);
    }

    XContainerCapacity(str) = new_capacity;
    // 确保终止符存在
    XString_data(str)[XString_length_base(str)] = 0;
    return true;
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
    XString_data(str)[position] = 0;

    // 清除缓存（截断后缓存内容失效）
    XString_deinitCache(str);
}

// 辅助函数：查找下一个分隔符位置
static int64_t find_next_delimiter(const XString* str, const char* delimiter, size_t current_pos, XChar_CaseSensitivity cs)
{
    if (!str || !delimiter || current_pos > XString_length_base(str)) {
        return -1;
    }
    return XString_indexOf_utf8(str, delimiter, current_pos,cs);
}

// 按分隔符拆分字符串
XStringList* XString_split_utf8(const XString* str, const char* delimiter, XChar_CaseSensitivity cs) 
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
XStringList* XString_split_limit_utf8(const XString* str, const char* delimiter, size_t limit, XChar_CaseSensitivity cs)
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
            XString_delete_base(substr);
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
            XString_delete_base(substr);
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
        XString_delete_base(substr);
    }

    return result;
}

// 获取可修改的内部XChar数组
XChar* XString_data(XString* str)
{
    if (!str) return NULL;
    XString_detach(str);  // 修改前确保分离
    return (XChar*)XContainerDataAddr(str);
}

// 分离共享数据（Copy-On-Write机制）
void XString_detach(XString* str)
{
    if (!str) return;

    if (!XContainerIsCow(str))
        return;

    // 不共享，无需分离
    if (!(XSharedData*)XContainerDataPtr(str) || !XSharedData_isShared((XSharedData*)XContainerDataPtr(str)))
        return;

    // 存在共享引用，必须拷贝数据（真正的写时复制点）
    size_t curr_size = XContainerSize(str);
    size_t curr_capacity = XContainerCapacity(str);

    // 分配新的缓冲区（保留原有容量，避免后续频繁分配）
        size_t bytes = (curr_capacity + 1) * sizeof(XChar);
    // 创建新的 XSharedData（一次分配）
    XSharedData* newShared = XSharedData_create_ex(NULL, bytes, XContainer_memory(str));
    if (!newShared) return;

    // 复制现有数据（包含终止符）
    if (curr_size > 0 && XString_cdata(str))
        memcpy(newShared->data, XString_cdata(str), (curr_size + 1) * sizeof(XChar));
    else
        memset(newShared->data, 0, bytes);

    // 减少旧引用，设置新引用
    XSharedData_release((XSharedData*)XContainerDataPtr(str), XContainer_memory(str));
    XContainerSetDataPtr(str, newShared);

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
        XContainer_free(str, str->m_cache[i].m_data);
        if (str->m_cache[i].m_data == local)
            local = NULL;
        str->m_cache[i].m_data = NULL;
        str->m_cache[i].m_length = 0;
    }
    if (local != NULL)
    {
        XContainer_free(str, local);
    }
    str->m_cache[0].m_data = NULL;
    str->m_cache[0].m_length = 0;
    /* 释放缓存数组本体：此前只释放了各槽位数据，漏掉由
       XString_initCache 分配的缓存容器，会导致一切用过
       toUtf8/toUtf16/toUtf32/toGbk/toLocal8Bit 的 XString
       在销毁时泄漏 80 字节。 */
    XContainer_free(str, str->m_cache);
    str->m_cache = NULL;
}

static void XString_initCache(XString* str)
{
    if (str == NULL || str->m_cache != NULL)
        return;
    str->m_cache = XContainer_calloc(str, XStringCache_Size, sizeof(XStringCache));
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：子串操作函数                                    */
/* ========================================================================== */

XString* XString_sliced(const XString* str, size_t pos)
{
    if (!str) return NULL;
    size_t len = XString_length_base(str);
    if (pos > len) return NULL;
    return XString_mid(str, pos, len - pos);
}

XString* XString_sliced_2(const XString* str, size_t pos, size_t n)
{
    if (!str) return NULL;
    return XString_mid(str, pos, n);
}

XString* XString_first(const XString* str, size_t n)
{
    if (!str) return NULL;
    return XString_left(str, n);
}

XString* XString_last(const XString* str, size_t n)
{
    if (!str) return NULL;
    return XString_right(str, n);
}

XString* XString_chopped(const XString* str, size_t len)
{
    if (!str) return NULL;
    size_t str_len = XString_length_base(str);
    if (len >= str_len) return XString_create_utf8("");
    return XString_mid(str, 0, str_len - len);
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：原地修改函数                                    */
/* ========================================================================== */

void XString_chop(XString* str, size_t n)
{
    if (!str) return;
    size_t len = XString_length_base(str);
    if (n >= len) {
        XString_clear_base(str);
        return;
    }
    XString_truncate(str, len - n);
}

void XString_resize_fill(XString* str, size_t size, XChar fillChar)
{
    if (!str) return;
    size_t current_size = XString_length_base(str);
    if (size == current_size) return;

    XString_detach(str);
    XString_reserve(str, size);

    XChar* data = XString_data(str);
    if (size > current_size) {
        for (size_t i = current_size; i < size; ++i) {
            data[i] = fillChar;
        }
    }
    XContainerSize(str) = size;
    data[size] = 0;
    XString_deinitCache(str);
}

bool XString_fill(XString* str, XChar ch, int64_t size)
{
    if (!str) return false;
    XString_detach(str);

    size_t new_size = (size < 0) ? XString_length_base(str) : (size_t)size;
    XString_reserve(str, new_size);

    XChar* data = XString_data(str);
    for (size_t i = 0; i < new_size; ++i) {
        data[i] = ch;
    }
    XContainerSize(str) = new_size;
    data[new_size] = 0;
    XString_deinitCache(str);
    return true;
}

void XString_swap(XString* str, XString* other)
{
    if (!str || !other) return;
    // 交换所有字段
    XContainer temp = str->parent;
    str->parent = other->parent;
    other->parent = temp;

    XStringCache* cache = str->m_cache;
    str->m_cache = other->m_cache;
    other->m_cache = cache;
}

void XString_squeeze(XString* str)
{
    if (!str) return;
    size_t len = XString_length_base(str);
    size_t cap = XContainerCapacity(str);
    if (len == cap) return;

    XString_detach(str);

    size_t bytes = (len + 1) * sizeof(XChar);
    XSharedData* newShared = XSharedData_create_ex(NULL, bytes, XContainer_memory(str));
    if (!newShared) return;

    if (XString_cdata(str) && len > 0) {
        memcpy(newShared->data, XString_cdata(str), bytes);
    } else {
        memset(newShared->data, 0, bytes);
    }

    XSharedData_release((XSharedData*)XContainerDataPtr(str), XContainer_memory(str));
    XContainerSetDataPtr(str, newShared);
    XContainerCapacity(str) = len;
}

bool XString_slice(XString* str, size_t pos)
{
    if (!str) return false;
    size_t len = XString_length_base(str);
    if (pos > len) return false;
    if (pos == 0) return true;

    XString_detach(str);
    size_t new_len = len - pos;
    XChar* data = XString_data(str);
    memmove(data, data + pos, new_len * sizeof(XChar));
    XContainerSize(str) = new_len;
    data[new_len] = 0;
    XString_deinitCache(str);
    return true;
}

bool XString_slice_2(XString* str, size_t pos, size_t n)
{
    if (!str) return false;
    if (pos + n > XString_length_base(str)) return false;

    XString_detach(str);
    XChar* data = XString_data(str);
    memmove(data, data + pos, n * sizeof(XChar));
    XContainerSize(str) = n;
    data[n] = 0;
    XString_deinitCache(str);
    return true;
}

void XString_resizeForOverwrite(XString* str, size_t size)
{
    if (!str) return;
    XString_resize(str, size);
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：转换函数                                        */
/* ========================================================================== */

XString* XString_toCaseFolded(const XString* str)
{
    if (!str) return NULL;
    XString* result = XString_create_copy(str);
    if (!result) return NULL;
    XString_detach(result);

    XChar* data = XString_data(result);
    for (size_t i = 0; i < XString_length_base(result); i++) {
        data[i] = XChar_toCaseFolded(data[i]);
    }
    data[XString_length_base(result)] = 0;
    XString_deinitCache(result);
    return result;
}

XString* XString_toHtmlEscaped(const XString* str)
{
    if (!str) return NULL;
    XString* result = XString_create_utf8("");
    if (!result) return NULL;

    size_t len = XString_length_base(str);
    const XChar* data = XString_cdata(str);
    XString_reserve(result, len * 6); // 最坏情况每个字符变成6个

    for (size_t i = 0; i < len; i++) {
        // 使用XChar的unicode值判断，避免HTML实体在源码中被误解析
        XChar ch = data[i];
        if (XChar_equals(ch, XChar_from('&'), XChar_CaseSensitive)) {
            XString_append_utf8(result, "&" "a" "m" "p" ";");
        } else if (XChar_equals(ch, XChar_from('<'), XChar_CaseSensitive)) {
            XString_append_utf8(result, "&" "l" "t" ";");
        } else if (XChar_equals(ch, XChar_from('>'), XChar_CaseSensitive)) {
            XString_append_utf8(result, "&" "g" "t" ";");
        } else if (XChar_equals(ch, XChar_from('"'), XChar_CaseSensitive)) {
            XString_append_utf8(result, "&" "q" "u" "o" "t" ";");
        } else if (XChar_equals(ch, XChar_from('\''), XChar_CaseSensitive)) {
            XString_append_utf8(result, "&" "#" "3" "9" ";");
        } else {
            XString_append_char(result, data[i]);
        }
    }
    return result;
}

XString* XString_leftJustified(const XString* str, size_t width, XChar fill, bool truncate)
{
    if (!str) return NULL;
    size_t len = XString_length_base(str);

    if (len >= width) {
        if (truncate) return XString_mid(str, 0, width);
        return XString_create_copy(str);
    }

    XString* result = XString_create_copy(str);
    if (!result) return NULL;
    XString_detach(result);
    XString_reserve(result, width);

    XChar* data = XString_data(result);
    for (size_t i = len; i < width; i++) {
        data[i] = fill;
    }
    XContainerSize(result) = width;
    data[width] = 0;
    XString_deinitCache(result);
    return result;
}

XString* XString_rightJustified(const XString* str, size_t width, XChar fill, bool truncate)
{
    if (!str) return NULL;
    size_t len = XString_length_base(str);

    if (len >= width) {
        if (truncate) return XString_mid(str, 0, width);
        return XString_create_copy(str);
    }

    XString* result = XString_create_utf8("");
    if (!result) return NULL;
    XString_detach(result);
    XString_reserve(result, width);

    XChar* data = XString_data(result);
    size_t prefix = width - len;
    for (size_t i = 0; i < prefix; i++) {
        data[i] = fill;
    }
    memcpy(data + prefix, XString_cdata(str), len * sizeof(XChar));
    XContainerSize(result) = width;
    data[width] = 0;
    XString_deinitCache(result);
    return result;
}

XString* XString_simplified(const XString* str)
{
    if (!str || XString_isEmpty_base(str)) return XString_create_utf8("");

    XString* result = XString_create_utf8("");
    if (!result) return NULL;

    size_t len = XString_length_base(str);
    const XChar* data = XString_cdata(str);
    bool in_whitespace = true; // 开始时视为空白状态（跳过前导空白）
    XChar space = XChar_from(' ');

    for (size_t i = 0; i < len; i++) {
        if (XChar_isSpace(data[i])) {
            in_whitespace = true;
        } else {
            if (in_whitespace && XString_length_base(result) > 0) {
                XString_append_char(result, space); // 内部连续空白替换为单个空格
            }
            XString_append_char(result, data[i]);
            in_whitespace = false;
        }
    }
    return result;
}

XString* XString_repeated(const XString* str, size_t times)
{
    if (!str || times == 0) return XString_create_utf8("");
    if (times == 1) return XString_create_copy(str);

    size_t len = XString_length_base(str);
    XString* result = XString_create_utf8("");
    if (!result) return NULL;
    XString_detach(result);
    XString_reserve(result, len * times);

    for (size_t i = 0; i < times; i++) {
        XString_append(result, str);
    }
    return result;
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：查询函数                                        */
/* ========================================================================== */

size_t XString_count(const XString* str, const XString* sub, XChar_CaseSensitivity cs)
{
    /* 委托给 XStringView_count */
    XStringView sv = XStringView_create_string(str);
    XStringView subv = XStringView_create_string(sub);
    return (size_t)XStringView_count(&sv, &subv, (int)cs);
}

size_t XString_count_utf8(const XString* str, const char* sub, XChar_CaseSensitivity cs)
{
    if (!str || !sub) return 0;
    XString* sub_str = XString_create_utf8(sub);
    if (!sub_str) return 0;
    size_t result = XString_count(str, sub_str, cs);
    XString_delete_base(sub_str);
    return result;
}

size_t XString_count_char(const XString* str, XChar ch, XChar_CaseSensitivity cs)
{
    /* 委托给 XStringView_count_char */
    XStringView sv = XStringView_create_string(str);
    return (size_t)XStringView_count_char(&sv, ch, (int)cs);
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：静态/最大值函数                                  */
/* ========================================================================== */

size_t XString_maxSize(void)
{
    // 与Qt一致，返回理论最大值（受平台和内存限制）
    return (size_t)-1 / sizeof(XChar) - 1;
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：数值转字符串静态函数                              */
/* ========================================================================== */

DEFINE_NUMBER(XString_number_llong, XString_setNum_llong, long long)
DEFINE_NUMBER(XString_number_ullong, XString_setNum_uLLong, unsigned long long)
DEFINE_NUMBER_FLOAT(XString_number_double, XString_setNum_double, double)

/* ========================================================================== */
/*                 Qt 6.8 对齐：数据访问函数                                    */
/* ========================================================================== */

XChar* XString_data_ptr(XString* str)
{
    return XString_data(str);
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：setNum扩展                                      */
/* ========================================================================== */

bool XString_setNum_short(XString* str, short n, int base)
{
    return XString_setNum_int(str, (int)n, base);
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：setUnicode/setUtf16                             */
/* ========================================================================== */

bool XString_setUnicode(XString* str, const XChar* unicode, size_t size)
{
    if (!str) return false;
    XString_clear_base(str);
    if (!unicode || size == 0) return true;

    XString_detach(str);
    XString_reserve(str, size);

    XChar* data = XString_data(str);
    memcpy(data, unicode, size * sizeof(XChar));
    XContainerSize(str) = size;
    data[size] = 0;
    XString_deinitCache(str);
    return true;
}

bool XString_setUtf16(XString* str, const uint16_t* unicode, size_t size)
{
    if (!str) return false;
    XString_clear_base(str);
    if (!unicode || size == 0) return true;

    int64_t xchar_count = XChar_fromUtf16Stream(unicode, size, NULL, 0);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    XString_reserve(str, (size_t)xchar_count);

    XChar* data = XString_data(str);
    xchar_count = XChar_fromUtf16Stream(unicode, size, data, (size_t)xchar_count + 1);
    if (xchar_count <= 0) return false;

    XContainerSize(str) = (size_t)xchar_count;
    data[xchar_count] = 0;
    XString_deinitCache(str);
    return true;
}

/* ========================================================================== */
/*                 Qt 6.8 对齐：XChar重载操作函数                                */
/* ========================================================================== */

int64_t XString_indexOf_char(const XString* str, XChar ch, size_t from, XChar_CaseSensitivity cs)
{
    /* 委托给 XStringView_indexOf_char */
    XStringView sv = XStringView_create_string(str);
    return XStringView_indexOf_char(&sv, ch, (int64_t)from, (int)cs);
}

int64_t XString_lastIndexOf_char(const XString* str, XChar ch, XChar_CaseSensitivity cs)
{
    /* 委托给 XStringView_lastIndexOf_char */
    XStringView sv = XStringView_create_string(str);
    return XStringView_lastIndexOf_char(&sv, ch, -1, (int)cs);
}

bool XString_contains_char(const XString* str, XChar ch, XChar_CaseSensitivity cs)
{
    return XString_indexOf_char(str, ch, 0, cs) != -1;
}

bool XString_remove_char(XString* str, XChar ch, XChar_CaseSensitivity cs)
{
    if (!str) return false;
    XString_detach(str);

    size_t len = XString_length_base(str);
    XChar* data = XString_data(str);
    size_t write_idx = 0;

    for (size_t i = 0; i < len; i++) {
        if (!XChar_equals(data[i], ch, cs)) {
            if (write_idx != i) data[write_idx] = data[i];
            write_idx++;
        }
    }

    if (write_idx == len) return false; // 没有移除任何字符
    XContainerSize(str) = write_idx;
    data[write_idx] = 0;
    XString_deinitCache(str);
    return true;
}

bool XString_replace_char(XString* str, XChar before, XChar after, XChar_CaseSensitivity cs)
{
    if (!str) return false;
    XString_detach(str);

    size_t len = XString_length_base(str);
    XChar* data = XString_data(str);
    bool replaced = false;

    for (size_t i = 0; i < len; i++) {
        if (XChar_equals(data[i], before, cs)) {
            data[i] = after;
            replaced = true;
        }
    }

    if (replaced) XString_deinitCache(str);
    return replaced;
}

// ==========================================================================
//          Qt 6.8 对齐：section() 分段提取
// ==========================================================================

XString* XString_section(const XString* str, const XString* sep, int64_t start, int64_t end, int flags)
{
    if (!str || !sep) return XString_create_utf8("");
    size_t str_len = XString_length_base(str);
    size_t sep_len = XString_length_base(sep);
    XChar_CaseSensitivity cs = (flags & XString_SectionCaseInsensitiveSeps) ? XChar_CaseInsensitive : XChar_CaseSensitive;
    bool skipEmpty = (flags & XString_SectionSkipEmpty) != 0;

    // 空分隔符：整个字符串视作单段
    if (sep_len == 0) {
        int64_t s = start, e = end;
        if (s < 0) s += 1;
        if (e < 0) e += 1;
        if (s >= 1 || e < 0 || s > e) return XString_create_utf8("");
        return XString_create_copy(str);
    }

    // 第一遍：统计段数(KeepEmptyParts语义)与空段数
    size_t sectionsSize = 1;
    size_t emptyCount = 0;
    {
        size_t seg = 0;
        while (true) {
            int64_t p = XString_indexOf(str, sep, seg, cs);
            if (p == -1) break;
            if ((size_t)p == seg) emptyCount++;
            sectionsSize++;
            seg = (size_t)p + sep_len;
        }
        if (str_len == seg) emptyCount++;  // 末段为空
    }

    // 段号负数修正（与Qt一致：从右计数）
    int64_t s = start, e = end;
    if (!skipEmpty) {
        if (s < 0) s += (int64_t)sectionsSize;
        if (e < 0) e += (int64_t)sectionsSize;
    } else {
        int64_t nonEmpty = (int64_t)(sectionsSize - emptyCount);
        if (s < 0) s += nonEmpty;
        if (e < 0) e += nonEmpty;
    }
    if (s >= (int64_t)sectionsSize || e < 0 || s > e) return XString_create_utf8("");

    // 第二遍：构造结果（对齐Qt算法，x为计数码段号，i为原始段号）
    XString* ret = XString_create_utf8("");
    int64_t first_i = s, last_i = e;
    int64_t x = 0, i = 0;
    size_t seg = 0;
    while (x <= e && i < (int64_t)sectionsSize) {
        int64_t p = XString_indexOf(str, sep, seg, cs);
        size_t segEnd = (p == -1) ? str_len : (size_t)p;
        bool empty = (segEnd == seg);
        if (x >= s) {
            if (x == s) first_i = i;
            if (x == e) last_i = i;
            if (x > s && i > 0) XString_append(ret, sep);
            if (segEnd > seg) {
                XString* piece = XString_mid(str, seg, segEnd - seg);
                XString_append(ret, piece);
                XString_delete_base(piece);
            }
        }
        if (!empty || !skipEmpty) x++;
        i++;
        if (p == -1) break;
        seg = (size_t)p + sep_len;
    }
    if ((flags & XString_SectionIncludeLeadingSep) && first_i > 0) XString_prepend(ret, sep);
    if ((flags & XString_SectionIncludeTrailingSep) && last_i < (int64_t)sectionsSize - 1) XString_append(ret, sep);
    return ret;
}

XString* XString_section_utf8(const XString* str, const char* sep, int64_t start, int64_t end, int flags)
{
    if (!str || !sep) return XString_create_utf8("");
    XString* sepStr = XString_create_utf8(sep);
    XString* ret = XString_section(str, sepStr, start, end, flags);
    XString_delete_base(sepStr);
    return ret;
}

XString* XString_section_char(const XString* str, XChar sep, int64_t start, int64_t end, int flags)
{
    if (!str) return XString_create_utf8("");
    XString* sepStr = XString_create();
    if (sepStr) XString_push_back_base(sepStr, sep);
    XString* ret = XString_section(str, sepStr, start, end, flags);
    XString_delete_base(sepStr);
    return ret;
}

#if XRegularExpression_ON
static bool XString_section_regularExpression_append(
        XStringList* sections,
        size_t** separatorLengths,
        size_t* sectionCount,
        size_t* sectionCapacity,
        const XString* source,
        size_t position,
        size_t length,
        size_t separatorLength)
{
    if (!sections || !separatorLengths || !sectionCount || !sectionCapacity || !source)
        return false;

    if (*sectionCount == *sectionCapacity) {
        size_t capacity = *sectionCapacity ? *sectionCapacity * 2 : 8;
        size_t* values = (size_t*)XContainer_realloc(source, *separatorLengths,
            capacity * sizeof(size_t));
        if (!values) return false;
        *separatorLengths = values;
        *sectionCapacity = capacity;
    }

    XString* section = XString_regularExpression_substring(source, position, length);
    if (!section) return false;
    if (!XStringList_push_back_base(sections, section)) {
        XString_delete_base(section);
        return false;
    }
    XString_delete_base(section);
    (*separatorLengths)[*sectionCount] = separatorLength;
    ++*sectionCount;
    return true;
}

XString* XString_section_regularExpression(const XString* str,
                                           const XRegularExpression* expression,
                                           int64_t start,
                                           int64_t end,
                                           int flags)
{
    if (!str || !expression) return XString_create();

    XRegularExpression* separator = XRegularExpression_create_copy(expression);
    if (!separator) return XString_create();
    if (flags & XString_SectionCaseInsensitiveSeps) {
        XRegularExpression_setPatternOptions(
                separator,
                XRegularExpression_patternOptions(separator) |
                        XRegularExpression_CaseInsensitiveOption);
    }

    XRegularExpressionMatchIterator* iterator = XRegularExpression_globalMatch(
            separator, str, 0, XRegularExpression_NormalMatch,
            XRegularExpression_NoMatchOption);
    if (!iterator || !XRegularExpressionMatchIterator_isValid(iterator)) {
        if (iterator) XRegularExpressionMatchIterator_delete_base(iterator);
        XRegularExpression_delete_base(separator);
        return XString_create();
    }

    XStringList* sections = XStringList_create();
    size_t* separatorLengths = NULL;
    size_t sectionCount = 0;
    size_t sectionCapacity = 0;
    size_t lastPosition = 0;
    size_t lastSeparatorLength = 0;
    bool ok = sections != NULL;

    while (ok && XRegularExpressionMatchIterator_hasNext(iterator)) {
        XRegularExpressionMatch* match = XRegularExpressionMatchIterator_next(iterator);
        if (!match) {
            ok = false;
            break;
        }
        int64_t startPosition = XRegularExpressionMatch_capturedStart(match, 0);
        int64_t endPosition = XRegularExpressionMatch_capturedEnd(match, 0);
        if (startPosition < 0 || endPosition < startPosition ||
                (size_t)startPosition < lastPosition) {
            XRegularExpressionMatch_delete_base(match);
            ok = false;
            break;
        }
        ok = XString_section_regularExpression_append(
                sections, &separatorLengths, &sectionCount, &sectionCapacity,
                str, lastPosition, (size_t)startPosition - lastPosition,
                lastSeparatorLength);
        lastPosition = (size_t)startPosition;
        lastSeparatorLength = (size_t)endPosition - (size_t)startPosition;
        XRegularExpressionMatch_delete_base(match);
    }

    if (ok) {
        ok = XString_section_regularExpression_append(
                sections, &separatorLengths, &sectionCount, &sectionCapacity,
                str, lastPosition, XString_size_base(str) - lastPosition,
                lastSeparatorLength);
    }

    XRegularExpressionMatchIterator_delete_base(iterator);
    XRegularExpression_delete_base(separator);
    if (!ok || sectionCount == 0) {
        if (separatorLengths) XContainer_free(str, separatorLengths);
        if (sections) XStringList_delete_base(sections);
        return XString_create();
    }

    bool skipEmpty = (flags & XString_SectionSkipEmpty) != 0;
    int64_t sectionsSize = (int64_t)sectionCount;
    if (!skipEmpty) {
        if (start < 0) start += sectionsSize;
        if (end < 0) end += sectionsSize;
    } else {
        int64_t skipped = 0;
        for (size_t i = 0; i < sectionCount; ++i) {
            const XString* section = (const XString*)XStringList_at_base(
                    sections, (int64_t)i);
            if (section && separatorLengths[i] == XString_size_base(section)) ++skipped;
        }
        int64_t nonEmptySize = sectionsSize - skipped;
        if (start < 0) start += nonEmptySize;
        if (end < 0) end += nonEmptySize;
    }

    XString* result = XString_create();
    if (!result || start < 0 || end < 0 || start >= sectionsSize || start > end) {
        if (result) XString_delete_base(result);
        XContainer_free(str, separatorLengths);
        XStringList_delete_base(sections);
        return XString_create();
    }

    int64_t sectionIndex = 0;
    int64_t firstIndex = -1;
    int64_t lastIndex = -1;
    for (size_t i = 0; i < sectionCount && sectionIndex <= end; ++i) {
        const XString* section = (const XString*)XStringList_at_base(
                sections, (int64_t)i);
        if (!section) continue;
        bool empty = separatorLengths[i] == XString_size_base(section);
        if (sectionIndex >= start && sectionIndex <= end) {
            if (sectionIndex == start) firstIndex = (int64_t)i;
            if (sectionIndex == end) lastIndex = (int64_t)i;
            if (sectionIndex == start) {
                XString* piece = XString_regularExpression_substring(
                        section, separatorLengths[i],
                        XString_size_base(section) - separatorLengths[i]);
                if (!piece || !XString_append(result, piece)) {
                    if (piece) XString_delete_base(piece);
                    XString_delete_base(result);
                    XContainer_free(str, separatorLengths);
                    XStringList_delete_base(sections);
                    return XString_create();
                }
                XString_delete_base(piece);
            } else if (!XString_append(result, section)) {
                XString_delete_base(result);
                XContainer_free(str, separatorLengths);
                XStringList_delete_base(sections);
                return XString_create();
            }
        }
        if (!empty || !skipEmpty) ++sectionIndex;
    }

    if ((flags & XString_SectionIncludeLeadingSep) && firstIndex >= 0) {
        const XString* section = (const XString*)XStringList_at_base(
                sections, firstIndex);
        XString* prefix = section ? XString_regularExpression_substring(
                section, 0, separatorLengths[firstIndex]) : NULL;
        if (!prefix || !XString_prepend(result, prefix)) {
            if (prefix) XString_delete_base(prefix);
            XString_delete_base(result);
            XContainer_free(str, separatorLengths);
            XStringList_delete_base(sections);
            return XString_create();
        }
        XString_delete_base(prefix);
    }
    if ((flags & XString_SectionIncludeTrailingSep) &&
            lastIndex >= 0 && lastIndex + 1 < (int64_t)sectionCount) {
        const XString* section = (const XString*)XStringList_at_base(
                sections, lastIndex + 1);
        XString* suffix = section ? XString_regularExpression_substring(
                section, 0, separatorLengths[lastIndex + 1]) : NULL;
        if (!suffix || !XString_append(result, suffix)) {
            if (suffix) XString_delete_base(suffix);
            XString_delete_base(result);
            XContainer_free(str, separatorLengths);
            XStringList_delete_base(sections);
            return XString_create();
        }
        XString_delete_base(suffix);
    }

    XContainer_free(str, separatorLengths);
    XStringList_delete_base(sections);
    return result;
}
#endif

// ==========================================================================
//          Qt 6.8 对齐：arg() 占位符替换
// ==========================================================================

XString* XString_arg(const XString* str, const XString* a, int fieldWidth, XChar fillChar)
{
    if (!str) return NULL;
    size_t len = XString_length_base(str);
    const XChar* data = XString_cdata(str);
    XChar percent = XChar_from('%');
    XChar Lch = XChar_from('L');

    // 第一遍：查找最低编号占位符 %N（1~2位数字，可选%L前缀）
    int min_escape = INT_MAX;
    int64_t occurrences = 0;
    int64_t escape_len = 0;
    {
        size_t i = 0;
        while (i < len) {
            while (i < len && data[i] != percent) i++;
            if (i == len) break;
            size_t esc_start = i;  // '%' 位置
            i++;
            if (i == len) break;
            if (data[i] == Lch) { i++; if (i == len) break; }
            int d1 = XChar_digitValue(data[i]);
            if (d1 == -1) continue;  // 非占位符
            int escape = d1;
            i++;
            if (i < len) {
                int d2 = XChar_digitValue(data[i]);
                if (d2 != -1) { escape = 10 * escape + d2; i++; }
            }
            if (escape > min_escape) continue;
            if (escape < min_escape) { min_escape = escape; occurrences = 0; escape_len = 0; }
            occurrences++;
            escape_len += (int64_t)(i - esc_start);
        }
    }

    // 无占位符：返回源串拷贝（对齐Qt：无占位符返回*this）
    if (occurrences == 0) return XString_create_copy(str);

    // 第二遍：构造结果，替换所有最低编号占位符
    size_t a_len = a ? XString_length_base(a) : 0;
    const XChar* asrc = a ? XString_cdata(a) : NULL;
    int abs_fw = fieldWidth < 0 ? -fieldWidth : fieldWidth;
    size_t use_len = ((size_t)abs_fw > a_len) ? (size_t)abs_fw : a_len;
    size_t result_len = len - (size_t)escape_len + (size_t)occurrences * use_len;

    XString* ret = XString_create_utf8("");
    if (!ret) return NULL;
    XString_reserve(ret, result_len > 0 ? result_len : 1);
    XChar* dst = XString_data(ret);
    size_t di = 0;
    int64_t repl = 0;

    size_t i = 0;
    while (i < len) {
        size_t text_start = i;
        while (i < len && data[i] != percent) i++;
        // 解析 i 处的占位符
        size_t j = i;
        int escape = -1;
        if (j < len) {
            j++;  // 跳过 '%'
            if (j < len && data[j] == Lch) j++;  // 跳过 'L'
            if (j < len) {
                int d1 = XChar_digitValue(data[j]);
                if (d1 != -1) {
                    escape = d1; j++;
                    if (j < len) { int d2 = XChar_digitValue(data[j]); if (d2 != -1) { escape = 10 * escape + d2; j++; } }
                }
            }
        }
        if (escape != min_escape) {
            size_t endpos = (i == len) ? len : j;
            if (endpos > text_start) { memcpy(dst + di, data + text_start, (endpos - text_start) * sizeof(XChar)); di += endpos - text_start; }
            i = endpos;
            if (i == len) break;
        } else {
            // 拷贝占位符之前的文本
            if (i > text_start) { memcpy(dst + di, data + text_start, (i - text_start) * sizeof(XChar)); di += i - text_start; }
            // 左填充（fieldWidth>0 右对齐）
            if (fieldWidth > 0 && (size_t)abs_fw > a_len) {
                size_t pad = (size_t)abs_fw - a_len;
                for (size_t p = 0; p < pad; p++) dst[di++] = fillChar;
            }
            if (a_len) { memcpy(dst + di, asrc, a_len * sizeof(XChar)); di += a_len; }
            // 右填充（fieldWidth<0 左对齐）
            if (fieldWidth < 0 && (size_t)abs_fw > a_len) {
                size_t pad = (size_t)abs_fw - a_len;
                for (size_t p = 0; p < pad; p++) dst[di++] = fillChar;
            }
            i = j;
            repl++;
            if (repl == occurrences) {
                if (len > i) { memcpy(dst + di, data + i, (len - i) * sizeof(XChar)); di += len - i; }
                i = len;
                break;
            }
        }
    }
    ret->parent.m_size = di;
    dst[di] = 0;
    XString_deinitCache(ret);
    return ret;
}

XString* XString_arg_utf8(const XString* str, const char* a, int fieldWidth, XChar fillChar)
{
    if (!str) return NULL;
    XString* as = XString_create_utf8(a);
    XString* ret = XString_arg(str, as, fieldWidth, fillChar);
    XString_delete_base(as);
    return ret;
}

XString* XString_arg_char(const XString* str, XChar a, int fieldWidth, XChar fillChar)
{
    if (!str) return NULL;
    XString* as = XString_create();
    if (as) XString_push_back_base(as, a);
    XString* ret = XString_arg(str, as, fieldWidth, fillChar);
    XString_delete_base(as);
    return ret;
}

/**
 * @brief arg整数系列实现宏：格式化数值后委托 XString_arg 替换最低编号占位符
 * @param FuncName 生成的函数名
 * @param SetNumFunc 数值格式化函数（XString_setNum_*）
 * @param IntType 数值类型
 * @note 内部宏，用于消除 arg_llong/arg_ullong 的重复实现
 */
#define DEFINE_ARG_INT(FuncName, SetNumFunc, IntType) \
XString* FuncName(const XString* str, IntType a, int fieldWidth, int base, XChar fillChar) { \
    XString* num = XString_create(); \
    if (!num) return XString_create_copy(str); \
    if (!SetNumFunc(num, a, base)) { XString_delete_base(num); return XString_create_copy(str); } \
    XString* ret = XString_arg(str, num, fieldWidth, fillChar); \
    XString_delete_base(num); \
    return ret; \
}

DEFINE_ARG_INT(XString_arg_llong, XString_setNum_llong, long long)
DEFINE_ARG_INT(XString_arg_ullong, XString_setNum_uLLong, unsigned long long)

XString* XString_arg_double(const XString* str, double a, int fieldWidth, char format, int precision, XChar fillChar)
{
    if (!str) return NULL;
    XString* num = XString_create();
    if (!num) return XString_create_copy(str);
    if (!XString_setNum_double(num, a, format, precision)) { XString_delete_base(num); return XString_create_copy(str); }
    XString* ret = XString_arg(str, num, fieldWidth, fillChar);
    XString_delete_base(num);
    return ret;
}

// ==========================================================================
//          Qt 6.8 对齐：localeAwareCompare 区域感知比较
// ==========================================================================

int32_t XString_localeAwareCompare(const XString* str1, const XString* str2)
{
    if (!str1 && !str2) return 0;
    if (!str1) return -1;
    if (!str2) return 1;

    const char* l1 = XString_toLocal(str1);
    const char* l2 = XString_toLocal(str2);
    if (!l1) l1 = "";
    if (!l2) l2 = "";
    return (int32_t)strcoll(l1, l2);
}
