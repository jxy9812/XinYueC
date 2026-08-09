/**
 * @file       XHttpServer.c
 * @brief      HTTP/1 服务端公共 API 实现。
 * @details    解析和发送均建立在 XinYueC 的 TCP、IO 和容器 API 之上。
 */

#include "XHttpServer.h"
#include "XHttpServerRouter.h"

#include "XMemory.h"
#include "XString.h"
#include "XVarList.h"
#include <stdio.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

typedef struct XHttpServerConnection {
    XTcpSocket* m_socket;          /**< TCP 套接字；由 XTcpServer 父对象拥有。 */
    XByteArray* m_input;           /**< 尚未组成完整请求的输入缓存；对象拥有。 */
    XConnection* m_readyRead;      /**< readyRead 信号连接；对象不拥有。 */
    XHttpServer* m_server;          /**< 所属服务器借用指针。 */
    bool m_handled;                /**< 是否已处理并关闭本次请求。 */
} XHttpServerConnection;

static void VXHttpServerRequest_deinit(XHttpServerRequest* self);
static void VXHttpServerResponse_deinit(XHttpServerResponse* self);
static void VXHttpServer_deinit(XHttpServer* self);
static void xhttp_server_new_connection(XObject* receiver, XVarList* args);
static void xhttp_server_socket_ready_read(XObject* sender, XVarList* args);
static bool xhttp_server_send_response_for_request(XHttpServerResponder* responder,
                                                   const XHttpServerResponse* response,
                                                   XHttpServerRequest_Method method);

static int64_t xhttp_server_find_byte(const XByteArray* data, uint8_t byte, size_t from)
{
    size_t i;
    const uint8_t* raw;
    if (!data || from > XByteArray_size_base(data))
        return -1;
    raw = XByteArray_data((XByteArray*)data);
    for (i = from; i < XByteArray_size_base(data); ++i) {
        if (raw[i] == byte)
            return (int64_t)i;
    }
    return -1;
}

static bool xhttp_server_range_equals(const XByteArray* data,
                                      size_t begin,
                                      size_t end,
                                      const char* text)
{
    size_t length;
    if (!data || !text || begin > end || end > XByteArray_size_base(data))
        return false;
    length = strlen(text);
    return length == end - begin &&
           memcmp(XByteArray_data((XByteArray*)data) + begin, text, length) == 0;
}

static bool xhttp_server_range_equals_ci(const XByteArray* data,
                                         size_t begin,
                                         size_t end,
                                         const char* text)
{
    size_t i;
    size_t length;
    const uint8_t* raw;
    if (!data || !text || begin > end || end > XByteArray_size_base(data))
        return false;
    length = strlen(text);
    if (length != end - begin)
        return false;
    raw = XByteArray_data((XByteArray*)data);
    for (i = 0; i < length; ++i) {
        uint8_t left = raw[begin + i];
        uint8_t right = (uint8_t)text[i];
        if (left >= 'A' && left <= 'Z') left = (uint8_t)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z') right = (uint8_t)(right + ('a' - 'A'));
        if (left != right)
            return false;
    }
    return true;
}

static bool xhttp_server_append_range(XByteArray* output,
                                      const XByteArray* source,
                                      size_t begin,
                                      size_t end)
{
    if (!output || !source || begin > end || end > XByteArray_size_base(source))
        return false;
    if (begin == end)
        return true;
    return XByteArray_push_back_2(output,
                                  XByteArray_data((XByteArray*)source) + begin,
                                  end - begin);
}

static bool xhttp_server_append_i32(XByteArray* output, int32_t value)
{
    XByteArray* number;
    bool result;
    if (!output)
        return false;
    number = XByteArray_create();
    if (!number)
        return false;
    XByteArray_setNum_i32(number, value, 10);
    result = xhttp_server_append_range(output, number, 0, XByteArray_size_base(number));
    XClass_delete_base((XClass*)number);
    return result;
}

static XHttpServerRequest_Method xhttp_server_method(const XByteArray* line,
                                                     size_t begin,
                                                     size_t end)
{
    if (xhttp_server_range_equals(line, begin, end, "GET")) return XHttpServerRequest_Get;
    if (xhttp_server_range_equals(line, begin, end, "PUT")) return XHttpServerRequest_Put;
    if (xhttp_server_range_equals(line, begin, end, "DELETE")) return XHttpServerRequest_Delete;
    if (xhttp_server_range_equals(line, begin, end, "POST")) return XHttpServerRequest_Post;
    if (xhttp_server_range_equals(line, begin, end, "HEAD")) return XHttpServerRequest_Head;
    if (xhttp_server_range_equals(line, begin, end, "OPTIONS")) return XHttpServerRequest_Options;
    if (xhttp_server_range_equals(line, begin, end, "PATCH")) return XHttpServerRequest_Patch;
    if (xhttp_server_range_equals(line, begin, end, "CONNECT")) return XHttpServerRequest_Connect;
    if (xhttp_server_range_equals(line, begin, end, "TRACE")) return XHttpServerRequest_Trace;
    return XHttpServerRequest_Unknown;
}

