#include "XByteArray.h"
#if XByteArray_ON
#include "XString.h"
#include "XByteArrayView.h"
#include <string.h>
uint8_t * XByteArray_data(XByteArray* other);
XByteArray* XByteArray_create_ex(bool useCow)
{
	XByteArray* array = XMalloc_System(sizeof(XByteArray));
	if (array == NULL)
		return NULL;
	XByteArray_init(array, useCow);
	Set_Class_MemoryFree(array, XFree_System);
	return array;
}

XByteArray* XByteArray_create_with_data(const char* data,size_t size)
{
	XByteArray* array=XByteArray_create();
	if (!array)return;
	//if (size != 0)
	//{
	//	XVector_resize_base(array, size);
	//}
	XByteArray_push_back_2(array,data,size);
	return array;
}

XByteArray* XByteArray_create_utf8(const char* utf8)
{
	if (!utf8)return NULL;
	XByteArray* array = XByteArray_create();
	XByteArray_append_utf8(array,utf8);
	return array;
}

XByteArray* XByteArray_create_copy(const XByteArray* other)
{
	if (other == NULL)
		return NULL;
	XByteArray* v = XByteArray_create();
	XByteArray_copy_base(v, other);
	return v;
}

XByteArray* XByteArray_create_move(XByteArray* other)
{
	if (other == NULL)
		return NULL;
	XByteArray* v = XByteArray_create();
	XByteArray_move_base(v, other);
	return v;
}

void XByteArray_init(XByteArray* array, bool useCow)
{
	if (array == NULL)
		return;
	XVector_init((XVector*)array, sizeof(uint8_t), useCow);
	XContainerSetCompare(array, uint8_t_compare);
}

bool XByteArray_push_front_1(XByteArray* array, const uint8_t byte)
{
	return XVector_push_front_1_base(array,&byte);
}

bool XByteArray_push_back_1(XByteArray* array, const uint8_t byte)
{
	return XVector_push_back_1_base(array, &byte);
}

bool XByteArray_insert_2(XByteArray* array, int64_t index, const uint8_t byte)
{
	return XVector_insert_2(array,index,&byte);
}

bool XByteArray_insert_1_base(XByteArray* array, int64_t index, uint8_t byte, size_t n)
{
	return XVector_insert_1_base(array, index, &byte,n);
}

bool XByteArray_append_utf8(XByteArray* array, const char* utf8)
{
	if (array == NULL || utf8 == NULL)
		return false;
	size_t len = strlen(utf8);
	if (len == 0)
		return false;
	return XByteArray_push_back_2(array,utf8,len);
}

bool XByteArray_find_base(const XByteArray* array, const uint8_t findVal, XByteArray_iterator* it)
{
	return XVector_find_base(array,&findVal,it);
}

int32_t XByteArray_compare(const XByteArray* lhs, const XByteArray* rhs)
{
    /* 委托给 XByteArrayView_compare（大小写敏感） */
    XByteArrayView lv = XByteArrayView_create_bytearray(lhs);
    XByteArrayView rv = XByteArrayView_create_bytearray(rhs);
    return XByteArrayView_compare(&lv, &rv, 1);
}

XByteArray* XByteArray_to16HexUtf8(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XByteArray* bytes = XByteArray_create();
	uint8_t temp[6];
	for (size_t i = 0; i < XByteArray_size_base(array); i++)
	{
		sprintf(temp,"%02X ", XByteArray_at_base(array, i));
		XByteArray_push_back_2(bytes,temp,3);
	}
	XByteArray_back_base(bytes) = 0;
	return bytes;
}
XString* XByteArray_to16HexString(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XString* str = XString_create();
	uint8_t temp[6];
	for (size_t i = 0; i < XByteArray_size_base(array); i++)
	{
		sprintf(temp, "%02X ", XByteArray_at_base(array, i));
		XString_append_with_length_utf8(str, temp,3);
	}
	return str;
}

