/**
 * @file       XServerChan.c
 * - @brief      Server酱消息推送客户端实现。
 */

#include "XServerChan.h"

#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XMemory.h"
#include "XThread.h"
#include "XUrl.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void xserverchan_result_set_message(XServerChanResult* self, const char* message)
{
    XString* value;
    if (!self) return;
    value = XString_create_utf8(message ? message : "");
    if (!value) return;
    if (self->m_message) XString_delete_base(self->m_message);
    self->m_message = value;
}

static void xserverchan_result_set_message_string(XServerChanResult* self,
                                                   const XString* message)
{
    XString* value;
    if (!self) return;
    value = message ? XString_create_copy(message) : XString_create_utf8("");
    if (!value) return;
    if (self->m_message) XString_delete_base(self->m_message);
    self->m_message = value;
}

static void VXServerChanResult_deinit(XServerChanResult* self)
{
    if (!self) return;
    if (self->m_message) {
        XString_delete_base(self->m_message);
        self->m_message = NULL;
    }
    if (self->m_responseBody) {
        XByteArray_delete_base(self->m_responseBody);
        self->m_responseBody = NULL;
    }
    self->m_apiCode = -1;
    self->m_httpStatusCode = 0;
    self->m_error = XServerChanResult_InvalidArgument;
    self->m_success = false;
}

static void VXServerChanResult_copy(XServerChanResult* dest,
                                    const XServerChanResult* src)
{
    XString* message = NULL;
    XByteArray* body = NULL;
    if (!dest || !src || dest == src || XClassIsVtableNull(src)) return;
    if (XClassIsVtableNull(dest)) XServerChanResult_init(dest);
    if (src->m_message && !(message = XString_create_copy(src->m_message))) return;
    if (src->m_responseBody && !(body = XByteArray_create_copy(src->m_responseBody))) {
        if (message) XString_delete_base(message);
        return;
    }
    if (dest->m_message) XString_delete_base(dest->m_message);
    if (dest->m_responseBody) XByteArray_delete_base(dest->m_responseBody);
    dest->m_message = message;
    dest->m_responseBody = body;
    dest->m_apiCode = src->m_apiCode;
    dest->m_httpStatusCode = src->m_httpStatusCode;
    dest->m_error = src->m_error;
    dest->m_success = src->m_success;
}

static void VXServerChanResult_move(XServerChanResult* dest, XServerChanResult* src)
{
    if (!dest || !src || dest == src || XClassIsVtableNull(src)) return;
    if (XClassIsVtableNull(dest)) XServerChanResult_init(dest);
    if (dest->m_message) XString_delete_base(dest->m_message);
    if (dest->m_responseBody) XByteArray_delete_base(dest->m_responseBody);
    dest->m_message = src->m_message;
    dest->m_responseBody = src->m_responseBody;
    dest->m_apiCode = src->m_apiCode;
    dest->m_httpStatusCode = src->m_httpStatusCode;
    dest->m_error = src->m_error;
    dest->m_success = src->m_success;
    src->m_message = NULL;
    src->m_responseBody = NULL;
    src->m_apiCode = -1;
    src->m_httpStatusCode = 0;
    src->m_error = XServerChanResult_InvalidArgument;
    src->m_success = false;
}

XVtable* XServerChanResult_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XServerChanResult)
	XCLASS_SET_CLASS_NAME_DEFAULT("XServerChanResult");
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXServerChanResult_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXServerChanResult_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXServerChanResult_move);
    XCLASS_SHOW_SIZE_DEFAULT(XServerChanResult);
    return XVTABLE_DEFAULT;
}

void XServerChanResult_init(XServerChanResult* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XServerChanResult);
    self->m_apiCode = -1;
    self->m_error = XServerChanResult_InvalidArgument;
}

