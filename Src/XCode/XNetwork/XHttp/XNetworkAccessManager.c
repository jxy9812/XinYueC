/**
 * @file       XNetworkAccessManager.c
 * @brief      HTTP/HTTPS 异步访问管理器实现。
 */

#include "XNetworkAccessManager.h"

#include "XBase64.h"
#include "XCryptographicHash.h"
#include "XMemory.h"
#include "XDir.h"
#include "XFile.h"
#include "XDateTime.h"
#include "XSslSocket.h"
#include "XTcpSocket.h"
#include "XHttp2Client.h"
#include "XHttp2Connection.h"
#include "XHttp2Frame.h"
#include "XString.h"
#include "XRandomGenerator.h"
#include "XCoreApplication.h"
#include "XEvent.h"
#include "XVarList.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/** @brief RFC 7540/9113 连接和流错误码。 */
#define XHTTP2_ERROR_NO_ERROR UINT32_C(0x0)
#define XHTTP2_ERROR_PROTOCOL_ERROR UINT32_C(0x1)
#define XHTTP2_ERROR_FLOW_CONTROL_ERROR UINT32_C(0x3)
#define XHTTP2_ERROR_REFUSE_STREAM UINT32_C(0x7)

/* NTLMSSP 标志位，取自 Qt 6.8 qauthenticator.cpp 的可移植 NTLMv2 实现。 */
#define XHTTP_NTLM_NEGOTIATE_UNICODE UINT32_C(0x00000001)
#define XHTTP_NTLM_NEGOTIATE_OEM UINT32_C(0x00000002)
#define XHTTP_NTLM_REQUEST_TARGET UINT32_C(0x00000004)
#define XHTTP_NTLM_NEGOTIATE_NTLM UINT32_C(0x00000200)
#define XHTTP_NTLM_NEGOTIATE_ALWAYS_SIGN UINT32_C(0x00008000)
#define XHTTP_NTLM_NEGOTIATE_NTLM2 UINT32_C(0x00080000)
#define XHTTP_NTLM_NEGOTIATE_TARGET_INFO UINT32_C(0x00800000)

typedef struct XHttpTransaction XHttpTransaction;
typedef struct XHttp2SharedConnection XHttp2SharedConnection;
typedef struct XHttpAuthenticationCacheEntry XHttpAuthenticationCacheEntry;

/* 凭据按来源、代理边界、realm 与认证方案隔离，避免跨站点或跨代理泄漏。 */
struct XHttpAuthenticationCacheEntry {
    XByteArray* m_origin;                  /**< 规范化的方案/主机/端口键；条目拥有。 */
    XByteArray* m_realm;                   /**< 服务端声明的 realm；条目拥有。 */
    XHttpAuthenticator* m_authenticator;   /**< 已验证凭据副本；条目拥有。 */
    bool m_proxy;                          /**< true 表示代理认证缓存。 */
};

struct XHttpTransaction {
    XNetworkAccessManager* m_manager;       /**< 管理器借用指针。 */
    XHttpReply* m_reply;                    /**< 响应所有权由调用者持有。 */
    XAbstractSocket* m_socket;              /**< 底层套接字所有权由事务持有。 */
    XHttp2SharedConnection* m_http2Connection; /**< HTTP/2 共享连接；借用，可为空。 */
    XConnection* m_connected;               /**< TCP connected 连接。 */
    XConnection* m_encrypted;               /**< TLS encrypted 连接。 */
    XConnection* m_readyRead;               /**< readyRead 连接。 */
    XConnection* m_disconnected;            /**< disconnected 连接。 */
    XConnection* m_errorOccurred;           /**< errorOccurred 连接。 */
    XConnection* m_replyFinished;            /**< reply finished 连接。 */
    XByteArray* m_requestWire;              /**< 待发送请求字节；由事务拥有。 */
    XHttp2ClientSession* m_http2Session;    /**< HTTP/2 客户端会话；由事务拥有。 */
    XHttp2HeaderDecoder* m_http2Decoder;   /**< HPACK 连接级解码器；由事务拥有。 */
    XByteArray* m_http2Input;               /**< 未组成完整 HTTP/2 帧的输入；由事务拥有。 */
    XByteArray* m_http2HeaderBlock;         /**< 正在接收的连续头块；由事务拥有。 */
    uint32_t m_http2StreamId;               /**< 当前请求的 HTTP/2 流号。 */
    int64_t m_http2SessionRecvWindow;       /**< 本地连接接收窗口。 */
    int64_t m_http2StreamRecvWindow;        /**< 本地流接收窗口。 */
    int64_t m_http2SessionSendWindow;       /**< 对端连接发送窗口。 */
    int64_t m_http2StreamSendWindow;        /**< 对端流发送窗口。 */
    uint32_t m_http2PeerInitialWindowSize;  /**< 对端初始流发送窗口。 */
    uint32_t m_http2SessionRecvTarget;      /**< 本地连接窗口目标值。 */
    uint32_t m_http2StreamRecvTarget;       /**< 本地流窗口目标值。 */
    uint32_t m_http2PeerMaxFrameSize;       /**< 对端允许的帧 payload 上限。 */
    uint32_t m_http2PeerMaxConcurrentStreams; /**< 对端允许的并发流数。 */
    size_t m_http2PeerMaxHeaderListSize;     /**< 对端允许的解压后头部大小。 */
    uint32_t m_http2RemoteLastStreamId;      /**< 对端 GOAWAY 的最后可处理流号。 */
    size_t m_http2FramePayloadOffset;       /**< 当前待发送 DATA 帧已消费字节数。 */
    size_t m_writeOffset;                   /**< 已写请求字节数。 */
    int m_redirectCount;                    /**< 已跟随的重定向次数。 */
    unsigned int m_authenticationAttempts;  /**< 当前请求已尝试认证重发的次数。 */
    XHttpAuthenticator* m_ntlmAuthenticator; /**< NTLM 两阶段协商凭据副本；由事务拥有。 */
    bool m_ntlmProxy;                        /**< NTLM 凭据副本是否用于代理认证。 */
    bool m_authenticationCacheUsed;         /**< 本次请求是否已尝试过缓存凭据。 */
    bool m_tls;                              /**< 是否使用 TLS。 */
    bool m_http2;                            /**< TLS ALPN 是否选择了 HTTP/2。 */
    bool m_h2cUpgradeRequested;              /**< 是否已发送 h2c Upgrade 请求。 */
    bool m_http2HeadersActive;               /**< 是否正在等待 CONTINUATION。 */
    bool m_http2PushPromiseActive;           /**< 是否正在接收 PUSH_PROMISE 头块。 */
    uint32_t m_http2PushPromisedStreamId;    /**< 当前 PUSH_PROMISE 的被承诺流号。 */
    bool m_http2HeaderEndStream;             /**< 初始 HEADERS 是否带 END_STREAM。 */
    bool m_http2ResponseHeadersReceived;     /**< 是否已收到最终响应头。 */
    bool m_http2SettingsAckPending;          /**< 本端 SETTINGS 是否等待 ACK。 */
    bool m_http2GoAwayReceived;              /**< 是否已收到对端 GOAWAY。 */
    bool m_http2GoAwaySent;                  /**< 是否已发送连接级 GOAWAY。 */
    bool m_requestSent;                     /**< 是否已经写出整个请求。 */
    bool m_finished;                        /**< 是否已完成清理。 */
    bool m_redirectFinished;                /**< 已拒绝自动重定向，等待最终信号。 */
    bool m_completionPosted;                /**< 是否已投递完成清理事件。 */
    XTimerId m_timeoutTimer;                /**< 本事务传输超时定时器。 */
};

/**
 * @brief 管理器内部的同源 HTTP/2 复用连接。
 * @details 套接字和协议状态机由本结构独占；transactions 中仅保存借用事务指针。
 *          不跨域合并连接，保证与请求的 authority 和 TLS 主机校验边界一致。
 */
struct XHttp2SharedConnection {
    XNetworkAccessManager* m_manager;       /**< 所属管理器；借用。 */
    XAbstractSocket* m_socket;              /**< TLS 套接字；连接拥有。 */
    XHttp2Connection* m_protocol;           /**< RFC 9113 状态机；连接拥有。 */
    XVector* m_transactions;                /**< XHttpTransaction* 借用列表。 */
    XString* m_host;                        /**< 同源主机名；连接拥有。 */
    uint16_t m_port;                        /**< 同源有效端口。 */
    XConnection* m_readyRead;               /**< readyRead 信号连接。 */
    XConnection* m_disconnected;            /**< disconnected 信号连接。 */
    XConnection* m_errorOccurred;           /**< errorOccurred 信号连接。 */
    bool m_usable;                          /**< false 表示已关闭、GOAWAY 或错误。 */
    bool m_tls;                             /**< true 表示该共享连接使用 TLS。 */
};

static void xhttp_manager_transaction_finish(XHttpTransaction* tx);
static void xhttp_manager_socket_connected(XObject* receiver, XVarList* args);
static void xhttp_manager_socket_encrypted(XObject* receiver, XVarList* args);
static void xhttp_manager_socket_ready_read(XObject* receiver, XVarList* args);
static void xhttp_manager_socket_disconnected(XObject* receiver, XVarList* args);
static void xhttp_manager_socket_error(XObject* receiver, XVarList* args);
static void xhttp_manager_reply_finished(XObject* receiver, XVarList* args);
static void VXNetworkAccessManager_deinit(XNetworkAccessManager* self);
static void xhttp_manager_disconnect_socket(XHttpTransaction* tx);
static bool xhttp_manager_send_pending(XHttpTransaction* tx);
static bool xhttp_manager_send_http2_pending(XHttpTransaction* tx);
static void xhttp_manager_complete(XHttpTransaction* tx);
static void xhttp_manager_deferred_redirect_event(XVarList* args);
static bool xhttp_manager_schedule_redirect_event(XHttpTransaction* tx);
static void xhttp_manager_handle_reply_state(XHttpTransaction* tx);
static void xhttp_manager_deferred_complete_event(XVarList* args);
static void VXNetworkAccessManager_timerEvent(XNetworkAccessManager* self, XTimerEvent* event);
static bool xhttp_manager_text_equal(const char* left, const char* right);
static bool xhttp_manager_hsts_insert(XNetworkAccessManager* manager,
                                      const XHstsPolicy* policy);
static void xhttp_manager_reset_http2(XHttpTransaction* tx);
static bool xhttp_manager_send_http2_frame(XHttpTransaction* tx, uint8_t type,
                                           uint8_t flags, uint32_t streamId,
                                           const XByteArray* payload);
static bool xhttp_manager_process_http2_frame(XHttpTransaction* tx,
                                              const XHttp2Frame* frame);
static void xhttp_manager_shared_http2_destroy(XHttp2SharedConnection* connection);
static void xhttp_manager_shared_http2_detach(XHttpTransaction* tx);
static void xhttp_manager_shared_http2_read_available(XHttp2SharedConnection* connection);
static void xhttp_manager_shared_http2_fail(XHttp2SharedConnection* connection,
                                            XHttpReply_NetworkError error,
                                            const char* message);
static bool xhttp_manager_shared_http2_flush(XHttp2SharedConnection* connection);
static void xhttp_manager_shared_http2_dispatch(XHttp2SharedConnection* connection);
static bool xhttp_manager_promote_h2c_upgrade(XHttpTransaction* tx);
static bool xhttp_manager_retry_http2_authentication(XHttpTransaction* tx);
static bool xhttp_manager_retry_authentication(XHttpTransaction* tx);
static XByteArray* xhttp_manager_authentication_parameter(const XByteArray* challenge,
                                                           const char* name);
static bool xhttp_manager_digest_algorithm(const XByteArray* value,
                                           XCryptographicHash_Algorithm* algorithm,
                                           bool* session);
static bool xhttp_manager_qop_allows_auth(const XByteArray* value);

static bool xhttp_manager_request_bool_attribute(const XHttpRequest* request,
                                                 XHttpRequest_Attribute attribute)
{
    XVariant* value;
    bool result = false;
    if (!request)
        return false;
    value = XHttpRequest_attribute(request, attribute);
    if (value && XVariant_type(value) == XVariantType_Bool)
        result = XVariant_toBool(value);
    if (value)
        XClass_delete_base((XClass*)value);
    return result;
}

static bool xhttp_manager_ascii_equal_n(const uint8_t* data, size_t size, const char* text)
{
    size_t textSize = text ? strlen(text) : 0;
    if (!data || !text || size != textSize)
        return false;
    for (size_t i = 0; i < size; ++i) {
        uint8_t value = data[i];
        uint8_t expected = (uint8_t)text[i];
        if (value >= 'A' && value <= 'Z') value = (uint8_t)(value - 'A' + 'a');
        if (expected >= 'A' && expected <= 'Z') expected = (uint8_t)(expected - 'A' + 'a');
        if (value != expected)
            return false;
    }
    return true;
}

/* 从 WWW-Authenticate/Proxy-Authenticate 的首个挑战中提取方法和 realm。 */
static bool xhttp_manager_parse_authentication_challenge(const XByteArray* challenge,
                                                         XHttpAuthenticator* authenticator)
{
    const uint8_t* data = challenge ? XByteArray_constData((XByteArray*)challenge) : NULL;
    size_t size = challenge ? XContainer_size_base((const XContainer*)challenge) : 0;
    size_t methodBegin = 0;
    size_t methodEnd;
    size_t realmBegin = 0;
    size_t realmEnd = 0;
    XHttpAuthenticator_Method method = XHttpAuthenticator_None;
    XByteArray* realm = NULL;
    bool result;
    if (!data || !size || !authenticator)
        return false;
    while (methodBegin < size && (data[methodBegin] == ' ' || data[methodBegin] == '\t'))
        ++methodBegin;
    methodEnd = methodBegin;
    while (methodEnd < size && data[methodEnd] != ' ' && data[methodEnd] != '\t' &&
           data[methodEnd] != ',')
        ++methodEnd;
    if (xhttp_manager_ascii_equal_n(data + methodBegin, methodEnd - methodBegin, "basic"))
        method = XHttpAuthenticator_Basic;
    else if (xhttp_manager_ascii_equal_n(data + methodBegin, methodEnd - methodBegin, "digest"))
        method = XHttpAuthenticator_Digest;
    else if (xhttp_manager_ascii_equal_n(data + methodBegin, methodEnd - methodBegin, "ntlm"))
        method = XHttpAuthenticator_Ntlm;
    else if (xhttp_manager_ascii_equal_n(data + methodBegin, methodEnd - methodBegin, "negotiate"))
        method = XHttpAuthenticator_Negotiate;
    for (size_t i = methodEnd; i + 6 <= size; ++i) {
        if (!xhttp_manager_ascii_equal_n(data + i, 5, "realm") || data[i + 5] != '=')
            continue;
        realmBegin = i + 6;
        while (realmBegin < size && (data[realmBegin] == ' ' || data[realmBegin] == '\t'))
            ++realmBegin;
        if (realmBegin < size && data[realmBegin] == '"') {
            ++realmBegin;
            realmEnd = realmBegin;
            while (realmEnd < size && data[realmEnd] != '"')
                ++realmEnd;
        } else {
            realmEnd = realmBegin;
            while (realmEnd < size && data[realmEnd] != ',' &&
                   data[realmEnd] != ' ' && data[realmEnd] != '\t')
                ++realmEnd;
        }
        break;
    }
    if (realmEnd > realmBegin)
        realm = XByteArray_create_with_data((const char*)data + realmBegin, realmEnd - realmBegin);
    result = (realmEnd == realmBegin || realm) &&
             XHttpAuthenticator_setChallenge(authenticator, method, realm);
    if (realm)
        XClass_delete_base((XClass*)realm);
    return result;
}

/* Qt 会遍历同名认证字段并在支持的方案中选择优先级最高者。 */
static XHttpAuthenticator_Method xhttp_manager_authentication_challenge_method(
    const XByteArray* challenge)
{
    const uint8_t* data = challenge ? XByteArray_constData((XByteArray*)challenge) : NULL;
    size_t size = challenge ? XContainer_size_base((const XContainer*)challenge) : 0;
    size_t begin = 0;
    size_t end;
    if (!data || size == 0)
        return XHttpAuthenticator_None;
    while (begin < size && (data[begin] == ' ' || data[begin] == '\t'))
        ++begin;
    end = begin;
    while (end < size && data[end] != ' ' && data[end] != '\t' && data[end] != ',')
        ++end;
    if (xhttp_manager_ascii_equal_n(data + begin, end - begin, "digest"))
        return XHttpAuthenticator_Digest;
    if (xhttp_manager_ascii_equal_n(data + begin, end - begin, "ntlm"))
        return XHttpAuthenticator_Ntlm;
    if (xhttp_manager_ascii_equal_n(data + begin, end - begin, "basic"))
        return XHttpAuthenticator_Basic;
    return XHttpAuthenticator_None;
}

static unsigned int xhttp_manager_authentication_method_priority(
    XHttpAuthenticator_Method method)
{
    if (method == XHttpAuthenticator_Digest)
        return 3;
    if (method == XHttpAuthenticator_Ntlm)
        return 2;
    if (method == XHttpAuthenticator_Basic)
        return 1;
    return 0;
}

/* 只提升当前实现能够构造 Authorization 的 Digest 挑战，保留 Basic 回退路径。 */
static bool xhttp_manager_authentication_challenge_supported(
    const XByteArray* challenge, XHttpAuthenticator_Method method)
{
    XByteArray* nonce;
    XByteArray* algorithmName;
    XByteArray* qop;
    XCryptographicHash_Algorithm algorithm;
    bool session;
    bool result;
    if (method == XHttpAuthenticator_Basic)
        return true;
    if (method == XHttpAuthenticator_Ntlm)
        return true;
    if (method != XHttpAuthenticator_Digest)
        return false;
    nonce = xhttp_manager_authentication_parameter(challenge, "nonce");
    algorithmName = xhttp_manager_authentication_parameter(challenge, "algorithm");
    qop = xhttp_manager_authentication_parameter(challenge, "qop");
    result = nonce && xhttp_manager_digest_algorithm(algorithmName, &algorithm, &session) &&
             xhttp_manager_qop_allows_auth(qop);
    if (qop)
        XClass_delete_base((XClass*)qop);
    if (algorithmName)
        XClass_delete_base((XClass*)algorithmName);
    if (nonce)
        XClass_delete_base((XClass*)nonce);
    return result;
}

/* 从所有 WWW-Authenticate/Proxy-Authenticate 字段中挑选本实现可重发的最佳挑战。 */
static XByteArray* xhttp_manager_select_authentication_challenge(const XHttpReply* reply,
                                                                  bool proxy)
{
    XByteArray* name;
    XVector* values;
    XByteArray* selected = NULL;
    unsigned int selectedPriority = 0;
    if (!reply)
        return NULL;
    name = XByteArray_create_utf8(proxy ? "proxy-authenticate" : "www-authenticate");
    values = name ? XHttpHeaders_values(XHttpReply_headers_const(reply), name) : NULL;
    if (name)
        XClass_delete_base((XClass*)name);
    if (!values)
        return NULL;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)values); ++i) {
        XByteArray** slot = (XByteArray**)XVector_at_base(values, (int64_t)i);
        XHttpAuthenticator_Method method = slot && *slot ?
            xhttp_manager_authentication_challenge_method(*slot) : XHttpAuthenticator_None;
        unsigned int priority = xhttp_manager_authentication_method_priority(method);
        if (!slot || !*slot || !xhttp_manager_authentication_challenge_supported(*slot, method))
            continue;
        if (priority > selectedPriority) {
            XByteArray* replacement = XByteArray_create_copy(*slot);
            if (!replacement) {
                if (selected)
                    XClass_delete_base((XClass*)selected);
                XHttpHeaders_values_free(values);
                return NULL;
            }
            if (selected)
                XClass_delete_base((XClass*)selected);
            selected = replacement;
            selectedPriority = priority;
            if (selectedPriority == 3)
                break;
        }
    }
    XHttpHeaders_values_free(values);
    return selected;
}

static bool xhttp_manager_apply_basic_authentication(XHttpRequest* request,
                                                     const XHttpAuthenticator* authenticator,
                                                     bool proxy)
{
    const XByteArray* user = XHttpAuthenticator_user_const(authenticator);
    const XByteArray* password = XHttpAuthenticator_password_const(authenticator);
    XByteArray* credentials = NULL;
    XByteArray* encoded = NULL;
    char* value = NULL;
    size_t encodedSize;
    bool result = false;
    if (!request || !user || !password)
        return false;
    credentials = XByteArray_create();
    if (!credentials ||
        !XByteArray_push_back_2((XVector*)credentials, XByteArray_constData((XByteArray*)user),
                                XContainer_size_base((const XContainer*)user)) ||
        !XByteArray_append_utf8(credentials, ":") ||
        !XByteArray_push_back_2((XVector*)credentials, XByteArray_constData((XByteArray*)password),
                                XContainer_size_base((const XContainer*)password)))
        goto done;
    encoded = XByteArray_toBase64(credentials);
    encodedSize = encoded ? XContainer_size_base((const XContainer*)encoded) : 0;
    value = encoded ? (char*)XMalloc_System(sizeof("Basic ") - 1 + encodedSize + 1) : NULL;
    if (!value)
        goto done;
    memcpy(value, "Basic ", sizeof("Basic ") - 1);
    if (encodedSize)
        memcpy(value + sizeof("Basic ") - 1, XByteArray_constData(encoded), encodedSize);
    value[sizeof("Basic ") - 1 + encodedSize] = '\0';
    result = XHttpRequest_setRawHeader(request,
        proxy ? "Proxy-Authorization" : "Authorization", value);
done:
    if (value) XFree_System(value);
    if (encoded) XClass_delete_base((XClass*)encoded);
    if (credentials) XClass_delete_base((XClass*)credentials);
    return result;
}

static bool xhttp_manager_byte_array_ascii_equal(const XByteArray* value, const char* text)
{
    return value && xhttp_manager_ascii_equal_n(XByteArray_constData((XByteArray*)value),
        XContainer_size_base((const XContainer*)value), text);
}

/* 从认证挑战参数中读取一个值，支持带引号值和反斜线转义。 */
static XByteArray* xhttp_manager_authentication_parameter(const XByteArray* challenge,
                                                           const char* name)
{
    const uint8_t* data = challenge ? XByteArray_constData((XByteArray*)challenge) : NULL;
    size_t size = challenge ? XContainer_size_base((const XContainer*)challenge) : 0;
    size_t nameSize = name ? strlen(name) : 0;
    if (!data || !nameSize)
        return NULL;
    for (size_t i = 0; i + nameSize + 1 <= size; ++i) {
        size_t begin;
        size_t end;
        XByteArray* result;
        if (i && data[i - 1] != ',' && data[i - 1] != ' ' && data[i - 1] != '\t')
            continue;
        if (!xhttp_manager_ascii_equal_n(data + i, nameSize, name))
            continue;
        begin = i + nameSize;
        while (begin < size && (data[begin] == ' ' || data[begin] == '\t'))
            ++begin;
        if (begin >= size || data[begin] != '=')
            continue;
        ++begin;
        while (begin < size && (data[begin] == ' ' || data[begin] == '\t'))
            ++begin;
        if (begin >= size)
            return XByteArray_create();
        if (data[begin] != '"') {
            end = begin;
            while (end < size && data[end] != ',' && data[end] != ' ' && data[end] != '\t')
                ++end;
            return XByteArray_create_with_data((const char*)data + begin, end - begin);
        }
        ++begin;
        result = XByteArray_create();
        if (!result)
            return NULL;
        for (end = begin; end < size && data[end] != '"'; ++end) {
            if (data[end] == '\\' && end + 1 < size)
                ++end;
            if (!XByteArray_push_back_2((XVector*)result, data + end, 1)) {
                XClass_delete_base((XClass*)result);
                return NULL;
            }
        }
        return result;
    }
    return NULL;
}

static XByteArray* xhttp_manager_hash_hex(const XByteArray* value,
                                          XCryptographicHash_Algorithm algorithm)
{
    static const char hex[] = "0123456789abcdef";
    XByteArray* digest;
    XByteArray* result;
    size_t size;
    if (!value)
        return NULL;
    digest = XCryptographicHash_hash((const char*)XByteArray_constData((XByteArray*)value),
                                     XContainer_size_base((const XContainer*)value), algorithm);
    if (!digest)
        return NULL;
    size = XContainer_size_base((const XContainer*)digest);
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base((XVector*)result, size * 2)) {
        if (result) XClass_delete_base((XClass*)result);
        XClass_delete_base((XClass*)digest);
        return NULL;
    }
    for (size_t i = 0; i < size; ++i) {
        uint8_t valueByte = XByteArray_constData(digest)[i];
        XByteArray_data(result)[i * 2] = (uint8_t)hex[valueByte >> 4];
        XByteArray_data(result)[i * 2 + 1] = (uint8_t)hex[valueByte & 0x0f];
    }
    XClass_delete_base((XClass*)digest);
    return result;
}

static bool xhttp_manager_append_literal(XByteArray* target, const char* text)
{
    return target && text && XByteArray_append_utf8(target, text);
}

static bool xhttp_manager_append_quoted(XByteArray* target, const XByteArray* value)
{
    const uint8_t* data = value ? XByteArray_constData((XByteArray*)value) : NULL;
    size_t size = value ? XContainer_size_base((const XContainer*)value) : 0;
    if (!target || (size && !data) || !xhttp_manager_append_literal(target, "\""))
        return false;
    for (size_t i = 0; i < size; ++i) {
        if ((data[i] == '\\' || data[i] == '"') &&
            !xhttp_manager_append_literal(target, "\\"))
            return false;
        if (!XByteArray_push_back_2((XVector*)target, data + i, 1))
            return false;
    }
    return xhttp_manager_append_literal(target, "\"");
}

