#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_CLIENT_ON
#include "XMqttClient_p.h"
#include "XMemory.h"
#include <string.h>

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

typedef struct XMqttReader {
    const uint8_t* data;
    size_t size;
    size_t pos;
    bool ok;
} XMqttReader;

static bool mqtt_append(XByteArray* output, const void* data, size_t size)
{
    if (!output || (!data && size)) return false;
    return size == 0 || XVector_push_back_2((XVector*)output, data, size);
}

static bool mqtt_append_u8(XByteArray* output, uint8_t value)
{
    return XByteArray_push_back_1(output, value);
}

static bool mqtt_append_u16(XByteArray* output, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return mqtt_append(output, bytes, sizeof(bytes));
}

static bool mqtt_append_u32(XByteArray* output, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value
    };
    return mqtt_append(output, bytes, sizeof(bytes));
}

static bool mqtt_append_varint(XByteArray* output, uint32_t value)
{
    if (value > 268435455U) return false;
    do {
        uint8_t byte = (uint8_t)(value % 128U);
        value /= 128U;
        if (value) byte |= 0x80U;
        if (!mqtt_append_u8(output, byte)) return false;
    } while (value);
    return true;
}

static bool mqtt_append_string(XByteArray* output, const XString* value)
{
    size_t size = value ? XString_toUtf8_length(value) : 0;
    const char* data = value ? XString_toUtf8(value) : NULL;
    return size <= UINT16_MAX && mqtt_append_u16(output, (uint16_t)size) &&
           mqtt_append(output, data, size);
}

static bool mqtt_append_binary(XByteArray* output, const XByteArray* value)
{
    size_t size = value ? XByteArray_size_base(value) : 0;
    return size <= UINT16_MAX && mqtt_append_u16(output, (uint16_t)size) &&
           mqtt_append(output, value ? XByteArray_constData((XByteArray*)value) : NULL, size);
}

static bool mqtt_append_property_string(XByteArray* output, uint8_t property,
                                        const XString* value)
{
    return mqtt_append_u8(output, property) && mqtt_append_string(output, value);
}

static bool mqtt_append_property_binary(XByteArray* output, uint8_t property,
                                        const XByteArray* value)
{
    return mqtt_append_u8(output, property) && mqtt_append_binary(output, value);
}

static bool mqtt_append_user_properties(XByteArray* output,
                                        const XMqttUserProperties* properties)
{
    if (!properties) return true;
    for (size_t i = 0; i < XVector_size_base((const XVector*)properties); ++i) {
        const XMqttStringPair* pair = (const XMqttStringPair*)XVector_at_base(
            (const XVector*)properties, (int64_t)i);
        if (!pair || !mqtt_append_u8(output, MQTT_PROP_USER_PROPERTY) ||
            !mqtt_append_string(output, XMqttStringPair_name_const(pair)) ||
            !mqtt_append_string(output, XMqttStringPair_value_const(pair))) return false;
    }
    return true;
}

static bool mqtt_append_property_block(XByteArray* output, XByteArray* properties)
{
    size_t size = properties ? XByteArray_size_base(properties) : 0;
    return size <= 268435455U && mqtt_append_varint(output, (uint32_t)size) &&
           mqtt_append(output, properties ? XByteArray_constData(properties) : NULL, size);
}

static XByteArray* mqtt_make_packet(uint8_t header, const XByteArray* payload)
{
    size_t size = payload ? XByteArray_size_base(payload) : 0;
    if (size > 268435455U) return NULL;
    XByteArray* packet = XByteArray_create();
    if (!packet || !mqtt_append_u8(packet, header) ||
        !mqtt_append_varint(packet, (uint32_t)size) ||
        !mqtt_append(packet, payload ? XByteArray_constData((XByteArray*)payload) : NULL, size)) {
        if (packet) XByteArray_delete_base(packet);
        return NULL;
    }
    return packet;
}

static bool mqtt_write_packet(XMqttClient* client, uint8_t header, const XByteArray* payload)
{
    if (!client || !client->m_transport) return false;
    XByteArray* packet = mqtt_make_packet(header, payload);
    if (!packet) return false;
    size_t size = XByteArray_size_base(packet);
    if (client->m_serverConnectionProperties &&
        XMqttServerConnectionProperties_isValid(client->m_serverConnectionProperties) &&
        size > client->m_serverConnectionProperties->m_base.m_maximumPacketSize) {
        XByteArray_delete_base(packet);
        return false;
    }
    int64_t written = XIODevice_write_1((XIODevice*)client->m_transport,
                                        (const char*)XByteArray_constData(packet),
                                        (int64_t)size);
    XByteArray_delete_base(packet);
    return written == (int64_t)size;
}

static uint8_t mqtt_read_u8(XMqttReader* reader)
{
    if (!reader || !reader->ok || reader->pos >= reader->size) {
        if (reader) reader->ok = false;
        return 0;
    }
    return reader->data[reader->pos++];
}

static uint16_t mqtt_read_u16(XMqttReader* reader)
{
    uint16_t high = mqtt_read_u8(reader);
    uint16_t low = mqtt_read_u8(reader);
    return (uint16_t)((high << 8) | low);
}

static uint32_t mqtt_read_u32(XMqttReader* reader)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value = (value << 8) | mqtt_read_u8(reader);
    return value;
}

static uint32_t mqtt_read_varint(XMqttReader* reader)
{
    uint32_t value = 0;
    uint32_t multiplier = 1;
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = mqtt_read_u8(reader);
        if (!reader->ok) return 0;
        value += (uint32_t)(byte & 0x7FU) * multiplier;
        if (!(byte & 0x80U)) return value;
        multiplier *= 128U;
    }
    reader->ok = false;
    return 0;
}

static XString* mqtt_read_string(XMqttReader* reader)
{
    uint16_t size = mqtt_read_u16(reader);
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

static XByteArray* mqtt_read_binary(XMqttReader* reader)
{
    uint16_t size = mqtt_read_u16(reader);
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

static bool mqtt_reader_property_end(XMqttReader* reader, size_t* end)
{
    uint32_t length = mqtt_read_varint(reader);
    if (!reader->ok || length > reader->size - reader->pos) {
        reader->ok = false;
        return false;
    }
    *end = reader->pos + length;
    return true;
}

static bool mqtt_add_user_property(XMqttUserProperties** properties,
                                   XString* name, XString* value)
{
    if (!name || !value) return false;
    if (!*properties) *properties = XMqttUserProperties_create();
    if (!*properties) return false;
    XMqttStringPair pair;
    XMqttStringPair_init(&pair, XString_toUtf8(name), XString_toUtf8(value));
    bool result = XVector_push_back_1_base((XVector*)*properties, &pair);
    XMqttStringPair_deinit_base(&pair);
    return result;
}

static void mqtt_subscription_entry_deinit(XMqttSubscriptionEntry* entry)
{
    if (entry && entry->wireTopic) {
        XMqttTopicFilter_delete_base(entry->wireTopic);
        entry->wireTopic = NULL;
    }
}

static void mqtt_alias_entry_deinit(XMqttTopicAliasEntry* entry)
{
    if (entry && entry->topic) {
        XString_delete_base(entry->topic);
        entry->topic = NULL;
    }
}

XMqttClientPrivate* XMqttClientPrivate_create(void)
{
    XMqttClientPrivate* priv = (XMqttClientPrivate*)XCalloc_System(1, sizeof(*priv));
    if (!priv) return NULL;
    priv->input = XByteArray_create();
    priv->subscriptions = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XMqttSubscriptionEntry), false);
    priv->pendingPublishes = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XMqttPendingPublish), false);
    priv->incomingQos2 = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(uint16_t), false);
    priv->receiveAliases = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XMqttTopicAliasEntry), false);
    priv->publishAliases = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XMqttTopicAliasEntry), false);
    priv->nextPacketIdentifier = 1;
    priv->keepAliveTimer = XTIMER_INVALID_ID;
    if (!priv->input || !priv->subscriptions || !priv->pendingPublishes ||
        !priv->incomingQos2 || !priv->receiveAliases || !priv->publishAliases) {
        XMqttClientPrivate_delete(priv);
        return NULL;
    }
    XContainerSetDataDeinitMethod(priv->subscriptions,
        (XCDataDeinitMethod)mqtt_subscription_entry_deinit);
    XContainerSetDataDeinitMethod(priv->receiveAliases,
        (XCDataDeinitMethod)mqtt_alias_entry_deinit);
    XContainerSetDataDeinitMethod(priv->publishAliases,
        (XCDataDeinitMethod)mqtt_alias_entry_deinit);
    return priv;
}

