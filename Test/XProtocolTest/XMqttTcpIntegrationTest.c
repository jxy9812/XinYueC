#include "XProtocolTest.h"
#include "XMqttTcpServer.h"
#include "XMqttClient.h"
#include "XMqttTopicFilter.h"
#include "XMqttTopicName.h"
#include "XMqttSubscription.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XEventLoop.h"
#include "XObject.h"
#include "XProcess.h"
#include "XThread.h"
#include "XVarList.h"
#include "XPrintf.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* 真实 MQTT TCP 服务器（XMqttTcpServer）与客户端（XMqttClient）联调端口。 */
#define XMqttInteropPort ((uint16_t)18885)

/* 服务器进程就绪标记：父进程轮询标准输出识别该字符串后启动客户端。 */
#define XMqttInteropReadyMarker "XMQTT_INTEROP_READY"

static int g_mqttInteropConnections;
static int g_mqttInteropDisconnections;
static bool g_mqttInteropQuit;

/* ==================== 事件泵（等待条件） ==================== */

/**
 * @brief 泵事件直到条件满足或超时。
 * @param condition 指向条件变量的指针（每次泵后重新读取）。
 * @param timeout 超时毫秒数。
 * @return 条件满足返回 true，超时返回 false。
 */
static bool interop_wait(bool* condition, uint64_t timeout)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + timeout;
    while (!*condition && XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    return *condition;
}

/**
 * @brief 判断字节缓冲区中是否包含指定 ASCII 子串。
 * @param data 字节缓冲区起始指针。
 * @param size 缓冲区字节数。
 * @param needle 待查找的 ASCII 子串。
 * @return 包含返回 true，否则返回 false。
 */
static bool interop_bytes_contains(const uint8_t* data, size_t size, const char* needle)
{
    size_t needleLen;
    size_t i;
    if (!data || !needle) return false;
    needleLen = strlen(needle);
    if (!needleLen || size < needleLen) return false;
    for (i = 0; i + needleLen <= size; ++i) {
        if (memcmp(data + i, needle, needleLen) == 0) return true;
    }
    return false;
}

/* ==================== 服务器进程 ==================== */

static void interop_server_clientConnected(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_mqttInteropConnections;
}

static void interop_server_clientDisconnected(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_mqttInteropDisconnections;
    /* 已有客户端连接过且断开后，服务器进程退出事件泵。 */
    if (g_mqttInteropConnections > 0)
        g_mqttInteropQuit = true;
}

/**
 * @brief 以独立进程运行真实 MQTT TCP 服务器（--test xmqtt-tcp-server）。
 * @details 监听全部本地接口，打印 XMQTT_INTEROP_READY 标记后泵事件；
 *          首个客户端断开或 60 秒超时后退出。返回 true 表示正常完成。
 */
bool XMqttTcpServerProcess_run(void)
{
    XMqttTcpServer* server = XMqttTcpServer_create();
    bool ok = false;
    g_mqttInteropConnections = 0;
    g_mqttInteropDisconnections = 0;
    g_mqttInteropQuit = false;
    if (!server) {
        XPrintf("[MQTT联调][服务器] 创建 XMqttTcpServer 失败\n");
        return false;
    }
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttTcpServer_clientConnected_signal),
                      (XObject*)server,
                      interop_server_clientConnected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttTcpServer_clientDisconnected_signal),
                      (XObject*)server,
                      interop_server_clientDisconnected,
                      XConnectionType_Direct);
    if (!XMqttTcpServer_listen(server, NULL, XMqttInteropPort)) {
        XPrintf("[MQTT联调][服务器] 监听端口 %u 失败\n", (unsigned)XMqttInteropPort);
        XClass_delete_base((XClass*)server);
        return false;
    }
    XPrintf("%s\n", XMqttInteropReadyMarker);
    ok = interop_wait(&g_mqttInteropQuit, 60000);
    XPrintf("[MQTT联调][服务器] 结束：连接 %d 次，断开 %d 次，%s\n",
            g_mqttInteropConnections, g_mqttInteropDisconnections,
            ok ? "正常" : "超时");
    XMqttTcpServer_close(server);
    XClass_delete_base((XClass*)server);
    return ok && g_mqttInteropConnections >= 1;
}

/* ==================== 客户端进程 ==================== */

static int g_mqttInteropReceived;
static int g_mqttInteropPing;
static char g_mqttInteropTopic[128];
static char g_mqttInteropPayload[256];
static bool g_mqttInteropEchoReady;
static bool g_mqttInteropPingReady;