static const char* xhttp_manager_request_method_name(const XHttpRequest* request)
{
    const XByteArray* custom;
    if (!request)
        return NULL;
    switch (XHttpRequest_method(request)) {
    case XHttpRequest_Head: return "HEAD";
    case XHttpRequest_Get: return "GET";
    case XHttpRequest_Post: return "POST";
    case XHttpRequest_Put: return "PUT";
    case XHttpRequest_Delete: return "DELETE";
    case XHttpRequest_Patch: return "PATCH";
    case XHttpRequest_Custom:
        custom = XHttpRequest_customMethod_const(request);
        return custom ? (const char*)XByteArray_constData((XByteArray*)custom) : NULL;
    default:
        return NULL;
    }
}

static XByteArray* xhttp_manager_request_digest_uri(const XHttpRequest* request)
{
    const XUrl* url = request ? XHttpRequest_url_const(request) : NULL;
    const XString* path = url ? XUrl_path_const(url) : NULL;
    const XString* query = url ? XUrl_query_const(url) : NULL;
    const char* pathData = path ? XString_toUtf8(path) : NULL;
    const char* queryData = query ? XString_toUtf8(query) : NULL;
    size_t pathSize = path ? XString_toUtf8_length(path) : 0;
    size_t querySize = query ? XString_toUtf8_length(query) : 0;
    XByteArray* result = XByteArray_create();
    if (!result || (pathSize && !pathData) || (querySize && !queryData) ||
        !(pathSize ? XByteArray_push_back_2((XVector*)result, pathData, pathSize) :
          XByteArray_append_utf8(result, "/")) ||
        (querySize && (!XByteArray_append_utf8(result, "?") ||
                       !XByteArray_push_back_2((XVector*)result, queryData, querySize)))) {
        if (result) XClass_delete_base((XClass*)result);
        return NULL;
    }
    return result;
}

static bool xhttp_manager_digest_algorithm(const XByteArray* value,
                                           XCryptographicHash_Algorithm* algorithm,
                                           bool* session)
{
    if (!algorithm || !session)
        return false;
    *algorithm = XCryptographicHash_Md5;
    *session = false;
    if (!value || XContainer_size_base((const XContainer*)value) == 0 ||
        xhttp_manager_byte_array_ascii_equal(value, "md5"))
        return true;
    if (xhttp_manager_byte_array_ascii_equal(value, "md5-sess")) {
        *session = true;
        return true;
    }
    if (xhttp_manager_byte_array_ascii_equal(value, "sha-256")) {
        *algorithm = XCryptographicHash_Sha256;
        return true;
    }
    if (xhttp_manager_byte_array_ascii_equal(value, "sha-256-sess")) {
        *algorithm = XCryptographicHash_Sha256;
        *session = true;
        return true;
    }
    return false;
}

static bool xhttp_manager_qop_allows_auth(const XByteArray* value)
{
    const uint8_t* data = value ? XByteArray_constData((XByteArray*)value) : NULL;
    size_t size = value ? XContainer_size_base((const XContainer*)value) : 0;
    size_t begin = 0;
    if (!value)
        return true;
    while (begin < size) {
        size_t end;
        while (begin < size && (data[begin] == ' ' || data[begin] == '\t' || data[begin] == ','))
            ++begin;
        end = begin;
        while (end < size && data[end] != ',')
            ++end;
        while (end > begin && (data[end - 1] == ' ' || data[end - 1] == '\t'))
            --end;
        if (xhttp_manager_ascii_equal_n(data + begin, end - begin, "auth"))
            return true;
        begin = end < size ? end + 1 : end;
    }
    return false;
}

static XByteArray* xhttp_manager_bytes_to_hex(const uint8_t* data, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    XByteArray* result;
    if (!data || size > SIZE_MAX / 2)
        return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base((XVector*)result, size * 2)) {
        if (result) XClass_delete_base((XClass*)result);
        return NULL;
    }
    for (size_t i = 0; i < size; ++i) {
        XByteArray_data(result)[i * 2] = (uint8_t)hex[data[i] >> 4];
        XByteArray_data(result)[i * 2 + 1] = (uint8_t)hex[data[i] & 0x0f];
    }
    return result;
}

static bool xhttp_manager_append_request_method(XByteArray* target, const XHttpRequest* request)
{
    const XByteArray* custom;
    if (!target || !request)
        return false;
    if (XHttpRequest_method(request) == XHttpRequest_Custom) {
        custom = XHttpRequest_customMethod_const(request);
        return custom && XByteArray_push_back_2((XVector*)target,
            XByteArray_constData((XByteArray*)custom),
            XContainer_size_base((const XContainer*)custom));
    }
    return xhttp_manager_append_literal(target, xhttp_manager_request_method_name(request));
}

static bool xhttp_manager_apply_digest_authentication(XHttpRequest* request,
                                                      const XHttpAuthenticator* authenticator,
                                                      const XByteArray* challenge,
                                                      bool proxy,
                                                      unsigned int attempt)
{
    const XByteArray* user = XHttpAuthenticator_user_const(authenticator);
    const XByteArray* password = XHttpAuthenticator_password_const(authenticator);
    const XByteArray* realm = XHttpAuthenticator_realm_const(authenticator);
    XByteArray* nonce = NULL;
    XByteArray* opaque = NULL;
    XByteArray* algorithmName = NULL;
    XByteArray* qop = NULL;
    XByteArray* uri = NULL;
    XByteArray* ha1Input = NULL;
    XByteArray* ha1 = NULL;
    XByteArray* ha2Input = NULL;
    XByteArray* ha2 = NULL;
    XByteArray* responseInput = NULL;
    XByteArray* response = NULL;
    XByteArray* cnonce = NULL;
    XByteArray* header = NULL;
    XCryptographicHash_Algorithm algorithm;
    uint8_t randomBytes[16];
    char nonceCount[9];
    bool sessionAlgorithm;
    bool useQop;
    char* rawHeader = NULL;
    bool result = false;
    if (!request || !user || !password || !realm || !challenge)
        goto done;
    nonce = xhttp_manager_authentication_parameter(challenge, "nonce");
    opaque = xhttp_manager_authentication_parameter(challenge, "opaque");
    algorithmName = xhttp_manager_authentication_parameter(challenge, "algorithm");
    qop = xhttp_manager_authentication_parameter(challenge, "qop");
    if (!nonce || !xhttp_manager_digest_algorithm(algorithmName, &algorithm, &sessionAlgorithm) ||
        !xhttp_manager_qop_allows_auth(qop))
        goto done;
    useQop = qop != NULL;
    if (!XRandomGenerator_fillSecure(randomBytes, sizeof(randomBytes))) {
        for (size_t i = 0; i < sizeof(randomBytes); ++i)
            randomBytes[i] = (uint8_t)(XRandomGenerator_random() >> ((i % 4) * 8));
    }
    cnonce = xhttp_manager_bytes_to_hex(randomBytes, sizeof(randomBytes));
    uri = xhttp_manager_request_digest_uri(request);
    ha1Input = XByteArray_create();
    ha2Input = XByteArray_create();
    if (!cnonce || !uri || !ha1Input || !ha2Input ||
        !XByteArray_push_back_2((XVector*)ha1Input, XByteArray_constData((XByteArray*)user),
                                XContainer_size_base((const XContainer*)user)) ||
        !xhttp_manager_append_literal(ha1Input, ":") ||
        !XByteArray_push_back_2((XVector*)ha1Input, XByteArray_constData((XByteArray*)realm),
                                XContainer_size_base((const XContainer*)realm)) ||
        !xhttp_manager_append_literal(ha1Input, ":") ||
        !XByteArray_push_back_2((XVector*)ha1Input, XByteArray_constData((XByteArray*)password),
                                XContainer_size_base((const XContainer*)password)) ||
        !xhttp_manager_append_request_method(ha2Input, request) ||
        !xhttp_manager_append_literal(ha2Input, ":") ||
        !XByteArray_push_back_2((XVector*)ha2Input, XByteArray_constData(uri),
                                XContainer_size_base((const XContainer*)uri)))
        goto done;
    ha1 = xhttp_manager_hash_hex(ha1Input, algorithm);
    if (!ha1)
        goto done;
    if (sessionAlgorithm) {
        XByteArray* sessionInput = XByteArray_create();
        XByteArray* replacement;
        if (!sessionInput ||
            !XByteArray_push_back_2((XVector*)sessionInput, XByteArray_constData(ha1),
                                    XContainer_size_base((const XContainer*)ha1)) ||
            !xhttp_manager_append_literal(sessionInput, ":") ||
            !XByteArray_push_back_2((XVector*)sessionInput, XByteArray_constData(nonce),
                                    XContainer_size_base((const XContainer*)nonce)) ||
            !xhttp_manager_append_literal(sessionInput, ":") ||
            !XByteArray_push_back_2((XVector*)sessionInput, XByteArray_constData(cnonce),
                                    XContainer_size_base((const XContainer*)cnonce))) {
            if (sessionInput) XClass_delete_base((XClass*)sessionInput);
            goto done;
        }
        replacement = xhttp_manager_hash_hex(sessionInput, algorithm);
        XClass_delete_base((XClass*)sessionInput);
        if (!replacement)
            goto done;
        XClass_delete_base((XClass*)ha1);
        ha1 = replacement;
    }
    ha2 = xhttp_manager_hash_hex(ha2Input, algorithm);
    responseInput = XByteArray_create();
    if (!ha2 || !responseInput)
        goto done;
    for (size_t i = 0; i < 8; ++i) {
        unsigned int shift = (7u - (unsigned int)i) * 4u;
        nonceCount[i] = "0123456789abcdef"[((attempt + 1u) >> shift) & 0x0fu];
    }
    nonceCount[8] = '\0';
    if (!XByteArray_push_back_2((XVector*)responseInput, XByteArray_constData(ha1),
                                XContainer_size_base((const XContainer*)ha1)) ||
        !xhttp_manager_append_literal(responseInput, ":") ||
        !XByteArray_push_back_2((XVector*)responseInput, XByteArray_constData(nonce),
                                XContainer_size_base((const XContainer*)nonce)) ||
        !xhttp_manager_append_literal(responseInput, ":"))
        goto done;
    if (useQop) {
        if (!xhttp_manager_append_literal(responseInput, nonceCount) ||
            !xhttp_manager_append_literal(responseInput, ":") ||
            !XByteArray_push_back_2((XVector*)responseInput, XByteArray_constData(cnonce),
                                    XContainer_size_base((const XContainer*)cnonce)) ||
            !xhttp_manager_append_literal(responseInput, ":auth:"))
            goto done;
    }
    if (!XByteArray_push_back_2((XVector*)responseInput, XByteArray_constData(ha2),
                                XContainer_size_base((const XContainer*)ha2)))
        goto done;
    response = xhttp_manager_hash_hex(responseInput, algorithm);
    header = XByteArray_create();
    if (!response || !header || !xhttp_manager_append_literal(header, "Digest username=") ||
        !xhttp_manager_append_quoted(header, user) || !xhttp_manager_append_literal(header, ", realm=") ||
        !xhttp_manager_append_quoted(header, realm) || !xhttp_manager_append_literal(header, ", nonce=") ||
        !xhttp_manager_append_quoted(header, nonce) || !xhttp_manager_append_literal(header, ", uri=") ||
        !xhttp_manager_append_quoted(header, uri) || !xhttp_manager_append_literal(header, ", response=") ||
        !xhttp_manager_append_quoted(header, response))
        goto done;
    if (algorithmName && XContainer_size_base((const XContainer*)algorithmName) != 0) {
        if (!xhttp_manager_append_literal(header, ", algorithm=") ||
            !XByteArray_push_back_2((XVector*)header, XByteArray_constData(algorithmName),
                                    XContainer_size_base((const XContainer*)algorithmName)))
            goto done;
    }
    if (opaque && XContainer_size_base((const XContainer*)opaque) != 0 &&
        (!xhttp_manager_append_literal(header, ", opaque=") ||
         !xhttp_manager_append_quoted(header, opaque)))
        goto done;
    if (useQop && (!xhttp_manager_append_literal(header, ", qop=auth, nc=") ||
                   !xhttp_manager_append_literal(header, nonceCount) ||
                   !xhttp_manager_append_literal(header, ", cnonce=") ||
                   !xhttp_manager_append_quoted(header, cnonce)))
        goto done;
    if (sessionAlgorithm && !useQop && (!xhttp_manager_append_literal(header, ", cnonce=") ||
                                        !xhttp_manager_append_quoted(header, cnonce)))
        goto done;
    rawHeader = (char*)XMalloc_System(XContainer_size_base((const XContainer*)header) + 1);
    if (!rawHeader)
        goto done;
    memcpy(rawHeader, XByteArray_constData(header), XContainer_size_base((const XContainer*)header));
    rawHeader[XContainer_size_base((const XContainer*)header)] = '\0';
    result = XHttpRequest_setRawHeader(request,
        proxy ? "Proxy-Authorization" : "Authorization", rawHeader);
done:
    if (rawHeader) XFree_System(rawHeader);
    if (header) XClass_delete_base((XClass*)header);
    if (cnonce) XClass_delete_base((XClass*)cnonce);
    if (response) XClass_delete_base((XClass*)response);
    if (responseInput) XClass_delete_base((XClass*)responseInput);
    if (ha2) XClass_delete_base((XClass*)ha2);
    if (ha2Input) XClass_delete_base((XClass*)ha2Input);
    if (ha1) XClass_delete_base((XClass*)ha1);
    if (ha1Input) XClass_delete_base((XClass*)ha1Input);
    if (uri) XClass_delete_base((XClass*)uri);
    if (qop) XClass_delete_base((XClass*)qop);
    if (algorithmName) XClass_delete_base((XClass*)algorithmName);
    if (opaque) XClass_delete_base((XClass*)opaque);
    if (nonce) XClass_delete_base((XClass*)nonce);
    return result;
}

/* NTLMSSP 的定长整数和安全缓冲区始终为小端序，不能直接依赖主机字节序。 */
static uint16_t xhttp_manager_ntlm_read_le16(const uint8_t* value)
{
    return value ? (uint16_t)value[0] | ((uint16_t)value[1] << 8) : 0;
}

static uint32_t xhttp_manager_ntlm_read_le32(const uint8_t* value)
{
    return value ? (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
        ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24) : 0;
}

static void xhttp_manager_ntlm_write_le16(uint8_t* value, uint16_t number)
{
    value[0] = (uint8_t)number;
    value[1] = (uint8_t)(number >> 8);
}

static void xhttp_manager_ntlm_write_le32(uint8_t* value, uint32_t number)
{
    value[0] = (uint8_t)number;
    value[1] = (uint8_t)(number >> 8);
    value[2] = (uint8_t)(number >> 16);
    value[3] = (uint8_t)(number >> 24);
}

static void xhttp_manager_ntlm_write_le64(uint8_t* value, uint64_t number)
{
    for (size_t i = 0; i < 8; ++i)
        value[i] = (uint8_t)(number >> (i * 8));
}

static bool xhttp_manager_ntlm_read_security_buffer(const uint8_t* data, size_t size,
                                                     size_t headerOffset, size_t* length,
                                                     size_t* offset)
{
    uint32_t bufferOffset;
    uint16_t bufferLength;
    if (!data || !length || !offset || headerOffset > size || size - headerOffset < 8)
        return false;
    bufferLength = xhttp_manager_ntlm_read_le16(data + headerOffset);
    bufferOffset = xhttp_manager_ntlm_read_le32(data + headerOffset + 4);
    if ((size_t)bufferOffset > size || (size_t)bufferLength > size - (size_t)bufferOffset)
        return false;
    *length = bufferLength;
    *offset = bufferOffset;
    return true;
}

/* 将 UTF-8 凭据转换为 NTLMv2 所需的 UTF-16LE，同时沿用项目的 Unicode 大写语义。 */
static XByteArray* xhttp_manager_ntlm_utf16le(const XByteArray* value, bool upper)
{
    const uint8_t* input = value ? XByteArray_constData((XByteArray*)value) : NULL;
    size_t inputSize = value ? XContainer_size_base((const XContainer*)value) : 0;
    XString* text = NULL;
    XString* converted = NULL;
    XByteArray* result = NULL;
    const uint16_t* units;
    size_t length;
    if (!value || (!input && inputSize != 0) || inputSize > SIZE_MAX / 2)
        return NULL;
    text = XString_create_with_length_utf8((const char*)input, inputSize);
    if (!text)
        goto done;
    if (upper) {
        converted = XString_toUpper(text);
        if (!converted)
            goto done;
    }
    units = XString_utf16(converted ? converted : text);
    length = XString_length_base((const XContainer*)(converted ? converted : text));
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base((XVector*)result, length * 2)) {
        if (result) XClass_delete_base((XClass*)result);
        result = NULL;
        goto done;
    }
    for (size_t i = 0; i < length; ++i)
        xhttp_manager_ntlm_write_le16(XByteArray_data(result) + i * 2, units[i]);
done:
    if (converted) XClass_delete_base((XClass*)converted);
    if (text) XClass_delete_base((XClass*)text);
    return result;
}

/* OEM 回退只用于服务器未声明 Unicode 的旧 NTLM 挑战，按字节扩展以保持无平台依赖。 */
static XByteArray* xhttp_manager_ntlm_oem_to_utf16le(const XByteArray* value)
{
    const uint8_t* input = value ? XByteArray_constData((XByteArray*)value) : NULL;
    size_t inputSize = value ? XContainer_size_base((const XContainer*)value) : 0;
    XByteArray* result;
    if (!value || (!input && inputSize != 0) || inputSize > SIZE_MAX / 2)
        return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_resize_base((XVector*)result, inputSize * 2)) {
        if (result) XClass_delete_base((XClass*)result);
        return NULL;
    }
    for (size_t i = 0; i < inputSize; ++i)
        xhttp_manager_ntlm_write_le16(XByteArray_data(result) + i * 2, input[i]);
    return result;
}

/* 解析 DOMAIN\\user；user@realm 按 Qt 规则保持空 NTLM 域字段。 */
static bool xhttp_manager_ntlm_split_user(const XByteArray* input, XByteArray** user,
                                          XByteArray** domain, bool* domainSpecified)
{
    const uint8_t* data = input ? XByteArray_constData((XByteArray*)input) : NULL;
    size_t size = input ? XContainer_size_base((const XContainer*)input) : 0;
    size_t separator = size;
    bool atForm = false;
    if (!input || !user || !domain || !domainSpecified || (!data && size != 0))
        return false;
    *user = NULL;
    *domain = NULL;
    *domainSpecified = false;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\\') {
            separator = i;
            break;
        }
        if (data[i] == '@')
            atForm = true;
    }
    if (separator != size) {
        *domain = XByteArray_create_with_data((const char*)data, separator);
        *user = XByteArray_create_with_data((const char*)data + separator + 1,
                                            size - separator - 1);
        *domainSpecified = true;
    } else {
        *user = XByteArray_create_copy(input);
        *domain = XByteArray_create();
        *domainSpecified = atForm;
    }
    if (!*user || !*domain) {
        if (*user) XClass_delete_base((XClass*)*user);
        if (*domain) XClass_delete_base((XClass*)*domain);
        *user = NULL;
        *domain = NULL;
        return false;
    }
    return true;
}

/* 取 NTLM 挑战中方案名后的可选 Base64 token；空数组表示第一阶段。 */
static XByteArray* xhttp_manager_ntlm_challenge_token(const XByteArray* challenge)
{
    const uint8_t* data = challenge ? XByteArray_constData((XByteArray*)challenge) : NULL;
    size_t size = challenge ? XContainer_size_base((const XContainer*)challenge) : 0;
    size_t begin = 0;
    size_t end;
    if (!data || size == 0)
        return NULL;
    while (begin < size && (data[begin] == ' ' || data[begin] == '\t'))
        ++begin;
    while (begin < size && data[begin] != ' ' && data[begin] != '\t' && data[begin] != ',')
        ++begin;
    while (begin < size && (data[begin] == ' ' || data[begin] == '\t'))
        ++begin;
    end = begin;
    while (end < size && data[end] != ' ' && data[end] != '\t' && data[end] != ',')
        ++end;
    return XByteArray_create_with_data((const char*)data + begin, end - begin);
}

static XByteArray* xhttp_manager_ntlm_decode_base64_token(const XByteArray* token)
{
    size_t size = token ? XContainer_size_base((const XContainer*)token) : 0;
    const uint8_t* data = token ? XByteArray_constData((XByteArray*)token) : NULL;
    XByteArray* input;
    XByteArray* result;
    if (!token || !data || size == 0)
        return NULL;
    while (size && data[size - 1] == '\0')
        --size;
    if (size == 0)
        return NULL;
    input = XByteArray_create_with_data((const char*)data, size);
    result = input ? XByteArray_fromBase64(input) : NULL;
    if (input) XClass_delete_base((XClass*)input);
    return result;
}

/* XVector 的批量追加拒绝长度为零；协议可选字段的空值应被视为成功。 */
static bool xhttp_manager_ntlm_append_bytes(XByteArray* target, const void* data, size_t size)
{
    return target && (size == 0 || (data && XByteArray_push_back_2((XVector*)target, data, size)));
}

static bool xhttp_manager_ntlm_append_security_buffer(XByteArray* message,
                                                       size_t headerOffset,
                                                       const XByteArray* payload)
{
    size_t payloadSize = payload ? XContainer_size_base((const XContainer*)payload) : 0;
    size_t offset;
    uint8_t zero = 0;
    uint8_t* header;
    if (!message || headerOffset > 56 || payloadSize > UINT16_MAX)
        return false;
    if (XContainer_size_base((const XContainer*)message) & 1U) {
        if (!XByteArray_push_back_2((XVector*)message, &zero, 1))
            return false;
    }
    offset = XContainer_size_base((const XContainer*)message);
    if (offset > UINT32_MAX)
        return false;
    header = XByteArray_data(message) + headerOffset;
    xhttp_manager_ntlm_write_le16(header, (uint16_t)payloadSize);
    xhttp_manager_ntlm_write_le16(header + 2, (uint16_t)payloadSize);
    xhttp_manager_ntlm_write_le32(header + 4, (uint32_t)offset);
    return payloadSize == 0 || XByteArray_push_back_2((XVector*)message,
        XByteArray_constData((XByteArray*)payload), payloadSize);
}

static bool xhttp_manager_ntlm_set_header(XHttpRequest* request, const XByteArray* message,
                                          bool proxy)
{
    XByteArray* encoded = NULL;
    char* header = NULL;
    size_t encodedSize;
    bool result = false;
    if (!request || !message)
        return false;
    encoded = XByteArray_toBase64((XByteArray*)message);
    encodedSize = encoded ? XContainer_size_base((const XContainer*)encoded) : 0;
    header = encoded ? (char*)XMalloc_System(sizeof("NTLM ") - 1 + encodedSize + 1) : NULL;
    if (!header)
        goto done;
    memcpy(header, "NTLM ", sizeof("NTLM ") - 1);
    if (encodedSize)
        memcpy(header + sizeof("NTLM ") - 1, XByteArray_constData(encoded), encodedSize);
    header[sizeof("NTLM ") - 1 + encodedSize] = '\0';
    result = XHttpRequest_setRawHeader(request,
        proxy ? "Proxy-Authorization" : "Authorization", header);
done:
    if (header) XFree_System(header);
    if (encoded) XClass_delete_base((XClass*)encoded);
    return result;
}

static XByteArray* xhttp_manager_ntlm_type1_message(void)
{
    XByteArray* message = XByteArray_create();
    uint8_t* data;
    if (!message || !XByteArray_resize_base((XVector*)message, 32)) {
        if (message) XClass_delete_base((XClass*)message);
        return NULL;
    }
    data = XByteArray_data(message);
    memset(data, 0, 32);
    memcpy(data, "NTLMSSP", 8);
    xhttp_manager_ntlm_write_le32(data + 8, 1);
    xhttp_manager_ntlm_write_le32(data + 12, XHTTP_NTLM_NEGOTIATE_UNICODE |
        XHTTP_NTLM_NEGOTIATE_NTLM | XHTTP_NTLM_REQUEST_TARGET |
        XHTTP_NTLM_NEGOTIATE_ALWAYS_SIGN | XHTTP_NTLM_NEGOTIATE_NTLM2);
    return message;
}

static bool xhttp_manager_ntlm_target_timestamp(const XByteArray* targetInfo,
                                                uint8_t timestamp[8])
{
    const uint8_t* data = targetInfo ? XByteArray_constData((XByteArray*)targetInfo) : NULL;
    size_t size = targetInfo ? XContainer_size_base((const XContainer*)targetInfo) : 0;
    size_t offset = 0;
    if (!targetInfo || (!data && size != 0))
        return false;
    while (offset + 4 <= size) {
        uint16_t id = xhttp_manager_ntlm_read_le16(data + offset);
        uint16_t length = xhttp_manager_ntlm_read_le16(data + offset + 2);
        offset += 4;
        if ((size_t)length > size - offset)
            return false;
        if (id == 7 && length == 8) {
            memcpy(timestamp, data + offset, 8);
            return true;
        }
        if (id == 0)
            return false;
        offset += length;
    }
    return false;
}

