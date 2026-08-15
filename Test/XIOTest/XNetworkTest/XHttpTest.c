/**
 * @file       XHttpTest.c
 * @brief      HTTP 核心数据模型和增量解析测试。
 */

#include "XPrintf.h"
#include "XHttpTest.h"

#include "XHttpHeaders.h"
#include "XHttpMultipart.h"
#include "XHttpReply.h"
#include "XHttpAuthenticator.h"
#include "XHttpRequest.h"
#include "XNetworkCookie.h"
#include "XNetworkAccessManager.h"
#include "XNetworkCache.h"
#include "XHttp2Frame.h"
#include "XHttp2Headers.h"
#include "XHttp2Client.h"
#include "XHttp2Connection.h"
#include "XHttpServer.h"
#include "XHttpServerRouter.h"
#include "XHttpServerRouterRule.h"
#include "XHttpServerWebSocketUpgradeResponse.h"
#include "XSslSocket.h"
#include "XCryptographic.h"
#include "XNetworkRequestFactory.h"
#include "XRestAccessManager.h"
#include "XRestReply.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XMemory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool xhttp_test_bytes_equal(const XByteArray* value, const char* expected)
{
    size_t size = expected ? strlen(expected) : 0;
    return value && XContainer_size_base((const XContainer*)value) == size &&
           (size == 0 || memcmp(XByteArray_constData((XByteArray*)value), expected, size) == 0);
}

static bool xhttp_test_bytes_contains(const XByteArray* value, const char* expected)
{
    size_t valueSize = value ? XContainer_size_base((const XContainer*)value) : 0;
    size_t expectedSize = expected ? strlen(expected) : 0;
    const uint8_t* data = value ? XByteArray_constData((XByteArray*)value) : NULL;
    if (!data || expectedSize > valueSize)
        return expectedSize == 0;
    for (size_t i = 0; i + expectedSize <= valueSize; ++i) {
        if (expectedSize == 0 || memcmp(data + i, expected, expectedSize) == 0)
            return true;
    }
    return false;
}

static int xhttp_test_authentication_signal_count = 0;

