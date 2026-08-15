/**
 * @file       XNetworkCookie.c
 * @brief      HTTP Cookie 和 CookieJar 实现。
 */

#include "XNetworkCookie.h"

#include "XDateTime.h"
#include "XMemory.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

static void VXNetworkCookie_deinit(XNetworkCookie* self);
static void VXNetworkCookie_copy(XNetworkCookie* dest, const XNetworkCookie* src);
static void VXNetworkCookie_move(XNetworkCookie* dest, XNetworkCookie* src);
static void VXNetworkCookieJar_deinit(XNetworkCookieJar* self);

static bool xcookie_append(XByteArray* target, const void* data, size_t size)
{
    if (!target || (!data && size != 0))
        return false;
    return size == 0 || XByteArray_push_back_2((XVector*)target, data, size);
}

static int xcookie_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool xcookie_equal(const XByteArray* left, const XByteArray* right)
{
    size_t leftSize;
    size_t rightSize;
    const uint8_t* leftData;
    const uint8_t* rightData;
    if (!left || !right)
        return false;
    leftSize = XByteArray_size_base((XContainer*)left);
    rightSize = XByteArray_size_base((XContainer*)right);
    if (leftSize != rightSize)
        return false;
    leftData = XByteArray_constData((XByteArray*)left);
    rightData = XByteArray_constData((XByteArray*)right);
    for (size_t i = 0; i < leftSize; ++i) {
        if (xcookie_lower(leftData[i]) != xcookie_lower(rightData[i]))
            return false;
    }
    return true;
}

static bool xcookie_equal_literal(const XByteArray* value, const char* literal)
{
    XByteArray* expected;
    bool result;
    if (!literal)
        return false;
    expected = XByteArray_create_utf8(literal);
    result = expected && xcookie_equal(value, expected);
    if (expected) XClass_delete_base((XClass*)expected);
    return result;
}

static bool xcookie_valid_name(const XByteArray* name)
{
    const uint8_t* data;
    size_t size;
    if (!name)
        return false;
    data = XByteArray_constData((XByteArray*)name);
    size = XByteArray_size_base((XContainer*)name);
    if (!data || size == 0)
        return false;
    for (size_t i = 0; i < size; ++i) {
        uint8_t c = data[i];
        if (c <= 0x20 || c >= 0x7f || c == '=' || c == ';' || c == ',')
            return false;
    }
    return true;
}

static XByteArray* xcookie_trim(const uint8_t* data, size_t begin, size_t end)
{
    while (begin < end && (data[begin] == ' ' || data[begin] == '\t')) ++begin;
    while (end > begin && (data[end - 1] == ' ' || data[end - 1] == '\t')) --end;
    return XByteArray_create_with_data((const char*)(data + begin), end - begin);
}

static bool xcookie_parse_int(const XByteArray* value, int64_t* result)
{
    char buffer[48];
    size_t size;
    char* end;
    long long number;
    if (!value || !result)
        return false;
    size = XByteArray_size_base((XContainer*)value);
    if (size == 0 || size >= sizeof(buffer))
        return false;
    memcpy(buffer, XByteArray_constData((XByteArray*)value), size);
    buffer[size] = '\0';
    number = strtoll(buffer, &end, 10);
    if (end == buffer || *end != '\0')
        return false;
    *result = (int64_t)number;
    return true;
}

static void xcookie_release_members(XNetworkCookie* self)
{
    if (!self)
        return;
    if (self->m_name) XClass_delete_base((XClass*)self->m_name);
    if (self->m_value) XClass_delete_base((XClass*)self->m_value);
    if (self->m_domain) XClass_delete_base((XClass*)self->m_domain);
    if (self->m_path) XClass_delete_base((XClass*)self->m_path);
    self->m_name = NULL;
    self->m_value = NULL;
    self->m_domain = NULL;
    self->m_path = NULL;
}