/* 依据 Type 2 挑战构造 Qt 同类的可移植 NTLMv2 Type 3 报文。 */
static XByteArray* xhttp_manager_ntlm_type3_message(const XHttpAuthenticator* authenticator,
                                                     const XByteArray* type2)
{
    const uint8_t* challengeData = type2 ? XByteArray_constData((XByteArray*)type2) : NULL;
    size_t challengeSize = type2 ? XContainer_size_base((const XContainer*)type2) : 0;
    const XByteArray* password = XHttpAuthenticator_password_const(authenticator);
    const XByteArray* originalUser = XHttpAuthenticator_user_const(authenticator);
    size_t targetNameLength;
    size_t targetNameOffset;
    size_t targetInfoLength;
    size_t targetInfoOffset;
    uint32_t flags;
    bool unicode;
    bool domainSpecified;
    XByteArray* targetNameWire = NULL;
    XByteArray* targetNameUtf16 = NULL;
    XByteArray* targetInfo = NULL;
    XByteArray* user = NULL;
    XByteArray* providedDomain = NULL;
    XByteArray* userUtf16 = NULL;
    XByteArray* upperUserUtf16 = NULL;
    XByteArray* passwordUtf16 = NULL;
    XByteArray* domainUtf16 = NULL;
    XByteArray* userWire = NULL;
    XByteArray* domainWire = NULL;
    XByteArray* ntHash = NULL;
    XByteArray* v2Input = NULL;
    XByteArray* v2Hash = NULL;
    XByteArray* blob = NULL;
    XByteArray* ntInput = NULL;
    XByteArray* ntProof = NULL;
    XByteArray* ntResponse = NULL;
    XByteArray* lmInput = NULL;
    XByteArray* lmProof = NULL;
    XByteArray* lmResponse = NULL;
    XByteArray* message = NULL;
    uint8_t clientChallenge[8];
    uint8_t timestamp[8];
    static const uint8_t ntlmv2BlobPrefix[8] = { 1, 1, 0, 0, 0, 0, 0, 0 };
    static const uint8_t reserved[4] = { 0, 0, 0, 0 };
    uint64_t windowsTime;
    bool result = false;
    if (!authenticator || !password || !originalUser || !challengeData || challengeSize < 48 ||
        memcmp(challengeData, "NTLMSSP", 8) != 0 ||
        xhttp_manager_ntlm_read_le32(challengeData + 8) != 2 ||
        !xhttp_manager_ntlm_read_security_buffer(challengeData, challengeSize, 12,
                                                  &targetNameLength, &targetNameOffset) ||
        !xhttp_manager_ntlm_read_security_buffer(challengeData, challengeSize, 40,
                                                  &targetInfoLength, &targetInfoOffset))
        goto done;
    flags = xhttp_manager_ntlm_read_le32(challengeData + 20);
    unicode = (flags & XHTTP_NTLM_NEGOTIATE_UNICODE) != 0;
    if (unicode && (targetNameLength & 1U))
        goto done;
    targetNameWire = targetNameLength ? XByteArray_create_with_data(
        (const char*)challengeData + targetNameOffset, targetNameLength) : XByteArray_create();
    targetInfo = targetInfoLength ? XByteArray_create_with_data(
        (const char*)challengeData + targetInfoOffset, targetInfoLength) : XByteArray_create();
    targetNameUtf16 = unicode ? XByteArray_create_copy(targetNameWire) :
        xhttp_manager_ntlm_oem_to_utf16le(targetNameWire);
    if (!targetNameWire || !targetInfo || !targetNameUtf16 ||
        !xhttp_manager_ntlm_split_user(originalUser, &user, &providedDomain, &domainSpecified))
        goto done;
    userUtf16 = xhttp_manager_ntlm_utf16le(user, false);
    upperUserUtf16 = xhttp_manager_ntlm_utf16le(user, true);
    passwordUtf16 = xhttp_manager_ntlm_utf16le(password, false);
    domainUtf16 = domainSpecified ? xhttp_manager_ntlm_utf16le(providedDomain, false) :
        XByteArray_create_copy(targetNameUtf16);
    userWire = unicode ? XByteArray_create_copy(userUtf16) : XByteArray_create_copy(user);
    domainWire = unicode ? XByteArray_create_copy(domainUtf16) :
        XByteArray_create_copy(domainSpecified ? providedDomain : targetNameWire);
    if (!userUtf16 || !upperUserUtf16 || !passwordUtf16 || !domainUtf16 || !userWire ||
        !domainWire)
        goto done;
    ntHash = XCryptographicHash_hash((const char*)XByteArray_constData(passwordUtf16),
                                     XContainer_size_base((const XContainer*)passwordUtf16),
                                     XCryptographicHash_Md4);
    v2Input = XByteArray_create();
    if (!ntHash || !v2Input ||
        !xhttp_manager_ntlm_append_bytes(v2Input, XByteArray_constData(upperUserUtf16),
            XContainer_size_base((const XContainer*)upperUserUtf16)) ||
        !xhttp_manager_ntlm_append_bytes(v2Input, XByteArray_constData(domainUtf16),
            XContainer_size_base((const XContainer*)domainUtf16)))
        goto done;
    v2Hash = XCryptographicHash_hmac((const char*)XByteArray_constData(ntHash),
        XContainer_size_base((const XContainer*)ntHash), (const char*)XByteArray_constData(v2Input),
        XContainer_size_base((const XContainer*)v2Input), XCryptographicHash_Md5);
    if (!v2Hash || !XRandomGenerator_fillSecure(clientChallenge, sizeof(clientChallenge)))
        goto done;
    if (!xhttp_manager_ntlm_target_timestamp(targetInfo, timestamp)) {
        uint64_t msecs = XDateTime_currentMSecsSinceEpoch();
        if (msecs > (UINT64_MAX - UINT64_C(116444736000000000)) / UINT64_C(10000))
            goto done;
        windowsTime = msecs * UINT64_C(10000) + UINT64_C(116444736000000000);
        xhttp_manager_ntlm_write_le64(timestamp, windowsTime);
    }
    blob = XByteArray_create();
    if (!blob || !XByteArray_push_back_2((XVector*)blob, ntlmv2BlobPrefix,
                                         sizeof(ntlmv2BlobPrefix)) ||
        !XByteArray_push_back_2((XVector*)blob, timestamp, sizeof(timestamp)) ||
        !XByteArray_push_back_2((XVector*)blob, clientChallenge, sizeof(clientChallenge)) ||
        !XByteArray_push_back_2((XVector*)blob, reserved, sizeof(reserved)) ||
        !xhttp_manager_ntlm_append_bytes(blob, XByteArray_constData(targetInfo),
            XContainer_size_base((const XContainer*)targetInfo)) ||
        !XByteArray_push_back_2((XVector*)blob, reserved, sizeof(reserved)))
        goto done;
    ntInput = XByteArray_create();
    if (!ntInput || !XByteArray_push_back_2((XVector*)ntInput, challengeData + 24, 8) ||
        !XByteArray_push_back_2((XVector*)ntInput, XByteArray_constData(blob),
                                XContainer_size_base((const XContainer*)blob)))
        goto done;
    ntProof = XCryptographicHash_hmac((const char*)XByteArray_constData(v2Hash),
        XContainer_size_base((const XContainer*)v2Hash), (const char*)XByteArray_constData(ntInput),
        XContainer_size_base((const XContainer*)ntInput), XCryptographicHash_Md5);
    ntResponse = XByteArray_create();
    if (!ntProof || !ntResponse ||
        !XByteArray_push_back_2((XVector*)ntResponse, XByteArray_constData(ntProof),
                                XContainer_size_base((const XContainer*)ntProof)) ||
        !XByteArray_push_back_2((XVector*)ntResponse, XByteArray_constData(blob),
                                XContainer_size_base((const XContainer*)blob)))
        goto done;
    lmResponse = XByteArray_create();
    if (!lmResponse)
        goto done;
    if (targetInfoLength == 0) {
        lmInput = XByteArray_create();
        if (!lmInput || !XByteArray_push_back_2((XVector*)lmInput, challengeData + 24, 8) ||
            !XByteArray_push_back_2((XVector*)lmInput, clientChallenge, sizeof(clientChallenge)))
            goto done;
        lmProof = XCryptographicHash_hmac((const char*)XByteArray_constData(v2Hash),
            XContainer_size_base((const XContainer*)v2Hash),
            (const char*)XByteArray_constData(lmInput),
            XContainer_size_base((const XContainer*)lmInput), XCryptographicHash_Md5);
        if (!lmProof ||
            !XByteArray_push_back_2((XVector*)lmResponse, XByteArray_constData(lmProof),
                                    XContainer_size_base((const XContainer*)lmProof)) ||
            !XByteArray_push_back_2((XVector*)lmResponse, clientChallenge,
                                    sizeof(clientChallenge)))
            goto done;
    }
    message = XByteArray_create();
    if (!message || !XByteArray_resize_base((XVector*)message, 64))
        goto done;
    memset(XByteArray_data(message), 0, 64);
    memcpy(XByteArray_data(message), "NTLMSSP", 8);
    xhttp_manager_ntlm_write_le32(XByteArray_data(message) + 8, 3);
    if (!xhttp_manager_ntlm_append_security_buffer(message, 28, domainWire) ||
        !xhttp_manager_ntlm_append_security_buffer(message, 36, userWire) ||
        !xhttp_manager_ntlm_append_security_buffer(message, 44, NULL) ||
        !xhttp_manager_ntlm_append_security_buffer(message, 12, lmResponse) ||
        !xhttp_manager_ntlm_append_security_buffer(message, 20, ntResponse) ||
        !xhttp_manager_ntlm_append_security_buffer(message, 52, NULL))
        goto done;
    flags = XHTTP_NTLM_NEGOTIATE_NTLM | XHTTP_NTLM_NEGOTIATE_TARGET_INFO |
        (unicode ? XHTTP_NTLM_NEGOTIATE_UNICODE : XHTTP_NTLM_NEGOTIATE_OEM);
    if (xhttp_manager_ntlm_read_le32(challengeData + 20) & XHTTP_NTLM_NEGOTIATE_NTLM2)
        flags |= XHTTP_NTLM_NEGOTIATE_NTLM2;
    if (xhttp_manager_ntlm_read_le32(challengeData + 20) & XHTTP_NTLM_NEGOTIATE_ALWAYS_SIGN)
        flags |= XHTTP_NTLM_NEGOTIATE_ALWAYS_SIGN;
    xhttp_manager_ntlm_write_le32(XByteArray_data(message) + 60, flags);
    result = true;
done:
    memset(clientChallenge, 0, sizeof(clientChallenge));
    memset(timestamp, 0, sizeof(timestamp));
    if (!result && message) {
        XClass_delete_base((XClass*)message);
        message = NULL;
    }
    if (lmProof) XClass_delete_base((XClass*)lmProof);
    if (lmInput) XClass_delete_base((XClass*)lmInput);
    if (lmResponse) XClass_delete_base((XClass*)lmResponse);
    if (ntResponse) XClass_delete_base((XClass*)ntResponse);
    if (ntProof) XClass_delete_base((XClass*)ntProof);
    if (ntInput) XClass_delete_base((XClass*)ntInput);
    if (blob) XClass_delete_base((XClass*)blob);
    if (v2Hash) XClass_delete_base((XClass*)v2Hash);
    if (v2Input) XClass_delete_base((XClass*)v2Input);
    if (ntHash) XClass_delete_base((XClass*)ntHash);
    if (domainWire) XClass_delete_base((XClass*)domainWire);
    if (userWire) XClass_delete_base((XClass*)userWire);
    if (domainUtf16) XClass_delete_base((XClass*)domainUtf16);
    if (passwordUtf16) XClass_delete_base((XClass*)passwordUtf16);
    if (upperUserUtf16) XClass_delete_base((XClass*)upperUserUtf16);
    if (userUtf16) XClass_delete_base((XClass*)userUtf16);
    if (providedDomain) XClass_delete_base((XClass*)providedDomain);
    if (user) XClass_delete_base((XClass*)user);
    if (targetInfo) XClass_delete_base((XClass*)targetInfo);
    if (targetNameUtf16) XClass_delete_base((XClass*)targetNameUtf16);
    if (targetNameWire) XClass_delete_base((XClass*)targetNameWire);
    return message;
}

static bool xhttp_manager_apply_ntlm_authentication(XHttpRequest* request,
                                                    const XHttpAuthenticator* authenticator,
                                                    const XByteArray* challenge, bool proxy)
{
    XByteArray* token = NULL;
    XByteArray* type2 = NULL;
    XByteArray* message = NULL;
    bool result = false;
    if (!request || !authenticator || !challenge)
        goto done;
    token = xhttp_manager_ntlm_challenge_token(challenge);
    if (!token)
        goto done;
    if (XContainer_size_base((const XContainer*)token) == 0)
        message = xhttp_manager_ntlm_type1_message();
    else {
        type2 = xhttp_manager_ntlm_decode_base64_token(token);
        message = type2 ? xhttp_manager_ntlm_type3_message(authenticator, type2) : NULL;
    }
    result = message && xhttp_manager_ntlm_set_header(request, message, proxy);
done:
    if (message) XClass_delete_base((XClass*)message);
    if (type2) XClass_delete_base((XClass*)type2);
    if (token) XClass_delete_base((XClass*)token);
    return result;
}

static bool xhttp_manager_apply_authentication(XHttpRequest* request,
                                                const XHttpAuthenticator* authenticator,
                                                const XByteArray* challenge,
                                                bool proxy, unsigned int attempt)
{
    if (!authenticator)
        return false;
    if (XHttpAuthenticator_method(authenticator) == XHttpAuthenticator_Basic)
        return xhttp_manager_apply_basic_authentication(request, authenticator, proxy);
    if (XHttpAuthenticator_method(authenticator) == XHttpAuthenticator_Digest)
        return xhttp_manager_apply_digest_authentication(request, authenticator, challenge,
                                                         proxy, attempt);
    if (XHttpAuthenticator_method(authenticator) == XHttpAuthenticator_Ntlm)
        return xhttp_manager_apply_ntlm_authentication(request, authenticator, challenge, proxy);
    return false;
}

static bool xhttp_manager_request_allows_http2(const XHttpRequest* request)
{
    return xhttp_manager_request_bool_attribute(request,
                                                XHttpRequest_Http2AllowedAttribute);
}

static bool xhttp_manager_request_http2_direct(const XHttpRequest* request)
{
    return xhttp_manager_request_bool_attribute(request,
                                                XHttpRequest_Http2DirectAttribute);
}

static bool xhttp_manager_request_h2c_allowed(const XHttpRequest* request)
{
    return xhttp_manager_request_bool_attribute(request,
                                                XHttpRequest_Http2CleartextAllowedAttribute);
}

static void xhttp_manager_release_protocols(XVector* protocols)
{
    if (!protocols)
        return;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)protocols); ++i) {
        XByteArray** slot = (XByteArray**)XVector_at_base(protocols, (int64_t)i);
        if (slot && *slot)
            XClass_delete_base((XClass*)*slot);
    }
    XVector_delete_base((XContainer*)protocols);
}

static XVector* xhttp_manager_create_alpn_protocols(bool includeHttp2)
{
    XVector* protocols = XVector_create(sizeof(XByteArray*));
    XByteArray* h2 = includeHttp2 ? XByteArray_create_utf8("h2") : NULL;
    XByteArray* http11 = XByteArray_create_utf8("http/1.1");
    if (!protocols || (includeHttp2 && !h2) || !http11 ||
        (includeHttp2 && !XVector_push_back_1_base(protocols, &h2)) ||
        !XVector_push_back_1_base(protocols, &http11)) {
        if (h2) XClass_delete_base((XClass*)h2);
        if (http11) XClass_delete_base((XClass*)http11);
        xhttp_manager_release_protocols(protocols);
        return NULL;
    }
    return protocols;
}

static XByteArray* xhttp_manager_h2c_settings_value(const XHttp2Configuration* configuration)
{
    XHttp2ClientSession* session = XHttp2ClientSession_create();
    XByteArray* start;
    XHttp2Frame* settings;
    XByteArray* payload;
    XByteArray* encoded;
    size_t consumed = 0;
    if (!session || (configuration &&
        !XHttp2ClientSession_setConfiguration(session, configuration))) {
        if (session) XClass_delete_base((XClass*)session);
        return NULL;
    }
    start = XHttp2ClientSession_start(session);
    settings = start ? XHttp2Frame_fromBytes(
        XByteArray_constData(start) + sizeof(XHttp2Frame_ClientPreface) - 1,
        XContainer_size_base((const XContainer*)start) -
        (sizeof(XHttp2Frame_ClientPreface) - 1), &consumed) : NULL;
    payload = settings ? XByteArray_create_copy(XHttp2Frame_payload_const(settings)) : NULL;
    encoded = payload ? XByteArray_toBase64(payload) : NULL;
    if (encoded) {
        uint8_t* data = XByteArray_data(encoded);
        size_t size = XContainer_size_base((const XContainer*)encoded);
        if (size && data[size - 1] == '\0')
            --size;
        while (size && data[size - 1] == '=')
            --size;
        for (size_t i = 0; i < size; ++i) {
            if (data[i] == '+') data[i] = '-';
            else if (data[i] == '/') data[i] = '_';
        }
        XByteArray_resize_base((XVector*)encoded, size);
    }
    if (payload) XClass_delete_base((XClass*)payload);
    if (settings) XClass_delete_base((XClass*)settings);
    if (start) XClass_delete_base((XClass*)start);
    XClass_delete_base((XClass*)session);
    return encoded;
}

static bool xhttp_manager_request_should_h2c_upgrade(const XHttpRequest* request)
{
    const XUrl* url = request ? XHttpRequest_url_const(request) : NULL;
    const XString* scheme = url ? XUrl_scheme_const(url) : NULL;
    return request && scheme && xhttp_manager_text_equal(XString_toUtf8(scheme), "http") &&
           xhttp_manager_request_allows_http2(request) &&
           !xhttp_manager_request_http2_direct(request) &&
           xhttp_manager_request_h2c_allowed(request);
}

static bool xhttp_manager_prepare_h2c_upgrade(XHttpRequest* request)
{
    const XHttp2Configuration* configuration = request ?
        XHttpRequest_http2Configuration_const(request) : NULL;
    XByteArray* settings;
    XByteArray* previousName;
    XByteArray* previous;
    char* connectionValue;
    char* settingsValue;
    size_t previousSize;
    size_t settingsSize;
    bool result;
    if (!xhttp_manager_request_should_h2c_upgrade(request))
        return false;
    settings = xhttp_manager_h2c_settings_value(configuration);
    previousName = XByteArray_create_utf8("connection");
    previous = previousName ? XHttpRequest_rawHeader(request, previousName) : NULL;
    previousSize = previous ? XContainer_size_base((const XContainer*)previous) : 0;
    settingsSize = settings ? XContainer_size_base((const XContainer*)settings) : 0;
    connectionValue = (char*)XMalloc_System(previousSize +
        (previousSize ? sizeof(", Upgrade, HTTP2-Settings") :
                        sizeof("Upgrade, HTTP2-Settings")));
    settingsValue = (char*)XMalloc_System(settingsSize + 1);
    if (!settings || !connectionValue || !settingsValue) {
        if (settingsValue) XFree_System(settingsValue);
        if (connectionValue) XFree_System(connectionValue);
        if (previous) XClass_delete_base((XClass*)previous);
        if (previousName) XClass_delete_base((XClass*)previousName);
        if (settings) XClass_delete_base((XClass*)settings);
        return false;
    }
    if (previousSize) {
        memcpy(connectionValue, XByteArray_constData(previous), previousSize);
        memcpy(connectionValue + previousSize, ", Upgrade, HTTP2-Settings",
               sizeof(", Upgrade, HTTP2-Settings"));
    } else {
        memcpy(connectionValue, "Upgrade, HTTP2-Settings",
               sizeof("Upgrade, HTTP2-Settings"));
    }
    memcpy(settingsValue, XByteArray_constData(settings), settingsSize);
    settingsValue[settingsSize] = '\0';
    result = XHttpRequest_setRawHeader(request, "Connection", connectionValue) &&
             XHttpRequest_setRawHeader(request, "Upgrade", "h2c") &&
             XHttpRequest_setRawHeader(request, "HTTP2-Settings", settingsValue);
    XFree_System(settingsValue);
    XFree_System(connectionValue);
    if (previous) XClass_delete_base((XClass*)previous);
    if (previousName) XClass_delete_base((XClass*)previousName);
    XClass_delete_base((XClass*)settings);
    return result;
}

static bool xhttp_manager_hsts_append_u32(XByteArray* data, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 8), (uint8_t)value};
    return data && XByteArray_push_back_2(data, bytes, sizeof(bytes));
}

static bool xhttp_manager_hsts_append_u64(XByteArray* data, uint64_t value)
{
    uint8_t bytes[8];
    size_t i;
    for (i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (uint8_t)(value >> (56 - i * 8));
    return data && XByteArray_push_back_2(data, bytes, sizeof(bytes));
}

static bool xhttp_manager_hsts_read_u32(const XByteArray* data, size_t* offset,
                                        uint32_t* value)
{
    uint8_t bytes[4];
    if (!data || !offset || !value || *offset > XByteArray_size_base(data) ||
        sizeof(bytes) > XByteArray_size_base(data) - *offset)
        return false;
    memcpy(bytes, XByteArray_constData((XByteArray*)data) + *offset, sizeof(bytes));
    *offset += sizeof(bytes);
    *value = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
             ((uint32_t)bytes[2] << 8) | bytes[3];
    return true;
}

static bool xhttp_manager_hsts_read_u64(const XByteArray* data, size_t* offset,
                                        uint64_t* value)
{
    uint8_t bytes[8];
    size_t i;
    if (!data || !offset || !value || *offset > XByteArray_size_base(data) ||
        sizeof(bytes) > XByteArray_size_base(data) - *offset)
        return false;
    memcpy(bytes, XByteArray_constData((XByteArray*)data) + *offset, sizeof(bytes));
    *offset += sizeof(bytes);
    *value = 0;
    for (i = 0; i < sizeof(bytes); ++i)
        *value = (*value << 8) | bytes[i];
    return true;
}

static XString* xhttp_manager_hsts_store_path(const XNetworkAccessManager* manager)
{
    XString* directoryText;
    XString* fileName;
    XString* result;
    XDir* directory;
    if (!manager || !manager->m_hstsStoreDirectory ||
        XByteArray_size_base(manager->m_hstsStoreDirectory) == 0)
        return NULL;
    directoryText = XString_create_with_length_utf8(
        (const char*)XByteArray_constData(manager->m_hstsStoreDirectory),
        XByteArray_size_base(manager->m_hstsStoreDirectory));
    fileName = XString_create_utf8("hsts.xhs");
    directory = directoryText ? XDir_create_2(directoryText) : NULL;
    result = directory && fileName ? XDir_filePath(directory, fileName) : NULL;
    if (directory) XClass_delete_base((XClass*)directory);
    if (fileName) XClass_delete_base((XClass*)fileName);
    if (directoryText) XClass_delete_base((XClass*)directoryText);
    return result;
}

static void xhttp_manager_hsts_store_save(const XNetworkAccessManager* manager)
{
    XString* directoryText = NULL;
    XString* path = NULL;
    XString* dot = NULL;
    XDir* directory = NULL;
    XFile* file = NULL;
    XByteArray* data = NULL;
    if (!manager || !manager->m_hstsStoreEnabled || !manager->m_hstsPolicies ||
        !manager->m_hstsStoreDirectory || XByteArray_size_base(manager->m_hstsStoreDirectory) == 0)
        return;
    directoryText = XString_create_with_length_utf8(
        (const char*)XByteArray_constData(manager->m_hstsStoreDirectory),
        XByteArray_size_base(manager->m_hstsStoreDirectory));
    directory = directoryText ? XDir_create_2(directoryText) : NULL;
    dot = XString_create_utf8(".");
    if (!directory || !dot || !XDir_mkpath(directory, dot))
        goto done;
    data = XByteArray_create();
    if (!data || !XByteArray_push_back_2(data, "XHS1", 4) ||
        !xhttp_manager_hsts_append_u32(data, (uint32_t)XContainer_size_base(
            (const XContainer*)manager->m_hstsPolicies)))
        goto done;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)manager->m_hstsPolicies); ++i) {
        XHstsPolicy* const* policy = (XHstsPolicy* const*)XVector_at_base(
            manager->m_hstsPolicies, (int64_t)i);
        const XByteArray* host = policy && *policy ? XHstsPolicy_host_const(*policy) : NULL;
        if (!host || XByteArray_size_base(host) > UINT32_MAX ||
            !xhttp_manager_hsts_append_u32(data, (uint32_t)XByteArray_size_base(host)) ||
            !XByteArray_push_back_2(data, XByteArray_constData((XByteArray*)host), XByteArray_size_base(host)) ||
            !xhttp_manager_hsts_append_u64(data, (uint64_t)XHstsPolicy_expiryMSecs(*policy)) ||
            !xhttp_manager_hsts_append_u32(data, XHstsPolicy_includesSubDomains(*policy) ? 1u : 0u))
            goto done;
    }
    path = xhttp_manager_hsts_store_path(manager);
    file = path ? XFile_create_2(path) : NULL;
    if (file && XFile_open_2(file, XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create, 0)) {
        XIODevice_write_2((XIODevice*)file, data);
        XFile_close_base(file);
    }
done:
    if (file) XClass_delete_base((XClass*)file);
    if (path) XClass_delete_base((XClass*)path);
    if (data) XClass_delete_base((XClass*)data);
    if (dot) XClass_delete_base((XClass*)dot);
    if (directory) XClass_delete_base((XClass*)directory);
    if (directoryText) XClass_delete_base((XClass*)directoryText);
}