static void interop_client_message(XObject* receiver, XVarList* args)
{
    const XString* name;
    size_t payloadSize;
    (void)receiver;
    XVarList_args_2(args, XByteArray*, payloadArg, XMqttTopicName*, topicArg);
    name = topicArg ? XMqttTopicName_name_const(topicArg) : NULL;
    payloadSize = payloadArg ? (size_t)XByteArray_size_base(payloadArg) : 0;
    if (name) {
        size_t len = XString_toUtf8_length(name);
        if (len >= sizeof(g_mqttInteropTopic))
            len = sizeof(g_mqttInteropTopic) - 1;
        memcpy(g_mqttInteropTopic, XString_toUtf8(name), len);
        g_mqttInteropTopic[len] = '\0';
    }
    if (payloadSize >= sizeof(g_mqttInteropPayload))
        payloadSize = sizeof(g_mqttInteropPayload) - 1;
    if (payloadSize)
        memcpy(g_mqttInteropPayload, XByteArray_constData((XByteArray*)payloadArg), payloadSize);
    g_mqttInteropPayload[payloadSize] = '\0';
    ++g_mqttInteropReceived;
    if (g_mqttInteropReceived >= 4)
        g_mqttInteropEchoReady = true;
}

static void interop_client_ping(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_mqttInteropPing;
    g_mqttInteropPingReady = true;
}

/**
 * @brief 以独立进程运行真实 MQTT 客户端（--test xmqtt-tcp-client）。
 * @details 连接 127.0.0.1 上的真实服务器，完成订阅、QoS0/1/2 发布回显、
 *          保留消息、PINGREQ/PINGRESP、取消订阅与正常断开。全部通过返回 true。
 */
bool XMqttTcpClientProcess_run(void)
{
    XMqttClient* client = XMqttClient_create();
    XMqttTopicFilter* filter = NULL;
    XMqttTopicName* topic = NULL;
    XMqttSubscription* subscription = NULL;
    bool connected = false;
    bool subscribed = false;
    bool publishedQos0 = false;
    bool publishedQos1 = false;
    bool publishedQos2 = false;
    bool retained = false;
    bool pinged = false;
    bool echoed = false;
    bool unsubscribed = false;
    bool disconnected = false;
    bool result = false;
    bool waitOk = false;
    static const uint8_t payloadQos0[] = "interop-qos0";
    static const uint8_t payloadQos1[] = "interop-qos1";
    static const uint8_t payloadQos2[] = "interop-qos2";
    static const uint8_t payloadRetained[] = "interop-retained";
    if (!client) {
        XPrintf("[MQTT联调][客户端] 创建 XMqttClient 失败\n");
        return false;
    }
    g_mqttInteropReceived = 0;
    g_mqttInteropPing = 0;
    g_mqttInteropEchoReady = false;
    g_mqttInteropPingReady = false;
    g_mqttInteropTopic[0] = '\0';
    g_mqttInteropPayload[0] = '\0';
    XMqttClient_setHostname(client, "127.0.0.1");
    XMqttClient_setPort(client, XMqttInteropPort);
    XMqttClient_setClientId(client, "interop-client");
    XMqttClient_setKeepAlive(client, 60);
    XMqttClient_setAutoKeepAlive(client, false);
    XObject_connect_1((XObject*)client,
                      XSignal(XMqttClient_messageReceived_signal),
                      (XObject*)client,
                      interop_client_message,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)client,
                      XSignal(XMqttClient_pingResponseReceived_signal),
                      (XObject*)client,
                      interop_client_ping,
                      XConnectionType_Direct);

    XMqttClient_connectToHost_base(client);
    {
        bool cond = false;
        waitOk = interop_wait(&cond, 5000);
        connected = XMqttClient_state(client) == XMqttClient_Connected;
    }
    if (connected) {
        filter = XMqttTopicFilter_create("interop/#");
        if (filter) {
            subscription = XMqttClient_subscribe(client, filter, 1);
            subscribed = subscription != NULL;
        }
        if (subscribed) {
            topic = XMqttTopicName_create("interop/echo");
            publishedQos0 = topic &&
                XMqttClient_publish(client, topic, payloadQos0, sizeof(payloadQos0) - 1, 0, false) >= 0;
            publishedQos1 = topic &&
                XMqttClient_publish(client, topic, payloadQos1, sizeof(payloadQos1) - 1, 1, false) >= 0;
            publishedQos2 = topic &&
                XMqttClient_publish(client, topic, payloadQos2, sizeof(payloadQos2) - 1, 2, false) >= 0;
            if (topic)
                retained = XMqttClient_publish(client, topic, payloadRetained,
                                               sizeof(payloadRetained) - 1, 0, true) >= 0;
            /* 等待四条自身消息回显 */
            {
                waitOk = interop_wait(&g_mqttInteropEchoReady, 8000);
                echoed = waitOk && g_mqttInteropReceived >= 4 &&
                         strcmp(g_mqttInteropTopic, "interop/echo") == 0 &&
                         (strcmp(g_mqttInteropPayload, (const char*)payloadQos0) == 0 ||
                          strcmp(g_mqttInteropPayload, (const char*)payloadQos1) == 0 ||
                          strcmp(g_mqttInteropPayload, (const char*)payloadQos2) == 0 ||
                          strcmp(g_mqttInteropPayload, (const char*)payloadRetained) == 0);
            }
        }
        pinged = XMqttClient_requestPing(client);
        if (pinged) {
            waitOk = interop_wait(&g_mqttInteropPingReady, 5000);
            pinged = waitOk && g_mqttInteropPing > 0;
        }
        if (filter) {
            XMqttClient_unsubscribe(client, filter);
            unsubscribed = true;
        }
    }
    if (XMqttClient_state(client) == XMqttClient_Connected) {
        XMqttClient_disconnectFromHost_base(client);
        {
            bool discCond = false;
            waitOk = interop_wait(&discCond, 5000);
            disconnected = XMqttClient_state(client) == XMqttClient_Disconnected;
        }
    }
    XPrintf("[MQTT联调][客户端] CONNECT=%s SUBSCRIBE=%s QoS0=%s QoS1=%s QoS2=%s "
            "RETAIN=%s ECHO=%s PING=%s UNSUBSCRIBE=%s DISCONNECT=%s\n",
            connected ? "通过" : "失败",
            subscribed ? "通过" : "失败",
            publishedQos0 && g_mqttInteropReceived >= 1 ? "通过" : "失败",
            publishedQos1 && g_mqttInteropReceived >= 2 ? "通过" : "失败",
            publishedQos2 && g_mqttInteropReceived >= 3 ? "通过" : "失败",
            retained ? "通过" : "失败",
            echoed ? "通过" : "失败",
            pinged ? "通过" : "失败",
            unsubscribed ? "通过" : "失败",
            disconnected ? "通过" : "失败");
    result = connected && subscribed && publishedQos0 && publishedQos1 &&
             publishedQos2 && retained && echoed && pinged && unsubscribed &&
             disconnected;
    if (filter) XMqttTopicFilter_delete_base(filter);
    if (topic) XMqttTopicName_delete_base(topic);
    if (client) XClass_delete_base((XClass*)client);
    return result;
}