static const char* xhttp_server_reason(XHttpServerResponse_StatusCode status)
{
    switch (status) {
    case XHttpServerResponse_Continue: return "Continue";
    case XHttpServerResponse_SwitchingProtocols: return "Switching Protocols";
    case XHttpServerResponse_Processing: return "Processing";
    case XHttpServerResponse_Ok: return "OK";
    case XHttpServerResponse_Created: return "Created";
    case XHttpServerResponse_Accepted: return "Accepted";
    case XHttpServerResponse_NonAuthoritativeInformation: return "Non-Authoritative Information";
    case XHttpServerResponse_NoContent: return "No Content";
    case XHttpServerResponse_ResetContent: return "Reset Content";
    case XHttpServerResponse_PartialContent: return "Partial Content";
    case XHttpServerResponse_MultiStatus: return "Multi-Status";
    case XHttpServerResponse_AlreadyReported: return "Already Reported";
    case XHttpServerResponse_IMUsed: return "IM Used";
    case XHttpServerResponse_MultipleChoices: return "Multiple Choices";
    case XHttpServerResponse_MovedPermanently: return "Moved Permanently";
    case XHttpServerResponse_Found: return "Found";
    case XHttpServerResponse_SeeOther: return "See Other";
    case XHttpServerResponse_NotModified: return "Not Modified";
    case XHttpServerResponse_UseProxy: return "Use Proxy";
    case XHttpServerResponse_TemporaryRedirect: return "Temporary Redirect";
    case XHttpServerResponse_PermanentRedirect: return "Permanent Redirect";
    case XHttpServerResponse_BadRequest: return "Bad Request";
    case XHttpServerResponse_Unauthorized: return "Unauthorized";
    case XHttpServerResponse_PaymentRequired: return "Payment Required";
    case XHttpServerResponse_Forbidden: return "Forbidden";
    case XHttpServerResponse_NotFound: return "Not Found";
    case XHttpServerResponse_MethodNotAllowed: return "Method Not Allowed";
    case XHttpServerResponse_NotAcceptable: return "Not Acceptable";
    case XHttpServerResponse_ProxyAuthenticationRequired: return "Proxy Authentication Required";
    case XHttpServerResponse_RequestTimeout: return "Request Timeout";
    case XHttpServerResponse_Conflict: return "Conflict";
    case XHttpServerResponse_Gone: return "Gone";
    case XHttpServerResponse_LengthRequired: return "Length Required";
    case XHttpServerResponse_PreconditionFailed: return "Precondition Failed";
    case XHttpServerResponse_PayloadTooLarge: return "Payload Too Large";
    case XHttpServerResponse_UriTooLong: return "URI Too Long";
    case XHttpServerResponse_UnsupportedMediaType: return "Unsupported Media Type";
    case XHttpServerResponse_RequestRangeNotSatisfiable: return "Range Not Satisfiable";
    case XHttpServerResponse_ExpectationFailed: return "Expectation Failed";
    case XHttpServerResponse_ImATeapot: return "I'm a teapot";
    case XHttpServerResponse_MisdirectedRequest: return "Misdirected Request";
    case XHttpServerResponse_UnprocessableEntity: return "Unprocessable Entity";
    case XHttpServerResponse_Locked: return "Locked";
    case XHttpServerResponse_FailedDependency: return "Failed Dependency";
    case XHttpServerResponse_UpgradeRequired: return "Upgrade Required";
    case XHttpServerResponse_PreconditionRequired: return "Precondition Required";
    case XHttpServerResponse_TooManyRequests: return "Too Many Requests";
    case XHttpServerResponse_RequestHeaderFieldsTooLarge: return "Request Header Fields Too Large";
    case XHttpServerResponse_UnavailableForLegalReasons: return "Unavailable For Legal Reasons";
    case XHttpServerResponse_InternalServerError: return "Internal Server Error";
    case XHttpServerResponse_NotImplemented: return "Not Implemented";
    case XHttpServerResponse_BadGateway: return "Bad Gateway";
    case XHttpServerResponse_ServiceUnavailable: return "Service Unavailable";
    case XHttpServerResponse_GatewayTimeout: return "Gateway Timeout";
    case XHttpServerResponse_HttpVersionNotSupported: return "HTTP Version Not Supported";
    case XHttpServerResponse_VariantAlsoNegotiates: return "Variant Also Negotiates";
    case XHttpServerResponse_InsufficientStorage: return "Insufficient Storage";
    case XHttpServerResponse_LoopDetected: return "Loop Detected";
    case XHttpServerResponse_NotExtended: return "Not Extended";
    case XHttpServerResponse_NetworkAuthenticationRequired: return "Network Authentication Required";
    case XHttpServerResponse_NetworkConnectTimeoutError: return "Network Connect Timeout Error";
    default: return "Unknown";
    }
}