static void xhttp_manager_hsts_store_load(XNetworkAccessManager* manager)
{
    XString* path;
    XFile* file;
    XByteArray* data;
    size_t offset = 0;
    uint32_t count;
    if (!manager || !manager->m_hstsStoreEnabled)
        return;
    path = xhttp_manager_hsts_store_path(manager);
    file = path ? XFile_create_2(path) : NULL;
    if (!file || !XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        goto done;
    }
    data = XIODevice_readAll_3((XIODevice*)file);
    XFile_close_base(file);
    if (!data || XByteArray_size_base(data) < 4 ||
        memcmp(XByteArray_constData(data), "XHS1", 4) != 0)
        goto done_data;
    offset = 4;
    if (!xhttp_manager_hsts_read_u32(data, &offset, &count))
        goto done_data;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t hostLength;
        uint64_t expiry;
        uint32_t flags;
        XByteArray* host;
        XHstsPolicy* policy;
        if (!xhttp_manager_hsts_read_u32(data, &offset, &hostLength) ||
            hostLength > XByteArray_size_base(data) - offset)
            break;
        host = XByteArray_create_with_data((const char*)XByteArray_constData(data) + offset, hostLength);
        offset += hostLength;
        if (!host || !xhttp_manager_hsts_read_u64(data, &offset, &expiry) ||
            !xhttp_manager_hsts_read_u32(data, &offset, &flags)) {
            if (host) XClass_delete_base((XClass*)host);
            break;
        }
        policy = XHstsPolicy_create_ex(host, (int64_t)expiry,
                                       flags ? XHstsPolicy_IncludeSubDomains : 0);
        if (policy) {
            xhttp_manager_hsts_insert(manager, policy);
            XClass_delete_base((XClass*)policy);
        }
        XClass_delete_base((XClass*)host);
    }
done_data:
    if (data) XClass_delete_base((XClass*)data);
done:
    if (file) XClass_delete_base((XClass*)file);
    if (path) XClass_delete_base((XClass*)path);
}

static void xhttp_manager_apply_cookie_header(XNetworkAccessManager* manager, XHttpRequest* request)
{
    XByteArray* name;
    XByteArray* value;
    const XUrl* url;
    bool hasCookie;
    if (!manager || !request || !manager->m_cookieJar)
        return;
    name = XByteArray_create_utf8("Cookie");
    if (!name)
        return;
    hasCookie = XHttpHeaders_contains(XHttpRequest_headers_const(request), name);
    XClass_delete_base((XClass*)name);
    if (hasCookie)
        return;
    url = XHttpRequest_url_const(request);
    value = url ? XNetworkCookieJar_cookieHeader(manager->m_cookieJar, url) : NULL;
    if (value && XByteArray_size_base((XContainer*)value) != 0) {
        name = XByteArray_create_utf8("Cookie");
        if (name) {
            XHttpHeaders_replaceOrAppend(XHttpRequest_headers(request), name, value);
            XClass_delete_base((XClass*)name);
        }
    }
    if (value) XClass_delete_base((XClass*)value);
}

static bool xhttp_manager_hsts_host_equal(const XHstsPolicy* policy, const XByteArray* host)
{
    const XByteArray* policyHost;
    size_t policySize;
    size_t hostSize;
    if (!policy || !host)
        return false;
    policyHost = XHstsPolicy_host_const(policy);
    policySize = policyHost ? XContainer_size_base((const XContainer*)policyHost) : 0;
    hostSize = XContainer_size_base((const XContainer*)host);
    return policySize == hostSize &&
           (hostSize == 0 || memcmp(XByteArray_constData((XByteArray*)policyHost),
                                    XByteArray_constData((XByteArray*)host), hostSize) == 0);
}

static bool xhttp_manager_hsts_host_matches(const XHstsPolicy* policy, const XByteArray* host)
{
    const XByteArray* policyHost;
    const uint8_t* policyData;
    const uint8_t* hostData;
    size_t policySize;
    size_t hostSize;
    if (!policy || !host || XHstsPolicy_isExpired(policy))
        return false;
    if (xhttp_manager_hsts_host_equal(policy, host))
        return true;
    if (!XHstsPolicy_includesSubDomains(policy))
        return false;
    policyHost = XHstsPolicy_host_const(policy);
    policySize = policyHost ? XContainer_size_base((const XContainer*)policyHost) : 0;
    hostSize = XContainer_size_base((const XContainer*)host);
    if (policySize == 0 || hostSize <= policySize + 1)
        return false;
    hostData = XByteArray_constData((XByteArray*)host);
    policyData = XByteArray_constData((XByteArray*)policyHost);
    return hostData[hostSize - policySize - 1] == '.' &&
           memcmp(hostData + hostSize - policySize, policyData, policySize) == 0;
}

static bool xhttp_manager_hsts_remove_host(XNetworkAccessManager* manager, const XByteArray* host)
{
    bool removed = false;
    if (!manager || !manager->m_hstsPolicies || !host)
        return false;
    for (size_t i = XContainer_size_base((const XContainer*)manager->m_hstsPolicies); i > 0; --i) {
        XHstsPolicy** policy = (XHstsPolicy**)XVector_at_base(manager->m_hstsPolicies, (int64_t)(i - 1));
        if (policy && *policy && xhttp_manager_hsts_host_equal(*policy, host)) {
            XClass_delete_base((XClass*)*policy);
            XVector_remove_base(manager->m_hstsPolicies, (int64_t)(i - 1), 1);
            removed = true;
        }
    }
    return removed;
}

static bool xhttp_manager_hsts_insert(XNetworkAccessManager* manager, const XHstsPolicy* policy)
{
    const XByteArray* host;
    XHstsPolicy* copy;
    if (!manager || !manager->m_hstsPolicies || !policy)
        return false;
    host = XHstsPolicy_host_const(policy);
    if (!host || XContainer_size_base((const XContainer*)host) == 0)
        return false;
    xhttp_manager_hsts_remove_host(manager, host);
    if (XHstsPolicy_isExpired(policy))
        return true;
    copy = XHstsPolicy_create_copy(policy);
    if (!copy || !XVector_push_back_1_base(manager->m_hstsPolicies, &copy)) {
        if (copy)
            XClass_delete_base((XClass*)copy);
        return false;
    }
    return true;
}

static bool xhttp_manager_hsts_host_from_url(const XUrl* url, XByteArray** host)
{
    const XString* hostString;
    const char* text;
    XByteArray* result;
    if (!url || !host)
        return false;
    hostString = XUrl_host_const(url);
    text = hostString ? XString_toUtf8(hostString) : NULL;
    result = text ? XByteArray_create_utf8(text) : NULL;
    if (!result)
        return false;
    XByteArray_toLower(result);
    *host = result;
    return true;
}

static bool xhttp_manager_hsts_url_is_http(const XUrl* url)
{
    const XString* scheme = url ? XUrl_scheme_const(url) : NULL;
    const char* text = scheme ? XString_toUtf8(scheme) : NULL;
    return xhttp_manager_text_equal(text, "http");
}

static bool xhttp_manager_hsts_url_is_https(const XUrl* url)
{
    const XString* scheme = url ? XUrl_scheme_const(url) : NULL;
    const char* text = scheme ? XString_toUtf8(scheme) : NULL;
    return xhttp_manager_text_equal(text, "https");
}

static bool xhttp_manager_hsts_requires_https(const XNetworkAccessManager* manager, const XUrl* url)
{
    XByteArray* host;
    bool result = false;
    if (!manager || !manager->m_hstsEnabled || !manager->m_hstsPolicies ||
        !xhttp_manager_hsts_url_is_http(url) || !xhttp_manager_hsts_host_from_url(url, &host))
        return false;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)manager->m_hstsPolicies); ++i) {
        XHstsPolicy** policy = (XHstsPolicy**)XVector_at_base(manager->m_hstsPolicies, (int64_t)i);
        if (policy && *policy && xhttp_manager_hsts_host_matches(*policy, host)) {
            result = true;
            break;
        }
    }
    XClass_delete_base((XClass*)host);
    return result;
}

static bool xhttp_manager_apply_hsts(XNetworkAccessManager* manager, XHttpRequest* request)
{
    const XUrl* oldUrl;
    XUrl* newUrl;
    XString* scheme;
    int port;
    if (!manager || !request)
        return false;
    oldUrl = XHttpRequest_url_const(request);
    if (!xhttp_manager_hsts_requires_https(manager, oldUrl))
        return true;
    newUrl = XUrl_create_copy(oldUrl);
    scheme = XString_create_utf8("https");
    if (!newUrl || !scheme) {
        if (newUrl) XClass_delete_base((XClass*)newUrl);
        if (scheme) XClass_delete_base((XClass*)scheme);
        return false;
    }
    XUrl_setScheme(newUrl, scheme);
    port = XUrl_port(newUrl);
    if (port == 80)
        XUrl_setPort(newUrl, 443);
    if (!XHttpRequest_setUrl(request, newUrl)) {
        XClass_delete_base((XClass*)newUrl);
        XClass_delete_base((XClass*)scheme);
        return false;
    }
    XClass_delete_base((XClass*)newUrl);
    XClass_delete_base((XClass*)scheme);
    return true;
}

static bool xhttp_manager_hsts_parse_max_age(const XByteArray* value,
                                             int64_t* maxAge,
                                             bool* includeSubDomains)
{
    XVector* parts;
    bool found = false;
    if (!value || !maxAge || !includeSubDomains)
        return false;
    parts = XByteArray_split(value, ';');
    if (!parts)
        return false;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)parts); ++i) {
        XByteArray** part = (XByteArray**)XVector_at_base(parts, (int64_t)i);
        XByteArray* token = part ? XByteArray_trimmed(*part) : NULL;
        const uint8_t* data;
        size_t size;
        if (!token)
            continue;
        XByteArray_toLower(token);
        data = XByteArray_constData(token);
        size = XContainer_size_base((const XContainer*)token);
        if (size == strlen("includesubdomains") &&
            memcmp(data, "includesubdomains", size) == 0) {
            *includeSubDomains = true;
        } else if (size > 8 && memcmp(data, "max-age=", 8) == 0) {
            int64_t parsed = 0;
            bool valid = true;
            for (size_t j = 8; j < size; ++j) {
                if (data[j] < '0' || data[j] > '9' || parsed > (INT64_MAX - (data[j] - '0')) / 10) {
                    valid = false;
                    break;
                }
                parsed = parsed * 10 + (data[j] - '0');
            }
            if (valid) {
                *maxAge = parsed;
                found = true;
            }
        }
        XClass_delete_base((XClass*)token);
    }
    XByteArray_split_free(parts);
    return found;
}

static void xhttp_manager_process_hsts_response(XNetworkAccessManager* manager, XHttpReply* reply)
{
    const XHttpRequest* request;
    const XUrl* url;
    XByteArray* name;
    XByteArray* value;
    XByteArray* host;
    int64_t maxAge = -1;
    bool includeSubDomains = false;
    XDateTime now;
    int64_t expiry;
    XHstsPolicy* policy;
    if (!manager || !manager->m_hstsEnabled || !reply ||
        !xhttp_manager_hsts_url_is_https(XHttpRequest_url_const(XHttpReply_request_const(reply))))
        return;
    name = XByteArray_create_utf8("strict-transport-security");
    value = name ? XHttpHeaders_value(XHttpReply_headers_const(reply), name) : NULL;
    if (name) XClass_delete_base((XClass*)name);
    request = XHttpReply_request_const(reply);
    url = request ? XHttpRequest_url_const(request) : NULL;
    if (!value || !xhttp_manager_hsts_parse_max_age(value, &maxAge, &includeSubDomains) ||
        !xhttp_manager_hsts_host_from_url(url, &host)) {
        if (value) XClass_delete_base((XClass*)value);
        return;
    }
    now = XDateTime_currentDateTimeUtc();
    if (maxAge == 0)
        expiry = -1;
    else if (maxAge > (INT64_MAX - XDateTime_toMSecsSinceEpoch(&now)) / 1000)
        expiry = INT64_MAX;
    else
        expiry = XDateTime_toMSecsSinceEpoch(&now) + maxAge * 1000;
    policy = XHstsPolicy_create_ex(host, expiry,
                                   includeSubDomains ? XHstsPolicy_IncludeSubDomains : 0);
    if (policy) {
        xhttp_manager_hsts_insert(manager, policy);
        xhttp_manager_hsts_store_save(manager);
        XClass_delete_base((XClass*)policy);
    }
    XClass_delete_base((XClass*)host);
    XClass_delete_base((XClass*)value);
}

static XHttpTransaction* xhttp_manager_transaction_at(const XNetworkAccessManager* self, size_t index)
{
    return self && self->m_transactions ? *(XHttpTransaction**)XVector_at_base(self->m_transactions, (int64_t)index) : NULL;
}

static bool xhttp_manager_remove_transaction(XNetworkAccessManager* self, XHttpTransaction* tx)
{
    if (!self || !self->m_transactions || !tx)
        return false;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_transactions); ++i) {
        if (xhttp_manager_transaction_at(self, i) == tx) {
            XVector_remove_base(self->m_transactions, (int64_t)i, 1);
            return true;
        }
    }
    return false;
}

static void xhttp_manager_disconnect(XHttpTransaction* tx)
{
    if (!tx)
        return;
    xhttp_manager_disconnect_socket(tx);
    if (tx->m_replyFinished) XObject_disconnect_2(tx->m_replyFinished);
    tx->m_replyFinished = NULL;
}

static void xhttp_manager_disconnect_socket(XHttpTransaction* tx)
{
    if (!tx)
        return;
    if (tx->m_connected) XObject_disconnect_2(tx->m_connected);
    if (tx->m_encrypted) XObject_disconnect_2(tx->m_encrypted);
    if (tx->m_readyRead) XObject_disconnect_2(tx->m_readyRead);
    if (tx->m_disconnected) XObject_disconnect_2(tx->m_disconnected);
    if (tx->m_errorOccurred) XObject_disconnect_2(tx->m_errorOccurred);
    tx->m_connected = NULL;
    tx->m_encrypted = NULL;
    tx->m_readyRead = NULL;
    tx->m_disconnected = NULL;
    tx->m_errorOccurred = NULL;
}

static void xhttp_manager_reset_http2(XHttpTransaction* tx)
{
    if (!tx)
        return;
    if (tx->m_http2Session)
        XClass_delete_base((XClass*)tx->m_http2Session);
    if (tx->m_http2Decoder)
        XClass_delete_base((XClass*)tx->m_http2Decoder);
    if (tx->m_http2Input)
        XClass_delete_base((XClass*)tx->m_http2Input);
    if (tx->m_http2HeaderBlock)
        XClass_delete_base((XClass*)tx->m_http2HeaderBlock);
    tx->m_http2Session = NULL;
    tx->m_http2Decoder = NULL;
    tx->m_http2Input = NULL;
    tx->m_http2HeaderBlock = NULL;
    tx->m_http2StreamId = 0;
    tx->m_http2 = false;
    tx->m_h2cUpgradeRequested = false;
    tx->m_http2HeadersActive = false;
    tx->m_http2PushPromiseActive = false;
    tx->m_http2PushPromisedStreamId = 0;
    tx->m_http2HeaderEndStream = false;
    tx->m_http2ResponseHeadersReceived = false;
    tx->m_http2SettingsAckPending = false;
    tx->m_http2GoAwayReceived = false;
    tx->m_http2GoAwaySent = false;
    tx->m_http2SessionRecvWindow = 0;
    tx->m_http2StreamRecvWindow = 0;
    tx->m_http2SessionSendWindow = 0;
    tx->m_http2StreamSendWindow = 0;
    tx->m_http2PeerInitialWindowSize = 65535;
    tx->m_http2SessionRecvTarget = 0;
    tx->m_http2StreamRecvTarget = 0;
    tx->m_http2PeerMaxFrameSize = XHttp2Configuration_MinFrameSize;
    tx->m_http2PeerMaxConcurrentStreams = 100;
    tx->m_http2PeerMaxHeaderListSize = SIZE_MAX;
    tx->m_http2RemoteLastStreamId = UINT32_C(0x7fffffff);
    tx->m_http2FramePayloadOffset = 0;
}

static bool xhttp_manager_send_http2_frame(XHttpTransaction* tx, uint8_t type,
                                           uint8_t flags, uint32_t streamId,
                                           const XByteArray* payload)
{
    XHttp2Frame* frame;
    XByteArray* wire;
    size_t offset = 0;
    size_t size;
    if (!tx || !tx->m_socket)
        return false;
    frame = XHttp2Frame_create_ex(type, flags, streamId, payload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    if (!frame || !wire) {
        if (wire) XClass_delete_base((XClass*)wire);
        if (frame) XClass_delete_base((XClass*)frame);
        return false;
    }
    size = XContainer_size_base((const XContainer*)wire);
    while (offset < size) {
        int64_t written = XIODevice_write_1((XIODevice*)tx->m_socket,
            (const char*)XByteArray_constData(wire) + offset,
            (int64_t)(size - offset));
        if (written <= 0) {
            XClass_delete_base((XClass*)wire);
            XClass_delete_base((XClass*)frame);
            return false;
        }
        offset += (size_t)written;
    }
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);
    return true;
}

static bool xhttp_manager_send_http2_u32_frame(XHttpTransaction* tx, uint8_t type,
                                               uint32_t streamId, uint32_t value)
{
    uint8_t bytes[4];
    XByteArray* payload;
    bool result;
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
    payload = XByteArray_create_with_data((const char*)bytes, sizeof(bytes));
    result = payload && xhttp_manager_send_http2_frame(tx, type, 0, streamId, payload);
    if (payload)
        XClass_delete_base((XClass*)payload);
    return result;
}

static bool xhttp_manager_send_http2_goaway(XHttpTransaction* tx, uint32_t errorCode)
{
    uint8_t bytes[8];
    XByteArray* payload;
    bool result;
    if (!tx || tx->m_http2GoAwaySent)
        return tx != NULL;
    bytes[0] = (uint8_t)(tx->m_http2StreamId >> 24);
    bytes[1] = (uint8_t)(tx->m_http2StreamId >> 16);
    bytes[2] = (uint8_t)(tx->m_http2StreamId >> 8);
    bytes[3] = (uint8_t)tx->m_http2StreamId;
    bytes[4] = (uint8_t)(errorCode >> 24);
    bytes[5] = (uint8_t)(errorCode >> 16);
    bytes[6] = (uint8_t)(errorCode >> 8);
    bytes[7] = (uint8_t)errorCode;
    payload = XByteArray_create_with_data((const char*)bytes, sizeof(bytes));
    result = payload && xhttp_manager_send_http2_frame(tx, XHttp2Frame_GoAway, 0, 0, payload);
    if (payload)
        XClass_delete_base((XClass*)payload);
    if (result)
        tx->m_http2GoAwaySent = true;
    return result;
}

static bool xhttp_manager_http2_payload(const XHttp2Frame* frame,
                                        size_t prefixSize,
                                        const uint8_t** data, size_t* size)
{
    const XByteArray* payload;
    const uint8_t* bytes;
    size_t payloadSize;
    size_t padding = 0;
    size_t offset = 0;
    if (!frame || !data || !size)
        return false;
    payload = XHttp2Frame_payload_const(frame);
    payloadSize = payload ? XContainer_size_base((const XContainer*)payload) : 0;
    bytes = payload ? XByteArray_constData((XByteArray*)payload) : NULL;
    if (XHttp2Frame_flags(frame) & XHttp2Frame_Padded) {
        if (!bytes || payloadSize == 0)
            return false;
        padding = bytes[0];
        offset = 1;
    }
    if (prefixSize > payloadSize - (offset <= payloadSize ? offset : payloadSize))
        return false;
    offset += prefixSize;
    if (padding > payloadSize - offset)
        return false;
    *data = bytes ? bytes + offset : NULL;
    *size = payloadSize - offset - padding;
    return *size == 0 || *data != NULL;
}

static bool xhttp_manager_finish_http2_headers(XHttpTransaction* tx)
{
    XHttp2HeaderList* headers;
    bool result;
    if (!tx || !tx->m_http2HeaderBlock)
        return false;
    headers = XHttp2HeaderDecoder_decode(tx->m_http2Decoder,
        XByteArray_constData(tx->m_http2HeaderBlock),
        XContainer_size_base((const XContainer*)tx->m_http2HeaderBlock));
    if (tx->m_http2PushPromiseActive) {
        /* 即使应用层拒绝推送，也必须解码头块以保持 HPACK 动态表同步。 */
        result = headers && xhttp_manager_send_http2_u32_frame(tx,
            XHttp2Frame_RstStream, tx->m_http2PushPromisedStreamId,
            XHTTP2_ERROR_REFUSE_STREAM);
    } else {
        result = headers && XHttpReply_feedHttp2Headers(tx->m_reply, headers,
                                                         tx->m_http2HeaderEndStream);
        if (result && XHttpReply_statusCode(tx->m_reply) >= 200)
            tx->m_http2ResponseHeadersReceived = true;
    }
    if (headers)
        XClass_delete_base((XClass*)headers);
    XByteArray_clear_base((XContainer*)tx->m_http2HeaderBlock);
    tx->m_http2HeadersActive = false;
    tx->m_http2PushPromiseActive = false;
    tx->m_http2PushPromisedStreamId = 0;
    tx->m_http2HeaderEndStream = false;
    return result;
}

static bool xhttp_manager_http2_read_u32(const XByteArray* payload,
                                         size_t offset, uint32_t* value)
{
    const uint8_t* data;
    if (!payload || !value || offset > XContainer_size_base((const XContainer*)payload) ||
        4 > XContainer_size_base((const XContainer*)payload) - offset)
        return false;
    data = XByteArray_constData((XByteArray*)payload);
    if (!data)
        return false;
    *value = ((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1] << 16) |
             ((uint32_t)data[offset + 2] << 8) | data[offset + 3];
    return true;
}

static bool xhttp_manager_send_window_update(XHttpTransaction* tx,
                                             uint32_t streamId, uint32_t delta)
{
    uint8_t bytes[4];
    XByteArray* payload;
    bool result;
    if (!tx || delta == 0 || delta > UINT32_C(0x7fffffff))
        return false;
    bytes[0] = (uint8_t)(delta >> 24);
    bytes[1] = (uint8_t)(delta >> 16);
    bytes[2] = (uint8_t)(delta >> 8);
    bytes[3] = (uint8_t)delta;
    payload = XByteArray_create_with_data((const char*)bytes, sizeof(bytes));
    result = payload && xhttp_manager_send_http2_frame(tx,
        XHttp2Frame_WindowUpdate, 0, streamId, payload);
    if (payload)
        XClass_delete_base((XClass*)payload);
    return result;
}

static bool xhttp_manager_apply_http2_settings(XHttpTransaction* tx,
                                               const XByteArray* payload)
{
    size_t size;
    if (!tx || !payload)
        return false;
    size = XContainer_size_base((const XContainer*)payload);
    for (size_t offset = 0; offset < size; offset += 6) {
        const uint8_t* bytes = XByteArray_constData((XByteArray*)payload);
        uint16_t identifier;
        uint32_t value;
        if (!bytes || offset + 6 > size)
            return false;
        identifier = (uint16_t)(((uint16_t)bytes[offset] << 8) | bytes[offset + 1]);
        value = ((uint32_t)bytes[offset + 2] << 24) |
                ((uint32_t)bytes[offset + 3] << 16) |
                ((uint32_t)bytes[offset + 4] << 8) | bytes[offset + 5];
        switch (identifier) {
        case 0x1: /* SETTINGS_HEADER_TABLE_SIZE */
            /* 对端该设置约束的是本端出站编码器，而不是入站解码器。 */
            if (!tx->m_http2Session ||
                !XHttp2ClientSession_setPeerHeaderTableSize(tx->m_http2Session, value))
                return false;
            break;
        case 0x2: /* SETTINGS_ENABLE_PUSH */
            if (value > 1)
                return false;
            break;
        case 0x3: /* SETTINGS_MAX_CONCURRENT_STREAMS */
            tx->m_http2PeerMaxConcurrentStreams = value;
            if (!tx->m_http2Session ||
                !XHttp2ClientSession_setPeerMaxConcurrentStreams(tx->m_http2Session, value))
                return false;
            break;
        case 0x4: { /* SETTINGS_INITIAL_WINDOW_SIZE */
            int64_t delta = (int64_t)value - (int64_t)tx->m_http2PeerInitialWindowSize;
            if (value > UINT32_C(0x7fffffff) ||
                tx->m_http2StreamSendWindow + delta > INT32_MAX)
                return false;
            tx->m_http2StreamSendWindow += delta;
            tx->m_http2PeerInitialWindowSize = value;
            break;
        }
        case 0x5: /* SETTINGS_MAX_FRAME_SIZE */
            if (value < XHttp2Configuration_MinFrameSize ||
                value > XHttp2Configuration_MaxFrameSize)
                return false;
            tx->m_http2PeerMaxFrameSize = value;
            if (!tx->m_http2Session ||
                !XHttp2ClientSession_setPeerMaxFrameSize(tx->m_http2Session, value))
                return false;
            break;
        case 0x6: /* SETTINGS_MAX_HEADER_LIST_SIZE */
            tx->m_http2PeerMaxHeaderListSize = value;
            if (!tx->m_http2Session ||
                !XHttp2ClientSession_setPeerMaxHeaderListSize(tx->m_http2Session, value))
                return false;
            break;
        default:
            /* RFC 7540 requires unknown settings to be ignored. */
            break;
        }
    }
    return true;
}