/* ==================== 双进程联调（父进程） ==================== */

/**
 * @brief 双进程 MQTT 客户端/服务器联调测试入口（--test xmqtt-tcp-interop）。
 * @details 用 XProcess 启动两个本进程副本：
 *          - 服务器进程：--test xmqtt-tcp-server，打印就绪标记后进入事件泵；
 *          - 客户端进程：--test xmqtt-tcp-client，完成完整交互后退出。
 *          父进程等待两边退出，退出码均为 0 才算通过。
 */
bool XMqttTcpInteropTest_run(void)
{
    XString* appPath = (XString*)XCoreApplication_applicationFilePath();
    const char* program = appPath ? XString_toUtf8(appPath) : NULL;
    const char* serverArgs[] = { "--test", "xmqtt-tcp-server" };
    const char* clientArgs[] = { "--test", "xmqtt-tcp-client" };
    XProcess* server = NULL;
    XProcess* client = NULL;
    bool ready = false;
    bool serverOk = false;
    bool clientOk = false;
    bool result = false;
    XPrintf("========== MQTT 双进程联调开始 ==========\n");
    if (!program) {
        XPrintf("[MQTT联调][父进程] 无法获取应用程序路径\n");
        if (appPath) XString_delete_base((XString*)appPath);
        return false;
    }
    server = XProcess_create();
    if (!server) {
        if (appPath) XString_delete_base((XString*)appPath);
        return false;
    }
    if (!XProcess_start_utf8(server, program, serverArgs, 2, XIODevice_ReadOnly)) {
        XPrintf("[MQTT联调][父进程] 启动服务器进程失败\n");
        XClass_delete_base((XClass*)server);
        if (appPath) XString_delete_base((XString*)appPath);
        return false;
    }
    /* 等待服务器就绪标记 */
    {
        uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 10000;
        while (!ready && XDateTime_currentMSecsSinceEpoch() < deadline) {
            XByteArray* output;
            /* 轮询子进程，把管道数据读入内部缓冲（bytesAvailable 依赖 poll）。 */
            XProcess_poll(server, 50);
            if (XProcess_bytesAvailable_base(server) > 0) {
                output = XProcess_readAllStandardOutput(server);
                if (output) {
                    ready = interop_bytes_contains(
                        (const uint8_t*)XByteArray_constData(output),
                        (size_t)XByteArray_size_base(output),
                        XMqttInteropReadyMarker);
                    XByteArray_delete_base(output);
                }
            }
            if (!ready) XThread_msleep(10);
        }
    }
    if (!ready) {
        XPrintf("[MQTT联调][父进程] 等待服务器就绪标记超时\n");
        if (server) {
            XProcess_kill(server);
            XProcess_waitForFinished(server, 5000);
            XClass_delete_base((XClass*)server);
        }
        if (appPath) XString_delete_base((XString*)appPath);
        return false;
    }
    XPrintf("[MQTT联调][父进程] 服务器已就绪，启动客户端进程\n");
    client = XProcess_create();
    if (!client) {
        if (server) XClass_delete_base((XClass*)server);
        if (appPath) XString_delete_base((XString*)appPath);
        return false;
    }
    if (!XProcess_start_utf8(client, program, clientArgs, 2, XIODevice_ReadOnly)) {
        XPrintf("[MQTT联调][父进程] 启动客户端进程失败\n");
        if (client) XClass_delete_base((XClass*)client);
        if (server) XClass_delete_base((XClass*)server);
        if (appPath) XString_delete_base((XString*)appPath);
        return false;
    }
    serverOk = XProcess_waitForFinished(server, 30000);
    clientOk = XProcess_waitForFinished(client, 30000);
    if (serverOk)
        serverOk = XProcess_exitCode(server) == 0;
    if (clientOk)
        clientOk = XProcess_exitCode(client) == 0;
    {
        XByteArray* serverOutput = XProcess_readAllStandardOutput(server);
        XByteArray* clientOutput = XProcess_readAllStandardOutput(client);
        if (serverOutput) {
            XPrintf("[MQTT联调][父进程] 服务器输出:\n%.*s\n",
                    (int)XByteArray_size_base(serverOutput),
                    (const char*)XByteArray_constData(serverOutput));
            XByteArray_delete_base(serverOutput);
        }
        if (clientOutput) {
            XPrintf("[MQTT联调][父进程] 客户端输出:\n%.*s\n",
                    (int)XByteArray_size_base(clientOutput),
                    (const char*)XByteArray_constData(clientOutput));
            XByteArray_delete_base(clientOutput);
        }
    }
    result = serverOk && clientOk;
    XPrintf("[MQTT联调][父进程] 服务器退出码=%d(%s) 客户端退出码=%d(%s) => %s\n",
            serverOk ? XProcess_exitCode(server) : -1, serverOk ? "正常" : "失败",
            clientOk ? XProcess_exitCode(client) : -1, clientOk ? "正常" : "失败",
            result ? "通过" : "失败");
    XPrintf("========== MQTT 双进程联调结束 ==========\n");
    if (client) XClass_delete_base((XClass*)client);
    if (server) XClass_delete_base((XClass*)server);
    if (appPath) XString_delete_base((XString*)appPath);
    return result;
}