static bool xhttp_server_status_has_no_body(XHttpServerResponse_StatusCode status)
{
    return ((int)status >= 100 && (int)status < 200) ||
           status == XHttpServerResponse_ResetContent ||
           status == XHttpServerResponse_NoContent ||
           status == XHttpServerResponse_NotModified;
}

static XHttpServerConnection* xhttp_server_find_connection(XHttpServer* self,
                                                            XTcpSocket* socket)
{
    size_t i;
    if (!self || !self->m_connections || !socket)
        return NULL;
    for (i = 0; i < XVector_size_base(self->m_connections); ++i) {
        XHttpServerConnection** slot = (XHttpServerConnection**)XVector_at_base(
            self->m_connections, (int64_t)i);
        if (slot && *slot && (*slot)->m_socket == socket)
            return *slot;
    }
    return NULL;
}

static void xhttp_server_free_connection(XHttpServerConnection* connection)
{
    if (!connection)
        return;
    if (connection->m_readyRead) {
        XObject_disconnect_2(connection->m_readyRead);
        connection->m_readyRead = NULL;
    }
    if (connection->m_input)
        XClass_delete_base((XClass*)connection->m_input);
    XFree_System(connection);
}

static void xhttp_server_remove_connection(XHttpServerConnection* connection)
{
    XHttpServer* self;
    size_t i;
    if (!connection || !(self = connection->m_server) || !self->m_connections)
        return;
    for (i = 0; i < XVector_size_base(self->m_connections); ++i) {
        XHttpServerConnection** slot = (XHttpServerConnection**)XVector_at_base(
            self->m_connections, (int64_t)i);
        if (slot && *slot == connection) {
            XVector_removeAt_base(self->m_connections, (int64_t)i);
            xhttp_server_free_connection(connection);
            return;
        }
    }
}

static bool xhttp_server_write_response(XHttpServerResponder* responder,
                                        const XHttpServerResponse* response)
{
    XTcpSocket* socket;
    XByteArray* output;
    XHttpServerResponse_StatusCode status;
    const XByteArray* body;
    const XByteArray* mime;
    size_t i;
    bool hasLength = false;
    bool hasType = false;
    bool writeBody;
    if (!responder || !response || responder->m_sent || !responder->m_socket)
        return false;
    socket = (XTcpSocket*)responder->m_socket;
    output = XByteArray_create();
    if (!output)
        return false;
    status = response->m_statusCode;
    body = response->m_body;
    mime = response->m_mimeType;
    XByteArray_append_utf8(output, "HTTP/1.1 ");
    xhttp_server_append_i32(output, (int32_t)status);
    XByteArray_append_utf8(output, " ");
    XByteArray_append_utf8(output, xhttp_server_reason(status));
    XByteArray_append_utf8(output, "\r\n");
    for (i = 0; i < XHttpHeaders_size(response->m_headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(response->m_headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(response->m_headers, i);
        if (!name || !value)
            continue;
        if (xhttp_server_range_equals_ci(name, 0, XByteArray_size_base(name), "content-length"))
            hasLength = true;
        if (xhttp_server_range_equals_ci(name, 0, XByteArray_size_base(name), "content-type"))
            hasType = true;
        xhttp_server_append_range(output, name, 0, XByteArray_size_base(name));
        XByteArray_append_utf8(output, ": ");
        xhttp_server_append_range(output, value, 0, XByteArray_size_base(value));
        XByteArray_append_utf8(output, "\r\n");
    }
    if (!hasType && mime && XByteArray_size_base(mime) > 0) {
        XByteArray_append_utf8(output, "Content-Type: ");
        xhttp_server_append_range(output, mime, 0, XByteArray_size_base(mime));
        XByteArray_append_utf8(output, "\r\n");
    }
    if (!hasLength) {
        XByteArray_append_utf8(output, "Content-Length: ");
        xhttp_server_append_i32(output, (int32_t)(body ? XByteArray_size_base(body) : 0));
        XByteArray_append_utf8(output, "\r\n");
    }
    XByteArray_append_utf8(output, "Connection: close\r\n\r\n");
    writeBody = !xhttp_server_status_has_no_body(status) &&
                responder->m_method != XHttpServerRequest_Head;
    if (XIODevice_write_2((XIODevice*)socket, output) < 0)
        writeBody = false;
    if (writeBody && body && XByteArray_size_base(body) > 0)
        XIODevice_write_2((XIODevice*)socket, body);
    XIODevice_flush((XIODevice*)socket);
    XAbstractSocket_waitForBytesWritten((XAbstractSocket*)socket, 1000);
    XClass_delete_base((XClass*)output);
    responder->m_sent = true;
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)socket);
    return true;
}