static bool xhttp_manager_process_http2_frame(XHttpTransaction* tx,
                                              const XHttp2Frame* frame)
{
    uint8_t type;
    uint8_t flags;
    uint32_t streamId;
    const uint8_t* data;
    size_t size;
    if (!tx || !frame || !tx->m_http2)
        return false;
    if (!XHttp2Frame_validatePayload(frame))
        goto protocol_error;
    type = XHttp2Frame_type(frame);
    flags = XHttp2Frame_flags(frame);
    streamId = XHttp2Frame_streamId(frame);
    if (type == XHttp2Frame_Settings) {
        const XByteArray* payload = XHttp2Frame_payload_const(frame);
        if (streamId != 0)
            goto protocol_error;
        if (flags & XHttp2Frame_Ack) {
            if (!tx->m_http2SettingsAckPending)
                goto protocol_error;
            tx->m_http2SettingsAckPending = false;
            return true;
        }
        if (!xhttp_manager_apply_http2_settings(tx, payload) ||
            !xhttp_manager_send_http2_frame(tx, XHttp2Frame_Settings,
                                             XHttp2Frame_Ack, 0, NULL))
            return false;
        return true;
    }
    if (type == XHttp2Frame_Ping) {
        const XByteArray* payload = XHttp2Frame_payload_const(frame);
        if (streamId != 0 || !payload ||
            XContainer_size_base((const XContainer*)payload) != 8)
            goto protocol_error;
        if (!(flags & XHttp2Frame_Ack) &&
            !xhttp_manager_send_http2_frame(tx, XHttp2Frame_Ping,
                                             XHttp2Frame_Ack, 0, payload))
            return false;
        return true;
    }
    if (type == XHttp2Frame_WindowUpdate) {
        const XByteArray* payload = XHttp2Frame_payload_const(frame);
        uint32_t increment;
        if (!payload || XContainer_size_base((const XContainer*)payload) != 4 ||
            !xhttp_manager_http2_read_u32(payload, 0, &increment) ||
            (increment & UINT32_C(0x80000000)) != 0 || increment == 0)
            goto protocol_error;
        increment &= UINT32_C(0x7fffffff);
        if (streamId == 0) {
            if (tx->m_http2SessionSendWindow + increment > INT32_MAX)
                goto protocol_error;
            tx->m_http2SessionSendWindow += increment;
        } else if (streamId == tx->m_http2StreamId) {
            if (tx->m_http2StreamSendWindow + increment > INT32_MAX)
                goto protocol_error;
            tx->m_http2StreamSendWindow += increment;
        }
        if (!xhttp_manager_send_pending(tx))
            goto protocol_error;
        return true;
    }
    if (type == XHttp2Frame_GoAway) {
        const XByteArray* payload = XHttp2Frame_payload_const(frame);
        uint32_t lastStreamId;
        if (streamId != 0 || !xhttp_manager_http2_read_u32(payload, 0, &lastStreamId) ||
            (lastStreamId & UINT32_C(0x80000000)) != 0)
            goto protocol_error;
        lastStreamId &= UINT32_C(0x7fffffff);
        /* 客户端发起的请求流为奇数；0 表示服务端未处理任何请求流。 */
        if (lastStreamId != 0 && (lastStreamId & 1u) == 0)
            goto protocol_error;
        tx->m_http2GoAwayReceived = true;
        tx->m_http2RemoteLastStreamId = lastStreamId;
        if (tx->m_http2Session)
            XHttp2ClientSession_setGoingAway(tx->m_http2Session);
        if (tx->m_http2StreamId > lastStreamId) {
            XHttpReply_setError(tx->m_reply, XHttpReply_RemoteHostClosedError,
                                "HTTP/2 GOAWAY 未包含当前请求流");
            return false;
        }
        return true;
    }
    if (type == XHttp2Frame_RstStream) {
        if (streamId == 0)
            goto protocol_error;
        if (streamId == tx->m_http2StreamId)
            XHttpReply_setError(tx->m_reply, XHttpReply_RemoteHostClosedError,
                                "HTTP/2 对端重置请求流");
        return streamId != tx->m_http2StreamId;
    }
    if (tx->m_http2HeadersActive &&
        (type != XHttp2Frame_Continuation || streamId != tx->m_http2StreamId))
        goto protocol_error;
    if (type == XHttp2Frame_Continuation) {
        if (!tx->m_http2HeadersActive || streamId != tx->m_http2StreamId ||
            !XByteArray_push_back_2((XVector*)tx->m_http2HeaderBlock,
                                    XHttp2Frame_payload_const(frame) ?
                                    XByteArray_constData((XByteArray*)XHttp2Frame_payload_const(frame)) : NULL,
                                    XHttp2Frame_payload_const(frame) ?
                                    XContainer_size_base((const XContainer*)XHttp2Frame_payload_const(frame)) : 0))
            goto protocol_error;
        if (flags & XHttp2Frame_EndHeaders)
            return xhttp_manager_finish_http2_headers(tx);
        return true;
    }
    if (type == XHttp2Frame_Priority) {
        /* Qt 同样读取但暂不调度依赖树；必须拒绝连接流上的 PRIORITY。 */
        if (streamId == 0)
            goto protocol_error;
        return true;
    }
    if (type == XHttp2Frame_PushPromise) {
        const XByteArray* payload = XHttp2Frame_payload_const(frame);
        const XHttp2Configuration* configuration = tx->m_http2Session ?
            XHttp2ClientSession_configuration_const(tx->m_http2Session) : NULL;
        uint32_t promisedStreamId;
        size_t promisedOffset = (flags & XHttp2Frame_Padded) ? 1 : 0;
        if (streamId != tx->m_http2StreamId ||
            !xhttp_manager_http2_read_u32(payload, promisedOffset, &promisedStreamId) ||
            (promisedStreamId & UINT32_C(0x80000000)) != 0)
            goto protocol_error;
        promisedStreamId &= UINT32_C(0x7fffffff);
        /* 服务端承诺的流必须是此前未见过的偶数流。 */
        if (promisedStreamId == 0 || (promisedStreamId & 1u) != 0)
            goto protocol_error;
        if ((!configuration || !XHttp2Configuration_serverPushEnabled(configuration)) &&
            !tx->m_http2SettingsAckPending)
            goto protocol_error;
        if (!xhttp_manager_http2_payload(frame, 4, &data, &size))
            goto protocol_error;
        if (!XByteArray_push_back_2((XVector*)tx->m_http2HeaderBlock, data, size))
            return false;
        tx->m_http2PushPromiseActive = true;
        tx->m_http2PushPromisedStreamId = promisedStreamId;
        tx->m_http2HeadersActive = (flags & XHttp2Frame_EndHeaders) == 0;
        return tx->m_http2HeadersActive || xhttp_manager_finish_http2_headers(tx);
    }
    /* RFC 7540 5.1：未知类型必须忽略，不能因其流号为 0 而破坏连接。 */
    if (type > XHttp2Frame_Continuation)
        return true;
    if (streamId != tx->m_http2StreamId)
        goto protocol_error;
    if (type == XHttp2Frame_Headers) {
        if (tx->m_http2HeadersActive ||
            !xhttp_manager_http2_payload(frame,
                (flags & XHttp2Frame_PriorityFlag) ? 5 : 0, &data, &size))
            goto protocol_error;
        if (!XByteArray_push_back_2((XVector*)tx->m_http2HeaderBlock, data, size))
            return false;
        tx->m_http2HeaderEndStream = (flags & XHttp2Frame_EndStream) != 0;
        tx->m_http2HeadersActive = (flags & XHttp2Frame_EndHeaders) == 0;
        return tx->m_http2HeadersActive || xhttp_manager_finish_http2_headers(tx);
    }
    if (type == XHttp2Frame_Data) {
        const XByteArray* payload = XHttp2Frame_payload_const(frame);
        size_t flowSize = payload ? XContainer_size_base((const XContainer*)payload) : 0;
        if ((int64_t)flowSize > tx->m_http2SessionRecvWindow ||
            (int64_t)flowSize > tx->m_http2StreamRecvWindow)
            goto flow_control_error;
        tx->m_http2SessionRecvWindow -= (int64_t)flowSize;
        tx->m_http2StreamRecvWindow -= (int64_t)flowSize;
        if (!xhttp_manager_http2_payload(frame, 0, &data, &size) ||
            !XHttpReply_feedHttp2Data(tx->m_reply, data, size,
                                      (flags & XHttp2Frame_EndStream) != 0))
            goto protocol_error;
        if (tx->m_http2SessionRecvWindow <
                (int64_t)(tx->m_http2SessionRecvTarget / 2)) {
            uint32_t delta = tx->m_http2SessionRecvTarget -
                             (uint32_t)tx->m_http2SessionRecvWindow;
            if (!xhttp_manager_send_window_update(tx, 0, delta))
                goto protocol_error;
            tx->m_http2SessionRecvWindow += delta;
        }
        if (tx->m_http2StreamRecvWindow <
                (int64_t)(tx->m_http2StreamRecvTarget / 2)) {
            uint32_t delta = tx->m_http2StreamRecvTarget -
                             (uint32_t)tx->m_http2StreamRecvWindow;
            if (!xhttp_manager_send_window_update(tx, tx->m_http2StreamId, delta))
                goto protocol_error;
            tx->m_http2StreamRecvWindow += delta;
        }
        return true;
    }
    return true;
protocol_error:
    xhttp_manager_send_http2_goaway(tx, XHTTP2_ERROR_PROTOCOL_ERROR);
    XHttpReply_setError(tx->m_reply, XHttpReply_ProtocolInvalidOperationError,
                        "HTTP/2 响应帧格式或流状态无效");
    return false;
flow_control_error:
    xhttp_manager_send_http2_goaway(tx, XHTTP2_ERROR_FLOW_CONTROL_ERROR);
    XHttpReply_setError(tx->m_reply, XHttpReply_ProtocolInvalidOperationError,
                        "HTTP/2 流控窗口耗尽");
    return false;
}

static void xhttp_manager_transaction_destroy(XHttpTransaction* tx)
{
    if (!tx)
        return;
    if (tx->m_timeoutTimer != XTIMER_INVALID_ID && tx->m_manager)
        XObject_killTimer((XObject*)tx->m_manager, tx->m_timeoutTimer);
    tx->m_timeoutTimer = XTIMER_INVALID_ID;
    xhttp_manager_shared_http2_detach(tx);
    xhttp_manager_disconnect(tx);
    if (tx->m_socket) {
        XAbstractSocket_abort(tx->m_socket);
        XClass_delete_base((XClass*)tx->m_socket);
        tx->m_socket = NULL;
    }
    xhttp_manager_reset_http2(tx);
    if (tx->m_requestWire)
        XClass_delete_base((XClass*)tx->m_requestWire);
    if (tx->m_ntlmAuthenticator)
        XClass_delete_base((XClass*)tx->m_ntlmAuthenticator);
    XFree_System(tx);
}

static void xhttp_manager_close_socket(XHttpTransaction* tx)
{
    if (!tx)
        return;
    if (tx->m_http2Connection) {
        /* 重定向仅脱离当前流，不能关闭仍服务其他流的 HTTP/2 连接。 */
        xhttp_manager_shared_http2_detach(tx);
        xhttp_manager_reset_http2(tx);
        return;
    }
    xhttp_manager_disconnect_socket(tx);
    if (tx->m_socket) {
        XAbstractSocket_abort(tx->m_socket);
        XClass_delete_base((XClass*)tx->m_socket);
        tx->m_socket = NULL;
    }
    xhttp_manager_reset_http2(tx);
}

static bool xhttp_manager_write_socket_bytes(XAbstractSocket* socket,
                                             const uint8_t* data, size_t size)
{
    size_t offset = 0;
    if (!socket || (!data && size != 0))
        return false;
    while (offset < size) {
        int64_t written = XIODevice_write_1((XIODevice*)socket,
            (const char*)data + offset, (int64_t)(size - offset));
        if (written <= 0)
            return false;
        offset += (size_t)written;
    }
    return true;
}

static bool xhttp_manager_write_bytes(XHttpTransaction* tx,
                                      const uint8_t* data, size_t size)
{
    return tx && xhttp_manager_write_socket_bytes(tx->m_socket, data, size);
}

static bool xhttp_manager_send_http2_pending(XHttpTransaction* tx)
{
    size_t total;
    if (!tx || !tx->m_socket || !tx->m_requestWire)
        return false;
    total = XContainer_size_base((const XContainer*)tx->m_requestWire);
    while (tx->m_writeOffset < total) {
        const uint8_t* wire = XByteArray_constData(tx->m_requestWire);
        size_t remaining;
        size_t consumed = 0;
        XHttp2Frame* frame;
        if (!wire)
            return false;
        if (tx->m_writeOffset < sizeof(XHttp2Frame_ClientPreface) - 1) {
            size_t prefaceRemaining = sizeof(XHttp2Frame_ClientPreface) - 1 -
                                      tx->m_writeOffset;
            if (!xhttp_manager_write_bytes(tx, wire + tx->m_writeOffset,
                                            prefaceRemaining))
                return false;
            tx->m_writeOffset += prefaceRemaining;
            continue;
        }
        remaining = total - tx->m_writeOffset;
        frame = XHttp2Frame_fromBytes(wire + tx->m_writeOffset, remaining, &consumed);
        if (!frame || consumed == 0) {
            if (frame) XClass_delete_base((XClass*)frame);
            return false;
        }
        if (XHttp2Frame_type(frame) == XHttp2Frame_Data &&
            XHttp2Frame_streamId(frame) == tx->m_http2StreamId) {
            const XByteArray* payload = XHttp2Frame_payload_const(frame);
            size_t payloadSize = payload ?
                XContainer_size_base((const XContainer*)payload) : 0;
            size_t dataOffset = tx->m_http2FramePayloadOffset;
            int64_t allowed = tx->m_http2SessionSendWindow <
                              tx->m_http2StreamSendWindow ?
                              tx->m_http2SessionSendWindow :
                              tx->m_http2StreamSendWindow;
            size_t chunk;
            XByteArray* part;
            XHttp2Frame* outgoing;
            XByteArray* encoded;
            uint8_t flags = 0;
            if (dataOffset > payloadSize) {
                XClass_delete_base((XClass*)frame);
                return false;
            }
            if (dataOffset == payloadSize) {
                tx->m_writeOffset += consumed;
                tx->m_http2FramePayloadOffset = 0;
                XClass_delete_base((XClass*)frame);
                continue;
            }
            if (allowed <= 0) {
                XClass_delete_base((XClass*)frame);
                return true;
            }
            chunk = payloadSize - dataOffset;
            if ((int64_t)chunk > allowed)
                chunk = (size_t)allowed;
            if (chunk > tx->m_http2PeerMaxFrameSize)
                chunk = tx->m_http2PeerMaxFrameSize;
            part = XByteArray_create_with_data(
                (const char*)XByteArray_constData((XByteArray*)payload) + dataOffset,
                chunk);
            if ((XHttp2Frame_flags(frame) & XHttp2Frame_EndStream) != 0 &&
                dataOffset + chunk == payloadSize)
                flags |= XHttp2Frame_EndStream;
            outgoing = part ? XHttp2Frame_create_ex(XHttp2Frame_Data, flags,
                                                     tx->m_http2StreamId, part) : NULL;
            encoded = outgoing ? XHttp2Frame_toByteArray(outgoing) : NULL;
            if (!part || !outgoing || !encoded ||
                !xhttp_manager_write_bytes(tx, XByteArray_constData(encoded),
                                            XContainer_size_base((const XContainer*)encoded))) {
                if (encoded) XClass_delete_base((XClass*)encoded);
                if (outgoing) XClass_delete_base((XClass*)outgoing);
                if (part) XClass_delete_base((XClass*)part);
                XClass_delete_base((XClass*)frame);
                return false;
            }
            tx->m_http2SessionSendWindow -= (int64_t)chunk;
            tx->m_http2StreamSendWindow -= (int64_t)chunk;
            tx->m_http2FramePayloadOffset += chunk;
            if (tx->m_http2FramePayloadOffset == payloadSize) {
                tx->m_writeOffset += consumed;
                tx->m_http2FramePayloadOffset = 0;
            }
            XClass_delete_base((XClass*)encoded);
            XClass_delete_base((XClass*)outgoing);
            XClass_delete_base((XClass*)part);
            XClass_delete_base((XClass*)frame);
            if (chunk < payloadSize - dataOffset)
                return true;
        } else {
            if (!xhttp_manager_write_bytes(tx, wire + tx->m_writeOffset, consumed)) {
                XClass_delete_base((XClass*)frame);
                return false;
            }
            tx->m_writeOffset += consumed;
            XClass_delete_base((XClass*)frame);
        }
    }
    tx->m_requestSent = true;
    return true;
}

static bool xhttp_manager_send_pending(XHttpTransaction* tx)
{
    XIODevice* device;
    size_t total;
    if (!tx || !tx->m_socket || !tx->m_requestWire || tx->m_requestSent)
        return tx && tx->m_requestSent;
    if (tx->m_http2)
        return xhttp_manager_send_http2_pending(tx);
    device = (XIODevice*)tx->m_socket;
    total = XContainer_size_base((const XContainer*)tx->m_requestWire);
    while (tx->m_writeOffset < total) {
        int64_t written = XIODevice_write_1(device,
            (const char*)XByteArray_constData(tx->m_requestWire) + tx->m_writeOffset,
            (int64_t)(total - tx->m_writeOffset));
        if (written <= 0)
            return false;
        tx->m_writeOffset += (size_t)written;
    }
    tx->m_requestSent = true;
    return true;
}

static bool xhttp_manager_feed_http2(XHttpTransaction* tx,
                                     const void* data, size_t size)
{
    size_t offset = 0;
    if (!tx || !tx->m_http2 || !tx->m_http2Input || (!data && size != 0))
        return false;
    if (size != 0 && !XByteArray_push_back_2((XVector*)tx->m_http2Input, data, size))
        return false;
    while (XContainer_size_base((const XContainer*)tx->m_http2Input) - offset >= 9) {
        const uint8_t* bytes = XByteArray_constData(tx->m_http2Input);
        size_t remaining = XContainer_size_base((const XContainer*)tx->m_http2Input) - offset;
        size_t payloadSize = ((size_t)bytes[offset] << 16) |
                             ((size_t)bytes[offset + 1] << 8) | bytes[offset + 2];
        size_t consumed = 0;
        XHttp2Frame* frame;
        if (payloadSize > remaining - 9)
            break;
        frame = XHttp2Frame_fromBytes(bytes + offset, remaining, &consumed);
        if (!frame || consumed == 0) {
            if (frame) XClass_delete_base((XClass*)frame);
            XHttpReply_setError(tx->m_reply, XHttpReply_ProtocolInvalidOperationError,
                                "HTTP/2 响应帧无法解码");
            return false;
        }
        if (!xhttp_manager_process_http2_frame(tx, frame)) {
            XClass_delete_base((XClass*)frame);
            return false;
        }
        XClass_delete_base((XClass*)frame);
        offset += consumed;
        if (XHttpReply_isFinished(tx->m_reply))
            break;
    }
    if (offset != 0)
        XByteArray_remove_base((XVector*)tx->m_http2Input, 0, (int64_t)offset);
    return true;
}

static bool xhttp_manager_reply_is_h2c_upgrade(const XHttpReply* reply)
{
    XByteArray* name;
    XByteArray* value;
    const uint8_t* bytes;
    size_t size;
    bool result;
    if (!reply || XHttpReply_statusCode(reply) != 101)
        return false;
    name = XByteArray_create_utf8("upgrade");
    value = name ? XHttpReply_rawHeader(reply, name) : NULL;
    bytes = value ? XByteArray_constData(value) : NULL;
    size = value ? XContainer_size_base((const XContainer*)value) : 0;
    result = bytes && size == 3 && tolower(bytes[0]) == 'h' &&
             tolower(bytes[1]) == '2' && tolower(bytes[2]) == 'c';
    if (value) XClass_delete_base((XClass*)value);
    if (name) XClass_delete_base((XClass*)name);
    return result;
}

static void xhttp_manager_read_available(XHttpTransaction* tx)
{
    XAbstractSocket* socket;
    XIODevice* device;
    XByteArray* bytes;
    if (!tx || !tx->m_socket || !tx->m_reply || XHttpReply_isFinished(tx->m_reply))
        return;
    socket = tx->m_socket;
    device = (XIODevice*)socket;
    for (;;) {
        bytes = XIODevice_readAll_3(device);
        if (!bytes)
            break;
        size_t size = XContainer_size_base((const XContainer*)bytes);
        XHttpReply* reply = tx->m_reply;
        bool ok = tx->m_http2 ? xhttp_manager_feed_http2(tx,
            XByteArray_constData(bytes), size) :
            (size == 0 || XHttpReply_feed(reply, XByteArray_constData(bytes), size));
        if (!ok) {
            XClass_delete_base((XClass*)bytes);
            return;
        }
        if (!tx->m_http2 && tx->m_h2cUpgradeRequested &&
            xhttp_manager_reply_is_h2c_upgrade(reply)) {
            XByteArray* remaining = NULL;
            size_t inputSize = reply->m_input ?
                XContainer_size_base((const XContainer*)reply->m_input) : 0;
            bool copied = true;
            bool reset;
            bool upgraded;
            if (inputSize > reply->m_parseOffset) {
                /* 101 后同一 TCP 读取中可能已经含有 HTTP/2 SETTINGS 等帧。 */
                remaining = XByteArray_create_with_data(
                    (const char*)XByteArray_constData(reply->m_input) + reply->m_parseOffset,
                    inputSize - reply->m_parseOffset);
                copied = remaining != NULL;
            }
            reset = copied && XHttpReply_resetForRequest(reply, XHttpReply_request_const(reply));
            /* 101 在 HTTP/1 解析器中已投递完成事件；h2c 接管后该事件必须失效。 */
            if (reset)
                tx->m_completionPosted = false;
            upgraded = reset && xhttp_manager_promote_h2c_upgrade(tx);
            XClass_delete_base((XClass*)bytes);
            if (!upgraded) {
                XHttpReply_setError(reply, XHttpReply_ProtocolInvalidOperationError,
                                    "h2c 升级后的 HTTP/2 初始化失败");
            } else if (remaining) {
                XHttp2SharedConnection* connection = tx->m_http2Connection;
                bool accepted = connection && connection->m_protocol &&
                    XHttp2Connection_feed(connection->m_protocol,
                        XByteArray_constData(remaining),
                        XContainer_size_base((const XContainer*)remaining)) &&
                    xhttp_manager_shared_http2_flush(connection);
                if (!accepted)
                    xhttp_manager_shared_http2_fail(connection,
                        XHttpReply_ProtocolInvalidOperationError,
                        "h2c 升级响应中的 HTTP/2 帧无效");
                else
                    xhttp_manager_shared_http2_dispatch(connection);
            }
            if (remaining)
                XClass_delete_base((XClass*)remaining);
            return;
        }
        XClass_delete_base((XClass*)bytes);
        xhttp_manager_handle_reply_state(tx);
        /* 401/407 和重定向会在同步回调中关闭旧套接字并创建新连接。 */
        if (tx->m_socket != socket)
            return;
        if (size == 0 || XHttpReply_isFinished(reply))
            break;
    }
}

