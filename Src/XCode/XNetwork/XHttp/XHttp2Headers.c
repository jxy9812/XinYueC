/**
 * @file       XHttp2Headers.c
 * @brief      HPACK 静态表头块编解码实现。
 */

#include "XHttp2Headers.h"

#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct XHttp2StaticHeader {
    const char* m_name;
    const char* m_value;
} XHttp2StaticHeader;

static const XHttp2StaticHeader xhttp2_static_table[] = {
    {":authority", ""}, {":method", "GET"}, {":method", "POST"}, {":path", "/"},
    {":path", "/index.html"}, {":scheme", "http"}, {":scheme", "https"}, {":status", "200"},
    {":status", "204"}, {":status", "206"}, {":status", "304"}, {":status", "400"},
    {":status", "404"}, {":status", "500"}, {"accept-charset", ""}, {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""}, {"accept-ranges", "bytes"}, {"accept", ""},
    {"access-control-allow-origin", ""}, {"age", ""}, {"allow", ""}, {"authorization", ""},
    {"cache-control", ""}, {"content-disposition", ""}, {"content-encoding", ""},
    {"content-language", ""}, {"content-length", ""}, {"content-location", ""},
    {"content-range", ""}, {"content-type", ""}, {"cookie", ""}, {"date", ""},
    {"etag", ""}, {"expect", ""}, {"expires", ""}, {"from", ""}, {"host", ""},
    {"if-match", ""}, {"if-modified-since", ""}, {"if-none-match", ""}, {"if-range", ""},
    {"if-unmodified-since", ""}, {"last-modified", ""}, {"link", ""}, {"location", ""},
    {"max-forwards", ""}, {"proxy-authenticate", ""}, {"proxy-authorization", ""},
    {"range", ""}, {"referer", ""}, {"refresh", ""}, {"retry-after", ""}, {"server", ""},
    {"set-cookie", ""}, {"strict-transport-security", ""}, {"transfer-encoding", ""},
    {"user-agent", ""}, {"vary", ""}, {"via", ""}, {"www-authenticate", ""}
};