XServerChanResult* XServerChanResult_create(void)
{
    XServerChanResult* self = (XServerChanResult*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XServerChanResult_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XServerChanResult* XServerChanResult_create_copy(const XServerChanResult* other)
{
    XServerChanResult* self;
    if (!other || XClassIsVtableNull(other)) return NULL;
    self = XServerChanResult_create();
    if (!self) return NULL;
    XServerChanResult_copy_base(self, other);
    return self;
}

XServerChanResult* XServerChanResult_create_move(XServerChanResult* other)
{
    XServerChanResult* self;
    if (!other || XClassIsVtableNull(other)) return NULL;
    self = XServerChanResult_create();
    if (!self) return NULL;
    XServerChanResult_move_base(self, other);
    return self;
}

int XServerChanResult_apiCode(const XServerChanResult* self)
{ return self ? self->m_apiCode : -1; }

int XServerChanResult_httpStatusCode(const XServerChanResult* self)
{ return self ? self->m_httpStatusCode : 0; }

XServerChanResult_Error XServerChanResult_error(const XServerChanResult* self)
{ return self ? self->m_error : XServerChanResult_InvalidArgument; }

bool XServerChanResult_isSuccess(const XServerChanResult* self)
{ return self && self->m_success; }

const XString* XServerChanResult_message_const(const XServerChanResult* self)
{ return self ? self->m_message : NULL; }

const XByteArray* XServerChanResult_responseBody_const(const XServerChanResult* self)
{ return self ? self->m_responseBody : NULL; }

static bool xserverchan_is_ascii_digit(unsigned char c)
{
    return c >= (unsigned char)'0' && c <= (unsigned char)'9';
}

static bool xserverchan_send_key_is_valid(const char* sendKey)
{
    const char* cursor;
    if (!sendKey || !*sendKey) return false;
    for (cursor = sendKey; *cursor; ++cursor) {
        unsigned char c = (unsigned char)*cursor;
        if (c <= 0x20 || c == 0x7f || c == '/' || c == '?' || c == '#') return false;
    }
    if (strncmp(sendKey, "sctp", 4) == 0) {
        cursor = sendKey + 4;
        if (!xserverchan_is_ascii_digit((unsigned char)*cursor)) return false;
        while (xserverchan_is_ascii_digit((unsigned char)*cursor)) ++cursor;
        if (*cursor != 't' || !cursor[1]) return false;
    }
    return true;
}

static bool xserverchan_title_is_valid(const char* title)
{
    return title && *title && !strchr(title, '\r') && !strchr(title, '\n');
}

static bool xserverchan_append_form_value(XByteArray* body, const char* value)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char* cursor = (const unsigned char*)(value ? value : "");
    if (!body) return false;
    while (*cursor) {
        unsigned char c = *cursor++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
            if (!XByteArray_push_back_1(body, c)) return false;
        } else if (!XByteArray_push_back_1(body, '%') ||
                   !XByteArray_push_back_1(body, (uint8_t)hex[c >> 4]) ||
                   !XByteArray_push_back_1(body, (uint8_t)hex[c & 0x0f])) {
            return false;
        }
    }
    return true;
}

static XByteArray* xserverchan_create_form_body(const char* title, const char* desp)
{
    XByteArray* body = XByteArray_create();
    if (!body || !XByteArray_append_utf8(body, "title=") ||
        !xserverchan_append_form_value(body, title)) {
        if (body) XByteArray_delete_base(body);
        return NULL;
    }
    if (desp && (!XByteArray_append_utf8(body, "&desp=") ||
                 !xserverchan_append_form_value(body, desp))) {
        XByteArray_delete_base(body);
        return NULL;
    }
    return body;
}

static XString* xserverchan_create_endpoint(const XServerChan* self)
{
    const char* key;
    const char* uidEnd;
    if (!self) return NULL;
    if (self->m_endpointUrl) return XString_create_copy(self->m_endpointUrl);
    if (!self->m_sendKey || !(key = XString_toUtf8(self->m_sendKey)) || !*key) return NULL;
    if (strncmp(key, "sctp", 4) == 0) {
        uidEnd = key + 4;
        while (xserverchan_is_ascii_digit((unsigned char)*uidEnd)) ++uidEnd;
        if (*uidEnd != 't') return NULL;
        return XString_create_fmt_utf8("https://%.*s.push.ft07.com/send/%s.send",
                                      (int)(uidEnd - (key + 4)), key + 4, key);
    }
    return XString_create_fmt_utf8("https://sctapi.ftqq.com/%s.send", key);
}

static bool xserverchan_set_result_error(XServerChanResult* result,
                                         XServerChanResult_Error error,
                                         const char* message)
{
    if (!result) return false;
    result->m_error = error;
    result->m_success = false;
    xserverchan_result_set_message(result, message);
    return true;
}

