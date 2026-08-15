/**
 * @file       XServerChanTest.c
 * @brief      Server酱客户端本地 HTTP 行为和真实发送测试。
 */

#include "XServerChanTest.h"

#include "XCoreApplication.h"
#include "XHttpServer.h"
#include "XServerChan.h"
#include "XThread.h"
#include "XUrl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct XServerChanTestState {
    int m_requests;
    bool m_methodValid;
    bool m_pathValid;
    bool m_headersValid;
    bool m_bodyValid;
} XServerChanTestState;

static bool xserverchan_test_bytes_equal(const XByteArray* value, const char* expected)
{
    size_t expectedSize = expected ? strlen(expected) : 0;
    return value && XByteArray_size_base(value) == expectedSize &&
        (expectedSize == 0 || memcmp(XByteArray_constData((XByteArray*)value),
                                     expected, expectedSize) == 0);
}

static bool xserverchan_test_bytes_starts_with(const XByteArray* value, const char* expected)
{
    size_t valueSize = value ? XByteArray_size_base(value) : 0;
    size_t expectedSize = expected ? strlen(expected) : 0;
    return value && valueSize >= expectedSize &&
        (expectedSize == 0 || memcmp(XByteArray_constData((XByteArray*)value),
                                     expected, expectedSize) == 0);
}

static void xserverchan_test_handler(const XHttpServerRequest* request,
                                     XHttpServerResponder* responder,
                                     void* context)
{
    XServerChanTestState* state = (XServerChanTestState*)context;
    const XUrl* url = XHttpServerRequest_url_const(request);
    const XString* path = url ? XUrl_path_const(url) : NULL;
    XByteArray* contentTypeName = NULL;
    XByteArray* contentType = NULL;
    XByteArray* response = NULL;
    if (state) {
        state->m_requests++;
        state->m_methodValid = XHttpServerRequest_method(request) == XHttpServerRequest_Post;
        state->m_pathValid = path && !strcmp(XString_toUtf8(path), "/send");
        contentTypeName = XByteArray_create_utf8("Content-Type");
        contentType = XHttpServerRequest_value(request, contentTypeName);
        state->m_headersValid = xserverchan_test_bytes_starts_with(contentType,
            "application/x-www-form-urlencoded");
        state->m_bodyValid = xserverchan_test_bytes_equal(XHttpServerRequest_body_const(request),
            "title=Hello%20Server&desp=%E6%B5%8B%E8%AF%95%0Aline%20two");
    }
    response = XByteArray_create_utf8("{\"code\":0,\"message\":\"ok\"}");
    XHttpServerResponder_write(responder, response, NULL, XHttpServerResponse_Ok);
    if (response) XByteArray_delete_base(response);
    if (contentType) XByteArray_delete_base(contentType);
    if (contentTypeName) XByteArray_delete_base(contentTypeName);
}

static void xserverchan_test_local_send(void)
{
    XServerChanTestState state = {0};
    XHttpServer* server = XHttpServer_create();
    XServerChan* client = XServerChan_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "SCT_test_key");
    XServerChanResult* result;
    char endpoint[96];
    assert(server && client);
    XHttpServer_setHandler(server, xserverchan_test_handler, &state);
    assert(XHttpServer_listen(server, NULL, 0));
    snprintf(endpoint, sizeof(endpoint), "http://127.0.0.1:%u/send",
             (unsigned int)XHttpServer_serverPort(server));
    assert(XServerChan_setEndpointUrl_utf8(client, endpoint));
    XServerChan_setTransferTimeout(client, 3000);
    result = XServerChan_sendBlocking(client, "Hello Server", "测试\nline two");
    assert(result && XServerChanResult_isSuccess(result));
    assert(XServerChanResult_apiCode(result) == 0);
    assert(XServerChanResult_httpStatusCode(result) == 200);
    assert(XServerChanResult_message_const(result) &&
           !strcmp(XString_toUtf8(XServerChanResult_message_const(result)), "ok"));
    assert(state.m_requests == 1 && state.m_methodValid && state.m_pathValid &&
           state.m_headersValid && state.m_bodyValid);
    XServerChanResult_delete_base(result);
    XServerChan_delete_base(client);
    XHttpServer_delete_base(server);
    printf("XServerChan 本地发送与 JSON 解析测试通过\n");
}

