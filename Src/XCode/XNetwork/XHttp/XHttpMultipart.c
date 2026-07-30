/**
 * @file       XHttpMultipart.c
 * @brief      HTTP MIME multipart 部件和 body 构造实现。
 */

#include "XHttpMultipart.h"

#include "XMemory.h"
#include "XRandomGenerator.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void VXHttpPart_deinit(XHttpPart* self);
static void VXHttpPart_copy(XHttpPart* dest, const XHttpPart* src);
static void VXHttpPart_move(XHttpPart* dest, XHttpPart* src);
static void VXHttpMultiPart_deinit(XHttpMultiPart* self);

static void xhttp_part_release_members(XHttpPart* self)
{
    if (!self)
        return;
    if (self->m_headers) {
        XClass_delete_base((XClass*)self->m_headers);
        self->m_headers = NULL;
    }
    if (self->m_body) {
        XClass_delete_base((XClass*)self->m_body);
        self->m_body = NULL;
    }
    self->m_bodyDevice = NULL;
}

static bool xhttp_append_bytes(XByteArray* target, const void* data, size_t size)
{
    if (!target || (!data && size != 0))
        return false;
    return size == 0 || XByteArray_push_back_2((XVector*)target, data, size);
}

static const char* xhttp_part_header_name(XHttpPart_KnownHeader header)
{
    switch (header) {
    case XHttpPart_ContentTypeHeader: return "Content-Type";
    case XHttpPart_ContentDispositionHeader: return "Content-Disposition";
    case XHttpPart_ContentTransferEncodingHeader: return "Content-Transfer-Encoding";
    case XHttpPart_ContentIdHeader: return "Content-ID";
    case XHttpPart_ContentDescriptionHeader: return "Content-Description";
    default: return NULL;
    }
}

static bool xhttp_multipart_valid_boundary(const XByteArray* boundary)
{
    const uint8_t* data;
    size_t size;
    if (!boundary)
        return false;
    data = XByteArray_constData((XByteArray*)boundary);
    size = XByteArray_size_base((XContainer*)boundary);
    if (!data || size == 0 || size > 70)
        return false;
    for (size_t i = 0; i < size; ++i) {
        uint8_t c = data[i];
        if (c <= 0x20 || c >= 0x7f || c == '"' || c == '\\')
            return false;
    }
    return true;
}

static bool xhttp_multipart_make_boundary(XByteArray* boundary)
{
    static const char hex[] = "0123456789abcdef";
    XRandomGenerator* generator;
    if (!boundary || !XByteArray_append_utf8(boundary, "boundary_.oOo._"))
        return false;
    generator = XRandomGenerator_global();
    if (!generator)
        return false;
    for (size_t i = 0; i < 24; ++i) {
        uint8_t value = (uint8_t)(XRandomGenerator_generate(generator) & 0x0fU);
        if (!XByteArray_push_back_1(boundary, (uint8_t)hex[value]))
            return false;
    }
    return true;
}

XVtable* XHttpPart_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHttpPart))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpPart_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttpPart_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttpPart_move);
    return XVTABLE_DEFAULT;
}

void XHttpPart_init(XHttpPart* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpPart);
    self->m_headers = XHttpHeaders_create();
    self->m_body = XByteArray_create();
}

