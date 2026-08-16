#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_SERVER_ON
#include "XMqttServer_p.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XRandomGenerator.h"
#include <string.h>

/* ==================== 报文类型与属性标识常量 ==================== */
enum {
    MQTT_CONNECT = 0x10,
    MQTT_CONNACK = 0x20,
    MQTT_PUBLISH = 0x30,
    MQTT_PUBACK = 0x40,
    MQTT_PUBREC = 0x50,
    MQTT_PUBREL = 0x60,
    MQTT_PUBCOMP = 0x70,
    MQTT_SUBSCRIBE = 0x80,
    MQTT_SUBACK = 0x90,
    MQTT_UNSUBSCRIBE = 0xA0,
    MQTT_UNSUBACK = 0xB0,
    MQTT_PINGREQ = 0xC0,
    MQTT_PINGRESP = 0xD0,
    MQTT_DISCONNECT = 0xE0,
    MQTT_AUTH = 0xF0
};

enum {
    MQTT_PROP_PAYLOAD_FORMAT = 0x01,
    MQTT_PROP_MESSAGE_EXPIRY = 0x02,
    MQTT_PROP_CONTENT_TYPE = 0x03,
    MQTT_PROP_RESPONSE_TOPIC = 0x08,
    MQTT_PROP_CORRELATION_DATA = 0x09,
    MQTT_PROP_SUBSCRIPTION_ID = 0x0B,
    MQTT_PROP_SESSION_EXPIRY = 0x11,
    MQTT_PROP_ASSIGNED_CLIENT_ID = 0x12,
    MQTT_PROP_SERVER_KEEP_ALIVE = 0x13,
    MQTT_PROP_AUTH_METHOD = 0x15,
    MQTT_PROP_AUTH_DATA = 0x16,
    MQTT_PROP_REQUEST_PROBLEM_INFO = 0x17,
    MQTT_PROP_WILL_DELAY = 0x18,
    MQTT_PROP_REQUEST_RESPONSE_INFO = 0x19,
    MQTT_PROP_RESPONSE_INFO = 0x1A,
    MQTT_PROP_SERVER_REFERENCE = 0x1C,
    MQTT_PROP_REASON_STRING = 0x1F,
    MQTT_PROP_RECEIVE_MAXIMUM = 0x21,
    MQTT_PROP_TOPIC_ALIAS_MAXIMUM = 0x22,
    MQTT_PROP_TOPIC_ALIAS = 0x23,
    MQTT_PROP_MAXIMUM_QOS = 0x24,
    MQTT_PROP_RETAIN_AVAILABLE = 0x25,
    MQTT_PROP_USER_PROPERTY = 0x26,
    MQTT_PROP_MAXIMUM_PACKET_SIZE = 0x27,
    MQTT_PROP_WILDCARD_AVAILABLE = 0x28,
    MQTT_PROP_SUBSCRIPTION_ID_AVAILABLE = 0x29,
    MQTT_PROP_SHARED_AVAILABLE = 0x2A
};

/* MQTT 5.0 连接保活系数：1.5 倍保活间隔 */
#define MQTT_SERVER_KEEPALIVE_FACTOR 3
#define MQTT_SERVER_KEEPALIVE_DIVISOR 2

/* ==================== 流式读取辅助 ==================== */

/**
 * @brief 只读字节流读取器。
 * @details pos 指向当前读取位置；ok 为 false 表示读取失败，后续读取立即返回默认值。
 */
typedef struct XMqttReader {
    const uint8_t* data;   ///< 数据缓冲区
    size_t size;           ///< 缓冲区大小
    size_t pos;            ///< 当前读取位置
    bool ok;               ///< 是否仍可读取
} XMqttReader;

static uint8_t server_read_u8(XMqttReader* reader)
{
    if (!reader || !reader->ok || reader->pos >= reader->size) {
        if (reader) reader->ok = false;
        return 0;
    }
    return reader->data[reader->pos++];
}

static uint16_t server_read_u16(XMqttReader* reader)
{
    uint16_t high = server_read_u8(reader);
    uint16_t low = server_read_u8(reader);
    return (uint16_t)((high << 8) | low);
}

static uint32_t server_read_u32(XMqttReader* reader)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value = (value << 8) | server_read_u8(reader);
    return value;
}

static uint32_t server_read_varint(XMqttReader* reader)
{
    uint32_t value = 0;
    uint32_t multiplier = 1;
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = server_read_u8(reader);
        if (!reader->ok) return 0;
        value += (uint32_t)(byte & 0x7FU) * multiplier;
        if (!(byte & 0x80U)) return value;
        multiplier *= 128U;
    }
    reader->ok = false;
    return 0;
}

static XString* server_read_string(XMqttReader* reader)
{
    uint16_t size = server_read_u16(reader);
    if (!reader->ok || reader->pos + size > reader->size) {
        reader->ok = false;
        return NULL;
    }
    XString* value = size ?
        XString_create_with_length_utf8((const char*)reader->data + reader->pos, size) :
        XString_create_utf8("");
    reader->pos += size;
    if (!value) reader->ok = false;
    return value;
}

static XByteArray* server_read_binary(XMqttReader* reader)
{
    uint16_t size = server_read_u16(reader);
    if (!reader->ok || reader->pos + size > reader->size) {
        reader->ok = false;
        return NULL;
    }
    XByteArray* value = size ?
        XByteArray_create_with_data((const char*)reader->data + reader->pos, size) :
        XByteArray_create();
    reader->pos += size;
    if (!value) reader->ok = false;
    return value;
}

static bool server_reader_property_end(XMqttReader* reader, size_t* end)
{
    uint32_t length = server_read_varint(reader);
    if (!reader->ok || length > reader->size - reader->pos) {
        reader->ok = false;
        return false;
    }
    *end = reader->pos + length;
    return true;
}

/* ==================== 写入辅助 ==================== */

static bool server_append(XByteArray* output, const void* data, size_t size)
{
    if (!output || (!data && size)) return false;
    return size == 0 || XVector_push_back_2((XVector*)output, data, size);
}

static bool server_append_u8(XByteArray* output, uint8_t value)
{
    return XByteArray_push_back_1(output, value);
}

static bool server_append_u16(XByteArray* output, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return server_append(output, bytes, sizeof(bytes));
}

static bool server_append_u32(XByteArray* output, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value
    };
    return server_append(output, bytes, sizeof(bytes));
}

static bool server_append_varint(XByteArray* output, uint32_t value)
{
    if (value > 268435455U) return false;
    do {
        uint8_t byte = (uint8_t)(value % 128U);
        value /= 128U;
        if (value) byte |= 0x80U;
        if (!server_append_u8(output, byte)) return false;
    } while (value);
    return true;
}

static bool server_append_string(XByteArray* output, const XString* value)
{
    size_t size = value ? XString_toUtf8_length(value) : 0;
    const char* data = value ? XString_toUtf8(value) : NULL;
    return size <= UINT16_MAX && server_append_u16(output, (uint16_t)size) &&
           server_append(output, data, size);
}

static bool server_append_binary(XByteArray* output, const XByteArray* value)
{
    size_t size = value ? XByteArray_size_base(value) : 0;
    return size <= UINT16_MAX && server_append_u16(output, (uint16_t)size) &&
           server_append(output, value ? XByteArray_constData((XByteArray*)value) : NULL, size);
}

static bool server_append_property_string(XByteArray* output, uint8_t property,
                                          const XString* value)
{
    return server_append_u8(output, property) && server_append_string(output, value);
}

static bool server_append_property_binary(XByteArray* output, uint8_t property,
                                          const XByteArray* value)
{
    return server_append_u8(output, property) && server_append_binary(output, value);
}

static bool server_append_user_properties(XByteArray* output,
                                          const XMqttUserProperties* properties)
{
    if (!properties) return true;
    for (size_t i = 0; i < XVector_size_base((const XVector*)properties); ++i) {
        const XMqttStringPair* pair = (const XMqttStringPair*)XVector_at_base(
            (const XVector*)properties, (int64_t)i);
        if (!pair || !server_append_u8(output, MQTT_PROP_USER_PROPERTY) ||
            !server_append_string(output, XMqttStringPair_name_const(pair)) ||
            !server_append_string(output, XMqttStringPair_value_const(pair))) return false;
    }
    return true;
}

static bool server_append_property_block(XByteArray* output, const XByteArray* properties)
{
    size_t size = properties ? XByteArray_size_base(properties) : 0;
    return size <= 268435455U && server_append_varint(output, (uint32_t)size) &&
           server_append(output, properties ? XByteArray_constData((XByteArray*)properties) : NULL, size);
}

static bool server_append_u32_property(XByteArray* props, uint8_t id, uint32_t value)
{
    return server_append_u8(props, id) && server_append_u32(props, value);
}

static bool server_append_u16_property(XByteArray* props, uint8_t id, uint16_t value)
{
    return server_append_u8(props, id) && server_append_u16(props, value);
}

static bool server_append_u8_property(XByteArray* props, uint8_t id, uint8_t value)
{
    return server_append_u8(props, id) && server_append_u8(props, value);
}

/**
 * @brief 组装完整 MQTT 报文（固定头 + 剩余长度 + 载荷）。
 * @param header 第一个字节（类型与标志）。
 * @param payload 载荷缓冲区，可为 NULL（空载荷）。
 * @return 报文缓冲区，调用者负责释放；失败返回 NULL。
 */
static XByteArray* server_make_packet(uint8_t header, const XByteArray* payload)
{
    size_t size = payload ? XByteArray_size_base(payload) : 0;
    if (size > 268435455U) return NULL;
    XByteArray* packet = XByteArray_create();
    if (!packet || !server_append_u8(packet, header) ||
        !server_append_varint(packet, (uint32_t)size) ||
        !server_append(packet, payload ? XByteArray_constData((XByteArray*)payload) : NULL, size)) {
        if (packet) XByteArray_delete_base(packet);
        return NULL;
    }
    return packet;
}

/**
 * @brief 向指定传输设备发送一个完整报文。
 * @details 先检查目标客户端声明的最大报文大小（MQTT 5.0），超限时报文不发送。
 * @return 全部字节写入成功返回 true。
 */
static bool server_send_packet(XMqttServer* server, XMqttServerClient* client,
                               uint8_t header, const XByteArray* payload)
{
    XByteArray* packet = server_make_packet(header, payload);
    size_t size;
    if (!packet) return false;
    size = XByteArray_size_base(packet);
    if (client && client->maxPacketSize && size > client->maxPacketSize) {
        XByteArray_delete_base(packet);
        return true; /* 按协议丢弃超限报文，不算写入失败 */
    }
    {
        bool ok = XMqttServer_sendData_base(server, client ? client->transport : NULL,
                                            (const uint8_t*)XByteArray_constData(packet), size);
        XByteArray_delete_base(packet);
        return ok;
    }
}

/* ==================== 字符串/二进制工具 ==================== */

static bool server_string_present(const XString* value)
{
    return value && XString_toUtf8_length(value) != 0;
}

static bool server_string_equal(const XString* a, const char* utf8)
{
    return a && XString_equals_utf8(a, utf8, XChar_CaseSensitive);
}

static void server_replace_string(XString** target, XString* value)
{
    if (*target) XString_delete_base(*target);
    *target = value;
}

static void server_replace_binary(XByteArray** target, XByteArray* value)
{
    if (*target) XByteArray_delete_base(*target);
    *target = value;
}

/**
 * @brief 生成服务器分配的客户端 ID（客户端提交空 ID 时使用）。
 * @details 使用全局随机数生成器产生 23 个十六进制字符，满足 MQTT 3.1
 *          客户端 ID 字符集要求（数字/小写/大写字母）。
 * @return 新创建的 XString，调用者负责释放；失败返回 NULL。
 */
static XString* server_generate_client_id(void)
{
    static const char hex[] = "0123456789abcdef";
    char value[24];
    XRandomGenerator* random = XRandomGenerator_global();
    uint32_t bits = 0;
    for (size_t i = 0; i < 23; ++i) {
        if ((i & 7U) == 0)
            bits = random ? XRandomGenerator_generate(random) : (uint32_t)i;
        value[i] = hex[(bits >> ((i & 7U) * 4U)) & 0x0FU];
    }
    value[23] = '\0';
    return XString_create_utf8(value);
}

/* 前置声明：别名/遗嘱/会话/订阅/私有状态辅助（在报文处理之后定义） */
static void server_inbound_alias_deinit(void* value);
static void server_outbound_alias_deinit(void* value);
static void server_publish_will_direct(XMqttServer* server, const XString* topic,
                                       const XByteArray* payload, uint8_t qos,
                                       bool retain, const XMqttPublishProperties* properties);
static void server_cancel_pending_will(XMqttServer* server, const XString* clientId);
static void server_resume_session(XMqttServer* server, XMqttServerClient* client);
static bool server_subscription_exists(const XMqttServerSession* session,
                                       const XString* filter);
static void server_deliver_retained(XMqttServer* server, XMqttServerClient* client,
                                    const XString* filter, const XString* actualFilter,
                                    uint8_t qos);
static void server_private_delete(XMqttServer* server);

/* ==================== 内存管理 ==================== */

static void server_client_delete(XMqttServerClient* client)
{
    if (!client) return;
    if (client->input) XByteArray_delete_base(client->input);
    if (client->clientId) XString_delete_base(client->clientId);
    if (client->assignedClientId) XString_delete_base(client->assignedClientId);
    if (client->username) XString_delete_base(client->username);
    if (client->password) XString_delete_base(client->password);
    if (client->willTopic) XString_delete_base(client->willTopic);
    if (client->willMessage) XByteArray_delete_base(client->willMessage);
    if (client->willProperties) XMqttPublishProperties_delete_base(client->willProperties);
    if (client->incomingQos2) XVector_delete_base(client->incomingQos2);
    if (client->inboundAliases) XVector_delete_base(client->inboundAliases);
    if (client->outboundAliases) XVector_delete_base(client->outboundAliases);
    XFree_System(client);
}

static XMqttServerClient* server_client_create(void* transport)
{
    XMqttServerClient* client = (XMqttServerClient*)XMalloc_System(sizeof(XMqttServerClient));
    if (!client) return NULL;
    memset(client, 0, sizeof(*client));
    client->transport = transport;
    client->input = XByteArray_create();
    client->incomingQos2 = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(uint16_t), false);
    client->inboundAliases = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               sizeof(XMqttServerTopicAlias), false);
    client->outboundAliases = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                sizeof(XMqttServerTopicAlias), false);
    client->nextPacketId = 1;
    client->keepAliveTimer = XTIMER_INVALID_ID;
    client->maxPacketSize = 0;
    client->receiveMaximum = 65535;
    if (!client->input || !client->incomingQos2 || !client->inboundAliases ||
        !client->outboundAliases) {
        server_client_delete(client);
        return NULL;
    }
    XContainerSetDataDeinitMethod(client->inboundAliases, (XCDataDeinitMethod)server_inbound_alias_deinit);
    XContainerSetDataDeinitMethod(client->outboundAliases, (XCDataDeinitMethod)server_outbound_alias_deinit);
    return client;
}

static void server_subscription_deinit(void* value)
{
    XMqttServerSubscription* sub = (XMqttServerSubscription*)value;
    if (!sub) return;
    if (sub->filter) XString_delete_base(sub->filter);
    if (sub->actualFilter) XString_delete_base(sub->actualFilter);
    if (sub->shareName) XString_delete_base(sub->shareName);
    sub->filter = NULL;
    sub->actualFilter = NULL;
    sub->shareName = NULL;
}

static void server_queued_message_deinit(void* value)
{
    XMqttServerQueuedMessage* msg = (XMqttServerQueuedMessage*)value;
    if (!msg) return;
    if (msg->topic) XString_delete_base(msg->topic);
    if (msg->payload) XByteArray_delete_base(msg->payload);
    if (msg->properties) XMqttPublishProperties_delete_base(msg->properties);
    msg->topic = NULL;
    msg->payload = NULL;
    msg->properties = NULL;
}

