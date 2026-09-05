#include "XProtocolTest.h"
#include "XMqttClient.h"
#include "XMqttTopicName.h"
#include "XMqttTopicFilter.h"
#include "XMqttPublishProperties.h"
#include "XMqttConnectionProperties.h"
#include "XIODevice.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>

XCLASS_DEFINE_BEGING(XMqttMockDevice)
XCLASS_DEFINE_EXTEND_END(XMqttMockDevice, XIODevice)

typedef struct XMqttMockDevice {
    XIODevice m_base;
    XByteArray* input;
    XByteArray* output;
    size_t readOffset;
} XMqttMockDevice;

static bool mqtt_test_append(XByteArray* buffer, const void* data, size_t size)
{
    return buffer && (!size || XVector_push_back_2((XVector*)buffer, data, size));
}

static int64_t mqtt_mock_bytes_available(const XIODevice* device)
{
    const XMqttMockDevice* mock = (const XMqttMockDevice*)device;
    size_t size = mock && mock->input ? XByteArray_size_base(mock->input) : 0;
    return size > mock->readOffset ? (int64_t)(size - mock->readOffset) : 0;
}

static int64_t mqtt_mock_read(XIODevice* device, char* data, int64_t maxlen)
{
    XMqttMockDevice* mock = (XMqttMockDevice*)device;
    size_t available;
    size_t count;
    if (!mock || !data || maxlen <= 0) return -1;
    available = (size_t)mqtt_mock_bytes_available(device);
    count = available < (size_t)maxlen ? available : (size_t)maxlen;
    if (!count) return 0;
    memcpy(data, (const uint8_t*)XByteArray_constData(mock->input) + mock->readOffset,
           count);
    mock->readOffset += count;
    return (int64_t)count;
}

static int64_t mqtt_mock_write(XIODevice* device, const char* data, int64_t len)
{
    XMqttMockDevice* mock = (XMqttMockDevice*)device;
    if (!mock || !data || len < 0 ||
        !mqtt_test_append(mock->output, data, (size_t)len)) return -1;
    return len;
}

static void mqtt_mock_deinit(XMqttMockDevice* mock)
{
    if (!mock) return;
    if (mock->input) XByteArray_delete_base(mock->input);
    if (mock->output) XByteArray_delete_base(mock->output);
    XClass_Deinit_Parent(XIODevice, mock);
}

static XVtable* mqtt_mock_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttMockDevice)
    XVTABLE_INHERIT_XCLASS(XIODevice);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, mqtt_mock_bytes_available);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, mqtt_mock_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, mqtt_mock_write);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, mqtt_mock_deinit);
    return XVTABLE_DEFAULT;
}

static XMqttMockDevice* mqtt_mock_create_ex(XMemoryType memory)
{
    XMqttMockDevice* mock = (XMqttMockDevice*)XMemory_malloc(sizeof(XMqttMockDevice), memory);
    if (!mock) return NULL;
    memset(mock, 0, sizeof(*mock));
    XIODevice_init(&mock->m_base);
    XClassGetVtable(mock) = mqtt_mock_class_init();
    mock->input = XByteArray_create();
    mock->output = XByteArray_create();
    Set_Class_Memory(mock, memory); Set_Class_IsHeap(mock, true);
    if (!mock->input || !mock->output ||
        !XIODevice_open_base((XIODevice*)mock, XIODevice_ReadWrite)) {
        XClass_delete_base((XClass*)mock);
        return NULL;
    }
    return mock;
}

static XMqttMockDevice* mqtt_mock_create(void)
{
    return mqtt_mock_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
}

static bool mqtt_mock_feed(XMqttMockDevice* mock, const uint8_t* data, size_t size)
{
    if (!mock || !mqtt_test_append(mock->input, data, size)) return false;
    XIODevice_readyRead_signal((XIODevice*)mock);
    return true;
}