XVtable* XNetworkCookie_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkCookie)
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkCookie_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkCookie_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkCookie_move);
    XCLASS_SHOW_SIZE_DEFAULT(XNetworkCookie);
    return XVTABLE_DEFAULT;
}

void XNetworkCookie_init(XNetworkCookie* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XNetworkCookie);
    self->m_name = XByteArray_create();
    self->m_value = XByteArray_create();
    self->m_domain = XByteArray_create();
    self->m_path = XByteArray_create();
    self->m_expirationSecs = -1;
    self->m_maxAge = -1;
    self->m_sameSite = XNetworkCookie_SameSiteDefault;
}

XNetworkCookie* XNetworkCookie_create_empty(void)
{
    return XNetworkCookie_create(NULL, NULL);
}

XNetworkCookie* XNetworkCookie_create_ex(XMemoryType memory, const XByteArray* name, const XByteArray* value)
{
    XNetworkCookie* self = (XNetworkCookie*)XMemory_malloc(sizeof(XNetworkCookie), memory);
    if (!self)
        return NULL;
    XNetworkCookie_init(self);
    if (!self->m_name || !self->m_value || !self->m_domain || !self->m_path ||
        (name && !XNetworkCookie_setName(self, name)) ||
        (value && !XNetworkCookie_setValue(self, value))) {
        XNetworkCookie_deinit_base((XClass*)self);
        XMemory_method(memory)->free(self);
        return NULL;
    }
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

XNetworkCookie* XNetworkCookie_create_copy(const XNetworkCookie* other)
{
    XNetworkCookie* self;
    if (!other)
        return NULL;
    self = XNetworkCookie_create_empty();
    if (self) XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XNetworkCookie* XNetworkCookie_create_move(XNetworkCookie* other)
{
    XNetworkCookie* self;
    if (!other)
        return NULL;
    self = XNetworkCookie_create_empty();
    if (self) XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

static void VXNetworkCookie_deinit(XNetworkCookie* self)
{
    if (!self)
        return;
    xcookie_release_members(self);
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXNetworkCookie_copy(XNetworkCookie* dest, const XNetworkCookie* src)
{
    XByteArray* name;
    XByteArray* value;
    XByteArray* domain;
    XByteArray* path;
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XNetworkCookie_init(dest);
    name = src->m_name ? XByteArray_create_copy(src->m_name) : XByteArray_create();
    value = src->m_value ? XByteArray_create_copy(src->m_value) : XByteArray_create();
    domain = src->m_domain ? XByteArray_create_copy(src->m_domain) : XByteArray_create();
    path = src->m_path ? XByteArray_create_copy(src->m_path) : XByteArray_create();
    if (!name || !value || !domain || !path) {
        if (name) XClass_delete_base((XClass*)name);
        if (value) XClass_delete_base((XClass*)value);
        if (domain) XClass_delete_base((XClass*)domain);
        if (path) XClass_delete_base((XClass*)path);
        return;
    }
    xcookie_release_members(dest);
    dest->m_name = name;
    dest->m_value = value;
    dest->m_domain = domain;
    dest->m_path = path;
    dest->m_expirationSecs = src->m_expirationSecs;
    dest->m_maxAge = src->m_maxAge;
    dest->m_secure = src->m_secure;
    dest->m_httpOnly = src->m_httpOnly;
    dest->m_sameSite = src->m_sameSite;
}

static void VXNetworkCookie_move(XNetworkCookie* dest, XNetworkCookie* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest)) XNetworkCookie_init(dest);
    xcookie_release_members(dest);
    dest->m_name = src->m_name;
    dest->m_value = src->m_value;
    dest->m_domain = src->m_domain;
    dest->m_path = src->m_path;
    dest->m_expirationSecs = src->m_expirationSecs;
    dest->m_maxAge = src->m_maxAge;
    dest->m_secure = src->m_secure;
    dest->m_httpOnly = src->m_httpOnly;
    dest->m_sameSite = src->m_sameSite;
    src->m_name = NULL;
    src->m_value = NULL;
    src->m_domain = NULL;
    src->m_path = NULL;
}

static bool xcookie_set_bytes(XByteArray** target, const XByteArray* value)
{
    XByteArray* replacement = value ? XByteArray_create_copy(value) : XByteArray_create();
    if (!replacement)
        return false;
    if (*target) XClass_delete_base((XClass*)*target);
    *target = replacement;
    return true;
}

bool XNetworkCookie_setName(XNetworkCookie* self, const XByteArray* name)
{
    return self && xcookie_valid_name(name) && xcookie_set_bytes(&self->m_name, name);
}

const XByteArray* XNetworkCookie_name_const(const XNetworkCookie* self)
{
    return self ? self->m_name : NULL;
}

bool XNetworkCookie_setValue(XNetworkCookie* self, const XByteArray* value)
{
    return self && xcookie_set_bytes(&self->m_value, value);
}

const XByteArray* XNetworkCookie_value_const(const XNetworkCookie* self)
{
    return self ? self->m_value : NULL;
}

bool XNetworkCookie_setDomain(XNetworkCookie* self, const XByteArray* domain)
{
    return self && xcookie_set_bytes(&self->m_domain, domain);
}

const XByteArray* XNetworkCookie_domain_const(const XNetworkCookie* self)
{
    return self ? self->m_domain : NULL;
}

bool XNetworkCookie_setPath(XNetworkCookie* self, const XByteArray* path)
{
    return self && xcookie_set_bytes(&self->m_path, path);
}

const XByteArray* XNetworkCookie_path_const(const XNetworkCookie* self)
{
    return self ? self->m_path : NULL;
}

void XNetworkCookie_setSecure(XNetworkCookie* self, bool enabled)
{
    if (self) self->m_secure = enabled;
}

bool XNetworkCookie_isSecure(const XNetworkCookie* self)
{
    return self ? self->m_secure : false;
}

void XNetworkCookie_setHttpOnly(XNetworkCookie* self, bool enabled)
{
    if (self) self->m_httpOnly = enabled;
}

bool XNetworkCookie_isHttpOnly(const XNetworkCookie* self)
{
    return self ? self->m_httpOnly : false;
}

void XNetworkCookie_setSameSitePolicy(XNetworkCookie* self, XNetworkCookie_SameSite policy)
{
    if (self) self->m_sameSite = policy <= XNetworkCookie_SameSiteStrict ? policy : XNetworkCookie_SameSiteDefault;
}

XNetworkCookie_SameSite XNetworkCookie_sameSitePolicy(const XNetworkCookie* self)
{
    return self ? self->m_sameSite : XNetworkCookie_SameSiteDefault;
}

void XNetworkCookie_setExpirationSecs(XNetworkCookie* self, int64_t seconds)
{
    if (self) self->m_expirationSecs = seconds;
}

int64_t XNetworkCookie_expirationSecs(const XNetworkCookie* self)
{
    return self ? self->m_expirationSecs : -1;
}

void XNetworkCookie_setMaxAge(XNetworkCookie* self, int64_t seconds)
{
    if (self) {
        self->m_maxAge = seconds;
        if (seconds >= 0)
            self->m_expirationSecs = XDateTime_currentSecsSinceEpoch() + seconds;
    }
}

int64_t XNetworkCookie_maxAge(const XNetworkCookie* self)
{
    return self ? self->m_maxAge : -1;
}

bool XNetworkCookie_isSessionCookie(const XNetworkCookie* self)
{
    return self && self->m_expirationSecs < 0 && self->m_maxAge < 0;
}

bool XNetworkCookie_hasSameIdentifier(const XNetworkCookie* self, const XNetworkCookie* other)
{
    return self && other && xcookie_equal(self->m_name, other->m_name) &&
           xcookie_equal(self->m_domain, other->m_domain) && xcookie_equal(self->m_path, other->m_path);
}

XByteArray* XNetworkCookie_toRawForm(const XNetworkCookie* self, XNetworkCookie_RawForm form)
{
    XByteArray* result;
    char number[64];
    int written;
    if (!self || !self->m_name || !self->m_value)
        return NULL;
    result = XByteArray_create();
    if (!result || !xcookie_append(result, XByteArray_constData(self->m_name), XByteArray_size_base((XContainer*)self->m_name)) ||
        !XByteArray_append_utf8(result, "=") ||
        !xcookie_append(result, XByteArray_constData(self->m_value), XByteArray_size_base((XContainer*)self->m_value)))
        goto failed;
    if (form == XNetworkCookie_NameAndValueOnly)
        return result;
    if (self->m_domain && XByteArray_size_base((XContainer*)self->m_domain) != 0 &&
        (!XByteArray_append_utf8(result, "; Domain=") ||
         !xcookie_append(result, XByteArray_constData(self->m_domain), XByteArray_size_base((XContainer*)self->m_domain))))
        goto failed;
    if (self->m_path && XByteArray_size_base((XContainer*)self->m_path) != 0 &&
        (!XByteArray_append_utf8(result, "; Path=") ||
         !xcookie_append(result, XByteArray_constData(self->m_path), XByteArray_size_base((XContainer*)self->m_path))))
        goto failed;
    if (self->m_maxAge >= 0) {
        written = snprintf(number, sizeof(number), "; Max-Age=%lld", (long long)self->m_maxAge);
        if (written < 0 || !xcookie_append(result, number, (size_t)written)) goto failed;
    }
    if (self->m_secure && !XByteArray_append_utf8(result, "; Secure")) goto failed;
    if (self->m_httpOnly && !XByteArray_append_utf8(result, "; HttpOnly")) goto failed;
    if (self->m_sameSite == XNetworkCookie_SameSiteNone && !XByteArray_append_utf8(result, "; SameSite=None")) goto failed;
    if (self->m_sameSite == XNetworkCookie_SameSiteLax && !XByteArray_append_utf8(result, "; SameSite=Lax")) goto failed;
    if (self->m_sameSite == XNetworkCookie_SameSiteStrict && !XByteArray_append_utf8(result, "; SameSite=Strict")) goto failed;
    return result;
failed:
    if (result) XClass_delete_base((XClass*)result);
    return NULL;
}

XVector* XNetworkCookie_parseCookies(const XByteArray* cookieString)
{
    const uint8_t* data;
    size_t size;
    size_t firstEnd;
    size_t equal;
    XByteArray* name;
    XByteArray* value;
    XNetworkCookie* cookie;
    XVector* result;
    if (!cookieString)
        return NULL;
    data = XByteArray_constData((XByteArray*)cookieString);
    size = XByteArray_size_base((XContainer*)cookieString);
    firstEnd = 0;
    while (firstEnd < size && data[firstEnd] != ';') ++firstEnd;
    equal = 0;
    while (equal < firstEnd && data[equal] != '=') ++equal;
    if (equal == 0 || equal >= firstEnd)
        return NULL;
    name = xcookie_trim(data, 0, equal);
    value = xcookie_trim(data, equal + 1, firstEnd);
    cookie = name && value ? XNetworkCookie_create(name, value) : NULL;
    if (name) XClass_delete_base((XClass*)name);
    if (value) XClass_delete_base((XClass*)value);
    if (!cookie)
        return NULL;
    while (firstEnd < size) {
        size_t begin = ++firstEnd;
        size_t end;
        size_t attrEqual;
        XByteArray* attr;
        XByteArray* attrValue = NULL;
        while (firstEnd < size && data[firstEnd] != ';') ++firstEnd;
        end = firstEnd;
        attrEqual = begin;
        while (attrEqual < end && data[attrEqual] != '=') ++attrEqual;
        attr = xcookie_trim(data, begin, attrEqual < end ? attrEqual : end);
        if (attrEqual < end)
            attrValue = xcookie_trim(data, attrEqual + 1, end);
        if (attr && xcookie_equal_literal(attr, "Domain") && attrValue)
            XNetworkCookie_setDomain(cookie, attrValue);
        else if (attr && xcookie_equal_literal(attr, "Path") && attrValue)
            XNetworkCookie_setPath(cookie, attrValue);
        else if (attr && xcookie_equal_literal(attr, "Max-Age") && attrValue) {
            int64_t valueNumber;
            if (xcookie_parse_int(attrValue, &valueNumber)) XNetworkCookie_setMaxAge(cookie, valueNumber);
        } else if (attr && xcookie_equal_literal(attr, "Secure"))
            cookie->m_secure = true;
        else if (attr && xcookie_equal_literal(attr, "HttpOnly"))
            cookie->m_httpOnly = true;
        else if (attr && xcookie_equal_literal(attr, "SameSite") && attrValue) {
            if (xcookie_equal_literal(attrValue, "None")) cookie->m_sameSite = XNetworkCookie_SameSiteNone;
            else if (xcookie_equal_literal(attrValue, "Lax")) cookie->m_sameSite = XNetworkCookie_SameSiteLax;
            else if (xcookie_equal_literal(attrValue, "Strict")) cookie->m_sameSite = XNetworkCookie_SameSiteStrict;
        }
        if (attr) XClass_delete_base((XClass*)attr);
        if (attrValue) XClass_delete_base((XClass*)attrValue);
    }
    result = XVector_create(sizeof(XNetworkCookie*));
    if (!result || !XVector_push_back_1_base(result, &cookie)) {
        if (result) XClass_delete_base((XClass*)result);
        XClass_delete_base((XClass*)cookie);
        return NULL;
    }
    return result;
}

bool XNetworkCookie_normalize(XNetworkCookie* self, const XUrl* url)
{
    const XString* host;
    const XString* path;
    XByteArray* value;
    size_t pathSize;
    size_t slash;
    if (!self || !url)
        return false;
    host = XUrl_host_const(url);
    path = XUrl_path_const(url);
    if (!host || XContainer_size_base((const XContainer*)host) == 0)
        return false;
    if (!self->m_domain || XByteArray_size_base((XContainer*)self->m_domain) == 0) {
        value = XByteArray_create_with_data(XString_toUtf8(host), XString_toUtf8_length(host));
        if (!value || !xcookie_set_bytes(&self->m_domain, value)) {
            if (value) XClass_delete_base((XClass*)value);
            return false;
        }
        XClass_delete_base((XClass*)value);
    }
    if (!self->m_path || XByteArray_size_base((XContainer*)self->m_path) == 0) {
        pathSize = path ? XString_toUtf8_length(path) : 0;
        if (pathSize == 0 || XString_toUtf8(path)[0] != '/') {
            value = XByteArray_create_utf8("/");
        } else {
            const char* pathData = XString_toUtf8(path);
            slash = pathSize;
            while (slash > 0 && pathData[slash - 1] != '/') --slash;
            value = XByteArray_create_with_data(pathData, slash <= 1 ? 1 : slash);
        }
        if (!value || !xcookie_set_bytes(&self->m_path, value)) {
            if (value) XClass_delete_base((XClass*)value);
            return false;
        }
        XClass_delete_base((XClass*)value);
    }
    return true;
}

static XNetworkCookie* xcookie_at(const XNetworkCookieJar* self, size_t index)
{
    XNetworkCookie** slot;
    if (!self || !self->m_cookies)
        return NULL;
    slot = (XNetworkCookie**)XVector_at_base(self->m_cookies, (int64_t)index);
    return slot ? *slot : NULL;
}

XVtable* XNetworkCookieJar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkCookieJar)
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkCookieJar_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XNetworkCookieJar);
    return XVTABLE_DEFAULT;
}