XHttpPart* XHttpPart_create(void)
{
    XHttpPart* self = (XHttpPart*)XMalloc_System(sizeof(XHttpPart));
    if (!self)
        return NULL;
    XHttpPart_init(self);
    if (!self->m_headers || !self->m_body) {
        XHttpPart_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttpPart* XHttpPart_create_copy(const XHttpPart* other)
{
    XHttpPart* self;
    if (!other)
        return NULL;
    self = XHttpPart_create();
    if (self)
        XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XHttpPart* XHttpPart_create_move(XHttpPart* other)
{
    XHttpPart* self;
    if (!other)
        return NULL;
    self = XHttpPart_create();
    if (self)
        XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

static void VXHttpPart_deinit(XHttpPart* self)
{
    if (!self)
        return;
    xhttp_part_release_members(self);
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttpPart_copy(XHttpPart* dest, const XHttpPart* src)
{
    XHttpHeaders* headers;
    XByteArray* body;
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpPart_init(dest);
    headers = src->m_headers ? XHttpHeaders_create_copy(src->m_headers) : XHttpHeaders_create();
    body = src->m_body ? XByteArray_create_copy(src->m_body) : XByteArray_create();
    if (!headers || !body) {
        if (headers) XClass_delete_base((XClass*)headers);
        if (body) XClass_delete_base((XClass*)body);
        return;
    }
    xhttp_part_release_members(dest);
    dest->m_headers = headers;
    dest->m_body = body;
    dest->m_bodyDevice = src->m_bodyDevice;
}

static void VXHttpPart_move(XHttpPart* dest, XHttpPart* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpPart_init(dest);
    xhttp_part_release_members(dest);
    dest->m_headers = src->m_headers;
    dest->m_body = src->m_body;
    dest->m_bodyDevice = src->m_bodyDevice;
    src->m_headers = NULL;
    src->m_body = NULL;
    src->m_bodyDevice = NULL;
}

bool XHttpPart_setRawHeader(XHttpPart* self, const XByteArray* name, const XByteArray* value)
{
    return self && self->m_headers && XHttpHeaders_replaceOrAppend(self->m_headers, name, value);
}

bool XHttpPart_setRawHeader_utf8(XHttpPart* self, const char* name, const char* value)
{
    XByteArray* nameBytes;
    XByteArray* valueBytes;
    bool result;
    if (!self || !name)
        return false;
    nameBytes = XByteArray_create_utf8(name);
    valueBytes = XByteArray_create_utf8(value ? value : "");
    result = nameBytes && valueBytes && XHttpPart_setRawHeader(self, nameBytes, valueBytes);
    if (nameBytes) XClass_delete_base((XClass*)nameBytes);
    if (valueBytes) XClass_delete_base((XClass*)valueBytes);
    return result;
}

bool XHttpPart_setHeader(XHttpPart* self, XHttpPart_KnownHeader header, const XByteArray* value)
{
    const char* name = xhttp_part_header_name(header);
    XByteArray* nameBytes;
    bool result;
    if (!name)
        return false;
    nameBytes = XByteArray_create_utf8(name);
    result = nameBytes && XHttpPart_setRawHeader(self, nameBytes, value);
    if (nameBytes) XClass_delete_base((XClass*)nameBytes);
    return result;
}

bool XHttpPart_setBody(XHttpPart* self, const XByteArray* body)
{
    XByteArray* replacement;
    if (!self)
        return false;
    replacement = body ? XByteArray_create_copy(body) : XByteArray_create();
    if (!replacement)
        return false;
    if (self->m_body) XClass_delete_base((XClass*)self->m_body);
    self->m_body = replacement;
    self->m_bodyDevice = NULL;
    return true;
}

bool XHttpPart_setBody_utf8(XHttpPart* self, const char* body)
{
    XByteArray* bytes;
    bool result;
    if (!self)
        return false;
    bytes = XByteArray_create_utf8(body ? body : "");
    result = bytes && XHttpPart_setBody(self, bytes);
    if (bytes) XClass_delete_base((XClass*)bytes);
    return result;
}

bool XHttpPart_setBodyDevice(XHttpPart* self, XIODevice* device)
{
    if (!self)
        return false;
    self->m_bodyDevice = device;
    return true;
}

const XHttpHeaders* XHttpPart_headers_const(const XHttpPart* self)
{
    return self ? self->m_headers : NULL;
}

const XByteArray* XHttpPart_body_const(const XHttpPart* self)
{
    return self ? self->m_body : NULL;
}

XIODevice* XHttpPart_bodyDevice(const XHttpPart* self)
{
    return self ? self->m_bodyDevice : NULL;
}

XVtable* XHttpMultiPart_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHttpMultiPart))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpMultiPart_deinit);
    return XVTABLE_DEFAULT;
}

void XHttpMultiPart_init(XHttpMultiPart* self, XHttpMultiPart_ContentType type)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XHttpMultiPart);
    self->m_parts = XVector_create(sizeof(XHttpPart*));
    self->m_boundary = XByteArray_create();
    self->m_type = type >= XHttpMultiPart_MixedType && type <= XHttpMultiPart_AlternativeType
        ? type : XHttpMultiPart_MixedType;
    if (self->m_boundary)
        xhttp_multipart_make_boundary(self->m_boundary);
}

XHttpMultiPart* XHttpMultiPart_create(void)
{
    return XHttpMultiPart_create_type(XHttpMultiPart_MixedType);
}