void XMqttClientPrivate_delete(XMqttClientPrivate* priv)
{
    if (!priv) return;
    if (priv->input) XByteArray_delete_base(priv->input);
    if (priv->subscriptions) XVector_delete_base(priv->subscriptions);
    if (priv->pendingPublishes) XVector_delete_base(priv->pendingPublishes);
    if (priv->incomingQos2) XVector_delete_base(priv->incomingQos2);
    if (priv->receiveAliases) XVector_delete_base(priv->receiveAliases);
    if (priv->publishAliases) XVector_delete_base(priv->publishAliases);
    XFree_System(priv);
}

static void mqtt_stop_keep_alive(XMqttClient* client)
{
    if (!client || !client->m_private) return;
    if (client->m_private->keepAliveTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)client, client->m_private->keepAliveTimer);
        client->m_private->keepAliveTimer = XTIMER_INVALID_ID;
    }
    client->m_private->pingTimeout = 0;
}

static void mqtt_clear_subscriptions(XMqttClient* client)
{
    if (!client || !client->m_private || !client->m_private->subscriptions) return;
    for (size_t i = 0; i < XVector_size_base(client->m_private->subscriptions); ++i) {
        XMqttSubscriptionEntry* entry = (XMqttSubscriptionEntry*)XVector_at_base(
            client->m_private->subscriptions, (int64_t)i);
        if (entry && entry->subscription)
            XMqttSubscription_setState(entry->subscription, XMqttSubscription_Unsubscribed);
    }
    XVector_clear_base(client->m_private->subscriptions);
}

void XMqttClientPrivate_resetConnection(XMqttClient* client, bool clearSubscriptions)
{
    if (!client || !client->m_private) return;
    XMqttClientPrivate* priv = client->m_private;
    mqtt_stop_keep_alive(client);
    priv->connectPacketSent = false;
    priv->disconnectRequested = false;
    priv->processingInput = false;
    XByteArray_clear_base(priv->input);
    XVector_clear_base(priv->pendingPublishes);
    XVector_clear_base(priv->incomingQos2);
    XVector_clear_base(priv->receiveAliases);
    XVector_clear_base(priv->publishAliases);
    if (clearSubscriptions) mqtt_clear_subscriptions(client);
}

static bool mqtt_identifier_in_use(const XMqttClient* client, uint16_t identifier)
{
    XMqttClientPrivate* priv = client->m_private;
    for (size_t i = 0; i < XVector_size_base(priv->pendingPublishes); ++i) {
        const XMqttPendingPublish* pending = (const XMqttPendingPublish*)XVector_at_base(
            priv->pendingPublishes, (int64_t)i);
        if (pending && pending->identifier == identifier) return true;
    }
    for (size_t i = 0; i < XVector_size_base(priv->subscriptions); ++i) {
        const XMqttSubscriptionEntry* entry = (const XMqttSubscriptionEntry*)XVector_at_base(
            priv->subscriptions, (int64_t)i);
        if (entry && entry->identifier == identifier) return true;
    }
    return false;
}

static uint16_t mqtt_next_identifier(XMqttClient* client)
{
    XMqttClientPrivate* priv = client->m_private;
    for (uint32_t attempts = 0; attempts < UINT16_MAX; ++attempts) {
        uint16_t id = priv->nextPacketIdentifier++;
        if (priv->nextPacketIdentifier == 0) priv->nextPacketIdentifier = 1;
        if (id && !mqtt_identifier_in_use(client, id)) return id;
    }
    return 0;
}

static XMqttTopicAliasEntry* mqtt_alias_by_id(XVector* aliases, uint16_t alias)
{
    for (size_t i = 0; aliases && i < XVector_size_base(aliases); ++i) {
        XMqttTopicAliasEntry* entry = (XMqttTopicAliasEntry*)XVector_at_base(aliases, (int64_t)i);
        if (entry && entry->alias == alias) return entry;
    }
    return NULL;
}

static XMqttTopicAliasEntry* mqtt_alias_by_topic(XVector* aliases, const XString* topic)
{
    for (size_t i = 0; aliases && i < XVector_size_base(aliases); ++i) {
        XMqttTopicAliasEntry* entry = (XMqttTopicAliasEntry*)XVector_at_base(aliases, (int64_t)i);
        if (entry && XString_equals(entry->topic, topic, XChar_CaseSensitive)) return entry;
    }
    return NULL;
}

static bool mqtt_alias_set(XVector* aliases, uint16_t alias, const XString* topic)
{
    XMqttTopicAliasEntry* entry = mqtt_alias_by_id(aliases, alias);
    if (entry) {
        if (entry->topic) XString_delete_base(entry->topic);
        entry->topic = XString_create_copy(topic);
        return entry->topic != NULL;
    }
    XMqttTopicAliasEntry value = {alias, XString_create_copy(topic)};
    if (!value.topic || !XVector_push_back_1_base(aliases, &value)) {
        if (value.topic) XString_delete_base(value.topic);
        return false;
    }
    return true;
}

static bool mqtt_string_present(const XString* value)
{
    return value && XString_toUtf8_length(value) != 0;
}

static bool mqtt_binary_present(const XByteArray* value)
{
    return value && XByteArray_size_base(value) != 0;
}

static bool mqtt_append_connection_properties(XByteArray* output,
                                              const XMqttConnectionProperties* prop)
{
    XByteArray* properties = XByteArray_create();
    if (!properties) return false;
    bool ok = true;
    if (prop) {
        if (prop->m_sessionExpiryInterval)
            ok = mqtt_append_u8(properties, MQTT_PROP_SESSION_EXPIRY) &&
                 mqtt_append_u32(properties, prop->m_sessionExpiryInterval);
        if (ok && prop->m_maximumReceive != UINT16_MAX)
            ok = mqtt_append_u8(properties, MQTT_PROP_RECEIVE_MAXIMUM) &&
                 mqtt_append_u16(properties, prop->m_maximumReceive);
        if (ok && prop->m_maximumPacketSize != UINT32_MAX)
            ok = mqtt_append_u8(properties, MQTT_PROP_MAXIMUM_PACKET_SIZE) &&
                 mqtt_append_u32(properties, prop->m_maximumPacketSize);
        if (ok && prop->m_maximumTopicAlias)
            ok = mqtt_append_u8(properties, MQTT_PROP_TOPIC_ALIAS_MAXIMUM) &&
                 mqtt_append_u16(properties, prop->m_maximumTopicAlias);
        if (ok && prop->m_requestResponseInformation)
            ok = mqtt_append_u8(properties, MQTT_PROP_REQUEST_RESPONSE_INFO) &&
                 mqtt_append_u8(properties, 1);
        if (ok && !prop->m_requestProblemInformation)
            ok = mqtt_append_u8(properties, MQTT_PROP_REQUEST_PROBLEM_INFO) &&
                 mqtt_append_u8(properties, 0);
        if (ok) ok = mqtt_append_user_properties(properties, prop->m_userProperties);
        if (ok && mqtt_string_present(prop->m_authenticationMethod))
            ok = mqtt_append_property_string(properties, MQTT_PROP_AUTH_METHOD,
                                             prop->m_authenticationMethod);
        if (ok && mqtt_binary_present(prop->m_authenticationData))
            ok = mqtt_append_property_binary(properties, MQTT_PROP_AUTH_DATA,
                                             prop->m_authenticationData);
    }
    if (ok) ok = mqtt_append_property_block(output, properties);
    XByteArray_delete_base(properties);
    return ok;
}

