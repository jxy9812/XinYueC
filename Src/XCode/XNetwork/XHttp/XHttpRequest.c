#include "XHttpRequest.h"

#include "XMemory.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool xhttp_request_set_bytes(XByteArray** target, const XByteArray* value)
{
    XByteArray* replacement = value ? XByteArray_create_copy(value) : XByteArray_create();
    if (!replacement)
        return false;
    if (*target)
        XClass_delete_base((XClass*)*target);
    *target = replacement;
    return true;
}

static const char* xhttp_request_method_name(const XHttpRequest* self)
{
    if (!self)
        return "GET";
    switch (self->m_method) {
    case XHttpRequest_Head: return "HEAD";
    case XHttpRequest_Get: return "GET";
    case XHttpRequest_Post: return "POST";
    case XHttpRequest_Put: return "PUT";
    case XHttpRequest_Delete: return "DELETE";
    case XHttpRequest_Patch: return "PATCH";
    case XHttpRequest_Custom:
        return self->m_customMethod ? (const char*)XByteArray_constData(self->m_customMethod) : "";
    default: return "";
    }
}

static bool xhttp_request_has_header(const XHttpRequest* self, const char* name)
{
    XByteArray* field = XByteArray_create_utf8(name);
    bool result = field && XHttpHeaders_contains(self ? self->m_headers : NULL, field);
    if (field)
        XClass_delete_base((XClass*)field);
    return result;
}