/* 发送响应前单独处理 HEAD，避免把请求方法塞入公共响应对象。 */
static bool xhttp_server_send_response_for_request(XHttpServerResponder* responder,
                                                   const XHttpServerResponse* response,
                                                   XHttpServerRequest_Method method)
{
    XByteArray* output;
    XTcpSocket* socket;
    const XByteArray* body;
    const XByteArray* mime;
    XHttpServerResponse_StatusCode status;
    size_t i;
    bool hasLength = false;
    bool hasType = false;
    if (!responder || !response || responder->m_sent || !responder->m_socket)
        return false;
    socket = (XTcpSocket*)responder->m_socket;
    output = XByteArray_create();
    if (!output)
        return false;
    status = response->m_statusCode;
    body = response->m_body;
    mime = response->m_mimeType;
    XByteArray_append_utf8(output, "HTTP/1.1 ");
    xhttp_server_append_i32(output, (int32_t)status);
    XByteArray_append_utf8(output, " ");
    XByteArray_append_utf8(output, xhttp_server_reason(status));
    XByteArray_append_utf8(output, "\r\n");
    for (i = 0; i < XHttpHeaders_size(response->m_headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(response->m_headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(response->m_headers, i);
        if (!name || !value) continue;
        if (xhttp_server_range_equals_ci(name, 0, XByteArray_size_base(name), "content-length")) hasLength = true;
        if (xhttp_server_range_equals_ci(name, 0, XByteArray_size_base(name), "content-type")) hasType = true;
        xhttp_server_append_range(output, name, 0, XByteArray_size_base(name));
        XByteArray_append_utf8(output, ": ");
        xhttp_server_append_range(output, value, 0, XByteArray_size_base(value));
        XByteArray_append_utf8(output, "\r\n");
    }
    if (!hasType && mime && XByteArray_size_base(mime) > 0) {
        XByteArray_append_utf8(output, "Content-Type: ");
        xhttp_server_append_range(output, mime, 0, XByteArray_size_base(mime));
        XByteArray_append_utf8(output, "\r\n");
    }
    if (!hasLength) {
        XByteArray_append_utf8(output, "Content-Length: ");
        xhttp_server_append_i32(output, (int32_t)(body ? XByteArray_size_base(body) : 0));
        XByteArray_append_utf8(output, "\r\n");
    }
    XByteArray_append_utf8(output, "Connection: close\r\n\r\n");
    if (XIODevice_write_2((XIODevice*)socket, output) < 0) {
        XClass_delete_base((XClass*)output);
        return false;
    }
    if (method != XHttpServerRequest_Head &&
        !xhttp_server_status_has_no_body(status) && body && XByteArray_size_base(body) > 0)
        XIODevice_write_2((XIODevice*)socket, body);
    XIODevice_flush((XIODevice*)socket);
    XAbstractSocket_waitForBytesWritten((XAbstractSocket*)socket, 1000);
    XClass_delete_base((XClass*)output);
    responder->m_sent = true;
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)socket);
    return true;
}

