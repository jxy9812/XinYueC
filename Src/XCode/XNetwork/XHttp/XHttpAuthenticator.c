/**
 * @file       XHttpAuthenticator.c
 * @brief      HTTP 认证凭据值对象实现。
 */

#include "XHttpAuthenticator.h"

#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

static void xhttp_authenticator_release(XHttpAuthenticator* self)
{
    if (!self)
        return;
    if (self->m_user) XClass_delete_base((XClass*)self->m_user);
    if (self->m_password) XClass_delete_base((XClass*)self->m_password);
    if (self->m_realm) XClass_delete_base((XClass*)self->m_realm);
    self->m_user = NULL;
    self->m_password = NULL;
    self->m_realm = NULL;
    self->m_method = XHttpAuthenticator_None;
}

static bool xhttp_authenticator_replace(XByteArray** target, const XByteArray* source)
{
    XByteArray* replacement;
    if (!target)
        return false;
    replacement = source ? XByteArray_create_copy(source) : NULL;
    if (source && !replacement)
        return false;
    if (*target)
        XClass_delete_base((XClass*)*target);
    *target = replacement;
    return true;
}

static void VXHttpAuthenticator_deinit(XHttpAuthenticator* self)
{
    xhttp_authenticator_release(self);
}

static void VXHttpAuthenticator_copy(XHttpAuthenticator* dest,
                                     const XHttpAuthenticator* src)
{
    XByteArray* user;
    XByteArray* password;
    XByteArray* realm;
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpAuthenticator_init(dest);
    user = src->m_user ? XByteArray_create_copy(src->m_user) : NULL;
    password = src->m_password ? XByteArray_create_copy(src->m_password) : NULL;
    realm = src->m_realm ? XByteArray_create_copy(src->m_realm) : NULL;
    if ((src->m_user && !user) || (src->m_password && !password) ||
        (src->m_realm && !realm)) {
        if (user) XClass_delete_base((XClass*)user);
        if (password) XClass_delete_base((XClass*)password);
        if (realm) XClass_delete_base((XClass*)realm);
        return;
    }
    xhttp_authenticator_release(dest);
    dest->m_user = user;
    dest->m_password = password;
    dest->m_realm = realm;
    dest->m_method = src->m_method;
}

static void VXHttpAuthenticator_move(XHttpAuthenticator* dest, XHttpAuthenticator* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpAuthenticator_init(dest);
    xhttp_authenticator_release(dest);
    dest->m_user = src->m_user;
    dest->m_password = src->m_password;
    dest->m_realm = src->m_realm;
    dest->m_method = src->m_method;
    src->m_user = NULL;
    src->m_password = NULL;
    src->m_realm = NULL;
    src->m_method = XHttpAuthenticator_None;
}

XVtable* XHttpAuthenticator_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHttpAuthenticator))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpAuthenticator_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttpAuthenticator_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttpAuthenticator_move);
    return XVTABLE_DEFAULT;
}

void XHttpAuthenticator_init(XHttpAuthenticator* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpAuthenticator);
    self->m_method = XHttpAuthenticator_None;
}

XHttpAuthenticator* XHttpAuthenticator_create(void)
{
    XHttpAuthenticator* self = (XHttpAuthenticator*)XMalloc_System(sizeof(*self));
    if (!self)
        return NULL;
    XHttpAuthenticator_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttpAuthenticator* XHttpAuthenticator_create_copy(const XHttpAuthenticator* other)
{
    XHttpAuthenticator* result;
    if (!other)
        return NULL;
    result = XHttpAuthenticator_create();
    if (result)
        XHttpAuthenticator_copy_base((XClass*)result, (const XClass*)other);
    return result;
}

XHttpAuthenticator* XHttpAuthenticator_create_move(XHttpAuthenticator* other)
{
    XHttpAuthenticator* result;
    if (!other)
        return NULL;
    result = XHttpAuthenticator_create();
    if (result)
        XHttpAuthenticator_move_base((XClass*)result, (XClass*)other);
    return result;
}

bool XHttpAuthenticator_setChallenge(XHttpAuthenticator* self,
                                     XHttpAuthenticator_Method method,
                                     const XByteArray* realm)
{
    XByteArray* replacement;
    if (!self || method < XHttpAuthenticator_None || method > XHttpAuthenticator_Negotiate)
        return false;
    replacement = realm ? XByteArray_create_copy(realm) : NULL;
    if (realm && !replacement)
        return false;
    if (self->m_realm)
        XClass_delete_base((XClass*)self->m_realm);
    self->m_realm = replacement;
    self->m_method = method;
    return true;
}

XHttpAuthenticator_Method XHttpAuthenticator_method(const XHttpAuthenticator* self)
{
    return self ? self->m_method : XHttpAuthenticator_None;
}

XByteArray* XHttpAuthenticator_realm(const XHttpAuthenticator* self)
{
    return self && self->m_realm ? XByteArray_create_copy(self->m_realm) : XByteArray_create();
}

const XByteArray* XHttpAuthenticator_realm_const(const XHttpAuthenticator* self)
{
    return self ? self->m_realm : NULL;
}

bool XHttpAuthenticator_setUser(XHttpAuthenticator* self, const XByteArray* user)
{
    return self && xhttp_authenticator_replace(&self->m_user, user);
}

bool XHttpAuthenticator_setUser_utf8(XHttpAuthenticator* self, const char* user)
{
    XByteArray* value = user ? XByteArray_create_utf8(user) : NULL;
    bool result = self && (!user || value) && XHttpAuthenticator_setUser(self, value);
    if (value) XClass_delete_base((XClass*)value);
    return result;
}

XByteArray* XHttpAuthenticator_user(const XHttpAuthenticator* self)
{
    return self && self->m_user ? XByteArray_create_copy(self->m_user) : XByteArray_create();
}

const XByteArray* XHttpAuthenticator_user_const(const XHttpAuthenticator* self)
{
    return self ? self->m_user : NULL;
}

bool XHttpAuthenticator_setPassword(XHttpAuthenticator* self, const XByteArray* password)
{
    return self && xhttp_authenticator_replace(&self->m_password, password);
}

bool XHttpAuthenticator_setPassword_utf8(XHttpAuthenticator* self, const char* password)
{
    XByteArray* value = password ? XByteArray_create_utf8(password) : NULL;
    bool result = self && (!password || value) && XHttpAuthenticator_setPassword(self, value);
    if (value) XClass_delete_base((XClass*)value);
    return result;
}

XByteArray* XHttpAuthenticator_password(const XHttpAuthenticator* self)
{
    return self && self->m_password ? XByteArray_create_copy(self->m_password) : XByteArray_create();
}

const XByteArray* XHttpAuthenticator_password_const(const XHttpAuthenticator* self)
{
    return self ? self->m_password : NULL;
}

bool XHttpAuthenticator_hasCredentials(const XHttpAuthenticator* self)
{
    return self && self->m_method != XHttpAuthenticator_None && self->m_user && self->m_password;
}