static bool mqtt_append_will_properties(XByteArray* output,
                                        const XMqttLastWillProperties* prop)
{
    XByteArray* properties = XByteArray_create();
    if (!properties) return false;
    bool ok = true;
    if (prop) {
        if (prop->m_willDelayInterval)
            ok = mqtt_append_u8(properties, MQTT_PROP_WILL_DELAY) &&
                 mqtt_append_u32(properties, prop->m_willDelayInterval);
        if (ok && prop->m_payloadFormatIndicator)
            ok = mqtt_append_u8(properties, MQTT_PROP_PAYLOAD_FORMAT) &&
                 mqtt_append_u8(properties, prop->m_payloadFormatIndicator);
        if (ok && prop->m_messageExpiryInterval)
            ok = mqtt_append_u8(properties, MQTT_PROP_MESSAGE_EXPIRY) &&
                 mqtt_append_u32(properties, prop->m_messageExpiryInterval);
        if (ok && mqtt_string_present(prop->m_contentType))
            ok = mqtt_append_property_string(properties, MQTT_PROP_CONTENT_TYPE,
                                             prop->m_contentType);
        if (ok && mqtt_string_present(prop->m_responseTopic))
            ok = mqtt_append_property_string(properties, MQTT_PROP_RESPONSE_TOPIC,
                                             prop->m_responseTopic);
        if (ok && mqtt_binary_present(prop->m_correlationData))
            ok = mqtt_append_property_binary(properties, MQTT_PROP_CORRELATION_DATA,
                                             prop->m_correlationData);
        if (ok) ok = mqtt_append_user_properties(properties, prop->m_userProperties);
    }
    if (ok) ok = mqtt_append_property_block(output, properties);
    XByteArray_delete_base(properties);
    return ok;
}

static bool mqtt_append_publish_properties(XByteArray* output,
                                           const XMqttPublishProperties* prop,
                                           uint16_t alias)
{
    XByteArray* properties = XByteArray_create();
    if (!properties) return false;
    bool ok = true;
    uint32_t available = prop ? prop->m_availableProperties : 0;
    if (prop && (available & XMqttPublishProperties_PayloadFormatIndicator))
        ok = mqtt_append_u8(properties, MQTT_PROP_PAYLOAD_FORMAT) &&
             mqtt_append_u8(properties, prop->m_payloadFormatIndicator);
    if (ok && prop && (available & XMqttPublishProperties_MessageExpiryInterval))
        ok = mqtt_append_u8(properties, MQTT_PROP_MESSAGE_EXPIRY) &&
             mqtt_append_u32(properties, prop->m_messageExpiryInterval);
    if (ok && alias)
        ok = mqtt_append_u8(properties, MQTT_PROP_TOPIC_ALIAS) &&
             mqtt_append_u16(properties, alias);
    if (ok && prop && (available & XMqttPublishProperties_ResponseTopic) &&
        mqtt_string_present(prop->m_responseTopic))
        ok = mqtt_append_property_string(properties, MQTT_PROP_RESPONSE_TOPIC,
                                         prop->m_responseTopic);
    if (ok && prop && (available & XMqttPublishProperties_CorrelationData) &&
        prop->m_correlationData)
        ok = mqtt_append_property_binary(properties, MQTT_PROP_CORRELATION_DATA,
                                         prop->m_correlationData);
    if (ok && prop && (available & XMqttPublishProperties_UserProperty))
        ok = mqtt_append_user_properties(properties, prop->m_userProperties);
    if (ok && prop && (available & XMqttPublishProperties_SubscriptionIdentifier) &&
        prop->m_subscriptionIdentifiers) {
        for (size_t i = 0; i < XVector_size_base(prop->m_subscriptionIdentifiers); ++i) {
            const uint32_t* id = (const uint32_t*)XVector_at_base(
                prop->m_subscriptionIdentifiers, (int64_t)i);
            if (!id || !*id || *id > 268435455U ||
                !mqtt_append_u8(properties, MQTT_PROP_SUBSCRIPTION_ID) ||
                !mqtt_append_varint(properties, *id)) { ok = false; break; }
        }
    }
    if (ok && prop && (available & XMqttPublishProperties_ContentType) &&
        mqtt_string_present(prop->m_contentType))
        ok = mqtt_append_property_string(properties, MQTT_PROP_CONTENT_TYPE,
                                         prop->m_contentType);
    if (ok) ok = mqtt_append_property_block(output, properties);
    XByteArray_delete_base(properties);
    return ok;
}

bool XMqttProtocol_sendConnect(XMqttClient* client)
{
    if (!client || !client->m_private || !client->m_transport ||
        client->m_private->connectPacketSent) return false;
    XByteArray* payload = XByteArray_create();
    if (!payload) return false;
    bool mqtt31 = client->m_protocolVersion == XMqttClient_MQTT_3_1;
    XString* protocol = XString_create_utf8(mqtt31 ? "MQIsdp" : "MQTT");
    bool hasWill = mqtt_string_present(client->m_willTopic);
    if (client->m_willQoS > 2 || (!hasWill && client->m_willRetain)) {
        if (protocol) XString_delete_base(protocol);
        XByteArray_delete_base(payload);
        return false;
    }
    uint8_t flags = client->m_cleanSession ? 0x02U : 0;
    if (hasWill) {
        flags |= 0x04U | (uint8_t)((client->m_willQoS & 0x03U) << 3);
        if (client->m_willRetain) flags |= 0x20U;
    }
    if (mqtt_string_present(client->m_username)) flags |= 0x80U;
    if (mqtt_string_present(client->m_password)) flags |= 0x40U;
    bool ok = protocol && mqtt_append_string(payload, protocol) &&
              mqtt_append_u8(payload, (uint8_t)client->m_protocolVersion) &&
              mqtt_append_u8(payload, flags) &&
              mqtt_append_u16(payload, client->m_keepAlive);
    if (ok && client->m_protocolVersion == XMqttClient_MQTT_5_0)
        ok = mqtt_append_connection_properties(payload, client->m_connectionProperties);
    if (ok) ok = mqtt_append_string(payload, client->m_clientId);
    if (ok && hasWill && client->m_protocolVersion == XMqttClient_MQTT_5_0)
        ok = mqtt_append_will_properties(payload, client->m_lastWillProperties);
    if (ok && hasWill)
        ok = mqtt_append_string(payload, client->m_willTopic) &&
             mqtt_append_binary(payload, client->m_willMessage);
    if (ok && (flags & 0x80U)) ok = mqtt_append_string(payload, client->m_username);
    if (ok && (flags & 0x40U)) ok = mqtt_append_string(payload, client->m_password);
    if (protocol) XString_delete_base(protocol);
    if (ok) ok = mqtt_write_packet(client, MQTT_CONNECT, payload);
    XByteArray_delete_base(payload);
    client->m_private->connectPacketSent = ok;
    if (!ok) XMqttProtocol_fail(client, XMqttClient_TransportInvalid);
    return ok;
}

