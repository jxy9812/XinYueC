#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_CLIENT_ON
#include "XMqttClient_p.h"
#include "XTcpSocket.h"
#include "XRandomGenerator.h"
#include "XMemory.h"
#include <string.h>

static void V_deinit(XMqttClient* client);
static void V_timerEvent(XObject* object, XTimerEvent* event);
static void V_connectToHost(XMqttClient* client);
static void V_disconnectFromHost(XMqttClient* client);
static XMqttSubscription* V_subscribe(XMqttClient* client,
                                      const XMqttTopicFilter* topic, uint8_t qos);
static void V_unsubscribe(XMqttClient* client, const XMqttTopicFilter* topic);
static int32_t V_publish(XMqttClient* client, const XMqttTopicName* topic,
                         const uint8_t* message, size_t messageLen,
                         uint8_t qos, bool retain);
static bool V_requestPing(XMqttClient* client);

static void mqtt_on_ready_read(XObject* receiver, XVarList* args);
static void mqtt_on_connected(XObject* receiver, XVarList* args);
static void mqtt_on_encrypted(XObject* receiver, XVarList* args);
static void mqtt_on_disconnected(XObject* receiver, XVarList* args);
static void mqtt_on_transport_error(XObject* receiver, XVarList* args);
static void mqtt_detach_transport(XMqttClient* client, void* transport);

XVtable* XMqttClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttClient)
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        V_connectToHost, V_disconnectFromHost, V_subscribe,
        V_unsubscribe, V_publish, V_requestPing
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, V_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, V_timerEvent);
    return XVTABLE_DEFAULT;
}

static XString* mqtt_generated_client_id(void)
{
    static const char hex[] = "0123456789abcdef";
    char value[24];
    XRandomGenerator* random = XRandomGenerator_global();
    uint32_t bits = 0;
    for (size_t i = 0; i < 23; ++i) {
        if ((i & 7U) == 0) bits = random ? XRandomGenerator_generate(random) : (uint32_t)i;
        value[i] = hex[(bits >> ((i & 7U) * 4U)) & 0x0FU];
    }
    value[23] = '\0';
    return XString_create_utf8(value);
}

XMqttClient* XMqttClient_create(void)
{
    XMqttClient* client = (XMqttClient*)XMalloc_System(sizeof(XMqttClient));
    if (client) {
        XMqttClient_init(client);
        Set_Class_MemoryFree(client, XFree_System);
    }
    return client;
}

void XMqttClient_init(XMqttClient* client)
{
    if (!client) return;
    memset(client, 0, sizeof(*client));
    XObject_init((XObject*)client);
    XClassGetVtable(client) = XMqttClient_class_init();
    client->m_hostname = XString_create_utf8("");
    client->m_clientId = mqtt_generated_client_id();
    client->m_username = XString_create_utf8("");
    client->m_password = XString_create_utf8("");
    client->m_willTopic = XString_create_utf8("");
    client->m_willMessage = XByteArray_create();
    client->m_connectionProperties = XMqttConnectionProperties_create();
    client->m_lastWillProperties = XMqttLastWillProperties_create();
    client->m_serverConnectionProperties = XMqttServerConnectionProperties_create();
    client->m_private = XMqttClientPrivate_create();
    client->m_port = 0;
    client->m_keepAlive = 60;
    client->m_protocolVersion = XMqttClient_MQTT_3_1_1;
    client->m_state = XMqttClient_Disconnected;
    client->m_error = XMqttClient_NoError;
    client->m_cleanSession = true;
    client->m_autoKeepAlive = true;
    /* Qt 6.8 initializes the transport kind to IODevice; an internally
     * created socket selects AbstractSocket during connectToHost(). */
    client->m_transportType = XMqttClient_IODevice;
}

static void V_deinit(XMqttClient* client)
{
    if (!client) return;
    if (client->m_transport) mqtt_detach_transport(client, client->m_transport);
    XMqttClientPrivate_resetConnection(client, true);
    if (client->m_private) {
        XMqttClientPrivate_delete(client->m_private);
        client->m_private = NULL;
    }
    if (client->m_hostname) XString_delete_base(client->m_hostname);
    if (client->m_clientId) XString_delete_base(client->m_clientId);
    if (client->m_username) XString_delete_base(client->m_username);
    if (client->m_password) XString_delete_base(client->m_password);
    if (client->m_willTopic) XString_delete_base(client->m_willTopic);
    if (client->m_willMessage) XByteArray_delete_base(client->m_willMessage);
    if (client->m_connectionProperties)
        XMqttConnectionProperties_delete_base(client->m_connectionProperties);
    if (client->m_lastWillProperties)
        XMqttLastWillProperties_delete_base(client->m_lastWillProperties);
    if (client->m_serverConnectionProperties)
        XMqttServerConnectionProperties_delete_base(client->m_serverConnectionProperties);
    client->m_transport = NULL;
    XClass_Deinit_Parent(XObject, client);
}

