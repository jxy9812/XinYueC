/**
 * @file       XHttpReply.c
 * @brief      HTTP/1.1 响应的增量解析实现。
 */

#include "XHttpReply.h"

#include "XMemory.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void VXHttpReply_deinit(XHttpReply* self);

static size_t xhttp_reply_size(const XByteArray* array)
{
    return array ? XContainer_size_base((const XContainer*)array) : 0;
}

static const uint8_t* xhttp_reply_data(const XByteArray* array)
{
    return array ? XByteArray_constData((XByteArray*)array) : NULL;
}

static bool xhttp_reply_append(XByteArray* target, const uint8_t* data, size_t size)
{
    if (!target || (!data && size != 0))
        return false;
    if (size == 0)
        return true;
    return XByteArray_push_back_2((XVector*)target, data, size);
}

static XByteArray* xhttp_reply_slice(const uint8_t* data, size_t start, size_t end)
{
    if (!data || end < start)
        return NULL;
    if (end == start)
        return XByteArray_create();
    return XByteArray_create_with_data((const char*)data + start, end - start);
}

static void xhttp_reply_delete_class(void* object)
{
    if (object)
        XClass_delete_base((XClass*)object);
}

static bool xhttp_reply_find_crlf(const uint8_t* data, size_t start, size_t size, size_t* end)
{
    if (!data || !end || start > size)
        return false;
    for (size_t i = start; i + 1 < size; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            *end = i;
            return true;
        }
    }
    return false;
}

static bool xhttp_reply_find_header_end(const uint8_t* data, size_t start, size_t size, size_t* end)
{
    if (!data || !end || start > size)
        return false;
    for (size_t i = start; i + 3 < size; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            *end = i;
            return true;
        }
    }
    return false;
}

static bool xhttp_reply_ascii_equal(const uint8_t* data, size_t size, const char* text)
{
    if (!data || !text)
        return false;
    size_t textSize = strlen(text);
    if (size != textSize)
        return false;
    for (size_t i = 0; i < size; ++i) {
        if ((char)tolower((unsigned char)data[i]) !=
            (char)tolower((unsigned char)text[i]))
            return false;
    }
    return true;
}

static bool xhttp_reply_name_equal(const XByteArray* left, const XByteArray* right)
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

static bool xhttp_reply_header_name_is(const XByteArray* name, const char* text)
{
    return xhttp_reply_ascii_equal(xhttp_reply_data(name), xhttp_reply_size(name), text);
}

static bool xhttp_reply_value_has_token(const XByteArray* value, const char* token)
{
    const uint8_t* data = xhttp_reply_data(value);
    size_t size = xhttp_reply_size(value);
    size_t tokenSize = token ? strlen(token) : 0;
    size_t start = 0;
    if (!data || tokenSize == 0)
        return false;
    while (start < size) {
        while (start < size && (data[start] == ' ' || data[start] == '\t' || data[start] == ','))
            ++start;
        size_t end = start;
        while (end < size && data[end] != ',')
            ++end;
        while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t'))
            --end;
        if (xhttp_reply_ascii_equal(data + start, end - start, token))
            return true;
        start = end < size ? end + 1 : end;
    }
    return false;
}

static bool xhttp_reply_parse_decimal(const XByteArray* value, size_t* result)
{
    const uint8_t* data = xhttp_reply_data(value);
    size_t size = xhttp_reply_size(value);
    size_t begin = 0;
    size_t end = size;
    size_t number = 0;
    if (!data || !result)
        return false;
    while (begin < end && (data[begin] == ' ' || data[begin] == '\t'))
        ++begin;
    while (end > begin && (data[end - 1] == ' ' || data[end - 1] == '\t'))
        --end;
    if (begin == end)
        return false;
    for (size_t i = begin; i < end; ++i) {
        if (data[i] < '0' || data[i] > '9')
            return false;
        size_t digit = (size_t)(data[i] - '0');
        if (number > (SIZE_MAX - digit) / 10)
            return false;
        number = number * 10 + digit;
    }
    *result = number;
    return true;
}

