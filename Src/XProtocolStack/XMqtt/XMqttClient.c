#include "XMqttClient.h"
#include "XMemory.h"
#include <string.h>

// ==================== 虚函数重载声明 ====================
static void V_deinit(XMqttClient* client);
static void V_connectToHost(XMqttClient* client);
static void V_disconnectFromHost(XMqttClient* client);
static XMqttSubscription* V_subscribe(XMqttClient* client, const XMqttTopicFilter* topic, uint8_t qos);
static void V_unsubscribe(XMqttClient* client, const XMqttTopicFilter* topic);
static int32_t V_publish(XMqttClient* client, const XMqttTopicName* topic,
                          const uint8_t* message, size_t messageLen,
                          uint8_t qos, bool retain);
static bool V_requestPing(XMqttClient* client);

// ==================== 类初始化 ====================
XVtable* XMqttClient_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XMqttClient)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);

    void* table[] = {
        V_connectToHost,
        V_disconnectFromHost,
        V_subscribe,
        V_unsubscribe,
        V_publish,
        V_requestPing
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, V_deinit);

    return XVTABLE_DEFAULT;
}

// ==================== 创建/初始化 ====================
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
    memset(client, 0, sizeof(XMqttClient));
    XObject_init((XObject*)client);
    XClassGetVtable(client) = XMqttClient_class_init();

    // 默认值
    client->m_port = 1883;
    client->m_keepAlive = 60;
    client->m_protocolVersion = XMqttClient_MQTT_3_1_1;
    client->m_state = XMqttClient_Disconnected;
    client->m_error = XMqttClient_NoError;
    client->m_cleanSession = true;
    client->m_autoKeepAlive = true;
    client->m_transportType = XMqttClient_AbstractSocket;
}

// ==================== 析构 ====================
static void V_deinit(XMqttClient* client)
{
    if (!client) return;

    if (client->m_hostname) { XString_delete_base(client->m_hostname); client->m_hostname = NULL; }
    if (client->m_clientId) { XString_delete_base(client->m_clientId); client->m_clientId = NULL; }
    if (client->m_username) { XString_delete_base(client->m_username); client->m_username = NULL; }
    if (client->m_password) { XString_delete_base(client->m_password); client->m_password = NULL; }
    if (client->m_willTopic) { XString_delete_base(client->m_willTopic); client->m_willTopic = NULL; }
    if (client->m_willMessage) { XByteArray_delete_base(client->m_willMessage); client->m_willMessage = NULL; }
    if (client->m_connectionProperties) { XMqttConnectionProperties_delete_base(client->m_connectionProperties); client->m_connectionProperties = NULL; }
    if (client->m_lastWillProperties) { XMqttLastWillProperties_delete_base(client->m_lastWillProperties); client->m_lastWillProperties = NULL; }
    if (client->m_serverConnectionProperties) { XMqttServerConnectionProperties_delete_base(client->m_serverConnectionProperties); client->m_serverConnectionProperties = NULL; }

    XClass_Deinit_Parent(XObject, client);
}

// ==================== 传输设置 ====================
void XMqttClient_setTransport(XMqttClient* client, void* device, XMqttClient_TransportType transport)
{
    if (!client) return;
    client->m_transport = device;
    client->m_transportType = transport;
}

void* XMqttClient_transport(const XMqttClient* client)
{
    return client ? client->m_transport : NULL;
}

// ==================== 订阅/取消订阅 ====================
XMqttSubscription* XMqttClient_subscribe(XMqttClient* client, const XMqttTopicFilter* topic, uint8_t qos)
{
    if (!client) return NULL;
    XMqttSubscription* (*func)(XMqttClient*, const XMqttTopicFilter*, uint8_t) =
        XClassGetVirtualFunc(client, EXMqttClient_Subscribe, XMqttSubscription* (*)(XMqttClient*, const XMqttTopicFilter*, uint8_t));
    if (func) return func(client, topic, qos);
    return NULL;
}