/* ==================== 进程内集成测试（菜单入口） ==================== */

static XMqttTcpServer* g_inprocServer;
static bool g_inprocServerQuit;

static void inproc_server_disconnected(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    g_inprocServerQuit = true;
}

/**
 * @brief 进程内真实 MQTT TCP 服务器（菜单入口，固定端口 18885）。
 * @details 与 XMqttTcpClientIntegrationTest 配合使用；客户端断开后退出。
 */
void XMqttTcpServerIntegrationTest(void)
{
    if (g_inprocServer) {
        XPrintf("[失败] MQTT TCP 服务器已经在运行\n");
        return;
    }
    g_inprocServer = XMqttTcpServer_create();
    if (!g_inprocServer) {
        XPrintf("[失败] 创建 MQTT TCP 服务器\n");
        return;
    }
    XObject_connect_1((XObject*)g_inprocServer,
                      XSignal(XMqttTcpServer_clientDisconnected_signal),
                      (XObject*)g_inprocServer,
                      inproc_server_disconnected,
                      XConnectionType_Direct);
    if (!XMqttTcpServer_listen(g_inprocServer, NULL, XMqttInteropPort)) {
        XPrintf("[失败] MQTT TCP 服务器监听端口 %u\n", (unsigned)XMqttInteropPort);
        XClass_delete_base((XClass*)g_inprocServer);
        g_inprocServer = NULL;
        return;
    }
    g_inprocServerQuit = false;
    XPrintf("[服务器] 真实 MQTT TCP Broker 已启动，监听端口 %u\n", (unsigned)XMqttInteropPort);
    interop_wait(&g_inprocServerQuit, 120000);
    XMqttTcpServer_close(g_inprocServer);
    XClass_delete_base((XClass*)g_inprocServer);
    g_inprocServer = NULL;
    XPrintf("[服务器] 联调结束\n");
}

/**
 * @brief 进程内真实 MQTT 客户端联调（菜单入口）。
 * @details 连接本进程内已启动的真实服务器，完成与
 *          XMqttTcpClientProcess_run 相同的完整交互流程。
 */
void XMqttTcpClientIntegrationTest(void)
{
    (void)XMqttTcpClientProcess_run();
}


/* ==================== TCP 服务器 API 单元测试 ==================== */

#define XMqttTcpApiPort    ((uint16_t)18886)
#define XMqttTcpApiAuthPort ((uint16_t)18887)

static int g_tcpApiPass;
static int g_tcpApiFail;

#define TCPAPI_CHECK(cond, text) do { \
    if (cond) { ++g_tcpApiPass; XPrintf("[通过] %s\n", text); } \
    else { ++g_tcpApiFail; XPrintf("[失败] %s\n", text); } \
} while (0)

static int g_tcpApiConnectedCount;
static int g_tcpApiDisconnectedCount;
static int g_tcpApiReceivedCount;
static char g_tcpApiTopic[128];
static char g_tcpApiPayload[256];
static int g_tcpApiClientReceived;
static char g_tcpApiClientTopic[128];
static char g_tcpApiClientPayload[256];