static bool xhttp_request_ascii_equal(const XByteArray* left, const XByteArray* right)
{
    size_t leftSize = left ? XContainer_size_base((const XContainer*)left) : 0;
    size_t rightSize = right ? XContainer_size_base((const XContainer*)right) : 0;
    const uint8_t* leftData = left ? XByteArray_constData((XByteArray*)left) : NULL;
    const uint8_t* rightData = right ? XByteArray_constData((XByteArray*)right) : NULL;
    if (!left || !right || leftSize != rightSize) return false;
    for (size_t i = 0; i < leftSize; ++i) {
        uint8_t a = leftData[i], b = rightData[i];
        if (a >= 'A' && a <= 'Z') a = (uint8_t)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (uint8_t)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static bool xhttp_request_method_can_have_body(const XHttpRequest* self)
{
    if (!self)
        return false;
    return self->m_method == XHttpRequest_Post || self->m_method == XHttpRequest_Put ||
           self->m_method == XHttpRequest_Patch || self->m_method == XHttpRequest_Custom;
}

static void xhttp_request_release_members(XHttpRequest* self)
{
    if (!self)
        return;
    if (self->m_url) {
        XClass_delete_base((XClass*)self->m_url);
        self->m_url = NULL;
    }
    if (self->m_headers) {
        XClass_delete_base((XClass*)self->m_headers);
        self->m_headers = NULL;
    }
    if (self->m_body) {
        XClass_delete_base((XClass*)self->m_body);
        self->m_body = NULL;
    }
    if (self->m_customMethod) {
        XClass_delete_base((XClass*)self->m_customMethod);
        self->m_customMethod = NULL;
    }
    if (self->m_http1Configuration) {
        XClass_delete_base((XClass*)self->m_http1Configuration);
        self->m_http1Configuration = NULL;
    }
    if (self->m_http2Configuration) {
        XClass_delete_base((XClass*)self->m_http2Configuration);
        self->m_http2Configuration = NULL;
    }
    if (self->m_attributes) {
        for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
            XHttpRequest_AttributeItem* item =
                (XHttpRequest_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
            if (item && item->m_value)
                XClass_delete_base((XClass*)item->m_value);
        }
        XContainer_clear_base((XContainer*)self->m_attributes);
        XClass_delete_base((XClass*)self->m_attributes);
        self->m_attributes = NULL;
    }
}

static XHttpRequest_AttributeItem* xhttp_request_find_attribute(const XHttpRequest* self, int code)
{
    if (!self || !self->m_attributes)
        return NULL;
    for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
        XHttpRequest_AttributeItem* item =
            (XHttpRequest_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
        if (item && item->m_code == code)
            return item;
    }
    return NULL;
}

static void VXHttpRequest_deinit(XHttpRequest* self)
{
    if (!self)
        return;
    xhttp_request_release_members(self);
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttpRequest_copy(XHttpRequest* dest, const XHttpRequest* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpRequest_init(dest);

    XUrl* url = src->m_url ? XUrl_create_copy(src->m_url) : NULL;
    XHttpHeaders* headers = src->m_headers ? XHttpHeaders_create_copy(src->m_headers) : XHttpHeaders_create();
    XByteArray* body = src->m_body ? XByteArray_create_copy(src->m_body) : XByteArray_create();
    XByteArray* custom = src->m_customMethod ? XByteArray_create_copy(src->m_customMethod) : NULL;
    XHttp1Configuration* http1 = src->m_http1Configuration ?
        XHttp1Configuration_create_copy(src->m_http1Configuration) : XHttp1Configuration_create();
    XHttp2Configuration* http2 = src->m_http2Configuration ?
        XHttp2Configuration_create_copy(src->m_http2Configuration) : XHttp2Configuration_create();
    XVector* attributes = XVector_create(sizeof(XHttpRequest_AttributeItem));
    if (!headers || !body || !http1 || !http2 || !attributes || (src->m_url && !url) || (src->m_customMethod && !custom)) {
        if (url) XClass_delete_base((XClass*)url);
        if (headers) XClass_delete_base((XClass*)headers);
        if (body) XClass_delete_base((XClass*)body);
        if (custom) XClass_delete_base((XClass*)custom);
        if (http1) XClass_delete_base((XClass*)http1);
        if (http2) XClass_delete_base((XClass*)http2);
        if (attributes) XClass_delete_base((XClass*)attributes);
        return;
    }
    xhttp_request_release_members(dest);
    dest->m_url = url;
    dest->m_headers = headers;
    dest->m_body = body;
    dest->m_customMethod = custom;
    dest->m_http1Configuration = http1;
    dest->m_http2Configuration = http2;
    dest->m_attributes = attributes;
    for (size_t i = 0; i < XVector_size_base(src->m_attributes); ++i) {
        XHttpRequest_AttributeItem* item =
            (XHttpRequest_AttributeItem*)XVector_at_base(src->m_attributes, (int64_t)i);
        if (!item || !XHttpRequest_setAttribute(dest, item->m_code, item->m_value)) {
            xhttp_request_release_members(dest);
            return;
        }
    }
    dest->m_method = src->m_method;
    dest->m_priority = src->m_priority;
    dest->m_redirectPolicy = src->m_redirectPolicy;
    dest->m_maximumRedirectsAllowed = src->m_maximumRedirectsAllowed;
    dest->m_transferTimeout = src->m_transferTimeout;
    dest->m_autoDecompress = src->m_autoDecompress;
}

static void VXHttpRequest_move(XHttpRequest* dest, XHttpRequest* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpRequest_init(dest);

    xhttp_request_release_members(dest);
    dest->m_url = src->m_url;
    dest->m_headers = src->m_headers;
    dest->m_body = src->m_body;
    dest->m_customMethod = src->m_customMethod;
    dest->m_http1Configuration = src->m_http1Configuration;
    dest->m_http2Configuration = src->m_http2Configuration;
    dest->m_attributes = src->m_attributes;
    dest->m_method = src->m_method;
    dest->m_priority = src->m_priority;
    dest->m_redirectPolicy = src->m_redirectPolicy;
    dest->m_maximumRedirectsAllowed = src->m_maximumRedirectsAllowed;
    dest->m_transferTimeout = src->m_transferTimeout;
    dest->m_autoDecompress = src->m_autoDecompress;
    src->m_url = NULL;
    src->m_headers = NULL;
    src->m_body = NULL;
    src->m_customMethod = NULL;
    src->m_http1Configuration = NULL;
    src->m_http2Configuration = NULL;
    src->m_attributes = XVector_create(sizeof(XHttpRequest_AttributeItem));
    src->m_method = XHttpRequest_Get;
    src->m_priority = XHttpRequest_NormalPriority;
    src->m_redirectPolicy = XHttpRequest_ManualRedirectPolicy;
    src->m_maximumRedirectsAllowed = -1;
    src->m_transferTimeout = 0;
    src->m_autoDecompress = false;
}

XVtable* XHttpRequest_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttpRequest)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttpRequest");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpRequest_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttpRequest_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttpRequest_move);
    XCLASS_SHOW_SIZE_DEFAULT(XHttpRequest);
    return XVTABLE_DEFAULT;
}

void XHttpRequest_init(XHttpRequest* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpRequest);
    self->m_headers = XHttpHeaders_create();
    self->m_body = XByteArray_create();
    self->m_http1Configuration = XHttp1Configuration_create();
    self->m_http2Configuration = XHttp2Configuration_create();
    self->m_attributes = XVector_create(sizeof(XHttpRequest_AttributeItem));
    self->m_method = XHttpRequest_Get;
    self->m_priority = XHttpRequest_NormalPriority;
    self->m_redirectPolicy = XHttpRequest_ManualRedirectPolicy;
    self->m_maximumRedirectsAllowed = -1;
}