static void V_timerEvent(XObject* object, XTimerEvent* event)
{
    XMqttProtocol_timerEvent((XMqttClient*)object, event);
}

static bool mqtt_transport_is_socket(const XMqttClient* client)
{
    return client && (client->m_transportType == XMqttClient_AbstractSocket ||
                      client->m_transportType == XMqttClient_SecureSocket);
}

static void mqtt_attach_transport(XMqttClient* client, void* transport)
{
    if (!client || !transport) return;
    XObject_connect_1((XObject*)transport, XSignal(XIODevice_readyRead_signal),
                      (XObject*)client, mqtt_on_ready_read, XConnectionType_Auto);
    if (mqtt_transport_is_socket(client)) {
        XObject_connect_1((XObject*)transport, XSignal(XAbstractSocket_connected_signal),
                          (XObject*)client, mqtt_on_connected, XConnectionType_Auto);
        XObject_connect_1((XObject*)transport, XSignal(XAbstractSocket_disconnected_signal),
                          (XObject*)client, mqtt_on_disconnected, XConnectionType_Auto);
        XObject_connect_1((XObject*)transport, XSignal(XAbstractSocket_errorOccurred_signal),
                          (XObject*)client, mqtt_on_transport_error, XConnectionType_Auto);
    }
    if (client->m_transportType == XMqttClient_SecureSocket)
        XObject_connect_1((XObject*)transport, XSignal(XSslSocket_encrypted_signal),
                          (XObject*)client, mqtt_on_encrypted, XConnectionType_Auto);
}

static void mqtt_detach_transport(XMqttClient* client, void* transport)
{
    if (!client || !transport) return;
    XObject_disconnect_1((XObject*)transport, XSignal(XIODevice_readyRead_signal),
                         (XObject*)client, mqtt_on_ready_read);
    if (mqtt_transport_is_socket(client)) {
        XObject_disconnect_1((XObject*)transport, XSignal(XAbstractSocket_connected_signal),
                             (XObject*)client, mqtt_on_connected);
        XObject_disconnect_1((XObject*)transport, XSignal(XAbstractSocket_disconnected_signal),
                             (XObject*)client, mqtt_on_disconnected);
        XObject_disconnect_1((XObject*)transport, XSignal(XAbstractSocket_errorOccurred_signal),
                             (XObject*)client, mqtt_on_transport_error);
    }
    if (client->m_transportType == XMqttClient_SecureSocket)
        XObject_disconnect_1((XObject*)transport, XSignal(XSslSocket_encrypted_signal),
                             (XObject*)client, mqtt_on_encrypted);
}

void XMqttClient_setTransport(XMqttClient* client, void* device,
                              XMqttClient_TransportType transport)
{
    if (!client || client->m_state != XMqttClient_Disconnected ||
        transport < XMqttClient_IODevice || transport > XMqttClient_SecureSocket) return;
    if (client->m_transport == device && client->m_transportType == transport) return;
    if (client->m_private && client->m_private->ownsTransport && client->m_transport) {
        mqtt_detach_transport(client, client->m_transport);
        XObject_setParent((XObject*)client->m_transport, NULL);
        XObject_deleteLater((XObject*)client->m_transport);
    }
    else if (client->m_transport) {
        mqtt_detach_transport(client, client->m_transport);
    }
    client->m_transport = device;
    client->m_transportType = transport;
    if (client->m_private) client->m_private->ownsTransport = false;
    if (device) mqtt_attach_transport(client, device);
}

void* XMqttClient_transport(const XMqttClient* client)
{
    return client ? client->m_transport : NULL;
}

XMqttSubscription* XMqttClient_subscribe(XMqttClient* client,
                                         const XMqttTopicFilter* topic, uint8_t qos)
{
    if (!client) return NULL;
    XMqttSubscription* (*func)(XMqttClient*, const XMqttTopicFilter*, uint8_t) =
        XClassGetVirtualFunc(client, EXMqttClient_Subscribe,
            XMqttSubscription* (*)(XMqttClient*, const XMqttTopicFilter*, uint8_t));
    return func ? func(client, topic, qos) : NULL;
}

