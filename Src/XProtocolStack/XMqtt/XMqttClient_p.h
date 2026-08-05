/**
 * @file XMqttClient_p.h
 * @brief XMqttClient 的私有协议状态和内部协议操作。
 * @details
 * 该头文件只供 XMqttClient.c、XMqttProtocol.c 等实现文件使用，不属于
 * 对外公开 API。这里保存连接期间的 MQTT 收发缓存、请求跟踪表、QoS
 * 状态和 MQTT 5.0 主题别名；公开头文件只通过不完整类型声明引用本结构。
 */
#ifndef XMQTTCLIENT_P_H
#define XMQTTCLIENT_P_H

#include "XMqttClient.h"
#include "XAbstractSocket.h"
#include "XEvent.h"
#include "XVector.h"

/**
 * @brief 一条待处理的订阅/取消订阅请求。
 * @details
 * MQTT SUBSCRIBE 和 UNSUBSCRIBE 共用包标识符空间。wireTopic 是内部保存
 * 的过滤器副本，由条目负责释放；subscription 指向公开的订阅对象，不由
 * 条目单独释放。取消订阅等待服务端应答时使用 unsubscribePending 标记。
 */
typedef struct XMqttSubscriptionEntry {
    uint16_t identifier;                 ///< SUBSCRIBE/UNSUBSCRIBE 报文标识符，0 表示无效。
    XMqttTopicFilter* wireTopic;         ///< 发线上的主题过滤器副本，条目拥有其所有权。
    XMqttSubscription* subscription;    ///< 对应的公开订阅对象，借用指针。
    bool unsubscribePending;             ///< 是否已发送取消订阅并等待 UNSUBACK。
} XMqttSubscriptionEntry;

/**
 * @brief 一条等待服务端 QoS 应答的出站 PUBLISH。
 * @details
 * QoS 1/2 发布在收到 PUBACK、PUBREC、PUBREL 或 PUBCOMP 前保留该记录；
 * stage 是实现内部的握手阶段值，不应被公开 API 或外部代码解释。
 */
typedef struct XMqttPendingPublish {
    uint16_t identifier;                 ///< PUBLISH 报文标识符。
    uint8_t qos;                         ///< 发布时使用的 QoS 等级（1 或 2）。
    uint8_t stage;                       ///< 当前 QoS 握手阶段，取值由协议实现维护。
} XMqttPendingPublish;

/**
 * @brief MQTT 主题别名表中的一项。
 * @details
 * MQTT 5.0 允许用整数别名替代重复的主题名。topic 为拥有的字符串副本；
 * receiveAliases 和 publishAliases 分别对应入站与出站方向。
 */
typedef struct XMqttTopicAliasEntry {
    uint16_t alias;                      ///< MQTT 主题别名，0 不作为有效别名使用。
    XString* topic;                      ///< 与别名绑定的主题名，条目拥有其所有权。
} XMqttTopicAliasEntry;

/**
 * @brief XMqttClient 的连接期私有状态。
 * @warning 该结构不是公开 API，字段布局可随协议实现变化。
 */
struct XMqttClientPrivate {
    XByteArray* input;                   ///< 尚未组成完整 MQTT 报文的入站字节缓存。
    XVector* subscriptions;              ///< XMqttSubscriptionEntry 数组，跟踪订阅请求。
    XVector* pendingPublishes;           ///< XMqttPendingPublish 数组，跟踪 QoS 发布请求。
    XVector* incomingQos2;               ///< 已收到但尚未完成 PUBREL 的入站 QoS 2 标识符。
    XVector* receiveAliases;             ///< 服务端到客户端方向的 MQTT 5.0 主题别名表。
    XVector* publishAliases;             ///< 客户端到服务端方向的 MQTT 5.0 主题别名表。
    uint16_t nextPacketIdentifier;       ///< 下一个待分配的报文标识符，自动跳过 0 和占用值。
    XTimerId keepAliveTimer;             ///< 自动保活定时器 ID，无定时器时为 XTIMER_INVALID_ID。
    unsigned int pingTimeout;            ///< 已发出 PINGREQ 后的等待标志/超时状态。
    bool ownsTransport;                  ///< 是否由客户端负责释放内部创建的传输对象。
    bool encryptedRequested;             ///< 是否通过加密连接入口请求了 TLS 传输。
    bool connectPacketSent;              ///< 本次连接是否已经发送 CONNECT，避免重复发送。
    bool disconnectRequested;            ///< 是否已经请求主动断开并等待传输层结束。
    bool processingInput;                ///< 是否正在解析输入，防止 readyRead 重入解析器。
};