static uint16_t xhttp_test_read_le16(const uint8_t* value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t xhttp_test_read_le32(const uint8_t* value)
{
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
        ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

/* 仅检查 NTLM 报文的协议布局；NTLMv2 proof 依赖每轮的安全随机挑战。 */
static bool xhttp_test_ntlm_authorization(const XByteArray* header, unsigned int expectedType)
{
    const uint8_t* raw = header ? XByteArray_constData((XByteArray*)header) : NULL;
    size_t rawSize = header ? XContainer_size_base((const XContainer*)header) : 0;
    XByteArray* encoded;
    XByteArray* message;
    const uint8_t* data;
    size_t size;
    bool result = false;
    if (!raw || rawSize <= 5 || memcmp(raw, "NTLM ", 5) != 0)
        return false;
    encoded = XByteArray_create_with_data((const char*)raw + 5, rawSize - 5);
    message = encoded ? XByteArray_fromBase64(encoded) : NULL;
    data = message ? XByteArray_constData(message) : NULL;
    size = message ? XContainer_size_base((const XContainer*)message) : 0;
    if (data && size >= 12 && memcmp(data, "NTLMSSP", 8) == 0 &&
        xhttp_test_read_le32(data + 8) == expectedType) {
        if (expectedType == 1)
            result = size == 32 && (xhttp_test_read_le32(data + 12) & UINT32_C(0x00000001));
        else if (expectedType == 3 && size >= 64) {
            uint16_t ntLength = xhttp_test_read_le16(data + 20);
            uint32_t ntOffset = xhttp_test_read_le32(data + 24);
            uint16_t userLength = xhttp_test_read_le16(data + 36);
            uint32_t userOffset = xhttp_test_read_le32(data + 40);
            result = ntLength >= 48 && ntOffset <= size && ntLength <= size - ntOffset &&
                userLength != 0 && userOffset <= size && userLength <= size - userOffset;
        }
    }
    if (message) XClass_delete_base((XClass*)message);
    if (encoded) XClass_delete_base((XClass*)encoded);
    return result;
}

/* 以服务端已知的挑战、用户名和密码复算 NTLMv2 proof，验证 Type 3 的密码学字段。 */
static bool xhttp_test_ntlm_v2_proof_valid(const XByteArray* header)
{
    static const uint8_t serverChallenge[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    static const uint8_t passwordUtf16[] = {
        'p',0,'a',0,'s',0,'s',0,'w',0,'o',0,'r',0,'d',0
    };
    static const uint8_t upperUserUtf16[] = { 'U',0,'S',0,'E',0,'R',0 };
    const uint8_t* raw = header ? XByteArray_constData((XByteArray*)header) : NULL;
    size_t rawSize = header ? XContainer_size_base((const XContainer*)header) : 0;
    XByteArray* encoded = NULL;
    XByteArray* message = NULL;
    XByteArray* ntHash = NULL;
    XByteArray* v2Hash = NULL;
    XByteArray* proofInput = NULL;
    XByteArray* expectedProof = NULL;
    const uint8_t* data;
    size_t size;
    uint16_t ntLength;
    uint32_t ntOffset;
    bool result = false;
    if (!raw || rawSize <= 5 || memcmp(raw, "NTLM ", 5) != 0)
        goto done;
    encoded = XByteArray_create_with_data((const char*)raw + 5, rawSize - 5);
    message = encoded ? XByteArray_fromBase64(encoded) : NULL;
    data = message ? XByteArray_constData(message) : NULL;
    size = message ? XContainer_size_base((const XContainer*)message) : 0;
    if (!data || size < 64 || memcmp(data, "NTLMSSP", 8) != 0 ||
        xhttp_test_read_le32(data + 8) != 3)
        goto done;
    ntLength = xhttp_test_read_le16(data + 20);
    ntOffset = xhttp_test_read_le32(data + 24);
    if (ntLength < 56 || ntOffset > size || ntLength > size - ntOffset ||
        memcmp(data + ntOffset + 16, "\x01\x01\0\0\0\0\0\0", 8) != 0)
        goto done;
    ntHash = XCryptographicHash_hash((const char*)passwordUtf16, sizeof(passwordUtf16),
                                     XCryptographicHash_Md4);
    v2Hash = ntHash ? XCryptographicHash_hmac((const char*)XByteArray_constData(ntHash),
        XContainer_size_base((const XContainer*)ntHash), (const char*)upperUserUtf16,
        sizeof(upperUserUtf16), XCryptographicHash_Md5) : NULL;
    proofInput = XByteArray_create();
    if (!ntHash || !v2Hash || !proofInput ||
        !XByteArray_push_back_2((XVector*)proofInput, serverChallenge, sizeof(serverChallenge)) ||
        !XByteArray_push_back_2((XVector*)proofInput, data + ntOffset + 16, ntLength - 16))
        goto done;
    expectedProof = XCryptographicHash_hmac((const char*)XByteArray_constData(v2Hash),
        XContainer_size_base((const XContainer*)v2Hash), (const char*)XByteArray_constData(proofInput),
        XContainer_size_base((const XContainer*)proofInput), XCryptographicHash_Md5);
    result = expectedProof && XContainer_size_base((const XContainer*)expectedProof) == 16 &&
        memcmp(data + ntOffset, XByteArray_constData(expectedProof), 16) == 0;
done:
    if (expectedProof) XClass_delete_base((XClass*)expectedProof);
    if (proofInput) XClass_delete_base((XClass*)proofInput);
    if (v2Hash) XClass_delete_base((XClass*)v2Hash);
    if (ntHash) XClass_delete_base((XClass*)ntHash);
    if (message) XClass_delete_base((XClass*)message);
    if (encoded) XClass_delete_base((XClass*)encoded);
    return result;
}

/* 认证信号必须是同步调用，槽函数在返回前填写凭据才可以用于立即重发。 */
static void xhttp_test_authentication_required_slot(XObject* sender, XVarList* args)
{
    assert(sender && args);
    XVarList_args_2(args, XHttpReply*, reply, XHttpAuthenticator*, authenticator);
    assert(reply && authenticator);
    assert(XHttpAuthenticator_setUser_utf8((XHttpAuthenticator*)authenticator, "user"));
    assert(XHttpAuthenticator_setPassword_utf8((XHttpAuthenticator*)authenticator, "password"));
    ++xhttp_test_authentication_signal_count;
}

static void xhttp_test_route_handler(const XHttpServerRequest* request,
                                     XHttpServerResponder* responder,
                                     void* context)
{
    int* count = (int*)context;
    (void)request;
    (void)responder;
    if (count)
        ++*count;
}

typedef struct XHttpTestNtlmServerState {
    unsigned int m_requests;
    unsigned int m_type1;
    unsigned int m_type3;
} XHttpTestNtlmServerState;

/* 构造携带 TargetInfo 的最小 Type 2，覆盖 Unicode、服务器时间和 NTLMv2 target-info 路径。 */
static XByteArray* xhttp_test_ntlm_type2_header(void)
{
    static const uint8_t type2[] = {
        'N','T','L','M','S','S','P',0, 2,0,0,0,
        0,0,0,0, 48,0,0,0,
        1,0,136,0, 1,2,3,4,5,6,7,8, 0,0,0,0,0,0,0,0,
        16,0,16,0, 48,0,0,0,
        7,0,8,0, 0,0,0,0,0,0,0,0, 0,0,0,0
    };
    XByteArray* raw = XByteArray_create_with_data((const char*)type2, sizeof(type2));
    XByteArray* encoded = raw ? XByteArray_toBase64(raw) : NULL;
    XByteArray* result = NULL;
    if (encoded) {
        size_t size = XContainer_size_base((const XContainer*)encoded);
        result = XByteArray_create();
        if (!result || !XByteArray_append_utf8(result, "NTLM " ) ||
            !XByteArray_push_back_2((XVector*)result, XByteArray_constData(encoded), size)) {
            if (result) XClass_delete_base((XClass*)result);
            result = NULL;
        }
    }
    if (encoded) XClass_delete_base((XClass*)encoded);
    if (raw) XClass_delete_base((XClass*)raw);
    return result;
}

static void xhttp_test_ntlm_server_handler(const XHttpServerRequest* request,
                                           XHttpServerResponder* responder, void* context)
{
    XHttpTestNtlmServerState* state = (XHttpTestNtlmServerState*)context;
    XByteArray* name = XByteArray_create_utf8("authorization");
    XByteArray* authorization = name ? XHttpServerRequest_value(request, name) : NULL;
    XByteArray* body = NULL;
    XHttpServerResponse* response = NULL;
    if (state)
        ++state->m_requests;
    if (authorization && xhttp_test_ntlm_authorization(authorization, 1)) {
        XByteArray* challenge = xhttp_test_ntlm_type2_header();
        response = XHttpServerResponse_create_status(XHttpServerResponse_Unauthorized);
        if (challenge && response)
            XHttpHeaders_append_utf8(XHttpServerResponse_headers(response),
                                    "WWW-Authenticate", (const char*)XByteArray_constData(challenge));
        if (state) ++state->m_type1;
        if (challenge) XClass_delete_base((XClass*)challenge);
    } else if (authorization && xhttp_test_ntlm_authorization(authorization, 3) &&
               xhttp_test_ntlm_v2_proof_valid(authorization)) {
        body = XByteArray_create_utf8("ntlm-ok");
        response = body ? XHttpServerResponse_create_body(body, XHttpServerResponse_Ok) : NULL;
        if (state) ++state->m_type3;
    } else {
        response = XHttpServerResponse_create_status(XHttpServerResponse_Unauthorized);
        if (response)
            XHttpHeaders_append_utf8(XHttpServerResponse_headers(response),
                                    "WWW-Authenticate", "NTLM");
    }
    assert(response && XHttpServerResponder_sendResponse(responder, response));
    if (response) XClass_delete_base((XClass*)response);
    if (body) XClass_delete_base((XClass*)body);
    if (authorization) XClass_delete_base((XClass*)authorization);
    if (name) XClass_delete_base((XClass*)name);
}

static bool xhttp_test_has_header(const XHttpHeaders* headers, const char* name)
{
    XByteArray* field = XByteArray_create_utf8(name);
    bool result = field && XHttpHeaders_contains(headers, field);
    if (field)
        XClass_delete_base((XClass*)field);
    return result;
}

static void xhttp_test_headers(void)
{
    XHttpHeaders* headers = XHttpHeaders_create();
    XHttpHeaders* copy;
    XByteArray* combined;
    XByteArray* defaultValue;
    XByteArray* field;
    XByteArray* replaceName;
    XByteArray* replaceValue;
    XByteArray* missingName;
    XByteArray* result;
    XHttpHeaders* knownHeaders;
    XByteArray* knownValue;
    XByteArray* knownDefault;
    XVector* knownValues;
    XVector* values;
    assert(headers);
    assert(XHttpHeaders_append_utf8(headers, "X-Test", " one "));
    assert(XHttpHeaders_append_utf8(headers, "x-test", "two"));
    assert(xhttp_test_has_header(headers, "X-TEST"));
    assert(xhttp_test_bytes_equal(XHttpHeaders_nameAt_const(headers, 0), "x-test"));
    {
        XByteArray* field = XByteArray_create_utf8("x-test");
        combined = XHttpHeaders_combinedValue(headers, field);
        XClass_delete_base((XClass*)field);
    }
    assert(xhttp_test_bytes_equal(combined, "one, two"));
    XClass_delete_base((XClass*)combined);
    field = XByteArray_create_utf8("x-test");
    assert(field);
    values = XHttpHeaders_values(headers, field);
    XClass_delete_base((XClass*)field);
    assert(values && XContainer_size_base((const XContainer*)values) == 2);
    XHttpHeaders_values_free(values);
    assert(!XHttpHeaders_append_utf8(headers, "Bad Name", "value"));
    assert(!XHttpHeaders_append_utf8(headers, "X-Bad", "line\nbreak"));
    replaceName = XByteArray_create_utf8("X-Test");
    replaceValue = XByteArray_create_utf8("replacement");
    assert(replaceName && replaceValue);
    assert(XHttpHeaders_replaceOrAppend(headers, replaceName, replaceValue));
    XClass_delete_base((XClass*)replaceName);
    XClass_delete_base((XClass*)replaceValue);
    assert(XHttpHeaders_size(headers) == 1);
    defaultValue = XByteArray_create_utf8("default");
    missingName = XByteArray_create_utf8("missing");
    assert(defaultValue && missingName);
    result = XHttpHeaders_value_or(headers, missingName, defaultValue);
    assert(xhttp_test_bytes_equal(result, "default"));
    XClass_delete_base((XClass*)result);
    XClass_delete_base((XClass*)missingName);
    XClass_delete_base((XClass*)defaultValue);
    copy = XHttpHeaders_create_copy(headers);
    assert(copy && XHttpHeaders_size(copy) == XHttpHeaders_size(headers));
    XClass_delete_base((XClass*)copy);
    XClass_delete_base((XClass*)headers);

    knownHeaders = XHttpHeaders_create();
    knownValue = XByteArray_create_utf8(" text/plain ");
    knownDefault = XByteArray_create_utf8("default");
    assert(knownHeaders && knownValue && knownDefault);
    assert(strcmp(XHttpHeaders_wellKnownHeaderName(
                     XHttpHeaders_WellKnownHeader_ContentType), "content-type") == 0);
    assert(XHttpHeaders_appendKnown(knownHeaders,
                                    XHttpHeaders_WellKnownHeader_ContentType, knownValue));
    assert(XHttpHeaders_insertKnown(knownHeaders, 0,
                                    XHttpHeaders_WellKnownHeader_Host, knownDefault));
    assert(XHttpHeaders_replaceKnown(knownHeaders, 0,
                                     XHttpHeaders_WellKnownHeader_Host, knownValue));
    assert(XHttpHeaders_replaceOrAppendKnown(knownHeaders,
                                             XHttpHeaders_WellKnownHeader_ContentType,
                                             knownValue));
    assert(XHttpHeaders_containsKnown(knownHeaders,
                                      XHttpHeaders_WellKnownHeader_ContentType));
    result = XHttpHeaders_valueKnown(knownHeaders,
                                     XHttpHeaders_WellKnownHeader_ContentType);
    assert(xhttp_test_bytes_equal(result, "text/plain"));
    XClass_delete_base((XClass*)result);
    result = XHttpHeaders_valueKnownOr(knownHeaders,
                                       XHttpHeaders_WellKnownHeader_Authorization,
                                       knownDefault);
    assert(xhttp_test_bytes_equal(result, "default"));
    XClass_delete_base((XClass*)result);
    knownValues = XHttpHeaders_valuesKnown(knownHeaders,
                                           XHttpHeaders_WellKnownHeader_ContentType);
    assert(knownValues && XContainer_size_base((const XContainer*)knownValues) == 1);
    XHttpHeaders_values_free(knownValues);
    result = XHttpHeaders_combinedValueKnown(knownHeaders,
                                             XHttpHeaders_WellKnownHeader_ContentType);
    assert(xhttp_test_bytes_equal(result, "text/plain"));
    XClass_delete_base((XClass*)result);
    XHttpHeaders_removeAllKnown(knownHeaders, XHttpHeaders_WellKnownHeader_Host);
    assert(!XHttpHeaders_containsKnown(knownHeaders, XHttpHeaders_WellKnownHeader_Host));
    XClass_delete_base((XClass*)knownDefault);
    XClass_delete_base((XClass*)knownValue);
    XClass_delete_base((XClass*)knownHeaders);
}

static void xhttp_test_request(void)
{
    XHttpRequest* request = XHttpRequest_create();
    XHttpRequest* copy;
    XHttp1Configuration* http1;
    XHttp2Configuration* http2;
    XByteArray* wire;
    XByteArray* rawName;
    XByteArray* rawValue;
    XByteArray* userAgent;
    XVector* rawNames;
    assert(request);
    assert(XHttpRequest_http1Configuration_const(request));
    assert(XHttp1Configuration_numberOfConnectionsPerHost(
               XHttpRequest_http1Configuration_const(request)) == 6);
    http1 = XHttp1Configuration_create();
    http2 = XHttp2Configuration_create();
    assert(http1 && http2);
    assert(XHttp1Configuration_setNumberOfConnectionsPerHost(http1, 0));
    assert(XHttp1Configuration_numberOfConnectionsPerHost(http1) == 6);
    assert(XHttp1Configuration_setNumberOfConnectionsPerHost(http1, 300));
    assert(XHttp1Configuration_numberOfConnectionsPerHost(http1) == 255);
    XHttp2Configuration_setServerPushEnabled(http2, true);
    XHttp2Configuration_setHuffmanCompressionEnabled(http2, false);
    assert(XHttp2Configuration_setSessionReceiveWindowSize(http2, 1024));
    assert(!XHttp2Configuration_setSessionReceiveWindowSize(http2, 0));
    assert(!XHttp2Configuration_setSessionReceiveWindowSize(
               http2, XHttp2Configuration_MaxWindowSize + UINT32_C(1)));
    assert(XHttp2Configuration_setStreamReceiveWindowSize(http2, 2048));
    assert(!XHttp2Configuration_setMaxFrameSize(http2, XHttp2Configuration_MinFrameSize - 1));
    assert(XHttp2Configuration_setMaxFrameSize(http2, XHttp2Configuration_MaxFrameSize));
    assert(XHttpRequest_setHttp1Configuration(request, http1));
    assert(XHttpRequest_setHttp2Configuration(request, http2));
    copy = XHttpRequest_create_copy(request);
    assert(copy);
    assert(XHttp1Configuration_numberOfConnectionsPerHost(
               XHttpRequest_http1Configuration_const(copy)) == 255);
    assert(XHttp2Configuration_serverPushEnabled(
               XHttpRequest_http2Configuration_const(copy)));
    assert(!XHttp2Configuration_huffmanCompressionEnabled(
               XHttpRequest_http2Configuration_const(copy)));
    assert(XHttp2Configuration_sessionReceiveWindowSize(
               XHttpRequest_http2Configuration_const(copy)) == 1024);
    assert(XHttp2Configuration_maxFrameSize(
               XHttpRequest_http2Configuration_const(copy)) == XHttp2Configuration_MaxFrameSize);
    XClass_delete_base((XClass*)copy);
    XClass_delete_base((XClass*)http2);
    XClass_delete_base((XClass*)http1);
    assert(XHttpRequest_setUrl_utf8(request, "http://example.com/a?b=1"));
    assert(XHttpRequest_setMethod(request, XHttpRequest_Post));
    assert(XHttpRequest_setBody_utf8(request, "abc"));
    assert(XHttpRequest_setRawHeader(request, "Content-Type", "text/plain"));
    rawName = XByteArray_create_utf8("content-type");
    assert(rawName && XHttpRequest_hasRawHeader(request, rawName));
    rawValue = XHttpRequest_rawHeader(request, rawName);
    rawNames = XHttpRequest_rawHeaderList(request);
    assert(rawValue && xhttp_test_bytes_equal(rawValue, "text/plain") && rawNames &&
           XContainer_size_base((const XContainer*)rawNames) >= 1);
    XHttpHeaders_values_free(rawNames);
    XClass_delete_base((XClass*)rawValue);
    XClass_delete_base((XClass*)rawName);
    userAgent = XByteArray_create_utf8("XinYueC");
    assert(userAgent && XHttpRequest_setHeaderKnown(request,
        XHttpHeaders_WellKnownHeader_UserAgent, userAgent));
    XClass_delete_base((XClass*)userAgent);
    rawValue = XHttpRequest_headerKnown(request, XHttpHeaders_WellKnownHeader_UserAgent);
    assert(rawValue && xhttp_test_bytes_equal(rawValue, "XinYueC"));
    XClass_delete_base((XClass*)rawValue);
    wire = XHttpRequest_toHttp1(request, false);
    assert(wire);
    assert(xhttp_test_bytes_contains(wire, "POST /a?b=1 HTTP/1.1\r\n"));
    assert(xhttp_test_bytes_contains(wire, "Host: example.com\r\n"));
    assert(xhttp_test_bytes_contains(wire, "Content-Length: 3\r\n"));
    assert(xhttp_test_bytes_contains(wire, "\r\n\r\nabc"));
    XClass_delete_base((XClass*)wire);
    assert(XHttpRequest_setCustomMethod(request, "MKCOL"));
    wire = XHttpRequest_toHttp1(request, false);
    assert(wire && memcmp(XByteArray_constData(wire), "MKCOL ", 6) == 0);
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)request);
}

static void xhttp_test_multipart(void)
{
    XHttpPart* part;
    XHttpPart* copy;
    XHttpMultiPart* multipart;
    XByteArray* body;
    XByteArray* contentType;
    XByteArray* boundary;
    part = XHttpPart_create();
    multipart = XHttpMultiPart_create_type(XHttpMultiPart_FormDataType);
    assert(part && multipart);
    assert(XHttpPart_setRawHeader_utf8(part, "Content-Disposition", "form-data; name=\"field\""));
    assert(XHttpPart_setBody_utf8(part, "value"));
    copy = XHttpPart_create_copy(part);
    assert(copy && XHttpPart_setBody_utf8(copy, "copy-value"));
    assert(XHttpMultiPart_append(multipart, part));
    assert(XHttpMultiPart_append(multipart, copy));
    boundary = XByteArray_create_utf8("test-boundary");
    assert(boundary && XHttpMultiPart_setBoundary(multipart, boundary));
    body = XHttpMultiPart_toByteArray(multipart);
    contentType = XHttpMultiPart_contentType(multipart);
    assert(body && contentType);
    assert(xhttp_test_bytes_contains(body, "--test-boundary\r\n"));
    assert(xhttp_test_bytes_contains(body, "content-disposition: form-data; name=\"field\"\r\n\r\nvalue\r\n"));
    assert(xhttp_test_bytes_contains(body, "copy-value\r\n--test-boundary--\r\n"));
    assert(xhttp_test_bytes_equal(contentType, "multipart/form-data; boundary=\"test-boundary\""));
    XClass_delete_base((XClass*)contentType);
    XClass_delete_base((XClass*)body);
    XClass_delete_base((XClass*)boundary);
    XClass_delete_base((XClass*)copy);
    XClass_delete_base((XClass*)part);
    XClass_delete_base((XClass*)multipart);
}

static void xhttp_test_cookie(void)
{
    XNetworkCookieJar* jar;
    XByteArray* setCookie;
    XVector* parsed;
    XHttpRequest* request;
    XByteArray* header;
    jar = XNetworkCookieJar_create();
    setCookie = XByteArray_create_utf8("sid=abc; Path=/api; HttpOnly; Max-Age=60");
    request = XHttpRequest_create();
    assert(jar && setCookie && request);
    assert(XHttpRequest_setUrl_utf8(request, "http://example.com/api/item"));
    parsed = XVector_create(sizeof(XByteArray*));
    assert(parsed && XVector_push_back_1_base(parsed, &setCookie));
    assert(XNetworkCookieJar_setCookiesFromUrl(jar, parsed, XHttpRequest_url_const(request)));
    header = XNetworkCookieJar_cookieHeader(jar, XHttpRequest_url_const(request));
    assert(header && xhttp_test_bytes_equal(header, "sid=abc"));
    XClass_delete_base((XClass*)header);
    XClass_delete_base((XClass*)parsed);
    XClass_delete_base((XClass*)setCookie);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)jar);
}

static void xhttp_test_content_length_reply(void)
{
    XHttpRequest* request = XHttpRequest_create();
    XHttpReply* reply;
    const XByteArray* body;
    XByteArray* rawName;
    XByteArray* rawValue;
    XVector* rawNames;
    XHttpHeaders* copiedHeaders;
    assert(request);
    reply = XHttpReply_create(request);
    assert(reply);
    static const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-A: one\r\n\r\nhe";
    assert(XHttpReply_feed(reply, response, sizeof(response) - 1));
    assert(!XHttpReply_isFinished(reply));
    assert(XHttpReply_feed(reply, "llo", 3));
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_statusCode(reply) == 200);
    body = XHttpReply_body_const(reply);
    assert(xhttp_test_bytes_equal(body, "hello"));
    assert(xhttp_test_has_header(XHttpReply_headers_const(reply), "x-a"));
    rawName = XByteArray_create_utf8("X-A");
    rawValue = XHttpReply_rawHeader(reply, rawName);
    rawNames = XHttpReply_rawHeaderList(reply);
    copiedHeaders = XHttpReply_headers(reply);
    assert(rawName);
    assert(rawValue);
    assert(xhttp_test_bytes_equal(rawValue, "one"));
    assert(rawNames);
    assert(copiedHeaders);
    assert(XHttpHeaders_size(copiedHeaders) == 2);
    XHttpHeaders_values_free(rawNames);
    XClass_delete_base((XClass*)copiedHeaders);
    XClass_delete_base((XClass*)rawValue);
    XClass_delete_base((XClass*)rawName);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
}