#include"XBase64.h"
XByteArray* XByteArray_toBase64(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XByteArray* base64 = XByteArray_create();
	if (base64 == NULL)
		return NULL;
	XByteArray_resize_base(base64, XBase64_encoded_size(XContainerSize(array)));
	size_t len = XContainerSize(base64);
	if (XBase64_encode(XByteArray_data(array), XContainerSize(array), XByteArray_data(base64), &len) != 0)
	{
		XByteArray_delete_base(base64);
		return NULL;
	}
	XContainerSize(base64) = len;
	return base64;
}
XByteArray* XByteArray_fromBase64(XByteArray* base64)
{
	if (base64 == NULL || XByteArray_isEmpty_base(base64))
		return NULL;
	XByteArray* data = XByteArray_create();
	if (data == NULL)
		return NULL;
	XByteArray_resize_base(data, XBase64_decoded_size(XByteArray_data(base64), XContainerSize(base64)));
	size_t len = XContainerSize(data);
	if (XBase64_decode(XByteArray_data(base64), XContainerSize(base64), XByteArray_data(data), &len) != 0)
	{
		XByteArray_delete_base(data);
		return NULL;
	}
	XContainerSize(data) = len;
	return data;
}

#include"zlib.h"
XByteArray* XByteArray_toCompress(XByteArray* sData)
{
	if (sData == NULL)
		return NULL;

	// 获取输入数据
	static const uint8_t empty_input = 0;
	const uint8_t* input_data = XByteArray_data(sData);
	uLongf input_len = XContainerSize(sData);
	if (!input_data) input_data = &empty_input;

	// 计算压缩缓冲区所需的最大大小
	uLongf output_len = compressBound(input_len);

	// 创建压缩结果缓冲区
	XByteArray* compressed = XByteArray_create();
	if (compressed == NULL)
		return NULL;
	if (!XByteArray_resize_base(compressed, output_len)) {
		XByteArray_delete_base(compressed);
		return NULL;
	}

	uint8_t* output_data = XByteArray_data(compressed);

	// 执行压缩
	int ret = compress(output_data, &output_len, input_data, input_len);
	if (ret != Z_OK)
	{
		XByteArray_delete_base(compressed);
		return NULL;
	}

	// 调整实际大小
	XContainerSize(compressed) = output_len;
	return compressed;
}
XByteArray* XByteArray_toDecompress(XByteArray* sData)
{
	if (sData == NULL || XByteArray_isEmpty_base(sData))
		return NULL;

	// 获取压缩数据
	const uint8_t* input_data = XByteArray_data(sData);
	uLongf input_len = XContainerSize(sData);

	// 初始解压缓冲区大小（设为输入大小的4倍，可根据实际情况调整）
	uLongf output_len = input_len * 4;
	XByteArray* decompressed = XByteArray_create();
	XByteArray_resize_base(decompressed, output_len);
	if (decompressed == NULL)
		return NULL;

	int ret;
	const int max_attempts = 10; // 最大尝试次数，避免无限循环
	int attempts = 0;

	while (attempts < max_attempts)
	{
		uint8_t* output_data = XByteArray_data(decompressed);
		uLongf current_output_len = output_len;

		// 执行解压
		ret = uncompress(output_data, &current_output_len, input_data, input_len);

		if (ret == Z_OK)
		{
			// 解压成功，调整大小并返回
			XContainerSize(decompressed) = current_output_len;
			return decompressed;
		}
		else if (ret == Z_BUF_ERROR)
		{
			// 缓冲区不足，尝试更大的缓冲区
			output_len *= 2;
			if (!XByteArray_resize_base(decompressed, output_len))
			{
				XByteArray_delete_base(decompressed);
				return NULL;
			}
			attempts++;
		}
		else
		{
			// 其他错误（如数据损坏）
			XByteArray_delete_base(decompressed);
			return NULL;
		}
	}

	// 超过最大尝试次数
	XByteArray_delete_base(decompressed);
	return NULL;
}

uint8_t* XByteArray_data(XByteArray* other)
{
	return XContainerDataAddr(other) ;
}



/* ============================== Qt 6.8 命名对齐: fill/truncate/chop/left/right/mid ============================== */