XMqttSubscription* XMqttClient_subscribe_with_properties(XMqttClient* client, const XMqttTopicFilter* topic,
                                                          const XMqttSubscriptionProperties* properties, uint8_t qos)
{
    (void)properties;
    // 默认实现忽略属性，由子类重写
    return XMqttClient_subscribe(client, topic, qos);
}

void XMqttClient_unsubscribe(XMqttClient* client, const XMqttTopicFilter* topic)
{
    if (!client) return;
    void (*func)(XMqttClient*, const XMqttTopicFilter*) =
        XClassGetVirtualFunc(client, EXMqttClient_Unsubscribe, void(*)(XMqttClient*, const XMqttTopicFilter*));
    if (func) func(client, topic);
}

void XMqttClient_unsubscribe_with_properties(XMqttClient* client, const XMqttTopicFilter* topic,
                                              const XMqttUnsubscriptionProperties* properties)
{
    (void)properties;
    XMqttClient_unsubscribe(client, topic);
}

// ==================== 发布 ====================
int32_t XMqttClient_publish(XMqttClient* client, const XMqttTopicName* topic,
                             const uint8_t* message, size_t messageLen,
                             uint8_t qos, bool retain)
{
    if (!client) return -1;
    int32_t (*func)(XMqttClient*, const XMqttTopicName*, const uint8_t*, size_t, uint8_t, bool) =
        XClassGetVirtualFunc(client, EXMqttClient_Publish, int32_t(*)(XMqttClient*, const XMqttTopicName*, const uint8_t*, size_t, uint8_t, bool));
    if (func) return func(client, topic, message, messageLen, qos, retain);
    return -1;
}

int32_t XMqttClient_publish_with_properties(XMqttClient* client, const XMqttTopicName* topic,
                                             const XMqttPublishProperties* properties,
                                             const uint8_t* message, size_t messageLen,
                                             uint8_t qos, bool retain)
{
    (void)properties;
    return XMqttClient_publish(client, topic, message, messageLen, qos, retain);
}

// ==================== Ping ====================
bool XMqttClient_requestPing(XMqttClient* client)
{
    if (!client) return false;
    bool (*func)(XMqttClient*) =
        XClassGetVirtualFunc(client, EXMqttClient_RequestPing, bool(*)(XMqttClient*));
    if (func) return func(client);
    return false;
}

// ==================== 连接/断开 ====================
void XMqttClient_connectToHost_base(XMqttClient* client)
{
    if (!client) return;
    void (*func)(XMqttClient*) =
        XClassGetVirtualFunc(client, EXMqttClient_ConnectToHost, void(*)(XMqttClient*));
    if (func) func(client);
}

/**
 * @brief 加密连接 Broker（对齐 Qt 6.8 QMqttClient::connectToHostEncrypted）
 * @param client 客户端实例指针（非 NULL）
 * @param sslConfig SSL 配置指针（非 NULL，指向 XSslConfiguration）
 * @details 使用 SSL/TLS 加密连接到 Broker。
 *          当前 stub 阶段设置传输类型为 SecureSocket 后调用 connectToHost_base。
 *          对齐 Qt 6.8: QMqttClient::connectToHostEncrypted(const QSslConfiguration &conf)
 */
void XMqttClient_connectToHostEncrypted(XMqttClient* client, const XSslConfiguration* sslConfig)
{
    if (!client) return;
    (void)sslConfig; /* 当前 stub 阶段忽略 SSL 配置，后续协议实现使用 */
    /* 对齐 Qt 6.8: 设置传输类型为 SecureSocket 后调用 connectToHost_base */
    XMqttClient_setTransport(client, NULL, XMqttClient_SecureSocket);
    XMqttClient_connectToHost_base(client);
}


void XMqttClient_disconnectFromHost_base(XMqttClient* client)
{
    if (!client) return;
    void (*func)(XMqttClient*) =
        XClassGetVirtualFunc(client, EXMqttClient_DisconnectFromHost, void(*)(XMqttClient*));
    if (func) func(client);
}

// ==================== 默认虚函数实现 ====================
static void V_connectToHost(XMqttClient* client)
{
    if (!client) return;
    client->m_state = XMqttClient_Connecting;
    XMqttClient_stateChanged_signal(client, XMqttClient_Connecting);
    // 基类不实现实际连接，由子类重写
}