static bool mqtt_mock_output_ends_with(const XMqttMockDevice* mock,
                                       const uint8_t* suffix, size_t size)
{
    size_t total = mock && mock->output ? XByteArray_size_base(mock->output) : 0;
    const uint8_t* data = mock && mock->output ?
        (const uint8_t*)XByteArray_constData(mock->output) : NULL;
    return data && total >= size && memcmp(data + total - size, suffix, size) == 0;
}

static int mqtt_received_count;
static int mqtt_subscription_received_count;
static int mqtt_sent_count;
static int mqtt_ping_count;
static char mqtt_received_topic[64];
static char mqtt_received_payload[64];

static void mqtt_test_received(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, XByteArray*, payload, XMqttTopicName*, topic);
    const XString* name = XMqttTopicName_name_const(topic);
    size_t size = payload ? XByteArray_size_base(payload) : 0;
    mqtt_received_count++;
    if (name) {
        strncpy(mqtt_received_topic, XString_toUtf8(name), sizeof(mqtt_received_topic) - 1);
        mqtt_received_topic[sizeof(mqtt_received_topic) - 1] = '\0';
    }
    if (size >= sizeof(mqtt_received_payload)) size = sizeof(mqtt_received_payload) - 1;
    if (size) memcpy(mqtt_received_payload, XByteArray_constData(payload), size);
    mqtt_received_payload[size] = '\0';
}

static void mqtt_test_subscription_received(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    mqtt_subscription_received_count++;
}

static void mqtt_test_sent(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    mqtt_sent_count++;
}

static void mqtt_test_ping(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    mqtt_ping_count++;
}