static bool xhttp_server_parse_request(XHttpServerConnection* connection)
{
    XByteArray* input;
    int64_t headerEnd;
    int64_t requestLineEnd;
    size_t cursor;
    size_t firstSpace;
    size_t secondSpace;
    size_t bodyLength = 0;
    XHttpServerRequest* request = NULL;
    XByteArray* target = NULL;
    XByteArray* host = NULL;
    XString* targetString = NULL;
    XString* urlString = NULL;
    XHttpServerResponder responder;
    XHttpServerResponse* fallback = NULL;
    bool hasContentLength = false;
    if (!connection || !(input = connection->m_input) || connection->m_handled)
        return false;
    headerEnd = -1;
    for (cursor = 0; cursor + 3 < XByteArray_size_base(input); ++cursor) {
        const uint8_t* raw = XByteArray_data(input);
        if (raw[cursor] == '\r' && raw[cursor + 1] == '\n' &&
            raw[cursor + 2] == '\r' && raw[cursor + 3] == '\n') {
            headerEnd = (int64_t)cursor;
            break;
        }
    }
    if (headerEnd < 0)
        return false;
    requestLineEnd = xhttp_server_find_byte(input, '\n', 0);
    if (requestLineEnd < 1 || (size_t)requestLineEnd > (size_t)headerEnd ||
        XByteArray_at_base(input, (size_t)requestLineEnd - 1) != '\r') {
        goto bad_request;
    }
    firstSpace = (size_t)xhttp_server_find_byte(input, ' ', 0);
    if ((int64_t)firstSpace < 0 || firstSpace >= (size_t)requestLineEnd) {
        goto bad_request;
    }
    secondSpace = (size_t)xhttp_server_find_byte(input, ' ', firstSpace + 1);
    if ((int64_t)secondSpace < 0 || secondSpace >= (size_t)requestLineEnd || secondSpace <= firstSpace + 1) {
        goto bad_request;
    }
    if (!xhttp_server_range_equals(input, secondSpace + 1, (size_t)requestLineEnd - 1, "HTTP/1.1") &&
        !xhttp_server_range_equals(input, secondSpace + 1, (size_t)requestLineEnd - 1, "HTTP/1.0")) {
        goto bad_request;
    }
    request = XHttpServerRequest_create();
    if (!request)
        goto bad_request;
    request->m_method = xhttp_server_method(input, 0, firstSpace);
    target = XByteArray_mid(input, (int64_t)firstSpace + 1,
                            (int64_t)(secondSpace - firstSpace - 1));
    request->m_headers = XHttpHeaders_create();
    request->m_body = XByteArray_create();
    if (!target || !request->m_headers || !request->m_body)
        goto bad_request;
    cursor = (size_t)requestLineEnd + 1;
    while (cursor < (size_t)headerEnd) {
        int64_t lineEnd = xhttp_server_find_byte(input, '\n', cursor);
        int64_t colon;
        XByteArray* name;
        XByteArray* value;
        if (lineEnd < 1 || (size_t)lineEnd > (size_t)headerEnd + 1 ||
            XByteArray_at_base(input, (size_t)lineEnd - 1) != '\r') {
            goto bad_request;
        }
        colon = xhttp_server_find_byte(input, ':', cursor);
        if (colon < 0 || (size_t)colon <= cursor || (size_t)colon >= (size_t)lineEnd - 1) {
            goto bad_request;
        }
        name = XByteArray_mid(input, (int64_t)cursor, colon - (int64_t)cursor);
        value = XByteArray_mid(input, colon + 1,
                               (int64_t)((size_t)lineEnd - 1 - (size_t)colon - 1));
        if (!name || !value || !XHttpHeaders_append(request->m_headers, name, value)) {
            if (name) XClass_delete_base((XClass*)name);
            if (value) XClass_delete_base((XClass*)value);
            goto bad_request;
        }
        if (xhttp_server_range_equals_ci(name, 0, XByteArray_size_base(name), "content-length")) {
            bool ok = false;
            int64_t parsed = XByteArray_toLongLong(value, &ok, 10);
            if (!ok || parsed < 0) {
                XClass_delete_base((XClass*)name);
                XClass_delete_base((XClass*)value);
                goto bad_request;
            }
            bodyLength = (size_t)parsed;
            hasContentLength = true;
        }
        if (xhttp_server_range_equals_ci(name, 0, XByteArray_size_base(name), "host"))
            host = XByteArray_create_copy(value);
        XClass_delete_base((XClass*)name);
        XClass_delete_base((XClass*)value);
        cursor = (size_t)lineEnd + 1;
    }
    if (hasContentLength && XByteArray_size_base(input) < (size_t)headerEnd + 4 + bodyLength)
        goto incomplete_request;
    if (bodyLength > 0 && !XByteArray_push_back_2(request->m_body,
        XByteArray_data(input) + headerEnd + 4, bodyLength)) {
        goto bad_request;
    }
    targetString = XString_create_with_length_utf8((const char*)XByteArray_data(target),
                                                   XByteArray_size_base(target));
    if (!targetString) {
        goto bad_request;
    }
    if (XString_startsWith_utf8(targetString, "http://", XChar_CaseSensitive) ||
        XString_startsWith_utf8(targetString, "https://", XChar_CaseSensitive)) {
        urlString = XString_create_copy(targetString);
    } else if (host && XByteArray_size_base(host) > 0) {
        urlString = XString_create_utf8("http://");
        if (urlString) {
            XString_append_with_length_utf8(urlString, (const char*)XByteArray_data(host),
                                            XByteArray_size_base(host));
            XString_append(urlString, targetString);
        }
    }
    if (!urlString) {
        goto bad_request;
    }
    request->m_url = XUrl_create_ex(urlString, XUrl_TolerantMode);
    request->m_remotePort = XAbstractSocket_peerPort((XAbstractSocket*)connection->m_socket);
    request->m_localPort = XAbstractSocket_localPort((XAbstractSocket*)connection->m_socket);
    if (!request->m_url)
        goto bad_request;
    responder.m_server = (XObject*)connection->m_server;
    responder.m_socket = (XObject*)connection->m_socket;
    responder.m_method = request->m_method;
    responder.m_sent = false;
    if (connection->m_server->m_router)
        XHttpServerRouter_handleRequest(connection->m_server->m_router,
                                        request, &responder);
    if (!responder.m_sent && connection->m_server->m_handler)
        connection->m_server->m_handler(request, &responder,
                                         connection->m_server->m_handlerContext);
    if (!responder.m_sent && connection->m_server->m_missingHandler)
        connection->m_server->m_missingHandler(request, &responder,
                                                connection->m_server->m_missingHandlerContext);
    if (!responder.m_sent) {
        fallback = XHttpServerResponse_create_body(NULL,
            XHttpServerResponse_NotFound);
        if (fallback)
            xhttp_server_send_response_for_request(&responder, fallback, request->m_method);
    }
    if (fallback)
        XClass_delete_base((XClass*)fallback);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)target);
    XClass_delete_base((XClass*)host);
    XClass_delete_base((XClass*)targetString);
    XClass_delete_base((XClass*)urlString);
    connection->m_handled = true;
    xhttp_server_remove_connection(connection);
    return true;