void XNetworkCookieJar_init(XNetworkCookieJar* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XNetworkCookieJar);
    self->m_cookies = XVector_create(sizeof(XNetworkCookie*));
}

XNetworkCookieJar* XNetworkCookieJar_create_ex(XMemoryType memory)
{
    XNetworkCookieJar* self = (XNetworkCookieJar*)XMemory_malloc(sizeof(XNetworkCookieJar), memory);
    if (!self)
        return NULL;
    XNetworkCookieJar_init(self);
    if (!self->m_cookies) {
        XNetworkCookieJar_deinit_base((XClass*)self);
        XMemory_method(memory)->free(self);
        return NULL;
    }
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

static void VXNetworkCookieJar_deinit(XNetworkCookieJar* self)
{
    if (!self)
        return;
    if (self->m_cookies) {
        for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_cookies); ++i) {
            XNetworkCookie* cookie = xcookie_at(self, i);
            if (cookie) XClass_delete_base((XClass*)cookie);
        }
        XClass_delete_base((XClass*)self->m_cookies);
        self->m_cookies = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static int xcookie_find_identifier(const XNetworkCookieJar* self, const XNetworkCookie* cookie)
{
    if (!self || !cookie || !self->m_cookies)
        return -1;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_cookies); ++i)
        if (XNetworkCookie_hasSameIdentifier(xcookie_at(self, i), cookie)) return (int)i;
    return -1;
}

