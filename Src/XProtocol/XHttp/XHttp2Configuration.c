/**
 * @file       XHttp2Configuration.c
 * @brief      HTTP/2 配置值对象实现。
 */

#include "XHttp2Configuration.h"

#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

static void VXHttp2Configuration_deinit(XHttp2Configuration* self)
{
    if (self)
        XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttp2Configuration_copy(XHttp2Configuration* dest, const XHttp2Configuration* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp2Configuration_init(dest);
    dest->m_serverPushEnabled = src->m_serverPushEnabled;
    dest->m_huffmanCompressionEnabled = src->m_huffmanCompressionEnabled;
    dest->m_sessionReceiveWindowSize = src->m_sessionReceiveWindowSize;
    dest->m_streamReceiveWindowSize = src->m_streamReceiveWindowSize;
    dest->m_maxFrameSize = src->m_maxFrameSize;
}

static void VXHttp2Configuration_move(XHttp2Configuration* dest, XHttp2Configuration* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp2Configuration_init(dest);
    dest->m_serverPushEnabled = src->m_serverPushEnabled;
    dest->m_huffmanCompressionEnabled = src->m_huffmanCompressionEnabled;
    dest->m_sessionReceiveWindowSize = src->m_sessionReceiveWindowSize;
    dest->m_streamReceiveWindowSize = src->m_streamReceiveWindowSize;
    dest->m_maxFrameSize = src->m_maxFrameSize;
    /* 保留源对象的 XClass 元数据，保证堆对象移动后仍可正常释放。 */
    src->m_serverPushEnabled = false;
    src->m_huffmanCompressionEnabled = true;
    src->m_sessionReceiveWindowSize = 65535;
    src->m_streamReceiveWindowSize = 65535;
    src->m_maxFrameSize = XHttp2Configuration_MinFrameSize;
}

XVtable* XHttp2Configuration_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttp2Configuration)
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp2Configuration_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttp2Configuration_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttp2Configuration_move);
    XCLASS_SHOW_SIZE_DEFAULT(XHttp2Configuration);
    return XVTABLE_DEFAULT;
}

void XHttp2Configuration_init(XHttp2Configuration* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp2Configuration);
    self->m_huffmanCompressionEnabled = true;
    self->m_sessionReceiveWindowSize = 65535;
    self->m_streamReceiveWindowSize = 65535;
    self->m_maxFrameSize = XHttp2Configuration_MinFrameSize;
}

XHttp2Configuration* XHttp2Configuration_create(void)
{
    XHttp2Configuration* self = (XHttp2Configuration*)XMalloc_System(sizeof(XHttp2Configuration));
    if (!self)
        return NULL;
    XHttp2Configuration_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttp2Configuration* XHttp2Configuration_create_copy(const XHttp2Configuration* other)
{
    XHttp2Configuration* self;
    if (!other)
        return NULL;
    self = XHttp2Configuration_create();
    if (self) XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XHttp2Configuration* XHttp2Configuration_create_move(XHttp2Configuration* other)
{
    XHttp2Configuration* self;
    if (!other)
        return NULL;
    self = XHttp2Configuration_create();
    if (self) XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

void XHttp2Configuration_setServerPushEnabled(XHttp2Configuration* self, bool enabled)
{
    if (self) self->m_serverPushEnabled = enabled;
}

bool XHttp2Configuration_serverPushEnabled(const XHttp2Configuration* self)
{
    return self ? self->m_serverPushEnabled : false;
}

void XHttp2Configuration_setHuffmanCompressionEnabled(XHttp2Configuration* self, bool enabled)
{
    if (self) self->m_huffmanCompressionEnabled = enabled;
}

bool XHttp2Configuration_huffmanCompressionEnabled(const XHttp2Configuration* self)
{
    return self ? self->m_huffmanCompressionEnabled : false;
}

bool XHttp2Configuration_setSessionReceiveWindowSize(XHttp2Configuration* self, uint32_t size)
{
    if (!self || size == 0 || size > XHttp2Configuration_MaxWindowSize)
        return false;
    self->m_sessionReceiveWindowSize = size;
    return true;
}

uint32_t XHttp2Configuration_sessionReceiveWindowSize(const XHttp2Configuration* self)
{
    return self ? self->m_sessionReceiveWindowSize : 65535;
}

bool XHttp2Configuration_setStreamReceiveWindowSize(XHttp2Configuration* self, uint32_t size)
{
    if (!self || size == 0 || size > XHttp2Configuration_MaxWindowSize)
        return false;
    self->m_streamReceiveWindowSize = size;
    return true;
}

uint32_t XHttp2Configuration_streamReceiveWindowSize(const XHttp2Configuration* self)
{
    return self ? self->m_streamReceiveWindowSize : 65535;
}

bool XHttp2Configuration_setMaxFrameSize(XHttp2Configuration* self, uint32_t size)
{
    if (!self || size < XHttp2Configuration_MinFrameSize || size > XHttp2Configuration_MaxFrameSize)
        return false;
    self->m_maxFrameSize = size;
    return true;
}

uint32_t XHttp2Configuration_maxFrameSize(const XHttp2Configuration* self)
{
    return self ? self->m_maxFrameSize : XHttp2Configuration_MinFrameSize;
}
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
