/**
 * @file       XHstsPolicy.c
 * @brief      HSTS 策略对象实现。
 */

#include "XHstsPolicy.h"

#include "XDateTime.h"
#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

static void xhsts_release_host(XHstsPolicy* self)
{
    if (self && self->m_host) {
        XClass_delete_base((XClass*)self->m_host);
        self->m_host = NULL;
    }
}

static void VXHstsPolicy_deinit(XHstsPolicy* self)
{
    if (!self)
        return;
    xhsts_release_host(self);
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHstsPolicy_copy(XHstsPolicy* dest, const XHstsPolicy* src)
{
    XByteArray* host;
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHstsPolicy_init(dest);
    host = src->m_host ? XByteArray_create_copy(src->m_host) : XByteArray_create();
    if (!host)
        return;
    xhsts_release_host(dest);
    dest->m_host = host;
    dest->m_expiryMSecs = src->m_expiryMSecs;
    dest->m_flags = src->m_flags;
}

static void VXHstsPolicy_move(XHstsPolicy* dest, XHstsPolicy* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHstsPolicy_init(dest);
    xhsts_release_host(dest);
    dest->m_host = src->m_host;
    dest->m_expiryMSecs = src->m_expiryMSecs;
    dest->m_flags = src->m_flags;
    src->m_host = XByteArray_create();
    src->m_expiryMSecs = -1;
    src->m_flags = 0;
}

XVtable* XHstsPolicy_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHstsPolicy)
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHstsPolicy_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHstsPolicy_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHstsPolicy_move);
    XCLASS_SHOW_SIZE_DEFAULT(XHstsPolicy);
    return XVTABLE_DEFAULT;
}

void XHstsPolicy_init(XHstsPolicy* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHstsPolicy);
    self->m_host = XByteArray_create();
    self->m_expiryMSecs = -1;
}

XHstsPolicy* XHstsPolicy_create_ex(XMemoryType memory, const XByteArray* host,
                                   int64_t expiryMSecs, uint32_t flags)
{
    XHstsPolicy* self = (XHstsPolicy*)XMemory_malloc(sizeof(XHstsPolicy), memory);
    if (!self)
        return NULL;
    XHstsPolicy_init(self);
    if (!self->m_host || !XHstsPolicy_setHost(self, host) ||
        (flags & ~XHstsPolicy_IncludeSubDomains) != 0) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    self->m_expiryMSecs = expiryMSecs;
    self->m_flags = flags;
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

XHstsPolicy* XHstsPolicy_create_copy(const XHstsPolicy* other)
{
    XHstsPolicy* self;
    if (!other)
        return NULL;
    self = XHstsPolicy_create();
    if (self)
        XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XHstsPolicy* XHstsPolicy_create_move(XHstsPolicy* other)
{
    XHstsPolicy* self;
    if (!other)
        return NULL;
    self = XHstsPolicy_create();
    if (self)
        XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

bool XHstsPolicy_setHost(XHstsPolicy* self, const XByteArray* host)
{
    XByteArray* replacement;
    size_t i;
    if (!self)
        return false;
    if (host) {
        for (i = 0; i < XByteArray_size_base(host); ++i) {
            if (XByteArray_constData((XByteArray*)host)[i] == 0)
                return false;
        }
    }
    replacement = host ? XByteArray_create_copy(host) : XByteArray_create();
    if (!replacement)
        return false;
    XByteArray_toLower(replacement);
    xhsts_release_host(self);
    self->m_host = replacement;
    return true;
}

const XByteArray* XHstsPolicy_host_const(const XHstsPolicy* self)
{
    return self ? self->m_host : NULL;
}

void XHstsPolicy_setExpiryMSecs(XHstsPolicy* self, int64_t expiryMSecs)
{
    if (self)
        self->m_expiryMSecs = expiryMSecs;
}

int64_t XHstsPolicy_expiryMSecs(const XHstsPolicy* self)
{
    return self ? self->m_expiryMSecs : -1;
}

void XHstsPolicy_setIncludesSubDomains(XHstsPolicy* self, bool include)
{
    if (self) {
        if (include)
            self->m_flags |= XHstsPolicy_IncludeSubDomains;
        else
            self->m_flags &= ~XHstsPolicy_IncludeSubDomains;
    }
}

bool XHstsPolicy_includesSubDomains(const XHstsPolicy* self)
{
    return self && (self->m_flags & XHstsPolicy_IncludeSubDomains) != 0;
}

bool XHstsPolicy_isExpired(const XHstsPolicy* self)
{
    XDateTime now;
    if (!self || !self->m_host || XContainer_size_base((const XContainer*)self->m_host) == 0 ||
        self->m_expiryMSecs < 0)
        return true;
    now = XDateTime_currentDateTimeUtc();
    return self->m_expiryMSecs <= XDateTime_toMSecsSinceEpoch(&now);
}

bool XHstsPolicy_equals(const XHstsPolicy* lhs, const XHstsPolicy* rhs)
{
    size_t size;
    if (!lhs || !rhs)
        return false;
    if (lhs == rhs)
        return true;
    if (lhs->m_expiryMSecs != rhs->m_expiryMSecs || lhs->m_flags != rhs->m_flags)
        return false;
    size = lhs->m_host ? XContainer_size_base((const XContainer*)lhs->m_host) : 0;
    if (size != (rhs->m_host ? XContainer_size_base((const XContainer*)rhs->m_host) : 0))
        return false;
    return size == 0 || memcmp(XByteArray_constData(lhs->m_host),
                               XByteArray_constData(rhs->m_host), size) == 0;
}

void XHstsPolicy_swap(XHstsPolicy* lhs, XHstsPolicy* rhs)
{
    XByteArray* host;
    int64_t expiry;
    uint32_t flags;
    if (!lhs || !rhs || lhs == rhs)
        return;
    host = lhs->m_host;
    lhs->m_host = rhs->m_host;
    rhs->m_host = host;
    expiry = lhs->m_expiryMSecs;
    lhs->m_expiryMSecs = rhs->m_expiryMSecs;
    rhs->m_expiryMSecs = expiry;
    flags = lhs->m_flags;
    lhs->m_flags = rhs->m_flags;
    rhs->m_flags = flags;
}

void XHstsPolicy_list_free(XVector* policies)
{
    if (!policies)
        return;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)policies); ++i) {
        XHstsPolicy** policy = (XHstsPolicy**)XVector_at_base(policies, (int64_t)i);
        if (policy && *policy)
            XClass_delete_base((XClass*)*policy);
    }
    XClass_delete_base((XClass*)policies);
}
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