static void xserverchan_parse_reply(XServerChanResult* result, const XHttpReply* reply)
{
    const XByteArray* body;
    XHttpReply_NetworkError networkError;
    XJsonDocument* document = NULL;
    const XJsonValue* root;
    XJsonObject* object;
    XJsonValue* codeValue;
    XJsonValue* messageValue;
    XString* codeKey = NULL;
    XString* messageKey = NULL;
    int64_t code;
    XString* errorString;
    if (!result || !reply) return;
    result->m_httpStatusCode = XHttpReply_statusCode(reply);
    body = XHttpReply_body_const(reply);
    if (body) result->m_responseBody = XByteArray_create_copy(body);
    networkError = XHttpReply_error(reply);
    if (networkError != XHttpReply_NoError) {
        errorString = XHttpReply_errorString(reply);
        result->m_error = networkError == XHttpReply_TimeoutError
            ? XServerChanResult_Timeout : XServerChanResult_NetworkError;
        result->m_success = false;
        xserverchan_result_set_message_string(result, errorString);
        if (errorString) XString_delete_base(errorString);
        return;
    }
    if (result->m_httpStatusCode < 200 || result->m_httpStatusCode >= 300) {
        xserverchan_set_result_error(result, XServerChanResult_HttpError,
                                     "Server酱 HTTP 响应状态不是 2xx");
        return;
    }
    document = XJsonDocument_fromJson(body);
    root = document ? XJsonDocument_root_const(document) : NULL;
    object = root ? XJsonValue_toObject(root) : NULL;
    codeKey = XString_create_utf8("code");
    codeValue = object && codeKey ? XJsonObject_value_base(object, codeKey) : NULL;
    if (!codeValue || !XJsonValue_isInt(codeValue)) {
        xserverchan_set_result_error(result, XServerChanResult_InvalidResponse,
                                     "Server酱响应不是包含整数 code 的 JSON 对象");
        if (codeKey) XString_delete_base(codeKey);
        if (document) XJsonDocument_delete(document);
        return;
    }
    code = XJsonValue_toInt(codeValue, -1);
    if (code > INT_MAX || code < INT_MIN) {
        xserverchan_set_result_error(result, XServerChanResult_InvalidResponse,
                                     "Server酱响应 code 超出 int 范围");
        XString_delete_base(codeKey);
        XJsonDocument_delete(document);
        return;
    }
    result->m_apiCode = (int)code;
    messageKey = XString_create_utf8("message");
    messageValue = object && messageKey ? XJsonObject_value_base(object, messageKey) : NULL;
    if (messageValue && XJsonValue_isString(messageValue)) {
        xserverchan_result_set_message_string(result, XJsonValue_toString(messageValue));
    }
    if (result->m_apiCode == 0) {
        result->m_error = XServerChanResult_NoError;
        result->m_success = true;
    } else {
        result->m_error = XServerChanResult_ApiError;
        result->m_success = false;
        if (!result->m_message || XString_isEmpty_base(result->m_message))
            xserverchan_result_set_message(result, "Server酱返回非零 code");
    }
    XString_delete_base(codeKey);
    if (messageKey) XString_delete_base(messageKey);
    XJsonDocument_delete(document);
}

static void VXServerChan_deinit(XServerChan* self)
{
    if (!self) return;
    if (self->m_manager) {
        XNetworkAccessManager_delete_base(self->m_manager);
        self->m_manager = NULL;
    }
    if (self->m_sendKey) {
        XString_delete_base(self->m_sendKey);
        self->m_sendKey = NULL;
    }
    if (self->m_endpointUrl) {
        XString_delete_base(self->m_endpointUrl);
        self->m_endpointUrl = NULL;
    }
    self->m_transferTimeout = 30000;
}

static void VXServerChan_copy(XServerChan* dest, const XServerChan* src)
{
    XNetworkAccessManager* manager = NULL;
    XString* sendKey = NULL;
    XString* endpoint = NULL;
    if (!dest || !src || dest == src || XClassIsVtableNull(src)) return;
    if (XClassIsVtableNull(dest)) XServerChan_init(dest);
    if (src->m_sendKey && !(sendKey = XString_create_copy(src->m_sendKey))) goto failed;
    if (src->m_endpointUrl && !(endpoint = XString_create_copy(src->m_endpointUrl))) goto failed;
    if (!dest->m_manager && !(manager = XNetworkAccessManager_create())) goto failed;
    if (manager) dest->m_manager = manager;
    if (dest->m_sendKey) XString_delete_base(dest->m_sendKey);
    if (dest->m_endpointUrl) XString_delete_base(dest->m_endpointUrl);
    dest->m_sendKey = sendKey;
    dest->m_endpointUrl = endpoint;
    dest->m_transferTimeout = src->m_transferTimeout;
    XNetworkAccessManager_setTransferTimeout(dest->m_manager, dest->m_transferTimeout);
    return;
failed:
    if (sendKey) XString_delete_base(sendKey);
    if (endpoint) XString_delete_base(endpoint);
}