static bool xhttp_manager_start_socket(XHttpTransaction* tx)
{
    const XUrl* url;
    const XString* host;
    const XString* scheme;
    const char* hostText;
    const char* schemeText;
    int urlPort;
    uint16_t port;
    XString* peerName;
    XVector* alpnProtocols;
    bool directHttp2;
    if (!tx || !tx->m_reply || !tx->m_manager)
        return false;
    url = XHttpRequest_url_const(XHttpReply_request_const(tx->m_reply));
    host = url ? XUrl_host_const(url) : NULL;
    scheme = url ? XUrl_scheme_const(url) : NULL;
    hostText = host ? XString_toUtf8(host) : NULL;
    schemeText = scheme ? XString_toUtf8(scheme) : NULL;
    if (!url || !hostText || !*hostText || !schemeText)
        return false;
    urlPort = XUrl_port(url);
    tx->m_tls = strcmp(schemeText, "https") == 0;
    if (strcmp(schemeText, "http") != 0 && !tx->m_tls)
        return false;
    port = urlPort >= 0 ? (uint16_t)urlPort : (tx->m_tls ? 443u : 80u);
    directHttp2 = xhttp_manager_request_http2_direct(XHttpReply_request_const(tx->m_reply));
    if (tx->m_tls) {
        XSslSocket* socket = XSslSocket_create();
        if (!socket)
            return false;
        tx->m_socket = (XAbstractSocket*)socket;
        peerName = XString_create_utf8(hostText);
        if (!peerName)
            return false;
        if (!directHttp2) {
            alpnProtocols = xhttp_manager_create_alpn_protocols(
                xhttp_manager_request_allows_http2(XHttpReply_request_const(tx->m_reply)));
            if (!alpnProtocols || !XSslSocket_setAllowedNextProtocols(socket, alpnProtocols)) {
                xhttp_manager_release_protocols(alpnProtocols);
                XClass_delete_base((XClass*)peerName);
                return false;
            }
            xhttp_manager_release_protocols(alpnProtocols);
        }
        XSslSocket_setPeerVerifyMode(socket, tx->m_manager->m_sslPeerVerifyMode);
        if (tx->m_manager->m_sslCaCertificate)
            XSslSocket_addCaCertificate(socket, tx->m_manager->m_sslCaCertificate);
        XSslSocket_setPeerVerifyName(socket, peerName);
        tx->m_connected = XObject_connect_1((XObject*)socket,
            XSignal(XAbstractSocket_connected_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_connected, XConnectionType_Direct);
        tx->m_encrypted = XObject_connect_1((XObject*)socket,
            XSignal(XSslSocket_encrypted_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_encrypted, XConnectionType_Direct);
        tx->m_readyRead = XObject_connect_1((XObject*)socket,
            XSignal(XIODevice_readyRead_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_ready_read, XConnectionType_Direct);
        tx->m_disconnected = XObject_connect_1((XObject*)socket,
            XSignal(XAbstractSocket_disconnected_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_disconnected, XConnectionType_Direct);
        tx->m_errorOccurred = XObject_connect_1((XObject*)socket,
            XSignal(XAbstractSocket_errorOccurred_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_error, XConnectionType_Direct);
        if (!tx->m_connected || !tx->m_encrypted || !tx->m_readyRead ||
            !tx->m_disconnected || !tx->m_errorOccurred)
            return false;
        if (tx->m_manager->m_proxy)
            XAbstractSocket_setProxy(tx->m_socket, tx->m_manager->m_proxy);
        XSslSocket_connectToHostEncrypted(socket, peerName, port);
        XClass_delete_base((XClass*)peerName);
    } else {
        XTcpSocket* socket = XTcpSocket_create();
        if (!socket)
            return false;
        tx->m_socket = (XAbstractSocket*)socket;
        tx->m_connected = XObject_connect_1((XObject*)socket,
            XSignal(XAbstractSocket_connected_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_connected, XConnectionType_Direct);
        tx->m_readyRead = XObject_connect_1((XObject*)socket,
            XSignal(XIODevice_readyRead_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_ready_read, XConnectionType_Direct);
        tx->m_disconnected = XObject_connect_1((XObject*)socket,
            XSignal(XAbstractSocket_disconnected_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_disconnected, XConnectionType_Direct);
        tx->m_errorOccurred = XObject_connect_1((XObject*)socket,
            XSignal(XAbstractSocket_errorOccurred_signal), (XObject*)tx->m_manager,
            xhttp_manager_socket_error, XConnectionType_Direct);
        if (!tx->m_connected || !tx->m_readyRead || !tx->m_disconnected || !tx->m_errorOccurred)
            return false;
        if (tx->m_manager->m_proxy)
            XAbstractSocket_setProxy(tx->m_socket, tx->m_manager->m_proxy);
        XAbstractSocket_connectToHost_base(tx->m_socket, hostText, port,
                                           XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    }
    return true;
}

static XHttpTransaction* xhttp_manager_find_socket(XNetworkAccessManager* manager, XObject* socket)
{
    if (!manager || !socket || !manager->m_transactions)
        return NULL;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)manager->m_transactions); ++i) {
        XHttpTransaction* tx = xhttp_manager_transaction_at(manager, i);
        if (tx && (XObject*)tx->m_socket == socket)
            return tx;
    }
    return NULL;
}

static XHttpTransaction* xhttp_manager_find_reply(XNetworkAccessManager* manager, XObject* reply)
{
    if (!manager || !reply || !manager->m_transactions)
        return NULL;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)manager->m_transactions); ++i) {
        XHttpTransaction* tx = xhttp_manager_transaction_at(manager, i);
        if (tx && (XObject*)tx->m_reply == reply)
            return tx;
    }
    return NULL;
}

static bool xhttp_manager_text_equal(const char* left, const char* right)
{
    if (!left || !right)
        return false;
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int xhttp_manager_effective_port(const XUrl* url)
{
    int port;
    const XString* scheme;
    const char* text;
    if (!url)
        return -1;
    port = XUrl_port(url);
    if (port >= 0)
        return port;
    scheme = XUrl_scheme_const(url);
    text = scheme ? XString_toUtf8(scheme) : NULL;
    if (xhttp_manager_text_equal(text, "https"))
        return 443;
    if (xhttp_manager_text_equal(text, "http"))
        return 80;
    return -1;
}

static bool xhttp_manager_byte_array_equal(const XByteArray* left, const XByteArray* right)
{
    size_t leftSize = left ? XContainer_size_base((const XContainer*)left) : 0;
    size_t rightSize = right ? XContainer_size_base((const XContainer*)right) : 0;
    if (!left || !right || leftSize != rightSize)
        return false;
    return leftSize == 0 || memcmp(XByteArray_constData((XByteArray*)left),
                                   XByteArray_constData((XByteArray*)right),
                                   leftSize) == 0;
}

static void xhttp_manager_authentication_cache_entry_delete(
    XHttpAuthenticationCacheEntry* entry)
{
    if (!entry)
        return;
    if (entry->m_origin)
        XClass_delete_base((XClass*)entry->m_origin);
    if (entry->m_realm)
        XClass_delete_base((XClass*)entry->m_realm);
    if (entry->m_authenticator)
        XClass_delete_base((XClass*)entry->m_authenticator);
    XFree_System(entry);
}

static void xhttp_manager_authentication_cache_clear(XNetworkAccessManager* manager)
{
    if (!manager || !manager->m_authenticationCache)
        return;
    for (size_t i = 0;
         i < XContainer_size_base((const XContainer*)manager->m_authenticationCache); ++i) {
        XHttpAuthenticationCacheEntry** slot =
            (XHttpAuthenticationCacheEntry**)XVector_at_base(manager->m_authenticationCache,
                                                              (int64_t)i);
        if (slot)
            xhttp_manager_authentication_cache_entry_delete(*slot);
    }
    XVector_clear_base((XContainer*)manager->m_authenticationCache);
}

/* 认证来源键使用规范化的小写 scheme/host/port；代理认证仅以代理端点作为来源。 */
static XByteArray* xhttp_manager_authentication_origin(const XHttpTransaction* tx, bool proxy)
{
    const XHttpRequest* request;
    const XUrl* url;
    const XString* scheme;
    const XString* host;
    const char* schemeText;
    const char* hostText;
    int port;
    char portText[16];
    int portLength;
    XByteArray* result;
    if (!tx || !tx->m_manager || !tx->m_reply)
        return NULL;
    if (proxy) {
        const XNetworkProxy* configured = tx->m_manager->m_proxy;
        host = configured ? XNetworkProxy_hostName_const(configured) : NULL;
        hostText = host ? XString_toUtf8(host) : NULL;
        port = configured ? (int)XNetworkProxy_port(configured) : -1;
        schemeText = "proxy";
    } else {
        request = XHttpReply_request_const(tx->m_reply);
        url = request ? XHttpRequest_url_const(request) : NULL;
        scheme = url ? XUrl_scheme_const(url) : NULL;
        host = url ? XUrl_host_const(url) : NULL;
        schemeText = scheme ? XString_toUtf8(scheme) : NULL;
        hostText = host ? XString_toUtf8(host) : NULL;
        port = xhttp_manager_effective_port(url);
    }
    if (!schemeText || !*schemeText || !hostText || !*hostText || port <= 0 || port > UINT16_MAX)
        return NULL;
    portLength = snprintf(portText, sizeof(portText), ":%d", port);
    if (portLength <= 0 || (size_t)portLength >= sizeof(portText))
        return NULL;
    result = XByteArray_create();
    if (!result || !XByteArray_append_utf8(result, schemeText) ||
        !XByteArray_append_utf8(result, "://") || !XByteArray_append_utf8(result, hostText) ||
        !XByteArray_push_back_2((XVector*)result, portText, (size_t)portLength)) {
        if (result)
            XClass_delete_base((XClass*)result);
        return NULL;
    }
    XByteArray_toLower(result);
    return result;
}

static bool xhttp_manager_authentication_reuse_allowed(const XHttpRequest* request)
{
    XVariant* attribute;
    bool result = true;
    if (!request)
        return false;
    attribute = XHttpRequest_attribute(request, XHttpRequest_AuthenticationReuseAttribute);
    if (attribute && XVariant_type(attribute) == XVariantType_Bool)
        result = XVariant_toBool(attribute);
    if (attribute)
        XClass_delete_base((XClass*)attribute);
    return result;
}

static XHttpAuthenticationCacheEntry* xhttp_manager_authentication_cache_find(
    const XNetworkAccessManager* manager, const XByteArray* origin, bool proxy,
    const XHttpAuthenticator* authenticator)
{
    const XByteArray* realm = XHttpAuthenticator_realm_const(authenticator);
    if (!manager || !manager->m_authenticationCache || !origin)
        return NULL;
    for (size_t i = 0;
         i < XContainer_size_base((const XContainer*)manager->m_authenticationCache); ++i) {
        XHttpAuthenticationCacheEntry** slot =
            (XHttpAuthenticationCacheEntry**)XVector_at_base(manager->m_authenticationCache,
                                                              (int64_t)i);
        XHttpAuthenticationCacheEntry* entry = slot ? *slot : NULL;
        if (entry && entry->m_proxy == proxy && entry->m_authenticator &&
            XHttpAuthenticator_method(entry->m_authenticator) ==
                XHttpAuthenticator_method(authenticator) &&
            xhttp_manager_byte_array_equal(entry->m_origin, origin) &&
            ((!realm && XContainer_size_base((const XContainer*)entry->m_realm) == 0) ||
             (realm && xhttp_manager_byte_array_equal(entry->m_realm, realm))))
            return entry;
    }
    return NULL;
}

static bool xhttp_manager_authentication_cache_apply(
    const XHttpAuthenticationCacheEntry* entry, XHttpAuthenticator* authenticator)
{
    const XByteArray* user;
    const XByteArray* password;
    if (!entry || !entry->m_authenticator || !authenticator)
        return false;
    user = XHttpAuthenticator_user_const(entry->m_authenticator);
    password = XHttpAuthenticator_password_const(entry->m_authenticator);
    return user && password && XHttpAuthenticator_setUser(authenticator, user) &&
           XHttpAuthenticator_setPassword(authenticator, password);
}

static bool xhttp_manager_authentication_cache_store(XNetworkAccessManager* manager,
                                                      const XByteArray* origin, bool proxy,
                                                      const XHttpAuthenticator* authenticator)
{
    XHttpAuthenticationCacheEntry* entry;
    const XByteArray* realm;
    if (!manager || !manager->m_authenticationCache || !origin || !authenticator ||
        !XHttpAuthenticator_hasCredentials(authenticator))
        return false;
    realm = XHttpAuthenticator_realm_const(authenticator);
    for (size_t i = 0;
         i < XContainer_size_base((const XContainer*)manager->m_authenticationCache); ++i) {
        XHttpAuthenticationCacheEntry** slot =
            (XHttpAuthenticationCacheEntry**)XVector_at_base(manager->m_authenticationCache,
                                                              (int64_t)i);
        XHttpAuthenticationCacheEntry* current = slot ? *slot : NULL;
        if (current && current->m_proxy == proxy && current->m_authenticator &&
            XHttpAuthenticator_method(current->m_authenticator) ==
                XHttpAuthenticator_method(authenticator) &&
            xhttp_manager_byte_array_equal(current->m_origin, origin) &&
            ((!realm && XContainer_size_base((const XContainer*)current->m_realm) == 0) ||
             (realm && xhttp_manager_byte_array_equal(current->m_realm, realm)))) {
            xhttp_manager_authentication_cache_entry_delete(current);
            XVector_remove_base(manager->m_authenticationCache, (int64_t)i, 1);
            break;
        }
    }
    entry = (XHttpAuthenticationCacheEntry*)XMalloc_System(sizeof(*entry));
    if (!entry)
        return false;
    memset(entry, 0, sizeof(*entry));
    entry->m_origin = XByteArray_create_copy(origin);
    entry->m_realm = realm ? XByteArray_create_copy(realm) : XByteArray_create();
    entry->m_authenticator = XHttpAuthenticator_create_copy(authenticator);
    entry->m_proxy = proxy;
    if (!entry->m_origin || !entry->m_realm || !entry->m_authenticator ||
        !XVector_push_back_1_base(manager->m_authenticationCache, &entry)) {
        xhttp_manager_authentication_cache_entry_delete(entry);
        return false;
    }
    return true;
}

static bool xhttp_manager_apply_url_credentials(const XHttpRequest* request,
                                                XHttpAuthenticator* authenticator)
{
    const XUrl* url = request ? XHttpRequest_url_const(request) : NULL;
    const XString* user = url ? XUrl_userName_const(url) : NULL;
    const XString* password = url ? XUrl_password_const(url) : NULL;
    const char* userText = user ? XString_toUtf8(user) : NULL;
    const char* passwordText = password ? XString_toUtf8(password) : NULL;
    return authenticator && userText && *userText && passwordText && *passwordText &&
           XHttpAuthenticator_setUser_utf8(authenticator, userText) &&
           XHttpAuthenticator_setPassword_utf8(authenticator, passwordText);
}

static bool xhttp_manager_apply_proxy_credentials(const XNetworkProxy* proxy,
                                                   XHttpAuthenticator* authenticator)
{
    const XString* user = proxy ? XNetworkProxy_user_const(proxy) : NULL;
    const XString* password = proxy ? XNetworkProxy_password_const(proxy) : NULL;
    const char* userText = user ? XString_toUtf8(user) : NULL;
    const char* passwordText = password ? XString_toUtf8(password) : NULL;
    return authenticator && userText && *userText && passwordText && *passwordText &&
           XHttpAuthenticator_setUser_utf8(authenticator, userText) &&
           XHttpAuthenticator_setPassword_utf8(authenticator, passwordText);
}

static XHttp2SharedConnection* xhttp_manager_shared_http2_at(
    const XNetworkAccessManager* manager, size_t index)
{
    XHttp2SharedConnection** slot;
    if (!manager || !manager->m_http2Connections)
        return NULL;
    slot = (XHttp2SharedConnection**)XVector_at_base(manager->m_http2Connections,
                                                      (int64_t)index);
    return slot ? *slot : NULL;
}

static bool xhttp_manager_remove_shared_http2(XNetworkAccessManager* manager,
                                              XHttp2SharedConnection* connection)
{
    if (!manager || !manager->m_http2Connections || !connection)
        return false;
    for (size_t i = 0;
         i < XContainer_size_base((const XContainer*)manager->m_http2Connections); ++i) {
        if (xhttp_manager_shared_http2_at(manager, i) == connection) {
            XVector_remove_base(manager->m_http2Connections, (int64_t)i, 1);
            return true;
        }
    }
    return false;
}

static bool xhttp_manager_shared_http2_matches(const XHttp2SharedConnection* connection,
                                                const XHttpRequest* request)
{
    const XUrl* url = request ? XHttpRequest_url_const(request) : NULL;
    const XString* scheme = url ? XUrl_scheme_const(url) : NULL;
    const XString* host = url ? XUrl_host_const(url) : NULL;
    const char* schemeText = scheme ? XString_toUtf8(scheme) : NULL;
    const char* hostText = host ? XString_toUtf8(host) : NULL;
    bool direct = xhttp_manager_request_http2_direct(request);
    bool canUseHttp2 = direct || (connection && connection->m_tls ?
        xhttp_manager_request_allows_http2(request) :
        (xhttp_manager_request_allows_http2(request) &&
         xhttp_manager_request_h2c_allowed(request)));
    const char* expectedScheme = connection && connection->m_tls ? "https" : "http";
    return connection && canUseHttp2 && connection->m_usable && connection->m_socket &&
           connection->m_protocol && !XHttp2Connection_isGoingAway(connection->m_protocol) &&
           xhttp_manager_text_equal(schemeText, expectedScheme) && connection->m_host && hostText &&
           xhttp_manager_text_equal(XString_toUtf8(connection->m_host), hostText) &&
           xhttp_manager_effective_port(url) == (int)connection->m_port;
}

static XHttp2SharedConnection* xhttp_manager_find_reusable_http2(
    XNetworkAccessManager* manager, const XHttpRequest* request)
{
    if (!manager || !manager->m_http2Connections || !request)
        return NULL;
    for (size_t i = 0;
         i < XContainer_size_base((const XContainer*)manager->m_http2Connections); ++i) {
        XHttp2SharedConnection* connection = xhttp_manager_shared_http2_at(manager, i);
        if (xhttp_manager_shared_http2_matches(connection, request))
            return connection;
    }
    return NULL;
}

static XHttp2SharedConnection* xhttp_manager_find_shared_http2_socket(
    XNetworkAccessManager* manager, const XObject* socket)
{
    if (!manager || !socket || !manager->m_http2Connections)
        return NULL;
    for (size_t i = 0;
         i < XContainer_size_base((const XContainer*)manager->m_http2Connections); ++i) {
        XHttp2SharedConnection* connection = xhttp_manager_shared_http2_at(manager, i);
        if (connection && (const XObject*)connection->m_socket == socket)
            return connection;
    }
    return NULL;
}

static void xhttp_manager_shared_http2_destroy(XHttp2SharedConnection* connection)
{
    if (!connection)
        return;
    if (connection->m_readyRead) XObject_disconnect_2(connection->m_readyRead);
    if (connection->m_disconnected) XObject_disconnect_2(connection->m_disconnected);
    if (connection->m_errorOccurred) XObject_disconnect_2(connection->m_errorOccurred);
    if (connection->m_socket) {
        XAbstractSocket_abort(connection->m_socket);
        XClass_delete_base((XClass*)connection->m_socket);
    }
    if (connection->m_protocol) XClass_delete_base((XClass*)connection->m_protocol);
    if (connection->m_transactions) XVector_delete_base((XClass*)connection->m_transactions);
    if (connection->m_host) XClass_delete_base((XClass*)connection->m_host);
    XFree_System(connection);
}

static void xhttp_manager_mark_http2_used(XHttpTransaction* tx)
{
    XVariant* used;
    if (!tx || !tx->m_reply || !tx->m_reply->m_request)
        return;
    used = XVariant_create_bool(true);
    if (!used)
        return;
    XHttpRequest_setAttribute(tx->m_reply->m_request,
                              XHttpRequest_Http2WasUsedAttribute, used);
    XClass_delete_base((XClass*)used);
}

static bool xhttp_manager_shared_http2_attach(XHttp2SharedConnection* connection,
                                              XHttpTransaction* tx)
{
    if (!connection || !tx || !connection->m_transactions)
        return false;
    if (!XVector_push_back_1_base(connection->m_transactions, &tx))
        return false;
    tx->m_http2Connection = connection;
    tx->m_http2 = true;
    tx->m_socket = NULL;
    xhttp_manager_mark_http2_used(tx);
    return true;
}

static void xhttp_manager_shared_http2_detach(XHttpTransaction* tx)
{
    XHttp2SharedConnection* connection;
    if (!tx || !(connection = tx->m_http2Connection))
        return;
    if (connection->m_protocol && tx->m_reply && tx->m_http2StreamId)
        XHttp2Connection_detachReply(connection->m_protocol, tx->m_http2StreamId, tx->m_reply);
    if (connection->m_transactions) {
        for (size_t i = 0;
             i < XContainer_size_base((const XContainer*)connection->m_transactions); ++i) {
            XHttpTransaction** slot = (XHttpTransaction**)XVector_at_base(
                connection->m_transactions, (int64_t)i);
            if (slot && *slot == tx) {
                XVector_remove_base(connection->m_transactions, (int64_t)i, 1);
                break;
            }
        }
    }
    tx->m_http2Connection = NULL;
    tx->m_http2StreamId = 0;
    tx->m_http2 = false;
}

static bool xhttp_manager_shared_http2_flush(XHttp2SharedConnection* connection)
{
    XByteArray* bytes;
    bool result;
    if (!connection || !connection->m_usable || !connection->m_socket ||
        !connection->m_protocol)
        return false;
    bytes = XHttp2Connection_takeOutgoing(connection->m_protocol);
    if (!bytes)
        return false;
    result = XContainer_size_base((const XContainer*)bytes) == 0 ||
             xhttp_manager_write_socket_bytes(connection->m_socket,
                 XByteArray_constData(bytes), XContainer_size_base((const XContainer*)bytes));
    XClass_delete_base((XClass*)bytes);
    return result;
}

static void xhttp_manager_shared_http2_dispatch(XHttp2SharedConnection* connection)
{
    if (!connection || !connection->m_transactions)
        return;
    for (size_t i = 0;
         i < XContainer_size_base((const XContainer*)connection->m_transactions); ++i) {
        XHttpTransaction** slot = (XHttpTransaction**)XVector_at_base(
            connection->m_transactions, (int64_t)i);
        if (slot && *slot)
            xhttp_manager_handle_reply_state(*slot);
    }
}

static void xhttp_manager_shared_http2_fail(XHttp2SharedConnection* connection,
                                            XHttpReply_NetworkError error,
                                            const char* message)
{
    if (!connection)
        return;
    connection->m_usable = false;
    if (connection->m_transactions) {
        for (size_t i = 0;
             i < XContainer_size_base((const XContainer*)connection->m_transactions); ++i) {
            XHttpTransaction** slot = (XHttpTransaction**)XVector_at_base(
                connection->m_transactions, (int64_t)i);
            if (slot && *slot && (*slot)->m_reply && !XHttpReply_isFinished((*slot)->m_reply))
                XHttpReply_setError((*slot)->m_reply, error, message);
        }
    }
    xhttp_manager_shared_http2_dispatch(connection);
}

static void xhttp_manager_shared_http2_read_available(XHttp2SharedConnection* connection)
{
    if (!connection || !connection->m_socket || !connection->m_protocol)
        return;
    for (;;) {
        XByteArray* bytes = XIODevice_readAll_3((XIODevice*)connection->m_socket);
        size_t size;
        bool ok;
        if (!bytes)
            break;
        size = XContainer_size_base((const XContainer*)bytes);
        ok = size == 0 || XHttp2Connection_feed(connection->m_protocol,
                                                  XByteArray_constData(bytes), size);
        XClass_delete_base((XClass*)bytes);
        if (!ok) {
            xhttp_manager_shared_http2_fail(connection,
                                            XHttpReply_ProtocolInvalidOperationError,
                                            "HTTP/2 连接收到无效协议帧");
            return;
        }
        if (!xhttp_manager_shared_http2_flush(connection)) {
            xhttp_manager_shared_http2_fail(connection, XHttpReply_UnknownNetworkError,
                                            "HTTP/2 控制帧发送失败");
            return;
        }
        xhttp_manager_shared_http2_dispatch(connection);
        if (size == 0)
            break;
    }
}

static XHttp2SharedConnection* xhttp_manager_shared_http2_create(
    XHttpTransaction* tx)
{
    const XHttpRequest* request;
    const XHttp2Configuration* configuration;
    const XUrl* url;
    const XString* host;
    XHttp2SharedConnection* connection;
    int port;
    if (!tx || !tx->m_manager || !tx->m_socket || !tx->m_reply)
        return NULL;
    request = XHttpReply_request_const(tx->m_reply);
    url = request ? XHttpRequest_url_const(request) : NULL;
    host = url ? XUrl_host_const(url) : NULL;
    port = xhttp_manager_effective_port(url);
    configuration = request ? XHttpRequest_http2Configuration_const(request) : NULL;
    if (!host || !XString_toUtf8(host) || port <= 0 || port > UINT16_MAX)
        return NULL;
    connection = (XHttp2SharedConnection*)XMalloc_System(sizeof(*connection));
    if (!connection)
        return NULL;
    memset(connection, 0, sizeof(*connection));
    connection->m_manager = tx->m_manager;
    connection->m_protocol = configuration ? XHttp2Connection_create_ex(configuration) :
                                             XHttp2Connection_create();
    connection->m_transactions = XVector_create(sizeof(XHttpTransaction*));
    connection->m_host = XString_create_copy(host);
    connection->m_port = (uint16_t)port;
    connection->m_usable = true;
    connection->m_tls = tx->m_tls;
    if (!connection->m_protocol || !connection->m_transactions || !connection->m_host) {
        xhttp_manager_shared_http2_destroy(connection);
        return NULL;
    }
    xhttp_manager_disconnect_socket(tx);
    connection->m_socket = tx->m_socket;
    tx->m_socket = NULL;
    connection->m_readyRead = XObject_connect_1((XObject*)connection->m_socket,
        XSignal(XIODevice_readyRead_signal), (XObject*)connection->m_manager,
        xhttp_manager_socket_ready_read, XConnectionType_Direct);
    connection->m_disconnected = XObject_connect_1((XObject*)connection->m_socket,
        XSignal(XAbstractSocket_disconnected_signal), (XObject*)connection->m_manager,
        xhttp_manager_socket_disconnected, XConnectionType_Direct);
    connection->m_errorOccurred = XObject_connect_1((XObject*)connection->m_socket,
        XSignal(XAbstractSocket_errorOccurred_signal), (XObject*)connection->m_manager,
        xhttp_manager_socket_error, XConnectionType_Direct);
    if (!connection->m_readyRead || !connection->m_disconnected || !connection->m_errorOccurred) {
        xhttp_manager_shared_http2_destroy(connection);
        return NULL;
    }
    return connection;
}

static bool xhttp_manager_shared_http2_send(XHttp2SharedConnection* connection,
                                            XHttpTransaction* tx)
{
    uint32_t streamId = 0;
    if (!connection || !tx || !tx->m_reply || !connection->m_usable ||
        !XHttp2Connection_sendRequestReply(connection->m_protocol, tx->m_reply, &streamId))
        return false;
    tx->m_http2StreamId = streamId;
    if (!xhttp_manager_shared_http2_attach(connection, tx)) {
        XHttp2Connection_detachReply(connection->m_protocol, streamId, tx->m_reply);
        return false;
    }
    if (!xhttp_manager_shared_http2_flush(connection))
        xhttp_manager_shared_http2_fail(connection, XHttpReply_UnknownNetworkError,
                                        "HTTP/2 请求发送失败");
    return true;
}

/* 解析挑战、优先使用同源缓存并构造带认证字段的新请求；成功后调用方拥有 nextRequest。 */
static bool xhttp_manager_prepare_authentication(XHttpTransaction* tx,
                                                 XHttpRequest** nextRequest)
{
    XHttpAuthenticator* authenticator = NULL;
    XHttpRequest* request = NULL;
    XByteArray* challenge = NULL;
    XByteArray* origin = NULL;
    XHttpAuthenticationCacheEntry* cached;
    const XHttpRequest* original;
    bool proxy;
    bool reuse;
    bool credentialsFromCache = false;
    bool result = false;
    int status;
    if (!tx || !tx->m_manager || !tx->m_reply || !nextRequest)
        return false;
    *nextRequest = NULL;
    status = XHttpReply_statusCode(tx->m_reply);
    if (status != 401 && status != 407)
        return false;
    proxy = status == 407;
    original = XHttpReply_request_const(tx->m_reply);
    reuse = xhttp_manager_authentication_reuse_allowed(original);
    challenge = xhttp_manager_select_authentication_challenge(tx->m_reply, proxy);
    authenticator = XHttpAuthenticator_create();
    if (!challenge || !authenticator ||
        !xhttp_manager_parse_authentication_challenge(challenge, authenticator))
        goto done;
    if (reuse)
        origin = xhttp_manager_authentication_origin(tx, proxy);
    /* NTLM 的 Type 3 必须使用 Type 1 轮次已确认的同一组凭据，不能再次异步询问调用方。 */
    if (XHttpAuthenticator_method(authenticator) == XHttpAuthenticator_Ntlm &&
        tx->m_ntlmAuthenticator && tx->m_ntlmProxy == proxy) {
        if (!XHttpAuthenticator_setUser(authenticator,
                XHttpAuthenticator_user_const(tx->m_ntlmAuthenticator)) ||
            !XHttpAuthenticator_setPassword(authenticator,
                XHttpAuthenticator_password_const(tx->m_ntlmAuthenticator)))
            goto done;
    } else {
        if (reuse && !tx->m_authenticationCacheUsed &&
            (proxy ? xhttp_manager_apply_proxy_credentials(tx->m_manager->m_proxy, authenticator) :
                     xhttp_manager_apply_url_credentials(original, authenticator))) {
            tx->m_authenticationCacheUsed = true;
        } else if (reuse && !tx->m_authenticationCacheUsed && origin &&
                   (cached = xhttp_manager_authentication_cache_find(tx->m_manager, origin, proxy,
                                                                      authenticator)) != NULL &&
                   xhttp_manager_authentication_cache_apply(cached, authenticator)) {
            tx->m_authenticationCacheUsed = true;
            credentialsFromCache = true;
        }
        if (!XHttpAuthenticator_hasCredentials(authenticator)) {
            if (proxy)
                XNetworkAccessManager_proxyAuthenticationRequired_signal(tx->m_manager,
                    tx->m_manager->m_proxy, authenticator);
            else
                XNetworkAccessManager_authenticationRequired_signal(tx->m_manager, tx->m_reply,
                                                                     authenticator);
        }
    }
    if (!XHttpAuthenticator_hasCredentials(authenticator))
        goto done;
    if (XHttpAuthenticator_method(authenticator) == XHttpAuthenticator_Ntlm &&
        (!tx->m_ntlmAuthenticator || tx->m_ntlmProxy != proxy)) {
        XHttpAuthenticator* replacement = XHttpAuthenticator_create_copy(authenticator);
        if (!replacement)
            goto done;
        if (tx->m_ntlmAuthenticator)
            XClass_delete_base((XClass*)tx->m_ntlmAuthenticator);
        tx->m_ntlmAuthenticator = replacement;
        tx->m_ntlmProxy = proxy;
    }
    if (reuse && origin && !credentialsFromCache)
        xhttp_manager_authentication_cache_store(tx->m_manager, origin, proxy, authenticator);
    request = XHttpRequest_create_copy(original);
    if (!request || !xhttp_manager_apply_authentication(request, authenticator, challenge,
                                                        proxy, tx->m_authenticationAttempts))
        goto done;
    *nextRequest = request;
    request = NULL;
    result = true;
done:
    if (request) XClass_delete_base((XClass*)request);
    if (origin) XClass_delete_base((XClass*)origin);
    if (authenticator) XClass_delete_base((XClass*)authenticator);
    if (challenge) XClass_delete_base((XClass*)challenge);
    return result;
}

/* 401/407 已完整结束时，在同一 HTTP/2 连接上复用响应对象和事务重发请求。 */
static bool xhttp_manager_retry_http2_authentication(XHttpTransaction* tx)
{
    XHttp2SharedConnection* connection;
    XHttpRequest* request = NULL;
    uint32_t oldStreamId;
    uint32_t streamId = 0;
    bool result = false;
    if (!tx || !tx->m_reply || !(connection = tx->m_http2Connection) ||
        !connection->m_protocol || tx->m_authenticationAttempts >= 3)
        return false;
    if (!xhttp_manager_prepare_authentication(tx, &request))
        goto done;
    oldStreamId = tx->m_http2StreamId;
    if (!oldStreamId || !XHttp2Connection_detachReply(connection->m_protocol, oldStreamId,
                                                       tx->m_reply) ||
        !XHttpReply_resetForRequest(tx->m_reply, request))
        goto done;
    if (!XHttp2Connection_sendRequestReply(connection->m_protocol, tx->m_reply, &streamId) ||
        !streamId) {
        XHttpReply_setError(tx->m_reply, XHttpReply_UnknownNetworkError,
                            "HTTP/2 认证后的请求重发失败");
        result = true;
        goto done;
    }
    tx->m_http2StreamId = streamId;
    ++tx->m_authenticationAttempts;
    if (!xhttp_manager_shared_http2_flush(connection)) {
        XHttpReply_setError(tx->m_reply, XHttpReply_UnknownNetworkError,
                            "HTTP/2 认证后的请求写出失败");
        result = true;
        goto done;
    }
    result = true;
done:
    if (request) XClass_delete_base((XClass*)request);
    return result;
}

/* HTTP/1.1 认证重发使用新的底层连接；HTTP/2 则保持在共享连接上新建奇数流。 */
static bool xhttp_manager_retry_authentication(XHttpTransaction* tx)
{
    XHttpRequest* request = NULL;
    XByteArray* wire = NULL;
    if (!tx || !tx->m_reply || tx->m_authenticationAttempts >= 3)
        return false;
    if (tx->m_http2Connection)
        return xhttp_manager_retry_http2_authentication(tx);
    if (!xhttp_manager_prepare_authentication(tx, &request))
        return false;
    wire = XHttpRequest_toHttp1(request, true);
    if (!wire)
        goto failed;
    xhttp_manager_close_socket(tx);
    if (!XHttpReply_resetForRequest(tx->m_reply, request))
        goto failed;
    if (tx->m_requestWire)
        XClass_delete_base((XClass*)tx->m_requestWire);
    tx->m_requestWire = wire;
    wire = NULL;
    tx->m_writeOffset = 0;
    tx->m_requestSent = false;
    ++tx->m_authenticationAttempts;
    if (!xhttp_manager_start_socket(tx)) {
        XHttpReply_setError(tx->m_reply, XHttpReply_UnknownNetworkError,
                            "认证后的 HTTP 请求重连失败");
    }
    XClass_delete_base((XClass*)request);
    return true;
failed:
    if (wire)
        XClass_delete_base((XClass*)wire);
    if (request)
        XClass_delete_base((XClass*)request);
    return false;
}

static bool xhttp_manager_shared_http2_adopt_upgrade(XHttp2SharedConnection* connection,
                                                      XHttpTransaction* tx)
{
    if (!connection || !tx || !tx->m_reply || !connection->m_usable ||
        !XHttp2Connection_adoptUpgradedRequest(connection->m_protocol, tx->m_reply))
        return false;
    tx->m_http2StreamId = 1;
    if (!xhttp_manager_shared_http2_attach(connection, tx)) {
        XHttp2Connection_detachReply(connection->m_protocol, 1, tx->m_reply);
        return false;
    }
    if (!xhttp_manager_shared_http2_flush(connection))
        xhttp_manager_shared_http2_fail(connection, XHttpReply_UnknownNetworkError,
                                        "h2c 客户端前言发送失败");
    return true;
}

static bool xhttp_manager_promote_http2(XHttpTransaction* tx)
{
    XHttp2SharedConnection* connection = xhttp_manager_shared_http2_create(tx);
    if (!connection)
        return false;
    if (!XVector_push_back_1_base(tx->m_manager->m_http2Connections, &connection) ||
        !xhttp_manager_shared_http2_send(connection, tx)) {
        xhttp_manager_remove_shared_http2(tx->m_manager, connection);
        xhttp_manager_shared_http2_destroy(connection);
        return false;
    }
    return true;
}

static bool xhttp_manager_promote_h2c_upgrade(XHttpTransaction* tx)
{
    XHttp2SharedConnection* connection = xhttp_manager_shared_http2_create(tx);
    if (!connection)
        return false;
    if (!XVector_push_back_1_base(tx->m_manager->m_http2Connections, &connection) ||
        !xhttp_manager_shared_http2_adopt_upgrade(connection, tx)) {
        xhttp_manager_remove_shared_http2(tx->m_manager, connection);
        xhttp_manager_shared_http2_destroy(connection);
        return false;
    }
    tx->m_h2cUpgradeRequested = false;
    return true;
}

static bool xhttp_manager_same_origin(const XUrl* oldUrl, const XUrl* newUrl)
{
    const XString* oldScheme = oldUrl ? XUrl_scheme_const(oldUrl) : NULL;
    const XString* newScheme = newUrl ? XUrl_scheme_const(newUrl) : NULL;
    const XString* oldHost = oldUrl ? XUrl_host_const(oldUrl) : NULL;
    const XString* newHost = newUrl ? XUrl_host_const(newUrl) : NULL;
    return oldScheme && newScheme && oldHost && newHost &&
           xhttp_manager_text_equal(XString_toUtf8(oldScheme), XString_toUtf8(newScheme)) &&
           xhttp_manager_text_equal(XString_toUtf8(oldHost), XString_toUtf8(newHost)) &&
           xhttp_manager_effective_port(oldUrl) == xhttp_manager_effective_port(newUrl);
}

static bool xhttp_manager_redirect_allowed(const XHttpRequest* request, const XUrl* target,
                                           XHttpRequest_RedirectPolicy policy)
{
    const XUrl* source = request ? XHttpRequest_url_const(request) : NULL;
    const XString* sourceScheme;
    const XString* targetScheme;
    const char* sourceText;
    const char* targetText;
    if (!source || !target || policy == XHttpRequest_ManualRedirectPolicy ||
        policy == XHttpRequest_UserVerifiedRedirectPolicy)
        return false;
    if (policy == XHttpRequest_SameOriginRedirectPolicy)
        return xhttp_manager_same_origin(source, target);
    sourceScheme = XUrl_scheme_const(source);
    targetScheme = XUrl_scheme_const(target);
    sourceText = sourceScheme ? XString_toUtf8(sourceScheme) : NULL;
    targetText = targetScheme ? XString_toUtf8(targetScheme) : NULL;
    if (!sourceText || !targetText)
        return false;
    return !(xhttp_manager_text_equal(sourceText, "https") &&
             xhttp_manager_text_equal(targetText, "http"));
}

static bool xhttp_manager_try_redirect(XHttpTransaction* tx)
{
    XHttpReply* reply;
    const XHttpRequest* oldRequest;
    XUrl* target;
    XHttpRequest* nextRequest;
    XByteArray* nextWire;
    int status;
    XHttpRequest_RedirectPolicy policy;
    int maximum;
    if (!tx || !tx->m_reply || !tx->m_manager)
        return false;
    reply = tx->m_reply;
    status = XHttpReply_statusCode(reply);
    if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)
        return false;
    oldRequest = XHttpReply_request_const(reply);
    policy = oldRequest ? XHttpRequest_redirectPolicy(oldRequest) : tx->m_manager->m_redirectPolicy;
    target = XHttpReply_redirectTarget(reply);
    if (!xhttp_manager_redirect_allowed(oldRequest, target, policy)) {
        if (target) XClass_delete_base((XClass*)target);
        return false;
    }
    maximum = oldRequest ? XHttpRequest_maximumRedirectsAllowed(oldRequest) : -1;
    if (maximum < 0)
        maximum = 50;
    if (tx->m_redirectCount >= maximum) {
        XClass_delete_base((XClass*)target);
        return false;
    }
    nextRequest = XHttpRequest_create_copy(oldRequest);
    if (!nextRequest || !XHttpRequest_setUrl(nextRequest, target)) {
        if (nextRequest) XClass_delete_base((XClass*)nextRequest);
        XClass_delete_base((XClass*)target);
        return false;
    }
    if (status == 303 || ((status == 301 || status == 302) &&
                          XHttpRequest_method(nextRequest) == XHttpRequest_Post)) {
        XHttpRequest_setMethod(nextRequest, XHttpRequest_Get);
        XHttpRequest_setBody(nextRequest, NULL);
    }
    xhttp_manager_apply_cookie_header(tx->m_manager, nextRequest);
    nextWire = XHttpRequest_toHttp1(nextRequest, true);
    if (!nextWire) {
        XClass_delete_base((XClass*)nextRequest);
        XClass_delete_base((XClass*)target);
        return false;
    }
    XHttpReply_redirected_signal(reply, target);
    xhttp_manager_close_socket(tx);
    if (!XHttpReply_resetForRequest(reply, nextRequest)) {
        XClass_delete_base((XClass*)nextWire);
        XClass_delete_base((XClass*)nextRequest);
        XClass_delete_base((XClass*)target);
        return false;
    }
    if (tx->m_requestWire)
        XClass_delete_base((XClass*)tx->m_requestWire);
    tx->m_requestWire = nextWire;
    tx->m_writeOffset = 0;
    tx->m_requestSent = false;
    ++tx->m_redirectCount;
    tx->m_authenticationAttempts = 0;
    tx->m_authenticationCacheUsed = false;
    if (tx->m_ntlmAuthenticator) {
        XClass_delete_base((XClass*)tx->m_ntlmAuthenticator);
        tx->m_ntlmAuthenticator = NULL;
    }
    XClass_delete_base((XClass*)nextRequest);
    XClass_delete_base((XClass*)target);
    if (!xhttp_manager_start_socket(tx))
        XHttpReply_setError(reply, XHttpReply_UnknownNetworkError, "重定向目标连接失败");
    return true;
}

static void xhttp_manager_socket_connected(XObject* receiver, XVarList* args)
{
    (void)args;
    XNetworkAccessManager* manager = (XNetworkAccessManager*)receiver;
    XHttpTransaction* tx = xhttp_manager_find_socket(manager, XObject_sender(receiver));
    if (!tx)
        return;
    if (tx->m_tls) {
        XSslSocket_startClientEncryption((XSslSocket*)tx->m_socket);
        return;
    }
    if (xhttp_manager_request_http2_direct(XHttpReply_request_const(tx->m_reply))) {
        if (!xhttp_manager_promote_http2(tx))
            XHttpReply_setError(tx->m_reply, XHttpReply_UnknownNetworkError,
                                "HTTP/2 直连初始化或请求发送失败");
        return;
    }
    if (!xhttp_manager_send_pending(tx))
        XHttpReply_setError(tx->m_reply, XHttpReply_UnknownNetworkError, "HTTP 请求发送失败");
}

static bool xhttp_manager_start_http2(XHttpTransaction* tx)
{
    const XHttpRequest* request;
    const XHttp2Configuration* configuration;
    XHttp2ClientSession* session;
    XHttp2HeaderDecoder* decoder;
    XByteArray* wire;
    XByteArray* input;
    XByteArray* headerBlock;
    uint32_t streamId = 0;
    if (!tx || !tx->m_reply || tx->m_http2)
        return false;
    request = XHttpReply_request_const(tx->m_reply);
    configuration = request ? XHttpRequest_http2Configuration_const(request) : NULL;
    session = XHttp2ClientSession_create();
    if (session && configuration &&
        !XHttp2ClientSession_setConfiguration(session, configuration)) {
        XClass_delete_base((XClass*)session);
        session = NULL;
    }
    wire = session ? XHttp2ClientSession_encodeRequest(session, request, &streamId) : NULL;
    decoder = wire ? XHttp2HeaderDecoder_create() : NULL;
    input = decoder ? XByteArray_create() : NULL;
    headerBlock = input ? XByteArray_create() : NULL;
    if (!session || !wire || !decoder || !input || !headerBlock || streamId == 0) {
        if (headerBlock) XClass_delete_base((XClass*)headerBlock);
        if (input) XClass_delete_base((XClass*)input);
        if (decoder) XClass_delete_base((XClass*)decoder);
        if (wire) XClass_delete_base((XClass*)wire);
        if (session) XClass_delete_base((XClass*)session);
        return false;
    }
    if (tx->m_requestWire)
        XClass_delete_base((XClass*)tx->m_requestWire);
    xhttp_manager_reset_http2(tx);
    tx->m_requestWire = wire;
    tx->m_http2Session = session;
    tx->m_http2Decoder = decoder;
    tx->m_http2Input = input;
    tx->m_http2HeaderBlock = headerBlock;
    tx->m_http2StreamId = streamId;
    tx->m_http2 = true;
    tx->m_http2SessionRecvTarget = configuration ?
        XHttp2Configuration_sessionReceiveWindowSize(configuration) : 65535;
    tx->m_http2StreamRecvTarget = configuration ?
        XHttp2Configuration_streamReceiveWindowSize(configuration) : 65535;
    tx->m_http2SessionRecvWindow = tx->m_http2SessionRecvTarget;
    tx->m_http2StreamRecvWindow = tx->m_http2StreamRecvTarget;
    tx->m_http2SessionSendWindow = 65535;
    tx->m_http2StreamSendWindow = 65535;
    tx->m_http2PeerInitialWindowSize = 65535;
    tx->m_http2PeerMaxFrameSize = XHttp2Configuration_MinFrameSize;
    tx->m_http2SettingsAckPending = true;
    tx->m_writeOffset = 0;
    tx->m_requestSent = false;
    {
        XVariant* used = XVariant_create_bool(true);
        if (used) {
            XHttpRequest_setAttribute(tx->m_reply->m_request,
                                      XHttpRequest_Http2WasUsedAttribute, used);
            XClass_delete_base((XClass*)used);
        }
    }
    return true;
}

static void xhttp_manager_socket_encrypted(XObject* receiver, XVarList* args)
{
    (void)args;
    XNetworkAccessManager* manager = (XNetworkAccessManager*)receiver;
    XHttpTransaction* tx = xhttp_manager_find_socket(manager, XObject_sender(receiver));
    if (tx) {
        XByteArray* negotiated = XSslSocket_nextNegotiatedProtocol((XSslSocket*)tx->m_socket);
        bool isHttp2 = xhttp_manager_request_http2_direct(XHttpReply_request_const(tx->m_reply)) ||
            (negotiated &&
            XContainer_size_base((const XContainer*)negotiated) == 2 &&
            memcmp(XByteArray_constData(negotiated), "h2", 2) == 0);
        if (negotiated)
            XClass_delete_base((XClass*)negotiated);
        if (isHttp2) {
            if (!xhttp_manager_promote_http2(tx))
                XHttpReply_setError(tx->m_reply,
                                    XHttpReply_UnknownNetworkError,
                                    "HTTP/2 连接初始化或请求发送失败");
            return;
        }
        if (!xhttp_manager_send_pending(tx))
            XHttpReply_setError(tx->m_reply, XHttpReply_UnknownNetworkError,
                                "TLS 后 HTTP 请求发送失败");
    }
}

static void xhttp_manager_socket_ready_read(XObject* receiver, XVarList* args)
{
    (void)args;
    XNetworkAccessManager* manager = (XNetworkAccessManager*)receiver;
    XObject* sender = XObject_sender(receiver);
    XHttp2SharedConnection* connection = xhttp_manager_find_shared_http2_socket(manager, sender);
    XHttpTransaction* tx;
    if (connection) {
        xhttp_manager_shared_http2_read_available(connection);
        return;
    }
    tx = xhttp_manager_find_socket(manager, sender);
    if (tx)
        xhttp_manager_read_available(tx);
}

static void xhttp_manager_socket_disconnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XNetworkAccessManager* manager = (XNetworkAccessManager*)receiver;
    XObject* sender = XObject_sender(receiver);
    XHttp2SharedConnection* connection = xhttp_manager_find_shared_http2_socket(manager, sender);
    XHttpTransaction* tx;
    if (connection) {
        xhttp_manager_shared_http2_fail(connection,
                                        XHttpReply_RemoteHostClosedError,
                                        "HTTP/2 连接在响应结束前断开");
        return;
    }
    tx = xhttp_manager_find_socket(manager, sender);
    if (tx && !XHttpReply_isFinished(tx->m_reply)) {
        if (tx->m_http2)
            XHttpReply_setError(tx->m_reply,
                                XHttpReply_ProtocolInvalidOperationError,
                                "HTTP/2 连接在 END_STREAM 前断开");
        else
            XHttpReply_endOfInput(tx->m_reply);
    }
    if (tx)
        xhttp_manager_handle_reply_state(tx);
}

static XHttpReply_NetworkError xhttp_manager_map_socket_error(XAbstractSocket_SocketError error)
{
    switch (error) {
    case XAbstractSocket_ConnectionRefusedError: return XHttpReply_ConnectionRefusedError;
    case XAbstractSocket_RemoteHostClosedError: return XHttpReply_RemoteHostClosedError;
    case XAbstractSocket_HostNotFoundError: return XHttpReply_HostNotFoundError;
    case XAbstractSocket_SocketTimeoutError: return XHttpReply_TimeoutError;
    case XAbstractSocket_SslHandshakeFailedError: return XHttpReply_SslHandshakeFailedError;
    case XAbstractSocket_ProxyAuthenticationRequiredError: return XHttpReply_AuthenticationRequiredError;
    default: return XHttpReply_UnknownNetworkError;
    }
}

static void xhttp_manager_socket_error(XObject* receiver, XVarList* args)
{
    XNetworkAccessManager* manager = (XNetworkAccessManager*)receiver;
    XObject* sender = XObject_sender(receiver);
    XHttp2SharedConnection* connection = xhttp_manager_find_shared_http2_socket(manager, sender);
    XHttpTransaction* tx;
    XAbstractSocket_SocketError socketError = XAbstractSocket_UnknownSocketError;
    if (args) {
        XVarList_start(args);
        socketError = XVarList_arg(args, XAbstractSocket_SocketError);
    }
    if (connection) {
        XString* errorString = XAbstractSocket_errorString(connection->m_socket);
        const char* text = errorString ? XString_toUtf8(errorString) : NULL;
        xhttp_manager_shared_http2_fail(connection,
                                        xhttp_manager_map_socket_error(socketError), text);
        if (errorString)
            XClass_delete_base((XClass*)errorString);
        return;
    }
    tx = xhttp_manager_find_socket(manager, sender);
    if (tx) {
        XString* errorString = XAbstractSocket_errorString(tx->m_socket);
        const char* text = errorString ? XString_toUtf8(errorString) : NULL;
        XHttpReply_setError(tx->m_reply, xhttp_manager_map_socket_error(socketError), text);
        if (errorString)
            XClass_delete_base((XClass*)errorString);
    }
}

static void xhttp_manager_reply_finished(XObject* receiver, XVarList* args)
{
    (void)args;
    XNetworkAccessManager* manager = (XNetworkAccessManager*)receiver;
    XHttpReply* reply = (XHttpReply*)XObject_sender(receiver);
    XHttpTransaction* tx = xhttp_manager_find_reply(manager, (XObject*)reply);
    if (!tx)
        return;
    xhttp_manager_process_hsts_response(manager, reply);
    if (manager && manager->m_cookieJar) {
        const XHttpRequest* request = XHttpReply_request_const(reply);
        if (request)
            XNetworkCookieJar_setCookiesFromHeaders(manager->m_cookieJar,
                                                    XHttpReply_headers_const(reply),
                                                    XHttpRequest_url_const(request));
    }
    if (manager && manager->m_cache && XHttpReply_statusCode(reply) == 200) {
        const XHttpRequest* request = XHttpReply_request_const(reply);
        if (request && XHttpRequest_method(request) == XHttpRequest_Get) {
            XNetworkCacheMetaData* metadata = XNetworkCacheMetaData_create();
            if (metadata && XNetworkCacheMetaData_setUrl(metadata, XHttpRequest_url_const(request)) &&
                XNetworkCacheMetaData_setHeaders(metadata, XHttpReply_headers_const(reply))) {
                XNetworkDiskCache_insert(manager->m_cache, metadata, XHttpReply_body_const(reply));
            }
            if (metadata)
                XClass_delete_base((XClass*)metadata);
        }
    }
    if (!tx->m_redirectFinished && xhttp_manager_schedule_redirect_event(tx))
        return;
    if (!tx->m_completionPosted) {
        XVarList* eventArgs = XVarList_create(2, sizeof(XHttpTransaction*), &tx);
        XEventFunc* event = XEventFunc_create(xhttp_manager_deferred_complete_event, eventArgs, NULL);
        if (event) {
            tx->m_completionPosted = true;
            XCoreApplication_postEvent((XObject*)manager, (XEvent*)event, XEVENT_PRIORITY_NORMAL);
            return;
        }
        if (eventArgs) XVarList_delete(eventArgs);
    }
    xhttp_manager_complete(tx);
}

static bool xhttp_manager_should_defer_redirect(const XHttpTransaction* tx)
{
    const XHttpRequest* request;
    XHttpRequest_RedirectPolicy policy;
    int status;
    if (!tx || !tx->m_reply)
        return false;
    status = XHttpReply_statusCode(tx->m_reply);
    if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)
        return false;
    request = XHttpReply_request_const(tx->m_reply);
    policy = request ? XHttpRequest_redirectPolicy(request) : XHttpRequest_ManualRedirectPolicy;
    return policy != XHttpRequest_ManualRedirectPolicy && policy != XHttpRequest_UserVerifiedRedirectPolicy;
}

static bool xhttp_manager_schedule_redirect_event(XHttpTransaction* tx)
{
    XVarList* eventArgs;
    XEventFunc* event;
    if (!tx || tx->m_redirectFinished || !xhttp_manager_should_defer_redirect(tx))
        return false;
    eventArgs = XVarList_create(2, sizeof(XHttpTransaction*), &tx);
    event = XEventFunc_create(xhttp_manager_deferred_redirect_event, eventArgs, NULL);
    if (!event) {
        if (eventArgs) XVarList_delete(eventArgs);
        return false;
    }
    XCoreApplication_postEvent((XObject*)tx->m_manager, (XEvent*)event, XEVENT_PRIORITY_NORMAL);
    return true;
}

static void xhttp_manager_handle_reply_state(XHttpTransaction* tx)
{
    if (!tx || !tx->m_reply || !XHttpReply_finishedSignalPending(tx->m_reply))
        return;
    if (XHttpReply_authenticationPending(tx->m_reply)) {
        if (xhttp_manager_retry_authentication(tx))
            return;
        XHttpReply_finishAuthenticationChallenge(tx->m_reply);
    }
    if (!XHttpReply_finishedSignalPending(tx->m_reply))
        return;
    if (!tx->m_redirectFinished && xhttp_manager_should_defer_redirect(tx)) {
        if (xhttp_manager_schedule_redirect_event(tx))
            return;
        tx->m_redirectFinished = true;
    }
    XHttpReply_emitFinished(tx->m_reply);
}

static void xhttp_manager_deferred_complete_event(XVarList* args)
{
    XHttpTransaction* tx;
    if (!args)
        return;
    XVarList_start(args);
    tx = XVarList_arg(args, XHttpTransaction*);
    /* h2c 升级会复位 101 响应，旧的延迟完成事件不能终止新的 HTTP/2 流。 */
    if (!tx || tx->m_finished || !tx->m_completionPosted)
        return;
    tx->m_completionPosted = false;
    xhttp_manager_complete(tx);
}

static void VXNetworkAccessManager_timerEvent(XNetworkAccessManager* self, XTimerEvent* event)
{
    XTimerId timerId;
    if (!self || !event)
        return;
    XEvent_accept((XEvent*)event);
    timerId = XTimerEvent_timerId(event);
    for (size_t i = 0; self->m_transactions &&
                      i < XContainer_size_base((const XContainer*)self->m_transactions); ++i) {
        XHttpTransaction* tx = xhttp_manager_transaction_at(self, i);
        if (tx && tx->m_timeoutTimer == timerId) {
            tx->m_timeoutTimer = XTIMER_INVALID_ID;
            XObject_killTimer((XObject*)self, timerId);
            XHttpReply_setError(tx->m_reply, XHttpReply_TimeoutError, "HTTP 请求超时");
            return;
        }
    }
}

static void xhttp_manager_complete(XHttpTransaction* tx)
{
    XNetworkAccessManager* manager;
    XHttpReply* reply;
    bool autoDelete;
    if (!tx)
        return;
    manager = tx->m_manager;
    reply = tx->m_reply;
    autoDelete = manager ? manager->m_autoDeleteReplies : false;
    xhttp_manager_transaction_finish(tx);
    if (manager && !manager->m_deinitializing) {
        XNetworkAccessManager_finished_signal(manager, reply);
        if (autoDelete)
            XObject_deleteLater((XObject*)reply);
    }
}

static void xhttp_manager_deferred_redirect_event(XVarList* args)
{
    XHttpTransaction* tx;
    XNetworkAccessManager* manager;
    if (!args)
        return;
    XVarList_start(args);
    tx = XVarList_arg(args, XHttpTransaction*);
    if (!tx || tx->m_finished)
        return;
    manager = tx->m_manager;
    if (!manager || manager->m_deinitializing ||
        xhttp_manager_find_reply(manager, (XObject*)tx->m_reply) != tx)
        return;
    if (!xhttp_manager_try_redirect(tx))
    {
        tx->m_redirectFinished = true;
        XHttpReply_emitFinished(tx->m_reply);
    }
}

static void xhttp_manager_transaction_finish(XHttpTransaction* tx)
{
    if (!tx || tx->m_finished)
        return;
    tx->m_finished = true;
    if (tx->m_manager)
        xhttp_manager_remove_transaction(tx->m_manager, tx);
    xhttp_manager_transaction_destroy(tx);
}

XVtable* XNetworkAccessManager_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNetworkAccessManager)
	XCLASS_SET_CLASS_NAME_DEFAULT("XNetworkAccessManager");
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkAccessManager_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXNetworkAccessManager_timerEvent);
    XCLASS_SHOW_SIZE_DEFAULT(XNetworkAccessManager);
    return XVTABLE_DEFAULT;
}