bool XNetworkCookieJar_deleteCookie(XNetworkCookieJar* self, const XNetworkCookie* cookie)
{
    int index = xcookie_find_identifier(self, cookie);
    if (index < 0)
        return false;
    XNetworkCookie* old = xcookie_at(self, (size_t)index);
    XVector_remove_base(self->m_cookies, index, 1);
    if (old) XClass_delete_base((XClass*)old);
    return true;
}

bool XNetworkCookieJar_insertCookie(XNetworkCookieJar* self, const XNetworkCookie* cookie)
{
    XNetworkCookie* copy;
    if (!self || !cookie || !cookie->m_name || XByteArray_size_base((XContainer*)cookie->m_name) == 0)
        return false;
    XNetworkCookieJar_deleteCookie(self, cookie);
    if ((!XNetworkCookie_isSessionCookie(cookie) && cookie->m_expirationSecs >= 0 &&
         cookie->m_expirationSecs <= XDateTime_currentSecsSinceEpoch()) || cookie->m_maxAge == 0)
        return true;
    copy = XNetworkCookie_create_copy(cookie);
    return copy && XVector_push_back_1_base(self->m_cookies, &copy)
        ? true : (copy ? (XClass_delete_base((XClass*)copy), false) : false);
}

bool XNetworkCookieJar_updateCookie(XNetworkCookieJar* self, const XNetworkCookie* cookie)
{
    return XNetworkCookieJar_insertCookie(self, cookie);
}

