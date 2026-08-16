#ifndef XMQTTSERVER_H
#define XMQTTSERVER_H

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XString.h"
#include "XByteArray.h"
#include "XMqttGlobal.h"
#include "XMqttTopicName.h"
#include "XMqttTopicFilter.h"
#include "XMqttMessage.h"
#include "XMqttPublishProperties.h"
#include "XMqttConnectionProperties.h"
#include "XMqttSubscriptionProperties.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttServer.h
 * @brief MQTT 服务器（Broker）基类。
 * @details 传输无关的 MQTT Broker 核心引擎，继承自 XObject。支持 MQTT 3.1、
 *          3.1.1 与 5.0 协议版本，提供完整的服务器端功能：
 *          - CONNECT/CONNACK 握手与认证回调；
 *          - PUBLISH QoS 0/1/2 双向投递与握手状态机；
 *          - SUBSCRIBE/UNSUBSCRIBE（含通配符 +/#、共享订阅 $share/...）；
 *          - 保留消息、遗嘱消息、保活超时检测；
 *          - 持久会话（cleanSession=0）与离线 QoS1/2 消息；
 *          - MQTT 5.0 属性（主题别名、订阅标识符、会话过期、用户属性等）。
 *
 *          本类不包含任何平台或网络 API：传输字节通过虚函数
 *          XMqttServer_sendData_base / XMqttServer_closeClient_base 交给子类
 *          处理。XTcpServer 子类见 XMqttTcpServer.h。
 */

/**
 * @brief MQTT 服务器认证回调。
 * @details 在 CONNECT 处理阶段被调用，用于决定是否接受连接。返回 0 表示接受；
 *          返回非 0 时按 MQTT 5.0 CONNACK 原因码向客户端报告失败
 *          （如 XMqtt_ReasonCode_BadUserNameOrPassword、XMqtt_ReasonCode_NotAuthorized）。
 * @param context 安装回调时传入的用户上下文，可为 NULL。
 * @param username 客户端提供的用户名（UTF-8），未提供时为 NULL。
 * @param password 客户端提供的密码副本（以 '\0' 结尾），未提供时为 NULL。
 * @return 0 接受连接；否则返回 CONNACK 原因码。
 * @note password 是本库创建的临时安全副本，回调返回后立即释放，禁止保存指针。
 */
typedef uint8_t (*XMqttServer_Authenticator)(void* context, const char* username, const char* password);

/** @brief XMqttServer 内部私有状态（不完整类型，仅供内部实现使用）。 */
typedef struct XMqttServerPrivate XMqttServerPrivate;

// ==================== 虚函数表定义 ====================
XCLASS_DEFINE_BEGING(XMqttServer)
XCLASS_DEFINE_ENUM(XMqttServer, SendData) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XMqttServer, CloseClient),
XCLASS_DEFINE_END(XMqttServer)

/**
 * @brief MQTT 服务器结构体。
 * @details 继承自 XObject。公开成员为服务器级配置；连接与会话等运行时状态
 *          保存在 m_private 中，外部不得直接访问。
 */
typedef struct XMqttServer {
    XObject m_class;                       ///< 继承自 XObject，第一个成员，由 XClass 管理，禁止手工修改。
    uint32_t m_maximumPacketSize;          ///< 服务器允许的最大报文大小（字节），默认 268435455。
    uint16_t m_topicAliasMaximum;          ///< 服务器支持的主题别名上限，默认 0（不支持别名）。
    uint16_t m_serverKeepAlive;            ///< 服务器保活间隔（秒），0 表示沿用客户端保活值。
    /* 紧凑标志位（位域优化：maximumQoS 2 bit + 4 个可用性开关各 1 bit） */
    uint32_t m_maximumQoS : 2;             ///< 服务器支持的最大 QoS（0/1/2），默认 2。
    uint32_t m_retainAvailable : 1;        ///< 是否支持保留消息，默认 true。
    uint32_t m_wildcardAvailable : 1;      ///< 是否支持主题通配符订阅，默认 true。
    uint32_t m_subscriptionIdAvailable : 1; ///< 是否支持 MQTT 5.0 订阅标识符，默认 true。
    uint32_t m_sharedAvailable : 1;        ///< 是否支持共享订阅，默认 true。
    XMqttServerPrivate* m_private;         ///< 内部协议状态（仅供实现使用，外部不得访问）。
} XMqttServer;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化 XMqttServer 的虚函数表。
 * @return 初始化完成的虚函数表指针，失败返回 NULL。
 */