static void V_disconnectFromHost(XMqttClient* client)
{
    if (!client) return;
    client->m_state = XMqttClient_Disconnected;
    XMqttClient_stateChanged_signal(client, XMqttClient_Disconnected);
    XMqttClient_disconnected_signal(client);
}

static XMqttSubscription* V_subscribe(XMqttClient* client, const XMqttTopicFilter* topic, uint8_t qos)
{
    /* 未连接时返回 NULL，对齐 Qt 6.8 QMqttClient::subscribe 行为 */
    if (!client || !topic) return NULL;
    if (client->m_state != XMqttClient_Connected) return NULL;
    XMqttSubscription* sub = XMqttSubscription_create(topic, qos);
    if (sub) {
        XMqttSubscription_setClient(sub, client);
        XMqttSubscription_setState(sub, XMqttSubscription_Subscribed);
    }
    return sub;
}

static void V_unsubscribe(XMqttClient* client, const XMqttTopicFilter* topic)
{
    (void)client; (void)topic;
    // 基类默认不实现
}

static int32_t V_publish(XMqttClient* client, const XMqttTopicName* topic,
                          const uint8_t* message, size_t messageLen,
                          uint8_t qos, bool retain)
{
    (void)client; (void)topic; (void)message; (void)messageLen; (void)qos; (void)retain;
    return -1; // 基类不实现
}

static bool V_requestPing(XMqttClient* client)
{
    (void)client;
    return false; // 基类不实现
}

// ==================== Getters ====================
const XString* XMqttClient_hostname_const(const XMqttClient* client) { return client ? client->m_hostname : NULL; }
XString* XMqttClient_hostname(const XMqttClient* client) { if (!client || !client->m_hostname) return NULL; return XString_create_copy(client->m_hostname); }
uint16_t XMqttClient_port(const XMqttClient* client) { return client ? client->m_port : 0; }
const XString* XMqttClient_clientId_const(const XMqttClient* client) { return client ? client->m_clientId : NULL; }
XString* XMqttClient_clientId(const XMqttClient* client) { if (!client || !client->m_clientId) return NULL; return XString_create_copy(client->m_clientId); }
uint16_t XMqttClient_keepAlive(const XMqttClient* client) { return client ? client->m_keepAlive : 0; }
uint8_t XMqttClient_protocolVersion(const XMqttClient* client) { return client ? client->m_protocolVersion : 0; }
uint8_t XMqttClient_state(const XMqttClient* client) { return client ? client->m_state : XMqttClient_Disconnected; }
uint8_t XMqttClient_error(const XMqttClient* client) { return client ? client->m_error : XMqttClient_UnknownError; }
const XString* XMqttClient_username_const(const XMqttClient* client) { return client ? client->m_username : NULL; }
XString* XMqttClient_username(const XMqttClient* client) { if (!client || !client->m_username) return NULL; return XString_create_copy(client->m_username); }
const XString* XMqttClient_password_const(const XMqttClient* client) { return client ? client->m_password : NULL; }
XString* XMqttClient_password(const XMqttClient* client) { if (!client || !client->m_password) return NULL; return XString_create_copy(client->m_password); }
bool XMqttClient_cleanSession(const XMqttClient* client) { return client ? client->m_cleanSession : true; }
const XString* XMqttClient_willTopic_const(const XMqttClient* client) { return client ? client->m_willTopic : NULL; }
XString* XMqttClient_willTopic(const XMqttClient* client) { if (!client || !client->m_willTopic) return NULL; return XString_create_copy(client->m_willTopic); }
uint8_t XMqttClient_willQoS(const XMqttClient* client) { return client ? client->m_willQoS : 0; }
const XByteArray* XMqttClient_willMessage_const(const XMqttClient* client) { return client ? client->m_willMessage : NULL; }
XByteArray* XMqttClient_willMessage(const XMqttClient* client) { if (!client || !client->m_willMessage) return NULL; return XByteArray_create_copy(client->m_willMessage); }
bool XMqttClient_willRetain(const XMqttClient* client) { return client ? client->m_willRetain : false; }
bool XMqttClient_autoKeepAlive(const XMqttClient* client) { return client ? client->m_autoKeepAlive : true; }