XMqttSubscription* XMqttClient_subscribe_with_properties(
    XMqttClient* client, const XMqttTopicFilter* topic,
    const XMqttSubscriptionProperties* properties, uint8_t qos)
{
    return XMqttProtocol_subscribe(client, topic, properties, qos);
}

void XMqttClient_unsubscribe(XMqttClient* client, const XMqttTopicFilter* topic)
{
    if (!client) return;
    void (*func)(XMqttClient*, const XMqttTopicFilter*) =
        XClassGetVirtualFunc(client, EXMqttClient_Unsubscribe,
                            void (*)(XMqttClient*, const XMqttTopicFilter*));
    if (func) func(client, topic);
}

void XMqttClient_unsubscribe_with_properties(
    XMqttClient* client, const XMqttTopicFilter* topic,
    const XMqttUnsubscriptionProperties* properties)
{
    XMqttProtocol_unsubscribe(client, topic, properties);
}

int32_t XMqttClient_publish(XMqttClient* client, const XMqttTopicName* topic,
                            const uint8_t* message, size_t messageLen,
                            uint8_t qos, bool retain)
{
    if (!client) return -1;
    int32_t (*func)(XMqttClient*, const XMqttTopicName*, const uint8_t*, size_t,
                    uint8_t, bool) =
        XClassGetVirtualFunc(client, EXMqttClient_Publish,
            int32_t (*)(XMqttClient*, const XMqttTopicName*, const uint8_t*,
                        size_t, uint8_t, bool));
    return func ? func(client, topic, message, messageLen, qos, retain) : -1;
}

int32_t XMqttClient_publish_with_properties(
    XMqttClient* client, const XMqttTopicName* topic,
    const XMqttPublishProperties* properties, const uint8_t* message,
    size_t messageLen, uint8_t qos, bool retain)
{
    return XMqttProtocol_publish(client, topic, properties, message, messageLen,
                                 qos, retain);
}

bool XMqttClient_requestPing(XMqttClient* client)
{
    if (!client) return false;
    bool (*func)(XMqttClient*) = XClassGetVirtualFunc(
        client, EXMqttClient_RequestPing, bool (*)(XMqttClient*));
    return func ? func(client) : false;
}

void XMqttClient_connectToHost_base(XMqttClient* client)
{
    if (!client) return;
    void (*func)(XMqttClient*) = XClassGetVirtualFunc(
        client, EXMqttClient_ConnectToHost, void (*)(XMqttClient*));
    if (func) func(client);
}

void XMqttClient_connectToHostEncrypted(XMqttClient* client,
                                        const XSslConfiguration* sslConfig)
{
    if (!client || client->m_state != XMqttClient_Disconnected) return;
    if (!client->m_transport || client->m_transportType != XMqttClient_SecureSocket) {
        XSslSocket* socket = XSslSocket_create();
        if (!socket) { XMqttProtocol_fail(client, XMqttClient_TransportInvalid); return; }
        XMqttClient_setTransport(client, socket, XMqttClient_SecureSocket);
        XObject_setParent((XObject*)socket, client);
        client->m_private->ownsTransport = true;
    }
    if (sslConfig)
        XSslSocket_setSslConfiguration((XSslSocket*)client->m_transport, sslConfig);
    client->m_private->encryptedRequested = true;
    XMqttClient_connectToHost_base(client);
}

void XMqttClient_disconnectFromHost_base(XMqttClient* client)
{
    if (!client) return;
    void (*func)(XMqttClient*) = XClassGetVirtualFunc(
        client, EXMqttClient_DisconnectFromHost, void (*)(XMqttClient*));
    if (func) func(client);
}

