#include "XHttpHeaders.h"

#include "XMemory.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char* const xhttp_well_known_header_names[] = {
    "a-im", "accept", "accept-additions", "accept-ch", "accept-datetime",
    "accept-encoding", "accept-features", "accept-language", "accept-patch",
    "accept-post", "accept-ranges", "accept-signature", "access-control-allow-credentials",
    "access-control-allow-headers", "access-control-allow-methods", "access-control-allow-origin",
    "access-control-expose-headers", "access-control-max-age", "access-control-request-headers",
    "access-control-request-method", "age", "allow", "alpn", "alt-svc", "alt-used",
    "alternates", "apply-to-redirect-ref", "authentication-control", "authentication-info",
    "authorization", "cache-control", "cache-status", "cal-managed-id", "caldav-timezones",
    "capsule-protocol", "cdn-cache-control", "cdn-loop", "cert-not-after", "cert-not-before",
    "clear-site-data", "client-cert", "client-cert-chain", "close", "connection", "content-digest",
    "content-disposition", "content-encoding", "content-id", "content-language", "content-length",
    "content-location", "content-range", "content-security-policy", "content-security-policy-report-only",
    "content-type", "cookie", "cross-origin-embedder-policy", "cross-origin-embedder-policy-report-only",
    "cross-origin-opener-policy", "cross-origin-opener-policy-report-only", "cross-origin-resource-policy",
    "dasl", "date", "dav", "delta-base", "depth", "destination", "differential-id", "dpop",
    "dpop-nonce", "early-data", "etag", "expect", "expect-ct", "expires", "forwarded", "from",
    "hobareg", "host", "if", "if-match", "if-modified-since", "if-none-match", "if-range",
    "if-schedule-tag-match", "if-unmodified-since", "im", "include-referred-token-binding-id",
    "keep-alive", "label", "last-event-id", "last-modified", "link", "location", "lock-token",
    "max-forwards", "memento-datetime", "meter", "mime-version", "negotiate", "nel",
    "odata-entityid", "odata-isolation", "odata-maxversion", "odata-version", "optional-www-authenticate",
    "ordering-type", "origin", "origin-agent-cluster", "oscore", "oslc-core-version", "overwrite",
    "ping-from", "ping-to", "position", "prefer", "preference-applied", "priority",
    "proxy-authenticate", "proxy-authentication-info", "proxy-authorization", "proxy-status",
    "public-key-pins", "public-key-pins-report-only", "range", "redirect-ref", "referer", "refresh",
    "replay-nonce", "repr-digest", "retry-after", "schedule-reply", "schedule-tag", "sec-purpose",
    "sec-token-binding", "sec-websocket-accept", "sec-websocket-extensions", "sec-websocket-key",
    "sec-websocket-protocol", "sec-websocket-version", "server", "server-timing", "set-cookie",
    "signature", "signature-input", "slug", "soapaction", "status-uri", "strict-transport-security",
    "sunset", "surrogate-capability", "surrogate-control", "tcn", "te", "timeout", "topic",
    "traceparent", "tracestate", "trailer", "transfer-encoding", "ttl", "upgrade", "urgency",
    "user-agent", "variant-vary", "vary", "via", "want-content-digest", "want-repr-digest",
    "www-authenticate", "x-content-type-options", "x-frame-options", "accept-charset", "c-pep-info",
    "pragma", "protocol-info", "protocol-query"
};

static const char* xhttp_well_known_header_name(XHttpHeaders_WellKnownHeader name)
{
    if ((int)name < 0 || name >= XHttpHeaders_WellKnownHeader_Count)
        return NULL;
    return xhttp_well_known_header_names[(size_t)name];
}

static XByteArray* xhttp_well_known_header_name_array(XHttpHeaders_WellKnownHeader name)
{
    const char* text = xhttp_well_known_header_name(name);
    return text ? XByteArray_create_utf8(text) : NULL;
}

_Static_assert(sizeof(xhttp_well_known_header_names) / sizeof(xhttp_well_known_header_names[0]) ==
                   XHttpHeaders_WellKnownHeader_Count,
               "QHttpHeaders WellKnownHeader mapping must match the public enum");

static size_t xhttp_byte_size(const XByteArray* array)
{
    return array ? XContainer_size_base((const XContainer*)array) : 0;
}