static void server_session_delete(XMqttServerSession* session)
{
    if (!session) return;
    if (session->clientId) XString_delete_base(session->clientId);
    if (session->subscriptions) XVector_delete_base(session->subscriptions);
    if (session->queuedMessages) XVector_delete_base(session->queuedMessages);
    XFree_System(session);
}

static XMqttServerSession* server_session_create(const XString* clientId)
{
    XMqttServerSession* session = (XMqttServerSession*)XMalloc_System(sizeof(XMqttServerSession));
    if (!session) return NULL;
    memset(session, 0, sizeof(*session));
    session->clientId = XString_create_copy(clientId);
    session->subscriptions = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               sizeof(XMqttServerSubscription), false);
    session->queuedMessages = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                sizeof(XMqttServerQueuedMessage), false);
    session->persistent = false;
    session->expireAt = -1;
    session->expiryTimer = XTIMER_INVALID_ID;
    session->client = NULL;
    if (!session->clientId || !session->subscriptions || !session->queuedMessages) {
        server_session_delete(session);
        return NULL;
    }
    XContainerSetDataDeinitMethod(session->subscriptions,
        (XCDataDeinitMethod)server_subscription_deinit);
    XContainerSetDataDeinitMethod(session->queuedMessages,
        (XCDataDeinitMethod)server_queued_message_deinit);
    return session;
}

static void server_retained_deinit(void* value)
{
    XMqttServerRetainedMessage* msg = (XMqttServerRetainedMessage*)value;
    if (!msg) return;
    if (msg->topic) XString_delete_base(msg->topic);
    if (msg->payload) XByteArray_delete_base(msg->payload);
    if (msg->properties) XMqttPublishProperties_delete_base(msg->properties);
    msg->topic = NULL;
    msg->payload = NULL;
    msg->properties = NULL;
    XFree_System(msg);
}

static void server_pending_will_deinit(void* value)
{
    XMqttServerPendingWill* will = (XMqttServerPendingWill*)value;
    if (!will) return;
    if (will->clientId) XString_delete_base(will->clientId);
    if (will->topic) XString_delete_base(will->topic);
    if (will->payload) XByteArray_delete_base(will->payload);
    if (will->properties) XMqttPublishProperties_delete_base(will->properties);
    will->clientId = NULL;
    will->topic = NULL;
    will->payload = NULL;
    will->properties = NULL;
}

/* ==================== 映射比较器与释放回调 ==================== */

/**
 * @brief 传输设备指针比较（按地址身份）。
 */
static int32_t server_transport_compare(const void* a, const void* b)
{
    const void* va = a ? *(void* const*)a : NULL;
    const void* vb = b ? *(void* const*)b : NULL;
    return va < vb ? -1 : (va > vb ? 1 : 0);
}

/**
 * @brief XString* 键比较（内容比较）。
 */
static int32_t server_string_key_compare(const void* a, const void* b)
{
    const XString* sa = a ? *(const XString* const*)a : NULL;
    const XString* sb = b ? *(const XString* const*)b : NULL;
    if (sa == sb) return 0;
    if (!sa) return -1;
    if (!sb) return 1;
    return XString_compare(sa, sb);
}

/**
 * @brief m_clients 值释放：删除客户端。
 */
static void server_client_value_deinit(void* value)
{
    XMqttServerClient** client = (XMqttServerClient**)value;
    if (client && *client) {
        server_client_delete(*client);
        *client = NULL;
    }
}

/**
 * @brief m_sessions 值释放：删除会话。
 */
static void server_session_value_deinit(void* value)
{
    XMqttServerSession** session = (XMqttServerSession**)value;
    if (session && *session) {
        server_session_delete(*session);
        *session = NULL;
    }
}

/**
 * @brief m_retained 值释放：删除保留消息。
 */
static void server_retained_value_deinit(void* value)
{
    XMqttServerRetainedMessage** msg = (XMqttServerRetainedMessage**)value;
    if (msg && *msg) {
        server_retained_deinit(*msg);
        *msg = NULL;
    }
}

/* ==================== 内部工具 ==================== */

static XMqttServerClient* server_find_client(XMqttServer* server, void* transport)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XMqttServerClient** slot;
    if (!priv || !transport) return NULL;
    slot = (XMqttServerClient**)XMapBase_value_base((XMapBase*)priv->m_clients, &transport);
    return slot ? *slot : NULL;
}

static XMqttServerSession* server_find_session(XMqttServer* server, const XString* clientId)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XMqttServerSession** slot;
    if (!priv || !clientId) return NULL;
    slot = (XMqttServerSession**)XMapBase_value_base((XMapBase*)priv->m_sessions, &clientId);
    return slot ? *slot : NULL;
}

static void server_stop_keep_alive(XMqttServer* server, XMqttServerClient* client)
{
    if (!server || !client) return;
    if (client->keepAliveTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)server, client->keepAliveTimer);
        client->keepAliveTimer = XTIMER_INVALID_ID;
    }
}

static void server_start_keep_alive(XMqttServer* server, XMqttServerClient* client)
{
    uint16_t keepAlive;
    uint64_t interval;
    if (!server || !client) return;
    server_stop_keep_alive(server, client);
    keepAlive = server->m_serverKeepAlive ? server->m_serverKeepAlive : client->keepAlive;
    if (!keepAlive) return;
    interval = (uint64_t)keepAlive * 1000U * MQTT_SERVER_KEEPALIVE_FACTOR /
               MQTT_SERVER_KEEPALIVE_DIVISOR;
    client->keepAliveTimer = XObject_startTimer_ms((XObject*)server, interval,
                                                   XTimerType_CoarseTimer);
}

static void server_session_stop_expiry(XMqttServer* server, XMqttServerSession* session)
{
    if (!server || !session) return;
    if (session->expiryTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)server, session->expiryTimer);
        session->expiryTimer = XTIMER_INVALID_ID;
    }
}

static void server_session_start_expiry(XMqttServer* server, XMqttServerSession* session)
{
    int64_t now;
    if (!server || !session || !session->persistent) return;
    server_session_stop_expiry(server, session);
    if (session->expireAt < 0) return; /* 永久保存 */
    now = XDateTime_currentMSecsSinceEpoch();
    if (session->expireAt <= now) return;
    session->expiryTimer = XObject_startTimer_ms((XObject*)server,
        (uint64_t)(session->expireAt - now), XTimerType_CoarseTimer);
}

static XMqttServerQueuedMessage* server_session_queued_at(XMqttServerSession* session,
                                                          size_t index)
{
    if (!session || !session->queuedMessages) return NULL;
    return (XMqttServerQueuedMessage*)XVector_at_base(session->queuedMessages,
                                                      (int64_t)index);
}

static uint16_t server_next_packet_id(XMqttServerClient* client)
{
    XMqttServerSession* session;
    uint32_t attempts;
    if (!client) return 0;
    session = client->session;
    for (attempts = 0; attempts < UINT16_MAX; ++attempts) {
        uint16_t id = client->nextPacketId++;
        bool inUse = false;
        if (client->nextPacketId == 0) client->nextPacketId = 1;
        if (!id) continue;
        if (session && session->queuedMessages) {
            for (size_t i = 0; i < XVector_size_base(session->queuedMessages); ++i) {
                XMqttServerQueuedMessage* queued = server_session_queued_at(session, i);
                if (queued && queued->packetId == id) { inUse = true; break; }
            }
        }
        if (!inUse) return id;
    }
    return 0;
}

static bool server_string_contains_wildcard(const XString* value)
{
    const char* data;
    size_t size;
    if (!value) return false;
    data = XString_toUtf8(value);
    size = XString_toUtf8_length(value);
    for (size_t i = 0; i < size; ++i)
        if (data[i] == '+' || data[i] == '#') return true;
    return false;
}

/**
 * @brief 校验主题过滤器（不含共享前缀的真实过滤器）。
 * @details 规则：非空；长度不超 65535；通配符 # 只能出现在末尾且作为单独层级；
 *          通配符 + 必须占据整个层级。
 */
static bool server_topic_filter_valid(const XString* filter, bool wildcardsAllowed)
{
    const char* data;
    size_t size;
    if (!filter) return false;
    data = XString_toUtf8(filter);
    size = XString_toUtf8_length(filter);
    if (size == 0 || size > UINT16_MAX) return false;
    if (!wildcardsAllowed && server_string_contains_wildcard(filter)) return false;
    for (size_t i = 0; i < size; ++i) {
        char c = data[i];
        if (c == '#') {
            if (i != size - 1) return false;
            if (i != 0 && data[i - 1] != '/') return false;
        } else if (c == '+') {
            if (i != 0 && data[i - 1] != '/') return false;
            if (i + 1 < size && data[i + 1] != '/') return false;
        }
    }
    return true;
}

static bool server_topic_name_valid(const XString* topic)
{
    const char* data;
    size_t size;
    if (!topic) return false;
    data = XString_toUtf8(topic);
    size = XString_toUtf8_length(topic);
    if (size == 0 || size > UINT16_MAX) return false;
    for (size_t i = 0; i < size; ++i)
        if (data[i] == '+' || data[i] == '#') return false;
    return true;
}

/**
 * @brief 解析共享订阅前缀。
 * @details 过滤器形如 $share/<ShareName>/<filter>。成功时返回 shareName 和
 *          actualFilter（均新建，调用者负责释放）；输入不是共享订阅时返回 false。
 */
static bool server_parse_shared_filter(const XString* filter,
                                       XString** shareName, XString** actualFilter)
{
    const char* data;
    size_t size;
    size_t pos = 0;
    size_t shareEnd;
    if (!filter || !shareName || !actualFilter) return false;
    data = XString_toUtf8(filter);
    size = XString_toUtf8_length(filter);
    if (size < 8 || memcmp(data, "$share/", 7) != 0) return false;
    pos = 7;
    shareEnd = pos;
    while (shareEnd < size && data[shareEnd] != '/') ++shareEnd;
    if (shareEnd == pos || shareEnd >= size) return false; /* 缺共享组名或缺过滤器 */
    for (size_t i = pos; i < shareEnd; ++i) {
        char c = data[i];
        if (c == '/' || c == '+' || c == '#') return false;
    }
    *shareName = XString_create_with_length_utf8(data + pos, shareEnd - pos);
    *actualFilter = XString_create_with_length_utf8(data + shareEnd + 1,
                                                    size - shareEnd - 1);
    if (!*shareName || !*actualFilter) {
        if (*shareName) XString_delete_base(*shareName);
        if (*actualFilter) XString_delete_base(*actualFilter);
        *shareName = NULL;
        *actualFilter = NULL;
        return false;
    }
    return true;
}

/**
 * @brief 生成共享订阅轮转键：shareName + "/" + actualFilter。
 */
static XString* server_shared_key(const XString* shareName, const XString* actualFilter)
{
    const char* share = XString_toUtf8(shareName);
    size_t shareLen = XString_toUtf8_length(shareName);
    const char* filter = XString_toUtf8(actualFilter);
    size_t filterLen = XString_toUtf8_length(actualFilter);
    char* buffer;
    XString* key;
    if (shareLen + filterLen + 1 > UINT16_MAX) return NULL;
    buffer = (char*)XMalloc_System(shareLen + filterLen + 2);
    if (!buffer) return NULL;
    memcpy(buffer, share, shareLen);
    buffer[shareLen] = '/';
    memcpy(buffer + shareLen + 1, filter, filterLen);
    buffer[shareLen + filterLen + 1] = '\0';
    key = XString_create_utf8(buffer);
    XFree_System(buffer);
    return key;
}

static void server_shared_deinit(void* slot)
{
    XString** key = (XString**)slot;
    if (key && *key) {
        XString_delete_base(*key);
        *key = NULL;
    }
}

static uint32_t server_shared_next_index(XMqttServer* server, const XString* key)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    uint32_t* slot;
    uint32_t value = 0;
    if (!priv || !key) return 0;
    slot = (uint32_t*)XMapBase_value_base((XMapBase*)priv->m_sharedIndex, &key);
    if (slot) value = *slot;
    else {
        XString* keyCopy = XString_create_copy(key);
        if (!keyCopy) return 0;
        XMapBase_insert_base((XMapBase*)priv->m_sharedIndex, &keyCopy, &value);
    }
    return value;
}

static void server_shared_set_index(XMqttServer* server, const XString* key, uint32_t value)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    uint32_t* slot;
    if (!priv || !key) return;
    slot = (uint32_t*)XMapBase_value_base((XMapBase*)priv->m_sharedIndex, &key);
    if (slot) *slot = value;
}

/**
 * @brief 共享订阅组在本轮路由中的选择结果。
 * @details 每个共享订阅组（shareName + actualFilter）在一轮消息路由中只
 *          选择一名在线成员接收消息，其余成员本轮不接收。选择结果在
 *          server_route_message 中先统一构建，再下发给各会话的匹配流程。
 */
typedef struct XMqttServerSharedSelection {
    XString* shareName;            ///< 共享订阅组名，条目拥有。
    XString* actualFilter;         ///< 实际主题过滤器，条目拥有。
    XMqttServerSession* winner;    ///< 本轮被选中的成员会话（借用，离线为 NULL）。
} XMqttServerSharedSelection;

/**
 * @brief 判断共享订阅是否匹配指定主题。
 * @param sub 共享订阅（shareName 非空）。
 * @param topicName 目标主题名。
 * @return 匹配返回 true；参数非法或使用普通过滤器不匹配返回 false。
 */
static bool server_shared_sub_matches(const XMqttServerSubscription* sub,
                                      const XMqttTopicName* topicName)
{
    XMqttTopicFilter filter;
    bool ok;
    if (!sub || !sub->shareName || !sub->actualFilter || !topicName) return false;
    XMqttTopicFilter_init(&filter, XString_toUtf8(sub->actualFilter));
    ok = XMqttTopicFilter_match(&filter, topicName,
        XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption);
    XMqttTopicFilter_deinit_base(&filter);
    return ok;
}

/**
 * @brief 统计共享订阅组在所有会话中的在线成员数。
 * @details 依次遍历全部会话，只统计处于在线状态（client 非空）且拥有同组
 *          共享订阅的会话；每个会话无论重复订阅多少次只计一次。
 * @param server 服务器实例（非 NULL）。
 * @param shareName 共享订阅组名。
 * @param actualFilter 实际主题过滤器。
 * @param topicName 目标主题名。
 * @return 在线成员数，参数非法返回 0。
 */
static size_t server_shared_member_count(XMqttServer* server, const XString* shareName,
                                         const XString* actualFilter,
                                         const XMqttTopicName* topicName)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XVector* keys;
    size_t count = 0;
    if (!priv || !shareName || !actualFilter || !topicName) return 0;
    keys = XMapBase_keys_base((XMapBase*)priv->m_sessions);
    if (!keys) return 0;
    for (size_t i = 0; i < XVector_size_base(keys); ++i) {
        XString** key = (XString**)XVector_at_base(keys, (int64_t)i);
        XMqttServerSession** slot = key ?
            (XMqttServerSession**)XMapBase_value_base((XMapBase*)priv->m_sessions, key) : NULL;
        XMqttServerSession* session = slot ? *slot : NULL;
        if (!session || !session->client || !session->subscriptions) continue;
        for (size_t j = 0; j < XVector_size_base(session->subscriptions); ++j) {
            XMqttServerSubscription* sub = (XMqttServerSubscription*)XVector_at_base(
                session->subscriptions, (int64_t)j);
            if (sub && sub->shareName &&
                XString_equals(sub->shareName, shareName, XChar_CaseSensitive) &&
                XString_equals(sub->actualFilter, actualFilter, XChar_CaseSensitive) &&
                server_shared_sub_matches(sub, topicName)) {
                ++count;
                break;
            }
        }
    }
    XVector_delete_base(keys);
    return count;
}

/**
 * @brief 获取共享订阅组中指定序号（0 基）的在线成员会话。
 * @details 遍历顺序与 server_shared_member_count 完全一致，因此两函数
 *          配合可稳定地定位轮转选中的成员。
 * @param server 服务器实例（非 NULL）。
 * @param shareName 共享订阅组名。
 * @param actualFilter 实际主题过滤器。
 * @param topicName 目标主题名。
 * @param position 成员序号（0 基）。
 * @return 对应会话指针；不存在或参数非法返回 NULL。
 */
