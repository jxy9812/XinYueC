#include "XProtocolStackTest.h"
#include "XMqttClient.h"
#include "XMqttTopicFilter.h"
#include "XMqttTopicName.h"
#include "XMqttSubscription.h"
#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XAbstractSocket.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XEventLoop.h"
#include "XObject.h"
#include "XThread.h"
#include "XVarList.h"
#include "XPrintf.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Qt 6.8 QtMqtt is client-side only; this is a test-only minimal broker. */
#define XMqttTcpIntegrationPort ((uint16_t)18884)

static XTcpServer* g_mqttTcpServer;
static XTcpSocket* g_mqttTcpPeer;
static XByteArray* g_mqttTcpInput;
static XEventLoop* g_mqttTcpServerLoop;

static void mqtt_tcp_send(const uint8_t* data, size_t size)
{
    if (g_mqttTcpPeer && data && size)
        XIODevice_write_1((XIODevice*)g_mqttTcpPeer, (const char*)data, (int64_t)size);
}

static void mqtt_tcp_send_publish(const uint8_t* topic, size_t topicSize,
                                  const uint8_t* payload, size_t payloadSize)
{
    uint8_t packet[256];
    size_t bodySize = 2 + topicSize + payloadSize;
    size_t pos = 0;
    if (!topic || topicSize > 80 || payloadSize > 160 || bodySize > 245)
        return;
    packet[pos++] = 0x30;
    packet[pos++] = (uint8_t)bodySize;
    packet[pos++] = (uint8_t)(topicSize >> 8);
    packet[pos++] = (uint8_t)topicSize;
    memcpy(packet + pos, topic, topicSize);
    pos += topicSize;
    if (payloadSize) {
        memcpy(packet + pos, payload, payloadSize);
        pos += payloadSize;
    }
    mqtt_tcp_send(packet, pos);
}

static bool mqtt_tcp_packet_length(const uint8_t* data, size_t size,
                                   size_t* headerSize, size_t* bodySize)
{
    size_t pos = 1;
    size_t multiplier = 1;
    size_t value = 0;
    uint8_t byte;
    if (!data || size < 2 || !headerSize || !bodySize)
        return false;
    do {
        if (pos >= size || pos > 4)
            return false;
        byte = data[pos++];
        value += (size_t)(byte & 0x7f) * multiplier;
        multiplier *= 128;
    } while (byte & 0x80);
    *headerSize = pos;
    *bodySize = value;
    return true;
}

static void mqtt_tcp_server_process(void)
{
    static const uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
    static const uint8_t pingresp[] = {0xd0, 0x00};
    while (g_mqttTcpInput && XByteArray_size_base(g_mqttTcpInput) >= 2) {
        const uint8_t* data = (const uint8_t*)XByteArray_constData(g_mqttTcpInput);
        size_t available = XByteArray_size_base(g_mqttTcpInput);
        size_t headerSize = 0;
        size_t bodySize = 0;
        size_t packetSize;
        uint8_t type;
        if (!mqtt_tcp_packet_length(data, available, &headerSize, &bodySize))
            return;
        packetSize = headerSize + bodySize;
        if (available < packetSize)
            return;
        type = data[0] & 0xf0;
        if (type == 0x10) {
            mqtt_tcp_send(connack, sizeof(connack));
        } else if (type == 0x80 && bodySize >= 4) {
            uint8_t response[5] = {0x90, 0x03, data[headerSize], data[headerSize + 1], 0x00};
            mqtt_tcp_send(response, sizeof(response));
        } else if (type == 0x30 && bodySize >= 2) {
            const uint8_t* body = data + headerSize;
            size_t topicSize = ((size_t)body[0] << 8) | body[1];
            size_t topicEnd = 2 + topicSize;
            uint8_t qos = (uint8_t)((data[0] >> 1) & 0x03);
            if (topicSize && topicEnd <= bodySize && topicSize < 80) {
                const uint8_t* payload = body + topicEnd;
                size_t payloadSize = bodySize - topicEnd;
                if (qos == 1 && payloadSize >= 2) {
                    uint8_t ack[] = {0x40, 0x02, payload[0], payload[1]};
                    mqtt_tcp_send(ack, sizeof(ack));
                    payload += 2;
                    payloadSize -= 2;
                }
                mqtt_tcp_send_publish(body + 2, topicSize, payload, payloadSize);
            }
        } else if (type == 0xc0) {
            mqtt_tcp_send(pingresp, sizeof(pingresp));
        } else if (type == 0xe0) {
            /* The disconnected signal below owns event-loop shutdown. */
        }
        XByteArray_remove_base(g_mqttTcpInput, 0, (int64_t)packetSize);
    }
}