/**
 * @brief 创建并初始化客户端私有状态。
 * @return 成功返回私有状态对象；任一内部容器创建失败时返回 NULL。
 */
XMqttClientPrivate* XMqttClientPrivate_create(void);

/**
 * @brief 释放客户端私有状态及其拥有的容器、缓存和别名字符串。
 * @param priv 待释放的私有状态，可为 NULL。
 */
void XMqttClientPrivate_delete(XMqttClientPrivate* priv);

/**
 * @brief 重置一次 MQTT 连接的瞬时状态。
 * @param client 客户端实例。
 * @param clearSubscriptions 是否同时清空公开订阅跟踪项；重连策略可保留订阅对象。
 */
void XMqttClientPrivate_resetConnection(XMqttClient* client, bool clearSubscriptions);

/** @brief 编码并发送 CONNECT 报文。 */
bool XMqttProtocol_sendConnect(XMqttClient* client);

/**
 * @brief 编码并发送 SUBSCRIBE 报文，创建待确认的公开订阅对象。
 * @param client 客户端实例。
 * @param topic 主题过滤器，调用方保持其生命周期；协议层会保存线上副本。
 * @param properties MQTT 5.0 订阅属性，可为 NULL。
 * @param qos 请求的最大 QoS 等级。
 */
XMqttSubscription* XMqttProtocol_subscribe(XMqttClient* client,
                                           const XMqttTopicFilter* topic,
                                           const XMqttSubscriptionProperties* properties,
                                           uint8_t qos);

/**
 * @brief 编码并发送 UNSUBSCRIBE 报文。
 * @param client 客户端实例。
 * @param topic 要取消的主题过滤器。
 * @param properties MQTT 5.0 取消订阅属性，可为 NULL。
 * @return 报文成功写入传输设备返回 true。
 */
bool XMqttProtocol_unsubscribe(XMqttClient* client,
                              const XMqttTopicFilter* topic,
                              const XMqttUnsubscriptionProperties* properties);

/**
 * @brief 编码并发送 PUBLISH 报文。
 * @param client 客户端实例。
 * @param topic 主题名；MQTT 5.0 下可结合属性使用主题别名。
 * @param properties MQTT 5.0 发布属性，可为 NULL。
 * @param message 载荷地址，messageLen 为 0 时可为 NULL。
 * @param messageLen 载荷字节数。
 * @param qos 发布 QoS 等级。
 * @param retain 是否设置 RETAIN 标志。
 * @return 成功返回报文标识符；QoS 0 返回实现约定的无标识结果，失败返回负值。
 */
int32_t XMqttProtocol_publish(XMqttClient* client,
                             const XMqttTopicName* topic,
                             const XMqttPublishProperties* properties,
                             const uint8_t* message,
                             size_t messageLen,
                             uint8_t qos,
                             bool retain);

/**
 * @brief 发送 PINGREQ。
 * @param automatic true 表示由自动保活定时器触发，false 表示用户主动请求。
 */
bool XMqttProtocol_sendPing(XMqttClient* client, bool automatic);

/** @brief 编码并发送 DISCONNECT 报文。 */
bool XMqttProtocol_sendDisconnect(XMqttClient* client);

/**
 * @brief 发送 MQTT 5.0 AUTH 报文。
 * @param properties 认证属性；MQTT 3.x 连接调用时由协议层拒绝。
 */
bool XMqttProtocol_authenticate(XMqttClient* client,
                               const XMqttAuthenticationProperties* properties);

/**
 * @brief 向 MQTT 流式解析器追加传输层收到的字节。
 * @param data 输入缓冲区；size 为 0 时可为 NULL。
 * @param size 输入字节数。
 * @details 函数可处理半包和粘包，并在解析完成后派发客户端信号。
 */
void XMqttProtocol_feed(XMqttClient* client, const uint8_t* data, size_t size);

/**
 * @brief 记录协议错误并使当前连接进入失败路径。
 * @param error 对外报告的客户端错误码。
 */
void XMqttProtocol_fail(XMqttClient* client, XMqttClient_Error error);

/**
 * @brief 处理客户端对象的定时器事件。
 * @details 负责 MQTT 自动保活、PINGRESP 超时和相关连接状态维护。
 */
void XMqttProtocol_timerEvent(XMqttClient* client, XTimerEvent* event);

#endif