XMqttSubscription* XMqttProtocol_subscribe(XMqttClient* client,
                                           const XMqttTopicFilter* topic,
                                           const XMqttSubscriptionProperties* properties,
                                           uint8_t qos)
{
    if (!client || !client->m_private || client->m_state != XMqttClient_Connected ||
        !topic || !XMqttTopicFilter_isValid(topic) || qos > 2) return NULL;
    for (size_t i = 0; i < XVector_size_base(client->m_private->subscriptions); ++i) {
        XMqttSubscriptionEntry* existing = (XMqttSubscriptionEntry*)XVector_at_base(
            client->m_private->subscriptions, (int64_t)i);
        if (existing && !existing->unsubscribePending &&
            XMqttTopicFilter_equal(existing->wireTopic, topic))
            return existing->subscription;
    }
    uint16_t identifier = mqtt_next_identifier(client);
    if (!identifier) return NULL;
    XByteArray* payload = XByteArray_create();
    XByteArray* prop = XByteArray_create();
    if (!payload || !prop) goto failed;
    bool ok = mqtt_append_u16(payload, identifier);
    if (ok && client->m_protocolVersion == XMqttClient_MQTT_5_0) {
        if (properties && properties->m_subscriptionIdentifier) {
            ok = mqtt_append_u8(prop, MQTT_PROP_SUBSCRIPTION_ID) &&
                 mqtt_append_varint(prop, properties->m_subscriptionIdentifier);
        }
        if (ok && properties)
            ok = mqtt_append_user_properties(prop, properties->m_userProperties);
        if (ok) ok = mqtt_append_property_block(payload, prop);
    }
    if (ok) ok = mqtt_append_string(payload, topic->m_filter);
    uint8_t options = qos;
    if (properties && properties->m_noLocal) options |= 0x04U;
    if (ok) ok = mqtt_append_u8(payload, options);
    XMqttSubscription* subscription = ok ? XMqttSubscription_create(topic, qos) : NULL;
    XMqttTopicFilter* wireTopic = subscription ? XMqttTopicFilter_create_copy(topic) : NULL;
    if (!subscription || !wireTopic) {
        if (subscription) XMqttSubscription_deleteLater(subscription);
        if (wireTopic) XMqttTopicFilter_delete_base(wireTopic);
        goto failed;
    }
    XMqttSubscription_setClient(subscription, client);
    XObject_setParent(subscription, client);
    if (client->m_protocolVersion == XMqttClient_MQTT_5_0) {
        XString* shareName = XMqttTopicFilter_sharedSubscriptionName(topic);
        if (shareName && XString_toUtf8_length(shareName)) {
            const char* wire = XString_toUtf8(topic->m_filter);
            const char* filterStart = wire ? strchr(wire + 7, '/') : NULL;
            subscription->m_sharedSubscription = true;
            subscription->m_sharedSubscriptionName = shareName;
            shareName = NULL;
            if (filterStart && subscription->m_topic) {
                XMqttTopicFilter_delete_base(subscription->m_topic);
                subscription->m_topic = XMqttTopicFilter_create(filterStart + 1);
            }
        }
        if (shareName) XString_delete_base(shareName);
    }
    XMqttSubscription_setState(subscription, XMqttSubscription_SubscriptionPending);
    XMqttSubscriptionEntry entry = {identifier, wireTopic, subscription, false};
    if (!XVector_push_back_1_base(client->m_private->subscriptions, &entry) ||
        !mqtt_write_packet(client, MQTT_SUBSCRIBE | 0x02U, payload)) {
        XObject_setParent(subscription, NULL);
        XMqttSubscription_deleteLater(subscription);
        mqtt_subscription_entry_deinit(&entry);
        goto failed;
    }
    XByteArray_delete_base(prop);
    XByteArray_delete_base(payload);
    return subscription;
failed:
    if (prop) XByteArray_delete_base(prop);
    if (payload) XByteArray_delete_base(payload);
    return NULL;
}

