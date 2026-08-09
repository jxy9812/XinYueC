/**
 * @file       XHttp2Frame.c
 * @brief      HTTP/2 帧编解码实现。
 */

#include "XHttp2Frame.h"

#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

static void xhttp2_frame_release_payload(XHttp2Frame* self)
{
    if (self && self->m_payload) {
        XClass_delete_base((XClass*)self->m_payload);
        self->m_payload = NULL;
    }
}

static void VXHttp2Frame_deinit(XHttp2Frame* self)
{
    if (!self)
        return;
    xhttp2_frame_release_payload(self);
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttp2Frame_copy(XHttp2Frame* dest, const XHttp2Frame* src)
{
    XByteArray* payload;
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp2Frame_init(dest);
    payload = src->m_payload ? XByteArray_create_copy(src->m_payload) : XByteArray_create();
    if (!payload)
        return;
    xhttp2_frame_release_payload(dest);
    dest->m_payload = payload;
    dest->m_streamId = src->m_streamId;
    dest->m_type = src->m_type;
    dest->m_flags = src->m_flags;
}

static void VXHttp2Frame_move(XHttp2Frame* dest, XHttp2Frame* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp2Frame_init(dest);
    xhttp2_frame_release_payload(dest);
    dest->m_payload = src->m_payload;
    dest->m_streamId = src->m_streamId;
    dest->m_type = src->m_type;
    dest->m_flags = src->m_flags;
    src->m_payload = NULL;
    src->m_streamId = 0;
    src->m_type = 0;
    src->m_flags = 0;
}

XVtable* XHttp2Frame_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttp2Frame)
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp2Frame_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttp2Frame_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttp2Frame_move);
    XCLASS_SHOW_SIZE_DEFAULT(XHttp2Frame);
    return XVTABLE_DEFAULT;
}

void XHttp2Frame_init(XHttp2Frame* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp2Frame);
    self->m_payload = XByteArray_create();
}