static bool xcookie_domain_match(const XByteArray* host, const XByteArray* domain)
{
    size_t hostSize;
    size_t domainSize;
    const uint8_t* hostData;
    const uint8_t* domainData;
    if (!host || !domain)
        return false;
    hostSize = XByteArray_size_base((XContainer*)host);
    domainSize = XByteArray_size_base((XContainer*)domain);
    hostData = XByteArray_constData((XByteArray*)host);
    domainData = XByteArray_constData((XByteArray*)domain);
    while (domainSize && domainData[0] == '.') { ++domainData; --domainSize; }
    if (hostSize < domainSize)
        return false;
    if (hostSize == domainSize) {
        for (size_t i = 0; i < hostSize; ++i) if (xcookie_lower(hostData[i]) != xcookie_lower(domainData[i])) return false;
        return true;
    }
    if (hostData[hostSize - domainSize - 1] != '.')
        return false;
    for (size_t i = 0; i < domainSize; ++i)
        if (xcookie_lower(hostData[hostSize - domainSize + i]) != xcookie_lower(domainData[i])) return false;
    return true;
}

static bool xcookie_path_match(const XByteArray* requestPath, const XByteArray* cookiePath)
{
    size_t requestSize = requestPath ? XByteArray_size_base((XContainer*)requestPath) : 0;
    size_t cookieSize = cookiePath ? XByteArray_size_base((XContainer*)cookiePath) : 0;
    const uint8_t* requestData = requestPath ? XByteArray_constData((XByteArray*)requestPath) : NULL;
    const uint8_t* cookieData = cookiePath ? XByteArray_constData((XByteArray*)cookiePath) : NULL;
    if (!cookieData || cookieSize == 0) return true;
    if (!requestData || requestSize < cookieSize || memcmp(requestData, cookieData, cookieSize) != 0) return false;
    return requestSize == cookieSize || cookieData[cookieSize - 1] == '/' || requestData[cookieSize] == '/';
}