XByteArray* XByteArray_fill(XByteArray* array, uint8_t byte, int64_t size)
{
    if (array == NULL) return NULL;
    if (size >= 0) {
        if (!XByteArray_resize_base(array, (size_t)size))
            return NULL;
    }
    size_t n = XByteArray_size_base(array);
    uint8_t* d = XByteArray_data(array);
    if (d && n > 0) memset(d, byte, n);
    return array;
}

void XByteArray_truncate(XByteArray* array, int64_t pos)
{
    if (array == NULL) return;
    int64_t cur = (int64_t)XByteArray_size_base(array);
    if (pos <= 0) { XByteArray_clear_base(array); return; }
    if (pos >= cur) return;
    // 删除从 pos 起的所有元素
    XVector_remove_base((XVector*)array, pos, cur - pos);
}

void XByteArray_chop(XByteArray* array, int64_t n)
{
    if (array == NULL || n <= 0) return;
    int64_t cur = (int64_t)XByteArray_size_base(array);
    if (n >= cur) { XByteArray_clear_base(array); return; }
    XVector_remove_base((XVector*)array, cur - n, n);
}

XByteArray* XByteArray_left(const XByteArray* array, int64_t n)
{
    if (array == NULL) return NULL;
    /* 委托给 XByteArrayView_first_n 获取子视图，再拷贝到新容器 */
    XByteArrayView view = XByteArrayView_create_bytearray(array);
    XByteArrayView sub = XByteArrayView_first_n(&view, n);
    XByteArray* out = XByteArray_create();
    if (!out || sub.m_size == 0) return out;
    XByteArray_push_back_2(out, (const char*)sub.m_data, (size_t)sub.m_size);
    return out;
}

XByteArray* XByteArray_right(const XByteArray* array, int64_t n)
{
    if (array == NULL) return NULL;
    /* 委托给 XByteArrayView_last_n 获取子视图，再拷贝到新容器 */
    XByteArrayView view = XByteArrayView_create_bytearray(array);
    XByteArrayView sub = XByteArrayView_last_n(&view, n);
    XByteArray* out = XByteArray_create();
    if (!out || sub.m_size == 0) return out;
    XByteArray_push_back_2(out, (const char*)sub.m_data, (size_t)sub.m_size);
    return out;
}

XByteArray* XByteArray_mid(const XByteArray* array, int64_t pos, int64_t n)
{
    if (array == NULL) return NULL;
    /* 委托给 XByteArrayView_mid 获取子视图，再拷贝到新容器 */
    XByteArrayView view = XByteArrayView_create_bytearray(array);
    XByteArrayView sub = XByteArrayView_mid(&view, pos, n);
    XByteArray* out = XByteArray_create();
    if (!out || sub.m_size == 0) return out;
    XByteArray_push_back_2(out, (const char*)sub.m_data, (size_t)sub.m_size);
    return out;
}



/* ============================== Qt 6.8 命名对齐 (重量项): 实现 ============================== */

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ---- replace ---- */
size_t XByteArray_replace(XByteArray* array,
    const uint8_t* before, size_t beforeLen,
    const uint8_t* after,  size_t afterLen)
{
    if (array == NULL || beforeLen == 0) return 0;
    size_t count = 0;
    size_t i = 0;
    for (;;) {
        size_t n = XByteArray_size_base(array);
        if (i + beforeLen > n) break;
        const uint8_t* d = XByteArray_data(array);
        if (memcmp(d + i, before, beforeLen) == 0) {
            /* 删除 before, 插入 after */
            XVector_remove_base((XVector*)array, (int64_t)i, (int64_t)beforeLen);
            if (afterLen > 0) {
                XVector_insert_1_base((XVector*)array, (int64_t)i, after, afterLen);
            }
            i += afterLen;
            ++count;
        } else {
            ++i;
        }
    }
    return count;
}