static bool xhttp_reply_parse_hex(const uint8_t* data, size_t size, size_t* result)
{
    size_t number = 0;
    bool haveDigit = false;
    if (!data || !result)
        return false;
    for (size_t i = 0; i < size; ++i) {
        uint8_t c = data[i];
        size_t digit;
        if (c == ';' || c == ' ' || c == '\t')
            break;
        if (c >= '0' && c <= '9') digit = (size_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (size_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (size_t)(c - 'A' + 10);
        else return false;
        if (number > (SIZE_MAX - digit) / 16)
            return false;
        number = number * 16 + digit;
        haveDigit = true;
    }
    *result = number;
    return haveDigit;
}

static void xhttp_reply_replace_error(XHttpReply* self, const char* text)
{
    XString* replacement = text ? XString_create_utf8(text) : NULL;
    if (self->m_errorString)
        XClass_delete_base((XClass*)self->m_errorString);
    self->m_errorString = replacement;
}

static void xhttp_reply_finish(XHttpReply* self)
{
    if (!self || self->m_state == XHttpReply_Finished)
        return;
    self->m_state = XHttpReply_Finished;
    if ((self->m_deferFinished && self->m_error == XHttpReply_NoError) ||
        self->m_authenticationPending)
        self->m_finishedPending = true;
    else
        XHttpReply_finished_signal(self);
}

static void xhttp_reply_fail(XHttpReply* self, XHttpReply_NetworkError error, const char* text)
{
    if (!self || self->m_state == XHttpReply_Finished)
        return;
    self->m_error = error;
    xhttp_reply_replace_error(self, text);
    if (!self->m_errorEmitted) {
        self->m_errorEmitted = true;
        XHttpReply_errorOccurred_signal(self, error);
    }
    xhttp_reply_finish(self);
}

static bool xhttp_reply_http2_name_equal(const XByteArray* name, const char* text)
{
    size_t size = text ? strlen(text) : 0;
    return name && text && xhttp_reply_size(name) == size &&
           (size == 0 || memcmp(xhttp_reply_data(name), text, size) == 0);
}

static bool xhttp_reply_http2_is_pseudo_header(const XByteArray* name)
{
    const uint8_t* data = xhttp_reply_data(name);
    return xhttp_reply_size(name) != 0 && data && data[0] == ':';
}

static bool xhttp_reply_http2_is_forbidden_header(const XByteArray* name,
                                                   const XByteArray* value)
{
    /* RFC 7540 8.1.2.2：连接专用字段不能出现在 HTTP/2 头块中。 */
    if (xhttp_reply_http2_name_equal(name, "connection") ||
        xhttp_reply_http2_name_equal(name, "keep-alive") ||
        xhttp_reply_http2_name_equal(name, "proxy-connection") ||
        xhttp_reply_http2_name_equal(name, "transfer-encoding") ||
        xhttp_reply_http2_name_equal(name, "upgrade"))
        return true;
    return xhttp_reply_http2_name_equal(name, "te") &&
           !xhttp_reply_http2_name_equal(value, "trailers");
}

static bool xhttp_reply_http2_status(const XByteArray* value, int* status)
{
    const uint8_t* data;
    if (!value || !status || xhttp_reply_size(value) != 3)
        return false;
    data = xhttp_reply_data(value);
    if (!data || data[0] < '0' || data[0] > '9' || data[1] < '0' || data[1] > '9' ||
        data[2] < '0' || data[2] > '9')
        return false;
    *status = (int)(data[0] - '0') * 100 + (int)(data[1] - '0') * 10 +
              (int)(data[2] - '0');
    return true;
}

static void xhttp_reply_set_status_error(XHttpReply* self)
{
    XHttpReply_NetworkError error = XHttpReply_NoError;
    switch (self->m_statusCode) {
    case 401:
    case 407:
        if (self->m_deferAuthentication) {
            self->m_authenticationPending = true;
            return;
        }
        error = XHttpReply_AuthenticationRequiredError;
        break;
    case 403: error = XHttpReply_ContentAccessDenied; break;
    case 404: error = XHttpReply_ContentNotFoundError; break;
    case 405:
    case 501: error = XHttpReply_OperationNotImplementedError; break;
    case 500: error = XHttpReply_InternalServerError; break;
    case 502:
    case 503:
    case 504: error = XHttpReply_ServiceUnavailableError; break;
    default: break;
    }
    if (error != XHttpReply_NoError) {
        self->m_error = error;
        xhttp_reply_replace_error(self, "HTTP 服务器返回错误状态");
        if (!self->m_errorEmitted) {
            self->m_errorEmitted = true;
            XHttpReply_errorOccurred_signal(self, error);
        }
    }
}

static void xhttp_reply_emit_progress(XHttpReply* self)
{
    uint64_t total = self->m_hasContentLength ? (uint64_t)self->m_contentLength : UINT64_MAX;
    XHttpReply_downloadProgress_signal(self, self->m_bodyReceived, total);
    if (self->m_body && xhttp_reply_size(self->m_body) != 0)
        XHttpReply_readyRead_signal(self);
}

static int xhttp_reply_parse_headers(XHttpReply* self)
{
    const uint8_t* data = xhttp_reply_data(self->m_input);
    size_t size = xhttp_reply_size(self->m_input);
    size_t headerEnd;
    size_t statusEnd;
    size_t firstSpace = SIZE_MAX;
    size_t statusStart;
    size_t cursor;
    if (!xhttp_reply_find_header_end(data, self->m_parseOffset, size, &headerEnd))
        return 0;
    if (!xhttp_reply_find_crlf(data, self->m_parseOffset, headerEnd + 2, &statusEnd) ||
        statusEnd < self->m_parseOffset + 12)
        return -1;
    for (size_t i = self->m_parseOffset; i < statusEnd; ++i) {
        if (data[i] == ' ') {
            firstSpace = i;
            break;
        }
    }
    if (firstSpace == SIZE_MAX || firstSpace < self->m_parseOffset + 5 ||
        memcmp(data + self->m_parseOffset, "HTTP/", 5) != 0)
        return -1;
    statusStart = firstSpace + 1;
    if (statusStart + 3 > statusEnd || data[statusStart] < '0' || data[statusStart] > '9' ||
        data[statusStart + 1] < '0' || data[statusStart + 1] > '9' ||
        data[statusStart + 2] < '0' || data[statusStart + 2] > '9' ||
        (statusStart + 3 < statusEnd && data[statusStart + 3] != ' '))
        return -1;
    self->m_statusCode = (int)(data[statusStart] - '0') * 100 +
                         (int)(data[statusStart + 1] - '0') * 10 +
                         (int)(data[statusStart + 2] - '0');
    if (self->m_reason)
        XClass_delete_base((XClass*)self->m_reason);
    self->m_reason = xhttp_reply_slice(data,
                                        statusStart + 3 < statusEnd ? statusStart + 4 : statusEnd,
                                        statusEnd);
    if (!self->m_reason)
        return -1;

    XHttpHeaders_clear(self->m_headers);
    XHttpHeaders_clear(self->m_trailers);
    cursor = statusEnd + 2;
    while (cursor < headerEnd) {
        size_t lineEnd;
        size_t colon = SIZE_MAX;
        XByteArray* name;
        XByteArray* value;
        if (!xhttp_reply_find_crlf(data, cursor, headerEnd + 2, &lineEnd))
            return -1;
        for (size_t i = cursor; i < lineEnd; ++i) {
            if (data[i] == ':') {
                colon = i;
                break;
            }
        }
        if (colon == SIZE_MAX || colon == cursor)
            return -1;
        size_t valueStart = colon + 1;
        while (valueStart < lineEnd && (data[valueStart] == ' ' || data[valueStart] == '\t'))
            ++valueStart;
        size_t valueEnd = lineEnd;
        while (valueEnd > valueStart && (data[valueEnd - 1] == ' ' || data[valueEnd - 1] == '\t'))
            --valueEnd;
        name = xhttp_reply_slice(data, cursor, colon);
        value = xhttp_reply_slice(data, valueStart, valueEnd);
        if (!name || !value || !XHttpHeaders_append(self->m_headers, name, value)) {
            xhttp_reply_delete_class(name);
            xhttp_reply_delete_class(value);
            return -1;
        }
        xhttp_reply_delete_class(name);
        xhttp_reply_delete_class(value);
        cursor = lineEnd + 2;
    }
    self->m_parseOffset = headerEnd + 4;
    self->m_headerParsed = true;
    self->m_hasContentLength = false;
    self->m_contentLength = 0;
    self->m_chunked = false;
    self->m_chunkNeedCrlf = false;
    self->m_chunkTrailer = false;
    self->m_chunkRemaining = 0;
    self->m_closeDelimited = false;
    self->m_noBody = self->m_request && XHttpRequest_method(self->m_request) == XHttpRequest_Head;
    self->m_noBody = self->m_noBody ||
                     (self->m_statusCode >= 100 && self->m_statusCode < 200) ||
                     self->m_statusCode == 204 || self->m_statusCode == 304;

    for (size_t i = 0; i < XHttpHeaders_size(self->m_headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(self->m_headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(self->m_headers, i);
        if (xhttp_reply_header_name_is(name, "Content-Length")) {
            size_t length;
            if (!xhttp_reply_parse_decimal(value, &length))
                return -1;
            if (self->m_hasContentLength && self->m_contentLength != length)
                return -1;
            self->m_contentLength = length;
            self->m_hasContentLength = true;
        } else if (xhttp_reply_header_name_is(name, "Transfer-Encoding") &&
                   xhttp_reply_value_has_token(value, "chunked")) {
            self->m_chunked = true;
        } else if (xhttp_reply_header_name_is(name, "Connection") &&
                   xhttp_reply_value_has_token(value, "close")) {
            self->m_closeDelimited = true;
        }
    }
    if (!self->m_noBody && !self->m_chunked && !self->m_hasContentLength)
        self->m_closeDelimited = true;
    xhttp_reply_set_status_error(self);
    XHttpReply_metaDataChanged_signal(self);
    return 1;
}

static int xhttp_reply_parse_chunked(XHttpReply* self)
{
    for (;;) {
        const uint8_t* data = xhttp_reply_data(self->m_input);
        size_t size = xhttp_reply_size(self->m_input);
        if (self->m_chunkRemaining != 0) {
            size_t available = size - self->m_parseOffset;
            size_t take = available < self->m_chunkRemaining ? available : self->m_chunkRemaining;
            if (take != 0) {
                if (!xhttp_reply_append(self->m_body, data + self->m_parseOffset, take))
                    return -1;
                self->m_parseOffset += take;
                self->m_chunkRemaining -= take;
                self->m_bodyReceived += (uint64_t)take;
                xhttp_reply_emit_progress(self);
            }
            if (self->m_chunkRemaining != 0)
                return 0;
            self->m_chunkNeedCrlf = true;
        }
        if (self->m_chunkNeedCrlf) {
            if (size - self->m_parseOffset < 2)
                return 0;
            if (data[self->m_parseOffset] != '\r' || data[self->m_parseOffset + 1] != '\n')
                return -1;
            self->m_parseOffset += 2;
            self->m_chunkNeedCrlf = false;
        }
        data = xhttp_reply_data(self->m_input);
        size = xhttp_reply_size(self->m_input);
        if (self->m_chunkTrailer) {
            size_t lineEnd;
            if (!xhttp_reply_find_crlf(data, self->m_parseOffset, size, &lineEnd))
                return 0;
            if (lineEnd == self->m_parseOffset) {
                self->m_parseOffset += 2;
                return 1;
            }
            size_t colon = self->m_parseOffset;
            while (colon < lineEnd && data[colon] != ':')
                ++colon;
            if (colon == lineEnd || colon == self->m_parseOffset)
                return -1;
            size_t valueStart = colon + 1;
            while (valueStart < lineEnd && (data[valueStart] == ' ' || data[valueStart] == '\t'))
                ++valueStart;
            size_t valueEnd = lineEnd;
            while (valueEnd > valueStart && (data[valueEnd - 1] == ' ' || data[valueEnd - 1] == '\t'))
                --valueEnd;
            XByteArray* name = xhttp_reply_slice(data, self->m_parseOffset, colon);
            XByteArray* value = xhttp_reply_slice(data, valueStart, valueEnd);
            bool ok = name && value && XHttpHeaders_append(self->m_trailers, name, value);
            xhttp_reply_delete_class(name);
            xhttp_reply_delete_class(value);
            if (!ok)
                return -1;
            self->m_parseOffset = lineEnd + 2;
            continue;
        }

        size_t lineEnd;
        if (!xhttp_reply_find_crlf(data, self->m_parseOffset, size, &lineEnd))
            return 0;
        size_t lineSize = lineEnd - self->m_parseOffset;
        size_t chunkSize;
        if (!xhttp_reply_parse_hex(data + self->m_parseOffset, lineSize, &chunkSize))
            return -1;
        self->m_parseOffset = lineEnd + 2;
        if (chunkSize == 0) {
            self->m_chunkTrailer = true;
            continue;
        }
        self->m_chunkRemaining = chunkSize;
    }
}

static int xhttp_reply_parse_body(XHttpReply* self)
{
    const uint8_t* data;
    size_t size;
    if (self->m_noBody)
        return 1;
    data = xhttp_reply_data(self->m_input);
    size = xhttp_reply_size(self->m_input);
    if (self->m_chunked)
        return xhttp_reply_parse_chunked(self);
    if (self->m_hasContentLength) {
        size_t remaining = self->m_contentLength - (size_t)self->m_bodyReceived;
        size_t available = size - self->m_parseOffset;
        size_t take = available < remaining ? available : remaining;
        if (take != 0) {
            if (!xhttp_reply_append(self->m_body, data + self->m_parseOffset, take))
                return -1;
            self->m_parseOffset += take;
            self->m_bodyReceived += (uint64_t)take;
            xhttp_reply_emit_progress(self);
        }
        return self->m_bodyReceived == (uint64_t)self->m_contentLength ? 1 : 0;
    }
    if (self->m_closeDelimited) {
        size_t available = size - self->m_parseOffset;
        if (available != 0) {
            if (!xhttp_reply_append(self->m_body, data + self->m_parseOffset, available))
                return -1;
            self->m_parseOffset += available;
            self->m_bodyReceived += (uint64_t)available;
            xhttp_reply_emit_progress(self);
        }
        return self->m_inputEnded ? 1 : 0;
    }
    return 1;
}

static bool xhttp_reply_parse(XHttpReply* self)
{
    while (!self->m_headerParsed) {
        int result = xhttp_reply_parse_headers(self);
        if (result < 0) {
            xhttp_reply_fail(self, XHttpReply_ProtocolInvalidOperationError,
                             "HTTP 响应头格式无效");
            return false;
        }
        if (result == 0)
            return true;
        if (self->m_statusCode >= 100 && self->m_statusCode < 200 && self->m_statusCode != 101) {
            self->m_headerParsed = false;
            /* 临时 1xx 响应只用于握手，最终响应重新从当前输入末尾开始。 */
            continue;
        }
        break;
    }
    if (!self->m_headerParsed || self->m_state == XHttpReply_Finished)
        return true;
    int result = xhttp_reply_parse_body(self);
    if (result < 0) {
        xhttp_reply_fail(self, XHttpReply_ProtocolInvalidOperationError,
                         "HTTP 响应体格式无效");
        return false;
    }
    if (result > 0) {
        if (self->m_inputEnded || self->m_noBody || self->m_hasContentLength || self->m_chunked)
            xhttp_reply_finish(self);
    }
    return true;
}

XVtable* XHttpReply_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpReply_deinit);
    return XVTABLE_DEFAULT;
}

void XHttpReply_init(XHttpReply* self, const XHttpRequest* request)
{
    if (!self)
        return;
    XObject_init((XObject*)self);
    XClassSetVtable(self, XHttpReply);
    self->m_request = request ? XHttpRequest_create_copy(request) : NULL;
    self->m_headers = XHttpHeaders_create();
    self->m_trailers = XHttpHeaders_create();
    self->m_body = XByteArray_create();
    self->m_input = XByteArray_create();
    self->m_reason = XByteArray_create();
    self->m_errorString = NULL;
    self->m_state = XHttpReply_Idle;
    self->m_error = XHttpReply_NoError;
    self->m_parseOffset = 0;
    self->m_statusCode = 0;
    self->m_contentLength = 0;
    self->m_chunkRemaining = 0;
    self->m_bodyReceived = 0;
    self->m_headerParsed = false;
    self->m_hasContentLength = false;
    self->m_chunked = false;
    self->m_chunkNeedCrlf = false;
    self->m_chunkTrailer = false;
    self->m_noBody = false;
    self->m_closeDelimited = false;
    self->m_inputEnded = false;
    self->m_errorEmitted = false;
    self->m_deferAuthentication = false;
    self->m_authenticationPending = false;
    self->m_deferFinished = self->m_request &&
        XHttpRequest_redirectPolicy(self->m_request) != XHttpRequest_ManualRedirectPolicy;
    self->m_finishedPending = false;
}

XHttpReply* XHttpReply_create(const XHttpRequest* request)
{
    XHttpReply* self = (XHttpReply*)XMalloc_System(sizeof(XHttpReply));
    if (!self)
        return NULL;
    XHttpReply_init(self, request);
    if (!self->m_headers || !self->m_trailers || !self->m_body || !self->m_input ||
        !self->m_reason || (request && !self->m_request)) {
        XHttpReply_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

static void VXHttpReply_deinit(XHttpReply* self)
{
    if (!self)
        return;
    if (self->m_request) XClass_delete_base((XClass*)self->m_request);
    if (self->m_headers) XClass_delete_base((XClass*)self->m_headers);
    if (self->m_trailers) XClass_delete_base((XClass*)self->m_trailers);
    if (self->m_body) XClass_delete_base((XClass*)self->m_body);
    if (self->m_input) XClass_delete_base((XClass*)self->m_input);
    if (self->m_reason) XClass_delete_base((XClass*)self->m_reason);
    if (self->m_errorString) XClass_delete_base((XClass*)self->m_errorString);
    self->m_request = NULL;
    self->m_headers = NULL;
    self->m_trailers = NULL;
    self->m_body = NULL;
    self->m_input = NULL;
    self->m_reason = NULL;
    self->m_errorString = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XHttpRequest* XHttpReply_request(const XHttpReply* self)
{
    return self && self->m_request ? XHttpRequest_create_copy(self->m_request) : NULL;
}

const XHttpRequest* XHttpReply_request_const(const XHttpReply* self)
{
    return self ? self->m_request : NULL;
}

const XHttpHeaders* XHttpReply_headers_const(const XHttpReply* self)
{
    return self ? self->m_headers : NULL;
}

const XHttpHeaders* XHttpReply_trailers_const(const XHttpReply* self)
{
    return self ? self->m_trailers : NULL;
}

int XHttpReply_statusCode(const XHttpReply* self)
{
    return self ? self->m_statusCode : 0;
}

XUrl* XHttpReply_url(const XHttpReply* self)
{ return self && self->m_request ? XUrl_create_copy(XHttpRequest_url_const(self->m_request)) : NULL; }

bool XHttpReply_hasRawHeader(const XHttpReply* self, const XByteArray* name)
{ return self && XHttpHeaders_contains(self->m_headers, name); }

XByteArray* XHttpReply_rawHeader(const XHttpReply* self, const XByteArray* name)
{ return self ? XHttpHeaders_value(self->m_headers, name) : NULL; }

XVector* XHttpReply_rawHeaderList(const XHttpReply* self)
{
    XVector* result = XVector_create(sizeof(XByteArray*));
    if (!result) return NULL;
    for (size_t i = 0; self && i < XHttpHeaders_size(self->m_headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(self->m_headers, i);
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            const XByteArray* previous = XHttpHeaders_nameAt_const(self->m_headers, j);
            if (name && previous && xhttp_reply_name_equal(name, previous)) {
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

XHttpHeaders* XHttpReply_headers(const XHttpReply* self)
{ return self && self->m_headers ? XHttpHeaders_create_copy(self->m_headers) : NULL; }

XByteArray* XHttpReply_headerKnown(const XHttpReply* self,
                                   XHttpHeaders_WellKnownHeader header)
{ return self ? XHttpHeaders_valueKnown(self->m_headers, header) : NULL; }

XByteArray* XHttpReply_reasonPhrase(const XHttpReply* self)
{
    return self && self->m_reason ? XByteArray_create_copy(self->m_reason) : XByteArray_create();
}

const XByteArray* XHttpReply_body_const(const XHttpReply* self)
{
    return self ? self->m_body : NULL;
}

XByteArray* XHttpReply_readAll(XHttpReply* self)
{
    if (!self || !self->m_body)
        return NULL;
    XByteArray* result = XByteArray_create_copy(self->m_body);
    if (result)
        XByteArray_clear_base((XContainer*)self->m_body);
    return result;
}

XHttpReply_NetworkError XHttpReply_error(const XHttpReply* self)
{
    return self ? self->m_error : XHttpReply_UnknownNetworkError;
}

XString* XHttpReply_errorString(const XHttpReply* self)
{
    return self && self->m_errorString ? XString_create_copy(self->m_errorString) : XString_create_utf8("");
}

void XHttpReply_setError(XHttpReply* self, XHttpReply_NetworkError error, const char* errorString)
{
    if (!self || error == XHttpReply_NoError)
        return;
    xhttp_reply_fail(self, error, errorString ? errorString : "HTTP 网络错误");
}

void XHttpReply_setDeferAuthentication(XHttpReply* self, bool enabled)
{
    if (self)
        self->m_deferAuthentication = enabled;
}

bool XHttpReply_authenticationPending(const XHttpReply* self)
{
    return self && self->m_authenticationPending;
}

bool XHttpReply_finishAuthenticationChallenge(XHttpReply* self)
{
    if (!self || !self->m_authenticationPending || self->m_state != XHttpReply_Finished)
        return false;
    self->m_authenticationPending = false;
    self->m_error = XHttpReply_AuthenticationRequiredError;
    xhttp_reply_replace_error(self, "HTTP 服务器需要认证凭据");
    if (!self->m_errorEmitted) {
        self->m_errorEmitted = true;
        XHttpReply_errorOccurred_signal(self, self->m_error);
    }
    return true;
}

bool XHttpReply_resetForRequest(XHttpReply* self, const XHttpRequest* request)
{
    XHttpRequest* replacement;
    if (!self || !request)
        return false;
    replacement = XHttpRequest_create_copy(request);
    if (!replacement)
        return false;
    if (self->m_request)
        XClass_delete_base((XClass*)self->m_request);
    self->m_request = replacement;
    XHttpHeaders_clear(self->m_headers);
    XHttpHeaders_clear(self->m_trailers);
    XByteArray_clear_base((XContainer*)self->m_body);
    XByteArray_clear_base((XContainer*)self->m_input);
    XByteArray_clear_base((XContainer*)self->m_reason);
    if (self->m_errorString) {
        XClass_delete_base((XClass*)self->m_errorString);
        self->m_errorString = NULL;
    }
    self->m_parseOffset = 0;
    self->m_contentLength = 0;
    self->m_chunkRemaining = 0;
    self->m_bodyReceived = 0;
    self->m_statusCode = 0;
    self->m_state = XHttpReply_Idle;
    self->m_error = XHttpReply_NoError;
    self->m_headerParsed = false;
    self->m_hasContentLength = false;
    self->m_chunked = false;
    self->m_chunkNeedCrlf = false;
    self->m_chunkTrailer = false;
    self->m_noBody = false;
    self->m_closeDelimited = false;
    self->m_inputEnded = false;
    self->m_errorEmitted = false;
    self->m_authenticationPending = false;
    self->m_deferFinished = replacement->m_redirectPolicy != XHttpRequest_ManualRedirectPolicy;
    self->m_finishedPending = false;
    return true;
}

bool XHttpReply_finishedSignalPending(const XHttpReply* self)
{
    return self && self->m_finishedPending;
}

bool XHttpReply_emitFinished(XHttpReply* self)
{
    if (!self || !self->m_finishedPending || self->m_state != XHttpReply_Finished)
        return false;
    self->m_finishedPending = false;
    XHttpReply_finished_signal(self);
    return true;
}

XUrl* XHttpReply_redirectTarget(const XHttpReply* self)
{
    XByteArray* field;
    const XByteArray* value;
    const uint8_t* data;
    size_t size;
    char* text;
    XString* location;
    XUrl* target;
    if (!self || !self->m_headers)
        return NULL;
    field = XByteArray_create_utf8("Location");
    value = field ? XHttpHeaders_value_const(self->m_headers, field) : NULL;
    data = xhttp_reply_data(value);
    size = xhttp_reply_size(value);
    if (field)
        XClass_delete_base((XClass*)field);
    if (!data || size == 0)
        return NULL;
    text = (char*)XMalloc_System(size + 1);
    if (!text)
        return NULL;
    memcpy(text, data, size);
    text[size] = '\0';
    location = XString_create_utf8(text);
    XFree_System(text);
    if (!location)
        return NULL;
    target = XUrl_create();
    if (target) {
        const XUrl* base = XHttpRequest_url_const(self->m_request);
        if (base)
            XUrl_resolved(base, location, target);
        else
            XUrl_setUrl(target, location, XUrl_TolerantMode);
        if (!XUrl_isValid(target)) {
            XClass_delete_base((XClass*)target);
            target = NULL;
        }
    }
    XClass_delete_base((XClass*)location);
    return target;
}

bool XHttpReply_isFinished(const XHttpReply* self)
{
    return self && self->m_state == XHttpReply_Finished;
}

bool XHttpReply_isRunning(const XHttpReply* self)
{
    return self && self->m_state == XHttpReply_Receiving;
}

bool XHttpReply_feed(XHttpReply* self, const void* data, size_t size)
{
    if (!self || self->m_state == XHttpReply_Finished || (!data && size != 0))
        return false;
    if (size != 0 && !xhttp_reply_append(self->m_input, (const uint8_t*)data, size))
        return false;
    if (self->m_state == XHttpReply_Idle)
        self->m_state = XHttpReply_Receiving;
    return xhttp_reply_parse(self);
}

bool XHttpReply_feedHttp2Headers(XHttpReply* self,
                                 const XHttp2HeaderList* headers,
                                 bool endStream)
{
    int status = 0;
    bool haveStatus = false;
    bool initial;
    bool regularHeaderSeen = false;
    if (!self || !headers || self->m_state == XHttpReply_Finished)
        return false;
    initial = !self->m_headerParsed;
    if (!initial) {
        /* RFC 9113 8.1：尾部字段必须通过携带 END_STREAM 的最终 HEADERS 结束响应。 */
        if (!endStream)
            goto invalid;
        /* HTTP/2 的后续 HEADERS 是尾部字段，不再重复要求 :status。 */
        for (size_t i = 0; i < XHttp2HeaderList_size(headers); ++i) {
            const XByteArray* name = XHttp2HeaderList_nameAt_const(headers, i);
            const XByteArray* value = XHttp2HeaderList_valueAt_const(headers, i);
            if (!name || !value || xhttp_reply_size(name) == 0 ||
                xhttp_reply_http2_is_pseudo_header(name) ||
                xhttp_reply_http2_is_forbidden_header(name, value) ||
                !XHttpHeaders_append(self->m_trailers, name, value))
                goto invalid;
        }
        if (endStream)
            xhttp_reply_finish(self);
        return true;
    }
    for (size_t i = 0; i < XHttp2HeaderList_size(headers); ++i) {
        const XByteArray* name = XHttp2HeaderList_nameAt_const(headers, i);
        const XByteArray* value = XHttp2HeaderList_valueAt_const(headers, i);
        if (!name || !value)
            goto invalid;
        if (xhttp_reply_http2_is_pseudo_header(name)) {
            if (regularHeaderSeen || !xhttp_reply_http2_name_equal(name, ":status"))
                goto invalid;
            if (haveStatus || !xhttp_reply_http2_status(value, &status))
                goto invalid;
            haveStatus = true;
        } else {
            regularHeaderSeen = true;
            if (xhttp_reply_http2_is_forbidden_header(name, value) ||
                !XHttpHeaders_append(self->m_headers, name, value))
                goto invalid;
        }
    }
    if (!haveStatus)
        goto invalid;
    if (self->m_statusCode >= 100 && self->m_statusCode < 200)
        XHttpHeaders_clear(self->m_headers);
    self->m_statusCode = status;
    if (status >= 100 && status < 200) {
        if (endStream)
            goto invalid;
        self->m_headerParsed = false;
        self->m_state = XHttpReply_Receiving;
        self->m_noBody = false;
        XHttpReply_metaDataChanged_signal(self);
        return true;
    }
    self->m_headerParsed = true;
    self->m_state = XHttpReply_Receiving;
    self->m_noBody = (self->m_request && XHttpRequest_method(self->m_request) == XHttpRequest_Head) ||
                     status == 204 || status == 205 || status == 304;
    xhttp_reply_set_status_error(self);
    XHttpReply_metaDataChanged_signal(self);
    if (endStream || self->m_noBody)
        xhttp_reply_finish(self);
    return true;
invalid:
    xhttp_reply_fail(self, XHttpReply_ProtocolInvalidOperationError,
                     "HTTP/2 响应头缺少有效的 :status 或包含非法字段");
    return false;
}

bool XHttpReply_feedHttp2Data(XHttpReply* self, const void* data, size_t size,
                              bool endStream)
{
    if (!self || self->m_state == XHttpReply_Finished || !self->m_headerParsed ||
        (!data && size != 0) || (self->m_noBody && size != 0)) {
        if (self && self->m_state != XHttpReply_Finished)
            xhttp_reply_fail(self, XHttpReply_ProtocolInvalidOperationError,
                             "HTTP/2 DATA 帧状态或载荷无效");
        return false;
    }
    if (size != 0 && !xhttp_reply_append(self->m_body, (const uint8_t*)data, size)) {
        xhttp_reply_fail(self, XHttpReply_InternalServerError, "HTTP/2 响应体内存不足");
        return false;
    }
    self->m_bodyReceived += (uint64_t)size;
    if (size != 0)
        xhttp_reply_emit_progress(self);
    if (endStream)
        xhttp_reply_finish(self);
    return true;
}

bool XHttpReply_endOfInput(XHttpReply* self)
{
    if (!self || self->m_state == XHttpReply_Finished)
        return self && self->m_state == XHttpReply_Finished;
    self->m_inputEnded = true;
    if (!xhttp_reply_parse(self))
        return false;
    if (!self->m_headerParsed) {
        xhttp_reply_fail(self, XHttpReply_ProtocolInvalidOperationError,
                         "HTTP 响应头不完整");
        return false;
    }
    if (self->m_chunked && (!self->m_chunkTrailer || self->m_chunkRemaining != 0 || self->m_chunkNeedCrlf)) {
        xhttp_reply_fail(self, XHttpReply_ProtocolInvalidOperationError,
                         "chunked 响应不完整");
        return false;
    }
    if (self->m_hasContentLength && self->m_bodyReceived != (uint64_t)self->m_contentLength) {
        xhttp_reply_fail(self, XHttpReply_ProtocolInvalidOperationError,
                         "Content-Length 与实际响应体长度不匹配");
        return false;
    }
    if (self->m_noBody || self->m_closeDelimited ||
        (self->m_hasContentLength && self->m_bodyReceived == (uint64_t)self->m_contentLength))
        xhttp_reply_finish(self);
    return self->m_state == XHttpReply_Finished;
}

void XHttpReply_abort(XHttpReply* self)
{
    if (self && self->m_state != XHttpReply_Finished)
        xhttp_reply_fail(self, XHttpReply_OperationCanceledError, "HTTP 请求已取消");
}

void* XHttpReply_readyRead_signal(XHttpReply* reply)
{
    XEmitSignal((XObject*)reply, XHttpReply_readyRead_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XHttpReply_metaDataChanged_signal(XHttpReply* reply)
{
    XEmitSignal((XObject*)reply, XHttpReply_metaDataChanged_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XHttpReply_finished_signal(XHttpReply* reply)
{
    XEmitSignal((XObject*)reply, XHttpReply_finished_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XHttpReply_errorOccurred_signal(XHttpReply* reply, XHttpReply_NetworkError error)
{
    XEmitSignal((XObject*)reply, XHttpReply_errorOccurred_signal,
                XVarList_create(2, sizeof(XHttpReply_NetworkError), &error), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XHttpReply_downloadProgress_signal(XHttpReply* reply, uint64_t received, uint64_t total)
{
    XEmitSignal((XObject*)reply, XHttpReply_downloadProgress_signal,
                XVarList_create(4, sizeof(uint64_t), &received, sizeof(uint64_t), &total), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XHttpReply_redirected_signal(XHttpReply* reply, const XUrl* target)
{
    XEmitSignal((XObject*)reply, XHttpReply_redirected_signal,
                XVarList_create(2, sizeof(const XUrl*), &target), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}