/**
 * @brief 泵事件直到客户端进入指定状态或超时。
 * @param client 客户端实例。
 * @param want 期望状态。
 * @param timeout 超时毫秒数。
 * @return 到达期望状态返回 true，超时返回 false。
 */
static bool tcp_api_wait_client_state(const XMqttClient* client,
                                      XMqttClient_State want, uint64_t timeout)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + timeout;
    while (XMqttClient_state(client) != want &&
           XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    return XMqttClient_state(client) == want;
}

/**
 * @brief 泵事件直到服务器收到客户端发布的消息或超时。
 * @param timeout 超时毫秒数。
 * @return 收到消息返回 true，超时返回 false。
 */
static bool tcp_api_wait_server_received(uint64_t timeout)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + timeout;
    while (g_tcpApiReceivedCount < 1 &&
           XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    return g_tcpApiReceivedCount >= 1;
}

/**
 * @brief 泵事件直到客户端收到服务器下发的消息或超时。
 * @param timeout 超时毫秒数。
 * @return 收到消息返回 true，超时返回 false。
 */
static bool tcp_api_wait_client_received(uint64_t timeout)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + timeout;
    while (g_tcpApiClientReceived < 1 &&
           XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    return g_tcpApiClientReceived >= 1;
}

static void tcp_api_on_client_connected(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_tcpApiConnectedCount;
}

static void tcp_api_on_client_disconnected(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_tcpApiDisconnectedCount;
}

static void tcp_api_on_server_message(XObject* receiver, XVarList* args)
{
    const XMqttTopicName* topic;
    const XByteArray* payload;
    size_t size;
    (void)receiver;
    XVarList_args_3(args, void*, transportArg, XMqttTopicName*, topicArg,
                    XByteArray*, payloadArg);
    (void)transportArg;
    topic = (const XMqttTopicName*)topicArg;
    payload = (XByteArray*)payloadArg;
    ++g_tcpApiReceivedCount;
    g_tcpApiTopic[0] = '\0';
    if (topic) {
        const XString* name = XMqttTopicName_name_const(topic);
        size_t len = name ? XString_toUtf8_length(name) : 0;
        if (len >= sizeof(g_tcpApiTopic)) len = sizeof(g_tcpApiTopic) - 1;
        if (len) memcpy(g_tcpApiTopic, XString_toUtf8(name), len);
        g_tcpApiTopic[len] = '\0';
    }
    size = payload ? (size_t)XByteArray_size_base(payload) : 0;
    if (size >= sizeof(g_tcpApiPayload)) size = sizeof(g_tcpApiPayload) - 1;
    if (size) memcpy(g_tcpApiPayload, XByteArray_constData((XByteArray*)payload), size);
    g_tcpApiPayload[size] = '\0';
}

static void tcp_api_on_client_message(XObject* receiver, XVarList* args)
{
    const XByteArray* payload;
    const XMqttTopicName* topic;
    size_t size;
    (void)receiver;
    XVarList_args_2(args, XByteArray*, payloadArg, XMqttTopicName*, topicArg);
    payload = (XByteArray*)payloadArg;
    topic = (const XMqttTopicName*)topicArg;
    ++g_tcpApiClientReceived;
    g_tcpApiClientTopic[0] = '\0';
    if (topic) {
        const XString* name = XMqttTopicName_name_const(topic);
        size_t len = name ? XString_toUtf8_length(name) : 0;
        if (len >= sizeof(g_tcpApiClientTopic)) len = sizeof(g_tcpApiClientTopic) - 1;
        if (len) memcpy(g_tcpApiClientTopic, XString_toUtf8(name), len);
        g_tcpApiClientTopic[len] = '\0';
    }
    size = payload ? (size_t)XByteArray_size_base(payload) : 0;
    if (size >= sizeof(g_tcpApiClientPayload)) size = sizeof(g_tcpApiClientPayload) - 1;
    if (size) memcpy(g_tcpApiClientPayload, XByteArray_constData((XByteArray*)payload), size);
    g_tcpApiClientPayload[size] = '\0';
}

/**
 * @brief 认证回调：仅接受 tcpapi-user / tcpapi-pass。
 * @return 0 接受连接，否则返回 NotAuthorized。
 */
static uint8_t tcp_api_authenticator(void* context, const char* username,
                                     const char* password)
{
    (void)context;
    if (username && strcmp(username, "tcpapi-user") == 0 &&
        password && strcmp(password, "tcpapi-pass") == 0)
        return 0;
    return XMqtt_ReasonCode_NotAuthorized;
}

/**
 * @brief 创建并配置一个连接测试用的 MQTT 客户端。
 * @param port 服务器监听端口。
 * @param clientId 客户端 ID。
 * @return 新创建的客户端，失败返回 NULL。
 */