void XNetworkAccessManager_init(XNetworkAccessManager* self)
{
    if (!self)
        return;
    XObject_init((XObject*)self);
    XClassSetVtable(self, XNetworkAccessManager);
    self->m_transactions = XVector_create(sizeof(XHttpTransaction*));
    self->m_http2Connections = XVector_create(sizeof(XHttp2SharedConnection*));
    self->m_proxy = NULL;
    self->m_cookieJar = XNetworkCookieJar_create();
    self->m_cache = NULL;
    self->m_authenticationCache = XVector_create(sizeof(XHttpAuthenticationCacheEntry*));
    self->m_hstsPolicies = XVector_create(sizeof(XHstsPolicy*));
    self->m_hstsStoreDirectory = XByteArray_create();
    self->m_transferTimeout = 30000;
    self->m_redirectPolicy = XHttpRequest_ManualRedirectPolicy;
    self->m_autoDeleteReplies = false;
    self->m_hstsEnabled = false;
    self->m_hstsStoreEnabled = false;
    self->m_sslPeerVerifyMode = XSSL_VerifyPeer;
    self->m_sslCaCertificate = NULL;
    self->m_deinitializing = false;
}

XNetworkAccessManager* XNetworkAccessManager_create(void)
{
    XNetworkAccessManager* self = (XNetworkAccessManager*)XMalloc_System(sizeof(XNetworkAccessManager));
    if (!self)
        return NULL;
    XNetworkAccessManager_init(self);
    if (!self->m_transactions || !self->m_http2Connections || !self->m_cookieJar ||
        !self->m_authenticationCache || !self->m_hstsPolicies ||
        !self->m_hstsStoreDirectory) {
        XNetworkAccessManager_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

static void VXNetworkAccessManager_deinit(XNetworkAccessManager* self)
{
    if (!self)
        return;
    self->m_deinitializing = true;
    XCoreApplication_removePostedEvents((XObject*)self, XEVENT_TYPE_FUNC_RUN);
    while (self->m_transactions && XContainer_size_base((const XContainer*)self->m_transactions) != 0) {
        XHttpTransaction* tx = xhttp_manager_transaction_at(self, 0);
        xhttp_manager_remove_transaction(self, tx);
        if (tx && tx->m_reply)
            XHttpReply_abort(tx->m_reply);
        xhttp_manager_transaction_destroy(tx);
    }
    while (self->m_http2Connections &&
           XContainer_size_base((const XContainer*)self->m_http2Connections) != 0) {
        XHttp2SharedConnection* connection = xhttp_manager_shared_http2_at(self, 0);
        xhttp_manager_remove_shared_http2(self, connection);
        xhttp_manager_shared_http2_destroy(connection);
    }
    if (self->m_transactions)
        XClass_delete_base((XClass*)self->m_transactions);
    if (self->m_http2Connections)
        XClass_delete_base((XClass*)self->m_http2Connections);
    if (self->m_proxy)
        XClass_delete_base((XClass*)self->m_proxy);
    if (self->m_cookieJar)
        XClass_delete_base((XClass*)self->m_cookieJar);
    if (self->m_cache)
        XClass_delete_base((XClass*)self->m_cache);
    xhttp_manager_authentication_cache_clear(self);
    if (self->m_authenticationCache)
        XClass_delete_base((XClass*)self->m_authenticationCache);
    if (self->m_hstsPolicies)
        XHstsPolicy_list_free(self->m_hstsPolicies);
    if (self->m_hstsStoreDirectory)
        XClass_delete_base((XClass*)self->m_hstsStoreDirectory);
    if (self->m_sslCaCertificate)
        XSsl_certificateDestroy(self->m_sslCaCertificate);
    self->m_transactions = NULL;
    self->m_http2Connections = NULL;
    self->m_proxy = NULL;
    self->m_cookieJar = NULL;
    self->m_cache = NULL;
    self->m_authenticationCache = NULL;
    self->m_hstsPolicies = NULL;
    self->m_hstsStoreDirectory = NULL;
    self->m_sslCaCertificate = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static XHttpReply* xhttp_manager_send(XNetworkAccessManager* self,
                                      XNetworkAccessManager_Operation operation,
                                      const XHttpRequest* request,
                                      const XByteArray* body,
                                      const XByteArray* customMethod)
{
    XHttpRequest* requestCopy;
    XHttpReply* reply;
    XHttpTransaction* tx;
    XByteArray* wire;
    int timeout;
    bool h2cUpgrade;
    if (!self || self->m_deinitializing || !request || !self->m_transactions)
        return NULL;
    requestCopy = XHttpRequest_create_copy(request);
    if (!requestCopy)
        return NULL;
    if (operation == XNetworkAccessManager_HeadOperation)
        XHttpRequest_setMethod(requestCopy, XHttpRequest_Head);
    else if (operation == XNetworkAccessManager_GetOperation) {
        XHttpRequest_setMethod(requestCopy, XHttpRequest_Get);
        XHttpRequest_setBody(requestCopy, NULL);
    }
    else if (operation == XNetworkAccessManager_PostOperation)
        XHttpRequest_setMethod(requestCopy, XHttpRequest_Post);
    else if (operation == XNetworkAccessManager_PutOperation)
        XHttpRequest_setMethod(requestCopy, XHttpRequest_Put);
    else if (operation == XNetworkAccessManager_DeleteOperation)
        XHttpRequest_setMethod(requestCopy, XHttpRequest_Delete);
    else if (operation == XNetworkAccessManager_CustomOperation) {
        if (!customMethod || !XHttpRequest_setCustomMethod_bytes(requestCopy, customMethod)) {
            XClass_delete_base((XClass*)requestCopy);
            return NULL;
        }
    }
    if (XHttpRequest_redirectPolicy(requestCopy) == XHttpRequest_ManualRedirectPolicy &&
        self->m_redirectPolicy != XHttpRequest_ManualRedirectPolicy)
        XHttpRequest_setRedirectPolicy(requestCopy, self->m_redirectPolicy);
    if (!xhttp_manager_apply_hsts(self, requestCopy)) {
        XClass_delete_base((XClass*)requestCopy);
        return NULL;
    }
    if (body && !XHttpRequest_setBody(requestCopy, body)) {
        XClass_delete_base((XClass*)requestCopy);
        return NULL;
    }
    timeout = XHttpRequest_transferTimeout(requestCopy);
    if (timeout <= 0)
        timeout = self->m_transferTimeout;
    xhttp_manager_apply_cookie_header(self, requestCopy);
    h2cUpgrade = xhttp_manager_request_should_h2c_upgrade(requestCopy) &&
                 !xhttp_manager_find_reusable_http2(self, requestCopy);
    if (h2cUpgrade && !xhttp_manager_prepare_h2c_upgrade(requestCopy)) {
        XClass_delete_base((XClass*)requestCopy);
        return NULL;
    }
    reply = XHttpReply_create(requestCopy);
    if (!reply) {
        XClass_delete_base((XClass*)requestCopy);
        return NULL;
    }
    XHttpReply_setDeferAuthentication(reply, true);
    wire = XHttpRequest_toHttp1(requestCopy, true);
    XClass_delete_base((XClass*)requestCopy);
    if (!wire) {
        XClass_delete_base((XClass*)reply);
        return NULL;
    }
    tx = (XHttpTransaction*)XMalloc_System(sizeof(XHttpTransaction));
    if (!tx) {
        XClass_delete_base((XClass*)wire);
        XClass_delete_base((XClass*)reply);
        return NULL;
    }
    memset(tx, 0, sizeof(*tx));
    tx->m_manager = self;
    tx->m_reply = reply;
    tx->m_requestWire = wire;
    tx->m_h2cUpgradeRequested = h2cUpgrade;
    tx->m_timeoutTimer = XTIMER_INVALID_ID;
    tx->m_replyFinished = XObject_connect_1((XObject*)reply,
        XSignal(XHttpReply_finished_signal), (XObject*)self,
        xhttp_manager_reply_finished, XConnectionType_Direct);
    if (!tx->m_replyFinished || !XVector_push_back_1_base(self->m_transactions, &tx)) {
        xhttp_manager_transaction_destroy(tx);
        XClass_delete_base((XClass*)reply);
        return NULL;
    }
    if (timeout > 0)
        tx->m_timeoutTimer = XObject_startTimer_ms((XObject*)self, (uint64_t)timeout,
                                                    XTimerType_CoarseTimer);
    {
        XHttp2SharedConnection* connection = xhttp_manager_find_reusable_http2(
            self, XHttpReply_request_const(reply));
        if (connection && xhttp_manager_shared_http2_send(connection, tx))
            return reply;
    }
    if (!xhttp_manager_start_socket(tx)) {
        xhttp_manager_remove_transaction(self, tx);
        xhttp_manager_transaction_destroy(tx);
        XClass_delete_base((XClass*)reply);
        return NULL;
    }
    return reply;
}

XHttpReply* XNetworkAccessManager_sendRequest(XNetworkAccessManager* self,
                                              XNetworkAccessManager_Operation operation,
                                              const XHttpRequest* request,
                                              const XByteArray* body,
                                              const XByteArray* customMethod)
{
    return xhttp_manager_send(self, operation, request, body, customMethod);
}

XHttpReply* XNetworkAccessManager_head(XNetworkAccessManager* self, const XHttpRequest* request)
{
    return xhttp_manager_send(self, XNetworkAccessManager_HeadOperation, request, NULL, NULL);
}

XHttpReply* XNetworkAccessManager_get(XNetworkAccessManager* self, const XHttpRequest* request)
{
    return xhttp_manager_send(self, XNetworkAccessManager_GetOperation, request, NULL, NULL);
}

XHttpReply* XNetworkAccessManager_post(XNetworkAccessManager* self, const XHttpRequest* request, const XByteArray* body)
{
    return xhttp_manager_send(self, XNetworkAccessManager_PostOperation, request, body, NULL);
}

XHttpReply* XNetworkAccessManager_postMultipart(XNetworkAccessManager* self,
                                                const XHttpRequest* request,
                                                const XHttpMultiPart* multipart)
{
    XHttpRequest* requestCopy;
    XByteArray* body;
    XByteArray* contentType;
    XByteArray* contentTypeName;
    XHttpReply* reply;
    if (!self || !request || !multipart)
        return NULL;
    body = XHttpMultiPart_toByteArray(multipart);
    contentType = XHttpMultiPart_contentType(multipart);
    requestCopy = XHttpRequest_create_copy(request);
    contentTypeName = XByteArray_create_utf8("Content-Type");
    if (!body || !contentType || !requestCopy || !contentTypeName) {
        if (body) XClass_delete_base((XClass*)body);
        if (contentType) XClass_delete_base((XClass*)contentType);
        if (requestCopy) XClass_delete_base((XClass*)requestCopy);
        if (contentTypeName) XClass_delete_base((XClass*)contentTypeName);
        return NULL;
    }
    if (!XHttpHeaders_contains(XHttpRequest_headers_const(requestCopy), contentTypeName) &&
        !XHttpHeaders_replaceOrAppend(XHttpRequest_headers(requestCopy), contentTypeName, contentType)) {
        XClass_delete_base((XClass*)body);
        XClass_delete_base((XClass*)contentType);
        XClass_delete_base((XClass*)requestCopy);
        XClass_delete_base((XClass*)contentTypeName);
        return NULL;
    }
    reply = xhttp_manager_send(self, XNetworkAccessManager_PostOperation, requestCopy, body, NULL);
    XClass_delete_base((XClass*)body);
    XClass_delete_base((XClass*)contentType);
    XClass_delete_base((XClass*)requestCopy);
    XClass_delete_base((XClass*)contentTypeName);
    return reply;
}

XHttpReply* XNetworkAccessManager_put(XNetworkAccessManager* self, const XHttpRequest* request, const XByteArray* body)
{
    return xhttp_manager_send(self, XNetworkAccessManager_PutOperation, request, body, NULL);
}

XHttpReply* XNetworkAccessManager_deleteResource(XNetworkAccessManager* self, const XHttpRequest* request)
{
    return xhttp_manager_send(self, XNetworkAccessManager_DeleteOperation, request, NULL, NULL);
}

XHttpReply* XNetworkAccessManager_sendCustomRequest(XNetworkAccessManager* self, const XHttpRequest* request,
                                                    const XByteArray* customMethod, const XByteArray* body)
{
    return xhttp_manager_send(self, XNetworkAccessManager_CustomOperation, request, body, customMethod);
}

void XNetworkAccessManager_setTransferTimeout(XNetworkAccessManager* self, int timeout)
{
    if (self) self->m_transferTimeout = timeout < 0 ? 0 : timeout;
}

int XNetworkAccessManager_transferTimeout(const XNetworkAccessManager* self)
{
    return self ? self->m_transferTimeout : 30000;
}

void XNetworkAccessManager_setRedirectPolicy(XNetworkAccessManager* self, XHttpRequest_RedirectPolicy policy)
{
    if (self && policy >= XHttpRequest_ManualRedirectPolicy && policy <= XHttpRequest_UserVerifiedRedirectPolicy)
        self->m_redirectPolicy = policy;
}

XHttpRequest_RedirectPolicy XNetworkAccessManager_redirectPolicy(const XNetworkAccessManager* self)
{
    return self ? self->m_redirectPolicy : XHttpRequest_ManualRedirectPolicy;
}

void XNetworkAccessManager_setAutoDeleteReplies(XNetworkAccessManager* self, bool enabled)
{
    if (self) self->m_autoDeleteReplies = enabled;
}

bool XNetworkAccessManager_autoDeleteReplies(const XNetworkAccessManager* self)
{
    return self ? self->m_autoDeleteReplies : false;
}

void XNetworkAccessManager_setSslPeerVerifyMode(XNetworkAccessManager* self,
                                                XSslPeerVerifyMode mode)
{
    if (self && mode >= XSSL_VerifyNone && mode <= XSSL_AutoVerifyPeer)
        self->m_sslPeerVerifyMode = mode;
}

XSslPeerVerifyMode XNetworkAccessManager_sslPeerVerifyMode(
    const XNetworkAccessManager* self)
{
    return self ? self->m_sslPeerVerifyMode : XSSL_VerifyPeer;
}

bool XNetworkAccessManager_setSslCaCertificate(XNetworkAccessManager* self,
                                               XSslCertificate* certificate)
{
    if (!self)
        return false;
    if (self->m_sslCaCertificate == certificate)
        return true;
    if (self->m_sslCaCertificate)
        XSsl_certificateDestroy(self->m_sslCaCertificate);
    self->m_sslCaCertificate = certificate;
    return true;
}

const XSslCertificate* XNetworkAccessManager_sslCaCertificate_const(
    const XNetworkAccessManager* self)
{
    return self ? self->m_sslCaCertificate : NULL;
}

bool XNetworkAccessManager_setProxy(XNetworkAccessManager* self, const XNetworkProxy* proxy)
{
    XNetworkProxy* replacement = proxy ? XNetworkProxy_copy(proxy) : NULL;
    if (proxy && !replacement)
        return false;
    if (self && self->m_proxy)
        XClass_delete_base((XClass*)self->m_proxy);
    if (self)
        self->m_proxy = replacement;
    else if (replacement)
        XClass_delete_base((XClass*)replacement);
    return self != NULL;
}

const XNetworkProxy* XNetworkAccessManager_proxy_const(const XNetworkAccessManager* self)
{
    return self ? self->m_proxy : NULL;
}

bool XNetworkAccessManager_setCookieJar(XNetworkAccessManager* self, XNetworkCookieJar* cookieJar)
{
    if (!self || self->m_cookieJar == cookieJar)
        return self != NULL;
    if (self->m_cookieJar)
        XClass_delete_base((XClass*)self->m_cookieJar);
    self->m_cookieJar = cookieJar;
    return true;
}

XNetworkCookieJar* XNetworkAccessManager_cookieJar(const XNetworkAccessManager* self)
{
    return self ? self->m_cookieJar : NULL;
}

bool XNetworkAccessManager_setCache(XNetworkAccessManager* self, XNetworkDiskCache* cache)
{
    if (!self || self->m_cache == cache)
        return self != NULL;
    if (self->m_cache)
        XClass_delete_base((XClass*)self->m_cache);
    self->m_cache = cache;
    return true;
}

XNetworkDiskCache* XNetworkAccessManager_cache(const XNetworkAccessManager* self)
{
    return self ? self->m_cache : NULL;
}

void XNetworkAccessManager_clearAccessCache(XNetworkAccessManager* self)
{
    if (self && self->m_cache)
        XNetworkDiskCache_clear(self->m_cache);
}

void XNetworkAccessManager_clearConnectionCache(XNetworkAccessManager* self)
{
    (void)self;
    /* 当前 HTTP/1 实现每个请求独立连接，没有可清除的连接池。 */
}

void XNetworkAccessManager_setStrictTransportSecurityEnabled(XNetworkAccessManager* self,
                                                             bool enabled)
{
    if (self)
        self->m_hstsEnabled = enabled;
}

bool XNetworkAccessManager_isStrictTransportSecurityEnabled(const XNetworkAccessManager* self)
{
    return self ? self->m_hstsEnabled : false;
}

bool XNetworkAccessManager_enableStrictTransportSecurityStore(XNetworkAccessManager* self,
                                                             bool enabled,
                                                             const XByteArray* directory)
{
    XByteArray* replacement;
    if (!self)
        return false;
    replacement = directory ? XByteArray_create_copy(directory) : XByteArray_create();
    if (!replacement)
        return false;
    if (self->m_hstsStoreDirectory)
        XClass_delete_base((XClass*)self->m_hstsStoreDirectory);
    self->m_hstsStoreDirectory = replacement;
    self->m_hstsStoreEnabled = enabled;
    if (enabled)
        xhttp_manager_hsts_store_load(self);
    return true;
}

bool XNetworkAccessManager_isStrictTransportSecurityStoreEnabled(const XNetworkAccessManager* self)
{
    return self ? self->m_hstsStoreEnabled : false;
}

bool XNetworkAccessManager_addStrictTransportSecurityHosts(XNetworkAccessManager* self,
                                                            const XVector* policies)
{
    if (!self || !self->m_hstsPolicies || !policies)
        return false;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)policies); ++i) {
        XHstsPolicy* const* policy = (XHstsPolicy* const*)XVector_at_base(policies, (int64_t)i);
        if (!policy || !*policy || !xhttp_manager_hsts_insert(self, *policy))
            return false;
    }
    xhttp_manager_hsts_store_save(self);
    return true;
}