static bool xhttp_is_token_byte(unsigned char byte)
{
    return ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9')) || byte == '!' || byte == '#' || byte == '$' ||
           byte == '%' || byte == '&' || byte == '\'' || byte == '*' ||
           byte == '+' || byte == '-' || byte == '.' || byte == '^' ||
           byte == '_' || byte == '`' || byte == '|' || byte == '~';
}

static bool xhttp_is_valid_name(const XByteArray* name)
{
    if (!name || xhttp_byte_size(name) == 0)
        return false;

    const unsigned char* data = XByteArray_constData((XByteArray*)name);
    size_t size = xhttp_byte_size(name);
    if (!data)
        return false;
    for (size_t i = 0; i < size; ++i) {
        if (!xhttp_is_token_byte(data[i]))
            return false;
    }
    return true;
}

static bool xhttp_is_valid_value(const XByteArray* value)
{
    if (!value)
        return false;

    const unsigned char* data = XByteArray_constData((XByteArray*)value);
    size_t size = xhttp_byte_size(value);
    if (!data && size != 0)
        return false;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\r' || data[i] == '\n' || data[i] == 0 ||
            (data[i] < 0x20 && data[i] != '\t') || data[i] == 0x7f)
            return false;
    }
    return true;
}

static bool xhttp_name_equals(const XByteArray* left, const XByteArray* right)
{
    if (!left || !right || xhttp_byte_size(left) != xhttp_byte_size(right))
        return false;

    const unsigned char* leftData = XByteArray_constData((XByteArray*)left);
    const unsigned char* rightData = XByteArray_constData((XByteArray*)right);
    size_t size = xhttp_byte_size(left);
    if ((!leftData || !rightData) && size != 0)
        return false;
    for (size_t i = 0; i < size; ++i) {
        if (tolower(leftData[i]) != tolower(rightData[i]))
            return false;
    }
    return true;
}

static void xhttp_field_deinit(XHttpHeaderField* field)
{
    if (!field)
        return;
    if (field->m_name) {
        XClass_delete_base((XClass*)field->m_name);
        field->m_name = NULL;
    }
    if (field->m_value) {
        XClass_delete_base((XClass*)field->m_value);
        field->m_value = NULL;
    }
}

static bool xhttp_field_assign(XHttpHeaderField* field, const XByteArray* name, const XByteArray* value)
{
    if (!field || !xhttp_is_valid_name(name) || !xhttp_is_valid_value(value))
        return false;

    /* 头名称随后会原地规范化为小写，必须使用独立存储，不能与源 QByteArray 共享 COW 数据。 */
    XByteArray* newName = XByteArray_create_with_data(
        (const char*)XByteArray_constData((XByteArray*)name), xhttp_byte_size(name));
    XByteArray* newValue = XByteArray_trimmed(value);
    if (!newName || !newValue) {
        if (newName)
            XClass_delete_base((XClass*)newName);
        if (newValue)
            XClass_delete_base((XClass*)newValue);
        return false;
    }
    XByteArray_toLower(newName);
    xhttp_field_deinit(field);
    field->m_name = newName;
    field->m_value = newValue;
    return true;
}

static bool xhttp_reserve(XHttpHeaders* self, size_t minimum)
{
    if (self->m_capacity >= minimum)
        return true;

    size_t capacity = self->m_capacity ? self->m_capacity : 8;
    while (capacity < minimum) {
        if (capacity > (SIZE_MAX / 2)) {
            capacity = minimum;
            break;
        }
        capacity *= 2;
    }
    if (capacity > (SIZE_MAX / sizeof(XHttpHeaderField)))
        return false;

    XHttpHeaderField* fields = (XHttpHeaderField*)XRealloc_System(
        self->m_fields, capacity * sizeof(XHttpHeaderField));
    if (!fields)
        return false;
    memset(fields + self->m_capacity, 0,
           (capacity - self->m_capacity) * sizeof(XHttpHeaderField));
    self->m_fields = fields;
    self->m_capacity = capacity;
    return true;
}