// ==================== Setters ====================
void XMqttClient_setHostname(XMqttClient* client, const char* hostname)
{
    if (!client) return;
    if (client->m_hostname) { XString_delete_base(client->m_hostname); }
    client->m_hostname = hostname ? XString_create_utf8(hostname) : NULL;
    XMqttClient_hostnameChanged_signal(client, client->m_hostname);
}

void XMqttClient_setPort(XMqttClient* client, uint16_t port)
{
    if (!client || client->m_port == port) return;
    client->m_port = port;
    XMqttClient_portChanged_signal(client, port);
}

void XMqttClient_setClientId(XMqttClient* client, const char* clientId)
{
    if (!client) return;
    if (client->m_clientId) { XString_delete_base(client->m_clientId); }
    client->m_clientId = clientId ? XString_create_utf8(clientId) : NULL;
    XMqttClient_clientIdChanged_signal(client, client->m_clientId);
}

void XMqttClient_setKeepAlive(XMqttClient* client, uint16_t keepAlive)
{
    if (!client || client->m_keepAlive == keepAlive) return;
    client->m_keepAlive = keepAlive;
    XMqttClient_keepAliveChanged_signal(client, keepAlive);
}

void XMqttClient_setProtocolVersion(XMqttClient* client, uint8_t protocolVersion)
{
    if (!client || client->m_protocolVersion == protocolVersion) return;
    if (protocolVersion < 3 || protocolVersion > 5) return;
    client->m_protocolVersion = protocolVersion;
    XMqttClient_protocolVersionChanged_signal(client, protocolVersion);
}

void XMqttClient_setState(XMqttClient* client, uint8_t state)
{
    if (!client || client->m_state == state) return;
    client->m_state = state;
    XMqttClient_stateChanged_signal(client, state);
    if (state == XMqttClient_Disconnected)
        XMqttClient_disconnected_signal(client);
    else if (state == XMqttClient_Connected)
        XMqttClient_connected_signal(client);
}

void XMqttClient_setError(XMqttClient* client, uint8_t error)
{
    if (!client || client->m_error == error) return;
    client->m_error = error;
    XMqttClient_errorChanged_signal(client, error);
}

void XMqttClient_setUsername(XMqttClient* client, const char* username)
{
    if (!client) return;
    if (client->m_username) { XString_delete_base(client->m_username); }
    client->m_username = username ? XString_create_utf8(username) : NULL;
    XMqttClient_usernameChanged_signal(client, client->m_username);
}

void XMqttClient_setPassword(XMqttClient* client, const char* password)
{
    if (!client) return;
    if (client->m_password) { XString_delete_base(client->m_password); }
    client->m_password = password ? XString_create_utf8(password) : NULL;
    XMqttClient_passwordChanged_signal(client, client->m_password);
}

void XMqttClient_setCleanSession(XMqttClient* client, bool cleanSession)
{
    if (!client || client->m_cleanSession == cleanSession) return;
    client->m_cleanSession = cleanSession;
    XMqttClient_cleanSessionChanged_signal(client, cleanSession);
}

void XMqttClient_setWillTopic(XMqttClient* client, const char* willTopic)
{
    if (!client) return;
    if (client->m_willTopic) { XString_delete_base(client->m_willTopic); }
    client->m_willTopic = willTopic ? XString_create_utf8(willTopic) : NULL;
    XMqttClient_willTopicChanged_signal(client, client->m_willTopic);
}

void XMqttClient_setWillQoS(XMqttClient* client, uint8_t willQoS)
{
    if (!client || client->m_willQoS == willQoS) return;
    client->m_willQoS = willQoS;
    XMqttClient_willQoSChanged_signal(client, willQoS);
}