XVector* XNetworkAccessManager_strictTransportSecurityHosts(const XNetworkAccessManager* self)
{
    XVector* result = XVector_create(sizeof(XHstsPolicy*));
    if (!result)
        return NULL;
    if (!self || !self->m_hstsPolicies)
        return result;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_hstsPolicies); ++i) {
        XHstsPolicy* const* policy =
            (XHstsPolicy* const*)XVector_at_base(self->m_hstsPolicies, (int64_t)i);
        XHstsPolicy* copy;
        if (!policy || !*policy || XHstsPolicy_isExpired(*policy))
            continue;
        copy = XHstsPolicy_create_copy(*policy);
        if (!copy || !XVector_push_back_1_base(result, &copy)) {
            if (copy)
                XClass_delete_base((XClass*)copy);
            XHstsPolicy_list_free(result);
            return NULL;
        }
    }
    return result;
}

size_t XNetworkAccessManager_activeReplyCount(const XNetworkAccessManager* self)
{
    return self && self->m_transactions ? XContainer_size_base((const XContainer*)self->m_transactions) : 0;
}

void* XNetworkAccessManager_finished_signal(XNetworkAccessManager* self, XHttpReply* reply)
{
    XEmitSignal((XObject*)self, XNetworkAccessManager_finished_signal,
                XVarList_create(2, sizeof(XHttpReply*), &reply), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XNetworkAccessManager_authenticationRequired_signal(
    XNetworkAccessManager* self, XHttpReply* reply, XHttpAuthenticator* authenticator)
{
    XEmitSignal((XObject*)self, XNetworkAccessManager_authenticationRequired_signal,
                XVarList_create(4, sizeof(XHttpReply*), &reply,
                                sizeof(XHttpAuthenticator*), &authenticator),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XNetworkAccessManager_proxyAuthenticationRequired_signal(
    XNetworkAccessManager* self, XNetworkProxy* proxy, XHttpAuthenticator* authenticator)
{
    XEmitSignal((XObject*)self, XNetworkAccessManager_proxyAuthenticationRequired_signal,
                XVarList_create(4, sizeof(XNetworkProxy*), &proxy,
                                sizeof(XHttpAuthenticator*), &authenticator),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