bool XMqttProtocol_unsubscribe(XMqttClient* client,
                              const XMqttTopicFilter* topic,
                              const XMqttUnsubscriptionProperties* properties)
{
    if (!client || !client->m_private || client->m_state != XMqttClient_Connected ||
        !topic || !XMqttTopicFilter_isValid(topic)) return false;
    XMqttSubscriptionEntry* entry = NULL;
    for (size_t i = 0; i < XVector_size_base(client->m_private->subscriptions); ++i) {
        XMqttSubscriptionEntry* candidate = (XMqttSubscriptionEntry*)XVector_at_base(
            client->m_private->subscriptions, (int64_t)i);
        if (candidate && !candidate->unsubscribePending &&
            XMqttTopicFilter_equal(candidate->wireTopic, topic)) { entry = candidate; break; }
    }
    if (!entry) return false;
    uint16_t identifier = mqtt_next_identifier(client);
    if (!identifier) return false;
    XByteArray* payload = XByteArray_create();
    XByteArray* prop = XByteArray_create();
    bool ok = payload && prop && mqtt_append_u16(payload, identifier);
    if (ok && client->m_protocolVersion == XMqttClient_MQTT_5_0) {
        if (properties) ok = mqtt_append_user_properties(prop, properties->m_userProperties);
        if (ok) ok = mqtt_append_property_block(payload, prop);
    }
    if (ok) ok = mqtt_append_string(payload, topic->m_filter) &&
                 mqtt_write_packet(client, MQTT_UNSUBSCRIBE | 0x02U, payload);
    if (ok) {
        entry->identifier = identifier;
        entry->unsubscribePending = true;
        XMqttSubscription_setState(entry->subscription,
                                   XMqttSubscription_UnsubscriptionPending);
    }
    if (prop) XByteArray_delete_base(prop);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

int32_t XMqttProtocol_publish(XMqttClient* client,
                             const XMqttTopicName* topic,
                             const XMqttPublishProperties* properties,
                             const uint8_t* message, size_t messageLen,
                             uint8_t qos, bool retain)
{
    if (!client || !client->m_private || client->m_state != XMqttClient_Connected ||
        !topic || !XMqttTopicName_isValid(topic) || qos > 2 || (!message && messageLen))
        return -1;
    uint16_t identifier = qos ? mqtt_next_identifier(client) : 0;
    if (qos && !identifier) return -1;
    uint16_t alias = 0;
    bool omitTopic = false;
    if (client->m_protocolVersion == XMqttClient_MQTT_5_0) {
        uint16_t maximum = client->m_serverConnectionProperties->m_base.m_maximumTopicAlias;
        if (properties && (properties->m_availableProperties & XMqttPublishProperties_TopicAlias)) {
            alias = properties->m_topicAlias;
            if (!alias || alias > maximum) return -1;
            XMqttTopicAliasEntry* known = mqtt_alias_by_id(client->m_private->publishAliases, alias);
            omitTopic = known && XString_equals(known->topic, topic->m_name, XChar_CaseSensitive);
            if (!omitTopic && !mqtt_alias_set(client->m_private->publishAliases, alias,
                                              topic->m_name)) return -1;
        } else if (maximum) {
            XMqttTopicAliasEntry* known = mqtt_alias_by_topic(client->m_private->publishAliases,
                                                              topic->m_name);
            if (known) { alias = known->alias; omitTopic = true; }
            else if (XVector_size_base(client->m_private->publishAliases) < maximum) {
                alias = (uint16_t)(XVector_size_base(client->m_private->publishAliases) + 1);
                if (!mqtt_alias_set(client->m_private->publishAliases, alias, topic->m_name))
                    return -1;
            }
        }
    }
    XByteArray* payload = XByteArray_create();
    if (!payload) return -1;
    XString* empty = omitTopic ? XString_create_utf8("") : NULL;
    bool ok = mqtt_append_string(payload, omitTopic ? empty : topic->m_name);
    if (empty) XString_delete_base(empty);
    if (ok && qos) ok = mqtt_append_u16(payload, identifier);
    if (ok && client->m_protocolVersion == XMqttClient_MQTT_5_0)
        ok = mqtt_append_publish_properties(payload, properties, alias);
    if (ok) ok = mqtt_append(payload, message, messageLen);
    /* 用位域联合体构造固定头第一字节（避免手工移位/掩码） */
    XMqttFixedHeader fixed;
    fixed.byte = 0;
    fixed.bits.type = (uint8_t)(MQTT_PUBLISH >> 4);
    fixed.bits.qos = qos;
    fixed.bits.retain = retain ? 1U : 0U;
    uint8_t header = fixed.byte;
    if (ok) ok = mqtt_write_packet(client, header, payload);
    XByteArray_delete_base(payload);
    if (!ok) return -1;
    if (!qos) {
        XMqttClient_messageSent_signal(client, 0);
        return 0;
    }
    XMqttPendingPublish pending = {identifier, qos, 0};
    if (!XVector_push_back_1_base(client->m_private->pendingPublishes, &pending)) return -1;
    XMqttClient_messageStatusChanged_signal(client, identifier,
                                            XMqtt_MessageStatus_Published, NULL);
    return identifier;
}

bool XMqttProtocol_sendPing(XMqttClient* client, bool automatic)
{
    if (!client || !client->m_private || client->m_state != XMqttClient_Connected)
        return false;
    if (!automatic && client->m_autoKeepAlive) return false;
    if (client->m_private->pingTimeout) return false;
    if (!mqtt_write_packet(client, MQTT_PINGREQ, NULL)) return false;
    client->m_private->pingTimeout = 1;
    return true;
}

bool XMqttProtocol_sendDisconnect(XMqttClient* client)
{
    if (!client || client->m_state != XMqttClient_Connected) return false;
    bool ok = mqtt_write_packet(client, MQTT_DISCONNECT, NULL);
    if (client->m_private) client->m_private->disconnectRequested = true;
    return ok;
}

bool XMqttProtocol_authenticate(XMqttClient* client,
                               const XMqttAuthenticationProperties* properties)
{
    if (!client || !properties || client->m_state != XMqttClient_Connected ||
        client->m_protocolVersion != XMqttClient_MQTT_5_0) return false;
    XByteArray* payload = XByteArray_create();
    XByteArray* prop = XByteArray_create();
    bool ok = payload && prop && mqtt_append_u8(payload, XMqtt_ReasonCode_ContinueAuthentication);
    if (ok && mqtt_string_present(properties->m_authenticationMethod))
        ok = mqtt_append_property_string(prop, MQTT_PROP_AUTH_METHOD,
                                         properties->m_authenticationMethod);
    if (ok && properties->m_authenticationData)
        ok = mqtt_append_property_binary(prop, MQTT_PROP_AUTH_DATA,
                                         properties->m_authenticationData);
    if (ok && mqtt_string_present(properties->m_reason))
        ok = mqtt_append_property_string(prop, MQTT_PROP_REASON_STRING,
                                         properties->m_reason);
    if (ok) ok = mqtt_append_user_properties(prop, properties->m_userProperties);
    if (ok) ok = mqtt_append_property_block(payload, prop) &&
                 mqtt_write_packet(client, MQTT_AUTH, payload);
    if (prop) XByteArray_delete_base(prop);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

static XMqttClient_Error mqtt_connack_error(uint8_t reason)
{
    switch (reason) {
    case 0: return XMqttClient_NoError;
    case 1: case 0x84: return XMqttClient_InvalidProtocolVersion;
    case 2: case 0x85: return XMqttClient_IdRejected;
    case 3: case 0x88: case 0x89: return XMqttClient_ServerUnavailable;
    case 4: case 0x86: return XMqttClient_BadUsernameOrPassword;
    case 5: case 0x87: return XMqttClient_NotAuthorized;
    default: return reason >= 0x80 ? XMqttClient_Mqtt5SpecificError : XMqttClient_UnknownError;
    }
}

static void mqtt_replace_string(XString** target, XString* value)
{
    if (*target) XString_delete_base(*target);
    *target = value;
}

static void mqtt_replace_binary(XByteArray** target, XByteArray* value)
{
    if (*target) XByteArray_delete_base(*target);
    *target = value;
}

static bool mqtt_parse_connack_properties(XMqttClient* client, XMqttReader* reader,
                                          XMqttServerConnectionProperties* prop)
{
    size_t end;
    if (!mqtt_reader_property_end(reader, &end)) return false;
    while (reader->ok && reader->pos < end) {
        uint8_t id = mqtt_read_u8(reader);
        switch (id) {
        case MQTT_PROP_SESSION_EXPIRY:
            prop->m_base.m_sessionExpiryInterval = mqtt_read_u32(reader);
            prop->m_availableProperties |= XMqttServerConnectionProperties_SessionExpiryInterval;
            break;
        case MQTT_PROP_RECEIVE_MAXIMUM:
            prop->m_base.m_maximumReceive = mqtt_read_u16(reader);
            prop->m_availableProperties |= XMqttServerConnectionProperties_MaximumReceive;
            if (!prop->m_base.m_maximumReceive) reader->ok = false;
            break;
        case MQTT_PROP_MAXIMUM_QOS:
            prop->m_maximumQoS = mqtt_read_u8(reader);
            prop->m_availableProperties |= XMqttServerConnectionProperties_MaximumQoS;
            if (prop->m_maximumQoS > 1) reader->ok = false;
            break;
        case MQTT_PROP_RETAIN_AVAILABLE:
            { uint8_t value = mqtt_read_u8(reader);
            if (value > 1) reader->ok = false;
            prop->m_retainAvailable = value != 0; }
            prop->m_availableProperties |= XMqttServerConnectionProperties_RetainAvailable;
            break;
        case MQTT_PROP_MAXIMUM_PACKET_SIZE:
            prop->m_base.m_maximumPacketSize = mqtt_read_u32(reader);
            prop->m_availableProperties |= XMqttServerConnectionProperties_MaximumPacketSize;
            if (!prop->m_base.m_maximumPacketSize) reader->ok = false;
            break;
        case MQTT_PROP_ASSIGNED_CLIENT_ID: {
            XString* assigned = mqtt_read_string(reader);
            if (!assigned) { reader->ok = false; break; }
            mqtt_replace_string(&client->m_clientId, assigned);
            XMqttClient_clientIdChanged_signal(client, client->m_clientId);
            prop->m_clientIdAssigned = true;
            prop->m_availableProperties |= XMqttServerConnectionProperties_AssignedClientId;
            break;
        }
        case MQTT_PROP_TOPIC_ALIAS_MAXIMUM:
            prop->m_base.m_maximumTopicAlias = mqtt_read_u16(reader);
            prop->m_availableProperties |= XMqttServerConnectionProperties_MaximumTopicAlias;
            break;
        case MQTT_PROP_REASON_STRING:
            mqtt_replace_string(&prop->m_reason, mqtt_read_string(reader));
            prop->m_availableProperties |= XMqttServerConnectionProperties_ReasonString;
            break;
        case MQTT_PROP_USER_PROPERTY: {
            XString* name = mqtt_read_string(reader);
            XString* value = mqtt_read_string(reader);
            bool ok = mqtt_add_user_property(&prop->m_base.m_userProperties, name, value);
            if (name) XString_delete_base(name);
            if (value) XString_delete_base(value);
            if (!ok) reader->ok = false;
            prop->m_availableProperties |= XMqttServerConnectionProperties_UserProperty;
            break;
        }
        case MQTT_PROP_WILDCARD_AVAILABLE:
            { uint8_t value = mqtt_read_u8(reader);
            if (value > 1) reader->ok = false;
            prop->m_wildcardSupported = value != 0; }
            prop->m_availableProperties |= XMqttServerConnectionProperties_WildCardSupported;
            break;
        case MQTT_PROP_SUBSCRIPTION_ID_AVAILABLE:
            { uint8_t value = mqtt_read_u8(reader);
            if (value > 1) reader->ok = false;
            prop->m_subscriptionIdentifierSupported = value != 0; }
            prop->m_availableProperties |= XMqttServerConnectionProperties_SubscriptionIdentifierSupport;
            break;
        case MQTT_PROP_SHARED_AVAILABLE:
            { uint8_t value = mqtt_read_u8(reader);
            if (value > 1) reader->ok = false;
            prop->m_sharedSubscriptionSupported = value != 0; }
            prop->m_availableProperties |= XMqttServerConnectionProperties_SharedSubscriptionSupport;
            break;
        case MQTT_PROP_SERVER_KEEP_ALIVE:
            prop->m_serverKeepAlive = mqtt_read_u16(reader);
            prop->m_availableProperties |= XMqttServerConnectionProperties_ServerKeepAlive;
            break;
        case MQTT_PROP_RESPONSE_INFO:
            mqtt_replace_string(&prop->m_responseInformation, mqtt_read_string(reader));
            prop->m_availableProperties |= XMqttServerConnectionProperties_ResponseInformation;
            break;
        case MQTT_PROP_SERVER_REFERENCE:
            mqtt_replace_string(&prop->m_serverReference, mqtt_read_string(reader));
            prop->m_availableProperties |= XMqttServerConnectionProperties_ServerReference;
            break;
        case MQTT_PROP_AUTH_METHOD:
            mqtt_replace_string(&prop->m_base.m_authenticationMethod, mqtt_read_string(reader));
            prop->m_availableProperties |= XMqttServerConnectionProperties_AuthenticationMethod;
            break;
        case MQTT_PROP_AUTH_DATA:
            mqtt_replace_binary(&prop->m_base.m_authenticationData, mqtt_read_binary(reader));
            prop->m_availableProperties |= XMqttServerConnectionProperties_AuthenticationData;
            break;
        default: reader->ok = false; break;
        }
    }
    if (reader->pos != end) reader->ok = false;
    return reader->ok;
}

static bool mqtt_parse_publish_properties(XMqttReader* reader,
                                          XMqttPublishProperties* prop)
{
    size_t end;
    if (!mqtt_reader_property_end(reader, &end)) return false;
    while (reader->ok && reader->pos < end) {
        uint8_t id = mqtt_read_u8(reader);
        switch (id) {
        case MQTT_PROP_PAYLOAD_FORMAT:
            prop->m_payloadFormatIndicator = mqtt_read_u8(reader);
            if (prop->m_payloadFormatIndicator > 1) reader->ok = false;
            prop->m_availableProperties |= XMqttPublishProperties_PayloadFormatIndicator;
            break;
        case MQTT_PROP_MESSAGE_EXPIRY:
            prop->m_messageExpiryInterval = mqtt_read_u32(reader);
            prop->m_availableProperties |= XMqttPublishProperties_MessageExpiryInterval;
            break;
        case MQTT_PROP_TOPIC_ALIAS:
            prop->m_topicAlias = mqtt_read_u16(reader);
            prop->m_availableProperties |= XMqttPublishProperties_TopicAlias;
            if (!prop->m_topicAlias) reader->ok = false;
            break;
        case MQTT_PROP_RESPONSE_TOPIC:
            mqtt_replace_string(&prop->m_responseTopic, mqtt_read_string(reader));
            prop->m_availableProperties |= XMqttPublishProperties_ResponseTopic;
            break;
        case MQTT_PROP_CORRELATION_DATA:
            mqtt_replace_binary(&prop->m_correlationData, mqtt_read_binary(reader));
            prop->m_availableProperties |= XMqttPublishProperties_CorrelationData;
            break;
        case MQTT_PROP_USER_PROPERTY: {
            XString* name = mqtt_read_string(reader);
            XString* value = mqtt_read_string(reader);
            bool ok = mqtt_add_user_property(&prop->m_userProperties, name, value);
            if (name) XString_delete_base(name);
            if (value) XString_delete_base(value);
            if (!ok) reader->ok = false;
            prop->m_availableProperties |= XMqttPublishProperties_UserProperty;
            break;
        }
        case MQTT_PROP_SUBSCRIPTION_ID: {
            uint32_t value = mqtt_read_varint(reader);
            if (!prop->m_subscriptionIdentifiers)
                prop->m_subscriptionIdentifiers = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(uint32_t), false);
            if (!value || !prop->m_subscriptionIdentifiers ||
                !XVector_push_back_1_base(prop->m_subscriptionIdentifiers, &value))
                reader->ok = false;
            prop->m_availableProperties |= XMqttPublishProperties_SubscriptionIdentifier;
            break;
        }
        case MQTT_PROP_CONTENT_TYPE:
            mqtt_replace_string(&prop->m_contentType, mqtt_read_string(reader));
            prop->m_availableProperties |= XMqttPublishProperties_ContentType;
            break;
        default: reader->ok = false; break;
        }
    }
    if (reader->pos != end) reader->ok = false;
    return reader->ok;
}

static bool mqtt_parse_reason_properties(XMqttReader* reader, XString** reason,
                                         XMqttUserProperties** userProperties)
{
    size_t end;
    if (!mqtt_reader_property_end(reader, &end)) return false;
    while (reader->ok && reader->pos < end) {
        uint8_t id = mqtt_read_u8(reader);
        if (id == MQTT_PROP_REASON_STRING) {
            mqtt_replace_string(reason, mqtt_read_string(reader));
        } else if (id == MQTT_PROP_USER_PROPERTY) {
            XString* name = mqtt_read_string(reader);
            XString* value = mqtt_read_string(reader);
            bool ok = mqtt_add_user_property(userProperties, name, value);
            if (name) XString_delete_base(name);
            if (value) XString_delete_base(value);
            if (!ok) reader->ok = false;
        } else {
            reader->ok = false;
        }
    }
    if (reader->pos != end) reader->ok = false;
    return reader->ok;
}

static bool mqtt_send_ack(XMqttClient* client, uint8_t type, uint16_t identifier,
                          uint8_t reason)
{
    XByteArray* payload = XByteArray_create();
    bool ok = payload && mqtt_append_u16(payload, identifier);
    if (ok && client->m_protocolVersion == XMqttClient_MQTT_5_0 && reason)
        ok = mqtt_append_u8(payload, reason) && mqtt_append_u8(payload, 0);
    if (ok) ok = mqtt_write_packet(client, type, payload);
    if (payload) XByteArray_delete_base(payload);
    return ok;
}

static int64_t mqtt_pending_publish_index(XMqttClient* client, uint16_t identifier)
{
    for (size_t i = 0; i < XVector_size_base(client->m_private->pendingPublishes); ++i) {
        XMqttPendingPublish* pending = (XMqttPendingPublish*)XVector_at_base(
            client->m_private->pendingPublishes, (int64_t)i);
        if (pending && pending->identifier == identifier) return (int64_t)i;
    }
    return -1;
}

static void mqtt_handle_connack(XMqttClient* client, XMqttReader* reader)
{
    if (client->m_state != XMqttClient_Connecting || reader->size < 2) {
        reader->ok = false; return;
    }
    uint8_t flags = mqtt_read_u8(reader);
    uint8_t reason = mqtt_read_u8(reader);
    if (flags & 0xFEU) { reader->ok = false; return; }
    if (client->m_protocolVersion != XMqttClient_MQTT_5_0 &&
        reader->pos != reader->size) { reader->ok = false; return; }
    if (client->m_serverConnectionProperties)
        XMqttServerConnectionProperties_delete_base(client->m_serverConnectionProperties);
    client->m_serverConnectionProperties = XMqttServerConnectionProperties_create();
    if (!client->m_serverConnectionProperties) { reader->ok = false; return; }
    client->m_serverConnectionProperties->m_valid = true;
    client->m_serverConnectionProperties->m_reasonCode = reason;
    if (client->m_protocolVersion == XMqttClient_MQTT_5_0 &&
        !mqtt_parse_connack_properties(client, reader,
                                       client->m_serverConnectionProperties)) return;
    if (reason != 0) {
        XMqttProtocol_fail(client, mqtt_connack_error(reason));
        return;
    }
    XMqttClient_setError(client, XMqttClient_NoError);
    XMqttClient_setState(client, XMqttClient_Connected);
    if (flags & 0x01U) XMqttClient_brokerSessionRestored_signal(client);
    if (client->m_autoKeepAlive && client->m_keepAlive && client->m_private) {
        uint16_t seconds = client->m_serverConnectionProperties->m_serverKeepAlive ?
            client->m_serverConnectionProperties->m_serverKeepAlive : client->m_keepAlive;
        client->m_private->keepAliveTimer = XObject_startTimer_ms(
            (XObject*)client, (uint64_t)seconds * 1000U, XTimerType_CoarseTimer);
    }
}

static void mqtt_handle_suback(XMqttClient* client, XMqttReader* reader, bool unsubscribe)
{
    uint16_t identifier = mqtt_read_u16(reader);
    XString* reason = NULL;
    XMqttUserProperties* users = NULL;
    uint8_t code = 0;
    if (client->m_protocolVersion == XMqttClient_MQTT_5_0) {
        /* MQTT 5: reason-code list precedes the property length.  One
         * subscription is emitted by this API, so exactly one code is
         * expected before the property block. */
        if (reader->pos >= reader->size) { reader->ok = false; goto cleanup; }
        code = mqtt_read_u8(reader);
        if (reader->ok && !mqtt_parse_reason_properties(reader, &reason, &users))
            goto cleanup;
    } else if (!unsubscribe) {
        /* MQTT 3.1.1 SUBACK contains one granted QoS and no properties. */
        if (reader->pos >= reader->size) { reader->ok = false; goto cleanup; }
        code = mqtt_read_u8(reader);
        if (reader->pos != reader->size) reader->ok = false;
    } else if (reader->pos != reader->size) {
        /* MQTT 3.1.1 UNSUBACK has no payload after the packet identifier. */
        reader->ok = false;
    }
    if (!unsubscribe && code > 2 && code < 0x80) reader->ok = false;
    for (size_t i = 0; i < XVector_size_base(client->m_private->subscriptions); ++i) {
        XMqttSubscriptionEntry* entry = (XMqttSubscriptionEntry*)XVector_at_base(
            client->m_private->subscriptions, (int64_t)i);
        if (!entry || entry->identifier != identifier || entry->unsubscribePending != unsubscribe)
            continue;
        XMqttSubscription* sub = entry->subscription;
        sub->m_reasonCode = code;
        mqtt_replace_string(&sub->m_reason, reason); reason = NULL;
        if (sub->m_userProperties) XMqttUserProperties_delete_base(sub->m_userProperties);
        sub->m_userProperties = users; users = NULL;
        if (unsubscribe) {
            XMqttSubscription_setState(sub, code < 0x80 ? XMqttSubscription_Unsubscribed
                                                        : XMqttSubscription_Error);
            XVector_removeAt_base(client->m_private->subscriptions, (int64_t)i);
        } else if (code <= 2) {
            XMqttSubscription_setQos(sub, code);
            XMqttSubscription_setState(sub, XMqttSubscription_Subscribed);
            entry->identifier = 0;
        } else {
            XMqttSubscription_setState(sub, XMqttSubscription_Error);
            entry->identifier = 0;
        }
        break;
    }
cleanup:
    if (reason) XString_delete_base(reason);
    if (users) XMqttUserProperties_delete_base(users);
}

static bool mqtt_incoming_qos2_seen(XMqttClient* client, uint16_t identifier)
{
    for (size_t i = 0; i < XVector_size_base(client->m_private->incomingQos2); ++i) {
        uint16_t* value = (uint16_t*)XVector_at_base(client->m_private->incomingQos2,
                                                     (int64_t)i);
        if (value && *value == identifier) return true;
    }
    return false;
}

static void mqtt_emit_incoming(XMqttClient* client, XMqttMessage* message)
{
    XMqttClient_messageReceived_signal(client, message->m_payload, message->m_topic);
    for (size_t i = 0; i < XVector_size_base(client->m_private->subscriptions); ++i) {
        XMqttSubscriptionEntry* entry = (XMqttSubscriptionEntry*)XVector_at_base(
            client->m_private->subscriptions, (int64_t)i);
        if (entry && !entry->unsubscribePending && entry->subscription &&
            XMqttSubscription_state(entry->subscription) == XMqttSubscription_Subscribed &&
            XMqttTopicFilter_match(entry->subscription->m_topic, message->m_topic,
                XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption))
            XMqttSubscription_messageReceived_signal(entry->subscription, message);
    }
}

static void mqtt_handle_publish(XMqttClient* client, uint8_t header, XMqttReader* reader)
{
    /* 用位域联合体解析固定头第一字节 */
    XMqttFixedHeader fixed = { header };
    uint8_t qos = (uint8_t)fixed.bits.qos;
    if (qos == 3) { reader->ok = false; return; }
    if (qos == 0 && fixed.bits.dup) { reader->ok = false; return; }
    XString* topic = mqtt_read_string(reader);
    uint16_t identifier = qos ? mqtt_read_u16(reader) : 0;
    if (qos && !identifier) reader->ok = false;
    XMqttPublishProperties* prop = XMqttPublishProperties_create();
    if (!topic || !prop) { reader->ok = false; goto cleanup; }
    if (client->m_protocolVersion == XMqttClient_MQTT_5_0 &&
        !mqtt_parse_publish_properties(reader, prop)) goto cleanup;
    if (prop->m_topicAlias) {
        uint16_t maximum = client->m_connectionProperties ?
            client->m_connectionProperties->m_maximumTopicAlias : 0;
        if (prop->m_topicAlias > maximum) { reader->ok = false; goto cleanup; }
        if (mqtt_string_present(topic)) {
            if (!mqtt_alias_set(client->m_private->receiveAliases, prop->m_topicAlias, topic))
                reader->ok = false;
        } else {
            XMqttTopicAliasEntry* alias = mqtt_alias_by_id(client->m_private->receiveAliases,
                                                           prop->m_topicAlias);
            if (!alias) reader->ok = false;
            else mqtt_replace_string(&topic, XString_create_copy(alias->topic));
        }
    }
    XMqttTopicName* topicName = reader->ok ? XMqttTopicName_create(XString_toUtf8(topic)) : NULL;
    if (!topicName || !XMqttTopicName_isValid(topicName)) {
        if (topicName) XMqttTopicName_delete_base(topicName);
        reader->ok = false; goto cleanup;
    }
    bool duplicateQos2 = qos == 2 && mqtt_incoming_qos2_seen(client, identifier);
    if (!duplicateQos2) {
        XMqttMessage* message = XMqttMessage_create_full(
            XString_toUtf8(topic), reader->data + reader->pos, reader->size - reader->pos,
            identifier, qos, fixed.bits.dup != 0, fixed.bits.retain != 0);
        if (!message) { XMqttTopicName_delete_base(topicName); reader->ok = false; goto cleanup; }
        if (message->m_publishProperties)
            XMqttPublishProperties_delete_base(message->m_publishProperties);
        message->m_publishProperties = prop; prop = NULL;
        mqtt_emit_incoming(client, message);
        if (identifier)
            XMqttClient_messageStatusChanged_signal(client, identifier,
                                                    XMqtt_MessageStatus_Published, NULL);
        XMqttMessage_delete_base(message);
        if (qos == 2 && !XVector_push_back_1_base(client->m_private->incomingQos2,
                                                  &identifier)) reader->ok = false;
    }
    XMqttTopicName_delete_base(topicName);
    reader->pos = reader->size;
    if (reader->ok && qos == 1) mqtt_send_ack(client, MQTT_PUBACK, identifier, 0);
    if (reader->ok && qos == 2) mqtt_send_ack(client, MQTT_PUBREC, identifier, 0);
cleanup:
    if (topic) XString_delete_base(topic);
    if (prop) XMqttPublishProperties_delete_base(prop);
}

static void mqtt_handle_publish_ack(XMqttClient* client, uint8_t type, XMqttReader* reader)
{
    uint16_t identifier = mqtt_read_u16(reader);
    if (!identifier) { reader->ok = false; return; }
    uint8_t reason = reader->pos < reader->size ? mqtt_read_u8(reader) : 0;
    XMqttMessageStatusProperties* prop = XMqttMessageStatusProperties_create();
    if (!prop) { reader->ok = false; return; }
    prop->m_reasonCode = reason;
    if (client->m_protocolVersion == XMqttClient_MQTT_5_0 && reader->pos < reader->size)
        if (!mqtt_parse_reason_properties(reader, &prop->m_reason, &prop->m_userProperties))
            reader->ok = false;
    if (client->m_protocolVersion != XMqttClient_MQTT_5_0 && reader->pos != reader->size)
        reader->ok = false;
    if (reader->pos != reader->size) reader->ok = false;
    int64_t index = mqtt_pending_publish_index(client, identifier);
    if (index < 0) { XMqttMessageStatusProperties_delete_base(prop); return; }
    XMqttPendingPublish* pending = (XMqttPendingPublish*)XVector_at_base(
        client->m_private->pendingPublishes, index);
    if (type == MQTT_PUBREC && pending->qos == 2) {
        pending->stage = 1;
        XMqttClient_messageStatusChanged_signal(client, identifier,
                                                XMqtt_MessageStatus_Received, prop);
        mqtt_send_ack(client, MQTT_PUBREL | 0x02U, identifier, 0);
        XMqttClient_messageStatusChanged_signal(client, identifier,
                                                XMqtt_MessageStatus_Released, NULL);
    } else if ((type == MQTT_PUBACK && pending->qos == 1) ||
               (type == MQTT_PUBCOMP && pending->qos == 2)) {
        XMqttClient_messageStatusChanged_signal(client, identifier,
            type == MQTT_PUBACK ? XMqtt_MessageStatus_Acknowledged
                                : XMqtt_MessageStatus_Completed, prop);
        XMqttClient_messageSent_signal(client, identifier);
        XVector_removeAt_base(client->m_private->pendingPublishes, index);
    } else {
        reader->ok = false;
    }
    XMqttMessageStatusProperties_delete_base(prop);
}

static void mqtt_handle_pubrel(XMqttClient* client, XMqttReader* reader)
{
    uint16_t identifier = mqtt_read_u16(reader);
    for (size_t i = 0; i < XVector_size_base(client->m_private->incomingQos2); ++i) {
        uint16_t* value = (uint16_t*)XVector_at_base(client->m_private->incomingQos2,
                                                     (int64_t)i);
        if (value && *value == identifier) {
            XVector_removeAt_base(client->m_private->incomingQos2, (int64_t)i);
            break;
        }
    }
    mqtt_send_ack(client, MQTT_PUBCOMP, identifier, 0);
}

static void mqtt_handle_auth(XMqttClient* client, XMqttReader* reader)
{
    uint8_t reason = reader->pos < reader->size ? mqtt_read_u8(reader) : 0;
    if (client->m_protocolVersion != XMqttClient_MQTT_5_0 ||
        (reason != XMqtt_ReasonCode_Success &&
         reason != XMqtt_ReasonCode_ContinueAuthentication &&
         reason != XMqtt_ReasonCode_ReAuthenticate)) {
        reader->ok = false;
        return;
    }
    XMqttAuthenticationProperties* prop = XMqttAuthenticationProperties_create();
    if (!prop) { reader->ok = false; return; }
    size_t end = reader->size;
    if (reader->pos < end) {
        size_t propertiesEnd;
        if (!mqtt_reader_property_end(reader, &propertiesEnd)) goto cleanup;
        while (reader->ok && reader->pos < propertiesEnd) {
            uint8_t id = mqtt_read_u8(reader);
            if (id == MQTT_PROP_AUTH_METHOD)
                mqtt_replace_string(&prop->m_authenticationMethod, mqtt_read_string(reader));
            else if (id == MQTT_PROP_AUTH_DATA)
                mqtt_replace_binary(&prop->m_authenticationData, mqtt_read_binary(reader));
            else if (id == MQTT_PROP_REASON_STRING)
                mqtt_replace_string(&prop->m_reason, mqtt_read_string(reader));
            else if (id == MQTT_PROP_USER_PROPERTY) {
                XString* name = mqtt_read_string(reader);
                XString* value = mqtt_read_string(reader);
                bool ok = mqtt_add_user_property(&prop->m_userProperties, name, value);
                if (name) XString_delete_base(name);
                if (value) XString_delete_base(value);
                if (!ok) reader->ok = false;
            } else reader->ok = false;
        }
    }
    if (reason == XMqtt_ReasonCode_ContinueAuthentication ||
        reason == XMqtt_ReasonCode_ReAuthenticate)
        XMqttClient_authenticationRequested_signal(client, prop);
    else
        XMqttClient_authenticationFinished_signal(client, prop);
cleanup:
    XMqttAuthenticationProperties_delete_base(prop);
}

static void mqtt_process_packet(XMqttClient* client, uint8_t header,
                                const uint8_t* data, size_t size)
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
    if (!reader.ok) { XMqttProtocol_fail(client, XMqttClient_ProtocolViolation); return; }
    switch (header & 0xF0U) {
    case MQTT_CONNACK: mqtt_handle_connack(client, &reader); break;
    case MQTT_PUBLISH: mqtt_handle_publish(client, header, &reader); break;
    case MQTT_PUBACK: case MQTT_PUBREC: case MQTT_PUBCOMP:
        mqtt_handle_publish_ack(client, header & 0xF0U, &reader); break;
    case MQTT_PUBREL: mqtt_handle_pubrel(client, &reader); break;
    case MQTT_SUBACK: mqtt_handle_suback(client, &reader, false); break;
    case MQTT_UNSUBACK: mqtt_handle_suback(client, &reader, true); break;
    case MQTT_PINGRESP:
        if (size != 0) reader.ok = false;
        else {
            client->m_private->pingTimeout = 0;
            XMqttClient_pingResponseReceived_signal(client);
        }
        break;
    case MQTT_DISCONNECT: {
        uint8_t reason = size ? mqtt_read_u8(&reader) : 0;
        if (client->m_protocolVersion != XMqttClient_MQTT_5_0 && size != 0)
            reader.ok = false;
        if (client->m_protocolVersion == XMqttClient_MQTT_5_0 && reader.pos < reader.size) {
            size_t end;
            if (!mqtt_reader_property_end(&reader, &end) || end != reader.size)
                reader.ok = false;
        }
        if (reason >= 0x80) XMqttClient_setError(client, XMqttClient_Mqtt5SpecificError);
        XMqttClient_setState(client, XMqttClient_Disconnected);
        break;
    }
    case MQTT_AUTH: mqtt_handle_auth(client, &reader); break;
    default: reader.ok = false; break;
    }
    if (!reader.ok) XMqttProtocol_fail(client, XMqttClient_ProtocolViolation);
}