static void VXHttpHeaders_deinit(XHttpHeaders* self)
{
    if (!self)
        return;
    XHttpHeaders_clear(self);
    if (self->m_fields) {
        XFree_System(self->m_fields);
        self->m_fields = NULL;
    }
    self->m_capacity = 0;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttpHeaders_copy(XHttpHeaders* dest, const XHttpHeaders* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpHeaders_init(dest);

    XHttpHeaders_clear(dest);
    if (!xhttp_reserve(dest, src->m_size))
        return;
    for (size_t i = 0; i < src->m_size; ++i) {
        if (!xhttp_field_assign(&dest->m_fields[dest->m_size],
                                src->m_fields[i].m_name, src->m_fields[i].m_value)) {
            XHttpHeaders_clear(dest);
            return;
        }
        ++dest->m_size;
    }
}

static void VXHttpHeaders_move(XHttpHeaders* dest, XHttpHeaders* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XHttpHeaders_init(dest);

    XHttpHeaders_clear(dest);
    if (dest->m_fields) {
        XFree_System(dest->m_fields);
        dest->m_fields = NULL;
    }
    dest->m_fields = src->m_fields;
    dest->m_size = src->m_size;
    dest->m_capacity = src->m_capacity;
    src->m_fields = NULL;
    src->m_size = 0;
    src->m_capacity = 0;
}

XVtable* XHttpHeaders_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHttpHeaders))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpHeaders_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttpHeaders_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttpHeaders_move);
    return XVTABLE_DEFAULT;
}

void XHttpHeaders_init(XHttpHeaders* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpHeaders);
}