static void V_connectToHost(XMqttClient* client)
{
    if (!client || client->m_state != XMqttClient_Disconnected ||
        !client->m_private) return;
    XMqttClientPrivate_resetConnection(client, false);
    XMqttClient_setError(client, XMqttClient_NoError);
    if (!client->m_transport) {
        XTcpSocket* socket = XTcpSocket_create();
        if (!socket) { XMqttProtocol_fail(client, XMqttClient_TransportInvalid); return; }
        XMqttClient_setTransport(client, socket, XMqttClient_AbstractSocket);
        XObject_setParent((XObject*)socket, client);
        client->m_private->ownsTransport = true;
    }
    XMqttClient_setState(client, XMqttClient_Connecting);
    if (client->m_transportType == XMqttClient_IODevice) {
        if (!XIODevice_isOpen((XIODevice*)client->m_transport))
            XMqttProtocol_fail(client, XMqttClient_TransportInvalid);
        else
            XMqttProtocol_sendConnect(client);
        return;
    }
    if (!client->m_hostname || XString_toUtf8_length(client->m_hostname) == 0) {
        XMqttProtocol_fail(client, XMqttClient_TransportInvalid);
        return;
    }
    if (client->m_transportType == XMqttClient_SecureSocket &&
        client->m_private->encryptedRequested) {
        XSslSocket_connectToHostEncrypted((XSslSocket*)client->m_transport,
                                          client->m_hostname, client->m_port);
    } else {
        XAbstractSocket_connectToHost_base((XAbstractSocket*)client->m_transport,
            XString_toUtf8(client->m_hostname), client->m_port,
            XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    }
}

static void V_disconnectFromHost(XMqttClient* client)
{
    if (!client || client->m_state == XMqttClient_Disconnected) return;
    if (client->m_state == XMqttClient_Connected) XMqttProtocol_sendDisconnect(client);
    if (client->m_transport) {
        mqtt_detach_transport(client, client->m_transport);
        if (mqtt_transport_is_socket(client))
            XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)client->m_transport);
        else
            XIODevice_close_base((XIODevice*)client->m_transport);
    }
    XMqttClientPrivate_resetConnection(client, true);
    XMqttClient_setState(client, XMqttClient_Disconnected);
}

static XMqttSubscription* V_subscribe(XMqttClient* client,
                                      const XMqttTopicFilter* topic, uint8_t qos)
{
    return XMqttProtocol_subscribe(client, topic, NULL, qos);
}

static void V_unsubscribe(XMqttClient* client, const XMqttTopicFilter* topic)
{
    XMqttProtocol_unsubscribe(client, topic, NULL);
}

static int32_t V_publish(XMqttClient* client, const XMqttTopicName* topic,
                         const uint8_t* message, size_t messageLen,
                         uint8_t qos, bool retain)
{
    return XMqttProtocol_publish(client, topic, NULL, message, messageLen, qos, retain);
}

static bool V_requestPing(XMqttClient* client)
{
    return XMqttProtocol_sendPing(client, false);
}

static void mqtt_on_ready_read(XObject* receiver, XVarList* args)
{
    (void)args;
    XMqttClient* client = (XMqttClient*)receiver;
    if (!client || !client->m_transport) return;
    XByteArray* data = XIODevice_readAll_3((XIODevice*)client->m_transport);
    if (data) {
        XMqttProtocol_feed(client, (const uint8_t*)XByteArray_constData(data),
                           XByteArray_size_base(data));
        XByteArray_delete_base(data);
    }
}

static void mqtt_on_connected(XObject* receiver, XVarList* args)
{
    (void)args;
    XMqttClient* client = (XMqttClient*)receiver;
    if (client && client->m_transportType != XMqttClient_SecureSocket)
        XMqttProtocol_sendConnect(client);
}

static void mqtt_on_encrypted(XObject* receiver, XVarList* args)
{
    (void)args;
    XMqttProtocol_sendConnect((XMqttClient*)receiver);
}

static void mqtt_on_disconnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XMqttClient* client = (XMqttClient*)receiver;
    if (!client) return;
    XMqttClientPrivate_resetConnection(client, true);
    XMqttClient_setState(client, XMqttClient_Disconnected);
}

static void mqtt_on_transport_error(XObject* receiver, XVarList* args)
{
    (void)args;
    XMqttProtocol_fail((XMqttClient*)receiver, XMqttClient_TransportInvalid);
}

