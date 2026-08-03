/**
 * @file       XNetworkRequestFactory.c
 * @brief      HTTP 请求工厂实现。
 */

#include "XNetworkRequestFactory.h"

#include "XBase64.h"
#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void xrequest_factory_release(XNetworkRequestFactory* self);

static size_t xrequest_factory_bytes_size(const XByteArray* value)
{ return value ? XContainer_size_base((const XContainer*)value) : 0; }

static const void* xrequest_factory_bytes_data(const XByteArray* value)
{ return value ? XByteArray_constData((XByteArray*)value) : NULL; }

static XByteArray* xrequest_factory_copy_bytes(const XByteArray* source)
{
    return source ? XByteArray_create_copy(source) : XByteArray_create();
}

static bool xrequest_factory_replace_bytes(XByteArray** target, const XByteArray* source)
{
    XByteArray* replacement = xrequest_factory_copy_bytes(source);
    if (!replacement)
        return false;
    if (*target)
        XClass_delete_base((XClass*)*target);
    *target = replacement;
    return true;
}

static void xrequest_factory_release_attributes(XNetworkRequestFactory* self)
{
    if (!self || !self->m_attributes)
        return;
    for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
        XNetworkRequestFactory_AttributeItem* item =
            (XNetworkRequestFactory_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
        if (item && item->m_value)
            XClass_delete_base((XClass*)item->m_value);
    }
    XContainer_clear_base((XContainer*)self->m_attributes);
}

static XNetworkRequestFactory_AttributeItem* xrequest_factory_find_attribute(
    const XNetworkRequestFactory* self, int code)
{
    if (!self || !self->m_attributes)
        return NULL;
    for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
        XNetworkRequestFactory_AttributeItem* item =
            (XNetworkRequestFactory_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
        if (item && item->m_code == code)
            return item;
    }
    return NULL;
}