static const struct XHttp2HuffmanCode {
    uint32_t m_code;
    uint8_t m_bits;
} xhttp2_huffman_table[257] = {
    {0xffc00000u, 13},
    {0xffffb000u, 23},
    {0xfffffe20u, 28},
    {0xfffffe30u, 28},
    {0xfffffe40u, 28},
    {0xfffffe50u, 28},
    {0xfffffe60u, 28},
    {0xfffffe70u, 28},
    {0xfffffe80u, 28},
    {0xffffea00u, 24},
    {0xfffffff0u, 30},
    {0xfffffe90u, 28},
    {0xfffffea0u, 28},
    {0xfffffff4u, 30},
    {0xfffffeb0u, 28},
    {0xfffffec0u, 28},
    {0xfffffed0u, 28},
    {0xfffffee0u, 28},
    {0xfffffef0u, 28},
    {0xffffff00u, 28},
    {0xffffff10u, 28},
    {0xffffff20u, 28},
    {0xfffffff8u, 30},
    {0xffffff30u, 28},
    {0xffffff40u, 28},
    {0xffffff50u, 28},
    {0xffffff60u, 28},
    {0xffffff70u, 28},
    {0xffffff80u, 28},
    {0xffffff90u, 28},
    {0xffffffa0u, 28},
    {0xffffffb0u, 28},
    {0x50000000u, 6},
    {0xfe000000u, 10},
    {0xfe400000u, 10},
    {0xffa00000u, 12},
    {0xffc80000u, 13},
    {0x54000000u, 6},
    {0xf8000000u, 8},
    {0xff400000u, 11},
    {0xfe800000u, 10},
    {0xfec00000u, 10},
    {0xf9000000u, 8},
    {0xff600000u, 11},
    {0xfa000000u, 8},
    {0x58000000u, 6},
    {0x5c000000u, 6},
    {0x60000000u, 6},
    {0x00000000u, 5},
    {0x08000000u, 5},
    {0x10000000u, 5},
    {0x64000000u, 6},
    {0x68000000u, 6},
    {0x6c000000u, 6},
    {0x70000000u, 6},
    {0x74000000u, 6},
    {0x78000000u, 6},
    {0x7c000000u, 6},
    {0xb8000000u, 7},
    {0xfb000000u, 8},
    {0xfff80000u, 15},
    {0x80000000u, 6},
    {0xffb00000u, 12},
    {0xff000000u, 10},
    {0xffd00000u, 13},
    {0x84000000u, 6},
    {0xba000000u, 7},
    {0xbc000000u, 7},
    {0xbe000000u, 7},
    {0xc0000000u, 7},
    {0xc2000000u, 7},
    {0xc4000000u, 7},
    {0xc6000000u, 7},
    {0xc8000000u, 7},
    {0xca000000u, 7},
    {0xcc000000u, 7},
    {0xce000000u, 7},
    {0xd0000000u, 7},
    {0xd2000000u, 7},
    {0xd4000000u, 7},
    {0xd6000000u, 7},
    {0xd8000000u, 7},
    {0xda000000u, 7},
    {0xdc000000u, 7},
    {0xde000000u, 7},
    {0xe0000000u, 7},
    {0xe2000000u, 7},
    {0xe4000000u, 7},
    {0xfc000000u, 8},
    {0xe6000000u, 7},
    {0xfd000000u, 8},
    {0xffd80000u, 13},
    {0xfffe0000u, 19},
    {0xffe00000u, 13},
    {0xfff00000u, 14},
    {0x88000000u, 6},
    {0xfffa0000u, 15},
    {0x18000000u, 5},
    {0x8c000000u, 6},
    {0x20000000u, 5},
    {0x90000000u, 6},
    {0x28000000u, 5},
    {0x94000000u, 6},
    {0x98000000u, 6},
    {0x9c000000u, 6},
    {0x30000000u, 5},
    {0xe8000000u, 7},
    {0xea000000u, 7},
    {0xa0000000u, 6},
    {0xa4000000u, 6},
    {0xa8000000u, 6},
    {0x38000000u, 5},
    {0xac000000u, 6},
    {0xec000000u, 7},
    {0xb0000000u, 6},
    {0x40000000u, 5},
    {0x48000000u, 5},
    {0xb4000000u, 6},
    {0xee000000u, 7},
    {0xf0000000u, 7},
    {0xf2000000u, 7},
    {0xf4000000u, 7},
    {0xf6000000u, 7},
    {0xfffc0000u, 15},
    {0xff800000u, 11},
    {0xfff40000u, 14},
    {0xffe80000u, 13},
    {0xffffffc0u, 28},
    {0xfffe6000u, 20},
    {0xffff4800u, 22},
    {0xfffe7000u, 20},
    {0xfffe8000u, 20},
    {0xffff4c00u, 22},
    {0xffff5000u, 22},
    {0xffff5400u, 22},
    {0xffffb200u, 23},
    {0xffff5800u, 22},
    {0xffffb400u, 23},
    {0xffffb600u, 23},
    {0xffffb800u, 23},
    {0xffffba00u, 23},
    {0xffffbc00u, 23},
    {0xffffeb00u, 24},
    {0xffffbe00u, 23},
    {0xffffec00u, 24},
    {0xffffed00u, 24},
    {0xffff5c00u, 22},
    {0xffffc000u, 23},
    {0xffffee00u, 24},
    {0xffffc200u, 23},
    {0xffffc400u, 23},
    {0xffffc600u, 23},
    {0xffffc800u, 23},
    {0xfffee000u, 21},
    {0xffff6000u, 22},
    {0xffffca00u, 23},
    {0xffff6400u, 22},
    {0xffffcc00u, 23},
    {0xffffce00u, 23},
    {0xffffef00u, 24},
    {0xffff6800u, 22},
    {0xfffee800u, 21},
    {0xfffe9000u, 20},
    {0xffff6c00u, 22},
    {0xffff7000u, 22},
    {0xffffd000u, 23},
    {0xffffd200u, 23},
    {0xfffef000u, 21},
    {0xffffd400u, 23},
    {0xffff7400u, 22},
    {0xffff7800u, 22},
    {0xfffff000u, 24},
    {0xfffef800u, 21},
    {0xffff7c00u, 22},
    {0xffffd600u, 23},
    {0xffffd800u, 23},
    {0xffff0000u, 21},
    {0xffff0800u, 21},
    {0xffff8000u, 22},
    {0xffff1000u, 21},
    {0xffffda00u, 23},
    {0xffff8400u, 22},
    {0xffffdc00u, 23},
    {0xffffde00u, 23},
    {0xfffea000u, 20},
    {0xffff8800u, 22},
    {0xffff8c00u, 22},
    {0xffff9000u, 22},
    {0xffffe000u, 23},
    {0xffff9400u, 22},
    {0xffff9800u, 22},
    {0xffffe200u, 23},
    {0xfffff800u, 26},
    {0xfffff840u, 26},
    {0xfffeb000u, 20},
    {0xfffe2000u, 19},
    {0xffff9c00u, 22},
    {0xffffe400u, 23},
    {0xffffa000u, 22},
    {0xfffff600u, 25},
    {0xfffff880u, 26},
    {0xfffff8c0u, 26},
    {0xfffff900u, 26},
    {0xfffffbc0u, 27},
    {0xfffffbe0u, 27},
    {0xfffff940u, 26},
    {0xfffff100u, 24},
    {0xfffff680u, 25},
    {0xfffe4000u, 19},
    {0xffff1800u, 21},
    {0xfffff980u, 26},
    {0xfffffc00u, 27},
    {0xfffffc20u, 27},
    {0xfffff9c0u, 26},
    {0xfffffc40u, 27},
    {0xfffff200u, 24},
    {0xffff2000u, 21},
    {0xffff2800u, 21},
    {0xfffffa00u, 26},
    {0xfffffa40u, 26},
    {0xffffffd0u, 28},
    {0xfffffc60u, 27},
    {0xfffffc80u, 27},
    {0xfffffca0u, 27},
    {0xfffec000u, 20},
    {0xfffff300u, 24},
    {0xfffed000u, 20},
    {0xffff3000u, 21},
    {0xffffa400u, 22},
    {0xffff3800u, 21},
    {0xffff4000u, 21},
    {0xffffe600u, 23},
    {0xffffa800u, 22},
    {0xffffac00u, 22},
    {0xfffff700u, 25},
    {0xfffff780u, 25},
    {0xfffff400u, 24},
    {0xfffff500u, 24},
    {0xfffffa80u, 26},
    {0xffffe800u, 23},
    {0xfffffac0u, 26},
    {0xfffffcc0u, 27},
    {0xfffffb00u, 26},
    {0xfffffb40u, 26},
    {0xfffffce0u, 27},
    {0xfffffd00u, 27},
    {0xfffffd20u, 27},
    {0xfffffd40u, 27},
    {0xfffffd60u, 27},
    {0xffffffe0u, 28},
    {0xfffffd80u, 27},
    {0xfffffda0u, 27},
    {0xfffffdc0u, 27},
    {0xfffffde0u, 27},
    {0xfffffe00u, 27},
    {0xfffffb80u, 26},
    {0xfffffffcu, 30}
};