static XMqttServerSession* server_shared_member_at(XMqttServer* server, const XString* shareName,
                                                   const XString* actualFilter,
                                                   const XMqttTopicName* topicName,
                                                   size_t position)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XVector* keys;
    size_t pos = 0;
    XMqttServerSession* result = NULL;
    if (!priv || !shareName || !actualFilter || !topicName) return NULL;
    keys = XMapBase_keys_base((XMapBase*)priv->m_sessions);
    if (!keys) return NULL;
    for (size_t i = 0; i < XVector_size_base(keys); ++i) {
        XString** key = (XString**)XVector_at_base(keys, (int64_t)i);
        XMqttServerSession** slot = key ?
            (XMqttServerSession**)XMapBase_value_base((XMapBase*)priv->m_sessions, key) : NULL;
        XMqttServerSession* session = slot ? *slot : NULL;
        if (!session || !session->client || !session->subscriptions) continue;
        for (size_t j = 0; j < XVector_size_base(session->subscriptions); ++j) {
            XMqttServerSubscription* sub = (XMqttServerSubscription*)XVector_at_base(
                session->subscriptions, (int64_t)j);
            if (sub && sub->shareName &&
                XString_equals(sub->shareName, shareName, XChar_CaseSensitive) &&
                XString_equals(sub->actualFilter, actualFilter, XChar_CaseSensitive) &&
                server_shared_sub_matches(sub, topicName)) {
                if (pos == position) result = session;
                ++pos;
                break;
            }
        }
    }
    XVector_delete_base(keys);
    return result;
}

/**
 * @brief 在共享选择表中查找指定组的条目。
 * @param selections 共享选择表（可为 NULL）。
 * @param shareName 共享订阅组名。
 * @param actualFilter 实际主题过滤器。
 * @return 匹配的条目指针，未找到返回 NULL。
 */
static XMqttServerSharedSelection* server_shared_selection_find(XVector* selections,
                                                                const XString* shareName,
                                                                const XString* actualFilter)
{
    if (!selections) return NULL;
    for (size_t i = 0; i < XVector_size_base(selections); ++i) {
        XMqttServerSharedSelection* sel = (XMqttServerSharedSelection*)XVector_at_base(
            selections, (int64_t)i);
        if (sel && XString_equals(sel->shareName, shareName, XChar_CaseSensitive) &&
            XString_equals(sel->actualFilter, actualFilter, XChar_CaseSensitive))
            return sel;
    }
    return NULL;
}

/**
 * @brief 释放共享选择表（含每个条目的字符串所有权）。
 * @param selections 共享选择表（可为 NULL）。
 */
static void server_shared_selections_deinit(XVector* selections)
{
    if (!selections) return;
    for (size_t i = 0; i < XVector_size_base(selections); ++i) {
        XMqttServerSharedSelection* sel = (XMqttServerSharedSelection*)XVector_at_base(
            selections, (int64_t)i);
        if (!sel) continue;
        if (sel->shareName) XString_delete_base(sel->shareName);
        if (sel->actualFilter) XString_delete_base(sel->actualFilter);
        sel->shareName = NULL;
        sel->actualFilter = NULL;
        sel->winner = NULL;
    }
    XVector_delete_base(selections);
}

/**
 * @brief 构建本轮消息路由的共享订阅选择表。
 * @details 遍历所有在线会话，收集匹配主题的共享订阅组；对每个组统计在线
 *          成员数，按全局轮转索引选出一名成员，并推进轮转索引。普通订阅
 *          不受影响。
 * @param server 服务器实例（非 NULL）。
 * @param topicName 目标主题名。
 * @return 新建的选择表（调用者负责 server_shared_selections_deinit 释放），
 *         内部资源不足返回 NULL。
 */
static XVector* server_shared_build_selections(XMqttServer* server,
                                               const XMqttTopicName* topicName)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XVector* selections;
    XVector* keys;
    if (!priv || !topicName) return NULL;
    selections = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   sizeof(XMqttServerSharedSelection), false);
    if (!selections) return NULL;
    keys = XMapBase_keys_base((XMapBase*)priv->m_sessions);
    if (!keys) return selections;
    for (size_t i = 0; i < XVector_size_base(keys); ++i) {
        XString** key = (XString**)XVector_at_base(keys, (int64_t)i);
        XMqttServerSession** slot = key ?
            (XMqttServerSession**)XMapBase_value_base((XMapBase*)priv->m_sessions, key) : NULL;
        XMqttServerSession* session = slot ? *slot : NULL;
        if (!session || !session->client || !session->subscriptions) continue;
        for (size_t j = 0; j < XVector_size_base(session->subscriptions); ++j) {
            XMqttServerSubscription* sub = (XMqttServerSubscription*)XVector_at_base(
                session->subscriptions, (int64_t)j);
            if (!sub || !sub->shareName) continue;
            if (!server_shared_sub_matches(sub, topicName)) continue;
            if (server_shared_selection_find(selections, sub->shareName, sub->actualFilter))
                continue;
            {
                XMqttServerSharedSelection sel;
                memset(&sel, 0, sizeof(sel));
                sel.shareName = XString_create_copy(sub->shareName);
                sel.actualFilter = XString_create_copy(sub->actualFilter);
                if (!sel.shareName || !sel.actualFilter) {
                    if (sel.shareName) XString_delete_base(sel.shareName);
                    if (sel.actualFilter) XString_delete_base(sel.actualFilter);
                    continue;
                }
                XVector_push_back_1_base(selections, &sel);
            }
        }
    }
    for (size_t i = 0; i < XVector_size_base(selections); ++i) {
        XMqttServerSharedSelection* sel = (XMqttServerSharedSelection*)XVector_at_base(
            selections, (int64_t)i);
        XString* key;
        size_t total;
        uint32_t index;
        if (!sel || !sel->shareName || !sel->actualFilter) continue;
        total = server_shared_member_count(server, sel->shareName, sel->actualFilter, topicName);
        if (!total) continue;
        key = server_shared_key(sel->shareName, sel->actualFilter);
        if (!key) continue;
        index = server_shared_next_index(server, key);
        sel->winner = server_shared_member_at(server, sel->shareName, sel->actualFilter,
                                              topicName, (size_t)(index % total));
        server_shared_set_index(server, key, index + 1);
        XString_delete_base(key);
    }
    XVector_delete_base(keys);
    return selections;
}

/* ==================== 订阅/消息结构辅助 ==================== */

static bool server_subscription_set(XMqttServerSession* session, XMqttServerSubscription* sub)
{
    XMqttServerSubscription value;
    if (!session || !session->subscriptions || !sub) return false;
    /* 同会话重复订阅同一过滤器：替换旧条目 */
    for (size_t i = 0; i < XVector_size_base(session->subscriptions); ++i) {
        XMqttServerSubscription* existing = (XMqttServerSubscription*)XVector_at_base(
            session->subscriptions, (int64_t)i);
        if (existing && existing->filter &&
            XString_equals(existing->filter, sub->filter, XChar_CaseSensitive)) {
            server_subscription_deinit(existing);
            *existing = *sub;
            memset(sub, 0, sizeof(*sub));
            return true;
        }
    }
    value = *sub;
    memset(sub, 0, sizeof(*sub));
    return XVector_push_back_1_base(session->subscriptions, &value);
}

static bool server_session_remove_subscription(XMqttServerSession* session,
                                               const XString* filter)
{
    if (!session || !session->subscriptions || !filter) return false;
    for (size_t i = 0; i < XVector_size_base(session->subscriptions); ++i) {
        XMqttServerSubscription* existing = (XMqttServerSubscription*)XVector_at_base(
            session->subscriptions, (int64_t)i);
        if (existing && existing->filter &&
            XString_equals(existing->filter, filter, XChar_CaseSensitive)) {
            server_subscription_deinit(existing);
            XVector_remove_base(session->subscriptions, (int64_t)i, 1);
            return true;
        }
    }
    return false;
}

static XMqttServerQueuedMessage* server_enqueue_message(XMqttServerSession* session,
                                                       const XString* topic,
                                                       const XByteArray* payload,
                                                       const XMqttPublishProperties* properties,
                                                       uint8_t qos, bool retain)
{
    XMqttServerQueuedMessage value;
    if (!session || !session->queuedMessages || !topic) return NULL;
    memset(&value, 0, sizeof(value));
    value.topic = XString_create_copy(topic);
    value.payload = payload ? XByteArray_create_copy(payload) : XByteArray_create();
    value.properties = properties ? XMqttPublishProperties_create_copy(properties) : NULL;
    value.qos = qos;
    value.retain = retain;
    value.stage = 0;
    if (!value.topic || !value.payload ||
        (properties && !value.properties)) {
        server_queued_message_deinit(&value);
        return NULL;
    }
    if (!XVector_push_back_1_base(session->queuedMessages, &value)) {
        server_queued_message_deinit(&value);
        return NULL;
    }
    return (XMqttServerQueuedMessage*)XVector_at_base(session->queuedMessages,
        (int64_t)(XVector_size_base(session->queuedMessages) - 1));
}

/* ==================== 别名辅助 ==================== */

static void server_inbound_alias_deinit(void* value)
{
    XMqttServerTopicAlias* alias = (XMqttServerTopicAlias*)value;
    if (alias && alias->topic) {
        XString_delete_base(alias->topic);
        alias->topic = NULL;
    }
}

static void server_outbound_alias_deinit(void* value)
{
    XMqttServerTopicAlias* alias = (XMqttServerTopicAlias*)value;
    if (alias && alias->topic) {
        XString_delete_base(alias->topic);
        alias->topic = NULL;
    }
}

static XMqttServerTopicAlias* server_alias_by_id(XVector* aliases, uint16_t alias)
{
    for (size_t i = 0; aliases && i < XVector_size_base(aliases); ++i) {
        XMqttServerTopicAlias* entry = (XMqttServerTopicAlias*)XVector_at_base(aliases,
                                                                               (int64_t)i);
        if (entry && entry->alias == alias) return entry;
    }
    return NULL;
}

static bool server_alias_set(XVector* aliases, uint16_t alias, const XString* topic)
{
    XMqttServerTopicAlias* entry = server_alias_by_id(aliases, alias);
    if (entry) {
        if (entry->topic) XString_delete_base(entry->topic);
        entry->topic = XString_create_copy(topic);
        return entry->topic != NULL;
    }
    {
        XMqttServerTopicAlias value = {alias, XString_create_copy(topic)};
        if (!value.topic || !XVector_push_back_1_base(aliases, &value)) {
            if (value.topic) XString_delete_base(value.topic);
            return false;
        }
    }
    return true;
}

/* ==================== 报文编码 ==================== */

/**
 * @brief 编码 CONNACK 报文。
 * @details v3/v4 固定两字节载荷；v5 携带服务器能力属性。
 */