static void xhttp_test_chunked_reply(void)
{
    XHttpReply* reply = XHttpReply_create(NULL);
    static const char response[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4\r\nWiki\r\n5\r\npedia\r\n0\r\nX-Trailer: yes\r\n\r\n";
    assert(reply);
    for (size_t i = 0; i < sizeof(response) - 1; ++i)
        assert(XHttpReply_feed(reply, response + i, 1));
    assert(XHttpReply_isFinished(reply));
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "Wikipedia"));
    assert(xhttp_test_has_header(XHttpReply_trailers_const(reply), "x-trailer"));
    XClass_delete_base((XClass*)reply);
}

static void xhttp_test_close_delimited_reply(void)
{
    XHttpReply* reply = XHttpReply_create(NULL);
    assert(reply);
    static const char response[] = "HTTP/1.0 200 OK\r\n\r\nbody";
    assert(XHttpReply_feed(reply, response, sizeof(response) - 1));
    assert(!XHttpReply_isFinished(reply));
    assert(XHttpReply_endOfInput(reply));
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "body"));
    XClass_delete_base((XClass*)reply);
}

static void xhttp_test_invalid_reply(void)
{
    XHttpReply* reply = XHttpReply_create(NULL);
    assert(reply);
    static const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n";
    assert(!XHttpReply_feed(reply, response, sizeof(response) - 1));
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_error(reply) == XHttpReply_ProtocolInvalidOperationError);
    XClass_delete_base((XClass*)reply);
}

static void xhttp_test_hsts(void)
{
    XNetworkAccessManager* manager = XNetworkAccessManager_create();
    XNetworkAccessManager* reopened = XNetworkAccessManager_create();
    XHstsPolicy* policy = NULL;
    XHstsPolicy* copy = NULL;
    XByteArray* host = XByteArray_create_utf8("example.com");
    XVector* policies = XVector_create(sizeof(XHstsPolicy*));
    XVector* current = NULL;
    XByteArray* directory = XByteArray_create_utf8("/tmp/xhttp_hsts_alignment_v2");
    assert(manager && reopened && host && policies && directory);
    policy = XHstsPolicy_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, host, INT64_MAX, XHstsPolicy_IncludeSubDomains);
    assert(policy);
    assert(!XHstsPolicy_isExpired(policy));
    assert(XHstsPolicy_includesSubDomains(policy));
    copy = XHstsPolicy_create_copy(policy);
    assert(copy && XHstsPolicy_equals(policy, copy));
    assert(XVector_push_back_1_base(policies, &policy));
    assert(XNetworkAccessManager_addStrictTransportSecurityHosts(manager, policies));
    XNetworkAccessManager_setStrictTransportSecurityEnabled(manager, true);
    assert(XNetworkAccessManager_isStrictTransportSecurityEnabled(manager));
    current = XNetworkAccessManager_strictTransportSecurityHosts(manager);
    assert(current && XContainer_size_base((const XContainer*)current) == 1);
    XHstsPolicy_list_free(current);
    assert(XNetworkAccessManager_enableStrictTransportSecurityStore(manager, true, directory));
    assert(XNetworkAccessManager_isStrictTransportSecurityStoreEnabled(manager));
    assert(XNetworkAccessManager_addStrictTransportSecurityHosts(manager, policies));
    assert(XNetworkAccessManager_enableStrictTransportSecurityStore(reopened, true, directory));
    XNetworkAccessManager_setStrictTransportSecurityEnabled(reopened, true);
    current = XNetworkAccessManager_strictTransportSecurityHosts(reopened);
    assert(current && XContainer_size_base((const XContainer*)current) == 1);
    XHstsPolicy_list_free(current);
    XClass_delete_base((XClass*)copy);
    XClass_delete_base((XClass*)policy);
    XClass_delete_base((XClass*)policies);
    XClass_delete_base((XClass*)host);
    XClass_delete_base((XClass*)directory);
    XClass_delete_base((XClass*)reopened);
    XClass_delete_base((XClass*)manager);
}

static void xhttp_test_cache(void)
{
    XNetworkDiskCache* cache = XNetworkDiskCache_create();
    XNetworkDiskCache* reopened = XNetworkDiskCache_create();
    XNetworkCacheMetaData* metadata = XNetworkCacheMetaData_create();
    XHttpRequest* request = XHttpRequest_create();
    XByteArray* body = XByteArray_create_utf8("cached");
    XString* directory = XString_create_utf8("/tmp/xhttp_cache_alignment");
    XByteArray* result;
    assert(cache && reopened && metadata && request && body && directory);
    assert(XNetworkDiskCache_setCacheDirectory(cache, directory));
    XNetworkDiskCache_clear(cache);
    assert(XHttpRequest_setUrl_utf8(request, "http://example.com/cache"));
    assert(XNetworkCacheMetaData_setUrl(metadata, XHttpRequest_url_const(request)));
    assert(XNetworkCacheMetaData_isValid(metadata));
    XNetworkCacheMetaData_setSaveToDisk(metadata, true);
    assert(XNetworkCacheMetaData_saveToDisk(metadata));
    assert(XNetworkDiskCache_insert(cache, metadata, body));
    assert(XNetworkDiskCache_cacheSize(cache) == 6);
    result = XNetworkDiskCache_data(cache, XHttpRequest_url_const(request));
    assert(result && xhttp_test_bytes_equal(result, "cached"));
    XClass_delete_base((XClass*)result);
    assert(XNetworkDiskCache_setCacheDirectory(reopened, directory));
    result = XNetworkDiskCache_data(reopened, XHttpRequest_url_const(request));
    assert(result && xhttp_test_bytes_equal(result, "cached"));
    XClass_delete_base((XClass*)result);
    XNetworkDiskCache_setMaximumCacheSize(cache, 3);
    assert(XNetworkDiskCache_cacheSize(cache) == 0);
    assert(!XNetworkDiskCache_remove(cache, XHttpRequest_url_const(request)));
    XClass_delete_base((XClass*)body);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)metadata);
    XClass_delete_base((XClass*)directory);
    XClass_delete_base((XClass*)reopened);
    XClass_delete_base((XClass*)cache);
}

static void xhttp_test_http2_frame(void)
{
    XByteArray* payload = XByteArray_create_utf8("abc");
    XHttp2Frame* frame;
    XHttp2Frame* decoded;
    XByteArray* encoded;
    size_t consumed = 0;
    assert(payload);
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Data, XHttp2Frame_EndStream, 1, payload);
    assert(frame);
    encoded = XHttp2Frame_toByteArray(frame);
    assert(encoded && XContainer_size_base((const XContainer*)encoded) == 12);
    decoded = XHttp2Frame_fromBytes(XByteArray_constData(encoded),
                                    XContainer_size_base((const XContainer*)encoded), &consumed);
    assert(decoded && consumed == 12);
    assert(XHttp2Frame_type(decoded) == XHttp2Frame_Data);
    assert(XHttp2Frame_flags(decoded) == XHttp2Frame_EndStream);
    assert(XHttp2Frame_streamId(decoded) == 1);
    assert(xhttp_test_bytes_equal(XHttp2Frame_payload_const(decoded), "abc"));
    assert(XHttp2Frame_hasClientPreface(XHttp2Frame_ClientPreface,
                                        sizeof(XHttp2Frame_ClientPreface) - 1));
    XClass_delete_base((XClass*)decoded);
    XClass_delete_base((XClass*)encoded);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)payload);
}

static void xhttp_test_http2_frame_validation(void)
{
    /* Qt Http2::Frame::validateHeader/validatePayload 对应的固定长度与布局用例。 */
    const uint8_t settingsAckWithPayload[] = {0, 0, 1, XHttp2Frame_Settings,
                                               XHttp2Frame_Ack, 0, 0, 0, 0, 0};
    const uint8_t shortPriority[] = {0, 0, 4, XHttp2Frame_Priority,
                                     0, 0, 0, 0, 1, 0, 0, 0, 0};
    const uint8_t badPaddedData[] = {0, 0, 3, XHttp2Frame_Data,
                                     XHttp2Frame_Padded, 0, 0, 0, 1, 3, 0, 0};
    const uint8_t shortPriorityHeaders[] = {0, 0, 4, XHttp2Frame_Headers,
                                            XHttp2Frame_PriorityFlag, 0, 0, 0, 1,
                                            0, 0, 0, 0};
    XByteArray* payload = XByteArray_create_utf8("x");
    XHttp2Frame* frame;
    size_t consumed = 0;
    assert(payload);
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Settings, XHttp2Frame_Ack, 0, payload);
    assert(frame && !XHttp2Frame_validateHeader(frame) &&
           !XHttp2Frame_toByteArray(frame));
    XClass_delete_base((XClass*)frame);
    assert(!XHttp2Frame_fromBytes(settingsAckWithPayload, sizeof(settingsAckWithPayload), &consumed));
    assert(consumed == 0);
    assert(!XHttp2Frame_fromBytes(shortPriority, sizeof(shortPriority), &consumed));
    assert(consumed == 0);
    assert(!XHttp2Frame_fromBytes(badPaddedData, sizeof(badPaddedData), &consumed));
    assert(consumed == 0);
    assert(!XHttp2Frame_fromBytes(shortPriorityHeaders, sizeof(shortPriorityHeaders), &consumed));
    assert(consumed == 0);
    XClass_delete_base((XClass*)payload);
}

static void xhttp_test_http2_headers(void)
{
    XHttp2HeaderList* headers = XHttp2HeaderList_create();
    XHttp2HeaderList* decoded;
    XByteArray* method = XByteArray_create_utf8(":method");
    XByteArray* get = XByteArray_create_utf8("GET");
    XByteArray* type = XByteArray_create_utf8("content-type");
    XByteArray* text = XByteArray_create_utf8("text/plain");
    XByteArray* encoded;
    assert(headers && method && get && type && text);
    assert(XHttp2HeaderList_append(headers, method, get));
    assert(XHttp2HeaderList_append(headers, type, text));
    encoded = XHttp2HeaderList_encode(headers, false);
    assert(encoded);
    decoded = XHttp2HeaderList_decode(XByteArray_constData(encoded),
                                      XContainer_size_base((const XContainer*)encoded));
    assert(decoded && XHttp2HeaderList_size(decoded) == 2);
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_nameAt_const(decoded, 0), ":method"));
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_valueAt_const(decoded, 0), "GET"));
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_nameAt_const(decoded, 1), "content-type"));
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_valueAt_const(decoded, 1), "text/plain"));
    XClass_delete_base((XClass*)decoded);
    XClass_delete_base((XClass*)encoded);
    encoded = XHttp2HeaderList_encode(headers, true);
    assert(encoded);
    decoded = XHttp2HeaderList_decode(XByteArray_constData(encoded),
                                      XContainer_size_base((const XContainer*)encoded));
    assert(decoded && XHttp2HeaderList_size(decoded) == 2);
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_valueAt_const(decoded, 1), "text/plain"));
    XClass_delete_base((XClass*)decoded);
    XClass_delete_base((XClass*)encoded);
    {
        static const uint8_t dynamicBlock[] = {
            0x40, 0x03, 'f', 'o', 'o', 0x03, 'b', 'a', 'r', 0xbe
        };
        decoded = XHttp2HeaderList_decode(dynamicBlock, sizeof(dynamicBlock));
        assert(decoded && XHttp2HeaderList_size(decoded) == 2);
        assert(xhttp_test_bytes_equal(XHttp2HeaderList_nameAt_const(decoded, 1), "foo"));
        assert(xhttp_test_bytes_equal(XHttp2HeaderList_valueAt_const(decoded, 1), "bar"));
        XClass_delete_base((XClass*)decoded);
    }
    XClass_delete_base((XClass*)text);
    XClass_delete_base((XClass*)type);
    XClass_delete_base((XClass*)get);
    XClass_delete_base((XClass*)method);
    XClass_delete_base((XClass*)headers);
}

static void xhttp_test_http2_stateful_decoder(void)
{
    static const uint8_t firstBlock[] = {
        0x40, 0x09, 'x', '-', 'd', 'y', 'n', 'a', 'm', 'i', 'c',
        0x03, 'o', 'n', 'e'
    };
    static const uint8_t dynamicIndexBlock[] = { 0xbe };
    XHttp2HeaderDecoder* decoder = XHttp2HeaderDecoder_create();
    XHttp2HeaderList* first;
    XHttp2HeaderList* second;
    assert(decoder);
    assert(XHttp2HeaderDecoder_setMaxDynamicTableSize(decoder, 4096));
    first = XHttp2HeaderDecoder_decode(decoder, firstBlock, sizeof(firstBlock));
    assert(first && XHttp2HeaderList_size(first) == 1);
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_nameAt_const(first, 0), "x-dynamic"));
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_valueAt_const(first, 0), "one"));
    second = XHttp2HeaderDecoder_decode(decoder, dynamicIndexBlock,
                                         sizeof(dynamicIndexBlock));
    assert(second && XHttp2HeaderList_size(second) == 1);
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_nameAt_const(second, 0), "x-dynamic"));
    assert(xhttp_test_bytes_equal(XHttp2HeaderList_valueAt_const(second, 0), "one"));
    XClass_delete_base((XClass*)second);
    XClass_delete_base((XClass*)first);
    XClass_delete_base((XClass*)decoder);
}