XHttpHeaders* XHttpHeaders_create(void)
{
    XHttpHeaders* self = (XHttpHeaders*)XMalloc_System(sizeof(XHttpHeaders));
    if (!self)
        return NULL;
    XHttpHeaders_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttpHeaders* XHttpHeaders_create_copy(const XHttpHeaders* other)
{
    if (!other)
        return NULL;
    XHttpHeaders* self = XHttpHeaders_create();
    if (!self)
        return NULL;
    XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XHttpHeaders* XHttpHeaders_create_move(XHttpHeaders* other)
{
    if (!other)
        return NULL;
    XHttpHeaders* self = XHttpHeaders_create();
    if (!self)
        return NULL;
    XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

bool XHttpHeaders_append(XHttpHeaders* self, const XByteArray* name, const XByteArray* value)
{
    return XHttpHeaders_insert(self, self ? self->m_size : 0, name, value);
}

bool XHttpHeaders_append_utf8(XHttpHeaders* self, const char* name, const char* value)
{
    if (!self || !name)
        return false;
    XByteArray* byteName = XByteArray_create_utf8(name);
    XByteArray* byteValue = XByteArray_create_utf8(value ? value : "");
    bool result = byteName && byteValue && XHttpHeaders_append(self, byteName, byteValue);
    if (byteName)
        XClass_delete_base((XClass*)byteName);
    if (byteValue)
        XClass_delete_base((XClass*)byteValue);
    return result;
}

bool XHttpHeaders_appendKnown(XHttpHeaders* self,
                              XHttpHeaders_WellKnownHeader name,
                              const XByteArray* value)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    bool result = fieldName && XHttpHeaders_append(self, fieldName, value);
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

bool XHttpHeaders_insert(XHttpHeaders* self, size_t index, const XByteArray* name, const XByteArray* value)
{
    if (!self || index > self->m_size || !xhttp_is_valid_name(name) || !xhttp_is_valid_value(value))
        return false;
    if (!xhttp_reserve(self, self->m_size + 1))
        return false;

    XHttpHeaderField field = { NULL, NULL };
    if (!xhttp_field_assign(&field, name, value))
        return false;
    if (index < self->m_size) {
        memmove(&self->m_fields[index + 1], &self->m_fields[index],
                (self->m_size - index) * sizeof(XHttpHeaderField));
    }
    self->m_fields[index] = field;
    ++self->m_size;
    return true;
}

bool XHttpHeaders_insertKnown(XHttpHeaders* self,
                              size_t index,
                              XHttpHeaders_WellKnownHeader name,
                              const XByteArray* value)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    bool result = fieldName && XHttpHeaders_insert(self, index, fieldName, value);
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

bool XHttpHeaders_replace(XHttpHeaders* self, size_t index, const XByteArray* name, const XByteArray* value)
{
    if (!self || index >= self->m_size)
        return false;
    return xhttp_field_assign(&self->m_fields[index], name, value);
}

bool XHttpHeaders_replaceKnown(XHttpHeaders* self,
                               size_t index,
                               XHttpHeaders_WellKnownHeader name,
                               const XByteArray* value)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    bool result = fieldName && XHttpHeaders_replace(self, index, fieldName, value);
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

bool XHttpHeaders_replaceOrAppend(XHttpHeaders* self, const XByteArray* name, const XByteArray* value)
{
    if (!self || !xhttp_is_valid_name(name) || !xhttp_is_valid_value(value))
        return false;
    for (size_t i = 0; i < self->m_size; ++i) {
        if (xhttp_name_equals(self->m_fields[i].m_name, name)) {
            if (!XHttpHeaders_replace(self, i, name, value))
                return false;
            for (size_t duplicate = self->m_size; duplicate > i + 1; --duplicate) {
                if (xhttp_name_equals(self->m_fields[duplicate - 1].m_name, name))
                    XHttpHeaders_removeAt(self, duplicate - 1);
            }
            return true;
        }
    }
    return XHttpHeaders_append(self, name, value);
}

bool XHttpHeaders_replaceOrAppendKnown(XHttpHeaders* self,
                                       XHttpHeaders_WellKnownHeader name,
                                       const XByteArray* value)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    bool result = fieldName && XHttpHeaders_replaceOrAppend(self, fieldName, value);
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

bool XHttpHeaders_contains(const XHttpHeaders* self, const XByteArray* name)
{
    return XHttpHeaders_value_const(self, name) != NULL;
}

bool XHttpHeaders_containsKnown(const XHttpHeaders* self, XHttpHeaders_WellKnownHeader name)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    bool result = fieldName && XHttpHeaders_contains(self, fieldName);
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

const XByteArray* XHttpHeaders_value_const(const XHttpHeaders* self, const XByteArray* name)
{
    if (!self || !name)
        return NULL;
    for (size_t i = 0; i < self->m_size; ++i) {
        if (xhttp_name_equals(self->m_fields[i].m_name, name))
            return self->m_fields[i].m_value;
    }
    return NULL;
}

XByteArray* XHttpHeaders_value(const XHttpHeaders* self, const XByteArray* name)
{
    const XByteArray* value = XHttpHeaders_value_const(self, name);
    return value ? XByteArray_create_copy(value) : NULL;
}

XByteArray* XHttpHeaders_valueKnown(const XHttpHeaders* self,
                                    XHttpHeaders_WellKnownHeader name)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    XByteArray* result = fieldName ? XHttpHeaders_value(self, fieldName) : NULL;
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

XByteArray* XHttpHeaders_value_or(const XHttpHeaders* self,
                                  const XByteArray* name,
                                  const XByteArray* defaultValue)
{
    const XByteArray* value = XHttpHeaders_value_const(self, name);
    if (value)
        return XByteArray_create_copy(value);
    return defaultValue ? XByteArray_create_copy(defaultValue) : XByteArray_create();
}

XByteArray* XHttpHeaders_valueKnownOr(const XHttpHeaders* self,
                                      XHttpHeaders_WellKnownHeader name,
                                      const XByteArray* defaultValue)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    XByteArray* result = fieldName ? XHttpHeaders_value_or(self, fieldName, defaultValue) :
                                     (defaultValue ? XByteArray_create_copy(defaultValue) : XByteArray_create());
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

XVector* XHttpHeaders_values(const XHttpHeaders* self, const XByteArray* name)
{
    XVector* values = XVector_create(sizeof(XByteArray*));
    if (!values)
        return NULL;
    if (!self || !name)
        return values;
    for (size_t i = 0; i < self->m_size; ++i) {
        XByteArray* value;
        if (!xhttp_name_equals(self->m_fields[i].m_name, name))
            continue;
        value = XByteArray_create_copy(self->m_fields[i].m_value);
        if (!value || !XVector_push_back_1_base(values, &value)) {
            if (value)
                XClass_delete_base((XClass*)value);
            XHttpHeaders_values_free(values);
            return NULL;
        }
    }
    return values;
}

XVector* XHttpHeaders_valuesKnown(const XHttpHeaders* self,
                                  XHttpHeaders_WellKnownHeader name)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    XVector* result = fieldName ? XHttpHeaders_values(self, fieldName) : XVector_create(sizeof(XByteArray*));
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

void XHttpHeaders_values_free(XVector* values)
{
    if (!values)
        return;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)values); ++i) {
        XByteArray** value = (XByteArray**)XVector_at_base(values, i);
        if (value && *value)
            XClass_delete_base((XClass*)*value);
    }
    XClass_delete_base((XClass*)values);
}