static bool server_send_connack(XMqttServer* server, XMqttServerClient* client,
                                uint8_t reason, bool sessionPresent)
{
    XByteArray* payload = XByteArray_create();
    XByteArray* props = XByteArray_create();
    bool ok = payload && props;
    if (ok) ok = server_append_u8(payload, sessionPresent ? 0x01U : 0x00U) &&
                 server_append_u8(payload, reason);
    if (ok && client->protocolVersion == 5) {
        if (server->m_maximumPacketSize != 268435455U)
            ok = server_append_u32_property(props, MQTT_PROP_MAXIMUM_PACKET_SIZE,
                                            server->m_maximumPacketSize);
        if (ok && server->m_topicAliasMaximum)
            ok = server_append_u16_property(props, MQTT_PROP_TOPIC_ALIAS_MAXIMUM,
                                            server->m_topicAliasMaximum);
        if (ok && server->m_maximumQoS < 2)
            ok = server_append_u8_property(props, MQTT_PROP_MAXIMUM_QOS,
                                           server->m_maximumQoS);
        if (ok && !server->m_retainAvailable)
            ok = server_append_u8_property(props, MQTT_PROP_RETAIN_AVAILABLE, 0);
        if (ok && !server->m_wildcardAvailable)
            ok = server_append_u8_property(props, MQTT_PROP_WILDCARD_AVAILABLE, 0);
        if (ok && !server->m_subscriptionIdAvailable)
            ok = server_append_u8_property(props, MQTT_PROP_SUBSCRIPTION_ID_AVAILABLE, 0);
        if (ok && !server->m_sharedAvailable)
            ok = server_append_u8_property(props, MQTT_PROP_SHARED_AVAILABLE, 0);
        if (ok && server->m_serverKeepAlive)
            ok = server_append_u16_property(props, MQTT_PROP_SERVER_KEEP_ALIVE,
                                            server->m_serverKeepAlive);
        if (ok && client->assignedClientId)
            ok = server_append_property_string(props, MQTT_PROP_ASSIGNED_CLIENT_ID,
                                               client->assignedClientId);
        if (ok) ok = server_append_property_block(payload, props);
    }
    if (ok) ok = server_send_packet(server, client, MQTT_CONNACK, payload);
    if (props) XByteArray_delete_base(props);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

/**
 * @brief 编码 SUBACK 报文。
 * @param reasonCodes 每个过滤器一个原因码（v3 为 granted QoS）。
 */
static bool server_send_suback(XMqttServer* server, XMqttServerClient* client,
                               uint16_t packetId, const uint8_t* reasonCodes,
                               size_t count)
{
    XByteArray* payload = XByteArray_create();
    XByteArray* props = XByteArray_create();
    bool ok = payload && props && server_append_u16(payload, packetId);
    if (ok && client->protocolVersion == 5)
        ok = server_append_property_block(payload, props);
    for (size_t i = 0; ok && i < count; ++i)
        ok = server_append_u8(payload, reasonCodes[i]);
    if (ok) ok = server_send_packet(server, client, MQTT_SUBACK, payload);
    if (props) XByteArray_delete_base(props);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

/**
 * @brief 编码 UNSUBACK 报文。
 */
static bool server_send_unsuback(XMqttServer* server, XMqttServerClient* client,
                                 uint16_t packetId, const uint8_t* reasonCodes,
                                 size_t count)
{
    XByteArray* payload = XByteArray_create();
    XByteArray* props = XByteArray_create();
    bool ok = payload && props && server_append_u16(payload, packetId);
    if (ok && client->protocolVersion == 5) {
        for (size_t i = 0; ok && i < count; ++i)
            ok = server_append_u8(payload, reasonCodes[i]);
        if (ok) ok = server_append_property_block(payload, props);
    }
    if (ok) ok = server_send_packet(server, client, MQTT_UNSUBACK, payload);
    if (props) XByteArray_delete_base(props);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

/**
 * @brief 编码 PUBACK/PUBREC/PUBREL/PUBCOMP 报文。
 */
static bool server_send_ack(XMqttServer* server, XMqttServerClient* client,
                            uint8_t type, uint16_t identifier, uint8_t reason)
{
    XByteArray* payload = XByteArray_create();
    bool ok = payload && server_append_u16(payload, identifier);
    if (ok && client->protocolVersion == 5) {
        ok = server_append_u8(payload, reason) && server_append_u8(payload, 0);
    }
    if (ok) ok = server_send_packet(server, client, type, payload);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

/**
 * @brief 编码出站 PUBLISH 报文。
 * @param dup 是否重发标志。
 * @param qos 实际投递 QoS。
 * @param retain RETAIN 标志。
 * @param topic 主题名。
 * @param packetId packetId（qos>0 时有效）。
 * @param properties MQTT 5.0 属性（可为 NULL）。
 * @param payload 载荷。
 */
static bool server_send_publish(XMqttServer* server, XMqttServerClient* client,
                                bool dup, uint8_t qos, bool retain,
                                const XString* topic, uint16_t packetId,
                                const XMqttPublishProperties* properties,
                                const XByteArray* payload)
{
    XByteArray* body = XByteArray_create();
    XByteArray* props = XByteArray_create();
    /* 用位域联合体构造固定头第一字节（避免手工移位/掩码） */
    XMqttFixedHeader fixed;
    fixed.byte = 0;
    fixed.bits.type = (uint8_t)(MQTT_PUBLISH >> 4);
    fixed.bits.qos = qos;
    fixed.bits.retain = retain ? 1U : 0U;
    fixed.bits.dup = dup ? 1U : 0U;
    uint8_t header = fixed.byte;
    bool ok = body && props && server_append_string(body, topic);
    if (ok && qos) ok = server_append_u16(body, packetId);
    if (ok && properties) {
        if (properties->m_messageExpiryInterval)
            ok = server_append_u32_property(props, MQTT_PROP_MESSAGE_EXPIRY,
                                            properties->m_messageExpiryInterval);
        if (ok && (properties->m_availableProperties &
                   XMqttPublishProperties_PayloadFormatIndicator))
            ok = server_append_u8_property(props, MQTT_PROP_PAYLOAD_FORMAT,
                                           properties->m_payloadFormatIndicator);
        if (ok && properties->m_contentType &&
            XString_toUtf8_length(properties->m_contentType))
            ok = server_append_property_string(props, MQTT_PROP_CONTENT_TYPE,
                                               properties->m_contentType);
        if (ok && properties->m_responseTopic &&
            XString_toUtf8_length(properties->m_responseTopic))
            ok = server_append_property_string(props, MQTT_PROP_RESPONSE_TOPIC,
                                               properties->m_responseTopic);
        if (ok && properties->m_correlationData &&
            XByteArray_size_base(properties->m_correlationData))
            ok = server_append_property_binary(props, MQTT_PROP_CORRELATION_DATA,
                                               properties->m_correlationData);
        if (ok && properties->m_subscriptionIdentifiers) {
            for (size_t i = 0; ok &&
                 i < XVector_size_base(properties->m_subscriptionIdentifiers); ++i) {
                uint32_t* id = (uint32_t*)XVector_at_base(
                    properties->m_subscriptionIdentifiers, (int64_t)i);
                if (id && *id)
                    ok = server_append_u8(props, MQTT_PROP_SUBSCRIPTION_ID) &&
                         server_append_varint(props, *id);
            }
        }
        if (ok) ok = server_append_user_properties(props, properties->m_userProperties);
    }
    if (ok && client->protocolVersion == 5)
        ok = server_append_property_block(body, props);
    if (ok) ok = server_append(body, payload ? XByteArray_constData((XByteArray*)payload) : NULL,
                               payload ? XByteArray_size_base(payload) : 0);
    if (ok) ok = server_send_packet(server, client, header, body);
    if (props) XByteArray_delete_base(props);
    if (body) XByteArray_delete_base(body);
    return ok;
}

/**
 * @brief 编码并发送 DISCONNECT 报文（MQTT 5.0 带原因码）。
 */
static bool server_send_disconnect(XMqttServer* server, XMqttServerClient* client,
                                   uint8_t reason)
{
    XByteArray* payload = NULL;
    bool ok = true;
    if (client && client->protocolVersion == 5) {
        payload = XByteArray_create();
        ok = payload && server_append_u8(payload, reason) &&
             server_append_u8(payload, 0);
    }
    if (ok) ok = server_send_packet(server, client, MQTT_DISCONNECT, payload);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

/* 前置声明：路由引擎（在报文处理之后定义） */
static void server_route_message(XMqttServer* server, XMqttServerClient* source,
                                 const XString* topic, const XByteArray* payload,
                                 const XMqttPublishProperties* properties,
                                 uint8_t qos, bool retain);

/* 前置声明：遗嘱发布 */
static void server_publish_will(XMqttServer* server, XMqttServerClient* client);

/* ==================== 连接管理 ==================== */

/**
 * @brief 释放客户端连接内部状态（不发布遗嘱）。
 */
static void server_discard_client(XMqttServer* server, void* transport, bool publishWill)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XMqttServerClient* client;
    XMqttServerSession* session;
    if (!priv || !transport) return;
    client = server_find_client(server, transport);
    if (!client) return;
    if (publishWill && !client->disconnectReceived) {
        if (client->willTopic && client->willMessage) {
            if (client->willDelay && client->protocolVersion == 5) {
                /* 延迟遗嘱：登记到待发布列表，由定时器发布 */
                XMqttServerPendingWill value;
                memset(&value, 0, sizeof(value));
                value.timer = XTIMER_INVALID_ID;
                value.clientId = XString_create_copy(client->clientId);
                value.topic = XString_create_copy(client->willTopic);
                value.payload = XByteArray_create_copy(client->willMessage);
                value.qos = client->willQoS;
                value.retain = client->willRetain;
                value.properties = client->willProperties ?
                    XMqttPublishProperties_create_copy(client->willProperties) : NULL;
                if (value.clientId && value.topic && value.payload &&
                    (!client->willProperties || value.properties) &&
                    XVector_push_back_1_base(priv->m_pendingWills, &value)) {
                    value.timer = XObject_startTimer_ms((XObject*)server,
                        (uint64_t)client->willDelay * 1000U, XTimerType_CoarseTimer);
                    /* 更新条目中的定时器 ID */
                    XMqttServerPendingWill* stored = (XMqttServerPendingWill*)XVector_at_base(
                        priv->m_pendingWills,
                        (int64_t)(XVector_size_base(priv->m_pendingWills) - 1));
                    if (stored) stored->timer = value.timer;
                } else {
                    server_pending_will_deinit(&value);
                    server_publish_will_direct(server, client->willTopic,
                                               client->willMessage, client->willQoS,
                                               client->willRetain, client->willProperties);
                }
            } else {
                server_publish_will_direct(server, client->willTopic,
                                           client->willMessage, client->willQoS,
                                           client->willRetain, client->willProperties);
            }
        }
    }
    session = client->session;
    if (session) {
        session->client = NULL;
        if (!session->persistent) {
            server_session_stop_expiry(server, session);
            XMapBase_remove_base((XMapBase*)priv->m_sessions, &session->clientId);
        } else {
            server_session_start_expiry(server, session);
        }
    }
    server_stop_keep_alive(server, client);
    XMapBase_remove_base((XMapBase*)priv->m_clients, &transport);
}

/**
 * @brief 结束客户端连接（公开 API 入口）。
 */
void XMqttServer_endClient(XMqttServer* server, void* transport)
{
    if (!server || !transport) return;
    if (!server_find_client(server, transport)) return;
    server_discard_client(server, transport, true);
    XMqttServer_clientDisconnected_signal(server, transport);
}

bool XMqttServer_beginClient(XMqttServer* server, void* transport)
{
    XMqttServerPrivate* priv;
    XMqttServerClient* client;
    if (!server || !transport) return false;
    priv = server->m_private;
    if (!priv || !priv->m_clients) return false;
    if (server_find_client(server, transport)) return false;
    client = server_client_create(transport);
    if (!client) return false;
    if (!XMapBase_insert_move_base((XMapBase*)priv->m_clients, &transport, &client)) {
        server_client_delete(client);
        return false;
    }
    return true;
}

/**
 * @brief 服务端主动关闭一条连接：先按协议发送 DISCONNECT（v5），再关闭传输层。
 */
static void server_close_transport(XMqttServer* server, XMqttServerClient* client)
{
    if (!server || !client) return;
    if (client->protocolVersion == 5 && client->connected)
        server_send_disconnect(server, client, XMqtt_ReasonCode_UnspecifiedError);
    XMqttServer_closeClient_base(server, client->transport);
}

/* ==================== 报文处理 ==================== */

static void server_handle_connect(XMqttServer* server, XMqttServerClient* client,
                                  XMqttReader* reader)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XString* protocolName = NULL;
    XString* clientId = NULL;
    XString* username = NULL;
    XByteArray* password = NULL;
    XString* willTopic = NULL;
    XByteArray* willMessage = NULL;
    XMqttPublishProperties* willProps = NULL;
    uint8_t level = 0;
    uint8_t flags = 0;
    uint16_t keepAlive = 0;
    uint32_t sessionExpiry = 0;
    bool cleanStart = true;
    bool hasWill = false;
    uint8_t willQoS = 0;
    bool willRetain = false;
    uint32_t willDelay = 0;
    uint8_t failReason = 0;
    bool sessionPresent = false;
    XMqttServerSession* session = NULL;
    XMqttServerClient* oldClient = NULL;
    XString* oldClientId = NULL;

    if (!priv || client->connected) { reader->ok = false; return; }
    protocolName = server_read_string(reader);
    level = server_read_u8(reader);
    flags = server_read_u8(reader);
    keepAlive = server_read_u16(reader);
    if (!reader->ok) goto protocol_error;
    if (level == 3) {
        if (!protocolName || !server_string_equal(protocolName, "MQIsdp"))
            failReason = XMqtt_ReasonCode_UnsupportedProtocolVersion;
    } else if (level == 4 || level == 5) {
        if (!protocolName || !server_string_equal(protocolName, "MQTT"))
            failReason = XMqtt_ReasonCode_UnsupportedProtocolVersion;
    } else {
        failReason = XMqtt_ReasonCode_UnsupportedProtocolVersion;
    }
    if (failReason) {
        if (client->protocolVersion == 5 || level == 5) {
            /* 无法解析版本时按 v5 尝试回复 */
            client->protocolVersion = 5;
            server_send_connack(server, client,
                                XMqtt_ReasonCode_UnsupportedProtocolVersion, false);
        } else {
            uint8_t legacy = XMqtt_ReasonCode_UnsupportedProtocolVersion & 0x0FU;
            client->protocolVersion = level == 3 ? 3 : 4;
            server_send_connack(server, client, legacy, false);
        }
        server_close_transport(server, client);
        goto cleanup;
    }
    client->protocolVersion = level;
    if (flags & 0x01U) { failReason = XMqtt_ReasonCode_MalformedPacket; goto reject; }
    cleanStart = (flags & 0x02U) != 0;
    hasWill = (flags & 0x04U) != 0;
    willQoS = (uint8_t)((flags >> 3) & 0x03U);
    willRetain = (flags & 0x20U) != 0;
    if (hasWill && willQoS > 2) { failReason = XMqtt_ReasonCode_MalformedPacket; goto reject; }
    if (!hasWill && (willQoS != 0 || willRetain)) {
        failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
    }
    if (level == 3 && (flags & 0x40U) && !(flags & 0x80U)) {
        /* MQTT 3.1：密码标志必须伴随用户名标志 */
        failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
    }
    if (level == 5) {
        XMqttReader propReader = *reader;
        size_t end;
        uint32_t receiveMaximum = 0;
        uint32_t maxPacketSize = 0;
        uint32_t maxTopicAlias = 0;
        if (!server_reader_property_end(&propReader, &end)) {
            failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
        }
        while (propReader.ok && propReader.pos < end) {
            uint8_t id = server_read_u8(&propReader);
            switch (id) {
            case MQTT_PROP_SESSION_EXPIRY:
                sessionExpiry = server_read_u32(&propReader);
                break;
            case MQTT_PROP_RECEIVE_MAXIMUM:
                receiveMaximum = server_read_u16(&propReader);
                if (!receiveMaximum) { failReason = XMqtt_ReasonCode_ProtocolError; goto reject; }
                break;
            case MQTT_PROP_MAXIMUM_PACKET_SIZE:
                maxPacketSize = server_read_u32(&propReader);
                if (!maxPacketSize) { failReason = XMqtt_ReasonCode_ProtocolError; goto reject; }
                break;
            case MQTT_PROP_TOPIC_ALIAS_MAXIMUM:
                maxTopicAlias = server_read_u16(&propReader);
                break;
            case MQTT_PROP_AUTH_METHOD: {
                XString* method = server_read_string(&propReader);
                /* 当前实现不提供增强认证，要求空方法 */
                if (method && server_string_present(method)) {
                    XString_delete_base(method);
                    failReason = XMqtt_ReasonCode_InvalidAuthenticationMethod;
                    goto reject;
                }
                if (method) XString_delete_base(method);
                break;
            }
            case MQTT_PROP_AUTH_DATA: {
                XByteArray* data = server_read_binary(&propReader);
                if (data && XByteArray_size_base(data)) {
                    XByteArray_delete_base(data);
                    failReason = XMqtt_ReasonCode_InvalidAuthenticationMethod;
                    goto reject;
                }
                if (data) XByteArray_delete_base(data);
                break;
            }
            case MQTT_PROP_REQUEST_PROBLEM_INFO:
                (void)server_read_u8(&propReader);
                break;
            case MQTT_PROP_REQUEST_RESPONSE_INFO:
                (void)server_read_u8(&propReader);
                break;
            case MQTT_PROP_USER_PROPERTY: {
                XString* name = server_read_string(&propReader);
                XString* value = server_read_string(&propReader);
                if (name) XString_delete_base(name);
                if (value) XString_delete_base(value);
                break;
            }
            default:
                failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
            }
        }
        if (!propReader.ok) { failReason = XMqtt_ReasonCode_MalformedPacket; goto reject; }
        reader->pos = end;
        client->receiveMaximum = receiveMaximum ? (uint16_t)receiveMaximum : 65535;
        client->maxPacketSize = maxPacketSize;
        if (maxTopicAlias)
            client->inboundAliasMaximum = (uint16_t)maxTopicAlias;
        if (sessionExpiry == 0 && !cleanStart) {
            /* MQTT 5：Clean Start=0 且会话过期=0 属于协议错误 */
            failReason = XMqtt_ReasonCode_ProtocolError; goto reject;
        }
    }
    clientId = server_read_string(reader);
    if (!clientId || !reader->ok) { failReason = XMqtt_ReasonCode_MalformedPacket; goto reject; }
    if (hasWill && level == 5) {
        XMqttReader propReader = *reader;
        size_t end;
        willProps = XMqttPublishProperties_create();
        if (!willProps) { failReason = XMqtt_ReasonCode_ImplementationSpecificError; goto reject; }
        if (!server_reader_property_end(&propReader, &end)) {
            failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
        }
        while (propReader.ok && propReader.pos < end) {
            uint8_t id = server_read_u8(&propReader);
            switch (id) {
            case MQTT_PROP_WILL_DELAY:
                willDelay = server_read_u32(&propReader);
                break;
            case MQTT_PROP_PAYLOAD_FORMAT:
                willProps->m_payloadFormatIndicator = server_read_u8(&propReader);
                willProps->m_availableProperties |=
                    XMqttPublishProperties_PayloadFormatIndicator;
                break;
            case MQTT_PROP_MESSAGE_EXPIRY:
                willProps->m_messageExpiryInterval = server_read_u32(&propReader);
                willProps->m_availableProperties |=
                    XMqttPublishProperties_MessageExpiryInterval;
                break;
            case MQTT_PROP_CONTENT_TYPE:
                server_replace_string(&willProps->m_contentType,
                                      server_read_string(&propReader));
                willProps->m_availableProperties |= XMqttPublishProperties_ContentType;
                break;
            case MQTT_PROP_RESPONSE_TOPIC:
                server_replace_string(&willProps->m_responseTopic,
                                      server_read_string(&propReader));
                willProps->m_availableProperties |= XMqttPublishProperties_ResponseTopic;
                break;
            case MQTT_PROP_CORRELATION_DATA:
                server_replace_binary(&willProps->m_correlationData,
                                      server_read_binary(&propReader));
                willProps->m_availableProperties |= XMqttPublishProperties_CorrelationData;
                break;
            case MQTT_PROP_USER_PROPERTY: {
                XString* name = server_read_string(&propReader);
                XString* value = server_read_string(&propReader);
                if (name && value && willProps->m_userProperties) {
                    XMqttStringPair pair;
                    XMqttStringPair_init(&pair, XString_toUtf8(name), XString_toUtf8(value));
                    XVector_push_back_1_base((XVector*)willProps->m_userProperties, &pair);
                    XMqttStringPair_deinit_base(&pair);
                }
                if (name) XString_delete_base(name);
                if (value) XString_delete_base(value);
                break;
            }
            default:
                failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
            }
        }
        if (!propReader.ok) { failReason = XMqtt_ReasonCode_MalformedPacket; goto reject; }
        reader->pos = end;
    }
    if (hasWill) {
        willTopic = server_read_string(reader);
        willMessage = server_read_binary(reader);
        if (!willTopic || !willMessage || !server_topic_name_valid(willTopic)) {
            failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
        }
    }
    if (flags & 0x80U) {
        username = server_read_string(reader);
        if (!username) { failReason = XMqtt_ReasonCode_MalformedPacket; goto reject; }
    }
    if (flags & 0x40U) {
        password = server_read_binary(reader);
        if (!password) { failReason = XMqtt_ReasonCode_MalformedPacket; goto reject; }
    }
    if (reader->pos != reader->size) {
        failReason = XMqtt_ReasonCode_MalformedPacket; goto reject;
    }
    /* 客户端 ID 规则 */
    if (level == 3) {
        size_t len = XString_toUtf8_length(clientId);
        const char* data = XString_toUtf8(clientId);
        if (len == 0 || len > 23) { failReason = XMqtt_ReasonCode_InvalidClientId; goto reject; }
        for (size_t i = 0; i < len; ++i) {
            char c = data[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z'))) {
                failReason = XMqtt_ReasonCode_InvalidClientId; goto reject;
            }
        }
    } else if (XString_toUtf8_length(clientId) == 0) {
        if (!cleanStart && level != 5) {
            failReason = XMqtt_ReasonCode_InvalidClientId; goto reject;
        }
        /* 空 ID：服务器分配内部 ID（v5 通过属性回传） */
        server_replace_string(&clientId, server_generate_client_id());
        if (!clientId) { failReason = XMqtt_ReasonCode_ImplementationSpecificError; goto reject; }
        client->assignedClientId = XString_create_copy(clientId);
        if (!client->assignedClientId) {
            failReason = XMqtt_ReasonCode_ImplementationSpecificError; goto reject;
        }
    }
    /* 认证回调 */
    if (priv->m_authenticator) {
        const char* userStr = username ? XString_toUtf8(username) : NULL;
        const char* passStr = NULL;
        char* passBuf = NULL;
        if (password && XByteArray_size_base(password)) {
            size_t passLen = (size_t)XByteArray_size_base(password);
            passBuf = (char*)XMemory_malloc(passLen + 1, XCLASS_DEFAULT_MEMORY_TYPE);
            if (passBuf) {
                memcpy(passBuf, XByteArray_constData(password), passLen);
                passBuf[passLen] = '\0';
                passStr = passBuf;
            }
        }
        if (priv->m_authenticator(priv->m_authContext, userStr, passStr) != 0) {
            if (passBuf) XMemory_free(passBuf, XCLASS_DEFAULT_MEMORY_TYPE);
            failReason = XMqtt_ReasonCode_NotAuthorized;
            goto reject;
        }
        if (passBuf) XMemory_free(passBuf, XCLASS_DEFAULT_MEMORY_TYPE);
    }
    /* 同 clientId 抢占旧连接：先发布旧遗嘱再关闭旧连接 */
    oldClient = NULL;
    {
        XVector* clients = XMapBase_keys_base((XMapBase*)priv->m_clients);
        if (clients) {
            for (size_t i = 0; i < XVector_size_base(clients); ++i) {
                void** key = (void**)XVector_at_base(clients, (int64_t)i);
                XMqttServerClient* candidate = key ?
                    server_find_client(server, *key) : NULL;
                if (candidate && candidate != client && candidate->connected &&
                    candidate->clientId &&
                    XString_equals(candidate->clientId, clientId, XChar_CaseSensitive)) {
                    oldClient = candidate;
                    oldClientId = *key ? *(XString**)XVector_at_base(clients, (int64_t)i) : NULL;
                    break;
                }
            }
            XVector_delete_base(clients);
        }
    }
    if (oldClient) {
        void* oldTransport = oldClient->transport;
        server_discard_client(server, oldTransport, true);
        XMqttServer_closeClient_base(server, oldTransport);
        XMqttServer_clientDisconnected_signal(server, oldTransport);
    }
    /* 会话选择 */
    if (cleanStart) {
        XMqttServerSession* old = server_find_session(server, clientId);
        if (old) {
            server_session_stop_expiry(server, old);
            XMapBase_remove_base((XMapBase*)priv->m_sessions, &clientId);
        }
        session = server_session_create(clientId);
        if (!session) { failReason = XMqtt_ReasonCode_ImplementationSpecificError; goto reject; }
        session->persistent = level == 5 ? (sessionExpiry > 0) : !cleanStart;
        session->expireAt = level == 5 && sessionExpiry > 0 ?
            XDateTime_currentMSecsSinceEpoch() + (int64_t)sessionExpiry * 1000 : -1;
        if (!XMapBase_insert_valueMove_base((XMapBase*)priv->m_sessions, &session->clientId, &session)) {
            server_session_delete(session);
            failReason = XMqtt_ReasonCode_ImplementationSpecificError; goto reject;
        }
    } else {
        session = server_find_session(server, clientId);
        if (!session) {
            session = server_session_create(clientId);
            if (!session) { failReason = XMqtt_ReasonCode_ImplementationSpecificError; goto reject; }
            session->persistent = level == 5 ? (sessionExpiry > 0) : true;
            session->expireAt = level == 5 && sessionExpiry > 0 ?
                XDateTime_currentMSecsSinceEpoch() + (int64_t)sessionExpiry * 1000 : -1;
            if (!XMapBase_insert_valueMove_base((XMapBase*)priv->m_sessions, &session->clientId, &session)) {
                server_session_delete(session);
                failReason = XMqtt_ReasonCode_ImplementationSpecificError; goto reject;
            }
        } else {
            sessionPresent = true;
            server_session_stop_expiry(server, session);
        }
    }
    /* 取消同一 clientId 的延迟遗嘱 */
    server_cancel_pending_will(server, clientId);
    /* 绑定会话与客户端 */
    session->client = client;
    client->session = session;
    server_replace_string(&client->clientId, XString_create_copy(clientId));
    client->cleanSession = cleanStart;
    client->keepAlive = keepAlive;
    client->sessionExpiry = sessionExpiry;
    if (hasWill) {
        server_replace_string(&client->willTopic, willTopic); willTopic = NULL;
        server_replace_binary(&client->willMessage, willMessage); willMessage = NULL;
        client->willQoS = willQoS;
        client->willRetain = willRetain;
        client->willDelay = willDelay;
        if (willProps) {
            if (client->willProperties) XMqttPublishProperties_delete_base(client->willProperties);
            client->willProperties = willProps; willProps = NULL;
        }
    } else {
        if (client->willTopic) XString_delete_base(client->willTopic);
        if (client->willMessage) XByteArray_delete_base(client->willMessage);
        if (client->willProperties) XMqttPublishProperties_delete_base(client->willProperties);
        client->willTopic = NULL;
        client->willMessage = NULL;
        client->willProperties = NULL;
        client->willQoS = 0;
        client->willRetain = false;
        client->willDelay = 0;
    }
    server_replace_string(&client->username, username); username = NULL;
    server_replace_binary(&client->password, password); password = NULL;
    client->connected = true;
    client->disconnectReceived = false;
    client->lastActivity = XDateTime_currentMSecsSinceEpoch();
    server_start_keep_alive(server, client);
    if (!server_send_connack(server, client, XMqtt_ReasonCode_Success, sessionPresent)) {
        server_close_transport(server, client);
        goto cleanup;
    }
    /* 恢复/投递持久会话离线消息与重发在途消息 */
    server_resume_session(server, client);
    XMqttServer_clientConnected_signal(server, client->transport);
    goto cleanup;

reject:
    if (client->protocolVersion == 5) {
        server_send_connack(server, client, failReason, false);
    } else {
        uint8_t legacy = 0;
        switch (failReason) {
        case XMqtt_ReasonCode_UnsupportedProtocolVersion: legacy = 1; break;
        case XMqtt_ReasonCode_InvalidClientId: legacy = 2; break;
        case XMqtt_ReasonCode_ServerNotAvailable: legacy = 3; break;
        case XMqtt_ReasonCode_InvalidUserNameOrPassword: legacy = 4; break;
        case XMqtt_ReasonCode_NotAuthorized: legacy = 5; break;
        default: legacy = 3; break;
        }
        server_send_connack(server, client, legacy, false);
    }
    server_close_transport(server, client);
    goto cleanup;

protocol_error:
    server_close_transport(server, client);

cleanup:
    if (protocolName) XString_delete_base(protocolName);
    if (clientId) XString_delete_base(clientId);
    if (username) XString_delete_base(username);
    if (password) XByteArray_delete_base(password);
    if (willTopic) XString_delete_base(willTopic);
    if (willMessage) XByteArray_delete_base(willMessage);
    if (willProps) XMqttPublishProperties_delete_base(willProps);
}

/* ==================== 路由引擎 ==================== */

/**
 * @brief 收集单个会话中匹配的订阅。
 * @details 返回最大 QoS、匹配订阅个数、订阅标识符合并结果（新建 XVector）。
 * @return 是否存在匹配且允许投递的订阅。
 */
static bool server_collect_matching(XMqttServer* server, XMqttServerSession* session,
                                    const XString* topic, const XMqttServerClient* source,
                                    uint8_t pubQos, uint8_t* maxQos,
                                    XVector** subscriptionIds, bool* deliveredShared,
                                    const XVector* sharedSelections)
{
    XMqttTopicName topicName;
    bool matched = false;
    bool sharedDelivered = false;
    XVector* ids = NULL;
    uint8_t resultQos = 0;
    if (!session || !session->subscriptions || !topic) return false;
    XMqttTopicName_init(&topicName, topic ? XString_toUtf8(topic) : "");
    for (size_t i = 0; i < XVector_size_base(session->subscriptions); ++i) {
        XMqttServerSubscription* sub = (XMqttServerSubscription*)XVector_at_base(
            session->subscriptions, (int64_t)i);
        if (!sub || !sub->actualFilter) continue;
        if (sub->shareName) {
            /* 共享订阅：只投递给本轮组内轮转选中的一名在线成员 */
            XMqttServerSharedSelection* sel;
            if (!session->client) continue;
            if (!server_shared_sub_matches(sub, &topicName)) continue;
            if (sub->noLocal && source && source->session == session) continue;
            sel = server_shared_selection_find((XVector*)sharedSelections,
                                               sub->shareName, sub->actualFilter);
            if (!sel || sel->winner != session) continue;
            sharedDelivered = true;
        } else {
            XMqttTopicFilter filter;
            XMqttTopicFilter_init(&filter, XString_toUtf8(sub->actualFilter));
            if (!XMqttTopicFilter_match(&filter, &topicName,
                    XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption)) {
                XMqttTopicFilter_deinit_base(&filter);
                continue;
            }
            XMqttTopicFilter_deinit_base(&filter);
        }
        if (sub->noLocal && source && source->session == session) continue;
        matched = true;
        if (sub->qos > resultQos) resultQos = sub->qos;
        if (sub->subscriptionId) {
            if (!ids) ids = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(uint32_t), false);
            if (ids) {
                uint32_t id = sub->subscriptionId;
                bool have = false;
                for (size_t k = 0; k < XVector_size_base(ids); ++k) {
                    uint32_t* existing = (uint32_t*)XVector_at_base(ids, (int64_t)k);
                    if (existing && *existing == id) { have = true; break; }
                }
                if (!have) XVector_push_back_1_base(ids, &id);
            }
        }
    }
    XMqttTopicName_deinit_base(&topicName);
    if (maxQos) *maxQos = resultQos;
    if (subscriptionIds) *subscriptionIds = ids;
    else if (ids) XVector_delete_base(ids);
    if (deliveredShared) *deliveredShared = sharedDelivered;
    return matched;
}

/**
 * @brief 向单个在线/离线会话投递一条消息。
 */
static void server_deliver_to_session(XMqttServer* server, XMqttServerSession* session,
                                      const XMqttServerClient* source,
                                      const XString* topic, const XByteArray* payload,
                                      const XMqttPublishProperties* properties,
                                      uint8_t qos, bool retain,
                                      const XVector* sharedSelections)
{
    uint8_t maxSubQos = 0;
    XVector* subscriptionIds = NULL;
    XMqttPublishProperties* merged = NULL;
    bool matched;
    if (!server || !session || !topic) return;
    matched = server_collect_matching(server, session, topic, source, qos,
                                      &maxSubQos, &subscriptionIds, NULL,
                                      sharedSelections);
    if (!matched) {
        if (subscriptionIds) XVector_delete_base(subscriptionIds);
        return;
    }
    qos = qos > maxSubQos ? maxSubQos : qos;
    if (qos > server->m_maximumQoS) qos = server->m_maximumQoS;
    /* 合并订阅标识符到发布属性 */
    if (subscriptionIds && XVector_size_base(subscriptionIds)) {
        merged = properties ? XMqttPublishProperties_create_copy(properties) :
            XMqttPublishProperties_create();
        if (merged) {
            if (merged->m_subscriptionIdentifiers)
                XVector_delete_base(merged->m_subscriptionIdentifiers);
            merged->m_subscriptionIdentifiers = subscriptionIds;
            subscriptionIds = NULL;
            merged->m_availableProperties |= XMqttPublishProperties_SubscriptionIdentifier;
        }
    }
    {
        XMqttServerClient* target = session->client;
        if (target) {
            /* 在线：直接发送，QoS1/2 同时在会话队列跟踪握手 */
            if (qos == 0) {
                server_send_publish(server, target, false, 0, retain, topic, 0,
                                    merged ? merged : properties, payload);
            } else {
                XMqttServerQueuedMessage* queued = server_enqueue_message(
                    session, topic, payload, merged ? merged : properties, qos, retain);
                if (queued) {
                    queued->packetId = server_next_packet_id(target);
                    if (!queued->packetId) {
                        /* 标识符耗尽：从队列移除 */
                        for (size_t i = 0; i < XVector_size_base(session->queuedMessages); ++i) {
                            XMqttServerQueuedMessage* q = server_session_queued_at(session, i);
                            if (q == queued) {
                                server_queued_message_deinit(q);
                                XVector_remove_base(session->queuedMessages, (int64_t)i, 1);
                                queued = NULL;
                                break;
                            }
                        }
                    } else {
                        server_send_publish(server, target, false, qos, retain, topic,
                                            queued->packetId, merged ? merged : properties,
                                            payload);
                    }
                }
            }
        } else if (session->persistent && qos > 0) {
            /* 离线持久会话：排队 */
            server_enqueue_message(session, topic, payload,
                                   merged ? merged : properties, qos, retain);
        }
    }
    if (merged) XMqttPublishProperties_delete_base(merged);
    if (subscriptionIds) XVector_delete_base(subscriptionIds);
}

/**
 * @brief 路由一条消息到所有会话（含保留表更新）。
 */
static void server_route_message(XMqttServer* server, XMqttServerClient* source,
                                 const XString* topic, const XByteArray* payload,
                                 const XMqttPublishProperties* properties,
                                 uint8_t qos, bool retain)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XVector* keys;
    if (!priv || !topic || !server_topic_name_valid(topic)) return;
    if (qos > server->m_maximumQoS) qos = server->m_maximumQoS;
    /* 保留消息表 */
    if (retain) {
        if (payload && XByteArray_size_base(payload)) {
            XMqttServerRetainedMessage* msg =
                (XMqttServerRetainedMessage*)XMalloc_System(sizeof(XMqttServerRetainedMessage));
            XString* topicCopy = XString_create_copy(topic);
            if (msg && topicCopy) {
                memset(msg, 0, sizeof(*msg));
                msg->qos = qos;
                msg->topic = topicCopy;
                msg->payload = XByteArray_create_copy(payload);
                msg->properties = properties ? XMqttPublishProperties_create_copy(properties) : NULL;
                if (msg->payload && (!properties || msg->properties)) {
                    XMqttServerRetainedMessage** slot = (XMqttServerRetainedMessage**)XMapBase_value_base(
                        (XMapBase*)priv->m_retained, &topic);
                    if (slot && *slot) {
                        /* 先移除旧条目再插入新条目：旧条目的键与值同属一条
                         * 保留消息，直接替换值会留下悬垂的键指针。 */
                        XMapBase_remove_base((XMapBase*)priv->m_retained, &topic);
                        XMapBase_insert_valueMove_base((XMapBase*)priv->m_retained,
                                                       &topicCopy, &msg);
                    } else {
                        XMapBase_insert_valueMove_base((XMapBase*)priv->m_retained,
                                                       &topicCopy, &msg);
                    }
                } else {
                    server_retained_deinit(msg);
                    XString_delete_base(topicCopy);
                }
            } else {
                if (msg) XFree_System(msg);
                if (topicCopy) XString_delete_base(topicCopy);
            }
        } else {
            XMapBase_remove_base((XMapBase*)priv->m_retained, &topic);
        }
    }
    /* 共享订阅：先统一构建本轮选择表（每组成员轮转一次） */
    {
        XMqttTopicName topicName;
        XVector* sharedSelections;
        XMqttTopicName_init(&topicName, XString_toUtf8(topic));
        sharedSelections = server_shared_build_selections(server, &topicName);
        /* 遍历所有会话投递 */
        keys = XMapBase_keys_base((XMapBase*)priv->m_sessions);
        if (keys) {
            for (size_t i = 0; i < XVector_size_base(keys); ++i) {
                XString** key = (XString**)XVector_at_base(keys, (int64_t)i);
                XMqttServerSession** slot = key ?
                    (XMqttServerSession**)XMapBase_value_base((XMapBase*)priv->m_sessions, key) : NULL;
                if (!slot || !*slot) continue;
                server_deliver_to_session(server, *slot, source, topic, payload,
                                          properties, qos, retain, sharedSelections);
            }
            XVector_delete_base(keys);
        }
        server_shared_selections_deinit(sharedSelections);
        XMqttTopicName_deinit_base(&topicName);
    }
}

/**
 * @brief 直接发布遗嘱消息（不经过客户端状态）。
 */
static void server_publish_will_direct(XMqttServer* server, const XString* topic,
                                       const XByteArray* payload, uint8_t qos,
                                       bool retain, const XMqttPublishProperties* properties)
{
    if (!server || !topic) return;
    server_route_message(server, NULL, topic, payload, properties, qos, retain);
}

/**
 * @brief 发布一条客户端遗嘱（异常断开时调用）。
 */
static void server_publish_will(XMqttServer* server, XMqttServerClient* client)
{
    if (!server || !client || !client->willTopic || !client->willMessage) return;
    server_publish_will_direct(server, client->willTopic, client->willMessage,
                               client->willQoS, client->willRetain,
                               client->willProperties);
}

/**
 * @brief 取消指定 clientId 的延迟遗嘱。
 */
static void server_cancel_pending_will(XMqttServer* server, const XString* clientId)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    if (!priv || !priv->m_pendingWills || !clientId) return;
    for (size_t i = 0; i < XVector_size_base(priv->m_pendingWills); ++i) {
        XMqttServerPendingWill* will = (XMqttServerPendingWill*)XVector_at_base(
            priv->m_pendingWills, (int64_t)i);
        if (will && will->clientId &&
            XString_equals(will->clientId, clientId, XChar_CaseSensitive)) {
            if (will->timer != XTIMER_INVALID_ID)
                XObject_killTimer((XObject*)server, will->timer);
            server_pending_will_deinit(will);
            XVector_remove_base(priv->m_pendingWills, (int64_t)i, 1);
            return;
        }
    }
}