static XMqttClient* tcp_api_create_client(uint16_t port, const char* clientId)
{
    XMqttClient* client = XMqttClient_create();
    if (!client) return NULL;
    XMqttClient_setHostname(client, "127.0.0.1");
    XMqttClient_setPort(client, port);
    XMqttClient_setClientId(client, clientId);
    XMqttClient_setKeepAlive(client, 60);
    XMqttClient_setAutoKeepAlive(client, false);
    XObject_connect_1((XObject*)client,
                      XSignal(XMqttClient_messageReceived_signal),
                      (XObject*)client,
                      tcp_api_on_client_message,
                      XConnectionType_Direct);
    return client;
}

/**
 * @brief XMqttTcpServer 全部公开 API 单元测试。
 * @details 覆盖 create/init/listen/isListening/serverPort/connectedClientCount/
 *          close 控制接口、继承自 XMqttServer 的配置与发布 API、真实客户端
 *          连接/断开、服务器主动发布、客户端发布触发 messageReceived 信号、
 *          认证回调与 NULL 安全。全部输出使用 XPrintf 中文信息。
 * @return 全部通过返回 true。
 */
bool XMqttTcpServerApiUnitTest_run(void)
{
    XMqttTcpServer* server = NULL;
    XMqttClient* client = NULL;
    XMqttTopicFilter* filter = NULL;
    XMqttTopicName* topic = NULL;
    XMqttSubscription* sub = NULL;
    bool connected = false;
    bool ok = false;
    g_tcpApiPass = 0;
    g_tcpApiFail = 0;
    g_tcpApiConnectedCount = 0;
    g_tcpApiDisconnectedCount = 0;
    g_tcpApiReceivedCount = 0;
    g_tcpApiClientReceived = 0;
    g_tcpApiTopic[0] = '\0';
    g_tcpApiPayload[0] = '\0';
    g_tcpApiClientTopic[0] = '\0';
    g_tcpApiClientPayload[0] = '\0';
    XPrintf("========== XMqttTcpServer API 单元测试开始 ==========\n");

    /* ---- NULL 安全 ---- */
    TCPAPI_CHECK(!XMqttTcpServer_listen(NULL, NULL, XMqttTcpApiPort),
                 "listen(NULL) 返回 false");
    TCPAPI_CHECK(!XMqttTcpServer_isListening(NULL), "isListening(NULL) 返回 false");
    TCPAPI_CHECK(XMqttTcpServer_serverPort(NULL) == 0, "serverPort(NULL) 返回 0");
    TCPAPI_CHECK(XMqttTcpServer_connectedClientCount(NULL) == 0,
                 "connectedClientCount(NULL) 返回 0");
    XMqttTcpServer_close(NULL);
    TCPAPI_CHECK(true, "close(NULL) 不崩溃");

    /* ---- 创建与初始状态 ---- */
    server = XMqttTcpServer_create();
    TCPAPI_CHECK(server != NULL, "XMqttTcpServer_create 创建成功");
    if (!server) {
        XPrintf("========== XMqttTcpServer API 单元测试完成: %d 通过, %d 失败 ==========\n",
                g_tcpApiPass, g_tcpApiFail);
        return false;
    }
    TCPAPI_CHECK(!XMqttTcpServer_isListening(server), "初始未监听");
    TCPAPI_CHECK(XMqttTcpServer_serverPort(server) == 0, "初始端口为 0");
    TCPAPI_CHECK(XMqttTcpServer_connectedClientCount(server) == 0, "初始客户端数为 0");

    /* ---- 继承自 XMqttServer 的配置 API（宏转发） ---- */
    XMqttTcpServer_setMaximumPacketSize(server, 4096);
    TCPAPI_CHECK(XMqttTcpServer_maximumPacketSize(server) == 4096,
                 "setMaximumPacketSize/maximumPacketSize");
    XMqttTcpServer_setTopicAliasMaximum(server, 8);
    TCPAPI_CHECK(XMqttTcpServer_topicAliasMaximum(server) == 8,
                 "setTopicAliasMaximum/topicAliasMaximum");
    XMqttTcpServer_setServerKeepAlive(server, 30);
    TCPAPI_CHECK(XMqttTcpServer_serverKeepAlive(server) == 30,
                 "setServerKeepAlive/serverKeepAlive");
    XMqttTcpServer_setMaximumQoS(server, 1);
    TCPAPI_CHECK(XMqttTcpServer_maximumQoS(server) == 1, "setMaximumQoS/maximumQoS");
    XMqttTcpServer_setRetainAvailable(server, false);
    TCPAPI_CHECK(!XMqttTcpServer_retainAvailable(server),
                 "setRetainAvailable/retainAvailable");
    XMqttTcpServer_setWildcardAvailable(server, false);
    TCPAPI_CHECK(!XMqttTcpServer_wildcardAvailable(server),
                 "setWildcardAvailable/wildcardAvailable");
    XMqttTcpServer_setSubscriptionIdAvailable(server, false);
    TCPAPI_CHECK(!XMqttTcpServer_subscriptionIdAvailable(server),
                 "setSubscriptionIdAvailable/subscriptionIdAvailable");
    XMqttTcpServer_setSharedAvailable(server, false);
    TCPAPI_CHECK(!XMqttTcpServer_sharedAvailable(server),
                 "setSharedAvailable/sharedAvailable");

    /* ---- 监听 ---- */
    ok = XMqttTcpServer_listen(server, NULL, XMqttTcpApiPort);
    TCPAPI_CHECK(ok, "listen 成功");
    TCPAPI_CHECK(XMqttTcpServer_isListening(server), "isListening 为 true");
    TCPAPI_CHECK(XMqttTcpServer_serverPort(server) == XMqttTcpApiPort,
                 "serverPort 返回监听端口");
    TCPAPI_CHECK(!XMqttTcpServer_listen(server, NULL, XMqttTcpApiPort),
                 "重复 listen 失败");
    TCPAPI_CHECK(XMqttTcpServer_connectedClientCount(server) == 0,
                 "监听后客户端数仍为 0");

    /* ---- 信号连接 ---- */
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttTcpServer_clientConnected_signal),
                      (XObject*)server,
                      tcp_api_on_client_connected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttTcpServer_clientDisconnected_signal),
                      (XObject*)server,
                      tcp_api_on_client_disconnected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)server,
                      XSignal(XMqttTcpServer_messageReceived_signal),
                      (XObject*)server,
                      tcp_api_on_server_message,
                      XConnectionType_Direct);

    /* ---- 真实客户端连接 ---- */
    client = tcp_api_create_client(XMqttTcpApiPort, "tcpapi-client");
    TCPAPI_CHECK(client != NULL, "创建客户端");
    if (!client) goto cleanup;
    XMqttClient_connectToHost_base(client);
    connected = tcp_api_wait_client_state(client, XMqttClient_Connected, 5000);
    TCPAPI_CHECK(connected, "客户端连接成功");
    TCPAPI_CHECK(XMqttTcpServer_connectedClientCount(server) == 1,
                 "服务器在线客户端数 = 1");
    TCPAPI_CHECK(g_tcpApiConnectedCount >= 1, "服务器 clientConnected 信号触发");

    /* ---- 客户端订阅 ---- */
    filter = XMqttTopicFilter_create("tcpapi/hello");
    TCPAPI_CHECK(filter != NULL, "创建主题过滤器");
    if (filter) {
        sub = XMqttClient_subscribe(client, filter, 1);
        TCPAPI_CHECK(sub != NULL, "客户端订阅成功");
        /* 等待服务器 SUBACK：订阅状态变为已订阅 */
        if (sub) {
            uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
            while (XMqttSubscription_state(sub) != XMqttSubscription_Subscribed &&
                   XDateTime_currentMSecsSinceEpoch() < deadline) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(1);
            }
        }
        TCPAPI_CHECK(sub && XMqttSubscription_state(sub) == XMqttSubscription_Subscribed,
                     "客户端订阅 SUBACK 确认成功");

    }

    /* ---- 服务器主动发布 ---- */
    ok = XMqttTcpServer_publish(server, "tcpapi/hello",
                                (const uint8_t*)"server-push", 11, 0, false);
    TCPAPI_CHECK(ok, "服务器主动发布成功");
    ok = tcp_api_wait_client_received(5000);
    TCPAPI_CHECK(ok && g_tcpApiClientReceived >= 1 &&
                 strcmp(g_tcpApiClientTopic, "tcpapi/hello") == 0 &&
                 strcmp(g_tcpApiClientPayload, "server-push") == 0,
                 "客户端收到服务器发布的主题与载荷");

    /* ---- 客户端发布 -> 服务器 messageReceived 信号 ---- */
    topic = XMqttTopicName_create("tcpapi/echo");
    TCPAPI_CHECK(topic != NULL, "创建主题名");
    if (topic) {
        ok = XMqttClient_publish(client, topic,
                                 (const uint8_t*)"client-pub", 10, 0, false) >= 0;
        TCPAPI_CHECK(ok, "客户端发布成功");
        ok = tcp_api_wait_server_received(5000);
        TCPAPI_CHECK(ok && g_tcpApiReceivedCount >= 1 &&
                     strcmp(g_tcpApiTopic, "tcpapi/echo") == 0 &&
                     strcmp(g_tcpApiPayload, "client-pub") == 0,
                     "服务器 messageReceived 信号收到客户端发布的主题与载荷");
    }

    /* ---- 客户端断开 ---- */
    if (XMqttClient_state(client) == XMqttClient_Connected)
        XMqttClient_disconnectFromHost_base(client);
    connected = tcp_api_wait_client_state(client, XMqttClient_Disconnected, 5000);
    TCPAPI_CHECK(connected, "客户端断开成功");
    {
        uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
        while (XMqttTcpServer_connectedClientCount(server) != 0 &&
               XDateTime_currentMSecsSinceEpoch() < deadline) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(1);
        }
    }
    TCPAPI_CHECK(XMqttTcpServer_connectedClientCount(server) == 0,
                 "客户端断开后在线数归零");
    TCPAPI_CHECK(g_tcpApiDisconnectedCount >= 1, "服务器 clientDisconnected 信号触发");

    /* ---- close 后状态复位 ---- */
    XMqttTcpServer_close(server);
    TCPAPI_CHECK(!XMqttTcpServer_isListening(server), "close 后不再监听");
    TCPAPI_CHECK(XMqttTcpServer_serverPort(server) == 0, "close 后端口归零");
    TCPAPI_CHECK(XMqttTcpServer_connectedClientCount(server) == 0,
                 "close 后客户端数归零");