static void xhttp_test_http2_stateful_encoder(void)
{
    XHttp2HeaderEncoder* encoder = XHttp2HeaderEncoder_create();
    XHttp2HeaderDecoder* decoder = XHttp2HeaderDecoder_create();
    XHttp2HeaderList* headers = XHttp2HeaderList_create();
    XByteArray* name = XByteArray_create_utf8("x-dynamic");
    XByteArray* value = XByteArray_create_utf8("one");
    XByteArray* first;
    XByteArray* second;
    XByteArray* resized;
    XHttp2HeaderList* decoded;
    assert(encoder && decoder && headers && name && value);
    assert(XHttp2HeaderList_append(headers, name, value));
    first = XHttp2HeaderEncoder_encode(encoder, headers, false);
    assert(first);
    decoded = XHttp2HeaderDecoder_decode(decoder, XByteArray_constData(first),
                                          XContainer_size_base((const XContainer*)first));
    assert(decoded && xhttp_test_bytes_equal(XHttp2HeaderList_nameAt_const(decoded, 0),
                                             "x-dynamic"));
    XClass_delete_base((XClass*)decoded);
    second = XHttp2HeaderEncoder_encode(encoder, headers, false);
    assert(second && XContainer_size_base((const XContainer*)second) == 1 &&
           XByteArray_constData(second)[0] == 0xbe);
    decoded = XHttp2HeaderDecoder_decode(decoder, XByteArray_constData(second),
                                          XContainer_size_base((const XContainer*)second));
    assert(decoded && xhttp_test_bytes_equal(XHttp2HeaderList_valueAt_const(decoded, 0), "one"));
    XClass_delete_base((XClass*)decoded);
    assert(XHttp2HeaderEncoder_setMaxDynamicTableSize(encoder, 0));
    assert(XHttp2HeaderDecoder_setMaxDynamicTableSize(decoder, 0));
    resized = XHttp2HeaderEncoder_encode(encoder, headers, false);
    assert(resized && XContainer_size_base((const XContainer*)resized) > 1 &&
           (XByteArray_constData(resized)[0] & 0xe0) == 0x20);
    decoded = XHttp2HeaderDecoder_decode(decoder, XByteArray_constData(resized),
                                          XContainer_size_base((const XContainer*)resized));
    assert(decoded && XHttp2HeaderList_size(decoded) == 1);
    XClass_delete_base((XClass*)decoded);
    XClass_delete_base((XClass*)resized);
    XClass_delete_base((XClass*)second);
    XClass_delete_base((XClass*)first);
    XClass_delete_base((XClass*)value);
    XClass_delete_base((XClass*)name);
    XClass_delete_base((XClass*)headers);
    XClass_delete_base((XClass*)decoder);
    XClass_delete_base((XClass*)encoder);
}

static void xhttp_test_http2_peer_header_limit(void)
{
    XHttp2ClientSession* session = XHttp2ClientSession_create();
    XHttpRequest* request = XHttpRequest_create();
    XByteArray* wire;
    uint32_t streamId = 0;
    assert(session && request);
    assert(XHttpRequest_setUrl_utf8(request, "https://example.com/limited"));
    assert(XHttp2ClientSession_setPeerMaxHeaderListSize(session, 1));
    wire = XHttp2ClientSession_encodeRequest(session, request, &streamId);
    assert(!wire && streamId == 0);
    assert(XHttp2ClientSession_setPeerMaxHeaderListSize(session, SIZE_MAX));
    wire = XHttp2ClientSession_encodeRequest(session, request, &streamId);
    assert(wire && streamId == 1);
    XClass_delete_base((XClass*)wire);
    assert(XHttp2ClientSession_activeStreamCount(session) == 1);
    assert(XHttp2ClientSession_setPeerMaxConcurrentStreams(session, 1));
    streamId = 0;
    wire = XHttp2ClientSession_encodeRequest(session, request, &streamId);
    assert(!wire && streamId == 0);
    assert(XHttp2ClientSession_markStreamClosed(session));
    wire = XHttp2ClientSession_encodeRequest(session, request, &streamId);
    assert(wire && streamId == 3 && XHttp2ClientSession_activeStreamCount(session) == 1);
    XClass_delete_base((XClass*)wire);
    assert(XHttp2ClientSession_markStreamClosed(session));
    /* 最高合法奇数流号 0x7fffffff 分配后，保留位不能被用于下一条流。 */
    session->m_nextStreamId = UINT32_C(0x7fffffff);
    assert(XHttp2ClientSession_nextStreamId(session) == UINT32_C(0x7fffffff));
    assert(XHttp2ClientSession_nextStreamId(session) == 0);
    XHttp2ClientSession_setGoingAway(session);
    assert(XHttp2ClientSession_isGoingAway(session));
    streamId = 0;
    assert(!XHttp2ClientSession_encodeRequest(session, request, &streamId));
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)session);
}

static void xhttp_test_http2_connection_multiplex(void)
{
    XHttp2Connection* connection = XHttp2Connection_create();
    XHttpRequest* first = XHttpRequest_create();
    XHttpRequest* second = XHttpRequest_create();
    XHttpReply* firstReply;
    XHttpReply* secondReply;
    XByteArray* outgoing;
    XByteArray* payload = XByteArray_create_utf8("\x88");
    XHttp2Frame* firstFrame;
    XHttp2Frame* secondFrame;
    XByteArray* firstWire;
    XByteArray* secondWire;
    XByteArray* input;
    uint32_t firstId = 0;
    uint32_t secondId = 0;
    assert(connection && first && second && payload);
    assert(XHttpRequest_setUrl_utf8(first, "https://example.com/first"));
    assert(XHttpRequest_setUrl_utf8(second, "https://example.com/second"));
    firstReply = XHttp2Connection_sendRequest(connection, first, &firstId);
    secondReply = XHttp2Connection_sendRequest(connection, second, &secondId);
    assert(firstReply && secondReply && firstId == 1 && secondId == 3);
    outgoing = XHttp2Connection_takeOutgoing(connection);
    assert(outgoing && XHttp2Frame_hasClientPreface(XByteArray_constData(outgoing),
                                                     XContainer_size_base((const XContainer*)outgoing)));
    XClass_delete_base((XClass*)outgoing);
    firstFrame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Headers,
        XHttp2Frame_EndHeaders | XHttp2Frame_EndStream, firstId, payload);
    secondFrame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Headers,
        XHttp2Frame_EndHeaders | XHttp2Frame_EndStream, secondId, payload);
    firstWire = firstFrame ? XHttp2Frame_toByteArray(firstFrame) : NULL;
    secondWire = secondFrame ? XHttp2Frame_toByteArray(secondFrame) : NULL;
    input = XByteArray_create();
    assert(firstFrame && secondFrame && firstWire && secondWire && input &&
           XByteArray_push_back_2((XVector*)input, XByteArray_constData(secondWire),
                                  XContainer_size_base((const XContainer*)secondWire)) &&
           XByteArray_push_back_2((XVector*)input, XByteArray_constData(firstWire),
                                  XContainer_size_base((const XContainer*)firstWire)));
    assert(XHttp2Connection_feed(connection, XByteArray_constData(input),
                                  XContainer_size_base((const XContainer*)input)));
    assert(XHttpReply_isFinished(firstReply) && XHttpReply_isFinished(secondReply));
    assert(XHttpReply_statusCode(firstReply) == 200 && XHttpReply_statusCode(secondReply) == 200);
    assert(XHttp2Connection_streamCount(connection) == 2);
    XClass_delete_base((XClass*)input);
    XClass_delete_base((XClass*)secondWire);
    XClass_delete_base((XClass*)firstWire);
    XClass_delete_base((XClass*)secondFrame);
    XClass_delete_base((XClass*)firstFrame);
    XClass_delete_base((XClass*)payload);
    XClass_delete_base((XClass*)second);
    XClass_delete_base((XClass*)first);
    XClass_delete_base((XClass*)connection);
}

static void xhttp_test_http2_connection_initial_window(void)
{
    XHttp2Configuration* configuration = XHttp2Configuration_create();
    XHttp2Connection* connection;
    XHttpRequest* request = XHttpRequest_create();
    XByteArray* outgoing;
    XHttp2Frame* settings;
    XHttp2Frame* update;
    const uint8_t* payload;
    size_t consumed = 0;
    bool foundStreamWindow = false;
    uint32_t streamId = 0;
    assert(configuration && request);
    assert(XHttp2Configuration_setSessionReceiveWindowSize(configuration, 100000));
    assert(XHttp2Configuration_setStreamReceiveWindowSize(configuration, 90000));
    connection = XHttp2Connection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, configuration);
    assert(connection && XHttpRequest_setUrl_utf8(request, "https://example.com/window") &&
           XHttp2Connection_sendRequest(connection, request, &streamId) && streamId == 1);
    outgoing = XHttp2Connection_takeOutgoing(connection);
    assert(outgoing && XHttp2Frame_hasClientPreface(XByteArray_constData(outgoing),
                                                     XContainer_size_base((const XContainer*)outgoing)));
    settings = XHttp2Frame_fromBytes(XByteArray_constData(outgoing) +
                                     sizeof(XHttp2Frame_ClientPreface) - 1,
                                     XContainer_size_base((const XContainer*)outgoing) -
                                     (sizeof(XHttp2Frame_ClientPreface) - 1), &consumed);
    assert(settings && XHttp2Frame_type(settings) == XHttp2Frame_Settings);
    payload = XByteArray_constData((XByteArray*)XHttp2Frame_payload_const(settings));
    for (size_t offset = 0; payload && offset <
         XContainer_size_base((const XContainer*)XHttp2Frame_payload_const(settings)); offset += 6) {
        uint16_t id = (uint16_t)(((uint16_t)payload[offset] << 8) | payload[offset + 1]);
        uint32_t value = ((uint32_t)payload[offset + 2] << 24) |
                         ((uint32_t)payload[offset + 3] << 16) |
                         ((uint32_t)payload[offset + 4] << 8) | payload[offset + 5];
        if (id == 4 && value == 90000)
            foundStreamWindow = true;
    }
    assert(foundStreamWindow);
    update = XHttp2Frame_fromBytes(XByteArray_constData(outgoing) +
                                   sizeof(XHttp2Frame_ClientPreface) - 1 + consumed,
                                   XContainer_size_base((const XContainer*)outgoing) -
                                   (sizeof(XHttp2Frame_ClientPreface) - 1 + consumed), &consumed);
    assert(update && XHttp2Frame_type(update) == XHttp2Frame_WindowUpdate &&
           XHttp2Frame_streamId(update) == 0 &&
           XContainer_size_base((const XContainer*)XHttp2Frame_payload_const(update)) == 4);
    payload = XByteArray_constData((XByteArray*)XHttp2Frame_payload_const(update));
    assert(((uint32_t)payload[0] << 24 | (uint32_t)payload[1] << 16 |
            (uint32_t)payload[2] << 8 | payload[3]) == 34465);
    XClass_delete_base((XClass*)update);
    XClass_delete_base((XClass*)settings);
    XClass_delete_base((XClass*)outgoing);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)connection);
    XClass_delete_base((XClass*)configuration);
}