/* ---- split ---- */
XVector* XByteArray_split(const XByteArray* array, uint8_t sep)
{
    XVector* parts = XVector_create(sizeof(XByteArray*));
    if (!parts || !array) return parts;
    size_t n = XByteArray_size_base((XByteArray*)array);
    const uint8_t* d = XByteArray_data((XByteArray*)array);
    size_t start = 0;
    for (size_t i = 0; i <= n; ++i) {
        if (i == n || d[i] == sep) {
            XByteArray* piece = XByteArray_create();
            if (i > start) {
                XByteArray_push_back_2(piece, (const char*)(d + start), i - start);
            }
            XVector_push_back_1_base(parts, &piece);
            start = i + 1;
        }
    }
    return parts;
}

void XByteArray_split_free(XVector* parts)
{
    if (!parts) return;
    size_t n = XVector_size_base(parts);
    for (size_t i = 0; i < n; ++i) {
        XByteArray** pp = (XByteArray**)XVector_at_base(parts, i);
        if (pp && *pp) XByteArray_delete_base(*pp);
    }
    XVector_delete_base(parts);
}

/* ---- trimmed / simplified ---- */
static int xba_is_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

XByteArray* XByteArray_trimmed(const XByteArray* array)
{
    if (!array) return XByteArray_create();
    /* 委托给 XByteArrayView_trimmed 获取修剪后的子视图，再拷贝到新容器 */
    XByteArrayView view = XByteArrayView_create_bytearray(array);
    XByteArrayView sub = XByteArrayView_trimmed(&view);
    XByteArray* out = XByteArray_create();
    if (!out || sub.m_size == 0) return out;
    XByteArray_push_back_2(out, (const char*)sub.m_data, (size_t)sub.m_size);
    return out;
}

XByteArray* XByteArray_simplified(const XByteArray* array)
{
    XByteArray* out = XByteArray_create();
    if (!array || !out) return out;
    size_t n = XByteArray_size_base((XByteArray*)array);
    const uint8_t* d = XByteArray_data((XByteArray*)array);
    size_t i = 0;
    while (i < n && xba_is_space(d[i])) ++i;
    bool prevSpace = false;
    for (; i < n; ++i) {
        if (xba_is_space(d[i])) {
            prevSpace = true;
        } else {
            if (prevSpace) {
                uint8_t sp = ' ';
                XByteArray_push_back_1(out, sp);
                prevSpace = false;
            }
            XByteArray_push_back_1(out, d[i]);
        }
    }
    return out;
}

/* ---- toUpper / toLower ---- */
XByteArray* XByteArray_toUpper(XByteArray* array)
{
    if (!array) return NULL;
    size_t n = XByteArray_size_base(array);
    uint8_t* d = XByteArray_data(array);
    for (size_t i = 0; i < n; ++i) {
        if (d[i] >= 'a' && d[i] <= 'z') d[i] = (uint8_t)(d[i] - 'a' + 'A');
    }
    return array;
}

XByteArray* XByteArray_toLower(XByteArray* array)
{
    if (!array) return NULL;
    size_t n = XByteArray_size_base(array);
    uint8_t* d = XByteArray_data(array);
    for (size_t i = 0; i < n; ++i) {
        if (d[i] >= 'A' && d[i] <= 'Z') d[i] = (uint8_t)(d[i] - 'A' + 'a');
    }
    return array;
}

/* ---- toLongLong / toInt / toDouble ---- */
int64_t XByteArray_toLongLong(const XByteArray* array, bool* ok, int base)
{
    /* 委托给 XByteArrayView_toLongLong，消除重复的 strtoll 实现 */
    XByteArrayView view = XByteArrayView_create_bytearray(array);
    return XByteArrayView_toLongLong(&view, ok, base);
}

int32_t XByteArray_toInt(const XByteArray* array, bool* ok, int base)
{
    /* 委托给 XByteArrayView_toInt，消除重复的数值转换代码 */
    XByteArrayView view = XByteArrayView_create_bytearray(array);
    return XByteArrayView_toInt(&view, ok, base);
}

double XByteArray_toDouble(const XByteArray* array, bool* ok)
{
    /* 委托给 XByteArrayView_toDouble */
    XByteArrayView view = XByteArrayView_create_bytearray(array);
    return XByteArrayView_toDouble(&view, ok);
}