XVtable* XMqttServer_class_init(void);

/**
 * @brief 在堆上创建并初始化 XMqttServer 实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）。
 * @return 新创建的实例指针，失败返回 NULL；调用者负责调用 XMqttServer_delete_base 释放。
 */
XMqttServer* XMqttServer_create_ex(XMemoryType memory);

/**
 * @brief 初始化已分配的 XMqttServer 实例。
 * @param server 待初始化的实例指针（非 NULL）。
 * @note 栈上使用必须与 XMqttServer_deinit_base 成对调用。
 */
void XMqttServer_init(XMqttServer* server);

/******************************************************************************************
 * 连接管理（传输无关）
 ******************************************************************************************/

/**
 * @brief 登记一条新的客户端连接。
 * @details 子类（或测试桩）在传输层建立新连接后调用。本函数创建内部连接状态
 *          并等待客户端发送 CONNECT 报文；在收到有效 CONNECT 之前不会发送任何字节。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 传输设备指针（如 XTcpSocket*），仅借用，服务器不负责释放。
 * @return 登记成功返回 true；transport 为 NULL 或内部资源不足返回 false。
 */
bool XMqttServer_beginClient(XMqttServer* server, void* transport);

/**
 * @brief 结束一条客户端连接。
 * @details 传输层断开（或调用方主动关闭）时调用。若客户端未发送 DISCONNECT
 *          报文（异常断开），本函数会触发遗嘱消息发布；持久会话按 cleanSession
 *          与会话过期设置决定去留。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 要结束的连接的传输设备指针。
 * @note 本函数不调用 XMqttServer_closeClient_base，子类应在传输层自行关闭；
 *       transport 对应的内部状态会被释放，之后不得再对同一 transport 调用 feedData。
 */
void XMqttServer_endClient(XMqttServer* server, void* transport);

/**
 * @brief 向服务器喂入传输层收到的原始字节。
 * @details 本函数可处理半包与粘包：内部缓存不完整的报文，直到凑齐完整 MQTT
 *          报文后才解析并派发。收到任何报文都会刷新保活计时。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 数据来源的传输设备指针。
 * @param data 收到的字节缓冲区，size 为 0 时可为 NULL。
 * @param size 缓冲区字节数。
 */
void XMqttServer_feedData(XMqttServer* server, void* transport,
                          const uint8_t* data, size_t size);

/******************************************************************************************
 * 服务端主动发布
 ******************************************************************************************/

/**
 * @brief 服务器主动向全部匹配订阅者发布一条消息。
 * @details 消息按主题过滤器的匹配规则派发给所有在线订阅者；QoS 取发布 QoS 与
 *          订阅 QoS 的较小值。retain 为 true 时同时更新保留消息表。
 * @param server 服务器实例指针（非 NULL）。
 * @param topic 主题名（UTF-8，非 NULL，不可含通配符）。
 * @param payload 消息载荷，payloadLen 为 0 时可为 NULL。
 * @param payloadLen 消息载荷字节数。
 * @param qos 发布 QoS（0/1/2），大于 2 时按 0 处理。
 * @param retain 是否保留消息。
 * @return 成功（至少完成路由）返回 true；参数非法返回 false。
 */
bool XMqttServer_publish(XMqttServer* server, const char* topic,
                         const uint8_t* payload, size_t payloadLen,
                         uint8_t qos, bool retain);