const XString* XMqttClient_hostname_const(const XMqttClient* c) { return c ? c->m_hostname : NULL; }
XString* XMqttClient_hostname(const XMqttClient* c) { return c && c->m_hostname ? XString_create_copy(c->m_hostname) : NULL; }
uint16_t XMqttClient_port(const XMqttClient* c) { return c ? c->m_port : 0; }
const XString* XMqttClient_clientId_const(const XMqttClient* c) { return c ? c->m_clientId : NULL; }
XString* XMqttClient_clientId(const XMqttClient* c) { return c && c->m_clientId ? XString_create_copy(c->m_clientId) : NULL; }
uint16_t XMqttClient_keepAlive(const XMqttClient* c) { return c ? c->m_keepAlive : 0; }
XMqttClient_ProtocolVersion XMqttClient_protocolVersion(const XMqttClient* c) { return c ? c->m_protocolVersion : XMqttClient_MQTT_3_1_1; }
XMqttClient_State XMqttClient_state(const XMqttClient* c) { return c ? c->m_state : XMqttClient_Disconnected; }
XMqttClient_Error XMqttClient_error(const XMqttClient* c) { return c ? c->m_error : XMqttClient_UnknownError; }
const XString* XMqttClient_username_const(const XMqttClient* c) { return c ? c->m_username : NULL; }
XString* XMqttClient_username(const XMqttClient* c) { return c && c->m_username ? XString_create_copy(c->m_username) : NULL; }
const XString* XMqttClient_password_const(const XMqttClient* c) { return c ? c->m_password : NULL; }
XString* XMqttClient_password(const XMqttClient* c) { return c && c->m_password ? XString_create_copy(c->m_password) : NULL; }
bool XMqttClient_cleanSession(const XMqttClient* c) { return c ? c->m_cleanSession : true; }
const XString* XMqttClient_willTopic_const(const XMqttClient* c) { return c ? c->m_willTopic : NULL; }
XString* XMqttClient_willTopic(const XMqttClient* c) { return c && c->m_willTopic ? XString_create_copy(c->m_willTopic) : NULL; }
uint8_t XMqttClient_willQoS(const XMqttClient* c) { return c ? c->m_willQoS : 0; }
const XByteArray* XMqttClient_willMessage_const(const XMqttClient* c) { return c ? c->m_willMessage : NULL; }
XByteArray* XMqttClient_willMessage(const XMqttClient* c) { return c && c->m_willMessage ? XByteArray_create_copy(c->m_willMessage) : NULL; }
bool XMqttClient_willRetain(const XMqttClient* c) { return c ? c->m_willRetain : false; }
bool XMqttClient_autoKeepAlive(const XMqttClient* c) { return c ? c->m_autoKeepAlive : true; }

static bool mqtt_configurable(const XMqttClient* client)
{
    return client && client->m_state == XMqttClient_Disconnected;
}

static bool mqtt_string_equals_utf8(const XString* value, const char* utf8)
{
    return value && XString_equals_utf8(value, utf8 ? utf8 : "", XChar_CaseSensitive);
}

static void mqtt_set_string(XString** target, const char* value)
{
    XString* replacement = XString_create_utf8(value ? value : "");
    if (!replacement) return;
    if (*target) XString_delete_base(*target);
    *target = replacement;
}