static void VXServerChan_move(XServerChan* dest, XServerChan* src)
{
    if (!dest || !src || dest == src || XClassIsVtableNull(src)) return;
    if (XClassIsVtableNull(dest)) XServerChan_init(dest);
    if (dest->m_manager) XNetworkAccessManager_delete_base(dest->m_manager);
    if (dest->m_sendKey) XString_delete_base(dest->m_sendKey);
    if (dest->m_endpointUrl) XString_delete_base(dest->m_endpointUrl);
    dest->m_manager = src->m_manager;
    dest->m_sendKey = src->m_sendKey;
    dest->m_endpointUrl = src->m_endpointUrl;
    dest->m_transferTimeout = src->m_transferTimeout;
    src->m_manager = NULL;
    src->m_sendKey = NULL;
    src->m_endpointUrl = NULL;
    src->m_transferTimeout = 30000;
}

XVtable* XServerChan_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XServerChan)
	XCLASS_SET_CLASS_NAME_DEFAULT("XServerChan");
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXServerChan_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXServerChan_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXServerChan_move);
    XCLASS_SHOW_SIZE_DEFAULT(XServerChan);
    return XVTABLE_DEFAULT;
}

void XServerChan_init(XServerChan* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XServerChan);
    self->m_manager = XNetworkAccessManager_create();
    self->m_transferTimeout = 30000;
    if (self->m_manager)
        XNetworkAccessManager_setTransferTimeout(self->m_manager, self->m_transferTimeout);
}