/**
 * @brief 处理 XMqttServer_publish 公开入口的路由。
 */
static bool server_publish_common(XMqttServer* server, const XString* topic,
                                  const XMqttPublishProperties* properties,
                                  const uint8_t* payload, size_t payloadLen,
                                  uint8_t qos, bool retain)
{
    XByteArray* payloadBuf = NULL;
    if (!server || !topic || !server_topic_name_valid(topic)) return false;
    if (qos > 2) qos = 0;
    if (payloadLen) {
        payloadBuf = XByteArray_create_with_data((const char*)payload, payloadLen);
        if (!payloadBuf) return false;
    } else {
        payloadBuf = XByteArray_create();
        if (!payloadBuf) return false;
    }
    server_route_message(server, NULL, topic, payloadBuf, properties, qos, retain);
    XByteArray_delete_base(payloadBuf);
    return true;
}

/* ==================== 持久会话恢复 ==================== */

/**
 * @brief 恢复持久会话：重发在途 QoS1/2 消息。
 */
static void server_resume_session(XMqttServer* server, XMqttServerClient* client)
{
    XMqttServerSession* session;
    if (!server || !client) return;
    session = client->session;
    if (!session || !session->queuedMessages) return;
    for (size_t i = 0; i < XVector_size_base(session->queuedMessages); ++i) {
        XMqttServerQueuedMessage* queued = server_session_queued_at(session, i);
        if (!queued) continue;
        if (queued->stage == 1) {
            /* 已发送 PUBREL：重发 PUBREL */
            server_send_ack(server, client, MQTT_PUBREL | 0x02U, queued->packetId, 0);
        } else {
            if (!queued->packetId) queued->packetId = server_next_packet_id(client);
            if (queued->packetId)
                server_send_publish(server, client, true, queued->qos, queued->retain,
                                    queued->topic, queued->packetId, queued->properties,
                                    queued->payload);
        }
    }
}