void XMqttClient_setWillMessage(XMqttClient* client, const uint8_t* data, size_t len)
{
    if (!client) return;
    if (client->m_willMessage) { XByteArray_delete_base(client->m_willMessage); }
    client->m_willMessage = (data && len) ? XByteArray_create_with_data((const char*)data, len) : NULL;
    XMqttClient_willMessageChanged_signal(client, client->m_willMessage);
}

void XMqttClient_setWillRetain(XMqttClient* client, bool willRetain)
{
    if (!client || client->m_willRetain == willRetain) return;
    client->m_willRetain = willRetain;
    XMqttClient_willRetainChanged_signal(client, willRetain);
}

void XMqttClient_setAutoKeepAlive(XMqttClient* client, bool autoKeepAlive)
{
    if (!client || client->m_autoKeepAlive == autoKeepAlive) return;
    client->m_autoKeepAlive = autoKeepAlive;
    XMqttClient_autoKeepAliveChanged_signal(client, autoKeepAlive);
}

// ==================== MQTT 5.0 属性 ====================
void XMqttClient_setConnectionProperties(XMqttClient* client, const XMqttConnectionProperties* prop)
{
    if (!client) return;
    if (client->m_connectionProperties) { XMqttConnectionProperties_delete_base(client->m_connectionProperties); }
    client->m_connectionProperties = prop ? XMqttConnectionProperties_create_copy(prop) : NULL;
}

XMqttConnectionProperties* XMqttClient_connectionProperties(const XMqttClient* client)
{
    if (!client || !client->m_connectionProperties) return NULL;
    return XMqttConnectionProperties_create_copy(client->m_connectionProperties);
}

const XMqttConnectionProperties* XMqttClient_connectionProperties_const(const XMqttClient* client)
{
    return client ? client->m_connectionProperties : NULL;
}

void XMqttClient_setLastWillProperties(XMqttClient* client, const XMqttLastWillProperties* prop)
{
    if (!client) return;
    if (client->m_lastWillProperties) { XMqttLastWillProperties_delete_base(client->m_lastWillProperties); }
    client->m_lastWillProperties = prop ? XMqttLastWillProperties_create_copy(prop) : NULL;
}

XMqttLastWillProperties* XMqttClient_lastWillProperties(const XMqttClient* client)
{
    if (!client || !client->m_lastWillProperties) return NULL;
    return XMqttLastWillProperties_create_copy(client->m_lastWillProperties);
}

const XMqttLastWillProperties* XMqttClient_lastWillProperties_const(const XMqttClient* client)
{
    return client ? client->m_lastWillProperties : NULL;
}

XMqttServerConnectionProperties* XMqttClient_serverConnectionProperties(const XMqttClient* client)
{
    if (!client || !client->m_serverConnectionProperties) return NULL;
    return XMqttServerConnectionProperties_create_copy(client->m_serverConnectionProperties);
}

const XMqttServerConnectionProperties* XMqttClient_serverConnectionProperties_const(const XMqttClient* client)
{
    return client ? client->m_serverConnectionProperties : NULL;
}

void XMqttClient_authenticate(XMqttClient* client, const XMqttAuthenticationProperties* prop)
{
    (void)client; (void)prop;
    // 基类不实现，由子类重写
}