XServerChan* XServerChan_create(void)
{
    XServerChan* self = (XServerChan*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XServerChan_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    if (!self->m_manager) {
        XServerChan_delete_base(self);
        return NULL;
    }
    return self;
}

XServerChan* XServerChan_create_ex(const char* sendKey)
{
    XServerChan* self = XServerChan_create();
    if (!self || !XServerChan_setSendKey_utf8(self, sendKey)) {
        if (self) XServerChan_delete_base(self);
        return NULL;
    }
    return self;
}

XServerChan* XServerChan_create_copy(const XServerChan* other)
{
    XServerChan* self;
    if (!other || XClassIsVtableNull(other)) return NULL;
    self = XServerChan_create();
    if (!self) return NULL;
    XServerChan_copy_base(self, other);
    return self;
}

XServerChan* XServerChan_create_move(XServerChan* other)
{
    XServerChan* self;
    if (!other || XClassIsVtableNull(other)) return NULL;
    self = XServerChan_create();
    if (!self) return NULL;
    XServerChan_move_base(self, other);
    return self;
}

bool XServerChan_setSendKey_utf8(XServerChan* self, const char* sendKey)
{
    XString* value;
    if (!self || !xserverchan_send_key_is_valid(sendKey)) return false;
    value = XString_create_utf8(sendKey);
    if (!value) return false;
    if (self->m_sendKey) XString_delete_base(self->m_sendKey);
    self->m_sendKey = value;
    return true;
}

bool XServerChan_setSendKey_environment(XServerChan* self, const char* variableName)
{
    const char* name = variableName ? variableName : "SERVERCHAN_SENDKEY";
    const char* value;
    if (!self || !*name || !(value = getenv(name))) return false;
    return XServerChan_setSendKey_utf8(self, value);
}

bool XServerChan_hasSendKey(const XServerChan* self)
{ return self && self->m_sendKey && xserverchan_send_key_is_valid(XString_toUtf8(self->m_sendKey)); }

bool XServerChan_setEndpointUrl_utf8(XServerChan* self, const char* endpointUrl)
{
    XString* value;
    XUrl* url;
    const XString* scheme;
    bool valid;
    if (!self) return false;
    if (!endpointUrl) {
        if (self->m_endpointUrl) XString_delete_base(self->m_endpointUrl);
        self->m_endpointUrl = NULL;
        return true;
    }
    value = XString_create_utf8(endpointUrl);
    url = value ? XUrl_create_ex(value, XUrl_TolerantMode) : NULL;
    scheme = url ? XUrl_scheme_const(url) : NULL;
    valid = url && XUrl_isValid(url) && scheme &&
        (!strcmp(XString_toUtf8(scheme), "http") || !strcmp(XString_toUtf8(scheme), "https"));
    if (url) XUrl_delete_base(url);
    if (!valid) {
        if (value) XString_delete_base(value);
        return false;
    }
    if (self->m_endpointUrl) XString_delete_base(self->m_endpointUrl);
    self->m_endpointUrl = value;
    return true;
}

bool XServerChan_setSslCaCertificateFile_utf8(XServerChan* self,
                                              const char* certificatePath)
{
    XSslCertificate* certificate;
    if (!self || !self->m_manager || !certificatePath || !*certificatePath)
        return false;
    certificate = XSsl_certificateLoad(certificatePath, XSSL_Pem);
    if (!certificate)
        return false;
    if (!XNetworkAccessManager_setSslCaCertificate(self->m_manager, certificate)) {
        XSsl_certificateDestroy(certificate);
        return false;
    }
    return true;
}

void XServerChan_setTransferTimeout(XServerChan* self, int timeout)
{
    if (!self) return;
    self->m_transferTimeout = timeout < 0 ? 0 : timeout;
    if (self->m_manager)
        XNetworkAccessManager_setTransferTimeout(self->m_manager, self->m_transferTimeout);
}

int XServerChan_transferTimeout(const XServerChan* self)
{ return self ? self->m_transferTimeout : 30000; }

XHttpReply* XServerChan_send(XServerChan* self, const char* title, const char* desp)
{
    XString* endpoint = NULL;
    XByteArray* body = NULL;
    XHttpRequest* request = NULL;
    XHttpReply* reply = NULL;
    if (!self || !self->m_manager || !XServerChan_hasSendKey(self) ||
        !xserverchan_title_is_valid(title)) return NULL;
    endpoint = xserverchan_create_endpoint(self);
    body = endpoint ? xserverchan_create_form_body(title, desp) : NULL;
    request = body ? XHttpRequest_create() : NULL;
    if (!endpoint || !body || !request ||
        !XHttpRequest_setUrl_utf8(request, XString_toUtf8(endpoint)) ||
        !XHttpRequest_setRawHeader(request, "Content-Type",
                                   "application/x-www-form-urlencoded; charset=UTF-8") ||
        !XHttpRequest_setRawHeader(request, "Accept", "application/json")) {
        if (request) XHttpRequest_delete_base(request);
        if (body) XByteArray_delete_base(body);
        if (endpoint) XString_delete_base(endpoint);
        return NULL;
    }
    reply = XNetworkAccessManager_post(self->m_manager, request, body);
    XHttpRequest_delete_base(request);
    XByteArray_delete_base(body);
    XString_delete_base(endpoint);
    return reply;
}

XServerChanResult* XServerChan_sendBlocking(XServerChan* self,
                                            const char* title,
                                            const char* desp)
{
    XServerChanResult* result = XServerChanResult_create();
    XHttpReply* reply;
    int64_t timeout;
    int64_t deadline;
    int64_t settleDeadline;
    if (!result) return NULL;
    if (!self || !self->m_manager || !XServerChan_hasSendKey(self)) {
        xserverchan_set_result_error(result, XServerChanResult_InvalidSendKey,
                                     "Server酱客户端未设置有效 SendKey");
        return result;
    }
    if (!xserverchan_title_is_valid(title)) {
        xserverchan_set_result_error(result, XServerChanResult_InvalidArgument,
                                     "Server酱消息标题不能为空且不能包含换行");
        return result;
    }
    if (!XCoreApplication_instance()) {
        xserverchan_set_result_error(result, XServerChanResult_NoEventLoop,
                                     "同步发送需要先创建 XCoreApplication");
        return result;
    }
    reply = XServerChan_send(self, title, desp);
    if (!reply) {
        xserverchan_set_result_error(result, XServerChanResult_InvalidEndpoint,
                                     "Server酱 API 端点无效或 HTTP 请求创建失败");
        return result;
    }
    timeout = self->m_transferTimeout > 0 ? self->m_transferTimeout : 30000;
    deadline = XDateTime_currentMSecsSinceEpoch() + timeout;
    while (!XHttpReply_isFinished(reply)) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) {
            XHttpReply_abort(reply);
            xserverchan_set_result_error(result, XServerChanResult_Timeout,
                                         "Server酱请求超时");
            break;
        }
        XThread_msleep(1);
    }
    if (XHttpReply_isFinished(reply) && result->m_error == XServerChanResult_InvalidArgument)
        xserverchan_parse_reply(result, reply);
    /* 管理器会把事务清理投递到事件队列；释放 reply 前必须先让借用指针失效。 */
    settleDeadline = XDateTime_currentMSecsSinceEpoch() + 1000;
    while (XNetworkAccessManager_activeReplyCount(self->m_manager) != 0 &&
           XDateTime_currentMSecsSinceEpoch() < settleDeadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XNetworkAccessManager_activeReplyCount(self->m_manager) != 0)
            XThread_msleep(1);
    }
    XHttpReply_delete_base(reply);
    return result;
}