/* ==================== 到期处理 ==================== */

/**
 * @brief 清理到期会话。
 */
static void server_purge_expired_sessions(XMqttServer* server)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XVector* keys;
    int64_t now;
    if (!priv || !priv->m_sessions) return;
    now = XDateTime_currentMSecsSinceEpoch();
    keys = XMapBase_keys_base((XMapBase*)priv->m_sessions);
    if (!keys) return;
    for (size_t i = 0; i < XVector_size_base(keys); ++i) {
        XString** key = (XString**)XVector_at_base(keys, (int64_t)i);
        XMqttServerSession** slot = key ?
            (XMqttServerSession**)XMapBase_value_base((XMapBase*)priv->m_sessions, key) : NULL;
        if (slot && *slot && !(*slot)->client && (*slot)->persistent &&
            (*slot)->expireAt >= 0 && (*slot)->expireAt <= now) {
            server_session_stop_expiry(server, *slot);
            XMapBase_remove_base((XMapBase*)priv->m_sessions, key);
        }
    }
    XVector_delete_base(keys);
}

/* ==================== 定时器事件 ==================== */

static void server_timer_event(XMqttServer* server, XTimerEvent* event)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XTimerId timerId;
    int64_t now;
    if (!server || !priv || !event) return;
    timerId = XTimerEvent_timerId(event);
    now = XDateTime_currentMSecsSinceEpoch();
    /* 保活超时检查 */
    if (priv->m_clients) {
        XVector* keys = XMapBase_keys_base((XMapBase*)priv->m_clients);
        if (keys) {
            for (size_t i = 0; i < XVector_size_base(keys); ++i) {
                void** key = (void**)XVector_at_base(keys, (int64_t)i);
                XMqttServerClient** slot = key ?
                    (XMqttServerClient**)XMapBase_value_base((XMapBase*)priv->m_clients, key) : NULL;
                XMqttServerClient* client = slot ? *slot : NULL;
                XTimerId keepAliveId = client ? client->keepAliveTimer : XTIMER_INVALID_ID;
                uint16_t effective;
                int64_t limit;
                if (!client || keepAliveId != timerId) continue;
                effective = server->m_serverKeepAlive ? server->m_serverKeepAlive
                                                      : client->keepAlive;
                if (!effective) { client->keepAliveTimer = XTIMER_INVALID_ID; continue; }
                limit = (int64_t)effective * 1000 * MQTT_SERVER_KEEPALIVE_FACTOR /
                        MQTT_SERVER_KEEPALIVE_DIVISOR;
                if (now - client->lastActivity > limit) {
                    void* transport = client->transport;
                    /* 保活超时：异常断开，发布遗嘱 */
                    server_discard_client(server, transport, true);
                    XMqttServer_closeClient_base(server, transport);
                    XMqttServer_clientDisconnected_signal(server, transport);
                    break;
                }
                client->keepAliveTimer = XTIMER_INVALID_ID;
                server_start_keep_alive(server, client);
            }
            XVector_delete_base(keys);
        }
    }
    /* 会话过期定时器 */
    server_purge_expired_sessions(server);
    /* 延迟遗嘱定时器 */
    if (priv->m_pendingWills) {
        for (size_t i = 0; i < XVector_size_base(priv->m_pendingWills); ++i) {
            XMqttServerPendingWill* will = (XMqttServerPendingWill*)XVector_at_base(
                priv->m_pendingWills, (int64_t)i);
            if (will && will->timer == timerId) {
                server_publish_will_direct(server, will->topic, will->payload,
                                           will->qos, will->retain, will->properties);
                server_pending_will_deinit(will);
                XVector_remove_base(priv->m_pendingWills, (int64_t)i, 1);
                break;
            }
        }
    }
}

/* ==================== 报文派发 ==================== */

static void server_handle_publish(XMqttServer* server, XMqttServerClient* client,
                                  uint8_t header, XMqttReader* reader)
{
    XString* topic = NULL;
    XMqttPublishProperties* props = NULL;
    /* 用位域联合体解析固定头第一字节 */
    XMqttFixedHeader fixed = { header };
    uint8_t qos = (uint8_t)fixed.bits.qos;
    bool retain = fixed.bits.retain != 0;
    bool dup = fixed.bits.dup != 0;
    uint16_t packetId = 0;
    XByteArray* payload = NULL;
    if (qos == 3 || (qos == 0 && dup)) { reader->ok = false; return; }
    if (!client->connected) { reader->ok = false; return; }
    if (retain && !server->m_retainAvailable) {
        server_send_disconnect(server, client, XMqtt_ReasonCode_RetainNotSupported);
        server_close_transport(server, client);
        reader->ok = false; return;
    }
    if (qos > server->m_maximumQoS) {
        server_send_disconnect(server, client, XMqtt_ReasonCode_QoSNotSupported);
        server_close_transport(server, client);
        reader->ok = false; return;
    }
    topic = server_read_string(reader);
    if (qos) packetId = server_read_u16(reader);
    if (qos && !packetId) { reader->ok = false; goto cleanup; }
    if (client->protocolVersion == 5) {
        size_t end;
        props = XMqttPublishProperties_create();
        if (!props || !server_reader_property_end(reader, &end)) {
            reader->ok = false; goto cleanup;
        }
        while (reader->ok && reader->pos < end) {
            uint8_t id = server_read_u8(reader);
            switch (id) {
            case MQTT_PROP_PAYLOAD_FORMAT: {
                uint8_t value = server_read_u8(reader);
                if (value > 1) { reader->ok = false; goto cleanup; }
                props->m_payloadFormatIndicator = (XMqtt_PayloadFormatIndicator)value;
                props->m_availableProperties |= XMqttPublishProperties_PayloadFormatIndicator;
                break;
            }
            case MQTT_PROP_MESSAGE_EXPIRY:
                props->m_messageExpiryInterval = server_read_u32(reader);
                props->m_availableProperties |= XMqttPublishProperties_MessageExpiryInterval;
                break;
            case MQTT_PROP_TOPIC_ALIAS:
                props->m_topicAlias = server_read_u16(reader);
                props->m_availableProperties |= XMqttPublishProperties_TopicAlias;
                break;
            case MQTT_PROP_RESPONSE_TOPIC:
                server_replace_string(&props->m_responseTopic, server_read_string(reader));
                props->m_availableProperties |= XMqttPublishProperties_ResponseTopic;
                break;
            case MQTT_PROP_CORRELATION_DATA:
                server_replace_binary(&props->m_correlationData, server_read_binary(reader));
                props->m_availableProperties |= XMqttPublishProperties_CorrelationData;
                break;
            case MQTT_PROP_USER_PROPERTY: {
                XString* name = server_read_string(reader);
                XString* value = server_read_string(reader);
                if (name && value && props->m_userProperties) {
                    XMqttStringPair pair;
                    XMqttStringPair_init(&pair, XString_toUtf8(name), XString_toUtf8(value));
                    XVector_push_back_1_base((XVector*)props->m_userProperties, &pair);
                    XMqttStringPair_deinit_base(&pair);
                }
                if (name) XString_delete_base(name);
                if (value) XString_delete_base(value);
                break;
            }
            case MQTT_PROP_CONTENT_TYPE:
                server_replace_string(&props->m_contentType, server_read_string(reader));
                props->m_availableProperties |= XMqttPublishProperties_ContentType;
                break;
            default:
                reader->ok = false; goto cleanup;
            }
        }
        if (!reader->ok || reader->pos != end) { reader->ok = false; goto cleanup; }
        /* 主题别名解析 */
        if (props->m_topicAlias) {
            if (!server->m_topicAliasMaximum || props->m_topicAlias > server->m_topicAliasMaximum) {
                server_send_disconnect(server, client, XMqtt_ReasonCode_ProtocolError);
                server_close_transport(server, client);
                reader->ok = false; goto cleanup;
            }
            if (server_string_present(topic)) {
                if (!server_alias_set(client->inboundAliases, props->m_topicAlias, topic))
                    reader->ok = false;
            } else {
                XMqttServerTopicAlias* alias = server_alias_by_id(client->inboundAliases,
                                                                   props->m_topicAlias);
                if (!alias) {
                    server_send_disconnect(server, client, XMqtt_ReasonCode_ProtocolError);
                    server_close_transport(server, client);
                    reader->ok = false; goto cleanup;
                }
                server_replace_string(&topic, XString_create_copy(alias->topic));
            }
        }
    }
    if (!topic || !server_topic_name_valid(topic)) {
        reader->ok = false; goto cleanup;
    }
    payload = XByteArray_create_with_data(
        (const char*)reader->data + reader->pos, reader->size - reader->pos);
    if (!payload) { reader->ok = false; goto cleanup; }
    reader->pos = reader->size;
    /* QoS2 去重 */
    if (qos == 2) {
        bool seen = false;
        for (size_t i = 0; i < XVector_size_base(client->incomingQos2); ++i) {
            uint16_t* value = (uint16_t*)XVector_at_base(client->incomingQos2, (int64_t)i);
            if (value && *value == packetId) { seen = true; break; }
        }
        if (!seen) {
            server_route_message(server, client, topic, payload, props, qos, retain);
            XVector_push_back_1_base(client->incomingQos2, &packetId);
        }
    } else {
        server_route_message(server, client, topic, payload, props, qos, retain);
    }
    /* 信号 */
    {
        XMqttTopicName* topicName = XMqttTopicName_create(XString_toUtf8(topic));
        if (topicName) {
            XMqttServer_messageReceived_signal(server, client->transport, topicName, payload);
            XMqttTopicName_delete_base(topicName);
        }
    }
    if (qos == 1) server_send_ack(server, client, MQTT_PUBACK, packetId, 0);
    if (qos == 2) server_send_ack(server, client, MQTT_PUBREC, packetId, 0);
cleanup:
    if (topic) XString_delete_base(topic);
    if (props) XMqttPublishProperties_delete_base(props);
    if (payload) XByteArray_delete_base(payload);
}