/**
 * @brief 服务器主动发布消息（带 MQTT 5.0 属性）。
 * @details 行为同 XMqttServer_publish，额外携带发布属性（如消息过期时间、
 *          用户属性、载荷格式等）。属性仅对 MQTT 5.0 订阅者下发。
 * @param server 服务器实例指针（非 NULL）。
 * @param topic 主题名（UTF-8，非 NULL）。
 * @param properties MQTT 5.0 发布属性，可为 NULL。
 * @param payload 消息载荷，payloadLen 为 0 时可为 NULL。
 * @param payloadLen 消息载荷字节数。
 * @param qos 发布 QoS（0/1/2）。
 * @param retain 是否保留消息。
 * @return 成功返回 true；参数非法返回 false。
 */
bool XMqttServer_publishWithProperties(XMqttServer* server, const char* topic,
                                       const XMqttPublishProperties* properties,
                                       const uint8_t* payload, size_t payloadLen,
                                       uint8_t qos, bool retain);

/******************************************************************************************
 * 认证配置
 ******************************************************************************************/

/**
 * @brief 安装连接认证回调。
 * @param server 服务器实例指针（非 NULL）。
 * @param context 回调上下文（借用），可为 NULL。
 * @param authenticator 认证回调，传入 NULL 表示不启用认证。
 */
void XMqttServer_setAuthenticator(XMqttServer* server, void* context,
                                  XMqttServer_Authenticator authenticator);

/******************************************************************************************
 * 配置接口
 ******************************************************************************************/

/**
 * @brief 设置服务器最大报文大小。
 * @param server 服务器实例指针（非 NULL）。
 * @param size 最大报文大小（字节），0 表示使用协议默认上限 268435455。
 */
void XMqttServer_setMaximumPacketSize(XMqttServer* server, uint32_t size);
/**
 * @brief 获取服务器最大报文大小。
 * @param server 服务器实例指针。
 * @return 最大报文大小（字节），server 为 NULL 时返回 0。
 */
uint32_t XMqttServer_maximumPacketSize(const XMqttServer* server);

/**
 * @brief 设置服务器主题别名上限。
 * @param server 服务器实例指针（非 NULL）。
 * @param maximum 主题别名上限（0..65535），0 表示不支持主题别名。
 */
void XMqttServer_setTopicAliasMaximum(XMqttServer* server, uint16_t maximum);
/**
 * @brief 获取服务器主题别名上限。
 * @param server 服务器实例指针。
 * @return 主题别名上限，server 为 NULL 时返回 0。
 */
uint16_t XMqttServer_topicAliasMaximum(const XMqttServer* server);

/**
 * @brief 设置服务器保活间隔。
 * @param server 服务器实例指针（非 NULL）。
 * @param seconds 保活间隔（秒），0 表示沿用客户端 CONNECT 中的保活值。
 */
void XMqttServer_setServerKeepAlive(XMqttServer* server, uint16_t seconds);
/**
 * @brief 获取服务器保活间隔。
 * @param server 服务器实例指针。
 * @return 服务器保活间隔（秒），server 为 NULL 时返回 0。
 */
uint16_t XMqttServer_serverKeepAlive(const XMqttServer* server);

/**
 * @brief 设置服务器支持的最大 QoS。
 * @param server 服务器实例指针（非 NULL）。
 * @param qos 最大 QoS（0/1/2）。
 */
void XMqttServer_setMaximumQoS(XMqttServer* server, uint8_t qos);
/**
 * @brief 获取服务器支持的最大 QoS。
 * @param server 服务器实例指针。
 * @return 最大 QoS，server 为 NULL 时返回 0。
 */
uint8_t XMqttServer_maximumQoS(const XMqttServer* server);

/**
 * @brief 设置是否支持保留消息。
 * @param server 服务器实例指针（非 NULL）。
 * @param available 是否支持。
 */
void XMqttServer_setRetainAvailable(XMqttServer* server, bool available);
/**
 * @brief 获取是否支持保留消息。
 * @param server 服务器实例指针。
 * @return 是否支持，server 为 NULL 时返回 false。
 */