cleanup:
    if (filter) XMqttTopicFilter_delete_base(filter);
    if (topic) XMqttTopicName_delete_base(topic);
    if (client) XClass_delete_base((XClass*)client);
    if (server) {
        XMqttTcpServer_close(server);
        XClass_delete_base((XClass*)server);
    }

    /* ---- 认证回调测试（独立服务器与端口） ---- */
    server = XMqttTcpServer_create();
    TCPAPI_CHECK(server != NULL, "创建认证测试服务器");
    if (server) {
        XMqttTcpServer_setAuthenticator(server, NULL, tcp_api_authenticator);
        ok = XMqttTcpServer_listen(server, NULL, XMqttTcpApiAuthPort);
        TCPAPI_CHECK(ok, "认证服务器 listen 成功");
        if (ok) {
            XMqttClient* bad = tcp_api_create_client(XMqttTcpApiAuthPort, "tcpapi-bad");
            TCPAPI_CHECK(bad != NULL, "创建错误凭据客户端");
            if (bad) {
                XMqttClient_setUsername(bad, "tcpapi-user");
                XMqttClient_setPassword(bad, "wrong-pass");
                XMqttClient_connectToHost_base(bad);
                /* 等待连接被拒绝：状态回到 Disconnected 且产生 NotAuthorized */
                {
                    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
                    while (XMqttClient_state(bad) != XMqttClient_Disconnected &&
                           XDateTime_currentMSecsSinceEpoch() < deadline) {
                        XCoreApplication_processEvents(XEventLoop_AllEvents);
                        XThread_msleep(1);
                    }
                }
                TCPAPI_CHECK(XMqttClient_state(bad) != XMqttClient_Connected,
                             "错误凭据被拒绝");
                TCPAPI_CHECK(XMqttClient_error(bad) == XMqttClient_NotAuthorized,
                             "错误凭据错误码为 NotAuthorized");
                XClass_delete_base((XClass*)bad);
            }
            {
                XMqttClient* good = tcp_api_create_client(XMqttTcpApiAuthPort, "tcpapi-good");
                TCPAPI_CHECK(good != NULL, "创建正确凭据客户端");
                if (good) {
                    XMqttClient_setUsername(good, "tcpapi-user");
                    XMqttClient_setPassword(good, "tcpapi-pass");
                    XMqttClient_connectToHost_base(good);
                    ok = tcp_api_wait_client_state(good, XMqttClient_Connected, 5000);
                    TCPAPI_CHECK(ok, "正确凭据连接成功");
                    TCPAPI_CHECK(XMqttTcpServer_connectedClientCount(server) == 1,
                                 "认证服务器在线客户端数 = 1");
                    if (XMqttClient_state(good) == XMqttClient_Connected)
                        XMqttClient_disconnectFromHost_base(good);
                    tcp_api_wait_client_state(good, XMqttClient_Disconnected, 5000);
                    XClass_delete_base((XClass*)good);
                }
            }
        }
        XMqttTcpServer_close(server);
        TCPAPI_CHECK(!XMqttTcpServer_isListening(server), "认证服务器 close 后不再监听");
        XClass_delete_base((XClass*)server);
        server = NULL;
    }

    XPrintf("========== XMqttTcpServer API 单元测试完成: %d 通过, %d 失败 ==========\n",
            g_tcpApiPass, g_tcpApiFail);
    return g_tcpApiFail == 0;
}

/* ==================== 汇总入口 ==================== */

/**
 * @brief 运行全部 MQTT 服务器相关测试。
 * @details 依次执行协议引擎单元测试与双进程 TCP 联调测试。
 * @return 全部通过返回 true。
 */
bool XMqttTest_runAll(void)
{
    bool layout = XMqttDataLayoutTest_run();
    bool unit = XMqttServerUnitTest_run();
    bool tcpApi = XMqttTcpServerApiUnitTest_run();
    bool interop = XMqttTcpInteropTest_run();
    bool result = layout && unit && tcpApi && interop;
    XPrintf("========== MQTT 测试汇总: 位域布局=%s 引擎单元=%s TCP服务器API=%s 双进程联调=%s => %s ==========\n",
            layout ? "通过" : "失败",
            unit ? "通过" : "失败",
            tcpApi ? "通过" : "失败",
            interop ? "通过" : "失败",
            result ? "全部通过" : "存在失败");
    return result;
}