XVector* XNetworkCookieJar_cookiesForUrl(const XNetworkCookieJar* self, const XUrl* url)
{
    XVector* result;
    XByteArray* host;
    XByteArray* path;
    const XString* scheme;
    bool https;
    if (!self || !url)
        return NULL;
    host = XByteArray_create_with_data(XString_toUtf8(XUrl_host_const(url)), XString_toUtf8_length(XUrl_host_const(url)));
    path = XByteArray_create_with_data(XString_toUtf8(XUrl_path_const(url)), XString_toUtf8_length(XUrl_path_const(url)));
    result = XVector_create(sizeof(XNetworkCookie*));
    scheme = XUrl_scheme_const(url);
    https = scheme && XString_equals_utf8(scheme, "https", XChar_CaseInsensitive);
    if (!host || !path || !result) goto failed;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_cookies); ++i) {
        XNetworkCookie* cookie = xcookie_at(self, i);
        XNetworkCookie* copy;
        if (!cookie || (cookie->m_secure && !https) ||
            (cookie->m_expirationSecs >= 0 && cookie->m_expirationSecs <= XDateTime_currentSecsSinceEpoch()) ||
            !xcookie_domain_match(host, cookie->m_domain) || !xcookie_path_match(path, cookie->m_path))
            continue;
        copy = XNetworkCookie_create_copy(cookie);
        if (!copy || !XVector_push_back_1_base(result, &copy)) {
            if (copy) XClass_delete_base((XClass*)copy);
            goto failed;
        }
    }
    XClass_delete_base((XClass*)host);
    XClass_delete_base((XClass*)path);
    return result;