static void mqtt_tcp_peer_ready_read(XObject* receiver, XVarList* args)
{
    XByteArray* bytes;
    (void)receiver;
    (void)args;
    if (!g_mqttTcpPeer || !g_mqttTcpInput)
        return;
    bytes = XIODevice_readAll_3((XIODevice*)g_mqttTcpPeer);
    if (bytes) {
        XByteArray_push_back_3(g_mqttTcpInput, (XVector*)bytes);
        XByteArray_delete_base(bytes);
    }
    mqtt_tcp_server_process();
}

static void mqtt_tcp_peer_disconnected(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (g_mqttTcpServerLoop)
        XEventLoop_quit(g_mqttTcpServerLoop);
}

static void mqtt_tcp_server_new_connection(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (!g_mqttTcpServer)
        return;
    g_mqttTcpPeer = XTcpServer_nextPendingConnection_base(g_mqttTcpServer);
    if (!g_mqttTcpPeer)
        return;
    XObject_connect_1((XObject*)g_mqttTcpPeer,
                      XSignal(XIODevice_readyRead_signal),
                      (XObject*)g_mqttTcpPeer,
                      mqtt_tcp_peer_ready_read,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)g_mqttTcpPeer,
                      XSignal(XAbstractSocket_disconnected_signal),
                      (XObject*)g_mqttTcpPeer,
                      mqtt_tcp_peer_disconnected,
                      XConnectionType_Direct);
}

void XMqttTcpServerIntegrationTest(void)
{
    int result;
    XEventLoop serverLoop;
    if (g_mqttTcpServer) {
        XPrintf("[失败] MQTT TCP 服务器已经在运行\n");
        return;
    }
    g_mqttTcpInput = XByteArray_create();
    g_mqttTcpServer = XTcpServer_create();
    if (!g_mqttTcpInput || !g_mqttTcpServer) {
        XPrintf("[失败] 创建 MQTT TCP 联调资源\n");
        if (g_mqttTcpInput) XByteArray_delete_base(g_mqttTcpInput);
        if (g_mqttTcpServer) XClass_delete_base((XClass*)g_mqttTcpServer);
        g_mqttTcpInput = NULL;
        g_mqttTcpServer = NULL;
        return;
    }
    XObject_connect_1((XObject*)g_mqttTcpServer,
                      XSignal(XTcpServer_newConnection_signal),
                      (XObject*)g_mqttTcpServer,
                      mqtt_tcp_server_new_connection,
                      XConnectionType_Direct);
    if (!XTcpServer_listen(g_mqttTcpServer, NULL, XMqttTcpIntegrationPort)) {
        XPrintf("[失败] MQTT TCP 服务器监听端口 %u\n", XMqttTcpIntegrationPort);
        XClass_delete_base((XClass*)g_mqttTcpServer);
        XByteArray_delete_base(g_mqttTcpInput);
        g_mqttTcpServer = NULL;
        g_mqttTcpInput = NULL;
        return;
    }
    XPrintf("[服务器] MQTT TCP Broker 已启动，监听 127.0.0.1:%u\n", XMqttTcpIntegrationPort);
    XPrintf("[服务器] 请在另一个 XinYueC 进程选择“真实 TCP MQTT 客户端联调”\n");
    XEventLoop_init(&serverLoop);
    g_mqttTcpServerLoop = &serverLoop;
    result = XEventLoop_exec(&serverLoop, XEventLoop_ApplicationExec);
    g_mqttTcpServerLoop = NULL;
    XPrintf("[服务器] 联调事件循环结束，返回码 %d\n", result);
    XTcpServer_close(g_mqttTcpServer);
    if (g_mqttTcpPeer)
        XTcpSocket_deleteLater(g_mqttTcpPeer);
    XClass_delete_base((XClass*)g_mqttTcpServer);
    XByteArray_delete_base(g_mqttTcpInput);
    g_mqttTcpPeer = NULL;
    g_mqttTcpServer = NULL;
    g_mqttTcpInput = NULL;
}

static int g_mqttTcpReceived;
static int g_mqttTcpPingReceived;
static char g_mqttTcpTopic[80];
static char g_mqttTcpPayload[160];