incomplete_request:
    if (request) XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)target);
    XClass_delete_base((XClass*)host);
    XClass_delete_base((XClass*)targetString);
    XClass_delete_base((XClass*)urlString);
    return false;

bad_request:
    if (request) XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)target);
    XClass_delete_base((XClass*)host);
    XClass_delete_base((XClass*)targetString);
    XClass_delete_base((XClass*)urlString);
    fallback = XHttpServerResponse_create_body(NULL, XHttpServerResponse_BadRequest);
    if (fallback) {
        responder.m_server = (XObject*)connection->m_server;
        responder.m_socket = (XObject*)connection->m_socket;
        responder.m_method = XHttpServerRequest_Get;
        responder.m_sent = false;
        xhttp_server_send_response_for_request(&responder, fallback, XHttpServerRequest_Get);
        XClass_delete_base((XClass*)fallback);
    }
    connection->m_handled = true;
    xhttp_server_remove_connection(connection);
    return false;
}

static void xhttp_server_socket_ready_read(XObject* sender, XVarList* args)
{
    XTcpSocket* socket = (XTcpSocket*)sender;
    XObject* tcpServerObject;
    XObject* serverObject;
    XHttpServerConnection* connection;
    (void)args;
    if (!socket)
        return;
    tcpServerObject = XObject_parent((XObject*)socket);
    serverObject = tcpServerObject ? XObject_parent(tcpServerObject) : NULL;
    connection = xhttp_server_find_connection((XHttpServer*)serverObject, socket);
    if (!connection || connection->m_handled)
        return;
    XIODevice_readAll_2((XIODevice*)socket, connection->m_input, true);
    xhttp_server_parse_request(connection);
}

static void xhttp_server_new_connection(XObject* receiver, XVarList* args)
{
    XHttpServer* self = (XHttpServer*)receiver;
    XTcpSocket* socket;
    XHttpServerConnection* connection;
    (void)args;
    if (!self || !self->m_tcpServer)
        return;
    socket = XTcpServer_nextPendingConnection_base(self->m_tcpServer);
    if (!socket)
        return;
    connection = (XHttpServerConnection*)XMalloc_System(sizeof(XHttpServerConnection));
    if (!connection) {
        XAbstractSocket_abort((XAbstractSocket*)socket);
        return;
    }
    memset(connection, 0, sizeof(*connection));
    connection->m_socket = socket;
    connection->m_server = self;
    connection->m_input = XByteArray_create();
    if (!connection->m_input || !XVector_push_back_1_base(self->m_connections, &connection)) {
        xhttp_server_free_connection(connection);
        XAbstractSocket_abort((XAbstractSocket*)socket);
        return;
    }
    connection->m_readyRead = XObject_connect_2((XObject*)socket,
        XSignal(XIODevice_readyRead_signal), xhttp_server_socket_ready_read);
    if (!connection->m_readyRead) {
        xhttp_server_remove_connection(connection);
        XAbstractSocket_abort((XAbstractSocket*)socket);
        return;
    }
    xhttp_server_socket_ready_read((XObject*)socket, NULL);
}