static void xserverchan_test_lifecycle(void)
{
    XServerChan* client = XServerChan_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "sctp123t_test_key");
    XServerChan* copy;
    XServerChan* moved;
    XServerChanResult* result = XServerChanResult_create();
    XServerChanResult* resultCopy;
    XServerChanResult* resultMove;
    assert(client && XServerChan_hasSendKey(client));
    assert(!XServerChan_setSendKey_utf8(client, "sctp_without_uid"));
    assert(XServerChan_setEndpointUrl_utf8(client, NULL));
    copy = XServerChan_create_copy(client);
    moved = copy ? XServerChan_create_move(copy) : NULL;
    assert(copy && moved && !XServerChan_hasSendKey(copy) && XServerChan_hasSendKey(moved));
    assert(result);
    result->m_apiCode = 0;
    result->m_httpStatusCode = 200;
    result->m_error = XServerChanResult_NoError;
    result->m_success = true;
    resultCopy = XServerChanResult_create_copy(result);
    resultMove = resultCopy ? XServerChanResult_create_move(resultCopy) : NULL;
    assert(resultCopy && resultMove && XServerChanResult_isSuccess(resultMove) &&
           !XServerChanResult_isSuccess(resultCopy));
    XServerChanResult_delete_base(resultMove);
    XServerChanResult_delete_base(resultCopy);
    XServerChanResult_delete_base(result);
    XServerChan_delete_base(moved);
    XServerChan_delete_base(copy);
    XServerChan_delete_base(client);
    printf("XServerChan 类生命周期、拷贝移动和 SendKey 校验测试通过\n");
}

static void xserverchan_test_live_send(void)
{
    static const struct {
        const char* m_title;
        const char* m_desp;
    } cases[] = {
        {"XinYueC ServerChan 普通文本测试", "这是一条普通文本测试消息。"},
        {"XinYueC ServerChan Markdown 测试",
         "## Markdown 测试\n\n- 第一项\n- 第二项\n\n`code`"},
        {"XinYueC ServerChan 编码测试",
         "中文、空格、&、=、%、+ 和换行测试。\n第二行内容。"},
        {"XinYueC ServerChan 空正文测试", NULL}
    };
    char sendKey[256] = {0};
    const char* caPaths[5];
    XServerChan* client;
    size_t successCount = 0;
    size_t i;

    printf("请输入 SendKey（不会打印或保存到源码）：");
    fflush(stdout);
    if (scanf("%255s", sendKey) != 1 || !sendKey[0]) {
        memset(sendKey, 0, sizeof(sendKey));
        printf("未读取到 SendKey，跳过真实发送测试\n");
        return;
    }
    client = XServerChan_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sendKey);
    memset(sendKey, 0, sizeof(sendKey));
    if (!client) {
        printf("SendKey 无效，跳过真实发送测试\n");
        return;
    }
    caPaths[0] = getenv("SSL_CERT_FILE");
    caPaths[1] = "/etc/ssl/certs/ISRG_Root_X2.pem";
    caPaths[2] = "/etc/ssl/certs/0b9bc432.0";
    caPaths[3] = "/etc/pki/tls/certs/ca-bundle.crt";
    caPaths[4] = "/etc/ssl/cert.pem";
    for (i = 0; i < sizeof(caPaths) / sizeof(caPaths[0]); ++i) {
        if (caPaths[i] && XServerChan_setSslCaCertificateFile_utf8(client, caPaths[i]))
            break;
    }
    if (i == sizeof(caPaths) / sizeof(caPaths[0])) {
        printf("未找到可用系统 CA，跳过真实发送测试（不关闭证书校验）\n");
        XServerChan_delete_base(client);
        return;
    }
    XServerChan_setTransferTimeout(client, 15000);
    printf("开始真实发送 %zu 条测试消息\n", sizeof(cases) / sizeof(cases[0]));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        XServerChanResult* result = XServerChan_sendBlocking(client,
            cases[i].m_title, cases[i].m_desp);
        if (result && XServerChanResult_isSuccess(result)) {
            ++successCount;
            printf("真实消息[%zu]发送成功：HTTP=%d code=%d\n", i + 1,
                   XServerChanResult_httpStatusCode(result),
                   XServerChanResult_apiCode(result));
        } else {
            const XString* message = result ? XServerChanResult_message_const(result) : NULL;
            printf("真实消息[%zu]发送失败：error=%d HTTP=%d code=%d\n", i + 1,
                   (int)XServerChanResult_error(result),
                   XServerChanResult_httpStatusCode(result),
                   XServerChanResult_apiCode(result));
            if (message && XString_toUtf8(message) && *XString_toUtf8(message))
                printf("失败原因：%s\n", XString_toUtf8(message));
        }
        if (result) XServerChanResult_delete_base(result);
        XThread_msleep(200);
    }
    XServerChan_delete_base(client);
    printf("真实发送测试完成：%zu/%zu 成功\n", successCount,
           sizeof(cases) / sizeof(cases[0]));
}

static void xserverchan_test_all(XVariant* data)
{
    (void)data;
    xserverchan_test_lifecycle();
    xserverchan_test_local_send();
    xserverchan_test_live_send();
}

void XServerChanTest_registerAll(XMenu* root)
{
    XMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XMenu_create("XServerChan(Server酱客户端)");
    if (!menu) return;
    XMenu_addMenu(root, menu);
    action = XMenu_addAction(menu, "类生命周期、表单发送和响应解析");
    if (action) XAction_setAction(action, xserverchan_test_all);
}