failed:
    if (host) XClass_delete_base((XClass*)host);
    if (path) XClass_delete_base((XClass*)path);
    if (result) {
        for (size_t i = 0; i < XContainer_size_base((const XContainer*)result); ++i) {
            XNetworkCookie** slot = (XNetworkCookie**)XVector_at_base(result, (int64_t)i);
            if (slot && *slot) XClass_delete_base((XClass*)*slot);
        }
        XClass_delete_base((XClass*)result);
    }
    return NULL;
}

bool XNetworkCookieJar_setCookiesFromUrl(XNetworkCookieJar* self, const XVector* values, const XUrl* url)
{
    bool changed = false;
    if (!self || !values || !url)
        return false;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)values); ++i) {
        XByteArray** slot = (XByteArray**)XVector_at_base(values, (int64_t)i);
        XVector* parsed = slot && *slot ? XNetworkCookie_parseCookies(*slot) : NULL;
        if (parsed) {
            for (size_t j = 0; j < XContainer_size_base((const XContainer*)parsed); ++j) {
                XNetworkCookie** cookieSlot = (XNetworkCookie**)XVector_at_base(parsed, (int64_t)j);
                if (cookieSlot && *cookieSlot && XNetworkCookie_normalize(*cookieSlot, url)) {
                    if (XNetworkCookieJar_insertCookie(self, *cookieSlot)) changed = true;
                }
            }
            for (size_t j = 0; j < XContainer_size_base((const XContainer*)parsed); ++j) {
                XNetworkCookie** cookieSlot = (XNetworkCookie**)XVector_at_base(parsed, (int64_t)j);
                if (cookieSlot && *cookieSlot) XClass_delete_base((XClass*)*cookieSlot);
            }
            XClass_delete_base((XClass*)parsed);
        }
    }
    return changed;
}