static void VXHttpServerRequest_deinit(XHttpServerRequest* self)
{
    if (!self) return;
    if (self->m_url) XClass_delete_base((XClass*)self->m_url);
    if (self->m_headers) XClass_delete_base((XClass*)self->m_headers);
    if (self->m_body) XClass_delete_base((XClass*)self->m_body);
    self->m_url = NULL;
    self->m_headers = NULL;
    self->m_body = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttpServerResponse_deinit(XHttpServerResponse* self)
{
    if (!self) return;
    if (self->m_body) XClass_delete_base((XClass*)self->m_body);
    if (self->m_mimeType) XClass_delete_base((XClass*)self->m_mimeType);
    if (self->m_headers) XClass_delete_base((XClass*)self->m_headers);
    self->m_body = NULL;
    self->m_mimeType = NULL;
    self->m_headers = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttpServer_deinit(XHttpServer* self)
{
    size_t i;
    if (!self) return;
    XHttpServer_close(self);
    if (self->m_connections) {
        for (i = 0; i < XVector_size_base(self->m_connections); ++i) {
            XHttpServerConnection** slot = (XHttpServerConnection**)XVector_at_base(
                self->m_connections, (int64_t)i);
            if (slot && *slot) xhttp_server_free_connection(*slot);
        }
        XClass_delete_base((XClass*)self->m_connections);
        self->m_connections = NULL;
    }
    if (self->m_tcpServer) {
        XObject_disconnect_1((XObject*)self->m_tcpServer,
            XSignal(XTcpServer_newConnection_signal), (XObject*)self,
            xhttp_server_new_connection);
        XClass_delete_base((XClass*)self->m_tcpServer);
        self->m_tcpServer = NULL;
    }
    if (self->m_router) {
        XClass_delete_base((XClass*)self->m_router);
        self->m_router = NULL;
    }
    XClass_Deinit_Parent(XObject, self);
}

static void VXHttpServerRequest_copy(XHttpServerRequest* dest,
                                     const XHttpServerRequest* src)
{
    (void)dest;
    (void)src;
}

static void VXHttpServerResponse_copy(XHttpServerResponse* dest,
                                      const XHttpServerResponse* src)
{
    (void)dest;
    (void)src;
}

XVtable* XHttpServerRequest_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttpServerRequest)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttpServerRequest");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpServerRequest_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttpServerRequest_copy);
    XCLASS_SHOW_SIZE_DEFAULT(XHttpServerRequest);
    return XVTABLE_DEFAULT;
}

XVtable* XHttpServerResponse_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttpServerResponse)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttpServerResponse");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpServerResponse_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttpServerResponse_copy);
    XCLASS_SHOW_SIZE_DEFAULT(XHttpServerResponse);
    return XVTABLE_DEFAULT;
}

XVtable* XHttpServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttpServer)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHttpServer");
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpServer_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XHttpServer);
    return XVTABLE_DEFAULT;
}