void XMqttProtocol_feed(XMqttClient* client, const uint8_t* data, size_t size)
{
    if (!client || !client->m_private || (!data && size) ||
        !mqtt_append(client->m_private->input, data, size) ||
        client->m_private->processingInput) return;
    client->m_private->processingInput = true;
    while (XByteArray_size_base(client->m_private->input) >= 2) {
        const uint8_t* input = (const uint8_t*)XByteArray_constData(client->m_private->input);
        size_t inputSize = XByteArray_size_base(client->m_private->input);
        uint32_t remaining = 0, multiplier = 1;
        size_t pos = 1;
        bool completeLength = false;
        for (int i = 0; i < 4 && pos < inputSize; ++i) {
            uint8_t byte = input[pos++];
            remaining += (uint32_t)(byte & 0x7FU) * multiplier;
            if (!(byte & 0x80U)) { completeLength = true; break; }
            multiplier *= 128U;
        }
        if (!completeLength) {
            if (pos >= 5 || inputSize >= 5) XMqttProtocol_fail(client, XMqttClient_ProtocolViolation);
            break;
        }
        if (remaining > 268435455U) {
            XMqttProtocol_fail(client, XMqttClient_ProtocolViolation); break;
        }
        if (inputSize < pos + remaining) break;
        uint8_t header = input[0];
        mqtt_process_packet(client, header, input + pos, remaining);
        XByteArray_remove_base(client->m_private->input, 0, (int64_t)(pos + remaining));
        if (client->m_state == XMqttClient_Disconnected &&
            client->m_error != XMqttClient_NoError) break;
    }
    client->m_private->processingInput = false;
}

void XMqttProtocol_fail(XMqttClient* client, XMqttClient_Error error)
{
    if (!client) return;
    XMqttClient_setError(client, error);
    if (client->m_transport) XIODevice_close_base((XIODevice*)client->m_transport);
    XMqttClient_setState(client, XMqttClient_Disconnected);
}

void XMqttProtocol_timerEvent(XMqttClient* client, XTimerEvent* event)
{
    if (!client || !client->m_private || !event ||
        XTimerEvent_timerId(event) != client->m_private->keepAliveTimer) return;
    if (client->m_private->pingTimeout) {
        XMqttProtocol_fail(client, XMqttClient_TransportInvalid);
        return;
    }
    XMqttProtocol_sendPing(client, true);
}

#endif /* XMQTT_CLIENT_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