void XMqttClient_setHostname(XMqttClient* c, const char* value)
{
    if (!mqtt_configurable(c) || mqtt_string_equals_utf8(c->m_hostname, value)) return;
    mqtt_set_string(&c->m_hostname, value); XMqttClient_hostnameChanged_signal(c, c->m_hostname);
}
void XMqttClient_setPort(XMqttClient* c, uint16_t value)
{
    if (!mqtt_configurable(c) || c->m_port == value) return;
    c->m_port = value; XMqttClient_portChanged_signal(c, value);
}
void XMqttClient_setClientId(XMqttClient* c, const char* value)
{
    if (!mqtt_configurable(c) || mqtt_string_equals_utf8(c->m_clientId, value)) return;
    mqtt_set_string(&c->m_clientId, value); XMqttClient_clientIdChanged_signal(c, c->m_clientId);
}
void XMqttClient_setKeepAlive(XMqttClient* c, uint16_t value)
{
    if (!mqtt_configurable(c) || c->m_keepAlive == value) return;
    c->m_keepAlive = value; XMqttClient_keepAliveChanged_signal(c, value);
}
void XMqttClient_setProtocolVersion(XMqttClient* c, XMqttClient_ProtocolVersion value)
{
    if (!mqtt_configurable(c) || c->m_protocolVersion == value ||
        (value != XMqttClient_MQTT_3_1 && value != XMqttClient_MQTT_3_1_1 &&
         value != XMqttClient_MQTT_5_0)) return;
    c->m_protocolVersion = value; XMqttClient_protocolVersionChanged_signal(c, value);
}
void XMqttClient_setState(XMqttClient* c, XMqttClient_State value)
{
    if (!c || c->m_state == value || value < XMqttClient_Disconnected ||
        value > XMqttClient_Connected) return;
    c->m_state = value; XMqttClient_stateChanged_signal(c, value);
    if (value == XMqttClient_Connected) XMqttClient_connected_signal(c);
    if (value == XMqttClient_Disconnected) XMqttClient_disconnected_signal(c);
}
void XMqttClient_setError(XMqttClient* c, XMqttClient_Error value)
{
    if (!c || c->m_error == value) return;
    c->m_error = value; XMqttClient_errorChanged_signal(c, value);
}
void XMqttClient_setUsername(XMqttClient* c, const char* value)
{
    if (!mqtt_configurable(c) || mqtt_string_equals_utf8(c->m_username, value)) return;
    mqtt_set_string(&c->m_username, value); XMqttClient_usernameChanged_signal(c, c->m_username);
}
void XMqttClient_setPassword(XMqttClient* c, const char* value)
{
    if (!mqtt_configurable(c) || mqtt_string_equals_utf8(c->m_password, value)) return;
    mqtt_set_string(&c->m_password, value); XMqttClient_passwordChanged_signal(c, c->m_password);
}
void XMqttClient_setCleanSession(XMqttClient* c, bool value)
{
    if (!mqtt_configurable(c) || c->m_cleanSession == value) return;
    c->m_cleanSession = value; XMqttClient_cleanSessionChanged_signal(c, value);
}
void XMqttClient_setWillTopic(XMqttClient* c, const char* value)
{
    if (!mqtt_configurable(c) || mqtt_string_equals_utf8(c->m_willTopic, value)) return;
    mqtt_set_string(&c->m_willTopic, value); XMqttClient_willTopicChanged_signal(c, c->m_willTopic);
}
void XMqttClient_setWillQoS(XMqttClient* c, uint8_t value)
{
    if (!mqtt_configurable(c) || value > 2 || c->m_willQoS == value) return;
    c->m_willQoS = value; XMqttClient_willQoSChanged_signal(c, value);
}
void XMqttClient_setWillMessage(XMqttClient* c, const uint8_t* data, size_t size)
{
    if (!mqtt_configurable(c) || (!data && size)) return;
    XByteArray* value = data && size ? XByteArray_create_with_data((const char*)data, size)
                                     : XByteArray_create();
    if (!value || (c->m_willMessage && XByteArray_compare(c->m_willMessage, value) == 0)) {
        if (value) XByteArray_delete_base(value); return;
    }
    if (c->m_willMessage) XByteArray_delete_base(c->m_willMessage);
    c->m_willMessage = value; XMqttClient_willMessageChanged_signal(c, value);
}
void XMqttClient_setWillRetain(XMqttClient* c, bool value)
{
    if (!mqtt_configurable(c) || c->m_willRetain == value) return;
    c->m_willRetain = value; XMqttClient_willRetainChanged_signal(c, value);
}
void XMqttClient_setAutoKeepAlive(XMqttClient* c, bool value)
{
    if (!mqtt_configurable(c) || c->m_autoKeepAlive == value) return;
    c->m_autoKeepAlive = value; XMqttClient_autoKeepAliveChanged_signal(c, value);
}

void XMqttClient_setConnectionProperties(XMqttClient* c,
                                         const XMqttConnectionProperties* value)
{
    if (!mqtt_configurable(c)) return;
    XMqttConnectionProperties* copy = value ? XMqttConnectionProperties_create_copy(value)
                                            : XMqttConnectionProperties_create();
    if (!copy) return;
    if (c->m_connectionProperties) XMqttConnectionProperties_delete_base(c->m_connectionProperties);
    c->m_connectionProperties = copy;
}
XMqttConnectionProperties* XMqttClient_connectionProperties(const XMqttClient* c)
{ return c && c->m_connectionProperties ? XMqttConnectionProperties_create_copy(c->m_connectionProperties) : NULL; }
const XMqttConnectionProperties* XMqttClient_connectionProperties_const(const XMqttClient* c)
{ return c ? c->m_connectionProperties : NULL; }
void XMqttClient_setLastWillProperties(XMqttClient* c,
                                       const XMqttLastWillProperties* value)
{
    if (!mqtt_configurable(c)) return;
    XMqttLastWillProperties* copy = value ? XMqttLastWillProperties_create_copy(value)
                                         : XMqttLastWillProperties_create();
    if (!copy) return;
    if (c->m_lastWillProperties) XMqttLastWillProperties_delete_base(c->m_lastWillProperties);
    c->m_lastWillProperties = copy;
}
XMqttLastWillProperties* XMqttClient_lastWillProperties(const XMqttClient* c)
{ return c && c->m_lastWillProperties ? XMqttLastWillProperties_create_copy(c->m_lastWillProperties) : NULL; }
const XMqttLastWillProperties* XMqttClient_lastWillProperties_const(const XMqttClient* c)
{ return c ? c->m_lastWillProperties : NULL; }
XMqttServerConnectionProperties* XMqttClient_serverConnectionProperties(const XMqttClient* c)
{ return c && c->m_serverConnectionProperties ? XMqttServerConnectionProperties_create_copy(c->m_serverConnectionProperties) : NULL; }
const XMqttServerConnectionProperties* XMqttClient_serverConnectionProperties_const(const XMqttClient* c)
{ return c ? c->m_serverConnectionProperties : NULL; }
void XMqttClient_authenticate(XMqttClient* c, const XMqttAuthenticationProperties* value)
{ XMqttProtocol_authenticate(c, value); }