static void server_handle_subscribe(XMqttServer* server, XMqttServerClient* client,
                                    XMqttReader* reader)
{
    XMqttServerSession* session;
    uint16_t packetId;
    uint8_t reason[64];
    size_t reasonCount = 0;
    XByteArray* propsBuf = NULL;
    if (!client || !client->session || !client->connected) { reader->ok = false; return; }
    session = client->session;
    packetId = server_read_u16(reader);
    if (!packetId) { reader->ok = false; return; }
    if (client->protocolVersion == 5) {
        size_t end;
        propsBuf = XByteArray_create();
        if (!server_reader_property_end(reader, &end)) { reader->ok = false; goto cleanup; }
        while (reader->ok && reader->pos < end) {
            uint8_t id = server_read_u8(reader);
            if (id == MQTT_PROP_USER_PROPERTY) {
                XString* name = server_read_string(reader);
                XString* value = server_read_string(reader);
                if (name) XString_delete_base(name);
                if (value) XString_delete_base(value);
            } else {
                reader->ok = false;
            }
        }
        if (!reader->ok || reader->pos != end) { reader->ok = false; goto cleanup; }
    }
    while (reader->ok && reader->pos < reader->size) {
        XMqttServerSubscription sub;
        XString* filter = server_read_string(reader);
        uint8_t options = server_read_u8(reader);
        uint8_t qos = options & 0x03U;
        bool noLocal = (options & 0x04U) != 0;
        bool retainAsPublished = (options & 0x08U) != 0;
        uint8_t retainHandling = (uint8_t)((options >> 4) & 0x03U);
        uint32_t subscriptionId = 0;
        XString* shareName = NULL;
        XString* actualFilter = NULL;
        if (!filter || !reader->ok) {
            if (filter) XString_delete_base(filter);
            reason[reasonCount] = XMqtt_ReasonCode_ProtocolError;
            ++reasonCount;
            continue;
        }
        memset(&sub, 0, sizeof(sub));
        sub.qos = qos;
        sub.noLocal = noLocal;
        sub.retainAsPublished = retainAsPublished;
        sub.retainHandling = retainHandling;
        if (client->protocolVersion != 5 && (noLocal || retainAsPublished ||
                                             retainHandling || (options & 0xC0U))) {
            XString_delete_base(filter);
            reason[reasonCount] = XMqtt_ReasonCode_ProtocolError;
            ++reasonCount;
            continue;
        }
        if (options & 0xC0U) {
            XString_delete_base(filter);
            reason[reasonCount] = XMqtt_ReasonCode_ProtocolError;
            ++reasonCount;
            continue;
        }
        if (qos > 2) {
            XString_delete_base(filter);
            reason[reasonCount] = XMqtt_ReasonCode_MalformedPacket;
            ++reasonCount;
            continue;
        }
        if (qos > server->m_maximumQoS) {
            XString_delete_base(filter);
            reason[reasonCount] = XMqtt_ReasonCode_QoSNotSupported;
            ++reasonCount;
            continue;
        }
        /* 订阅标识符（v5，属性在过滤器之前解析：实际在 SUBSCRIBE 属性块中） */
        if (XString_toUtf8_length(filter) >= 8 &&
            memcmp(XString_toUtf8(filter), "$share/", 7) == 0) {
            if (!server->m_sharedAvailable) {
                XString_delete_base(filter);
                reason[reasonCount] = XMqtt_ReasonCode_SharedSubscriptionsNotSupported;
                ++reasonCount;
                continue;
            }
            if (!server_parse_shared_filter(filter, &shareName, &actualFilter)) {
                XString_delete_base(filter);
                reason[reasonCount] = XMqtt_ReasonCode_InvalidTopicFilter;
                ++reasonCount;
                continue;
            }
        } else {
            actualFilter = XString_create_copy(filter);
            if (!actualFilter) {
                XString_delete_base(filter);
                reason[reasonCount] = XMqtt_ReasonCode_ImplementationSpecificError;
                ++reasonCount;
                continue;
            }
        }
        if (!server_topic_filter_valid(actualFilter, server->m_wildcardAvailable)) {
            if (shareName) XString_delete_base(shareName);
            XString_delete_base(actualFilter);
            XString_delete_base(filter);
            reason[reasonCount] = server->m_wildcardAvailable ?
                XMqtt_ReasonCode_InvalidTopicFilter : XMqtt_ReasonCode_WildCardSubscriptionsNotSupported;
            ++reasonCount;
            continue;
        }
        sub.filter = filter;
        sub.actualFilter = actualFilter;
        sub.shareName = shareName;
        if (!server_subscription_set(session, &sub)) {
            server_subscription_deinit(&sub);
            reason[reasonCount] = XMqtt_ReasonCode_ImplementationSpecificError;
            ++reasonCount;
            continue;
        }
        reason[reasonCount] = qos;
        ++reasonCount;
        /* 新订阅下发保留消息（retainHandling 0/1）。
         * 注意：server_subscription_set 已把所有权移入会话并把 sub 清零，
         * 这里必须使用局部 filter/actualFilter（对象仍由订阅条目持有）。 */
        if (retainHandling < 2 && server->m_retainAvailable &&
            !(retainHandling == 1 && server_subscription_exists(session, filter))) {
            server_deliver_retained(server, client, filter, actualFilter, qos);
        }
    }
    if (reasonCount > sizeof(reason) / sizeof(reason[0]))
        reasonCount = sizeof(reason) / sizeof(reason[0]);
    if (reasonCount)
        server_send_suback(server, client, packetId, reason, reasonCount);
cleanup:
    if (propsBuf) XByteArray_delete_base(propsBuf);
}

static bool server_subscription_exists(const XMqttServerSession* session,
                                       const XString* filter)
{
    if (!session || !session->subscriptions || !filter) return false;
    for (size_t i = 0; i < XVector_size_base(session->subscriptions); ++i) {
        const XMqttServerSubscription* sub = (const XMqttServerSubscription*)XVector_at_base(
            session->subscriptions, (int64_t)i);
        if (sub && sub->filter &&
            XString_equals(sub->filter, filter, XChar_CaseSensitive)) return true;
    }
    return false;
}

/**
 * @brief 向新订阅者投递保留消息。
 */
static void server_deliver_retained(XMqttServer* server, XMqttServerClient* client,
                                    const XString* filter, const XString* actualFilter,
                                    uint8_t qos)
{
    XMqttServerPrivate* priv = server ? server->m_private : NULL;
    XVector* keys;
    XMqttTopicName topicName;
    XMqttTopicFilter topicFilter;
    if (!priv || !priv->m_retained || !client || !filter) return;
    keys = XMapBase_keys_base((XMapBase*)priv->m_retained);
    if (!keys) return;
    XMqttTopicFilter_init(&topicFilter, XString_toUtf8(actualFilter ? actualFilter : filter));
    for (size_t i = 0; i < XVector_size_base(keys); ++i) {
        XString** key = (XString**)XVector_at_base(keys, (int64_t)i);
        XMqttServerRetainedMessage** slot = key ?
            (XMqttServerRetainedMessage**)XMapBase_value_base((XMapBase*)priv->m_retained, key) : NULL;
        if (!slot || !*slot) continue;
        XMqttTopicName_init(&topicName, XString_toUtf8((*slot)->topic));
        if (XMqttTopicFilter_match(&topicFilter, &topicName,
                XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption)) {
            uint8_t deliverQos = (*slot)->qos;
            if (deliverQos > qos) deliverQos = qos;
            if (deliverQos > server->m_maximumQoS) deliverQos = server->m_maximumQoS;
            /* MQTT-3.3.1-7：因新订阅下发保留消息时 RETAIN 标志必须为 1，
             * 与订阅的 Retain As Published 选项无关。 */
            if (deliverQos == 0) {
                server_send_publish(server, client, false, 0,
                                    true, (*slot)->topic, 0,
                                    (*slot)->properties, (*slot)->payload);
            } else {
                XMqttServerQueuedMessage* queued = server_enqueue_message(
                    client->session, (*slot)->topic, (*slot)->payload,
                    (*slot)->properties, deliverQos, true);
                if (queued) {
                    queued->packetId = server_next_packet_id(client);
                    if (queued->packetId)
                        server_send_publish(server, client, false, deliverQos,
                                            true, (*slot)->topic,
                                            queued->packetId, (*slot)->properties,
                                            (*slot)->payload);
                    else {
                        for (size_t j = 0; j < XVector_size_base(client->session->queuedMessages); ++j) {
                            XMqttServerQueuedMessage* q = server_session_queued_at(client->session, j);
                            if (q == queued) {
                                server_queued_message_deinit(q);
                                XVector_remove_base(client->session->queuedMessages, (int64_t)j, 1);
                                break;
                            }
                        }
                    }
                }
            }
        }
        XMqttTopicName_deinit_base(&topicName);
    }
    XMqttTopicFilter_deinit_base(&topicFilter);
    XVector_delete_base(keys);
}

static void server_handle_unsubscribe(XMqttServer* server, XMqttServerClient* client,
                                      XMqttReader* reader)
{
    XMqttServerSession* session;
    uint16_t packetId;
    uint8_t reason[64];
    size_t reasonCount = 0;
    if (!client || !client->session || !client->connected) { reader->ok = false; return; }
    session = client->session;
    packetId = server_read_u16(reader);
    if (!packetId) { reader->ok = false; return; }
    if (client->protocolVersion == 5) {
        size_t end;
        if (!server_reader_property_end(reader, &end)) { reader->ok = false; return; }
        while (reader->ok && reader->pos < end) {
            uint8_t id = server_read_u8(reader);
            if (id == MQTT_PROP_USER_PROPERTY) {
                XString* name = server_read_string(reader);
                XString* value = server_read_string(reader);
                if (name) XString_delete_base(name);
                if (value) XString_delete_base(value);
            } else {
                reader->ok = false;
            }
        }
        if (!reader->ok || reader->pos != end) return;
    }
    while (reader->ok && reader->pos < reader->size) {
        XString* filter = server_read_string(reader);
        if (!filter || !reader->ok) {
            if (filter) XString_delete_base(filter);
            reason[reasonCount] = XMqtt_ReasonCode_ProtocolError;
            ++reasonCount;
            continue;
        }
        if (server_session_remove_subscription(session, filter)) {
            reason[reasonCount] = XMqtt_ReasonCode_Success;
        } else {
            reason[reasonCount] = XMqtt_ReasonCode_NoSubscriptionExisted;
        }
        ++reasonCount;
        XString_delete_base(filter);
    }
    if (reasonCount > sizeof(reason) / sizeof(reason[0]))
        reasonCount = sizeof(reason) / sizeof(reason[0]);
    if (reasonCount)
        server_send_unsuback(server, client, packetId, reason, reasonCount);
}

static void server_handle_puback(XMqttServer* server, XMqttServerClient* client,
                                 uint8_t type, XMqttReader* reader)
{
    XMqttServerSession* session;
    uint16_t identifier;
    if (!client || !client->session) { reader->ok = false; return; }
    session = client->session;
    identifier = server_read_u16(reader);
    if (!identifier) { reader->ok = false; return; }
    if (client->protocolVersion == 5 && reader->pos < reader->size) {
        (void)server_read_u8(reader); /* 原因码 */
        if (reader->pos < reader->size) {
            size_t end;
            if (!server_reader_property_end(reader, &end)) { reader->ok = false; return; }
            reader->pos = end;
        }
    }
    for (size_t i = 0; i < XVector_size_base(session->queuedMessages); ++i) {
        XMqttServerQueuedMessage* queued = server_session_queued_at(session, i);
        if (!queued || queued->packetId != identifier) continue;
        if (type == MQTT_PUBACK && queued->qos == 1 && queued->stage == 0) {
            server_queued_message_deinit(queued);
            XVector_remove_base(session->queuedMessages, (int64_t)i, 1);
        } else if (type == MQTT_PUBREC && queued->qos == 2 && queued->stage == 0) {
            queued->stage = 1;
            server_send_ack(server, client, MQTT_PUBREL | 0x02U, identifier, 0);
        } else if (type == MQTT_PUBCOMP && queued->qos == 2 && queued->stage == 1) {
            server_queued_message_deinit(queued);
            XVector_remove_base(session->queuedMessages, (int64_t)i, 1);
        }
        return;
    }
}

static void server_handle_pubrel(XMqttServer* server, XMqttServerClient* client,
                                 XMqttReader* reader)
{
    uint16_t identifier;
    if (!client) { reader->ok = false; return; }
    identifier = server_read_u16(reader);
    if (!identifier) { reader->ok = false; return; }
    if (client->protocolVersion == 5 && reader->pos < reader->size) {
        (void)server_read_u8(reader);
        if (reader->pos < reader->size) {
            size_t end;
            if (!server_reader_property_end(reader, &end)) { reader->ok = false; return; }
            reader->pos = end;
        }
    }
    for (size_t i = 0; i < XVector_size_base(client->incomingQos2); ++i) {
        uint16_t* value = (uint16_t*)XVector_at_base(client->incomingQos2, (int64_t)i);
        if (value && *value == identifier) {
            XVector_remove_base(client->incomingQos2, (int64_t)i, 1);
            break;
        }
    }
    server_send_ack(server, client, MQTT_PUBCOMP, identifier, 0);
}

static void server_handle_pingreq(XMqttServer* server, XMqttServerClient* client)
{
    if (!client || !client->connected) return;
    server_send_packet(server, client, MQTT_PINGRESP, NULL);
}

static void server_handle_disconnect(XMqttServer* server, XMqttServerClient* client,
                                     XMqttReader* reader)
{
    if (!client || !client->connected) { reader->ok = false; return; }
    if (client->protocolVersion == 5) {
        if (reader->pos < reader->size) {
            uint8_t reason = server_read_u8(reader);
            if (reason >= 0x80) {
                /* 客户端以错误码断开：视为异常，发布遗嘱 */
                client->disconnectReceived = false;
            } else {
                client->disconnectReceived = true;
            }
            if (reader->pos < reader->size) {
                size_t end;
                if (!server_reader_property_end(reader, &end)) {
                    reader->ok = false;
                    return;
                }
                reader->pos = end;
            }
        } else {
            client->disconnectReceived = true;
        }
    } else {
        if (reader->pos != reader->size) { reader->ok = false; return; }
        client->disconnectReceived = true;
    }
    if (client->session) {
        client->session->client = NULL;
        if (!client->session->persistent) {
            XMqttServerPrivate* priv = server->m_private;
            server_session_stop_expiry(server, client->session);
            if (priv)
                XMapBase_remove_base((XMapBase*)priv->m_sessions,
                                     &client->session->clientId);
        } else {
            server_session_start_expiry(server, client->session);
        }
    }
    server_stop_keep_alive(server, client);
    client->session = NULL;
    client->connected = false;
    client->disconnectReceived = true;
    XMqttServer_closeClient_base(server, client->transport);
}

static void server_handle_auth(XMqttServer* server, XMqttServerClient* client,
                               XMqttReader* reader)
{
    (void)reader;
    if (!client || !client->connected || client->protocolVersion != 5) {
        if (reader) reader->ok = false;
        return;
    }
    /* 当前实现不提供增强认证：回复不支持 */
    server_send_disconnect(server, client, XMqtt_ReasonCode_InvalidAuthenticationMethod);
    server_close_transport(server, client);
}

static void server_process_packet(XMqttServer* server, XMqttServerClient* client,
                                  uint8_t header, const uint8_t* data, size_t size)
{
    XMqttReader reader = {data, size, 0, true};
    const uint8_t flags = header & 0x0FU;
    switch (header & 0xF0U) {
    case MQTT_PUBLISH: break;
    case MQTT_SUBSCRIBE: case MQTT_UNSUBSCRIBE: case MQTT_PUBREL:
        if (flags != 0x02U) reader.ok = false;
        break;
    default:
        if (flags != 0x00U) reader.ok = false;
        break;
    }
    if (!reader.ok) { server_close_transport(server, client); return; }
    switch (header & 0xF0U) {
    case MQTT_CONNECT: server_handle_connect(server, client, &reader); break;
    case MQTT_PUBLISH: server_handle_publish(server, client, header, &reader); break;
    case MQTT_PUBACK: case MQTT_PUBREC: case MQTT_PUBCOMP:
        server_handle_puback(server, client, header & 0xF0U, &reader); break;
    case MQTT_PUBREL: server_handle_pubrel(server, client, &reader); break;
    case MQTT_SUBSCRIBE: server_handle_subscribe(server, client, &reader); break;
    case MQTT_UNSUBSCRIBE: server_handle_unsubscribe(server, client, &reader); break;
    case MQTT_PINGREQ: server_handle_pingreq(server, client); break;
    case MQTT_DISCONNECT: server_handle_disconnect(server, client, &reader); break;
    case MQTT_AUTH: server_handle_auth(server, client, &reader); break;
    default: reader.ok = false; break;
    }
    if (!reader.ok && client && client->connected) {
        if (client->protocolVersion == 5)
            server_send_disconnect(server, client, XMqtt_ReasonCode_MalformedPacket);
        server_close_transport(server, client);
    }
}