static void xhttp_test_http2_connection_h2c_upgrade(void)
{
    XHttp2Connection* connection = XHttp2Connection_create();
    XHttpRequest* request = XHttpRequest_create();
    XHttpReply* reply;
    XByteArray* outgoing;
    XHttp2Frame* settings;
    XByteArray* headerPayload = XByteArray_create_utf8("\x88");
    XByteArray* dataPayload = XByteArray_create_utf8("upgraded");
    XHttp2Frame* headers;
    XHttp2Frame* data;
    XByteArray* headerWire;
    XByteArray* dataWire;
    XByteArray* input;
    size_t consumed = 0;
    assert(connection && request && headerPayload && dataPayload);
    assert(XHttpRequest_setUrl_utf8(request, "http://example.com/h2c"));
    reply = XHttpReply_create(request);
    assert(reply && XHttp2Connection_adoptUpgradedRequest(connection, reply));
    outgoing = XHttp2Connection_takeOutgoing(connection);
    assert(outgoing && XHttp2Frame_hasClientPreface(XByteArray_constData(outgoing),
                                                     XContainer_size_base((const XContainer*)outgoing)));
    settings = XHttp2Frame_fromBytes(XByteArray_constData(outgoing) +
                                     sizeof(XHttp2Frame_ClientPreface) - 1,
                                     XContainer_size_base((const XContainer*)outgoing) -
                                     (sizeof(XHttp2Frame_ClientPreface) - 1), &consumed);
    /* h2c 已通过 HTTP/1 发送原始请求，流 1 只发送前言和 SETTINGS，不能重复 HEADERS。 */
    assert(settings && XHttp2Frame_type(settings) == XHttp2Frame_Settings &&
           sizeof(XHttp2Frame_ClientPreface) - 1 + consumed ==
               XContainer_size_base((const XContainer*)outgoing));
    headers = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Headers, XHttp2Frame_EndHeaders,
                                    1, headerPayload);
    data = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Data, XHttp2Frame_EndStream, 1, dataPayload);
    headerWire = headers ? XHttp2Frame_toByteArray(headers) : NULL;
    dataWire = data ? XHttp2Frame_toByteArray(data) : NULL;
    input = XByteArray_create();
    assert(headers && data && headerWire && dataWire && input &&
           XByteArray_push_back_2((XVector*)input, XByteArray_constData(headerWire),
                                  XContainer_size_base((const XContainer*)headerWire)) &&
           XByteArray_push_back_2((XVector*)input, XByteArray_constData(dataWire),
                                  XContainer_size_base((const XContainer*)dataWire)) &&
           XHttp2Connection_feed(connection, XByteArray_constData(input),
                                 XContainer_size_base((const XContainer*)input)));
    assert(XHttpReply_isFinished(reply) && XHttpReply_statusCode(reply) == 200 &&
           xhttp_test_bytes_equal(XHttpReply_body_const(reply), "upgraded"));
    XClass_delete_base((XClass*)input);
    XClass_delete_base((XClass*)dataWire);
    XClass_delete_base((XClass*)headerWire);
    XClass_delete_base((XClass*)data);
    XClass_delete_base((XClass*)headers);
    XClass_delete_base((XClass*)dataPayload);
    XClass_delete_base((XClass*)headerPayload);
    XClass_delete_base((XClass*)settings);
    XClass_delete_base((XClass*)outgoing);
    XClass_delete_base((XClass*)connection);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
}

static void xhttp_test_http2_connection_protocol_limits(void)
{
    XHttp2Connection* connection;
    XByteArray* settingsPayload;
    XByteArray* pingPayload;
    XByteArray* invalidHeaderPayload;
    XHttp2Frame* settings;
    XHttp2Frame* ping;
    XHttp2Frame* invalidHeaders;
    XByteArray* wire;
    const uint8_t oversizedHeader[] = {
        0x00, 0x40, 0x01, XHttp2Frame_Data, 0x00, 0x00, 0x00, 0x00, 0x01
    };

    /* Qt QHttp2Connection::acceptSetting：服务端不能向客户端启用 PUSH。 */
    connection = XHttp2Connection_create();
    settingsPayload = XByteArray_create_with_data("\0\2\0\0\0\1", 6);
    settings = settingsPayload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Settings, 0, 0,
                                                        settingsPayload) : NULL;
    wire = settings ? XHttp2Frame_toByteArray(settings) : NULL;
    assert(connection && settingsPayload && settings && wire &&
           !XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                  XContainer_size_base((const XContainer*)wire)) &&
           XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)settings);
    XClass_delete_base((XClass*)settingsPayload);
    XClass_delete_base((XClass*)connection);

    /* Qt 将未主动发送 PING 时收到的 ACK 作为诊断状态，不关闭连接。 */
    connection = XHttp2Connection_create();
    pingPayload = XByteArray_create_with_data("12345678", 8);
    ping = pingPayload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Ping, XHttp2Frame_Ack, 0,
                                                pingPayload) : NULL;
    wire = ping ? XHttp2Frame_toByteArray(ping) : NULL;
    assert(connection && pingPayload && ping && wire &&
           XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                 XContainer_size_base((const XContainer*)wire)) &&
           !XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)ping);
    XClass_delete_base((XClass*)pingPayload);
    XClass_delete_base((XClass*)connection);

    /* 即使只收到 9 字节帧头，也要拒绝超过本端 SETTINGS_MAX_FRAME_SIZE 的载荷。 */
    connection = XHttp2Connection_create();
    assert(connection && !XHttp2Connection_feed(connection, oversizedHeader,
                                                  sizeof(oversizedHeader)) &&
           XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)connection);

    /* HPACK 解码失败必须发送连接级 GOAWAY(COMPRESSION_ERROR)，不能静默停在半包状态。 */
    {
        XHttpRequest* request = XHttpRequest_create();
        XByteArray* outgoing;
        XHttp2Frame* goaway;
        const uint8_t* goawayPayload;
        uint32_t streamId = 0;
        connection = XHttp2Connection_create();
        invalidHeaderPayload = XByteArray_create_with_data("\xff", 1);
        invalidHeaders = invalidHeaderPayload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Headers, XHttp2Frame_EndHeaders, 1, invalidHeaderPayload) : NULL;
        wire = invalidHeaders ? XHttp2Frame_toByteArray(invalidHeaders) : NULL;
        assert(connection && request && invalidHeaderPayload && invalidHeaders && wire &&
               XHttpRequest_setUrl_utf8(request, "https://example.com/hpack") &&
               XHttp2Connection_sendRequest(connection, request, &streamId) && streamId == 1);
        outgoing = XHttp2Connection_takeOutgoing(connection);
        assert(outgoing);
        XClass_delete_base((XClass*)outgoing);
        assert(!XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                      XContainer_size_base((const XContainer*)wire)) &&
               XHttp2Connection_isGoingAway(connection));
        outgoing = XHttp2Connection_takeOutgoing(connection);
        goaway = outgoing ? XHttp2Frame_fromBytes(XByteArray_constData(outgoing),
                                                   XContainer_size_base((const XContainer*)outgoing),
                                                   NULL) : NULL;
        goawayPayload = goaway ? XByteArray_constData(
            (XByteArray*)XHttp2Frame_payload_const(goaway)) : NULL;
        assert(outgoing && goaway && goawayPayload &&
               XHttp2Frame_type(goaway) == XHttp2Frame_GoAway &&
               goawayPayload[7] == 9);
        XClass_delete_base((XClass*)goaway);
        XClass_delete_base((XClass*)outgoing);
        XClass_delete_base((XClass*)wire);
        XClass_delete_base((XClass*)invalidHeaders);
        XClass_delete_base((XClass*)invalidHeaderPayload);
        XClass_delete_base((XClass*)request);
        XClass_delete_base((XClass*)connection);
    }
}

static void xhttp_test_http2_connection_detach_closed_reply(void)
{
    XHttp2Connection* connection = XHttp2Connection_create();
    XHttpRequest* firstRequest = XHttpRequest_create();
    XHttpRequest* secondRequest = XHttpRequest_create();
    XHttpReply* firstReply;
    XHttpReply* secondReply;
    XByteArray* payload = XByteArray_create_utf8("\x88");
    XHttp2Frame* response;
    XHttp2Frame* data;
    XHttp2Frame* update;
    XHttp2Frame* rst;
    XByteArray* controlPayload;
    XByteArray* latePayload;
    XByteArray* wire;
    uint32_t firstId = 0;
    uint32_t secondId = 0;
    assert(connection && firstRequest && secondRequest && payload);
    assert(XHttpRequest_setUrl_utf8(firstRequest, "https://example.com/first") &&
           XHttpRequest_setUrl_utf8(secondRequest, "https://example.com/second"));
    firstReply = XHttpReply_create(firstRequest);
    assert(firstReply && XHttp2Connection_sendRequestReply(connection, firstReply, &firstId) &&
           firstId == 1);
    wire = XHttp2Connection_takeOutgoing(connection);
    assert(wire);
    XClass_delete_base((XClass*)wire);
    response = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Headers,
                                     XHttp2Frame_EndHeaders | XHttp2Frame_EndStream,
                                     firstId, payload);
    wire = response ? XHttp2Frame_toByteArray(response) : NULL;
    assert(response && wire && XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                       XContainer_size_base((const XContainer*)wire)) &&
           XHttpReply_isFinished(firstReply) &&
           XHttp2Connection_detachReply(connection, firstId, firstReply) &&
           XHttp2Connection_streamCount(connection) == 0);
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)response);

    /* 已关闭流的 WINDOW_UPDATE/RST_STREAM 可忽略；DATA 必须以 STREAM_CLOSED 拒绝。 */
    controlPayload = XByteArray_create_with_data("\0\0\0\1", 4);
    update = controlPayload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_WindowUpdate, 0, firstId,
                                                     controlPayload) : NULL;
    wire = update ? XHttp2Frame_toByteArray(update) : NULL;
    assert(update && wire && XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                    XContainer_size_base((const XContainer*)wire)) &&
           !XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)update);
    XClass_delete_base((XClass*)controlPayload);
    latePayload = XByteArray_create_utf8("late");
    data = latePayload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Data, 0, firstId,
                                               latePayload) : NULL;
    wire = data ? XHttp2Frame_toByteArray(data) : NULL;
    assert(data && wire && XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                  XContainer_size_base((const XContainer*)wire)));
    XClass_delete_base((XClass*)wire);
    wire = XHttp2Connection_takeOutgoing(connection);
    rst = wire ? XHttp2Frame_fromBytes(XByteArray_constData(wire),
                                       XContainer_size_base((const XContainer*)wire), NULL) : NULL;
    assert(wire && rst && XHttp2Frame_type(rst) == XHttp2Frame_RstStream &&
           XHttp2Frame_streamId(rst) == firstId);
    XClass_delete_base((XClass*)rst);
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)data);
    XClass_delete_base((XClass*)latePayload);
    secondReply = XHttpReply_create(secondRequest);
    assert(secondReply && XHttp2Connection_sendRequestReply(connection, secondReply, &secondId) &&
           secondId == 3);
    XClass_delete_base((XClass*)connection);
    XClass_delete_base((XClass*)secondReply);
    XClass_delete_base((XClass*)firstReply);
    XClass_delete_base((XClass*)secondRequest);
    XClass_delete_base((XClass*)firstRequest);
    XClass_delete_base((XClass*)payload);
}

static void xhttp_test_http2_connection_idle_stream_errors(void)
{
    XHttp2Connection* connection = XHttp2Connection_create();
    XByteArray* payload = XByteArray_create_with_data("\0\0\0\1", 4);
    XHttp2Frame* update = payload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_WindowUpdate, 0, 1,
                                                           payload) : NULL;
    XByteArray* wire = update ? XHttp2Frame_toByteArray(update) : NULL;
    /* 对齐 Qt：未关联活动流的 WINDOW_UPDATE 按已关闭流忽略。 */
    assert(connection && payload && update && wire &&
           XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                 XContainer_size_base((const XContainer*)wire)) &&
           !XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)update);
    XClass_delete_base((XClass*)payload);
    XClass_delete_base((XClass*)connection);

    /* Qt 将没有对应主动 PING 的 ACK 作为诊断信息，不关闭连接。 */
    connection = XHttp2Connection_create();
    payload = XByteArray_create_with_data("\0\0\0\0\0\0\0\0", 8);
    update = payload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Ping, XHttp2Frame_Ack, 0, payload) : NULL;
    wire = update ? XHttp2Frame_toByteArray(update) : NULL;
    assert(connection && payload && update && wire &&
           XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                 XContainer_size_base((const XContainer*)wire)) &&
           !XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)update);
    XClass_delete_base((XClass*)payload);
    XClass_delete_base((XClass*)connection);

    /* 活动流收到零窗口增量时只发送 RST_STREAM，其他流和连接仍可继续。 */
    {
        XHttpRequest* request = XHttpRequest_create();
        XHttpReply* reply;
        XHttp2Frame* rst;
        XByteArray* outgoing;
        uint32_t streamId = 0;
        connection = XHttp2Connection_create();
        payload = XByteArray_create_with_data("\0\0\0\0", 4);
        update = payload ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_WindowUpdate, 0, 1, payload) : NULL;
        wire = update ? XHttp2Frame_toByteArray(update) : NULL;
        assert(connection && request && payload && update && wire &&
               XHttpRequest_setUrl_utf8(request, "https://example.com/window-error"));
        reply = XHttp2Connection_sendRequest(connection, request, &streamId);
        assert(reply && streamId == 1);
        outgoing = XHttp2Connection_takeOutgoing(connection);
        assert(outgoing);
        XClass_delete_base((XClass*)outgoing);
        assert(XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                     XContainer_size_base((const XContainer*)wire)) &&
               !XHttp2Connection_isGoingAway(connection) &&
               XHttpReply_error(reply) == XHttpReply_ProtocolInvalidOperationError);
        outgoing = XHttp2Connection_takeOutgoing(connection);
        rst = outgoing ? XHttp2Frame_fromBytes(XByteArray_constData(outgoing),
                                                XContainer_size_base((const XContainer*)outgoing),
                                                NULL) : NULL;
        assert(outgoing && rst && XHttp2Frame_type(rst) == XHttp2Frame_RstStream &&
               XHttp2Frame_streamId(rst) == streamId);
        XClass_delete_base((XClass*)rst);
        XClass_delete_base((XClass*)outgoing);
        XClass_delete_base((XClass*)wire);
        XClass_delete_base((XClass*)update);
        XClass_delete_base((XClass*)payload);
        XClass_delete_base((XClass*)request);
        XClass_delete_base((XClass*)connection);
    }
}