XByteArray* XHttpHeaders_combinedValue(const XHttpHeaders* self, const XByteArray* name)
{
    if (!self || !name)
        return NULL;
    XByteArray* result = NULL;
    for (size_t i = 0; i < self->m_size; ++i) {
        if (!xhttp_name_equals(self->m_fields[i].m_name, name))
            continue;
        if (!result) {
            result = XByteArray_create_copy(self->m_fields[i].m_value);
            if (!result)
                return NULL;
        } else {
            static const char separator[] = ", ";
            XByteArray_push_back_2((XVector*)result, separator, sizeof(separator) - 1);
            XByteArray_push_back_2((XVector*)result, XByteArray_constData(self->m_fields[i].m_value),
                                    xhttp_byte_size(self->m_fields[i].m_value));
        }
    }
    return result;
}

XByteArray* XHttpHeaders_combinedValueKnown(const XHttpHeaders* self,
                                            XHttpHeaders_WellKnownHeader name)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    XByteArray* result = fieldName ? XHttpHeaders_combinedValue(self, fieldName) : NULL;
    if (fieldName)
        XClass_delete_base((XClass*)fieldName);
    return result;
}

const char* XHttpHeaders_wellKnownHeaderName(XHttpHeaders_WellKnownHeader name)
{
    return xhttp_well_known_header_name(name);
}

const XByteArray* XHttpHeaders_nameAt_const(const XHttpHeaders* self, size_t index)
{
    return self && index < self->m_size ? self->m_fields[index].m_name : NULL;
}

const XByteArray* XHttpHeaders_valueAt_const(const XHttpHeaders* self, size_t index)
{
    return self && index < self->m_size ? self->m_fields[index].m_value : NULL;
}

void XHttpHeaders_removeAt(XHttpHeaders* self, size_t index)
{
    if (!self || index >= self->m_size)
        return;
    xhttp_field_deinit(&self->m_fields[index]);
    if (index + 1 < self->m_size) {
        memmove(&self->m_fields[index], &self->m_fields[index + 1],
                (self->m_size - index - 1) * sizeof(XHttpHeaderField));
    }
    --self->m_size;
    memset(&self->m_fields[self->m_size], 0, sizeof(XHttpHeaderField));
}

void XHttpHeaders_removeAll(XHttpHeaders* self, const XByteArray* name)
{
    if (!self || !name)
        return;
    for (size_t i = self->m_size; i > 0; --i) {
        if (xhttp_name_equals(self->m_fields[i - 1].m_name, name))
            XHttpHeaders_removeAt(self, i - 1);
    }
}

void XHttpHeaders_removeAllKnown(XHttpHeaders* self, XHttpHeaders_WellKnownHeader name)
{
    XByteArray* fieldName = xhttp_well_known_header_name_array(name);
    if (fieldName) {
        XHttpHeaders_removeAll(self, fieldName);
        XClass_delete_base((XClass*)fieldName);
    }
}

void XHttpHeaders_clear(XHttpHeaders* self)
{
    if (!self)
        return;
    for (size_t i = 0; i < self->m_size; ++i)
        xhttp_field_deinit(&self->m_fields[i]);
    self->m_size = 0;
}

size_t XHttpHeaders_size(const XHttpHeaders* self)
{
    return self ? self->m_size : 0;
}

bool XHttpHeaders_isEmpty(const XHttpHeaders* self)
{
    return !self || self->m_size == 0;
}

bool XHttpHeaders_reserve(XHttpHeaders* self, size_t capacity)
{
    return self && xhttp_reserve(self, capacity);
}

void XHttpHeaders_swap(XHttpHeaders* lhs, XHttpHeaders* rhs)
{
    XHttpHeaderField* fields;
    size_t size;
    size_t capacity;
    if (!lhs || !rhs || lhs == rhs)
        return;
    fields = lhs->m_fields;
    lhs->m_fields = rhs->m_fields;
    rhs->m_fields = fields;
    size = lhs->m_size;
    lhs->m_size = rhs->m_size;
    rhs->m_size = size;
    capacity = lhs->m_capacity;
    lhs->m_capacity = rhs->m_capacity;
    rhs->m_capacity = capacity;
}
