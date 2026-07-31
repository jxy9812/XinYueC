/**
 * @file       XHttp1Configuration.c
 * @brief      HTTP/1 配置值对象实现。
 */

#include "XHttp1Configuration.h"

#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void VXHttp1Configuration_deinit(XHttp1Configuration* self)
{
    if (self)
        XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttp1Configuration_copy(XHttp1Configuration* dest, const XHttp1Configuration* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp1Configuration_init(dest);
    dest->m_numberOfConnectionsPerHost = src->m_numberOfConnectionsPerHost;
}

static void VXHttp1Configuration_move(XHttp1Configuration* dest, XHttp1Configuration* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XHttp1Configuration_init(dest);
    dest->m_numberOfConnectionsPerHost = src->m_numberOfConnectionsPerHost;
    src->m_numberOfConnectionsPerHost = 6;
}

XVtable* XHttp1Configuration_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    //虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHttp1Configuration))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp1Configuration_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttp1Configuration_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttp1Configuration_move);
#if SHOWCONTAINERSIZE
    printf("XHttp1Configuration size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

void XHttp1Configuration_init(XHttp1Configuration* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp1Configuration);
    self->m_numberOfConnectionsPerHost = 6;
}

XHttp1Configuration* XHttp1Configuration_create(void)
{
    XHttp1Configuration* self = (XHttp1Configuration*)XMalloc_System(sizeof(XHttp1Configuration));
    if (!self)
        return NULL;
    XHttp1Configuration_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttp1Configuration* XHttp1Configuration_create_copy(const XHttp1Configuration* other)
{
    XHttp1Configuration* self;
    if (!other)
        return NULL;
    self = XHttp1Configuration_create();
    if (self) XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XHttp1Configuration* XHttp1Configuration_create_move(XHttp1Configuration* other)
{
    XHttp1Configuration* self;
    if (!other)
        return NULL;
    self = XHttp1Configuration_create();
    if (self) XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

bool XHttp1Configuration_setNumberOfConnectionsPerHost(XHttp1Configuration* self, size_t amount)
{
    if (!self)
        return false;
    if (amount == 0)
        return true;
    self->m_numberOfConnectionsPerHost = amount > 255 ? 255 : amount;
    return true;
}

size_t XHttp1Configuration_numberOfConnectionsPerHost(const XHttp1Configuration* self)
{
    return self ? self->m_numberOfConnectionsPerHost : 6;
}