bool XNetworkCookieJar_setCookiesFromHeaders(XNetworkCookieJar* self, const XHttpHeaders* headers, const XUrl* url)
{
    XVector* values;
    bool result;
    XByteArray* name;
    if (!self || !headers || !url)
        return false;
    values = XVector_create(sizeof(XByteArray*));
    name = XByteArray_create_utf8("Set-Cookie");
    if (!values || !name) {
        if (values) XClass_delete_base((XClass*)values);
        if (name) XClass_delete_base((XClass*)name);
        return false;
    }
    for (size_t i = 0; i < XHttpHeaders_size(headers); ++i) {
        const XByteArray* fieldName = XHttpHeaders_nameAt_const(headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(headers, i);
        XByteArray* copy;
        if (!fieldName || !value || !xcookie_equal(fieldName, name)) continue;
        copy = XByteArray_create_copy(value);
        if (!copy || !XVector_push_back_1_base(values, &copy)) {
            if (copy) XClass_delete_base((XClass*)copy);
            continue;
        }
    }
    result = XNetworkCookieJar_setCookiesFromUrl(self, values, url);
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)values); ++i) {
        XByteArray** slot = (XByteArray**)XVector_at_base(values, (int64_t)i);
        if (slot && *slot) XClass_delete_base((XClass*)*slot);
    }
    XClass_delete_base((XClass*)values);
    XClass_delete_base((XClass*)name);
    return result;
}

XByteArray* XNetworkCookieJar_cookieHeader(const XNetworkCookieJar* self, const XUrl* url)
{
    XVector* cookies;
    XByteArray* result;
    if (!self || !url)
        return NULL;
    cookies = XNetworkCookieJar_cookiesForUrl(self, url);
    result = XByteArray_create();
    if (!cookies || !result) goto failed;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)cookies); ++i) {
        XNetworkCookie** slot = (XNetworkCookie**)XVector_at_base(cookies, (int64_t)i);
        XByteArray* raw = slot && *slot ? XNetworkCookie_toRawForm(*slot, XNetworkCookie_NameAndValueOnly) : NULL;
        if (!raw || (i != 0 && !XByteArray_append_utf8(result, "; ")) ||
            !xcookie_append(result, XByteArray_constData(raw), XByteArray_size_base((XContainer*)raw))) {
            if (raw) XClass_delete_base((XClass*)raw);
            goto failed;
        }
        XClass_delete_base((XClass*)raw);
    }
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)cookies); ++i) {
        XNetworkCookie** slot = (XNetworkCookie**)XVector_at_base(cookies, (int64_t)i);
        if (slot && *slot) XClass_delete_base((XClass*)*slot);
    }
    XClass_delete_base((XClass*)cookies);
    return result;
failed:
    if (result) XClass_delete_base((XClass*)result);
    if (cookies) {
        for (size_t i = 0; i < XContainer_size_base((const XContainer*)cookies); ++i) {
            XNetworkCookie** slot = (XNetworkCookie**)XVector_at_base(cookies, (int64_t)i);
            if (slot && *slot) XClass_delete_base((XClass*)*slot);
        }
        XClass_delete_base((XClass*)cookies);
    }
    return NULL;
}

size_t XNetworkCookieJar_size(const XNetworkCookieJar* self)
{
    return self && self->m_cookies ? XContainer_size_base((const XContainer*)self->m_cookies) : 0;
}
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
