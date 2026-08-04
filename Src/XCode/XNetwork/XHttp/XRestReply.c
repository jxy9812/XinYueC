/**
 * @file       XRestReply.c
 * @brief      REST 响应包装实现。
 */

#include "XRestReply.h"

#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void VXRestReply_deinit(XRestReply* self)
{
    if (!self) return;
    self->m_reply = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXRestReply_copy(XRestReply* dest, const XRestReply* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XRestReply_init(dest, src->m_reply);
    dest->m_reply = src->m_reply;
}

XVtable* XRestReply_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XRestReply)
	XCLASS_SET_CLASS_NAME_DEFAULT("XRestReply");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRestReply_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXRestReply_copy);
    XCLASS_SHOW_SIZE_DEFAULT(XRestReply);
    return XVTABLE_DEFAULT;
}

void XRestReply_init(XRestReply* self, XHttpReply* reply)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XRestReply);
    self->m_reply = reply;
}

XRestReply* XRestReply_create(XHttpReply* reply)
{
    XRestReply* self = (XRestReply*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XRestReply_init(self, reply);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XRestReply* XRestReply_create_copy(const XRestReply* other)
{
    XRestReply* self;
    if (!other) return NULL;
    self = XRestReply_create(other->m_reply);
    return self;
}

XHttpReply* XRestReply_networkReply(const XRestReply* self)
{ return self ? self->m_reply : NULL; }

XJsonDocument* XRestReply_readJson(XRestReply* self, XString** errorText)
{
    XByteArray* body;
    XJsonDocument* document;
    if (errorText) *errorText = NULL;
    if (!self || !self->m_reply) return NULL;
    body = XHttpReply_readAll(self->m_reply);
    /* XJsonDocument_fromJson 的旧接口以最后一个 NUL 作为解析边界；网络 body 不保证带 NUL。 */
    if (body && (XByteArray_size_base(body) == 0 ||
                 XByteArray_constData(body)[XByteArray_size_base(body) - 1] != 0))
        XByteArray_push_back_1(body, 0);
    document = body ? XJsonDocument_fromJson(body) : NULL;
    if (!document && errorText)
        *errorText = XString_create_utf8("REST 响应不是合法 JSON");
    if (body) XClass_delete_base((XClass*)body);
    return document;
}

XByteArray* XRestReply_readBody(XRestReply* self)
{ return self && self->m_reply ? XHttpReply_readAll(self->m_reply) : XByteArray_create(); }

XString* XRestReply_readText(XRestReply* self)
{
    XByteArray* body = XRestReply_readBody(self);
    XString* text = body ? XString_create_with_length_utf8((const char*)XByteArray_constData(body),
                                                            XByteArray_size_base(body)) : NULL;
    if (body) XClass_delete_base((XClass*)body);
    return text;
}

int XRestReply_httpStatus(const XRestReply* self)
{ return self && self->m_reply ? XHttpReply_statusCode(self->m_reply) : 0; }

bool XRestReply_isHttpStatusSuccess(const XRestReply* self)
{
    int status = XRestReply_httpStatus(self);
    return status >= 200 && status <= 299;
}

XHttpReply_NetworkError XRestReply_error(const XRestReply* self)
{ return self && self->m_reply ? XHttpReply_error(self->m_reply) : XHttpReply_UnknownNetworkError; }

bool XRestReply_hasError(const XRestReply* self)
{ return XRestReply_error(self) != XHttpReply_NoError; }

bool XRestReply_isSuccess(const XRestReply* self)
{ return self && !XRestReply_hasError(self) && XRestReply_isHttpStatusSuccess(self); }

XString* XRestReply_errorString(const XRestReply* self)
{ return self && self->m_reply ? XHttpReply_errorString(self->m_reply) : XString_create_utf8("无效 REST 响应"); }