void XMqttServer_feedData(XMqttServer* server, void* transport,
                          const uint8_t* data, size_t size)
{
    XMqttServerClient* client;
    if (!server || !transport || (!data && size)) return;
    client = server_find_client(server, transport);
    if (!client || !client->input) return;
    if (!server_append(client->input, data, size)) return;
    client->lastActivity = XDateTime_currentMSecsSinceEpoch();
    while (XByteArray_size_base(client->input) >= 2) {
        const uint8_t* input = (const uint8_t*)XByteArray_constData(client->input);
        size_t inputSize = XByteArray_size_base(client->input);
        uint32_t remaining = 0;
        uint32_t multiplier = 1;
        size_t pos = 1;
        bool completeLength = false;
        for (int i = 0; i < 4 && pos < inputSize; ++i) {
            uint8_t byte = input[pos++];
            remaining += (uint32_t)(byte & 0x7FU) * multiplier;
            if (!(byte & 0x80U)) { completeLength = true; break; }
            multiplier *= 128U;
        }
        if (!completeLength) {
            if (pos >= 5 || inputSize >= 5) server_close_transport(server, client);
            break;
        }
        if (remaining > 268435455U) {
            server_close_transport(server, client);
            break;
        }
        if (server->m_maximumPacketSize != 268435455U &&
            remaining > server->m_maximumPacketSize) {
            server_close_transport(server, client);
            break;
        }
        if (inputSize < pos + remaining) break;
        server_process_packet(server, client, input[0], input + pos, remaining);
        /*
         * 注意：处理报文（如 DISCONNECT、协议错误、恶意报文）可能通过
         * 传输关闭虚函数（XMqttServer_closeClient_base）间接释放当前客户端
         * 对象，因此必须重新查找客户端；找不到说明连接已结束，直接退出
         * 输入缓冲区处理循环，避免悬垂指针访问。
         */
        client = server_find_client(server, transport);
        if (!client) break;
        XByteArray_remove_base(client->input, 0, (int64_t)(pos + remaining));
        if (!client->connected && client->session == NULL && client->transport == NULL) break;
    }
}

/* ==================== 报文处理结束 ==================== */

/* ==================== 虚函数实现 ==================== */

/**
 * @brief 默认发送实现：不发送任何数据。
 * @return 恒为 false，提醒子类必须重写此虚函数。
 */
static bool V_sendData(XMqttServer* server, void* transport,
                       const uint8_t* data, size_t size)
{
    (void)server; (void)transport; (void)data; (void)size;
    return false;
}

/**
 * @brief 默认关闭实现：空操作。
 * @details 基类传输无关，不拥有传输设备；TCP 子类重写为断开底层套接字。
 */
static void V_closeClient(XMqttServer* server, void* transport)
{
    (void)server; (void)transport;
}

/**
 * @brief 定时器事件虚函数：转发给协议引擎处理保活超时、会话过期与延迟遗嘱。
 */
static void V_timerEvent(XObject* object, XTimerEvent* event)
{
    if (!object) return;
    server_timer_event((XMqttServer*)object, event);
}

/**
 * @brief 析构虚函数：释放私有协议状态并调用父类析构。
 */
static void V_deinit(XMqttServer* server)
{
    if (!server) return;
    server_private_delete(server);
    XClass_Deinit_Parent(XObject, server);
}

/* ==================== 私有状态创建/销毁 ==================== */

/**
 * @brief 释放私有状态中的所有容器（不处理定时器）。
 * @param priv 服务器私有状态指针，可空。
 */
static void server_private_cleanup(XMqttServerPrivate* priv)
{
    if (!priv) return;
    if (priv->m_clients) XMapBase_delete_base(priv->m_clients);
    if (priv->m_sessions) XMapBase_delete_base(priv->m_sessions);
    if (priv->m_retained) XMapBase_delete_base(priv->m_retained);
    if (priv->m_sharedIndex) XMapBase_delete_base(priv->m_sharedIndex);
    if (priv->m_pendingWills) XVector_delete_base(priv->m_pendingWills);
    XFree_System(priv);
}

/**
 * @brief 销毁服务器私有状态。
 * @details 先停止所有活动定时器（保活、会话过期、延迟遗嘱），避免析构期间
 *          再触发定时器回调，然后释放全部容器。
 * @param server 服务器实例指针（非 NULL）。
 */
static void server_private_delete(XMqttServer* server)
{
    XMqttServerPrivate* priv;
    if (!server) return;
    priv = server->m_private;
    if (!priv) return;
    /* 停止全部客户端保活定时器 */
    if (priv->m_clients) {
        XVector* keys = XMapBase_keys_base((XMapBase*)priv->m_clients);
        if (keys) {
            for (size_t i = 0; i < XVector_size_base(keys); ++i) {
                void** key = (void**)XVector_at_base(keys, (int64_t)i);
                XMqttServerClient** slot = key ?
                    (XMqttServerClient**)XMapBase_value_base((XMapBase*)priv->m_clients, key) : NULL;
                if (slot && *slot) server_stop_keep_alive(server, *slot);
            }
            XVector_delete_base(keys);
        }
    }
    /* 停止全部会话过期定时器 */
    if (priv->m_sessions) {
        XVector* keys = XMapBase_keys_base((XMapBase*)priv->m_sessions);
        if (keys) {
            for (size_t i = 0; i < XVector_size_base(keys); ++i) {
                XString** key = (XString**)XVector_at_base(keys, (int64_t)i);
                XMqttServerSession** slot = key ?
                    (XMqttServerSession**)XMapBase_value_base((XMapBase*)priv->m_sessions, key) : NULL;
                if (slot && *slot) server_session_stop_expiry(server, *slot);
            }
            XVector_delete_base(keys);
        }
    }
    /* 停止全部延迟遗嘱定时器 */
    if (priv->m_pendingWills) {
        for (size_t i = 0; i < XVector_size_base(priv->m_pendingWills); ++i) {
            XMqttServerPendingWill* will = (XMqttServerPendingWill*)XVector_at_base(
                priv->m_pendingWills, (int64_t)i);
            if (will && will->timer != XTIMER_INVALID_ID)
                XObject_killTimer((XObject*)server, will->timer);
        }
    }
    if (priv->m_purgeTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)server, priv->m_purgeTimer);
    server_private_cleanup(priv);
    server->m_private = NULL;
}

/**
 * @brief 创建服务器私有状态。
 * @return 新创建的私有状态指针，失败返回 NULL。
 * @note m_sessions / m_retained 的键与各自值内部的字符串共享同一份所有权
 *       （别名设计），因此不注册键释放回调，避免双重释放；m_sharedIndex 的
 *       键由引擎复制插入，必须注册释放回调。
 */
static XMqttServerPrivate* server_private_create(void)
{
    XMqttServerPrivate* priv;
    priv = (XMqttServerPrivate*)XMalloc_System(sizeof(XMqttServerPrivate));
    if (!priv) return NULL;
    memset(priv, 0, sizeof(*priv));
    priv->m_purgeTimer = XTIMER_INVALID_ID;
    priv->m_clients = XMap_Create(void*, XMqttServerClient*, server_transport_compare);
    priv->m_sessions = XMap_Create(XString*, XMqttServerSession*, server_string_key_compare);
    priv->m_retained = XMap_Create(XString*, XMqttServerRetainedMessage*, server_string_key_compare);
    priv->m_sharedIndex = XMap_Create(XString*, uint32_t, server_string_key_compare);
    priv->m_pendingWills = XVector_Create(XMqttServerPendingWill);
    if (!priv->m_clients || !priv->m_sessions || !priv->m_retained ||
        !priv->m_sharedIndex || !priv->m_pendingWills) {
        server_private_cleanup(priv);
        return NULL;
    }
    XContainerSetDataDeinitMethod(priv->m_clients,
        (XCDataDeinitMethod)server_client_value_deinit);
    XContainerSetDataDeinitMethod(priv->m_sessions,
        (XCDataDeinitMethod)server_session_value_deinit);
    XContainerSetDataDeinitMethod(priv->m_retained,
        (XCDataDeinitMethod)server_retained_value_deinit);
    XMapBaseSetKeyDeinitMethod(priv->m_sharedIndex,
        (XCDataDeinitMethod)server_shared_deinit);
    XContainerSetDataDeinitMethod(priv->m_pendingWills,
        (XCDataDeinitMethod)server_pending_will_deinit);
    return priv;
}

/* ==================== 类初始化/实例创建 ==================== */

XVtable* XMqttServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttServer)
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = { V_sendData, V_closeClient };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, V_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, V_timerEvent);
    return XVTABLE_DEFAULT;
}

XMqttServer* XMqttServer_create_ex(XMemoryType memory)
{
    XMqttServer* server = (XMqttServer*)XMemory_malloc(sizeof(XMqttServer), memory);
    if (server) {
        XMqttServer_init(server);
        Set_Class_Memory(server, memory); Set_Class_IsHeap(server, true);
    }
    return server;
}

void XMqttServer_init(XMqttServer* server)
{
    if (!server) return;
    memset(server, 0, sizeof(*server));
    XObject_init((XObject*)server);
    XClassGetVtable(server) = XMqttServer_class_init();
    server->m_maximumPacketSize = 268435455U;
    server->m_topicAliasMaximum = 0;
    server->m_serverKeepAlive = 0;
    server->m_maximumQoS = 2;
    server->m_retainAvailable = true;
    server->m_wildcardAvailable = true;
    server->m_subscriptionIdAvailable = true;
    server->m_sharedAvailable = true;
    server->m_private = server_private_create();
}

/* ==================== 虚函数调度入口 ==================== */

bool XMqttServer_sendData_base(XMqttServer* server, void* transport,
                               const uint8_t* data, size_t size)
{
    if (!server || !XClassGetVtable(server)) return false;
    return XClassGetVirtualFunc(server, EXMqttServer_SendData,
        bool (*)(XMqttServer*, void*, const uint8_t*, size_t))(
            server, transport, data, size);
}

void XMqttServer_closeClient_base(XMqttServer* server, void* transport)
{
    if (!server || !XClassGetVtable(server)) return;
    XClassGetVirtualFunc(server, EXMqttServer_CloseClient,
        void (*)(XMqttServer*, void*))(server, transport);
}

/* ==================== 信号 ==================== */

/**
 * @brief messageReceived 信号参数释放：释放深拷贝的主题与载荷。
 */
static void mqtt_server_message_args_delete(XVarList* list)
{
    XMqttTopicName* topic;
    XByteArray* payload;
    if (!list) return;
    /* 参数顺序：transport(借用，不释放)、topicCopy、payloadCopy */
    XVarList_args_3(list, void*, transportArg, XMqttTopicName*, topicArg, XByteArray*, payloadArg);
    (void)transportArg;
    topic = (XMqttTopicName*)topicArg;
    payload = (XByteArray*)payloadArg;
    if (topic) XMqttTopicName_delete_base(topic);
    if (payload) XByteArray_delete_base(payload);
}

void* XMqttServer_clientConnected_signal(XMqttServer* server, void* transport)
{
    if (!server) return (void*)(size_t)XMqttServer_clientConnected_signal;
    XEmitSignal(server, XMqttServer_clientConnected_signal,
                XVarList_Create(XVar(void*, transport)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttServer_clientDisconnected_signal(XMqttServer* server, void* transport)
{
    if (!server) return (void*)(size_t)XMqttServer_clientDisconnected_signal;
    XEmitSignal(server, XMqttServer_clientDisconnected_signal,
                XVarList_Create(XVar(void*, transport)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttServer_messageReceived_signal(XMqttServer* server, void* transport,
                                         const XMqttTopicName* topic,
                                         const XByteArray* payload)
{
    XMqttTopicName* topicCopy;
    XByteArray* payloadCopy;
    XVarList* args;
    if (!server) return (void*)(size_t)XMqttServer_messageReceived_signal;
    topicCopy = topic ? XMqttTopicName_create_copy(topic) : NULL;
    payloadCopy = payload ? XByteArray_create_copy(payload) : XByteArray_create();
    args = XVarList_Create(XVar(void*, transport),
                           XVar(XMqttTopicName*, topicCopy),
                           XVar(XByteArray*, payloadCopy));
    XObject_emitSignal((XObject*)server, (size_t)XMqttServer_messageReceived_signal,
                       args, mqtt_server_message_args_delete, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)(size_t)XMqttServer_messageReceived_signal;
}

/* ==================== 服务端主动发布 ==================== */

bool XMqttServer_publish(XMqttServer* server, const char* topic,
                         const uint8_t* payload, size_t payloadLen,
                         uint8_t qos, bool retain)
{
    XString* topicString;
    bool ok;
    if (!server || !topic) return false;
    topicString = XString_create_utf8(topic);
    if (!topicString) return false;
    ok = server_publish_common(server, topicString, NULL, payload, payloadLen,
                               qos, retain);
    XString_delete_base(topicString);
    return ok;
}

bool XMqttServer_publishWithProperties(XMqttServer* server, const char* topic,
                                       const XMqttPublishProperties* properties,
                                       const uint8_t* payload, size_t payloadLen,
                                       uint8_t qos, bool retain)
{
    XString* topicString;
    bool ok;
    if (!server || !topic) return false;
    topicString = XString_create_utf8(topic);
    if (!topicString) return false;
    ok = server_publish_common(server, topicString, properties, payload, payloadLen,
                               qos, retain);
    XString_delete_base(topicString);
    return ok;
}

/* ==================== 认证配置 ==================== */

void XMqttServer_setAuthenticator(XMqttServer* server, void* context,
                                  XMqttServer_Authenticator authenticator)
{
    if (!server || !server->m_private) return;
    server->m_private->m_authContext = context;
    server->m_private->m_authenticator = authenticator;
}

/* ==================== 配置接口 ==================== */

void XMqttServer_setMaximumPacketSize(XMqttServer* server, uint32_t size)
{
    if (!server) return;
    server->m_maximumPacketSize = size ? size : 268435455U;
}

uint32_t XMqttServer_maximumPacketSize(const XMqttServer* server)
{
    return server ? server->m_maximumPacketSize : 0;
}

void XMqttServer_setTopicAliasMaximum(XMqttServer* server, uint16_t maximum)
{
    if (!server) return;
    server->m_topicAliasMaximum = maximum;
}

uint16_t XMqttServer_topicAliasMaximum(const XMqttServer* server)
{
    return server ? server->m_topicAliasMaximum : 0;
}

void XMqttServer_setServerKeepAlive(XMqttServer* server, uint16_t seconds)
{
    if (!server) return;
    server->m_serverKeepAlive = seconds;
}

uint16_t XMqttServer_serverKeepAlive(const XMqttServer* server)
{
    return server ? server->m_serverKeepAlive : 0;
}

void XMqttServer_setMaximumQoS(XMqttServer* server, uint8_t qos)
{
    if (!server) return;
    server->m_maximumQoS = qos > 2 ? 2 : qos;
}

uint8_t XMqttServer_maximumQoS(const XMqttServer* server)
{
    return server ? server->m_maximumQoS : 0;
}

void XMqttServer_setRetainAvailable(XMqttServer* server, bool available)
{
    if (!server) return;
    server->m_retainAvailable = available;
}

bool XMqttServer_retainAvailable(const XMqttServer* server)
{
    return server ? server->m_retainAvailable : false;
}

void XMqttServer_setWildcardAvailable(XMqttServer* server, bool available)
{
    if (!server) return;
    server->m_wildcardAvailable = available;
}

bool XMqttServer_wildcardAvailable(const XMqttServer* server)
{
    return server ? server->m_wildcardAvailable : false;
}

void XMqttServer_setSubscriptionIdAvailable(XMqttServer* server, bool available)
{
    if (!server) return;
    server->m_subscriptionIdAvailable = available;
}

bool XMqttServer_subscriptionIdAvailable(const XMqttServer* server)
{
    return server ? server->m_subscriptionIdAvailable : false;
}

void XMqttServer_setSharedAvailable(XMqttServer* server, bool available)
{
    if (!server) return;
    server->m_sharedAvailable = available;
}

bool XMqttServer_sharedAvailable(const XMqttServer* server)
{
    return server ? server->m_sharedAvailable : false;
}

#endif /* XMQTT_SERVER_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