static void mqtt_tcp_client_message(XObject* receiver, XVarList* args)
{
    const XString* name;
    size_t payloadSize;
    (void)receiver;
    XVarList_args_2(args, XByteArray*, payloadArg, XMqttTopicName*, topicArg);
    name = topicArg ? XMqttTopicName_name_const(topicArg) : NULL;
    payloadSize = payloadArg ? XByteArray_size_base(payloadArg) : 0;
    if (name)
        strncpy(g_mqttTcpTopic, XString_toUtf8(name), sizeof(g_mqttTcpTopic) - 1);
    g_mqttTcpTopic[sizeof(g_mqttTcpTopic) - 1] = '\0';
    if (payloadSize >= sizeof(g_mqttTcpPayload))
        payloadSize = sizeof(g_mqttTcpPayload) - 1;
    if (payloadSize)
        memcpy(g_mqttTcpPayload, XByteArray_constData((XByteArray*)payloadArg), payloadSize);
    g_mqttTcpPayload[payloadSize] = '\0';
    g_mqttTcpReceived++;
}

static void mqtt_tcp_client_ping(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    g_mqttTcpPingReceived++;
}

static bool mqtt_tcp_client_wait(XMqttClient* client, XMqttClient_State state, uint64_t timeout)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + timeout;
    while (XMqttClient_state(client) != state && XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    return XMqttClient_state(client) == state;
}

void XMqttTcpClientIntegrationTest(void)
{
    XMqttClient* client = XMqttClient_create();
    XMqttTopicFilter* filter = NULL;
    XMqttTopicName* topic = NULL;
    XMqttSubscription* subscription = NULL;
    bool connected = false;
    bool subscribed = false;
    bool published = false;
    bool pinged = false;
    bool disconnected = false;
    static const uint8_t payload[] = "hello-from-tcp";
    if (!client) {
        XPrintf("[失败] 创建 MQTT TCP 客户端\n");
        return;
    }
    g_mqttTcpReceived = 0;
    g_mqttTcpPingReceived = 0;
    g_mqttTcpTopic[0] = '\0';
    g_mqttTcpPayload[0] = '\0';
    XMqttClient_setHostname(client, "127.0.0.1");
    XMqttClient_setPort(client, XMqttTcpIntegrationPort);
    XMqttClient_setAutoKeepAlive(client, false);
    XObject_connect_1((XObject*)client,
                      XSignal(XMqttClient_messageReceived_signal),
                      (XObject*)client,
                      mqtt_tcp_client_message,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)client,
                      XSignal(XMqttClient_pingResponseReceived_signal),
                      (XObject*)client,
                      mqtt_tcp_client_ping,
                      XConnectionType_Direct);
    XMqttClient_connectToHost_base(client);
    connected = mqtt_tcp_client_wait(client, XMqttClient_Connected, 5000);
    if (connected) {
        filter = XMqttTopicFilter_create("demo/#");
        subscription = filter ? XMqttClient_subscribe(client, filter, 0) : NULL;
        subscribed = subscription != NULL;
        if (subscribed) {
            topic = XMqttTopicName_create("demo/tcp");
            published = topic && XMqttClient_publish(client, topic, payload,
                                                       sizeof(payload) - 1, 0, false) >= 0;
            if (published) {
                uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
                while (!g_mqttTcpReceived && XDateTime_currentMSecsSinceEpoch() < deadline) {
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                    XThread_msleep(1);
                }
            }
            pinged = XMqttClient_requestPing(client);
            if (pinged) {
                uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + 5000;
                while (!g_mqttTcpPingReceived && XDateTime_currentMSecsSinceEpoch() < deadline) {
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                    XThread_msleep(1);
                }
                pinged = g_mqttTcpPingReceived > 0;
            }
        }
    }
    disconnected = XMqttClient_state(client) == XMqttClient_Connected;
    XMqttClient_disconnectFromHost_base(client);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XPrintf("[客户端] CONNECT=%s SUBSCRIBE=%s PUBLISH回显=%s PING=%s DISCONNECT=%s\n",
            connected ? "通过" : "失败",
            subscribed ? "通过" : "失败",
            published && g_mqttTcpReceived && strcmp(g_mqttTcpTopic, "demo/tcp") == 0 &&
                strcmp(g_mqttTcpPayload, (const char*)payload) == 0 ? "通过" : "失败",
            pinged ? "通过" : "失败",
            disconnected ? "通过" : "失败");
    if (filter) XMqttTopicFilter_delete_base(filter);
    if (topic) XMqttTopicName_delete_base(topic);
    XClass_delete_base((XClass*)client);
}