static bool xrequest_factory_apply_headers(XHttpRequest* request, const XHttpHeaders* headers)
{
    if (!request || !headers)
        return true;
    for (size_t i = 0; i < XHttpHeaders_size(headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(headers, i);
        if (!name || !value || !XHttpHeaders_replaceOrAppend(XHttpRequest_headers(request), name, value))
            return false;
    }
    return true;
}

static bool xrequest_factory_has_header(const XHttpRequest* request, const char* name)
{
    XByteArray* key = name ? XByteArray_create_utf8(name) : NULL;
    bool result = key && XHttpHeaders_contains(XHttpRequest_headers_const(request), key);
    if (key)
        XClass_delete_base((XClass*)key);
    return result;
}

static bool xrequest_factory_apply_auth(XNetworkRequestFactory* self, XHttpRequest* request)
{
    XByteArray* value = NULL;
    XByteArray* credentials = NULL;
    XByteArray* encoded = NULL;
    if (!self || !request || xrequest_factory_has_header(request, "Authorization"))
        return true;
    if (self->m_bearerToken && xrequest_factory_bytes_size(self->m_bearerToken) != 0) {
        value = XByteArray_create_utf8("Bearer ");
        if (!value || !XByteArray_push_back_2((XVector*)value, xrequest_factory_bytes_data(self->m_bearerToken),
                                               xrequest_factory_bytes_size(self->m_bearerToken)))
            goto failed;
    } else if (self->m_userName || self->m_password) {
        credentials = XByteArray_create();
        if (!credentials || (self->m_userName &&
            !XByteArray_push_back_2((XVector*)credentials, xrequest_factory_bytes_data(self->m_userName),
                                     xrequest_factory_bytes_size(self->m_userName))) ||
            !XByteArray_append_utf8(credentials, ":") ||
            (self->m_password &&
             !XByteArray_push_back_2((XVector*)credentials, xrequest_factory_bytes_data(self->m_password),
                                      xrequest_factory_bytes_size(self->m_password))))
            goto failed;
        encoded = XByteArray_toBase64(credentials);
        value = encoded ? XByteArray_create_utf8("Basic ") : NULL;
        if (!value || !XByteArray_push_back_2((XVector*)value, xrequest_factory_bytes_data(encoded),
                                               xrequest_factory_bytes_size(encoded)))
            goto failed;
    }
    if (value && !XHttpHeaders_replaceOrAppendKnown(XHttpRequest_headers(request),
                                                     XHttpHeaders_WellKnownHeader_Authorization, value))
        goto failed;
    if (value) XClass_delete_base((XClass*)value);
    if (credentials) XClass_delete_base((XClass*)credentials);
    if (encoded) XClass_delete_base((XClass*)encoded);
    return true;
failed:
    if (value) XClass_delete_base((XClass*)value);
    if (credentials) XClass_delete_base((XClass*)credentials);
    if (encoded) XClass_delete_base((XClass*)encoded);
    return false;
}

static bool xrequest_factory_apply_query(XNetworkRequestFactory* self, XHttpRequest* request,
                                         const XByteArray* query)
{
    XString* text = NULL;
    XUrl* url = NULL;
    bool result = false;
    const XByteArray* effective = query ? query : (self ? self->m_queryParameters : NULL);
    if (!request || !effective || xrequest_factory_bytes_size(effective) == 0)
        return true;
    url = XUrl_create_copy(XHttpRequest_url_const(request));
    text = XString_create_with_length_utf8((const char*)xrequest_factory_bytes_data(effective),
                                           xrequest_factory_bytes_size(effective));
    if (url && text) {
        XUrl_setQuery(url, text);
        result = XHttpRequest_setUrl(request, url);
    }
    if (url) XClass_delete_base((XClass*)url);
    if (text) XClass_delete_base((XClass*)text);
    return result;
}

static XHttpRequest* xrequest_factory_create_internal(const XNetworkRequestFactory* self,
                                                       const char* path,
                                                       const XByteArray* query)
{
    XNetworkRequestFactory* mutableSelf = (XNetworkRequestFactory*)self;
    XHttpRequest* request = NULL;
    XUrl* resolved = NULL;
    XString* relative = NULL;
    if (!self)
        return NULL;
    if (path && self->m_baseUrl) {
        relative = XString_create_utf8(path);
        resolved = XUrl_create();
        if (!relative || !resolved) goto failed;
        XUrl_resolved(self->m_baseUrl, relative, resolved);
        request = XHttpRequest_create_url(resolved);
    } else if (path) {
        XString* absolute = XString_create_utf8(path);
        resolved = absolute ? XUrl_create_ex(absolute, XUrl_TolerantMode) : NULL;
        if (absolute) XClass_delete_base((XClass*)absolute);
        request = resolved ? XHttpRequest_create_url(resolved) : NULL;
    } else if (self->m_baseUrl) {
        request = XHttpRequest_create_url(self->m_baseUrl);
    } else {
        request = XHttpRequest_create();
    }
    if (!request || !xrequest_factory_apply_headers(request, self->m_commonHeaders) ||
        !xrequest_factory_apply_auth(mutableSelf, request) ||
        !xrequest_factory_apply_query(mutableSelf, request, query))
        goto failed;
    XHttpRequest_setTransferTimeout(request, self->m_transferTimeout);
    XHttpRequest_setPriority(request, self->m_priority);
    for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
        XNetworkRequestFactory_AttributeItem* item =
            (XNetworkRequestFactory_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
        if (item && item->m_value)
            XHttpRequest_setAttribute(request, item->m_code, item->m_value);
    }
    if (resolved) XClass_delete_base((XClass*)resolved);
    if (relative) XClass_delete_base((XClass*)relative);
    return request;
failed:
    if (request) XClass_delete_base((XClass*)request);
    if (resolved) XClass_delete_base((XClass*)resolved);
    if (relative) XClass_delete_base((XClass*)relative);
    return NULL;
}

static void xrequest_factory_release(XNetworkRequestFactory* self)
{
    if (!self) return;
    if (self->m_baseUrl) XClass_delete_base((XClass*)self->m_baseUrl);
    if (self->m_commonHeaders) XClass_delete_base((XClass*)self->m_commonHeaders);
    if (self->m_bearerToken) XClass_delete_base((XClass*)self->m_bearerToken);
    if (self->m_userName) XClass_delete_base((XClass*)self->m_userName);
    if (self->m_password) XClass_delete_base((XClass*)self->m_password);
    if (self->m_queryParameters) XClass_delete_base((XClass*)self->m_queryParameters);
    xrequest_factory_release_attributes(self);
    if (self->m_attributes) XClass_delete_base((XClass*)self->m_attributes);
    self->m_baseUrl = NULL; self->m_commonHeaders = NULL; self->m_bearerToken = NULL;
    self->m_userName = NULL; self->m_password = NULL; self->m_queryParameters = NULL;
    self->m_attributes = NULL;
}

static void VXNetworkRequestFactory_deinit(XNetworkRequestFactory* self)
{
    if (!self) return;
    xrequest_factory_release(self);
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXNetworkRequestFactory_copy(XNetworkRequestFactory* dest,
                                         const XNetworkRequestFactory* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XNetworkRequestFactory_init(dest);
    XNetworkRequestFactory* copy = XNetworkRequestFactory_create();
    if (!copy) return;
    if (!XNetworkRequestFactory_setBaseUrl(copy, src->m_baseUrl) ||
        !XNetworkRequestFactory_setCommonHeaders(copy, src->m_commonHeaders) ||
        !XNetworkRequestFactory_setBearerToken(copy, src->m_bearerToken) ||
        !XNetworkRequestFactory_setUserName(copy, src->m_userName) ||
        !XNetworkRequestFactory_setPassword(copy, src->m_password) ||
        !XNetworkRequestFactory_setQueryParameters(copy, src->m_queryParameters)) {
        XClass_delete_base((XClass*)copy); return;
    }
    copy->m_transferTimeout = src->m_transferTimeout;
    copy->m_priority = src->m_priority;
    for (size_t i = 0; i < XVector_size_base(src->m_attributes); ++i) {
        XNetworkRequestFactory_AttributeItem* item =
            (XNetworkRequestFactory_AttributeItem*)XVector_at_base(src->m_attributes, (int64_t)i);
        if (!item || !XNetworkRequestFactory_setAttribute(copy, item->m_code, item->m_value)) {
            XClass_delete_base((XClass*)copy); return;
        }
    }
    xrequest_factory_release(dest);
    dest->m_baseUrl = copy->m_baseUrl; copy->m_baseUrl = NULL;
    dest->m_commonHeaders = copy->m_commonHeaders; copy->m_commonHeaders = NULL;
    dest->m_bearerToken = copy->m_bearerToken; copy->m_bearerToken = NULL;
    dest->m_userName = copy->m_userName; copy->m_userName = NULL;
    dest->m_password = copy->m_password; copy->m_password = NULL;
    dest->m_queryParameters = copy->m_queryParameters; copy->m_queryParameters = NULL;
    dest->m_attributes = copy->m_attributes; copy->m_attributes = NULL;
    dest->m_transferTimeout = copy->m_transferTimeout;
    dest->m_priority = copy->m_priority;
    XClass_delete_base((XClass*)copy);
}

static void VXNetworkRequestFactory_move(XNetworkRequestFactory* dest,
                                         XNetworkRequestFactory* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XNetworkRequestFactory_init(dest);
    xrequest_factory_release(dest);
    dest->m_baseUrl = src->m_baseUrl; dest->m_commonHeaders = src->m_commonHeaders;
    dest->m_bearerToken = src->m_bearerToken; dest->m_userName = src->m_userName;
    dest->m_password = src->m_password; dest->m_queryParameters = src->m_queryParameters;
    dest->m_attributes = src->m_attributes; dest->m_transferTimeout = src->m_transferTimeout;
    dest->m_priority = src->m_priority;
    src->m_baseUrl = NULL; src->m_commonHeaders = NULL; src->m_bearerToken = NULL;
    src->m_userName = NULL; src->m_password = NULL; src->m_queryParameters = NULL;
    src->m_attributes = XVector_create(sizeof(XNetworkRequestFactory_AttributeItem));
    src->m_transferTimeout = 30000; src->m_priority = XHttpRequest_NormalPriority;
}

XVtable* XNetworkRequestFactory_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    //虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XNetworkRequestFactory)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkRequestFactory_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkRequestFactory_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkRequestFactory_move);
#if SHOWCONTAINERSIZE
    printf("XNetworkRequestFactory size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

void XNetworkRequestFactory_init(XNetworkRequestFactory* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XNetworkRequestFactory);
    self->m_commonHeaders = XHttpHeaders_create();
    self->m_attributes = XVector_create(sizeof(XNetworkRequestFactory_AttributeItem));
    self->m_transferTimeout = 30000;
    self->m_priority = XHttpRequest_NormalPriority;
}

XNetworkRequestFactory* XNetworkRequestFactory_create(void)
{
    XNetworkRequestFactory* self = (XNetworkRequestFactory*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XNetworkRequestFactory_init(self);
    if (!self->m_commonHeaders || !self->m_attributes) {
        XNetworkRequestFactory_deinit_base((XClass*)self); XFree_System(self); return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XNetworkRequestFactory* XNetworkRequestFactory_create_url(const XUrl* baseUrl)
{
    XNetworkRequestFactory* self = XNetworkRequestFactory_create();
    if (self && !XNetworkRequestFactory_setBaseUrl(self, baseUrl)) {
        XNetworkRequestFactory_delete_base((XClass*)self); return NULL;
    }
    return self;
}

XNetworkRequestFactory* XNetworkRequestFactory_create_copy(const XNetworkRequestFactory* other)
{
    if (!other) return NULL;
    XNetworkRequestFactory* self = XNetworkRequestFactory_create();
    if (self) XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XNetworkRequestFactory* XNetworkRequestFactory_create_move(XNetworkRequestFactory* other)
{
    if (!other) return NULL;
    XNetworkRequestFactory* self = XNetworkRequestFactory_create();
    if (self) XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

XUrl* XNetworkRequestFactory_baseUrl(const XNetworkRequestFactory* self)
{ return self && self->m_baseUrl ? XUrl_create_copy(self->m_baseUrl) : NULL; }

bool XNetworkRequestFactory_setBaseUrl(XNetworkRequestFactory* self, const XUrl* url)
{
    if (!self) return false;
    XUrl* replacement = url ? XUrl_create_copy(url) : NULL;
    if (url && !replacement) return false;
    if (self->m_baseUrl) XClass_delete_base((XClass*)self->m_baseUrl);
    self->m_baseUrl = replacement; return true;
}

XHttpHeaders* XNetworkRequestFactory_commonHeaders(const XNetworkRequestFactory* self)
{ return self && self->m_commonHeaders ? XHttpHeaders_create_copy(self->m_commonHeaders) : XHttpHeaders_create(); }

bool XNetworkRequestFactory_setCommonHeaders(XNetworkRequestFactory* self, const XHttpHeaders* headers)
{
    if (!self) return false;
    XHttpHeaders* replacement = headers ? XHttpHeaders_create_copy(headers) : XHttpHeaders_create();
    if (!replacement) return false;
    if (self->m_commonHeaders) XClass_delete_base((XClass*)self->m_commonHeaders);
    self->m_commonHeaders = replacement; return true;
}

void XNetworkRequestFactory_clearCommonHeaders(XNetworkRequestFactory* self)
{ if (self && self->m_commonHeaders) XHttpHeaders_clear(self->m_commonHeaders); }

XByteArray* XNetworkRequestFactory_bearerToken(const XNetworkRequestFactory* self)
{ return self && self->m_bearerToken ? XByteArray_create_copy(self->m_bearerToken) : XByteArray_create(); }
bool XNetworkRequestFactory_setBearerToken(XNetworkRequestFactory* self, const XByteArray* token)
{ return self ? xrequest_factory_replace_bytes(&self->m_bearerToken, token) : false; }
void XNetworkRequestFactory_clearBearerToken(XNetworkRequestFactory* self)
{ if (self && self->m_bearerToken) { XClass_delete_base((XClass*)self->m_bearerToken); self->m_bearerToken = NULL; } }
bool XNetworkRequestFactory_setUserName(XNetworkRequestFactory* self, const XByteArray* name)
{ return self ? xrequest_factory_replace_bytes(&self->m_userName, name) : false; }
XByteArray* XNetworkRequestFactory_userName(const XNetworkRequestFactory* self)
{ return self && self->m_userName ? XByteArray_create_copy(self->m_userName) : XByteArray_create(); }
void XNetworkRequestFactory_clearUserName(XNetworkRequestFactory* self)
{ if (self && self->m_userName) { XClass_delete_base((XClass*)self->m_userName); self->m_userName = NULL; } }
bool XNetworkRequestFactory_setPassword(XNetworkRequestFactory* self, const XByteArray* password)
{ return self ? xrequest_factory_replace_bytes(&self->m_password, password) : false; }
XByteArray* XNetworkRequestFactory_password(const XNetworkRequestFactory* self)
{ return self && self->m_password ? XByteArray_create_copy(self->m_password) : XByteArray_create(); }
void XNetworkRequestFactory_clearPassword(XNetworkRequestFactory* self)
{ if (self && self->m_password) { XClass_delete_base((XClass*)self->m_password); self->m_password = NULL; } }
bool XNetworkRequestFactory_setQueryParameters(XNetworkRequestFactory* self, const XByteArray* query)
{ return self ? xrequest_factory_replace_bytes(&self->m_queryParameters, query) : false; }
XByteArray* XNetworkRequestFactory_queryParameters(const XNetworkRequestFactory* self)
{ return self && self->m_queryParameters ? XByteArray_create_copy(self->m_queryParameters) : XByteArray_create(); }
void XNetworkRequestFactory_clearQueryParameters(XNetworkRequestFactory* self)
{ if (self && self->m_queryParameters) { XClass_delete_base((XClass*)self->m_queryParameters); self->m_queryParameters = NULL; } }
void XNetworkRequestFactory_setTransferTimeout(XNetworkRequestFactory* self, int timeout)
{ if (self) self->m_transferTimeout = timeout < 0 ? 0 : timeout; }
int XNetworkRequestFactory_transferTimeout(const XNetworkRequestFactory* self)
{ return self ? self->m_transferTimeout : 30000; }
bool XNetworkRequestFactory_setPriority(XNetworkRequestFactory* self, XHttpRequest_Priority priority)
{
    if (!self || (priority != XHttpRequest_HighPriority &&
                  priority != XHttpRequest_NormalPriority &&
                  priority != XHttpRequest_LowPriority))
        return false;
    self->m_priority = priority;
    return true;
}
XHttpRequest_Priority XNetworkRequestFactory_priority(const XNetworkRequestFactory* self)
{ return self ? self->m_priority : XHttpRequest_NormalPriority; }

XHttpRequest* XNetworkRequestFactory_createRequest(const XNetworkRequestFactory* self)
{ return xrequest_factory_create_internal(self, NULL, NULL); }
XHttpRequest* XNetworkRequestFactory_createRequest_path(const XNetworkRequestFactory* self, const char* path)
{ return xrequest_factory_create_internal(self, path, NULL); }
XHttpRequest* XNetworkRequestFactory_createRequest_path_query(const XNetworkRequestFactory* self,
                                                              const char* path, const XByteArray* query)
{ return xrequest_factory_create_internal(self, path, query); }

bool XNetworkRequestFactory_setAttribute(XNetworkRequestFactory* self, int code, const XVariant* value)
{
    XNetworkRequestFactory_AttributeItem* item;
    XVariant* copy;
    if (!self || code < 0 || (code > XNetworkRequestFactory_FullLocalServerNameAttribute &&
                              (code < XNetworkRequestFactory_UserAttribute ||
                               code > XNetworkRequestFactory_UserMaxAttribute))) return false;
    item = xrequest_factory_find_attribute(self, code);
    if (!value) { XNetworkRequestFactory_clearAttribute(self, code); return true; }
    copy = XVariant_create_copy(value);
    if (!copy) return false;
    if (item) {
        if (item->m_value) XClass_delete_base((XClass*)item->m_value);
        item->m_value = copy; return true;
    }
    XNetworkRequestFactory_AttributeItem created = { code, copy };
    if (!XVector_push_back_1_base(self->m_attributes, &created)) {
        XClass_delete_base((XClass*)copy); return false;
    }
    return true;
}

XVariant* XNetworkRequestFactory_attribute(const XNetworkRequestFactory* self, int code)
{
    XNetworkRequestFactory_AttributeItem* item = xrequest_factory_find_attribute(self, code);
    return item && item->m_value ? XVariant_create_copy(item->m_value) : NULL;
}

void XNetworkRequestFactory_clearAttribute(XNetworkRequestFactory* self, int code)
{
    if (!self || !self->m_attributes) return;
    for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
        XNetworkRequestFactory_AttributeItem* item =
            (XNetworkRequestFactory_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
        if (item && item->m_code == code) {
            if (item->m_value) XClass_delete_base((XClass*)item->m_value);
            XVector_removeAt_base(self->m_attributes, (int64_t)i); return;
        }
    }
}

void XNetworkRequestFactory_clearAttributes(XNetworkRequestFactory* self)
{ xrequest_factory_release_attributes(self); }