static void mqtt_string_arg_delete(XVarList* list)
{ XVarList_args_1(list, XString*, value); if (value) XString_delete_base(value); }
static void mqtt_byte_array_arg_delete(XVarList* list)
{ XVarList_args_1(list, XByteArray*, value); if (value) XByteArray_delete_base(value); }
static void mqtt_message_args_delete(XVarList* list)
{
    XVarList_args_2(list, XByteArray*, payload, XMqttTopicName*, topic);
    if (payload) XByteArray_delete_base(payload);
    if (topic) XMqttTopicName_delete_base(topic);
}
static void mqtt_status_args_delete(XVarList* list)
{
    XMqttMessageStatusProperties* properties = NULL;
    if (list)
        memcpy(&properties, list->data + sizeof(int32_t) + sizeof(uint8_t),
               sizeof(properties));
    if (properties) XMqttMessageStatusProperties_delete_base(properties);
}
static void mqtt_auth_args_delete(XVarList* list)
{ XVarList_args_1(list, XMqttAuthenticationProperties*, value); if (value) XMqttAuthenticationProperties_delete_base(value); }

#define MQTT_SIMPLE_SIGNAL(object, function) \
    XEmitSignal(object, function, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL)

void* XMqttClient_connected_signal(XMqttClient* c) { MQTT_SIMPLE_SIGNAL(c, XMqttClient_connected_signal); }
void* XMqttClient_disconnected_signal(XMqttClient* c) { MQTT_SIMPLE_SIGNAL(c, XMqttClient_disconnected_signal); }
void* XMqttClient_pingResponseReceived_signal(XMqttClient* c) { MQTT_SIMPLE_SIGNAL(c, XMqttClient_pingResponseReceived_signal); }
void* XMqttClient_brokerSessionRestored_signal(XMqttClient* c) { MQTT_SIMPLE_SIGNAL(c, XMqttClient_brokerSessionRestored_signal); }