// ==================== 信号 ====================
void* XMqttClient_connected_signal(XMqttClient* client) {
    XEmitSignal(client, XMqttClient_connected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_disconnected_signal(XMqttClient* client) {
    XEmitSignal(client, XMqttClient_disconnected_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_messageReceived_signal(XMqttClient* client, const XByteArray* message, const XMqttTopicName* topic) {
    XEmitSignal(client, XMqttClient_messageReceived_signal,
        XVarList_Create(XVar(const XByteArray*, message), XVar(const XMqttTopicName*, topic)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_messageStatusChanged_signal(XMqttClient* client, int32_t id, uint8_t s, const XMqttMessageStatusProperties* properties) {
    XEmitSignal(client, XMqttClient_messageStatusChanged_signal,
        XVarList_Create(XVar(int32_t, id), XVar(uint8_t, s), XVar(const XMqttMessageStatusProperties*, properties)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_messageSent_signal(XMqttClient* client, int32_t id) {
    XEmitSignal(client, XMqttClient_messageSent_signal,
        XVarList_Create(XVar(int32_t, id)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_pingResponseReceived_signal(XMqttClient* client) {
    XEmitSignal(client, XMqttClient_pingResponseReceived_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_brokerSessionRestored_signal(XMqttClient* client) {
    XEmitSignal(client, XMqttClient_brokerSessionRestored_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_hostnameChanged_signal(XMqttClient* client, const XString* hostname) {
    XEmitSignal(client, XMqttClient_hostnameChanged_signal,
        XVarList_Create(XVar(const XString*, hostname)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_portChanged_signal(XMqttClient* client, uint16_t port) {
    XEmitSignal(client, XMqttClient_portChanged_signal,
        XVarList_Create(XVar(uint16_t, port)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_clientIdChanged_signal(XMqttClient* client, const XString* clientId) {
    XEmitSignal(client, XMqttClient_clientIdChanged_signal,
        XVarList_Create(XVar(const XString*, clientId)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_keepAliveChanged_signal(XMqttClient* client, uint16_t keepAlive) {
    XEmitSignal(client, XMqttClient_keepAliveChanged_signal,
        XVarList_Create(XVar(uint16_t, keepAlive)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_protocolVersionChanged_signal(XMqttClient* client, uint8_t protocolVersion) {
    XEmitSignal(client, XMqttClient_protocolVersionChanged_signal,
        XVarList_Create(XVar(uint8_t, protocolVersion)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_stateChanged_signal(XMqttClient* client, uint8_t state) {
    XEmitSignal(client, XMqttClient_stateChanged_signal,
        XVarList_Create(XVar(uint8_t, state)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_errorChanged_signal(XMqttClient* client, uint8_t error) {
    XEmitSignal(client, XMqttClient_errorChanged_signal,
        XVarList_Create(XVar(uint8_t, error)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_usernameChanged_signal(XMqttClient* client, const XString* username) {
    XEmitSignal(client, XMqttClient_usernameChanged_signal,
        XVarList_Create(XVar(const XString*, username)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_passwordChanged_signal(XMqttClient* client, const XString* password) {
    XEmitSignal(client, XMqttClient_passwordChanged_signal,
        XVarList_Create(XVar(const XString*, password)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_cleanSessionChanged_signal(XMqttClient* client, bool cleanSession) {
    XEmitSignal(client, XMqttClient_cleanSessionChanged_signal,
        XVarList_Create(XVar(bool, cleanSession)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_willTopicChanged_signal(XMqttClient* client, const XString* willTopic) {
    XEmitSignal(client, XMqttClient_willTopicChanged_signal,
        XVarList_Create(XVar(const XString*, willTopic)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_willQoSChanged_signal(XMqttClient* client, uint8_t willQoS) {
    XEmitSignal(client, XMqttClient_willQoSChanged_signal,
        XVarList_Create(XVar(uint8_t, willQoS)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_willMessageChanged_signal(XMqttClient* client, const XByteArray* willMessage) {
    XEmitSignal(client, XMqttClient_willMessageChanged_signal,
        XVarList_Create(XVar(const XByteArray*, willMessage)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_willRetainChanged_signal(XMqttClient* client, bool willRetain) {
    XEmitSignal(client, XMqttClient_willRetainChanged_signal,
        XVarList_Create(XVar(bool, willRetain)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_autoKeepAliveChanged_signal(XMqttClient* client, bool autoKeepAlive) {
    XEmitSignal(client, XMqttClient_autoKeepAliveChanged_signal,
        XVarList_Create(XVar(bool, autoKeepAlive)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_authenticationRequested_signal(XMqttClient* client, const XMqttAuthenticationProperties* p) {
    XEmitSignal(client, XMqttClient_authenticationRequested_signal,
        XVarList_Create(XVar(const XMqttAuthenticationProperties*, p)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttClient_authenticationFinished_signal(XMqttClient* client, const XMqttAuthenticationProperties* p) {
    XEmitSignal(client, XMqttClient_authenticationFinished_signal,
        XVarList_Create(XVar(const XMqttAuthenticationProperties*, p)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