static void xhttp_test_authenticator_and_deferred_authentication(void)
{
    XByteArray* realm = XByteArray_create_utf8("restricted");
    XHttpAuthenticator* authenticator = XHttpAuthenticator_create();
    XHttpAuthenticator* copy;
    XHttpAuthenticator* moved;
    XNetworkAccessManager* manager = XNetworkAccessManager_create();
    XHttpRequest* request = XHttpRequest_create();
    XHttpReply* reply;
    assert(realm && authenticator && manager && request);
    assert(XHttpAuthenticator_setChallenge(authenticator, XHttpAuthenticator_Basic, realm));
    assert(XHttpAuthenticator_setUser_utf8(authenticator, "alice"));
    assert(XHttpAuthenticator_setPassword_utf8(authenticator, "secret"));
    assert(XHttpAuthenticator_hasCredentials(authenticator));
    copy = XHttpAuthenticator_create_copy(authenticator);
    moved = copy ? XHttpAuthenticator_create_move(copy) : NULL;
    assert(copy && moved && XHttpAuthenticator_method(moved) == XHttpAuthenticator_Basic &&
           xhttp_test_bytes_equal(XHttpAuthenticator_user_const(moved), "alice") &&
           xhttp_test_bytes_equal(XHttpAuthenticator_password_const(moved), "secret") &&
           XHttpAuthenticator_method(copy) == XHttpAuthenticator_None);
    assert(XHttpRequest_setUrl_utf8(request, "https://example.com/auth"));
    reply = XHttpReply_create(request);
    assert(reply);
    xhttp_test_authentication_signal_count = 0;
    assert(XObject_connect_2((XObject*)manager,
                             XSignal(XNetworkAccessManager_authenticationRequired_signal),
                             xhttp_test_authentication_required_slot));
    assert(XNetworkAccessManager_authenticationRequired_signal(manager, reply, authenticator));
    assert(xhttp_test_authentication_signal_count == 1 &&
           xhttp_test_bytes_equal(XHttpAuthenticator_user_const(authenticator), "user") &&
           xhttp_test_bytes_equal(XHttpAuthenticator_password_const(authenticator), "password"));

    XHttpReply_setDeferAuthentication(reply, true);
    assert(XHttpReply_feed(reply,
        "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"restricted\"\r\n"
        "Content-Length: 0\r\n\r\n",
        strlen("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"restricted\"\r\n"
               "Content-Length: 0\r\n\r\n")));
    assert(XHttpReply_isFinished(reply) && XHttpReply_authenticationPending(reply) &&
           XHttpReply_finishedSignalPending(reply) && XHttpReply_error(reply) == XHttpReply_NoError);
    assert(XHttpReply_finishAuthenticationChallenge(reply));
    assert(XHttpReply_error(reply) == XHttpReply_AuthenticationRequiredError &&
           XHttpReply_emitFinished(reply));

    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)manager);
    XClass_delete_base((XClass*)moved);
    XClass_delete_base((XClass*)copy);
    XClass_delete_base((XClass*)authenticator);
    XClass_delete_base((XClass*)realm);
}

/* 覆盖 Qt QHttp2Connection 对无效 PRIORITY、PUSH_PROMISE 编号和 RST_STREAM 的处理。 */
static void xhttp_test_http2_connection_qt_stream_state(void)
{
    XHttp2Configuration* configuration = XHttp2Configuration_create();
    XHttp2Connection* connection;
    XHttpRequest* request = XHttpRequest_create();
    XByteArray* priorityPayload = XByteArray_create_with_data("\0\0\0\0\20", 5);
    XByteArray* firstPromisePayload = XByteArray_create_with_data("\0\0\0\4\x82", 5);
    XByteArray* secondPromisePayload = XByteArray_create_with_data("\0\0\0\2\x82", 5);
    XByteArray* rstPayload = XByteArray_create_with_data("\0\0\0\0", 4);
    XHttp2Frame* frame;
    XByteArray* wire;
    XByteArray* outgoing;
    uint32_t streamId = 0;

    assert(configuration && request && priorityPayload && firstPromisePayload &&
           secondPromisePayload && rstPayload);
    XHttp2Configuration_setServerPushEnabled(configuration, true);
    assert(XHttpRequest_setUrl_utf8(request, "https://example.com/push-state"));

    connection = XHttp2Connection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, configuration);
    frame = connection ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Priority, 0, 1, priorityPayload) : NULL;
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    assert(connection && frame && wire &&
           !XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                  XContainer_size_base((const XContainer*)wire)) &&
           XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)connection);

    connection = XHttp2Connection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, configuration);
    assert(connection && XHttp2Connection_sendRequest(connection, request, &streamId) && streamId == 1);
    outgoing = XHttp2Connection_takeOutgoing(connection);
    assert(outgoing);
    XClass_delete_base((XClass*)outgoing);

    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_PushPromise, XHttp2Frame_EndHeaders,
                                  streamId, firstPromisePayload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    assert(frame && wire && XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                   XContainer_size_base((const XContainer*)wire)));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);

    /* 已接收服务端流 4 后，服务端不能再承诺更小的流 2。 */
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_PushPromise, XHttp2Frame_EndHeaders,
                                  streamId, secondPromisePayload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    assert(frame && wire && !XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                    XContainer_size_base((const XContainer*)wire)) &&
           XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)connection);

    /* 已见过服务端流 4 时，未记录的较小偶数 RST_STREAM 按关闭流忽略。 */
    connection = XHttp2Connection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, configuration);
    assert(connection && XHttp2Connection_sendRequest(connection, request, &streamId) && streamId == 1);
    outgoing = XHttp2Connection_takeOutgoing(connection);
    assert(outgoing);
    XClass_delete_base((XClass*)outgoing);
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_PushPromise, XHttp2Frame_EndHeaders,
                                  streamId, firstPromisePayload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    assert(frame && wire && XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                   XContainer_size_base((const XContainer*)wire)));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_RstStream, 0, 2, rstPayload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    assert(frame && wire && XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                   XContainer_size_base((const XContainer*)wire)) &&
           !XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)connection);
    XClass_delete_base((XClass*)rstPayload);
    XClass_delete_base((XClass*)secondPromisePayload);
    XClass_delete_base((XClass*)firstPromisePayload);
    XClass_delete_base((XClass*)priorityPayload);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)configuration);
}

static void xhttp_test_http2_connection_goaway(void)
{
    XHttp2Connection* connection = XHttp2Connection_create();
    XHttpRequest* first = XHttpRequest_create();
    XHttpRequest* second = XHttpRequest_create();
    XHttpReply* firstReply;
    XHttpReply* secondReply;
    XByteArray* invalidPayload = XByteArray_create_with_data("\0\0\0\5\0\0\0\0", 8);
    XByteArray* gracefulPayload = XByteArray_create_with_data("\0\0\0\1\0\0\0\0", 8);
    XHttp2Frame* frame;
    XByteArray* wire;
    XByteArray* outgoing;
    uint32_t firstId = 0;
    uint32_t secondId = 0;

    assert(connection && first && second && invalidPayload && gracefulPayload);
    assert(XHttpRequest_setUrl_utf8(first, "https://example.com/goaway/one"));
    assert(XHttpRequest_setUrl_utf8(second, "https://example.com/goaway/two"));
    firstReply = XHttp2Connection_sendRequest(connection, first, &firstId);
    assert(firstReply && firstId == 1);
    outgoing = XHttp2Connection_takeOutgoing(connection);
    assert(outgoing);
    XClass_delete_base((XClass*)outgoing);
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_GoAway, 0, 0, invalidPayload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    assert(frame && wire && !XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                    XContainer_size_base((const XContainer*)wire)) &&
           XHttp2Connection_isGoingAway(connection));
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)connection);

    connection = XHttp2Connection_create();
    assert(connection);
    firstReply = XHttp2Connection_sendRequest(connection, first, &firstId);
    secondReply = XHttp2Connection_sendRequest(connection, second, &secondId);
    assert(firstReply && secondReply && firstId == 1 && secondId == 3);
    outgoing = XHttp2Connection_takeOutgoing(connection);
    assert(outgoing);
    XClass_delete_base((XClass*)outgoing);
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_GoAway, 0, 0, gracefulPayload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    assert(frame && wire && XHttp2Connection_feed(connection, XByteArray_constData(wire),
                                                   XContainer_size_base((const XContainer*)wire)) &&
           XHttpReply_error(secondReply) == XHttpReply_ContentReSendError);
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)connection);
    XClass_delete_base((XClass*)gracefulPayload);
    XClass_delete_base((XClass*)invalidPayload);
    XClass_delete_base((XClass*)second);
    XClass_delete_base((XClass*)first);
}

static void xhttp_test_http2_connection_push_promise(void)
{
    XHttp2Configuration* configuration = XHttp2Configuration_create();
    XHttp2Connection* connection;
    XHttpRequest* request = XHttpRequest_create();
    XByteArray* promisePayload = XByteArray_create_with_data("\0\0\0\2\x82", 5);
    XByteArray* responsePayload = XByteArray_create_utf8("\x88");
    XHttp2Frame* promise;
    XHttp2Frame* response;
    XByteArray* promiseWire;
    XByteArray* responseWire;
    XByteArray* input;
    XByteArray* discarded;
    XHttpReply* pushed;
    uint32_t streamId = 0;
    assert(configuration && request && promisePayload && responsePayload);
    XHttp2Configuration_setServerPushEnabled(configuration, true);
    connection = XHttp2Connection_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, configuration);
    assert(connection && XHttpRequest_setUrl_utf8(request, "https://example.com/push") &&
           XHttp2Connection_sendRequest(connection, request, &streamId) && streamId == 1);
    discarded = XHttp2Connection_takeOutgoing(connection);
    assert(discarded);
    XClass_delete_base((XClass*)discarded);
    promise = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_PushPromise, XHttp2Frame_EndHeaders,
                                    streamId, promisePayload);
    response = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Headers,
        XHttp2Frame_EndHeaders | XHttp2Frame_EndStream, 2, responsePayload);
    promiseWire = promise ? XHttp2Frame_toByteArray(promise) : NULL;
    responseWire = response ? XHttp2Frame_toByteArray(response) : NULL;
    input = XByteArray_create();
    assert(promise && response && promiseWire && responseWire && input &&
           XByteArray_push_back_2((XVector*)input, XByteArray_constData(promiseWire),
                                  XContainer_size_base((const XContainer*)promiseWire)) &&
           XByteArray_push_back_2((XVector*)input, XByteArray_constData(responseWire),
                                  XContainer_size_base((const XContainer*)responseWire)));
    assert(XHttp2Connection_feed(connection, XByteArray_constData(input),
                                  XContainer_size_base((const XContainer*)input)));
    pushed = XHttp2Connection_takePushedReply(connection);
    assert(pushed && XHttpReply_isFinished(pushed) && XHttpReply_statusCode(pushed) == 200);
    XClass_delete_base((XClass*)pushed);
    XClass_delete_base((XClass*)input);
    XClass_delete_base((XClass*)responseWire);
    XClass_delete_base((XClass*)promiseWire);
    XClass_delete_base((XClass*)response);
    XClass_delete_base((XClass*)promise);
    XClass_delete_base((XClass*)responsePayload);
    XClass_delete_base((XClass*)promisePayload);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)connection);
    XClass_delete_base((XClass*)configuration);
}

static void xhttp_test_server_values(void)
{
    XHttpServerRequest* request = XHttpServerRequest_create();
    XHttpServerResponse* response;
    XHttpHeaders* headers;
    XString* urlText = XString_create_utf8("http://example.com/path/alice?name=value");
    XUrl* url;
    XByteArray* body = XByteArray_create_utf8("server-body");
    XByteArray* mime = XByteArray_create_utf8("text/plain");
    XByteArray* value;
    XHttpServerRouter* router;
    XHttpServerRouterRule* rule;
    XHttpServerResponder responder;
    XHttpServerWebSocketUpgradeResponse* upgrade;
    XHttpServerWebSocketUpgradeResponse* upgradeCopy;
    XByteArray* denyMessage;
    int routeCount = 0;
    assert(request && body && mime && urlText);
    url = XUrl_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, urlText, XUrl_TolerantMode);
    assert(url);
    request->m_url = url;
    request->m_method = XHttpServerRequest_Get;
    headers = XHttpHeaders_create();
    assert(headers && XHttpHeaders_append_utf8(headers, "X-Test", "value"));
    response = XHttpServerResponse_create_mime(mime, body, XHttpServerResponse_Created);
    assert(response && XHttpServerResponse_setHeaders(response, headers));
    {
        XByteArray* missing = XByteArray_create_utf8("missing");
        value = XHttpServerRequest_value(request, missing);
        if (missing) XClass_delete_base((XClass*)missing);
    }
    assert(!value);
    assert(XHttpServerRequest_query_const(request) &&
           XString_equals_utf8(XHttpServerRequest_query_const(request),
                               "name=value", XChar_CaseSensitive));
    router = XHttpServerRouter_create(NULL);
    assert(router);
    rule = XHttpServerRouter_addRule_utf8(
        router, "/path/<arg>", XHttpServerRequest_methodFlag(XHttpServerRequest_Get),
        xhttp_test_route_handler, &routeCount);
    assert(rule && XHttpServerRouter_size(router) == 1);
    assert(XHttpServerRouterRule_matches(rule, request));
    memset(&responder, 0, sizeof(responder));
    assert(XHttpServerRouter_handleRequest(router, request, &responder));
    assert(routeCount == 1);
    request->m_method = XHttpServerRequest_Post;
    assert(!XHttpServerRouterRule_matches(rule, request));
    XClass_delete_base((XClass*)router);
    denyMessage = XByteArray_create_utf8("not allowed");
    upgrade = XHttpServerWebSocketUpgradeResponse_denyWith(401, denyMessage);
    upgradeCopy = XHttpServerWebSocketUpgradeResponse_create_copy(upgrade);
    assert(denyMessage && upgrade && upgradeCopy);
    assert(XHttpServerWebSocketUpgradeResponse_type(upgrade) ==
               XHttpServerWebSocketUpgradeResponse_Deny &&
           XHttpServerWebSocketUpgradeResponse_denyStatus(upgradeCopy) == 401 &&
           xhttp_test_bytes_equal(
               XHttpServerWebSocketUpgradeResponse_denyMessage_const(upgradeCopy),
               "not allowed"));
    XClass_delete_base((XClass*)upgradeCopy);
    XClass_delete_base((XClass*)upgrade);
    XClass_delete_base((XClass*)denyMessage);
    assert(XHttpServerResponse_statusCode(response) == XHttpServerResponse_Created);
    assert(xhttp_test_bytes_equal(XHttpServerResponse_data_const(response), "server-body"));
    assert(xhttp_test_bytes_equal(XHttpServerResponse_mimeType_const(response), "text/plain"));
    XClass_delete_base((XClass*)response);
    XClass_delete_base((XClass*)headers);
    XClass_delete_base((XClass*)mime);
    XClass_delete_base((XClass*)body);
    XClass_delete_base((XClass*)urlText);
    XClass_delete_base((XClass*)request);
}