static bool xhttp2_valid_name(const XByteArray* name)
{
    const uint8_t* data;
    size_t size;
    if (!name)
        return false;
    data = XByteArray_constData((XByteArray*)name);
    size = XContainer_size_base((const XContainer*)name);
    if (!data || size == 0)
        return false;
    for (size_t i = 0; i < size; ++i) {
        uint8_t c = data[i];
        if (i == 0 && c == ':')
            continue;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
              c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
              c == '^' || c == '_' || c == '`' || c == '|' || c == '~'))
            return false;
    }
    return data[0] != ':' || size > 1;
}

static bool xhttp2_valid_value(const XByteArray* value)
{
    const uint8_t* data;
    size_t size;
    if (!value)
        return false;
    data = XByteArray_constData((XByteArray*)value);
    size = XContainer_size_base((const XContainer*)value);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == 0 || data[i] == '\r' || data[i] == '\n')
            return false;
    }
    return true;
}

static void xhttp2_field_delete(XHttp2HeaderField* field)
{
    if (!field)
        return;
    if (field->m_name) XClass_delete_base((XClass*)field->m_name);
    if (field->m_value) XClass_delete_base((XClass*)field->m_value);
    field->m_name = NULL;
    field->m_value = NULL;
}

static bool xhttp2_reserve(XHttp2HeaderList* self, size_t count)
{
    XHttp2HeaderField* fields;
    size_t capacity;
    if (self->m_capacity >= count)
        return true;
    capacity = self->m_capacity ? self->m_capacity * 2 : 8;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) {
            capacity = count;
            break;
        }
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(XHttp2HeaderField))
        return false;
    fields = (XHttp2HeaderField*)XRealloc_System(self->m_fields,
                                                  capacity * sizeof(XHttp2HeaderField));
    if (!fields)
        return false;
    memset(fields + self->m_capacity, 0,
           (capacity - self->m_capacity) * sizeof(XHttp2HeaderField));
    self->m_fields = fields;
    self->m_capacity = capacity;
    return true;
}

static void VXHttp2HeaderList_deinit(XHttp2HeaderList* self)
{
    if (!self)
        return;
    for (size_t i = 0; i < self->m_size; ++i)
        xhttp2_field_delete(&self->m_fields[i]);
    if (self->m_fields) XFree_System(self->m_fields);
    self->m_fields = NULL;
    self->m_size = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttp2HeaderList_copy(XHttp2HeaderList* dest, const XHttp2HeaderList* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp2HeaderList_init(dest);
    for (size_t i = 0; i < src->m_size; ++i)
        if (!XHttp2HeaderList_append(dest, src->m_fields[i].m_name, src->m_fields[i].m_value))
            return;
}

static void VXHttp2HeaderList_move(XHttp2HeaderList* dest, XHttp2HeaderList* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp2HeaderList_init(dest);
    VXHttp2HeaderList_deinit(dest);
    dest->m_fields = src->m_fields;
    dest->m_size = src->m_size;
    dest->m_capacity = src->m_capacity;
    src->m_fields = NULL;
    src->m_size = 0;
    src->m_capacity = 0;
    XClass_init((XClass*)src);
    XClassSetVtable(src, XHttp2HeaderList);
}

XVtable* XHttp2HeaderList_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttp2HeaderList)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttp2HeaderList");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp2HeaderList_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttp2HeaderList_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttp2HeaderList_move);
    XCLASS_SHOW_SIZE_DEFAULT(XHttp2HeaderList);
    return XVTABLE_DEFAULT;
}

void XHttp2HeaderList_init(XHttp2HeaderList* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp2HeaderList);
}