void* XMqttClient_messageReceived_signal(XMqttClient* c, const XByteArray* message,
                                         const XMqttTopicName* topic)
{
    if (!c) return (void*)(size_t)XMqttClient_messageReceived_signal;
    XByteArray* payload = message ? XByteArray_create_copy(message) : XByteArray_create();
    XMqttTopicName* topicCopy = topic ? XMqttTopicName_create_copy(topic) : NULL;
    XVarList* args = XVarList_Create(XVar(XByteArray*, payload), XVar(XMqttTopicName*, topicCopy));
    XObject_emitSignal((XObject*)c, (size_t)XMqttClient_messageReceived_signal,
                       args, mqtt_message_args_delete, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)(size_t)XMqttClient_messageReceived_signal;
}
void* XMqttClient_messageStatusChanged_signal(XMqttClient* c, int32_t id, uint8_t status,
                                              const XMqttMessageStatusProperties* properties)
{
    if (!c) return (void*)(size_t)XMqttClient_messageStatusChanged_signal;
    XMqttMessageStatusProperties* copy = properties ?
        XMqttMessageStatusProperties_create_copy(properties) : NULL;
    XVarList* args = XVarList_Create(XVar(int32_t, id), XVar(uint8_t, status),
                                    XVar(XMqttMessageStatusProperties*, copy));
    XObject_emitSignal((XObject*)c, (size_t)XMqttClient_messageStatusChanged_signal,
                       args, mqtt_status_args_delete, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)(size_t)XMqttClient_messageStatusChanged_signal;
}
void* XMqttClient_messageSent_signal(XMqttClient* c, int32_t id)
{ XEmitSignal(c, XMqttClient_messageSent_signal, XVarList_Create(XVar(int32_t, id)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }

static void* mqtt_emit_string_signal(XMqttClient* c, size_t signal, const XString* value)
{
    if (!c) return (void*)signal;
    XString* copy = value ? XString_create_copy(value) : XString_create_utf8("");
    XObject_emitSignal((XObject*)c, signal, XVarList_Create(XVar(XString*, copy)),
                       mqtt_string_arg_delete, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)signal;
}
void* XMqttClient_hostnameChanged_signal(XMqttClient* c, const XString* v)
{ return mqtt_emit_string_signal(c, (size_t)XMqttClient_hostnameChanged_signal, v); }
void* XMqttClient_clientIdChanged_signal(XMqttClient* c, const XString* v)
{ return mqtt_emit_string_signal(c, (size_t)XMqttClient_clientIdChanged_signal, v); }
void* XMqttClient_usernameChanged_signal(XMqttClient* c, const XString* v)
{ return mqtt_emit_string_signal(c, (size_t)XMqttClient_usernameChanged_signal, v); }
void* XMqttClient_passwordChanged_signal(XMqttClient* c, const XString* v)
{ return mqtt_emit_string_signal(c, (size_t)XMqttClient_passwordChanged_signal, v); }
void* XMqttClient_willTopicChanged_signal(XMqttClient* c, const XString* v)
{ return mqtt_emit_string_signal(c, (size_t)XMqttClient_willTopicChanged_signal, v); }
void* XMqttClient_portChanged_signal(XMqttClient* c, uint16_t v)
{ XEmitSignal(c, XMqttClient_portChanged_signal, XVarList_Create(XVar(uint16_t, v)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_keepAliveChanged_signal(XMqttClient* c, uint16_t v)
{ XEmitSignal(c, XMqttClient_keepAliveChanged_signal, XVarList_Create(XVar(uint16_t, v)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_protocolVersionChanged_signal(XMqttClient* c, XMqttClient_ProtocolVersion v)
{ int value = v; XEmitSignal(c, XMqttClient_protocolVersionChanged_signal, XVarList_Create(XVar(int, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_stateChanged_signal(XMqttClient* c, XMqttClient_State v)
{ int value = v; XEmitSignal(c, XMqttClient_stateChanged_signal, XVarList_Create(XVar(int, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_errorChanged_signal(XMqttClient* c, XMqttClient_Error v)
{ int value = v; XEmitSignal(c, XMqttClient_errorChanged_signal, XVarList_Create(XVar(int, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_cleanSessionChanged_signal(XMqttClient* c, bool v)
{ XEmitSignal(c, XMqttClient_cleanSessionChanged_signal, XVarList_Create(XVar(bool, v)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_willQoSChanged_signal(XMqttClient* c, uint8_t v)
{ XEmitSignal(c, XMqttClient_willQoSChanged_signal, XVarList_Create(XVar(uint8_t, v)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_willMessageChanged_signal(XMqttClient* c, const XByteArray* v)
{
    if (!c) return (void*)(size_t)XMqttClient_willMessageChanged_signal;
    XByteArray* copy = v ? XByteArray_create_copy(v) : XByteArray_create();
    XObject_emitSignal((XObject*)c, (size_t)XMqttClient_willMessageChanged_signal,
                       XVarList_Create(XVar(XByteArray*, copy)),
                       mqtt_byte_array_arg_delete, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)(size_t)XMqttClient_willMessageChanged_signal;
}
void* XMqttClient_willRetainChanged_signal(XMqttClient* c, bool v)
{ XEmitSignal(c, XMqttClient_willRetainChanged_signal, XVarList_Create(XVar(bool, v)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
void* XMqttClient_autoKeepAliveChanged_signal(XMqttClient* c, bool v)
{ XEmitSignal(c, XMqttClient_autoKeepAliveChanged_signal, XVarList_Create(XVar(bool, v)), NULL, NULL, XEVENT_PRIORITY_NORMAL); }
static void* mqtt_emit_auth_signal(XMqttClient* c, size_t signal,
                                   const XMqttAuthenticationProperties* value)
{
    if (!c) return (void*)signal;
    XMqttAuthenticationProperties* copy = value ?
        XMqttAuthenticationProperties_create_copy(value) : NULL;
    XObject_emitSignal((XObject*)c, signal,
        XVarList_Create(XVar(XMqttAuthenticationProperties*, copy)),
        mqtt_auth_args_delete, NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)signal;
}
void* XMqttClient_authenticationRequested_signal(XMqttClient* c,
                                                  const XMqttAuthenticationProperties* v)
{ return mqtt_emit_auth_signal(c, (size_t)XMqttClient_authenticationRequested_signal, v); }
void* XMqttClient_authenticationFinished_signal(XMqttClient* c,
                                                 const XMqttAuthenticationProperties* v)
{ return mqtt_emit_auth_signal(c, (size_t)XMqttClient_authenticationFinished_signal, v); }

#endif /* XMQTT_CLIENT_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