static void xhttp_test_http2_client(void)
{
    XHttp2ClientSession* session = XHttp2ClientSession_create();
    XHttpRequest* request = XHttpRequest_create();
    XByteArray* start;
    XByteArray* wire;
    XHttp2Frame* frame;
    size_t offset;
    size_t consumed;
    uint32_t streamId = 0;
    assert(session && request);
    assert(XHttpRequest_setUrl_utf8(request, "https://example.com/api"));
    assert(XHttpRequest_setBody_utf8(request, "hello"));
    start = XHttp2ClientSession_start(session);
    assert(start && XHttp2Frame_hasClientPreface(XByteArray_constData(start),
                                                  XByteArray_size_base(start)));
    frame = XHttp2Frame_fromBytes(XByteArray_constData(start) +
                                  sizeof(XHttp2Frame_ClientPreface) - 1,
                                  XByteArray_size_base(start) -
                                  (sizeof(XHttp2Frame_ClientPreface) - 1), &consumed);
    assert(frame && XHttp2Frame_type(frame) == XHttp2Frame_Settings &&
           XContainer_size_base((const XContainer*)XHttp2Frame_payload_const(frame)) == 6 &&
           XByteArray_constData((XByteArray*)XHttp2Frame_payload_const(frame))[0] == 0 &&
           XByteArray_constData((XByteArray*)XHttp2Frame_payload_const(frame))[1] == 2);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)start);
    wire = XHttp2ClientSession_encodeRequest(session, request, &streamId);
    assert(wire && streamId == 1);
    offset = 0;
    frame = XHttp2Frame_fromBytes(XByteArray_constData(wire),
                                  XByteArray_size_base(wire), &consumed);
    assert(frame && XHttp2Frame_type(frame) == XHttp2Frame_Headers &&
           XHttp2Frame_streamId(frame) == streamId);
    offset += consumed;
    XClass_delete_base((XClass*)frame);
    frame = XHttp2Frame_fromBytes(XByteArray_constData(wire) + offset,
                                  XByteArray_size_base(wire) - offset, &consumed);
    assert(frame && XHttp2Frame_type(frame) == XHttp2Frame_Data &&
           XHttp2Frame_flags(frame) == XHttp2Frame_EndStream);
    XClass_delete_base((XClass*)frame);
    XClass_delete_base((XClass*)wire);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)session);
}

static void xhttp_test_http2_reply(void)
{
    XHttpRequest* request = XHttpRequest_create();
    XHttpReply* reply;
    XHttp2HeaderList* headers = XHttp2HeaderList_create();
    XByteArray* statusName = XByteArray_create_utf8(":status");
    XByteArray* status = XByteArray_create_utf8("200");
    XByteArray* contentType = XByteArray_create_utf8("content-type");
    XByteArray* text = XByteArray_create_utf8("text/plain");
    XHttp2HeaderList* trailers = XHttp2HeaderList_create();
    XHttpReply* invalidTrailerReply;
    XByteArray* trailerName = XByteArray_create_utf8("x-trailer");
    XByteArray* trailerValue = XByteArray_create_utf8("done");
    assert(request && headers && statusName && status && contentType && text &&
           trailers && trailerName && trailerValue);
    assert(XHttpRequest_setUrl_utf8(request, "https://example.com/h2"));
    reply = XHttpReply_create(request);
    assert(reply);
    assert(XHttp2HeaderList_append(headers, statusName, status));
    assert(XHttp2HeaderList_append(headers, contentType, text));
    assert(XHttpReply_feedHttp2Headers(reply, headers, false));
    assert(XHttpReply_statusCode(reply) == 200);
    assert(XHttpReply_hasRawHeader(reply, contentType));
    assert(XHttpReply_feedHttp2Data(reply, "h2-ok", 5, false));
    assert(XHttp2HeaderList_append(trailers, trailerName, trailerValue));
    assert(XHttpReply_feedHttp2Headers(reply, trailers, true));
    assert(XHttpReply_isFinished(reply));
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "h2-ok"));
    assert(XHttpHeaders_contains(XHttpReply_trailers_const(reply), trailerName));
    invalidTrailerReply = XHttpReply_create(request);
    assert(invalidTrailerReply && XHttpReply_feedHttp2Headers(invalidTrailerReply, headers, false));
    /* HTTP/2 尾部字段必须携带 END_STREAM，不能在其后继续接收 DATA。 */
    assert(!XHttpReply_feedHttp2Headers(invalidTrailerReply, trailers, false) &&
           XHttpReply_error(invalidTrailerReply) == XHttpReply_ProtocolInvalidOperationError);
    XClass_delete_base((XClass*)invalidTrailerReply);
    XClass_delete_base((XClass*)trailerValue);
    XClass_delete_base((XClass*)trailerName);
    XClass_delete_base((XClass*)trailers);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)text);
    XClass_delete_base((XClass*)contentType);
    XClass_delete_base((XClass*)status);
    XClass_delete_base((XClass*)statusName);
    XClass_delete_base((XClass*)headers);
    XClass_delete_base((XClass*)request);
}

static void xhttp_test_http2_reply_header_rules(void)
{
    XHttpRequest* request = XHttpRequest_create();
    XHttpReply* reply;
    XHttp2HeaderList* headers = XHttp2HeaderList_create();
    XByteArray* contentType = XByteArray_create_utf8("content-type");
    XByteArray* text = XByteArray_create_utf8("text/plain");
    XByteArray* statusName = XByteArray_create_utf8(":status");
    XByteArray* status = XByteArray_create_utf8("200");
    XHttp2HeaderList* forbidden = XHttp2HeaderList_create();
    XByteArray* connection = XByteArray_create_utf8("connection");
    XByteArray* close = XByteArray_create_utf8("close");
    assert(request && headers && contentType && text && statusName && status && forbidden &&
           connection && close);
    assert(XHttpRequest_setUrl_utf8(request, "https://example.com/h2-rules"));
    reply = XHttpReply_create(request);
    assert(reply);
    /* :status 必须位于所有普通字段之前。 */
    assert(XHttp2HeaderList_append(headers, contentType, text));
    assert(XHttp2HeaderList_append(headers, statusName, status));
    assert(!XHttpReply_feedHttp2Headers(reply, headers, false));
    assert(XHttpReply_error(reply) == XHttpReply_ProtocolInvalidOperationError);
    XClass_delete_base((XClass*)reply);
    reply = XHttpReply_create(request);
    assert(reply);
    assert(XHttp2HeaderList_append(forbidden, statusName, status));
    assert(XHttp2HeaderList_append(forbidden, connection, close));
    assert(!XHttpReply_feedHttp2Headers(reply, forbidden, false));
    assert(XHttpReply_error(reply) == XHttpReply_ProtocolInvalidOperationError);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)close);
    XClass_delete_base((XClass*)connection);
    XClass_delete_base((XClass*)forbidden);
    XClass_delete_base((XClass*)status);
    XClass_delete_base((XClass*)statusName);
    XClass_delete_base((XClass*)text);
    XClass_delete_base((XClass*)contentType);
    XClass_delete_base((XClass*)headers);
    XClass_delete_base((XClass*)request);
}

static void xhttp_test_ssl_alpn_api(void)
{
    XSslSocket* socket = XSslSocket_create();
    XVector* protocols = XVector_create(sizeof(XByteArray*));
    XByteArray* h2 = XByteArray_create_utf8("h2");
    XByteArray* http11 = XByteArray_create_utf8("http/1.1");
    XVector* configured;
    XVector* empty;
    assert(socket && protocols && h2 && http11);
    assert(XVector_push_back_1_base(protocols, &h2));
    assert(XVector_push_back_1_base(protocols, &http11));
    assert(XSslSocket_setAllowedNextProtocols(socket, protocols));
    configured = XSslSocket_allowedNextProtocols(socket);
    assert(configured && XContainer_size_base((const XContainer*)configured) == 2);
    assert(XSslSocket_nextProtocolNegotiationStatus(socket) ==
           XSSL_NextProtocolNegotiationUnsupported);
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)configured); ++i) {
        XByteArray** slot = (XByteArray**)XVector_at_base(configured, (int64_t)i);
        if (slot && *slot) XClass_delete_base((XClass*)*slot);
    }
    XVector_delete_base((XContainer*)configured);
    empty = XVector_create(sizeof(XByteArray*));
    assert(empty && XSslSocket_setAllowedNextProtocols(socket, empty));
    assert(XSslSocket_nextProtocolNegotiationStatus(socket) ==
           XSSL_NextProtocolNegotiationNone);
    XVector_delete_base((XContainer*)empty);
    XClass_delete_base((XClass*)h2);
    XClass_delete_base((XClass*)http11);
    XVector_delete_base((XContainer*)protocols);
    XClass_delete_base((XClass*)socket);
}

static void xhttp_test_manager_ssl_api(void)
{
    XNetworkAccessManager* manager = XNetworkAccessManager_create();
    assert(manager);
    assert(XNetworkAccessManager_sslPeerVerifyMode(manager) == XSSL_VerifyPeer);
    XNetworkAccessManager_setSslPeerVerifyMode(manager, XSSL_VerifyNone);
    assert(XNetworkAccessManager_sslPeerVerifyMode(manager) == XSSL_VerifyNone);
    XNetworkAccessManager_setSslPeerVerifyMode(manager, XSSL_AutoVerifyPeer);
    assert(XNetworkAccessManager_sslPeerVerifyMode(manager) == XSSL_AutoVerifyPeer);
    assert(XNetworkAccessManager_setSslCaCertificate(manager, NULL));
    assert(XNetworkAccessManager_sslCaCertificate_const(manager) == NULL);
    XClass_delete_base((XClass*)manager);
}

static void xhttp_test_request_factory_and_rest(void)
{
    XNetworkRequestFactory* factory = XNetworkRequestFactory_create();
    XHttpHeaders* common = XHttpHeaders_create();
    XByteArray* query = XByteArray_create_utf8("page=2");
    XByteArray* token = XByteArray_create_utf8("token");
    XString* baseUrlText = XString_create_utf8("https://example.com/api");
    XUrl* baseUrl;
    XHttpRequest* request;
    XString* requestUrl;
    XVariant* priorityValue = XVariant_create_int(XHttpRequest_HighPriority);
    XVariant* redirectAttribute;
    XHttpReply* reply;
    XRestReply* rest;
    XJsonDocument* json;
    XString* errorText = NULL;
    XNetworkAccessManager* manager;
    XRestAccessManager* restManager;
    assert(factory && common && query && token && baseUrlText && priorityValue);
    baseUrl = XUrl_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, baseUrlText, XUrl_TolerantMode);
    assert(baseUrl);
    assert(XHttpHeaders_append_utf8(common, "X-Common", "yes"));
    assert(XNetworkRequestFactory_setBaseUrl(factory, baseUrl));
    assert(XNetworkRequestFactory_setCommonHeaders(factory, common));
    assert(XNetworkRequestFactory_setBearerToken(factory, token));
    assert(XNetworkRequestFactory_setQueryParameters(factory, query));
    XNetworkRequestFactory_setTransferTimeout(factory, 1234);
    assert(XNetworkRequestFactory_transferTimeout(factory) == 1234);
    assert(XNetworkRequestFactory_setPriority(factory, XHttpRequest_HighPriority));
    assert(XNetworkRequestFactory_setAttribute(factory, XNetworkRequestFactory_RedirectPolicyAttribute,
                                               priorityValue));
    request = XNetworkRequestFactory_createRequest_path(factory, "items");
    assert(request && XHttpRequest_priority(request) == XHttpRequest_HighPriority);
    assert(xhttp_test_has_header(XHttpRequest_headers_const(request), "x-common"));
    assert(xhttp_test_has_header(XHttpRequest_headers_const(request), "authorization"));
    redirectAttribute = XHttpRequest_attribute(request,
                                                XNetworkRequestFactory_RedirectPolicyAttribute);
    assert(redirectAttribute);
    XClass_delete_base((XClass*)redirectAttribute);
    requestUrl = XUrl_toString(XHttpRequest_url_const(request));
    assert(requestUrl && strcmp(XString_toUtf8(requestUrl), "https://example.com/items?page=2") == 0);
    XClass_delete_base((XClass*)requestUrl);

    reply = XHttpReply_create(request);
    assert(reply);
    assert(XHttpReply_feed(reply, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"ok\":true}",
                           strlen("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"ok\":true}")));
    rest = XRestReply_create(reply);
    assert(rest && XRestReply_isSuccess(rest));
    json = XRestReply_readJson(rest, &errorText);
    assert(json && !errorText && XJsonDocument_isObject(json));
    XJsonDocument_delete(json);
    manager = XNetworkAccessManager_create();
    restManager = XRestAccessManager_create(manager);
    assert(manager && restManager && XRestAccessManager_networkAccessManager(restManager) == manager);
    XClass_delete_base((XClass*)restManager);
    XClass_delete_base((XClass*)manager);
    XClass_delete_base((XClass*)rest);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)priorityValue);
    XClass_delete_base((XClass*)baseUrl);
    XClass_delete_base((XClass*)baseUrlText);
    XClass_delete_base((XClass*)token);
    XClass_delete_base((XClass*)query);
    XClass_delete_base((XClass*)common);
    XClass_delete_base((XClass*)factory);
}