XHttp2HeaderList* XHttp2HeaderList_create(void)
{
    XHttp2HeaderList* self = (XHttp2HeaderList*)XMalloc_System(sizeof(XHttp2HeaderList));
    if (!self)
        return NULL;
    XHttp2HeaderList_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

bool XHttp2HeaderList_append(XHttp2HeaderList* self,
                             const XByteArray* name,
                             const XByteArray* value)
{
    XHttp2HeaderField field;
    if (!self || !xhttp2_valid_name(name) || !xhttp2_valid_value(value) ||
        !xhttp2_reserve(self, self->m_size + 1))
        return false;
    field.m_name = XByteArray_create_copy(name);
    field.m_value = XByteArray_create_copy(value);
    if (!field.m_name || !field.m_value) {
        xhttp2_field_delete(&field);
        return false;
    }
    self->m_fields[self->m_size++] = field;
    return true;
}

size_t XHttp2HeaderList_size(const XHttp2HeaderList* self)
{
    return self ? self->m_size : 0;
}

const XByteArray* XHttp2HeaderList_nameAt_const(const XHttp2HeaderList* self, size_t index)
{
    return self && index < self->m_size ? self->m_fields[index].m_name : NULL;
}

const XByteArray* XHttp2HeaderList_valueAt_const(const XHttp2HeaderList* self, size_t index)
{
    return self && index < self->m_size ? self->m_fields[index].m_value : NULL;
}

static bool xhttp2_bytes_equal_literal(const XByteArray* value, const char* literal)
{
    size_t size = value ? XContainer_size_base((const XContainer*)value) : 0;
    size_t literalSize = literal ? strlen(literal) : 0;
    return size == literalSize && (!size || memcmp(XByteArray_constData((XByteArray*)value), literal, size) == 0);
}

static int xhttp2_static_exact(const XByteArray* name, const XByteArray* value)
{
    for (size_t i = 0; i < sizeof(xhttp2_static_table) / sizeof(xhttp2_static_table[0]); ++i) {
        if (xhttp2_bytes_equal_literal(name, xhttp2_static_table[i].m_name) &&
            xhttp2_bytes_equal_literal(value, xhttp2_static_table[i].m_value))
            return (int)i + 1;
    }
    return 0;
}

static int xhttp2_static_name(const XByteArray* name)
{
    for (size_t i = 0; i < sizeof(xhttp2_static_table) / sizeof(xhttp2_static_table[0]); ++i)
        if (xhttp2_bytes_equal_literal(name, xhttp2_static_table[i].m_name))
            return (int)i + 1;
    return 0;
}

static bool xhttp2_encode_integer(XByteArray* out, uint8_t prefix, uint8_t prefixBits, size_t value)
{
    size_t limit = ((size_t)1 << prefixBits) - 1;
    uint8_t first;
    if (!out || prefixBits == 0 || prefixBits > 8)
        return false;
    first = (uint8_t)(value < limit ? value : limit);
    if (!XByteArray_push_back_1(out, (uint8_t)(prefix | first)))
        return false;
    if (value < limit)
        return true;
    value -= limit;
    while (value >= 128) {
        if (!XByteArray_push_back_1(out, (uint8_t)((value & 127) | 128)))
            return false;
        value >>= 7;
    }
    return XByteArray_push_back_1(out, (uint8_t)value);
}

static XByteArray* xhttp2_huffman_encode(const XByteArray* value)
{
    XByteArray* result;
    uint8_t current = 0;
    uint8_t bits = 0;
    size_t i;
    if (!value)
        return NULL;
    result = XByteArray_create();
    if (!result)
        return NULL;
    for (i = 0; i < XByteArray_size_base(value); ++i) {
        uint8_t symbol = XByteArray_constData((XByteArray*)value)[i];
        uint32_t code = xhttp2_huffman_table[symbol].m_code;
        uint8_t codeBits = xhttp2_huffman_table[symbol].m_bits;
        uint8_t bit;
        code >>= 32 - codeBits;
        while (codeBits-- != 0) {
            bit = (uint8_t)((code >> codeBits) & 1u);
            current = (uint8_t)((current << 1) | bit);
            if (++bits == 8) {
                if (!XByteArray_push_back_1(result, current)) {
                    XClass_delete_base((XClass*)result);
                    return NULL;
                }
                current = 0;
                bits = 0;
            }
        }
    }
    if (bits != 0) {
        current = (uint8_t)(current << (8 - bits));
        current = (uint8_t)(current | (uint8_t)((1u << (8 - bits)) - 1u));
        if (!XByteArray_push_back_1(result, current)) {
            XClass_delete_base((XClass*)result);
            return NULL;
        }
    }
    return result;
}

static XByteArray* xhttp2_huffman_decode(const uint8_t* data, size_t size)
{
    XByteArray* result;
    uint32_t code = 0;
    uint8_t bits = 0;
    size_t i;
    if (!data && size != 0)
        return NULL;
    result = XByteArray_create();
    if (!result)
        return NULL;
    for (i = 0; i < size; ++i) {
        int bitIndex;
        for (bitIndex = 7; bitIndex >= 0; --bitIndex) {
            size_t symbol;
            bool matched = false;
            code = (code << 1) | ((data[i] >> bitIndex) & 1u);
            if (++bits > 30) {
                XClass_delete_base((XClass*)result);
                return NULL;
            }
            for (symbol = 0; symbol < 257; ++symbol) {
                const struct XHttp2HuffmanCode* entry = &xhttp2_huffman_table[symbol];
                if (entry->m_bits == bits &&
                    (entry->m_code >> (32 - bits)) == code) {
                    if (symbol == 256 || !XByteArray_push_back_1(result, (uint8_t)symbol)) {
                        XClass_delete_base((XClass*)result);
                        return NULL;
                    }
                    code = 0;
                    bits = 0;
                    matched = true;
                    break;
                }
            }
            (void)matched;
        }
    }
    if (bits > 7 || (bits != 0 && code != (uint32_t)((1u << bits) - 1u))) {
        XClass_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

static bool xhttp2_encode_string(XByteArray* out, const XByteArray* value,
                                 bool enableHuffman)
{
    size_t size = value ? XContainer_size_base((const XContainer*)value) : 0;
    XByteArray* compressed = enableHuffman ? xhttp2_huffman_encode(value) : NULL;
    const XByteArray* payload = compressed ? compressed : value;
    bool result = size <= UINT32_MAX && payload &&
                  xhttp2_encode_integer(out, compressed ? 0x80 : 0, 7,
                                        XByteArray_size_base((XByteArray*)payload)) &&
                  (XByteArray_size_base((XByteArray*)payload) == 0 ||
                   XByteArray_push_back_2((XVector*)out,
                                          XByteArray_constData((XByteArray*)payload),
                                          XByteArray_size_base((XByteArray*)payload)));
    if (compressed)
        XClass_delete_base((XClass*)compressed);
    return result;
}

XByteArray* XHttp2HeaderList_encode(const XHttp2HeaderList* self, bool enableHuffman)
{
    XByteArray* out;
    if (!self)
        return NULL;
    out = XByteArray_create();
    if (!out)
        return NULL;
    for (size_t i = 0; i < self->m_size; ++i) {
        const XByteArray* name = self->m_fields[i].m_name;
        const XByteArray* value = self->m_fields[i].m_value;
        int exact = xhttp2_static_exact(name, value);
        int nameIndex = xhttp2_static_name(name);
        if (exact) {
            if (!xhttp2_encode_integer(out, 0x80, 7, (size_t)exact))
                goto fail;
        } else {
            if (!xhttp2_encode_integer(out, 0, 4, (size_t)nameIndex))
                goto fail;
            if (!nameIndex && !xhttp2_encode_string(out, name, enableHuffman))
                goto fail;
            if (!xhttp2_encode_string(out, value, enableHuffman))
                goto fail;
        }
    }
    return out;
fail:
    XClass_delete_base((XClass*)out);
    return NULL;
}

static bool xhttp2_decode_integer(const uint8_t* data, size_t size, size_t* offset,
                                  uint8_t prefixBits, size_t* value)
{
    size_t limit;
    size_t result;
    size_t shift = 0;
    if (!data || !offset || !value || *offset >= size || prefixBits == 0 || prefixBits > 8)
        return false;
    limit = ((size_t)1 << prefixBits) - 1;
    result = data[*offset] & limit;
    if (result < limit) {
        *value = result;
        ++*offset;
        return true;
    }
    ++*offset;
    result = limit;
    while (*offset < size) {
        uint8_t byte = data[(*offset)++];
        if (shift >= sizeof(size_t) * 8 - 7)
            return false;
        result += (size_t)(byte & 127) << shift;
        if (!(byte & 128)) {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

static XByteArray* xhttp2_decode_string(const uint8_t* data, size_t size, size_t* offset)
{
    size_t length;
    XByteArray* result;
    bool compressed;
    if (!data || !offset || *offset >= size)
        return NULL;
    compressed = (data[*offset] & 0x80) != 0;
    if (!xhttp2_decode_integer(data, size, offset, 7, &length) || length > size - *offset)
        return NULL;
    result = compressed ? xhttp2_huffman_decode(data + *offset, length) :
                          XByteArray_create_with_data((const char*)(data + *offset), length);
    *offset += length;
    return result;
}

static void xhttp2_dynamic_clear(XHttp2HeaderField* fields, size_t size)
{
    size_t i;
    if (!fields)
        return;
    for (i = 0; i < size; ++i)
        xhttp2_field_delete(&fields[i]);
    XFree_System(fields);
}

static size_t xhttp2_dynamic_entry_size(const XHttp2HeaderField* field)
{
    size_t nameSize;
    size_t valueSize;
    if (!field || !field->m_name || !field->m_value)
        return SIZE_MAX;
    nameSize = XByteArray_size_base(field->m_name);
    valueSize = XByteArray_size_base(field->m_value);
    if (nameSize > SIZE_MAX - valueSize - 32)
        return SIZE_MAX;
    return nameSize + valueSize + 32;
}

static void xhttp2_dynamic_evict(XHttp2HeaderField* fields, size_t* size,
                                 size_t* dataSize, size_t maxSize)
{
    while (fields && size && dataSize && *size > 0 && *dataSize > maxSize) {
        size_t entrySize = xhttp2_dynamic_entry_size(&fields[*size - 1]);
        if (entrySize <= *dataSize)
            *dataSize -= entrySize;
        else
            *dataSize = 0;
        xhttp2_field_delete(&fields[*size - 1]);
        --*size;
    }
}

static bool xhttp2_dynamic_prepend(XHttp2HeaderField** fields, size_t* size,
                                   size_t* capacity, size_t* dataSize,
                                   size_t maxSize, const XByteArray* name,
                                   const XByteArray* value)
{
    XByteArray* nameCopy;
    XByteArray* valueCopy;
    XHttp2HeaderField* replacement;
    size_t entrySize;
    if (!fields || !size || !capacity || !dataSize || !name || !value)
        return false;
    entrySize = XByteArray_size_base(name) + XByteArray_size_base(value) + 32;
    if (entrySize < XByteArray_size_base(name) || entrySize < XByteArray_size_base(value))
        return false;
    if (entrySize > maxSize) {
        xhttp2_dynamic_clear(*fields, *size);
        *fields = NULL;
        *size = 0;
        *capacity = 0;
        *dataSize = 0;
        return true;
    }
    nameCopy = XByteArray_create_copy(name);
    valueCopy = XByteArray_create_copy(value);
    if (!nameCopy || !valueCopy) {
        if (nameCopy) XClass_delete_base((XClass*)nameCopy);
        if (valueCopy) XClass_delete_base((XClass*)valueCopy);
        return false;
    }
    while (*size > 0 && *dataSize > maxSize - entrySize) {
        size_t oldSize = xhttp2_dynamic_entry_size(&(*fields)[*size - 1]);
        if (oldSize <= *dataSize) *dataSize -= oldSize; else *dataSize = 0;
        xhttp2_field_delete(&(*fields)[*size - 1]);
        --*size;
    }
    if (*size == *capacity) {
        size_t newCapacity = *capacity ? *capacity * 2 : 8;
        if (newCapacity < *size + 1 || newCapacity > SIZE_MAX / sizeof(**fields))
            newCapacity = *size + 1;
        replacement = (XHttp2HeaderField*)XRealloc_System(
            *fields, newCapacity * sizeof(**fields));
        if (!replacement) {
            XClass_delete_base((XClass*)nameCopy);
            XClass_delete_base((XClass*)valueCopy);
            return false;
        }
        *fields = replacement;
        *capacity = newCapacity;
    }
    if (*size > 0)
        memmove(*fields + 1, *fields, *size * sizeof(**fields));
    (*fields)[0].m_name = nameCopy;
    (*fields)[0].m_value = valueCopy;
    ++*size;
    *dataSize += entrySize;
    xhttp2_dynamic_evict(*fields, size, dataSize, maxSize);
    return true;
}

static void xhttp2_encoder_clear_dynamic(XHttp2HeaderEncoder* self)
{
    if (!self)
        return;
    xhttp2_dynamic_clear(self->m_dynamicFields, self->m_dynamicSize);
    self->m_dynamicFields = NULL;
    self->m_dynamicSize = 0;
    self->m_dynamicCapacity = 0;
    self->m_dynamicDataSize = 0;
}

static bool xhttp2_encoder_copy_dynamic(XHttp2HeaderEncoder* dest,
                                        const XHttp2HeaderEncoder* src)
{
    if (!dest || !src)
        return false;
    for (size_t i = src->m_dynamicSize; i > 0; --i) {
        const XHttp2HeaderField* field = &src->m_dynamicFields[i - 1];
        if (!xhttp2_dynamic_prepend(&dest->m_dynamicFields, &dest->m_dynamicSize,
                                    &dest->m_dynamicCapacity, &dest->m_dynamicDataSize,
                                    dest->m_dynamicMaxSize, field->m_name,
                                    field->m_value))
            return false;
    }
    return true;
}

static void VXHttp2HeaderEncoder_deinit(XHttp2HeaderEncoder* self)
{
    if (!self)
        return;
    xhttp2_encoder_clear_dynamic(self);
    self->m_dynamicMaxSize = 0;
    self->m_maxSize = 0;
    self->m_sizeUpdatePending = false;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttp2HeaderEncoder_copy(XHttp2HeaderEncoder* dest,
                                      const XHttp2HeaderEncoder* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttp2HeaderEncoder_init(dest);
    xhttp2_encoder_clear_dynamic(dest);
    dest->m_dynamicMaxSize = src->m_dynamicMaxSize;
    dest->m_maxSize = src->m_maxSize;
    dest->m_sizeUpdatePending = src->m_sizeUpdatePending;
    if (!xhttp2_encoder_copy_dynamic(dest, src))
        xhttp2_encoder_clear_dynamic(dest);
}

static void VXHttp2HeaderEncoder_move(XHttp2HeaderEncoder* dest,
                                      XHttp2HeaderEncoder* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttp2HeaderEncoder_init(dest);
    xhttp2_encoder_clear_dynamic(dest);
    dest->m_dynamicFields = src->m_dynamicFields;
    dest->m_dynamicSize = src->m_dynamicSize;
    dest->m_dynamicCapacity = src->m_dynamicCapacity;
    dest->m_dynamicDataSize = src->m_dynamicDataSize;
    dest->m_dynamicMaxSize = src->m_dynamicMaxSize;
    dest->m_maxSize = src->m_maxSize;
    dest->m_sizeUpdatePending = src->m_sizeUpdatePending;
    src->m_dynamicFields = NULL;
    src->m_dynamicSize = 0;
    src->m_dynamicCapacity = 0;
    src->m_dynamicDataSize = 0;
    src->m_dynamicMaxSize = 4096;
    src->m_maxSize = 4096;
    src->m_sizeUpdatePending = false;
    XClass_init((XClass*)src);
    XClassSetVtable(src, XHttp2HeaderEncoder);
}

XVtable* XHttp2HeaderEncoder_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttp2HeaderEncoder)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttp2HeaderEncoder");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp2HeaderEncoder_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttp2HeaderEncoder_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttp2HeaderEncoder_move);
    XCLASS_SHOW_SIZE_DEFAULT(XHttp2HeaderEncoder);
    return XVTABLE_DEFAULT;
}

void XHttp2HeaderEncoder_init(XHttp2HeaderEncoder* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp2HeaderEncoder);
    self->m_dynamicMaxSize = 4096;
    self->m_maxSize = 4096;
}

XHttp2HeaderEncoder* XHttp2HeaderEncoder_create(void)
{
    XHttp2HeaderEncoder* self = (XHttp2HeaderEncoder*)XMalloc_System(sizeof(*self));
    if (!self)
        return NULL;
    XHttp2HeaderEncoder_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

bool XHttp2HeaderEncoder_setMaxDynamicTableSize(XHttp2HeaderEncoder* self,
                                                size_t size)
{
    if (!self || size > 65536)
        return false;
    if (self->m_dynamicMaxSize != size)
        self->m_sizeUpdatePending = true;
    self->m_maxSize = size;
    self->m_dynamicMaxSize = size;
    xhttp2_dynamic_evict(self->m_dynamicFields, &self->m_dynamicSize,
                         &self->m_dynamicDataSize, self->m_dynamicMaxSize);
    return true;
}

size_t XHttp2HeaderEncoder_maxDynamicTableSize(const XHttp2HeaderEncoder* self)
{
    return self ? self->m_maxSize : 0;
}

static bool xhttp2_bytes_equal(const XByteArray* left, const XByteArray* right)
{
    size_t leftSize = left ? XContainer_size_base((const XContainer*)left) : 0;
    size_t rightSize = right ? XContainer_size_base((const XContainer*)right) : 0;
    if (!left || !right || leftSize != rightSize)
        return false;
    return leftSize == 0 || memcmp(XByteArray_constData((XByteArray*)left),
                                   XByteArray_constData((XByteArray*)right),
                                   leftSize) == 0;
}

static int xhttp2_encoder_dynamic_exact(const XHttp2HeaderEncoder* self,
                                        const XByteArray* name, const XByteArray* value)
{
    size_t staticSize = sizeof(xhttp2_static_table) / sizeof(xhttp2_static_table[0]);
    if (!self)
        return 0;
    for (size_t i = 0; i < self->m_dynamicSize; ++i) {
        const XHttp2HeaderField* field = &self->m_dynamicFields[i];
        if (xhttp2_bytes_equal(field->m_name, name) &&
            xhttp2_bytes_equal(field->m_value, value))
            return (int)(staticSize + i + 1);
    }
    return 0;
}

static int xhttp2_encoder_dynamic_name(const XHttp2HeaderEncoder* self,
                                       const XByteArray* name)
{
    size_t staticSize = sizeof(xhttp2_static_table) / sizeof(xhttp2_static_table[0]);
    if (!self)
        return 0;
    for (size_t i = 0; i < self->m_dynamicSize; ++i) {
        if (xhttp2_bytes_equal(self->m_dynamicFields[i].m_name, name))
            return (int)(staticSize + i + 1);
    }
    return 0;
}

static bool xhttp2_encoder_sensitive_name(const XByteArray* name)
{
    return xhttp2_bytes_equal_literal(name, "authorization") ||
           xhttp2_bytes_equal_literal(name, "cookie") ||
           xhttp2_bytes_equal_literal(name, "set-cookie");
}

XByteArray* XHttp2HeaderEncoder_encode(XHttp2HeaderEncoder* self,
                                       const XHttp2HeaderList* headers,
                                       bool enableHuffman)
{
    XByteArray* result;
    if (!self || !headers)
        return NULL;
    result = XByteArray_create();
    if (!result)
        return NULL;
    if (self->m_sizeUpdatePending &&
        !xhttp2_encode_integer(result, 0x20, 5, self->m_dynamicMaxSize))
        goto fail;
    for (size_t i = 0; i < headers->m_size; ++i) {
        const XByteArray* name = headers->m_fields[i].m_name;
        const XByteArray* value = headers->m_fields[i].m_value;
        int exact;
        int nameIndex;
        bool incremental;
        if (!name || !value)
            goto fail;
        exact = xhttp2_static_exact(name, value);
        if (!exact)
            exact = xhttp2_encoder_dynamic_exact(self, name, value);
        if (exact) {
            if (!xhttp2_encode_integer(result, 0x80, 7, (size_t)exact))
                goto fail;
            continue;
        }
        nameIndex = xhttp2_static_name(name);
        if (!nameIndex)
            nameIndex = xhttp2_encoder_dynamic_name(self, name);
        incremental = !xhttp2_encoder_sensitive_name(name);
        if (!xhttp2_encode_integer(result, incremental ? 0x40 : 0x10,
                                   incremental ? 6 : 4, (size_t)nameIndex) ||
            (!nameIndex && !xhttp2_encode_string(result, name, enableHuffman)) ||
            !xhttp2_encode_string(result, value, enableHuffman))
            goto fail;
        if (incremental && !xhttp2_dynamic_prepend(&self->m_dynamicFields,
                                                   &self->m_dynamicSize,
                                                   &self->m_dynamicCapacity,
                                                   &self->m_dynamicDataSize,
                                                   self->m_dynamicMaxSize,
                                                   name, value))
            goto fail;
    }
    self->m_sizeUpdatePending = false;
    return result;
fail:
    XClass_delete_base((XClass*)result);
    return NULL;
}

static void xhttp2_decoder_clear_dynamic(XHttp2HeaderDecoder* self)
{
    if (!self)
        return;
    xhttp2_dynamic_clear(self->m_dynamicFields, self->m_dynamicSize);
    self->m_dynamicFields = NULL;
    self->m_dynamicSize = 0;
    self->m_dynamicCapacity = 0;
    self->m_dynamicDataSize = 0;
}

static bool xhttp2_decoder_copy_dynamic(XHttp2HeaderDecoder* dest,
                                        const XHttp2HeaderDecoder* src)
{
    if (!dest || !src)
        return false;
    for (size_t i = src->m_dynamicSize; i > 0; --i) {
        const XHttp2HeaderField* field = &src->m_dynamicFields[i - 1];
        if (!xhttp2_dynamic_prepend(&dest->m_dynamicFields, &dest->m_dynamicSize,
                                    &dest->m_dynamicCapacity, &dest->m_dynamicDataSize,
                                    dest->m_dynamicMaxSize, field->m_name,
                                    field->m_value))
            return false;
    }
    return true;
}

static void VXHttp2HeaderDecoder_deinit(XHttp2HeaderDecoder* self)
{
    if (!self)
        return;
    xhttp2_decoder_clear_dynamic(self);
    self->m_dynamicMaxSize = 0;
    self->m_maxSize = 0;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttp2HeaderDecoder_copy(XHttp2HeaderDecoder* dest,
                                      const XHttp2HeaderDecoder* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttp2HeaderDecoder_init(dest);
    xhttp2_decoder_clear_dynamic(dest);
    dest->m_dynamicMaxSize = src->m_dynamicMaxSize;
    dest->m_maxSize = src->m_maxSize;
    if (!xhttp2_decoder_copy_dynamic(dest, src))
        xhttp2_decoder_clear_dynamic(dest);
}

static void VXHttp2HeaderDecoder_move(XHttp2HeaderDecoder* dest,
                                      XHttp2HeaderDecoder* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttp2HeaderDecoder_init(dest);
    xhttp2_decoder_clear_dynamic(dest);
    dest->m_dynamicFields = src->m_dynamicFields;
    dest->m_dynamicSize = src->m_dynamicSize;
    dest->m_dynamicCapacity = src->m_dynamicCapacity;
    dest->m_dynamicDataSize = src->m_dynamicDataSize;
    dest->m_dynamicMaxSize = src->m_dynamicMaxSize;
    dest->m_maxSize = src->m_maxSize;
    src->m_dynamicFields = NULL;
    src->m_dynamicSize = 0;
    src->m_dynamicCapacity = 0;
    src->m_dynamicDataSize = 0;
    src->m_dynamicMaxSize = 4096;
    src->m_maxSize = 4096;
    XClass_init((XClass*)src);
    XClassSetVtable(src, XHttp2HeaderDecoder);
}

XVtable* XHttp2HeaderDecoder_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttp2HeaderDecoder)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttp2HeaderDecoder");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp2HeaderDecoder_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttp2HeaderDecoder_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttp2HeaderDecoder_move);
    XCLASS_SHOW_SIZE_DEFAULT(XHttp2HeaderDecoder);
    return XVTABLE_DEFAULT;
}

void XHttp2HeaderDecoder_init(XHttp2HeaderDecoder* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp2HeaderDecoder);
    self->m_dynamicMaxSize = 4096;
    self->m_maxSize = 4096;
}

XHttp2HeaderDecoder* XHttp2HeaderDecoder_create(void)
{
    XHttp2HeaderDecoder* self = (XHttp2HeaderDecoder*)XMalloc_System(sizeof(*self));
    if (!self)
        return NULL;
    XHttp2HeaderDecoder_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

bool XHttp2HeaderDecoder_setMaxDynamicTableSize(XHttp2HeaderDecoder* self,
                                                size_t size)
{
    if (!self || size > 65536)
        return false;
    self->m_maxSize = size;
    if (self->m_dynamicMaxSize > size)
        self->m_dynamicMaxSize = size;
    xhttp2_dynamic_evict(self->m_dynamicFields, &self->m_dynamicSize,
                         &self->m_dynamicDataSize, self->m_dynamicMaxSize);
    return true;
}

size_t XHttp2HeaderDecoder_maxDynamicTableSize(const XHttp2HeaderDecoder* self)
{
    return self ? self->m_maxSize : 0;
}

static bool xhttp2_table_field(size_t index, XHttp2HeaderField* dynamicFields,
                               size_t dynamicSize, XByteArray** name,
                               XByteArray** value)
{
    if (!name || !value || index == 0)
        return false;
    *name = NULL;
    *value = NULL;
    if (index <= sizeof(xhttp2_static_table) / sizeof(xhttp2_static_table[0])) {
        *name = XByteArray_create_utf8(xhttp2_static_table[index - 1].m_name);
        *value = XByteArray_create_utf8(xhttp2_static_table[index - 1].m_value);
    } else {
        size_t dynamicIndex = index - sizeof(xhttp2_static_table) /
                              sizeof(xhttp2_static_table[0]) - 1;
        if (!dynamicFields || dynamicIndex >= dynamicSize)
            return false;
        *name = XByteArray_create_copy(dynamicFields[dynamicIndex].m_name);
        *value = XByteArray_create_copy(dynamicFields[dynamicIndex].m_value);
    }
    if (!*name || !*value) {
        if (*name) XClass_delete_base((XClass*)*name);
        if (*value) XClass_delete_base((XClass*)*value);
        *name = NULL;
        *value = NULL;
        return false;
    }
    return true;
}

XHttp2HeaderList* XHttp2HeaderDecoder_decode(XHttp2HeaderDecoder* decoder,
                                             const void* rawData, size_t size)
{
    const uint8_t* data = (const uint8_t*)rawData;
    XHttp2HeaderList* result;
    size_t offset = 0;
    if (!decoder || (!data && size != 0))
        return NULL;
    result = XHttp2HeaderList_create();
    if (!result)
        return NULL;
    while (offset < size) {
        size_t index;
        XByteArray* name = NULL;
        XByteArray* value = NULL;
        bool incremental = false;
        if ((data[offset] & 0xe0) == 0x20) {
            size_t newSize;
            if (!xhttp2_decode_integer(data, size, &offset, 5, &newSize) ||
                newSize > decoder->m_maxSize)
                goto fail;
            decoder->m_dynamicMaxSize = newSize;
            xhttp2_dynamic_evict(decoder->m_dynamicFields, &decoder->m_dynamicSize,
                                 &decoder->m_dynamicDataSize, decoder->m_dynamicMaxSize);
            continue;
        }
        if (data[offset] & 0x80) {
            if (!xhttp2_decode_integer(data, size, &offset, 7, &index) || index == 0 ||
                !xhttp2_table_field(index, decoder->m_dynamicFields,
                                    decoder->m_dynamicSize, &name, &value))
                goto fail;
        } else {
            uint8_t prefixBits = (data[offset] & 0x40) ? 6 : 4;
            incremental = (data[offset] & 0x40) != 0;
            if (!xhttp2_decode_integer(data, size, &offset, prefixBits, &index))
                goto fail;
            if (index != 0) {
                XByteArray* indexedValue = NULL;
                if (!xhttp2_table_field(index, decoder->m_dynamicFields,
                                        decoder->m_dynamicSize, &name, &indexedValue))
                    goto fail;
                XClass_delete_base((XClass*)indexedValue);
            } else {
                name = xhttp2_decode_string(data, size, &offset);
            }
            value = xhttp2_decode_string(data, size, &offset);
        }
        if (!name || !value || !XHttp2HeaderList_append(result, name, value)) {
            if (name) XClass_delete_base((XClass*)name);
            if (value) XClass_delete_base((XClass*)value);
            goto fail;
        }
        XClass_delete_base((XClass*)name);
        XClass_delete_base((XClass*)value);
        if (incremental && !xhttp2_dynamic_prepend(&decoder->m_dynamicFields,
                                                   &decoder->m_dynamicSize,
                                                   &decoder->m_dynamicCapacity,
                                                   &decoder->m_dynamicDataSize,
                                                   decoder->m_dynamicMaxSize,
                                                   result->m_fields[result->m_size - 1].m_name,
                                                   result->m_fields[result->m_size - 1].m_value))
            goto fail;
    }
    return result;
fail:
    XClass_delete_base((XClass*)result);
    return NULL;
}

XHttp2HeaderList* XHttp2HeaderList_decode(const void* rawData, size_t size)
{
    XHttp2HeaderDecoder* decoder = XHttp2HeaderDecoder_create();
    XHttp2HeaderList* result = decoder ?
        XHttp2HeaderDecoder_decode(decoder, rawData, size) : NULL;
    if (decoder)
        XClass_delete_base((XClass*)decoder);
    return result;
}