bool XMqttServer_retainAvailable(const XMqttServer* server);

/**
 * @brief 设置是否支持主题通配符订阅。
 * @param server 服务器实例指针（非 NULL）。
 * @param available 是否支持。
 */
void XMqttServer_setWildcardAvailable(XMqttServer* server, bool available);
/**
 * @brief 获取是否支持主题通配符订阅。
 * @param server 服务器实例指针。
 * @return 是否支持，server 为 NULL 时返回 false。
 */
bool XMqttServer_wildcardAvailable(const XMqttServer* server);

/**
 * @brief 设置是否支持 MQTT 5.0 订阅标识符。
 * @param server 服务器实例指针（非 NULL）。
 * @param available 是否支持。
 */
void XMqttServer_setSubscriptionIdAvailable(XMqttServer* server, bool available);
/**
 * @brief 获取是否支持 MQTT 5.0 订阅标识符。
 * @param server 服务器实例指针。
 * @return 是否支持，server 为 NULL 时返回 false。
 */
bool XMqttServer_subscriptionIdAvailable(const XMqttServer* server);

/**
 * @brief 设置是否支持共享订阅。
 * @param server 服务器实例指针（非 NULL）。
 * @param available 是否支持。
 */
void XMqttServer_setSharedAvailable(XMqttServer* server, bool available);
/**
 * @brief 获取是否支持共享订阅。
 * @param server 服务器实例指针。
 * @return 是否支持，server 为 NULL 时返回 false。
 */
bool XMqttServer_sharedAvailable(const XMqttServer* server);

/******************************************************************************************
 * 虚函数接口
 ******************************************************************************************/

/**
 * @brief 向指定传输设备写入原始字节（虚函数调度入口）。
 * @details 基类默认实现不发送任何数据并返回 false；TCP 子类重写为写入
 *          XTcpSocket，测试桩可重写为写入内存缓冲区。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 目标传输设备指针。
 * @param data 待发送字节缓冲区，size 为 0 时可为 NULL。
 * @param size 待发送字节数。
 * @return 全部字节写入成功返回 true，失败返回 false。
 */
bool XMqttServer_sendData_base(XMqttServer* server, void* transport,
                               const uint8_t* data, size_t size);

/**
 * @brief 关闭指定传输设备（虚函数调度入口）。
 * @details 服务器在协议违规、保活超时、同 clientId 抢占等场景下调用本函数
 *          主动断开连接。基类默认实现为空操作；TCP 子类重写为调用
 *          XAbstractSocket_disconnectFromHost_base。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 要关闭的传输设备指针。
 */
void XMqttServer_closeClient_base(XMqttServer* server, void* transport);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

/**
 * @brief 客户端完成 CONNECT 握手信号。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 完成握手的客户端传输设备指针。
 * @return 信号发送结果。
 */
void* XMqttServer_clientConnected_signal(XMqttServer* server, void* transport);

/**
 * @brief 客户端连接结束信号。
 * @details 连接断开（无论是否正常）都会发送；参数 transport 对应的内部状态
 *          在信号返回后即被释放。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 已断开的客户端传输设备指针。
 * @return 信号发送结果。
 */
void* XMqttServer_clientDisconnected_signal(XMqttServer* server, void* transport);

/**
 * @brief 收到客户端发布的消息信号。
 * @param server 服务器实例指针（非 NULL）。
 * @param transport 发布消息的客户端传输设备指针。
 * @param topic 消息主题。
 * @param payload 消息载荷。
 * @return 信号发送结果。
 */
void* XMqttServer_messageReceived_signal(XMqttServer* server, void* transport,
                                         const XMqttTopicName* topic,
                                         const XByteArray* payload);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XMqttServer_deleteLater   XObject_deleteLater
#define XMqttServer_deinitLater   XObject_deinitLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttServer_create
#define XMqttServer_create() XMqttServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMQTTSERVER_H