XHttpRequest* XHttpRequest_create(void)
{
    XHttpRequest* self = (XHttpRequest*)XMalloc_System(sizeof(XHttpRequest));
    if (!self)
        return NULL;
    XHttpRequest_init(self);
    if (!self->m_headers || !self->m_body || !self->m_http1Configuration ||
        !self->m_http2Configuration || !self->m_attributes) {
        XHttpRequest_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttpRequest* XHttpRequest_create_url(const XUrl* url)
{
    if (!url)
        return NULL;
    XHttpRequest* self = XHttpRequest_create();
    if (self && !XHttpRequest_setUrl(self, url)) {
        XHttpRequest_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

XHttpRequest* XHttpRequest_create_copy(const XHttpRequest* other)
{
    if (!other)
        return NULL;
    XHttpRequest* self = XHttpRequest_create();
    if (self)
        XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XHttpRequest* XHttpRequest_create_move(XHttpRequest* other)
{
    if (!other)
        return NULL;
    XHttpRequest* self = XHttpRequest_create();
    if (self)
        XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

const XUrl* XHttpRequest_url_const(const XHttpRequest* self)
{
    return self ? self->m_url : NULL;
}

const XHttp1Configuration* XHttpRequest_http1Configuration_const(const XHttpRequest* self)
{
    return self ? self->m_http1Configuration : NULL;
}

bool XHttpRequest_setHttp1Configuration(XHttpRequest* self,
                                        const XHttp1Configuration* configuration)
{
    XHttp1Configuration* replacement;
    if (!self || !configuration)
        return false;
    replacement = XHttp1Configuration_create_copy(configuration);
    if (!replacement)
        return false;
    if (self->m_http1Configuration)
        XClass_delete_base((XClass*)self->m_http1Configuration);
    self->m_http1Configuration = replacement;
    return true;
}

const XHttp2Configuration* XHttpRequest_http2Configuration_const(const XHttpRequest* self)
{
    return self ? self->m_http2Configuration : NULL;
}

bool XHttpRequest_setHttp2Configuration(XHttpRequest* self,
                                        const XHttp2Configuration* configuration)
{
    XHttp2Configuration* replacement;
    if (!self || !configuration)
        return false;
    replacement = XHttp2Configuration_create_copy(configuration);
    if (!replacement)
        return false;
    if (self->m_http2Configuration)
        XClass_delete_base((XClass*)self->m_http2Configuration);
    self->m_http2Configuration = replacement;
    return true;
}

bool XHttpRequest_setUrl(XHttpRequest* self, const XUrl* url)
{
    if (!self)
        return false;
    XUrl* replacement = url ? XUrl_create_copy(url) : NULL;
    if (url && !replacement)
        return false;
    if (self->m_url)
        XClass_delete_base((XClass*)self->m_url);
    self->m_url = replacement;
    return true;
}

bool XHttpRequest_setUrl_utf8(XHttpRequest* self, const char* url)
{
    if (!self || !url)
        return false;
    XString* text = XString_create_utf8(url);
    XUrl* parsed = text ? XUrl_create_ex(text, XUrl_TolerantMode) : NULL;
    bool result = parsed && XUrl_isValid(parsed) && XHttpRequest_setUrl(self, parsed);
    if (text) XClass_delete_base((XClass*)text);
    if (parsed) XClass_delete_base((XClass*)parsed);
    return result;
}

const XHttpHeaders* XHttpRequest_headers_const(const XHttpRequest* self)
{
    return self ? self->m_headers : NULL;
}

XHttpHeaders* XHttpRequest_headers(XHttpRequest* self)
{
    return self ? self->m_headers : NULL;
}

bool XHttpRequest_setRawHeader(XHttpRequest* self, const char* name, const char* value)
{
    if (!self || !self->m_headers || !name)
        return false;
    XByteArray* n = XByteArray_create_utf8(name);
    XByteArray* v = XByteArray_create_utf8(value ? value : "");
    bool result = n && v && XHttpHeaders_replaceOrAppend(self->m_headers, n, v);
    if (n) XClass_delete_base((XClass*)n);
    if (v) XClass_delete_base((XClass*)v);
    return result;
}

bool XHttpRequest_hasRawHeader(const XHttpRequest* self, const XByteArray* name)
{ return self && XHttpHeaders_contains(self->m_headers, name); }

XByteArray* XHttpRequest_rawHeader(const XHttpRequest* self, const XByteArray* name)
{ return self ? XHttpHeaders_value(self->m_headers, name) : NULL; }

XVector* XHttpRequest_rawHeaderList(const XHttpRequest* self)
{
    XVector* result = XVector_create(sizeof(XByteArray*));
    if (!result) return NULL;
    for (size_t i = 0; self && i < XHttpHeaders_size(self->m_headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(self->m_headers, i);
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            const XByteArray* previous = XHttpHeaders_nameAt_const(self->m_headers, j);
            if (name && previous && xhttp_request_ascii_equal(name, previous)) {
                duplicate = true; break;
            }
        }
        if (!duplicate && name) {
            XByteArray* copy = XByteArray_create_copy(name);
            if (!copy || !XVector_push_back_1_base(result, &copy)) {
                if (copy) XClass_delete_base((XClass*)copy);
                XHttpHeaders_values_free(result); return NULL;
            }
        }
    }
    return result;
}

bool XHttpRequest_setHeaderKnown(XHttpRequest* self, XHttpHeaders_WellKnownHeader header,
                                 const XByteArray* value)
{ return self && XHttpHeaders_replaceOrAppendKnown(self->m_headers, header, value); }

XByteArray* XHttpRequest_headerKnown(const XHttpRequest* self,
                                     XHttpHeaders_WellKnownHeader header)
{ return self ? XHttpHeaders_valueKnown(self->m_headers, header) : NULL; }

const XByteArray* XHttpRequest_body_const(const XHttpRequest* self)
{
    return self ? self->m_body : NULL;
}

bool XHttpRequest_setBody(XHttpRequest* self, const XByteArray* body)
{
    return self ? xhttp_request_set_bytes(&self->m_body, body) : false;
}

bool XHttpRequest_setBody_utf8(XHttpRequest* self, const char* body)
{
    if (!self)
        return false;
    XByteArray* bytes = XByteArray_create_utf8(body ? body : "");
    if (!bytes)
        return false;
    bool result = XHttpRequest_setBody(self, bytes);
    XClass_delete_base((XClass*)bytes);
    return result;
}

bool XHttpRequest_setMethod(XHttpRequest* self, XHttpRequest_Method method)
{
    if (!self || method < XHttpRequest_Head || method > XHttpRequest_Custom)
        return false;
    self->m_method = method;
    return true;
}

bool XHttpRequest_setCustomMethod(XHttpRequest* self, const char* method)
{
    if (!self || !method || !*method)
        return false;
    XByteArray* value = XByteArray_create_utf8(method);
    if (!value)
        return false;
    bool result = XHttpRequest_setCustomMethod_bytes(self, value);
    XClass_delete_base((XClass*)value);
    return result;
}

bool XHttpRequest_setCustomMethod_bytes(XHttpRequest* self, const XByteArray* method)
{
    if (!self || !method || XContainer_size_base((const XContainer*)method) == 0)
        return false;
    XByteArray* value = XByteArray_create_copy(method);
    if (!value)
        return false;
    bool result = true;
    const uint8_t* data = XByteArray_constData(value);
    size_t size = XContainer_size_base((const XContainer*)value);
    for (size_t i = 0; i < size; ++i) {
        if (!isalnum(data[i]) && data[i] != '!' && data[i] != '#' && data[i] != '$' &&
            data[i] != '%' && data[i] != '&' && data[i] != '\'' && data[i] != '*' &&
            data[i] != '+' && data[i] != '-' && data[i] != '.' && data[i] != '^' &&
            data[i] != '_' && data[i] != '`' && data[i] != '|' && data[i] != '~') {
            result = false;
            break;
        }
    }
    if (result) {
        if (self->m_customMethod) XClass_delete_base((XClass*)self->m_customMethod);
        self->m_customMethod = value;
        self->m_method = XHttpRequest_Custom;
        return true;
    }
    XClass_delete_base((XClass*)value);
    return false;
}

XHttpRequest_Method XHttpRequest_method(const XHttpRequest* self)
{
    return self ? self->m_method : XHttpRequest_Get;
}

const XByteArray* XHttpRequest_customMethod_const(const XHttpRequest* self)
{
    return self && self->m_method == XHttpRequest_Custom ? self->m_customMethod : NULL;
}

bool XHttpRequest_setPriority(XHttpRequest* self, XHttpRequest_Priority priority)
{
    if (!self || (priority != XHttpRequest_HighPriority && priority != XHttpRequest_NormalPriority &&
                  priority != XHttpRequest_LowPriority))
        return false;
    self->m_priority = priority;
    return true;
}

XHttpRequest_Priority XHttpRequest_priority(const XHttpRequest* self)
{
    return self ? self->m_priority : XHttpRequest_NormalPriority;
}

bool XHttpRequest_setRedirectPolicy(XHttpRequest* self, XHttpRequest_RedirectPolicy policy)
{
    if (!self || policy < XHttpRequest_ManualRedirectPolicy || policy > XHttpRequest_UserVerifiedRedirectPolicy)
        return false;
    self->m_redirectPolicy = policy;
    return true;
}

XHttpRequest_RedirectPolicy XHttpRequest_redirectPolicy(const XHttpRequest* self)
{
    return self ? self->m_redirectPolicy : XHttpRequest_ManualRedirectPolicy;
}

void XHttpRequest_setMaximumRedirectsAllowed(XHttpRequest* self, int maximumRedirectsAllowed)
{
    if (self)
        self->m_maximumRedirectsAllowed = maximumRedirectsAllowed;
}

int XHttpRequest_maximumRedirectsAllowed(const XHttpRequest* self)
{
    return self ? self->m_maximumRedirectsAllowed : -1;
}

void XHttpRequest_setTransferTimeout(XHttpRequest* self, int timeout)
{
    if (self)
        self->m_transferTimeout = timeout < 0 ? 0 : timeout;
}

int XHttpRequest_transferTimeout(const XHttpRequest* self)
{
    return self ? self->m_transferTimeout : 0;
}

void XHttpRequest_setAutoDecompress(XHttpRequest* self, bool enabled)
{
    if (self)
        self->m_autoDecompress = enabled;
}

bool XHttpRequest_autoDecompress(const XHttpRequest* self)
{
    return self ? self->m_autoDecompress : false;
}

bool XHttpRequest_setAttribute(XHttpRequest* self, int code, const XVariant* value)
{
    XHttpRequest_AttributeItem* item;
    XVariant* copy;
    if (!self || code < 0 || (code > XHttpRequest_FullLocalServerNameAttribute &&
                              (code < XHttpRequest_UserAttribute || code > XHttpRequest_UserMaxAttribute)))
        return false;
    item = xhttp_request_find_attribute(self, code);
    if (!value) {
        XHttpRequest_clearAttribute(self, code);
        return true;
    }
    copy = XVariant_create_copy(value);
    if (!copy)
        return false;
    if (item) {
        if (item->m_value) XClass_delete_base((XClass*)item->m_value);
        item->m_value = copy;
        if (code == XHttpRequest_RedirectPolicyAttribute && XVariant_type(copy) == XVariantType_Int)
            XHttpRequest_setRedirectPolicy(self,
                (XHttpRequest_RedirectPolicy)XVariant_toInt(copy));
        return true;
    }
    XHttpRequest_AttributeItem created = { code, copy };
    if (!XVector_push_back_1_base(self->m_attributes, &created)) {
        XClass_delete_base((XClass*)copy);
        return false;
    }
    if (code == XHttpRequest_RedirectPolicyAttribute && XVariant_type(copy) == XVariantType_Int)
        XHttpRequest_setRedirectPolicy(self,
            (XHttpRequest_RedirectPolicy)XVariant_toInt(copy));
    return true;
}

XVariant* XHttpRequest_attribute(const XHttpRequest* self, int code)
{
    XHttpRequest_AttributeItem* item = xhttp_request_find_attribute(self, code);
    return item && item->m_value ? XVariant_create_copy(item->m_value) : NULL;
}

void XHttpRequest_clearAttribute(XHttpRequest* self, int code)
{
    if (!self || !self->m_attributes)
        return;
    for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
        XHttpRequest_AttributeItem* item =
            (XHttpRequest_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
        if (item && item->m_code == code) {
            if (item->m_value) XClass_delete_base((XClass*)item->m_value);
            XVector_removeAt_base(self->m_attributes, (int64_t)i);
            return;
        }
    }
}

void XHttpRequest_clearAttributes(XHttpRequest* self)
{
    if (!self || !self->m_attributes)
        return;
    for (size_t i = 0; i < XVector_size_base(self->m_attributes); ++i) {
        XHttpRequest_AttributeItem* item =
            (XHttpRequest_AttributeItem*)XVector_at_base(self->m_attributes, (int64_t)i);
        if (item && item->m_value)
            XClass_delete_base((XClass*)item->m_value);
    }
    XContainer_clear_base((XContainer*)self->m_attributes);
}

XByteArray* XHttpRequest_toHttp1(const XHttpRequest* self, bool includeConnectionClose)
{
    if (!self || !self->m_url || !self->m_headers || !self->m_body)
        return NULL;
    const XString* path = XUrl_path_const(self->m_url);
    const XString* query = XUrl_query_const(self->m_url);
    const XString* host = XUrl_host_const(self->m_url);
    if (!host || XContainer_isEmpty_base((const XContainer*)host))
        return NULL;

    XByteArray* result = XByteArray_create();
    if (!result)
        return NULL;
    if (self->m_method == XHttpRequest_Custom) {
        if (!self->m_customMethod || XContainer_size_base((const XContainer*)self->m_customMethod) == 0 ||
            !XByteArray_push_back_2((XVector*)result,
                                    XByteArray_constData(self->m_customMethod),
                                    XContainer_size_base((const XContainer*)self->m_customMethod)))
            goto failed;
    } else {
        const char* method = xhttp_request_method_name(self);
        if (!method || !*method || !XByteArray_append_utf8(result, method))
            goto failed;
    }
    if (!XByteArray_append_utf8(result, " "))
        goto failed;
    if (!path || XContainer_isEmpty_base((const XContainer*)path)) {
        if (!XByteArray_append_utf8(result, "/")) goto failed;
    } else if (!XByteArray_push_back_2((XVector*)result, XString_toUtf8(path), XString_toUtf8_length(path))) {
        goto failed;
    }
    if (query && !XContainer_isEmpty_base((const XContainer*)query)) {
        if (!XByteArray_append_utf8(result, "?") ||
            !XByteArray_push_back_2((XVector*)result, XString_toUtf8(query), XString_toUtf8_length(query)))
            goto failed;
    }
    if (!XByteArray_append_utf8(result, " HTTP/1.1\r\n"))
        goto failed;

    if (!xhttp_request_has_header(self, "Host")) {
        if (!XByteArray_append_utf8(result, "Host: ") ||
            !XByteArray_push_back_2((XVector*)result, XString_toUtf8(host), XString_toUtf8_length(host)))
            goto failed;

        char port[32];
        int urlPort = XUrl_port(self->m_url);
        const XString* scheme = XUrl_scheme_const(self->m_url);
        const char* schemeText = scheme ? XString_toUtf8(scheme) : NULL;
        if (urlPort >= 0 && ((schemeText && strcmp(schemeText, "http") == 0 && urlPort != 80) ||
                             (schemeText && strcmp(schemeText, "https") == 0 && urlPort != 443))) {
            int n = snprintf(port, sizeof(port), ":%d", urlPort);
            if (n < 0 || !XByteArray_push_back_2((XVector*)result, port, (size_t)n)) goto failed;
        }
        if (!XByteArray_append_utf8(result, "\r\n")) goto failed;
    }

    for (size_t i = 0; i < XHttpHeaders_size(self->m_headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(self->m_headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(self->m_headers, i);
        if (!name || !value || !XByteArray_push_back_2((XVector*)result, XByteArray_constData((XByteArray*)name),
                                                        XContainer_size_base((const XContainer*)name)) ||
            !XByteArray_append_utf8(result, ": ") ||
            !XByteArray_push_back_2((XVector*)result, XByteArray_constData((XByteArray*)value),
                                    XContainer_size_base((const XContainer*)value)) ||
            !XByteArray_append_utf8(result, "\r\n"))
            goto failed;
    }
    if (!xhttp_request_has_header(self, "Content-Length") &&
        (XContainer_size_base((const XContainer*)self->m_body) > 0 ||
         xhttp_request_method_can_have_body(self))) {
        char length[64];
        int n = snprintf(length, sizeof(length), "Content-Length: %zu\r\n",
                         XContainer_size_base((const XContainer*)self->m_body));
        if (n < 0 || !XByteArray_push_back_2((XVector*)result, length, (size_t)n)) goto failed;
    }
    if (!xhttp_request_has_header(self, "Connection") && includeConnectionClose &&
        !XByteArray_append_utf8(result, "Connection: close\r\n")) goto failed;
    if (self->m_autoDecompress && !xhttp_request_has_header(self, "Accept-Encoding") &&
        !XByteArray_append_utf8(result, "Accept-Encoding: gzip, deflate\r\n")) goto failed;
    if (!XByteArray_append_utf8(result, "\r\n")) goto failed;
    if (XContainer_size_base((const XContainer*)self->m_body) > 0 &&
        !XByteArray_push_back_2((XVector*)result, XByteArray_constData(self->m_body),
                                XContainer_size_base((const XContainer*)self->m_body))) goto failed;
    return result;

failed:
    XClass_delete_base((XClass*)result);
    return NULL;
}