XHttpServerRequest* XHttpServerRequest_create(void)
{
    XHttpServerRequest* self = (XHttpServerRequest*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpServerRequest);
    self->m_method = XHttpServerRequest_Unknown;
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XHttpServerResponse* XHttpServerResponse_create_status(XHttpServerResponse_StatusCode status)
{
    XHttpServerResponse* self = (XHttpServerResponse*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpServerResponse);
    self->m_headers = XHttpHeaders_create();
    self->m_body = XByteArray_create();
    self->m_statusCode = status;
    Set_Class_MemoryFree(self, XFree_System);
    if (!self->m_headers || !self->m_body) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

XHttpServerResponse* XHttpServerResponse_create_body(const XByteArray* body,
                                                     XHttpServerResponse_StatusCode status)
{
    XHttpServerResponse* self = XHttpServerResponse_create_status(status);
    if (self && body && !XByteArray_push_back_2(self->m_body,
        XByteArray_data((XByteArray*)body), XByteArray_size_base(body))) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

XHttpServerResponse* XHttpServerResponse_create_mime(const XByteArray* mimeType,
                                                     const XByteArray* body,
                                                     XHttpServerResponse_StatusCode status)
{
    XHttpServerResponse* self = XHttpServerResponse_create_body(body, status);
    if (self && mimeType) self->m_mimeType = XByteArray_create_copy(mimeType);
    if (self && mimeType && !self->m_mimeType) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

const XUrl* XHttpServerRequest_url_const(const XHttpServerRequest* self)
{ return self ? self->m_url : NULL; }
XHttpServerRequest_Method XHttpServerRequest_method(const XHttpServerRequest* self)
{ return self ? self->m_method : XHttpServerRequest_Unknown; }
const XString* XHttpServerRequest_query_const(const XHttpServerRequest* self)
{ return self && self->m_url ? XUrl_query_const(self->m_url) : NULL; }
const XHttpHeaders* XHttpServerRequest_headers_const(const XHttpServerRequest* self)
{ return self ? self->m_headers : NULL; }
const XByteArray* XHttpServerRequest_body_const(const XHttpServerRequest* self)
{ return self ? self->m_body : NULL; }
XByteArray* XHttpServerRequest_value(const XHttpServerRequest* self, const XByteArray* key)
{ return self && self->m_headers ? XHttpHeaders_value(self->m_headers, key) : NULL; }
uint16_t XHttpServerRequest_remotePort(const XHttpServerRequest* self)
{ return self ? self->m_remotePort : 0; }
uint16_t XHttpServerRequest_localPort(const XHttpServerRequest* self)
{ return self ? self->m_localPort : 0; }

const XByteArray* XHttpServerResponse_data_const(const XHttpServerResponse* self)
{ return self ? self->m_body : NULL; }
const XByteArray* XHttpServerResponse_mimeType_const(const XHttpServerResponse* self)
{ return self ? self->m_mimeType : NULL; }
XHttpServerResponse_StatusCode XHttpServerResponse_statusCode(const XHttpServerResponse* self)
{ return self ? self->m_statusCode : XHttpServerResponse_InternalServerError; }
XHttpHeaders* XHttpServerResponse_headers(XHttpServerResponse* self)
{ return self ? self->m_headers : NULL; }
bool XHttpServerResponse_setHeaders(XHttpServerResponse* self, const XHttpHeaders* headers)
{
    XHttpHeaders* copy;
    if (!self || !headers) return false;
    copy = XHttpHeaders_create_copy(headers);
    if (!copy) return false;
    if (self->m_headers) XClass_delete_base((XClass*)self->m_headers);
    self->m_headers = copy;
    return true;
}

bool XHttpServerResponder_sendResponse(XHttpServerResponder* responder,
                                       const XHttpServerResponse* response)
{
    return responder ? xhttp_server_send_response_for_request(
        responder, response, responder->m_method) : false;
}

bool XHttpServerResponder_write(XHttpServerResponder* responder,
                                const XByteArray* body,
                                const XByteArray* mimeType,
                                XHttpServerResponse_StatusCode status)
{
    XHttpServerResponse* response = XHttpServerResponse_create_mime(mimeType, body, status);
    bool result;
    if (!response) return false;
    result = responder ? xhttp_server_send_response_for_request(
        responder, response, responder->m_method) : false;
    XClass_delete_base((XClass*)response);
    return result;
}

bool XHttpServerResponder_writeStatus(XHttpServerResponder* responder,
                                      XHttpServerResponse_StatusCode status)
{
    return XHttpServerResponder_write(responder, NULL, NULL, status);
}

void XHttpServer_init(XHttpServer* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init(&self->m_class);
    XClassSetVtable(self, XHttpServer);
    self->m_tcpServer = XTcpServer_create();
    self->m_connections = XVector_Create(XHttpServerConnection*);
    self->m_router = XHttpServerRouter_create(self);
    if (self->m_tcpServer)
        XObject_setParent((XObject*)self->m_tcpServer, (XObject*)self);
    if (self->m_tcpServer)
        self->m_newConnection = XObject_connect_1((XObject*)self->m_tcpServer,
            XSignal(XTcpServer_newConnection_signal), (XObject*)self,
            xhttp_server_new_connection, XConnectionType_Direct);
}

XHttpServer* XHttpServer_create(void)
{
    XHttpServer* self = (XHttpServer*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XHttpServer_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    if (!self->m_tcpServer || !self->m_connections || !self->m_router) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

void XHttpServer_setHandler(XHttpServer* self, XHttpServer_Handler handler, void* context)
{
    if (!self) return;
    self->m_handler = handler;
    self->m_handlerContext = context;
}

void XHttpServer_setMissingHandler(XHttpServer* self,
                                   XHttpServer_Handler handler,
                                   void* context)
{
    if (!self)
        return;
    self->m_missingHandler = handler;
    self->m_missingHandlerContext = context;
}

XHttpServerRouter* XHttpServer_router(const XHttpServer* self)
{ return self ? self->m_router : NULL; }

XHttpServerRouterRule* XHttpServer_route(XHttpServer* self,
                                         const char* pathPattern,
                                         uint32_t methods,
                                         XHttpServer_RouteHandler handler,
                                         void* context)
{
    return self && self->m_router ? XHttpServerRouter_addRule_utf8(
        self->m_router, pathPattern, methods, handler, context) : NULL;
}

bool XHttpServer_listen(XHttpServer* self, const XHostAddress* address, uint16_t port)
{
    return self && self->m_tcpServer && XTcpServer_listen(self->m_tcpServer, address, port);
}

bool XHttpServer_isListening(const XHttpServer* self)
{ return self && self->m_tcpServer && XTcpServer_isListening(self->m_tcpServer); }

uint16_t XHttpServer_serverPort(const XHttpServer* self)
{ return self && self->m_tcpServer ? XTcpServer_serverPort(self->m_tcpServer) : 0; }

XTcpServer* XHttpServer_tcpServer(const XHttpServer* self)
{ return self ? self->m_tcpServer : NULL; }

void XHttpServer_close(XHttpServer* self)
{
    size_t i;
    if (!self) return;
    if (self->m_tcpServer) XTcpServer_close(self->m_tcpServer);
    if (!self->m_connections) return;
    for (i = 0; i < XVector_size_base(self->m_connections); ++i) {
        XHttpServerConnection** slot = (XHttpServerConnection**)XVector_at_base(
            self->m_connections, (int64_t)i);
        if (slot && *slot) {
            (*slot)->m_handled = true;
            if ((*slot)->m_socket)
                XAbstractSocket_abort((XAbstractSocket*)(*slot)->m_socket);
            xhttp_server_free_connection(*slot);
        }
    }
    XVector_clear_base(self->m_connections);
}
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