XHttpMultiPart* XHttpMultiPart_create_type(XHttpMultiPart_ContentType type)
{
    XHttpMultiPart* self = (XHttpMultiPart*)XMalloc_System(sizeof(XHttpMultiPart));
    if (!self)
        return NULL;
    XHttpMultiPart_init(self, type);
    if (!self->m_parts || !self->m_boundary) {
        XHttpMultiPart_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

static void VXHttpMultiPart_deinit(XHttpMultiPart* self)
{
    if (!self)
        return;
    if (self->m_parts) {
        for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_parts); ++i) {
            XHttpPart** part = (XHttpPart**)XVector_at_base(self->m_parts, (int64_t)i);
            if (part && *part)
                XClass_delete_base((XClass*)*part);
        }
        XClass_delete_base((XClass*)self->m_parts);
        self->m_parts = NULL;
    }
    if (self->m_boundary) {
        XClass_delete_base((XClass*)self->m_boundary);
        self->m_boundary = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

bool XHttpMultiPart_append(XHttpMultiPart* self, const XHttpPart* part)
{
    XHttpPart* copy;
    if (!self || !self->m_parts || !part)
        return false;
    copy = XHttpPart_create_copy(part);
    if (!copy)
        return false;
    if (!XVector_push_back_1_base(self->m_parts, &copy)) {
        XClass_delete_base((XClass*)copy);
        return false;
    }
    return true;
}

bool XHttpMultiPart_setContentType(XHttpMultiPart* self, XHttpMultiPart_ContentType type)
{
    if (!self || type < XHttpMultiPart_MixedType || type > XHttpMultiPart_AlternativeType)
        return false;
    self->m_type = type;
    return true;
}

const XByteArray* XHttpMultiPart_boundary_const(const XHttpMultiPart* self)
{
    return self ? self->m_boundary : NULL;
}

bool XHttpMultiPart_setBoundary(XHttpMultiPart* self, const XByteArray* boundary)
{
    XByteArray* replacement;
    if (!self || !xhttp_multipart_valid_boundary(boundary))
        return false;
    replacement = XByteArray_create_copy(boundary);
    if (!replacement)
        return false;
    if (self->m_boundary) XClass_delete_base((XClass*)self->m_boundary);
    self->m_boundary = replacement;
    return true;
}

static bool xhttp_multipart_append_header_block(XByteArray* body, const XHttpHeaders* headers)
{
    if (!body || !headers)
        return false;
    for (size_t i = 0; i < XHttpHeaders_size(headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(headers, i);
        if (!name || !value ||
            !xhttp_append_bytes(body, XByteArray_constData((XByteArray*)name), XByteArray_size_base((XContainer*)name)) ||
            !XByteArray_append_utf8(body, ": ") ||
            !xhttp_append_bytes(body, XByteArray_constData((XByteArray*)value), XByteArray_size_base((XContainer*)value)) ||
            !XByteArray_append_utf8(body, "\r\n"))
            return false;
    }
    return XByteArray_append_utf8(body, "\r\n");
}

XByteArray* XHttpMultiPart_toByteArray(const XHttpMultiPart* self)
{
    XByteArray* body;
    const uint8_t* boundary;
    size_t boundarySize;
    if (!self || !self->m_boundary || !self->m_parts)
        return NULL;
    body = XByteArray_create();
    if (!body)
        return NULL;
    boundary = XByteArray_constData(self->m_boundary);
    boundarySize = XByteArray_size_base((XContainer*)self->m_boundary);
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_parts); ++i) {
        XHttpPart* const* partPtr = (XHttpPart* const*)XVector_at_base(self->m_parts, (int64_t)i);
        XHttpPart* part = partPtr ? *partPtr : NULL;
        XByteArray* deviceBody = NULL;
        const XByteArray* partBody;
        if (!part || !xhttp_append_bytes(body, "--", 2) ||
            !xhttp_append_bytes(body, boundary, boundarySize) ||
            !XByteArray_append_utf8(body, "\r\n") ||
            !xhttp_multipart_append_header_block(body, part->m_headers))
            goto failed;
        if (part->m_bodyDevice)
            deviceBody = XIODevice_readAll_3(part->m_bodyDevice);
        partBody = deviceBody ? deviceBody : part->m_body;
        if (partBody && !xhttp_append_bytes(body, XByteArray_constData((XByteArray*)partBody),
                                            XByteArray_size_base((XContainer*)partBody))) {
            if (deviceBody) XClass_delete_base((XClass*)deviceBody);
            goto failed;
        }
        if (deviceBody) XClass_delete_base((XClass*)deviceBody);
        if (!XByteArray_append_utf8(body, "\r\n"))
            goto failed;
    }
    if (!xhttp_append_bytes(body, "--", 2) ||
        !xhttp_append_bytes(body, boundary, boundarySize) ||
        !XByteArray_append_utf8(body, "--\r\n"))
        goto failed;
    return body;

failed:
    XClass_delete_base((XClass*)body);
    return NULL;
}

XByteArray* XHttpMultiPart_contentType(const XHttpMultiPart* self)
{
    static const char* types[] = { "mixed", "related", "form-data", "alternative" };
    XByteArray* result;
    if (!self || !self->m_boundary || self->m_type > XHttpMultiPart_AlternativeType)
        return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_append_utf8(result, "multipart/") ||
        !XByteArray_append_utf8(result, types[self->m_type]) ||
        !XByteArray_append_utf8(result, "; boundary=\"") ||
        !xhttp_append_bytes(result, XByteArray_constData(self->m_boundary),
                            XByteArray_size_base((XContainer*)self->m_boundary)) ||
        !XByteArray_append_utf8(result, "\"")) {
        if (result) XClass_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}