/* ---- setNum ---- */
XByteArray* XByteArray_setNum_i64(XByteArray* array, int64_t value, int base)
{
    if (!array) return NULL;
    XByteArray_clear_base(array);
    if (base < 2 || base > 36) base = 10;
    char buf[80];
    if (base == 10) {
        snprintf(buf, sizeof(buf), "%lld", (long long)value);
    } else {
        /* 手动按 base 转换 */
        char tmp[80]; int idx = 0;
        uint64_t uv;
        bool neg = false;
        if (value < 0) { neg = true; uv = (uint64_t)(-(value + 1)) + 1; }
        else           { uv = (uint64_t)value; }
        if (uv == 0) tmp[idx++] = '0';
        while (uv > 0) {
            int r = (int)(uv % (uint64_t)base);
            tmp[idx++] = (char)(r < 10 ? ('0' + r) : ('a' + r - 10));
            uv /= (uint64_t)base;
        }
        int p = 0;
        if (neg) buf[p++] = '-';
        while (idx > 0) buf[p++] = tmp[--idx];
        buf[p] = 0;
    }
    XByteArray_push_back_2(array, buf, strlen(buf));
    return array;
}

XByteArray* XByteArray_setNum_i32(XByteArray* array, int32_t value, int base)
{
    return XByteArray_setNum_i64(array, (int64_t)value, base);
}

XByteArray* XByteArray_setNum_double(XByteArray* array, double value, char fmt, int prec)
{
    if (!array) return NULL;
    XByteArray_clear_base(array);
    if (prec < 0) prec = 6;
    char pattern[16];
    char f = (fmt == 'e' || fmt == 'E' || fmt == 'f' || fmt == 'F' || fmt == 'g' || fmt == 'G') ? fmt : 'g';
    snprintf(pattern, sizeof(pattern), "%%.%d%c", prec, f);
    char buf[64];
    snprintf(buf, sizeof(buf), pattern, value);
    XByteArray_push_back_2(array, buf, strlen(buf));
    return array;
}

/* ---- toPercentEncoding / fromPercentEncoding ---- */
static int xba_hexval(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

XByteArray* XByteArray_toPercentEncoding(const XByteArray* array)
{
    XByteArray* out = XByteArray_create();
    if (!array || !out) return out;
    size_t n = XByteArray_size_base((XByteArray*)array);
    const uint8_t* d = XByteArray_data((XByteArray*)array);
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = d[i];
        bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                 || c == '-' || c == '.' || c == '_' || c == '~';
        if (safe) {
            XByteArray_push_back_1(out, c);
        } else {
            uint8_t triplet[3] = { '%', (uint8_t)hex[(c >> 4) & 0xF], (uint8_t)hex[c & 0xF] };
            XByteArray_push_back_2(out, (const char*)triplet, 3);
        }
    }
    return out;
}

XByteArray* XByteArray_fromPercentEncoding(const XByteArray* array)
{
    XByteArray* out = XByteArray_create();
    if (!array || !out) return out;
    size_t n = XByteArray_size_base((XByteArray*)array);
    const uint8_t* d = XByteArray_data((XByteArray*)array);
    for (size_t i = 0; i < n; ) {
        uint8_t c = d[i];
        if (c == '%' && i + 2 < n) {
            int hi = xba_hexval(d[i+1]);
            int lo = xba_hexval(d[i+2]);
            if (hi >= 0 && lo >= 0) {
                uint8_t b = (uint8_t)((hi << 4) | lo);
                XByteArray_push_back_1(out, b);
                i += 3;
                continue;
            }
        }
        if (c == '+') { XByteArray_push_back_1(out, ' '); ++i; continue; }
        XByteArray_push_back_1(out, c);
        ++i;
    }
    return out;
}

/* ---- compare with case sensitivity ---- */
int32_t XByteArray_compareCS(const XByteArray* lhs, const XByteArray* rhs, int cs)
{
    /* 委托给 XByteArrayView_compare */
    XByteArrayView lv = XByteArrayView_create_bytearray(lhs);
    XByteArrayView rv = XByteArrayView_create_bytearray(rhs);
    return XByteArrayView_compare(&lv, &rv, cs);
}

#endif