static void xhttp_test_manager_local(XVariant* data)
{
    XNetworkAccessManager* manager;
    XHttpRequest* request;
    XHttpReply* reply;
    (void)data;
    manager = XNetworkAccessManager_create();
    request = XHttpRequest_create();
    assert(manager && request);
    assert(XHttpRequest_setUrl_utf8(request, "http://127.0.0.1:18080/"));
    reply = XNetworkAccessManager_get(manager, request);
    assert(reply);
    for (int i = 0; i < 100 && (!XHttpReply_isFinished(reply) ||
                                XNetworkAccessManager_activeReplyCount(manager) != 0); ++i) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
    }
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_error(reply) == XHttpReply_NoError);
    assert(XHttpReply_statusCode(reply) == 200);
    assert(XNetworkAccessManager_activeReplyCount(manager) == 0);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)manager);
    XPrintf("XNetworkAccessManager 本地 HTTP 测试通过\n");
}

static void xhttp_test_manager_redirect(XVariant* data)
{
    XNetworkAccessManager* manager;
    XHttpRequest* request;
    XHttpReply* reply;
    (void)data;
    manager = XNetworkAccessManager_create();
    request = XHttpRequest_create();
    assert(manager && request);
    XNetworkAccessManager_setRedirectPolicy(manager, XHttpRequest_NoLessSafeRedirectPolicy);
    assert(XHttpRequest_setUrl_utf8(request, "http://127.0.0.1:18080/redirect"));
    reply = XNetworkAccessManager_get(manager, request);
    assert(reply);
    for (int i = 0; i < 100 && (!XHttpReply_isFinished(reply) ||
                                XNetworkAccessManager_activeReplyCount(manager) != 0); ++i) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
    }
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_statusCode(reply) == 200);
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "redirect-ok"));
    assert(XNetworkAccessManager_activeReplyCount(manager) == 0);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)manager);
    XPrintf("XNetworkAccessManager 重定向测试通过\n");
}

/* 本地服务端首次返回 401，认证信号同步填写凭据后管理器必须在新连接上重发。 */
static void xhttp_test_manager_authentication(XVariant* data)
{
    XNetworkAccessManager* manager;
    XHttpRequest* request;
    XHttpReply* reply;
    (void)data;
    manager = XNetworkAccessManager_create();
    request = XHttpRequest_create();
    assert(manager && request);
    assert(XHttpRequest_setUrl_utf8(request, "http://127.0.0.1:18080/auth-basic"));
    xhttp_test_authentication_signal_count = 0;
    assert(XObject_connect_2((XObject*)manager,
                             XSignal(XNetworkAccessManager_authenticationRequired_signal),
                             xhttp_test_authentication_required_slot));
    reply = XNetworkAccessManager_get(manager, request);
    assert(reply);
    for (int i = 0; i < 300 && (!XHttpReply_isFinished(reply) ||
                                XNetworkAccessManager_activeReplyCount(manager) != 0); ++i) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
    }
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_error(reply) == XHttpReply_NoError);
    assert(XHttpReply_statusCode(reply) == 200);
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "auth-ok"));
    assert(xhttp_test_authentication_signal_count == 1);
    assert(XNetworkAccessManager_activeReplyCount(manager) == 0);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)manager);
    XPrintf("XNetworkAccessManager Basic 认证重发测试通过\n");
}

/* 服务端同时给出 Basic 和 Digest 时，管理器必须选择优先级更高的 Digest 挑战。 */
static void xhttp_test_manager_digest_authentication(XVariant* data)
{
    XNetworkAccessManager* manager;
    XHttpRequest* request;
    XHttpReply* reply;
    (void)data;
    manager = XNetworkAccessManager_create();
    request = XHttpRequest_create();
    assert(manager && request);
    assert(XHttpRequest_setUrl_utf8(request, "http://127.0.0.1:18080/auth-digest"));
    xhttp_test_authentication_signal_count = 0;
    assert(XObject_connect_2((XObject*)manager,
                             XSignal(XNetworkAccessManager_authenticationRequired_signal),
                             xhttp_test_authentication_required_slot));
    reply = XNetworkAccessManager_get(manager, request);
    assert(reply);
    for (int i = 0; i < 300 && (!XHttpReply_isFinished(reply) ||
                                XNetworkAccessManager_activeReplyCount(manager) != 0); ++i) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
    }
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_error(reply) == XHttpReply_NoError);
    assert(XHttpReply_statusCode(reply) == 200);
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "digest-ok"));
    assert(xhttp_test_authentication_signal_count == 1);
    assert(XNetworkAccessManager_activeReplyCount(manager) == 0);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)manager);
    XPrintf("XNetworkAccessManager Digest 优先认证重发测试通过\n");
}

/* Digest 条件不满足时必须回退到同一响应中可用的 Basic 挑战。 */
static void xhttp_test_manager_authentication_fallback(XVariant* data)
{
    XNetworkAccessManager* manager;
    XHttpRequest* request;
    XHttpReply* reply;
    (void)data;
    manager = XNetworkAccessManager_create();
    request = XHttpRequest_create();
    assert(manager && request);
    assert(XHttpRequest_setUrl_utf8(request, "http://127.0.0.1:18080/auth-fallback"));
    xhttp_test_authentication_signal_count = 0;
    assert(XObject_connect_2((XObject*)manager,
                             XSignal(XNetworkAccessManager_authenticationRequired_signal),
                             xhttp_test_authentication_required_slot));
    reply = XNetworkAccessManager_get(manager, request);
    assert(reply);
    for (int i = 0; i < 300 && (!XHttpReply_isFinished(reply) ||
                                XNetworkAccessManager_activeReplyCount(manager) != 0); ++i) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
    }
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_error(reply) == XHttpReply_NoError);
    assert(XHttpReply_statusCode(reply) == 200);
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "fallback-ok"));
    assert(xhttp_test_authentication_signal_count == 1);
    assert(XNetworkAccessManager_activeReplyCount(manager) == 0);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)manager);
    XPrintf("XNetworkAccessManager Basic 回退认证测试通过\n");
}

/* 本地服务按 NTLM 空挑战、Type 2 挑战、最终成功的顺序验证管理器两阶段重发。 */
static void xhttp_test_manager_ntlm_authentication(XVariant* data)
{
    XHttpServer* server;
    XHttpTestNtlmServerState state;
    XNetworkAccessManager* manager;
    XHttpRequest* request;
    XHttpReply* reply;
    (void)data;
    memset(&state, 0, sizeof(state));
    server = XHttpServer_create();
    manager = XNetworkAccessManager_create();
    request = XHttpRequest_create();
    assert(server && manager && request);
    XHttpServer_setHandler(server, xhttp_test_ntlm_server_handler, &state);
    assert(XHttpServer_listen(server, NULL, 0));
    assert(XHttpServer_serverPort(server) != 0);
    {
        char url[96];
        snprintf(url, sizeof(url), "http://127.0.0.1:%u/auth-ntlm",
                 (unsigned int)XHttpServer_serverPort(server));
        assert(XHttpRequest_setUrl_utf8(request, url));
    }
    xhttp_test_authentication_signal_count = 0;
    assert(XObject_connect_2((XObject*)manager,
                             XSignal(XNetworkAccessManager_authenticationRequired_signal),
                             xhttp_test_authentication_required_slot));
    reply = XNetworkAccessManager_get(manager, request);
    assert(reply);
    for (int i = 0; i < 500 && (!XHttpReply_isFinished(reply) ||
                                XNetworkAccessManager_activeReplyCount(manager) != 0); ++i) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(10);
    }
    assert(XHttpReply_isFinished(reply));
    assert(XHttpReply_error(reply) == XHttpReply_NoError);
    assert(XHttpReply_statusCode(reply) == 200);
    assert(xhttp_test_bytes_equal(XHttpReply_body_const(reply), "ntlm-ok"));
    assert(xhttp_test_authentication_signal_count == 1);
    assert(state.m_requests == 3 && state.m_type1 == 1 && state.m_type3 == 1);
    assert(XNetworkAccessManager_activeReplyCount(manager) == 0);
    XClass_delete_base((XClass*)reply);
    XClass_delete_base((XClass*)request);
    XClass_delete_base((XClass*)manager);
    XClass_delete_base((XClass*)server);
    XPrintf("XNetworkAccessManager NTLMv2 两阶段认证测试通过\n");
}

static void xhttp_test_all(XVariant* data)
{
    (void)data;
    xhttp_test_headers();
    xhttp_test_request();
    xhttp_test_multipart();
    xhttp_test_cookie();
    xhttp_test_content_length_reply();
    xhttp_test_chunked_reply();
    xhttp_test_close_delimited_reply();
    xhttp_test_invalid_reply();
    xhttp_test_hsts();
    xhttp_test_cache();
    xhttp_test_http2_frame();
    xhttp_test_http2_frame_validation();
    xhttp_test_http2_headers();
    xhttp_test_http2_stateful_decoder();
    xhttp_test_http2_stateful_encoder();
    xhttp_test_http2_peer_header_limit();
    xhttp_test_http2_connection_multiplex();
    xhttp_test_http2_connection_initial_window();
    xhttp_test_http2_connection_h2c_upgrade();
    xhttp_test_http2_connection_protocol_limits();
    xhttp_test_http2_connection_detach_closed_reply();
    xhttp_test_http2_connection_idle_stream_errors();
    xhttp_test_http2_connection_qt_stream_state();
    xhttp_test_http2_connection_goaway();
    xhttp_test_http2_connection_push_promise();
    xhttp_test_authenticator_and_deferred_authentication();
    xhttp_test_server_values();
    xhttp_test_http2_client();
    xhttp_test_http2_reply();
    xhttp_test_http2_reply_header_rules();
    xhttp_test_ssl_alpn_api();
    xhttp_test_manager_ssl_api();
    xhttp_test_request_factory_and_rest();
    xhttp_test_manager_ntlm_authentication(NULL);
    XPrintf("XHttp 本地核心行为测试通过\n");
}

void XHttpTest_registerAll(XMenu* root)
{
    XMenu* menu;
    XAction* action;
    if (!root)
        return;
    menu = XMenu_create("XHttp(HTTP客户端)");
    if (!menu)
        return;
    XMenu_addMenu(root, menu);
    action = XMenu_addAction(menu, "核心头部、请求、MIME和响应解析");
    if (action)
        XAction_setAction(action, xhttp_test_all);
    action = XMenu_addAction(menu, "管理器本地 HTTP（需 127.0.0.1:18080）");
    if (action)
        XAction_setAction(action, xhttp_test_manager_local);
    action = XMenu_addAction(menu, "管理器重定向（需本地测试服务）");
    if (action)
        XAction_setAction(action, xhttp_test_manager_redirect);
    action = XMenu_addAction(menu, "管理器 Basic 认证重发（需本地测试服务）");
    if (action)
        XAction_setAction(action, xhttp_test_manager_authentication);
    action = XMenu_addAction(menu, "管理器 Digest 优先认证重发（需本地测试服务）");
    if (action)
        XAction_setAction(action, xhttp_test_manager_digest_authentication);
    action = XMenu_addAction(menu, "管理器 Basic 回退认证（需本地测试服务）");
    if (action)
        XAction_setAction(action, xhttp_test_manager_authentication_fallback);
    action = XMenu_addAction(menu, "管理器 NTLMv2 两阶段认证（需本地测试服务）");
    if (action)
        XAction_setAction(action, xhttp_test_manager_ntlm_authentication);
}