int XMqttPublicApiTest_run(void)
{
    int passed = 0;
    int failed = 0;
#define MQTT_CHECK(condition, text) do { \
    if (condition) { XPrintf("  [通过] %s\n", text); passed++; } \
    else { XPrintf("  [失败] %s\n", text); failed++; } \
} while (0)

    XPrintf("========== XMqtt Qt 6.8 公开 API 回归开始 ==========\n");

    XMqttTopicName* empty = XMqttTopicName_create(NULL);
    XMqttTopicName* levelsName = XMqttTopicName_create("/a//");
    XVector* levels = XMqttTopicName_levels(levelsName);
    MQTT_CHECK(empty && XMqttTopicName_name_const(empty) &&
               !XMqttTopicName_isValid(empty), "空主题保持非 NULL 值语义且无效");
    XString** level0 = levels ? (XString**)XVector_at_base(levels, 0) : NULL;
    XString** level2 = levels ? (XString**)XVector_at_base(levels, 2) : NULL;
    XString** level3 = levels ? (XString**)XVector_at_base(levels, 3) : NULL;
    MQTT_CHECK(levels && XVector_size_base(levels) == 4 &&
               level0 && level2 && level3 &&
               XString_length_base(*level0) == 0 &&
               XString_length_base(*level2) == 0 &&
               XString_length_base(*level3) == 0,
               "主题层级保留前导、连续和尾随空层级");
    if (levels) XVector_delete_base(levels);
    XMqttTopicName_delete_base(empty);
    XMqttTopicName_delete_base(levelsName);

    XMqttTopicFilter* hash = XMqttTopicFilter_create("sport/#");
    XMqttTopicName* parent = XMqttTopicName_create("sport");
    XMqttTopicFilter* dollar = XMqttTopicFilter_create("#");
    XMqttTopicName* systemTopic = XMqttTopicName_create("$SYS/status");
    XMqttTopicFilter* invalidHash = XMqttTopicFilter_create("sport/#/rank");
    MQTT_CHECK(XMqttTopicFilter_match(hash, parent, XMqttTopicFilter_NoMatchOption),
               "多层通配符同时匹配父层级");
    MQTT_CHECK(!XMqttTopicFilter_match(dollar, systemTopic,
        XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption),
        "美元主题通配符选项与 Qt 一致");
    MQTT_CHECK(!XMqttTopicFilter_isValid(invalidHash), "非法多层通配符被拒绝");
    XMqttTopicFilter_delete_base(hash);
    XMqttTopicName_delete_base(parent);
    XMqttTopicFilter_delete_base(dollar);
    XMqttTopicName_delete_base(systemTopic);
    XMqttTopicFilter_delete_base(invalidHash);

    XMqttConnectionProperties* cp = XMqttConnectionProperties_create();
    MQTT_CHECK(cp && XMqttConnectionProperties_maximumReceive(cp) == UINT16_MAX &&
        XMqttConnectionProperties_maximumPacketSize(cp) == UINT32_MAX &&
        XMqttConnectionProperties_requestProblemInformation(cp),
        "连接属性默认值与 Qt 6.8 一致");
    XMqttConnectionProperties_setMaximumReceive(cp, 20);
    XMqttConnectionProperties_setMaximumReceive(cp, 0);
    XMqttConnectionProperties_setMaximumPacketSize(cp, 1000);
    XMqttConnectionProperties_setMaximumPacketSize(cp, 0);
    MQTT_CHECK(XMqttConnectionProperties_maximumReceive(cp) == 20 &&
        XMqttConnectionProperties_maximumPacketSize(cp) == 1000,
        "连接属性拒绝协议禁止的零值");
    XMqttConnectionProperties_delete_base(cp);

    XMqttPublishProperties* pp = XMqttPublishProperties_create();
    XMqttPublishProperties_setTopicAlias(pp, 2);
    XMqttPublishProperties_setTopicAlias(pp, 0);
    MQTT_CHECK(XMqttPublishProperties_topicAlias(pp) == 2,
               "发布主题别名拒绝零值");
    XMqttPublishProperties_delete_base(pp);

    XMqttServerConnectionProperties* sp = XMqttServerConnectionProperties_create();
    MQTT_CHECK(sp && !XMqttServerConnectionProperties_isValid(sp) &&
        XMqttServerConnectionProperties_maximumQoS(sp) == 2 &&
        XMqttServerConnectionProperties_retainAvailable(sp) &&
        XMqttServerConnectionProperties_wildcardSupported(sp) &&
        XMqttServerConnectionProperties_subscriptionIdentifierSupported(sp) &&
        XMqttServerConnectionProperties_sharedSubscriptionSupported(sp),
        "服务端连接属性默认能力与有效标志正确");
    XMqttServerConnectionProperties_delete_base(sp);

    XMqttClient* client = XMqttClient_create();
    XMqttMockDevice* mock = mqtt_mock_create();
    MQTT_CHECK(client && mock && XMqttClient_port(client) == 0 &&
        XMqttClient_clientId_const(client) &&
        XString_length_base(XMqttClient_clientId_const(client)) == 23,
        "客户端默认端口和随机 Client ID 与 Qt 6.8 一致");
    if (!client || !mock) {
        if (client) XClass_delete_base((XClass*)client);
        if (mock) XClass_delete_base((XClass*)mock);
        return 1;
    }
    XMqttClient_setError(client, XMqttClient_TransportInvalid);
    MQTT_CHECK(XMqttClient_error(client) == XMqttClient_TransportInvalid,
               "大于 255 的客户端错误码不再截断");
    XMqttClient_setError(client, XMqttClient_NoError);
    XMqttClient_setAutoKeepAlive(client, false);
    XMqttClient_setTransport(client, mock, XMqttClient_IODevice);
    XMqttClient_connectToHost_base(client);
    MQTT_CHECK(XMqttClient_state(client) == XMqttClient_Connecting &&
        XByteArray_size_base(mock->output) > 2 &&
        ((const uint8_t*)XByteArray_constData(mock->output))[0] == 0x10,
        "自定义 XIODevice 发出真实 CONNECT 报文");

    {
        const uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
        mqtt_mock_feed(mock, connack, sizeof(connack));
    }
    MQTT_CHECK(XMqttClient_state(client) == XMqttClient_Connected &&
        XMqttServerConnectionProperties_isValid(
            XMqttClient_serverConnectionProperties_const(client)),
        "CONNACK 驱动连接状态与服务端属性有效标志");
    XMqttClient_setHostname(client, "should-not-change");
    MQTT_CHECK(XString_toUtf8_length(XMqttClient_hostname_const(client)) == 0,
               "连接期间拒绝修改连接配置");

    mqtt_received_count = mqtt_subscription_received_count = 0;
    mqtt_sent_count = mqtt_ping_count = 0;
    memset(mqtt_received_topic, 0, sizeof(mqtt_received_topic));
    memset(mqtt_received_payload, 0, sizeof(mqtt_received_payload));
    XObject_connect_1((XObject*)client, XSignal(XMqttClient_messageReceived_signal),
                      (XObject*)client, mqtt_test_received, XConnectionType_Direct);
    XObject_connect_1((XObject*)client, XSignal(XMqttClient_messageSent_signal),
                      (XObject*)client, mqtt_test_sent, XConnectionType_Direct);
    XObject_connect_1((XObject*)client, XSignal(XMqttClient_pingResponseReceived_signal),
                      (XObject*)client, mqtt_test_ping, XConnectionType_Direct);

    XMqttTopicFilter* filter = XMqttTopicFilter_create("test/#");
    XMqttSubscription* subscription = XMqttClient_subscribe(client, filter, 1);
    MQTT_CHECK(subscription && XMqttSubscription_state(subscription) ==
        XMqttSubscription_SubscriptionPending, "订阅在 SUBACK 前保持 Pending");
    if (subscription)
        XObject_connect_1((XObject*)subscription,
            XSignal(XMqttSubscription_messageReceived_signal), (XObject*)client,
            mqtt_test_subscription_received, XConnectionType_Direct);
    {
        const uint8_t suback[] = {0x90, 0x03, 0x00, 0x01, 0x01};
        mqtt_mock_feed(mock, suback, sizeof(suback));
    }
    MQTT_CHECK(subscription && XMqttSubscription_state(subscription) ==
        XMqttSubscription_Subscribed && XMqttSubscription_qos(subscription) == 1,
        "SUBACK 更新订阅状态和授权 QoS");

    {
        const uint8_t incoming[] = {
            0x30, 0x0A, 0x00, 0x06, 't', 'e', 's', 't', '/', 'x', 'o', 'k'
        };
        mqtt_mock_feed(mock, incoming, sizeof(incoming));
    }
    MQTT_CHECK(mqtt_received_count == 1 && mqtt_subscription_received_count == 1 &&
        strcmp(mqtt_received_topic, "test/x") == 0 &&
        strcmp(mqtt_received_payload, "ok") == 0,
        "入站 PUBLISH 同时分发给客户端和匹配订阅");

    XMqttTopicName* publishTopic = XMqttTopicName_create("test/out");
    int32_t qos1 = XMqttClient_publish(client, publishTopic,
                                       (const uint8_t*)"one", 3, 1, false);
    MQTT_CHECK(qos1 == 2, "QoS 1 发布分配报文标识符");
    {
        const uint8_t puback[] = {0x40, 0x02, 0x00, 0x02};
        mqtt_mock_feed(mock, puback, sizeof(puback));
    }
    MQTT_CHECK(mqtt_sent_count == 1, "PUBACK 完成 QoS 1 发布");

    int32_t qos2 = XMqttClient_publish(client, publishTopic,
                                       (const uint8_t*)"two", 3, 2, false);
    {
        const uint8_t pubrec[] = {0x50, 0x02, 0x00, 0x03};
        const uint8_t pubrel[] = {0x62, 0x02, 0x00, 0x03};
        mqtt_mock_feed(mock, pubrec, sizeof(pubrec));
        MQTT_CHECK(qos2 == 3 && mqtt_mock_output_ends_with(mock, pubrel, sizeof(pubrel)),
                   "PUBREC 触发 QoS 2 PUBREL");
    }
    {
        const uint8_t pubcomp[] = {0x70, 0x02, 0x00, 0x03};
        mqtt_mock_feed(mock, pubcomp, sizeof(pubcomp));
    }
    MQTT_CHECK(mqtt_sent_count == 2, "PUBCOMP 完成 QoS 2 发布");

    MQTT_CHECK(XMqttClient_requestPing(client), "手动保活关闭自动模式后可发 PINGREQ");
    {
        const uint8_t pingresp[] = {0xD0, 0x00};
        mqtt_mock_feed(mock, pingresp, sizeof(pingresp));
    }
    MQTT_CHECK(mqtt_ping_count == 1, "PINGRESP 发出响应信号");

    XMqttClient_unsubscribe(client, filter);
    MQTT_CHECK(subscription && XMqttSubscription_state(subscription) ==
        XMqttSubscription_UnsubscriptionPending, "取消订阅在 UNSUBACK 前保持 Pending");
    {
        const uint8_t unsuback[] = {0xB0, 0x02, 0x00, 0x04};
        mqtt_mock_feed(mock, unsuback, sizeof(unsuback));
    }
    MQTT_CHECK(subscription && XMqttSubscription_state(subscription) ==
        XMqttSubscription_Unsubscribed, "UNSUBACK 完成取消订阅");

    XMqttClient_disconnectFromHost_base(client);
    MQTT_CHECK(XMqttClient_state(client) == XMqttClient_Disconnected,
               "主动断开回到 Disconnected");

    XMqttTopicName_delete_base(publishTopic);
    XMqttTopicFilter_delete_base(filter);
    XClass_delete_base((XClass*)client);
    XClass_delete_base((XClass*)mock);

    /* MQTT 5.0 属性、分片和主题别名。 */
    client = XMqttClient_create();
    mock = mqtt_mock_create();
    cp = XMqttConnectionProperties_create();
    XMqttConnectionProperties_setMaximumTopicAlias(cp, 2);
    XMqttClient_setConnectionProperties(client, cp);
    XMqttConnectionProperties_delete_base(cp);
    XMqttClient_setProtocolVersion(client, XMqttClient_MQTT_5_0);
    XMqttClient_setClientId(client, "");
    XMqttClient_setAutoKeepAlive(client, false);
    XMqttClient_setTransport(client, mock, XMqttClient_IODevice);
    XMqttClient_connectToHost_base(client);
    {
        const uint8_t connack5[] = {
            0x20, 0x13, 0x00, 0x00, 0x10,
            0x12, 0x00, 0x03, 's', 'r', 'v',
            0x24, 0x01, 0x25, 0x00,
            0x22, 0x00, 0x02, 0x13, 0x00, 0x1e
        };
        mqtt_mock_feed(mock, connack5, 2);
        MQTT_CHECK(XMqttClient_state(client) == XMqttClient_Connecting,
                   "分片 CONNACK 未完整时保持 Connecting");
        mqtt_mock_feed(mock, connack5 + 2, sizeof(connack5) - 2);
    }
    sp = (XMqttServerConnectionProperties*)
        XMqttClient_serverConnectionProperties_const(client);
    MQTT_CHECK(XMqttClient_state(client) == XMqttClient_Connected && sp &&
        XMqttServerConnectionProperties_clientIdAssigned(sp) &&
        XString_equals_utf8(XMqttClient_clientId_const(client), "srv",
                            XChar_CaseSensitive) &&
        XMqttServerConnectionProperties_maximumQoS(sp) == 1 &&
        !XMqttServerConnectionProperties_retainAvailable(sp) &&
        XMqttServerConnectionProperties_serverKeepAlive(sp) == 30,
        "MQTT 5 CONNACK 属性和服务端分配 Client ID 正确");
    {
        XMqttTopicFilter* m5Filter = XMqttTopicFilter_create("m5/#");
        XMqttSubscription* m5Sub = XMqttClient_subscribe(client, m5Filter, 1);
        const uint8_t m5Suback[] = {0x90, 0x04, 0x00, 0x01, 0x01, 0x00};
        mqtt_mock_feed(mock, m5Suback, sizeof(m5Suback));
        MQTT_CHECK(m5Sub && XMqttSubscription_state(m5Sub) == XMqttSubscription_Subscribed,
                   "MQTT 5 SUBACK 先读原因码再读属性长度");
        XMqttTopicFilter_delete_base(m5Filter);
    }

    mqtt_received_count = 0;
    XObject_connect_1((XObject*)client, XSignal(XMqttClient_messageReceived_signal),
                      (XObject*)client, mqtt_test_received, XConnectionType_Direct);
    publishTopic = XMqttTopicName_create("m5/out");
    XByteArray_clear_base(mock->output);
    MQTT_CHECK(XMqttClient_publish(client, publishTopic,
                                   (const uint8_t*)"a", 1, 0, false) == 0,
               "MQTT 5 首次发布建立自动主题别名");
    XByteArray_clear_base(mock->output);
    MQTT_CHECK(XMqttClient_publish(client, publishTopic,
                                   (const uint8_t*)"b", 1, 0, false) == 0 &&
        XByteArray_size_base(mock->output) > 6 &&
        ((const uint8_t*)XByteArray_constData(mock->output))[2] == 0 &&
        ((const uint8_t*)XByteArray_constData(mock->output))[3] == 0,
        "MQTT 5 重复主题发布省略主题名并复用别名");
    {
        const uint8_t incomingAlias[] = {
            0x30, 0x0c, 0x00, 0x05, 'm', '5', '/', 'i', 'n',
            0x03, 0x23, 0x00, 0x01, 'A'
        };
        const uint8_t incomingAliasOnly[] = {
            0x30, 0x07, 0x00, 0x00, 0x03, 0x23, 0x00, 0x01, 'B'
        };
        mqtt_mock_feed(mock, incomingAlias, sizeof(incomingAlias));
        mqtt_mock_feed(mock, incomingAliasOnly, sizeof(incomingAliasOnly));
    }
    MQTT_CHECK(mqtt_received_count == 2 &&
        strcmp(mqtt_received_topic, "m5/in") == 0 &&
        strcmp(mqtt_received_payload, "B") == 0,
        "MQTT 5 入站主题别名建立与复用正确");

    XMqttAuthenticationProperties* auth = XMqttAuthenticationProperties_create();
    XMqttAuthenticationProperties_setAuthenticationMethod(auth, "token");
    XByteArray_clear_base(mock->output);
    XMqttClient_authenticate(client, auth);
    MQTT_CHECK(XByteArray_size_base(mock->output) > 2 &&
        ((const uint8_t*)XByteArray_constData(mock->output))[0] == 0xf0,
        "MQTT 5 扩展认证发出 AUTH 报文");

    /* 值类型 move 自赋值必须保持对象有效，避免清空自身资源。 */
    XMqttTopicName* moveName = XMqttTopicName_create("move/topic");
    if (moveName) {
        XMove(moveName, moveName);
        MQTT_CHECK(XMqttTopicName_isValid(moveName) &&
                   XString_equals_utf8(XMqttTopicName_name_const(moveName),
                                       "move/topic", XChar_CaseSensitive),
                   "TopicName move 自赋值保持值不变");
        XMqttTopicName_delete_base(moveName);
    } else {
        MQTT_CHECK(false, "TopicName move 自赋值保持值不变");
    }
    XMqttAuthenticationProperties_delete_base(auth);
    XMqttClient_disconnectFromHost_base(client);
    XMqttTopicName_delete_base(publishTopic);
    XClass_delete_base((XClass*)client);
    XClass_delete_base((XClass*)mock);

    XPrintf("========== XMqtt Qt 6.8 公开 API 回归完成: %d 通过, %d 失败 ==========\n",
            passed, failed);
#undef MQTT_CHECK
    return failed == 0 ? 0 : 1;
}

void XMqttPublicApiTest(void)
{
    (void)XMqttPublicApiTest_run();
}