XHttp2Frame* XHttp2Frame_create(void)
{
    XHttp2Frame* self = (XHttp2Frame*)XMalloc_System(sizeof(XHttp2Frame));
    if (!self)
        return NULL;
    XHttp2Frame_init(self);
    if (!self->m_payload) {
        XHttp2Frame_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttp2Frame* XHttp2Frame_create_ex(uint8_t type, uint8_t flags, uint32_t streamId,
                                   const XByteArray* payload)
{
    XHttp2Frame* self = XHttp2Frame_create();
    if (!self || !XHttp2Frame_setStreamId(self, streamId) || !XHttp2Frame_setPayload(self, payload)) {
        if (self) XClass_delete_base((XClass*)self);
        return NULL;
    }
    self->m_type = type;
    self->m_flags = flags;
    return self;
}

bool XHttp2Frame_setPayload(XHttp2Frame* self, const XByteArray* payload)
{
    XByteArray* replacement;
    size_t size = payload ? XContainer_size_base((const XContainer*)payload) : 0;
    if (!self || size > UINT32_C(0xFFFFFF))
        return false;
    replacement = payload ? XByteArray_create_copy(payload) : XByteArray_create();
    if (!replacement)
        return false;
    xhttp2_frame_release_payload(self);
    self->m_payload = replacement;
    return true;
}

const XByteArray* XHttp2Frame_payload_const(const XHttp2Frame* self)
{
    return self ? self->m_payload : NULL;
}

void XHttp2Frame_setType(XHttp2Frame* self, uint8_t type)
{
    if (self) self->m_type = type;
}

uint8_t XHttp2Frame_type(const XHttp2Frame* self)
{
    return self ? self->m_type : 0;
}

void XHttp2Frame_setFlags(XHttp2Frame* self, uint8_t flags)
{
    if (self) self->m_flags = flags;
}

uint8_t XHttp2Frame_flags(const XHttp2Frame* self)
{
    return self ? self->m_flags : 0;
}

bool XHttp2Frame_setStreamId(XHttp2Frame* self, uint32_t streamId)
{
    if (!self || (streamId & UINT32_C(0x80000000)) != 0)
        return false;
    self->m_streamId = streamId;
    return true;
}

uint32_t XHttp2Frame_streamId(const XHttp2Frame* self)
{
    return self ? self->m_streamId : 0;
}

bool XHttp2Frame_validateHeader(const XHttp2Frame* self)
{
    size_t size;
    if (!self || !self->m_payload || (self->m_streamId & UINT32_C(0x80000000)) != 0)
        return false;
    size = XContainer_size_base((const XContainer*)self->m_payload);
    switch (self->m_type) {
    case XHttp2Frame_Settings:
        /* SETTINGS ACK 不携带载荷；普通 SETTINGS 由六字节项组成。 */
        if ((self->m_flags & XHttp2Frame_Ack) ? size != 0 : (size % 6) != 0)
            return false;
        break;
    case XHttp2Frame_Priority:
        if (size != 5)
            return false;
        break;
    case XHttp2Frame_Ping:
        if (size != 8)
            return false;
        break;
    case XHttp2Frame_GoAway:
        if (size < 8)
            return false;
        break;
    case XHttp2Frame_RstStream:
    case XHttp2Frame_WindowUpdate:
        if (size != 4)
            return false;
        break;
    case XHttp2Frame_PushPromise:
        if (size < 4)
            return false;
        break;
    default:
        /* DATA、HEADERS、CONTINUATION 和未知帧在载荷阶段继续校验。 */
        break;
    }
    return true;
}

bool XHttp2Frame_validatePayload(const XHttp2Frame* self)
{
    const uint8_t* bytes;
    size_t size;
    size_t offset = 0;
    size_t padding = 0;
    if (!XHttp2Frame_validateHeader(self))
        return false;
    if (self->m_type != XHttp2Frame_Data && self->m_type != XHttp2Frame_Headers &&
        self->m_type != XHttp2Frame_PushPromise)
        return true;
    size = XContainer_size_base((const XContainer*)self->m_payload);
    bytes = XByteArray_constData(self->m_payload);
    if (self->m_flags & XHttp2Frame_Padded) {
        if (size == 0 || !bytes)
            return false;
        padding = bytes[0];
        offset = 1;
        if (padding > size - offset)
            return false;
    }
    size -= offset + padding;
    if (self->m_type == XHttp2Frame_Headers &&
        (self->m_flags & XHttp2Frame_PriorityFlag) && size < 5)
        return false;
    if (self->m_type == XHttp2Frame_PushPromise && size < 4)
        return false;
    return true;
}

XByteArray* XHttp2Frame_toByteArray(const XHttp2Frame* self)
{
    XByteArray* result;
    size_t length;
    uint8_t header[9];
    if (!self || !self->m_payload || !XHttp2Frame_validatePayload(self))
        return NULL;
    length = XContainer_size_base((const XContainer*)self->m_payload);
    if (length > UINT32_C(0xFFFFFF) || (self->m_streamId & UINT32_C(0x80000000)) != 0)
        return NULL;
    header[0] = (uint8_t)((length >> 16) & 0xff);
    header[1] = (uint8_t)((length >> 8) & 0xff);
    header[2] = (uint8_t)(length & 0xff);
    header[3] = self->m_type;
    header[4] = self->m_flags;
    header[5] = (uint8_t)((self->m_streamId >> 24) & 0x7f);
    header[6] = (uint8_t)((self->m_streamId >> 16) & 0xff);
    header[7] = (uint8_t)((self->m_streamId >> 8) & 0xff);
    header[8] = (uint8_t)(self->m_streamId & 0xff);
    result = XByteArray_create();
    if (!result || !XByteArray_push_back_2((XVector*)result, header, sizeof(header)) ||
        (length != 0 && !XByteArray_push_back_2((XVector*)result,
                                                 XByteArray_constData(self->m_payload), length))) {
        if (result) XClass_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

XHttp2Frame* XHttp2Frame_fromBytes(const void* data, size_t size, size_t* consumed)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t length;
    XByteArray* payload;
    XHttp2Frame* frame;
    if (consumed) *consumed = 0;
    if (!bytes || size < 9)
        return NULL;
    length = ((size_t)bytes[0] << 16) | ((size_t)bytes[1] << 8) | bytes[2];
    if (size < 9 + length)
        return NULL;
    payload = XByteArray_create_with_data((const char*)(bytes + 9), length);
    if (!payload)
        return NULL;
    frame = XHttp2Frame_create_ex(bytes[3], bytes[4],
                                  ((uint32_t)(bytes[5] & 0x7f) << 24) |
                                  ((uint32_t)bytes[6] << 16) |
                                  ((uint32_t)bytes[7] << 8) | bytes[8], payload);
    XClass_delete_base((XClass*)payload);
    if (frame && !XHttp2Frame_validatePayload(frame)) {
        XClass_delete_base((XClass*)frame);
        frame = NULL;
    }
    if (frame && consumed)
        *consumed = 9 + length;
    return frame;
}

bool XHttp2Frame_hasClientPreface(const void* data, size_t size)
{
    static const char preface[] = XHttp2Frame_ClientPreface;
    return data && size >= sizeof(preface) - 1 &&
           memcmp(data, preface, sizeof(preface) - 1) == 0;
}
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
