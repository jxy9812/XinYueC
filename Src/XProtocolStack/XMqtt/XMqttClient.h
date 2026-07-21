#ifndef XMQTTCLIENT_H
#define XMQTTCLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XString.h"
#include "XByteArray.h"
#include "XMqttGlobal.h"
#include "XMqttTopicName.h"
#include "XMqttTopicFilter.h"
#include "XMqttMessage.h"
#include "XMqttSubscription.h"
#include "XMqttPublishProperties.h"
#include "XMqttConnectionProperties.h"
#include "XMqttSubscriptionProperties.h"
#include "XMqttAuthenticationProperties.h"
#include "XSslSocket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttClient.h
 * @brief MQTT 客户端（对齐 Qt 6.8 QMqttClient）
 * @details 表示与 MQTT Broker 通信的客户端，继承自 XObject。
 *          支持 MQTT 3.1、3.1.1 和 5.0 协议版本。
 */

/**
 * @brief 传输类型枚举
 * @details 对齐 Qt 6.8 QMqttClient::TransportType
 */
typedef enum {
    XMqttClient_IODevice = 0,        ///< 基于 QIODevice 的传输
    XMqttClient_AbstractSocket,      ///< 基于 QAbstractSocket 的传输
    XMqttClient_SecureSocket         ///< 基于 QSslSocket 的传输
} XMqttClient_TransportType;

/**
 * @brief 客户端状态枚举
 * @details 对齐 Qt 6.8 QMqttClient::ClientState
 */
typedef enum {
    XMqttClient_Disconnected = 0,    ///< 未连接
    XMqttClient_Connecting,          ///< 连接中
    XMqttClient_Connected            ///< 已连接
} XMqttClient_State;

/**
 * @brief 客户端错误枚举
 * @details 对齐 Qt 6.8 QMqttClient::ClientError
 */
typedef enum {
    /* 协议状态错误 */
    XMqttClient_NoError                = 0,     ///< 无错误
    XMqttClient_InvalidProtocolVersion = 1,     ///< 无效的协议版本
    XMqttClient_IdRejected             = 2,     ///< 客户端 ID 被拒绝
    XMqttClient_ServerUnavailable      = 3,     ///< 服务器不可用
    XMqttClient_BadUsernameOrPassword  = 4,     ///< 用户名或密码错误
    XMqttClient_NotAuthorized          = 5,     ///< 未授权
    /* Qt 内部状态错误 */
    XMqttClient_TransportInvalid       = 256,   ///< 传输无效
    XMqttClient_ProtocolViolation,              ///< 协议违规
    XMqttClient_UnknownError,                   ///< 未知错误
    XMqttClient_Mqtt5SpecificError              ///< MQTT 5.0 特定错误
} XMqttClient_Error;

/**
 * @brief 协议版本枚举
 * @details 对齐 Qt 6.8 QMqttClient::ProtocolVersion
 */
typedef enum {
    XMqttClient_MQTT_3_1   = 3,     ///< MQTT 3.1
    XMqttClient_MQTT_3_1_1 = 4,     ///< MQTT 3.1.1
    XMqttClient_MQTT_5_0   = 5      ///< MQTT 5.0
} XMqttClient_ProtocolVersion;

/**
 * @brief XMqttClient 虚函数表枚举
 */
XCLASS_DEFINE_BEGING(XMqttClient)
XCLASS_DEFINE_ENUM(XMqttClient, ConnectToHost) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XMqttClient, DisconnectFromHost),
XCLASS_DEFINE_ENUM(XMqttClient, Subscribe),
XCLASS_DEFINE_ENUM(XMqttClient, Unsubscribe),
XCLASS_DEFINE_ENUM(XMqttClient, Publish),
XCLASS_DEFINE_ENUM(XMqttClient, RequestPing),
XCLASS_DEFINE_END(XMqttClient)

/**
 * @brief MQTT 客户端结构体
 * @details 对齐 Qt 6.8 QMqttClient，继承自 XObject。
 */
typedef struct XMqttClient {
    XObject m_class;                              ///< 继承自 XObject

    /* 连接配置 */
    XString* m_hostname;                          ///< Broker 主机名
    uint16_t m_port;                              ///< Broker 端口
    XString* m_clientId;                          ///< 客户端 ID
    uint16_t m_keepAlive;                         ///< 保活间隔（秒）
    uint8_t m_protocolVersion;                    ///< 协议版本（XMqttClient_ProtocolVersion）

    /* 状态 */
    uint8_t m_state;                              ///< 客户端状态（XMqttClient_State）
    uint8_t m_error;                              ///< 错误码（XMqttClient_Error）

    /* 认证 */
    XString* m_username;                          ///< 用户名
    XString* m_password;                          ///< 密码
    bool m_cleanSession;                          ///< 是否使用清洁会话

    /* 遗嘱消息 */
    XString* m_willTopic;                         ///< 遗嘱主题
    uint8_t m_willQoS;                            ///< 遗嘱 QoS
    XByteArray* m_willMessage;                    ///< 遗嘱消息载荷
    bool m_willRetain;                            ///< 遗嘱消息是否保留
    bool m_autoKeepAlive;                         ///< 是否自动保活

    /* MQTT 5.0 属性 */
    XMqttConnectionProperties* m_connectionProperties;     ///< 连接属性
    XMqttLastWillProperties* m_lastWillProperties;         ///< 遗嘱属性
    XMqttServerConnectionProperties* m_serverConnectionProperties; ///< 服务端连接属性

    /* 传输 */
    uint8_t m_transportType;                      ///< 传输类型（XMqttClient_TransportType）
    void* m_transport;                            ///< 传输设备指针（不拥有）
} XMqttClient;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化 XMqttClient 的虚函数表
 */
XVtable* XMqttClient_class_init(void);

/**
 * @brief 在堆上创建并初始化 XMqttClient 实例
 */
XMqttClient* XMqttClient_create(void);

/**
 * @brief 初始化 XMqttClient 实例
 */
void XMqttClient_init(XMqttClient* client);

/******************************************************************************************
 * 传输设置
 ******************************************************************************************/

/**
 * @brief 设置传输设备
 * @param client 客户端实例指针（非 NULL）
 * @param device 传输设备指针
 * @param transport 传输类型
 */
void XMqttClient_setTransport(XMqttClient* client, void* device, XMqttClient_TransportType transport);

/**
 * @brief 获取传输设备
 * @param client 客户端实例指针
 * @return 传输设备指针，未设置返回 NULL
 */
void* XMqttClient_transport(const XMqttClient* client);

/******************************************************************************************
 * 订阅/取消订阅
 ******************************************************************************************/

/**
 * @brief 订阅主题
 * @param client 客户端实例指针（非 NULL）
 * @param topic 主题过滤器
 * @param qos QoS 等级
 * @return 订阅实例指针，失败返回 NULL
 */
XMqttSubscription* XMqttClient_subscribe(XMqttClient* client, const XMqttTopicFilter* topic, uint8_t qos);

/**
 * @brief 订阅主题（带 MQTT 5.0 属性）
 * @param client 客户端实例指针（非 NULL）
 * @param topic 主题过滤器
 * @param properties 订阅属性
 * @param qos QoS 等级
 * @return 订阅实例指针，失败返回 NULL
 */
XMqttSubscription* XMqttClient_subscribe_with_properties(XMqttClient* client, const XMqttTopicFilter* topic,
                                                          const XMqttSubscriptionProperties* properties, uint8_t qos);

/**
 * @brief 取消订阅
 * @param client 客户端实例指针（非 NULL）
 * @param topic 主题过滤器
 */
void XMqttClient_unsubscribe(XMqttClient* client, const XMqttTopicFilter* topic);

/**
 * @brief 取消订阅（带 MQTT 5.0 属性）
 * @param client 客户端实例指针（非 NULL）
 * @param topic 主题过滤器
 * @param properties 取消订阅属性
 */
void XMqttClient_unsubscribe_with_properties(XMqttClient* client, const XMqttTopicFilter* topic,
                                              const XMqttUnsubscriptionProperties* properties);

/******************************************************************************************
 * 发布消息
 ******************************************************************************************/

/**
 * @brief 发布消息
 * @param client 客户端实例指针（非 NULL）
 * @param topic 主题名称
 * @param message 消息载荷
 * @param messageLen 消息载荷长度
 * @param qos QoS 等级
 * @param retain 是否保留
 * @return 消息 ID，失败返回 -1
 */
int32_t XMqttClient_publish(XMqttClient* client, const XMqttTopicName* topic,
                             const uint8_t* message, size_t messageLen,
                             uint8_t qos, bool retain);

/**
 * @brief 发布消息（带 MQTT 5.0 属性）
 * @param client 客户端实例指针（非 NULL）
 * @param topic 主题名称
 * @param properties 发布属性
 * @param message 消息载荷
 * @param messageLen 消息载荷长度
 * @param qos QoS 等级
 * @param retain 是否保留
 * @return 消息 ID，失败返回 -1
 */
int32_t XMqttClient_publish_with_properties(XMqttClient* client, const XMqttTopicName* topic,
                                             const XMqttPublishProperties* properties,
                                             const uint8_t* message, size_t messageLen,
                                             uint8_t qos, bool retain);

/******************************************************************************************
 * Ping 请求
 ******************************************************************************************/

/**
 * @brief 请求 Ping
 * @param client 客户端实例指针（非 NULL）
 * @return 成功返回 true
 */
bool XMqttClient_requestPing(XMqttClient* client);

/******************************************************************************************
 * 连接/断开
 ******************************************************************************************/

/**
 * @brief 获取主机名（常量引用）
 * @param client 客户端实例指针
 * @return 主机名字符串（常量引用，调用者不应释放），未设置返回 NULL
 */
const XString* XMqttClient_hostname_const(const XMqttClient* client);
/**
 * @brief 获取主机名（深拷贝）
 * @param client 客户端实例指针
 * @return 主机名字符串的深拷贝，调用者负责释放，未设置返回 NULL
 */
XString* XMqttClient_hostname(const XMqttClient* client);

/**
 * @brief 获取 Broker 端口
 * @param client 客户端实例指针
 * @return Broker 端口号，未设置返回 0
 */
uint16_t XMqttClient_port(const XMqttClient* client);

/**
 * @brief 获取客户端 ID（常量引用）
 * @param client 客户端实例指针
 * @return 客户端 ID 字符串（常量引用，调用者不应释放），未设置返回 NULL
 */
const XString* XMqttClient_clientId_const(const XMqttClient* client);
/**
 * @brief 获取客户端 ID（深拷贝）
 * @param client 客户端实例指针
 * @return 客户端 ID 字符串的深拷贝，调用者负责释放，未设置返回 NULL
 */
XString* XMqttClient_clientId(const XMqttClient* client);

/**
 * @brief 获取保活间隔
 * @param client 客户端实例指针
 * @return 保活间隔（秒），未设置返回 0
 */
uint16_t XMqttClient_keepAlive(const XMqttClient* client);

/**
 * @brief 获取协议版本
 * @param client 客户端实例指针
 * @return 协议版本（XMqttClient_ProtocolVersion），未设置返回 0
 */
uint8_t XMqttClient_protocolVersion(const XMqttClient* client);

/**
 * @brief 连接 Broker（虚函数调度入口）
 * @param client 客户端实例指针（非 NULL）
 * @details 通过虚函数表调度到子类的具体实现，基类默认不执行实际连接操作
 */
void XMqttClient_connectToHost_base(XMqttClient* client);

/**
 * @brief 加密连接 Broker（对齐 Qt 6.8 QMqttClient::connectToHostEncrypted）
 * @param client 客户端实例指针（非 NULL）
 * @param sslConfig SSL 配置指针（非 NULL，指向 XSslConfiguration）
 * @details 使用 SSL/TLS 加密连接到 Broker。
 *          当 XSSL_USE_MBEDTLS 未定义时，此函数退化为调用 connectToHost_base。
 *          对齐 Qt 6.8: QMqttClient::connectToHostEncrypted(const QSslConfiguration &conf)
 */
void XMqttClient_connectToHostEncrypted(XMqttClient* client, const XSslConfiguration* sslConfig);


/**
 * @brief 断开与 Broker 的连接（虚函数调度入口）
 * @param client 客户端实例指针（非 NULL）
 * @details 通过虚函数表调度到子类的具体实现，基类默认不执行实际断开操作
 */
void XMqttClient_disconnectFromHost_base(XMqttClient* client);

/******************************************************************************************
 * 状态/错误查询
 ******************************************************************************************/

/**
 * @brief 获取客户端状态
 * @param client 客户端实例指针
 * @return 客户端状态（XMqttClient_State），未初始化返回 Disconnected
 */
uint8_t XMqttClient_state(const XMqttClient* client);

/**
 * @brief 获取错误码
 * @param client 客户端实例指针
 * @return 错误码（XMqttClient_Error），无错误返回 NoError
 */
uint8_t XMqttClient_error(const XMqttClient* client);

/******************************************************************************************
 * 属性 Getters/Setters
 ******************************************************************************************/

/**
 * @brief 获取用户名（常量引用）
 * @param client 客户端实例指针
 * @return 用户名字符串（常量引用，调用者不应释放），未设置返回 NULL
 */
const XString* XMqttClient_username_const(const XMqttClient* client);
/**
 * @brief 获取用户名（深拷贝）
 * @param client 客户端实例指针
 * @return 用户名字符串的深拷贝，调用者负责释放，未设置返回 NULL
 */
XString* XMqttClient_username(const XMqttClient* client);
/**
 * @brief 设置用户名
 * @param client 客户端实例指针（非 NULL）
 * @param username 用户名字符串（UTF-8，可为 NULL 表示清除）
 */
void XMqttClient_setUsername(XMqttClient* client, const char* username);

/**
 * @brief 获取密码（常量引用）
 * @param client 客户端实例指针
 * @return 密码字符串（常量引用，调用者不应释放），未设置返回 NULL
 */
const XString* XMqttClient_password_const(const XMqttClient* client);
/**
 * @brief 获取密码（深拷贝）
 * @param client 客户端实例指针
 * @return 密码字符串的深拷贝，调用者负责释放，未设置返回 NULL
 */
XString* XMqttClient_password(const XMqttClient* client);
/**
 * @brief 设置密码
 * @param client 客户端实例指针（非 NULL）
 * @param password 密码字符串（UTF-8，可为 NULL 表示清除）
 */
void XMqttClient_setPassword(XMqttClient* client, const char* password);

/**
 * @brief 是否使用清洁会话
 * @param client 客户端实例指针
 * @return 使用清洁会话返回 true，默认返回 true
 */
bool XMqttClient_cleanSession(const XMqttClient* client);
/**
 * @brief 设置是否使用清洁会话
 * @param client 客户端实例指针（非 NULL）
 * @param cleanSession 是否使用清洁会话
 */
void XMqttClient_setCleanSession(XMqttClient* client, bool cleanSession);

/**
 * @brief 获取遗嘱主题（常量引用）
 * @param client 客户端实例指针
 * @return 遗嘱主题字符串（常量引用，调用者不应释放），未设置返回 NULL
 */
const XString* XMqttClient_willTopic_const(const XMqttClient* client);
/**
 * @brief 获取遗嘱主题（深拷贝）
 * @param client 客户端实例指针
 * @return 遗嘱主题字符串的深拷贝，调用者负责释放，未设置返回 NULL
 */
XString* XMqttClient_willTopic(const XMqttClient* client);
/**
 * @brief 设置遗嘱主题
 * @param client 客户端实例指针（非 NULL）
 * @param willTopic 遗嘱主题字符串（UTF-8，可为 NULL 表示清除）
 */
void XMqttClient_setWillTopic(XMqttClient* client, const char* willTopic);

/**
 * @brief 获取遗嘱消息的 QoS 等级
 * @param client 客户端实例指针
 * @return QoS 等级（0/1/2），未设置返回 0
 */
uint8_t XMqttClient_willQoS(const XMqttClient* client);
/**
 * @brief 设置遗嘱消息的 QoS 等级
 * @param client 客户端实例指针（非 NULL）
 * @param willQoS QoS 等级（0/1/2）
 */
void XMqttClient_setWillQoS(XMqttClient* client, uint8_t willQoS);

/**
 * @brief 获取遗嘱消息载荷（常量引用）
 * @param client 客户端实例指针
 * @return 遗嘱消息载荷（常量引用，调用者不应释放），未设置返回 NULL
 */
const XByteArray* XMqttClient_willMessage_const(const XMqttClient* client);
/**
 * @brief 获取遗嘱消息载荷（深拷贝）
 * @param client 客户端实例指针
 * @return 遗嘱消息载荷的深拷贝，调用者负责释放，未设置返回 NULL
 */
XByteArray* XMqttClient_willMessage(const XMqttClient* client);
/**
 * @brief 设置遗嘱消息载荷
 * @param client 客户端实例指针（非 NULL）
 * @param data 遗嘱消息载荷数据缓冲区（可为 NULL）
 * @param len 遗嘱消息载荷数据长度
 */
void XMqttClient_setWillMessage(XMqttClient* client, const uint8_t* data, size_t len);

/**
 * @brief 遗嘱消息是否保留
 * @param client 客户端实例指针
 * @return 保留返回 true，默认返回 false
 */
bool XMqttClient_willRetain(const XMqttClient* client);
/**
 * @brief 设置遗嘱消息是否保留
 * @param client 客户端实例指针（非 NULL）
 * @param willRetain 是否保留遗嘱消息
 */
void XMqttClient_setWillRetain(XMqttClient* client, bool willRetain);

/**
 * @brief 是否自动保活
 * @param client 客户端实例指针
 * @return 自动保活返回 true，默认返回 true
 */
bool XMqttClient_autoKeepAlive(const XMqttClient* client);
/**
 * @brief 设置是否自动保活
 * @param client 客户端实例指针（非 NULL）
 * @param autoKeepAlive 是否自动保活
 */
void XMqttClient_setAutoKeepAlive(XMqttClient* client, bool autoKeepAlive);

/**
 * @brief 设置 Broker 主机名
 * @param client 客户端实例指针（非 NULL）
 * @param hostname 主机名字符串（UTF-8，可为 NULL 表示清除）
 */
void XMqttClient_setHostname(XMqttClient* client, const char* hostname);
/**
 * @brief 设置 Broker 端口
 * @param client 客户端实例指针（非 NULL）
 * @param port 端口号
 */
void XMqttClient_setPort(XMqttClient* client, uint16_t port);
/**
 * @brief 设置客户端 ID
 * @param client 客户端实例指针（非 NULL）
 * @param clientId 客户端 ID 字符串（UTF-8，可为 NULL 表示清除）
 */
void XMqttClient_setClientId(XMqttClient* client, const char* clientId);
/**
 * @brief 设置保活间隔
 * @param client 客户端实例指针（非 NULL）
 * @param keepAlive 保活间隔（秒）
 */
void XMqttClient_setKeepAlive(XMqttClient* client, uint16_t keepAlive);
/**
 * @brief 设置协议版本
 * @param client 客户端实例指针（非 NULL）
 * @param protocolVersion 协议版本（XMqttClient_ProtocolVersion）
 */
void XMqttClient_setProtocolVersion(XMqttClient* client, uint8_t protocolVersion);
/**
 * @brief 设置客户端状态
 * @param client 客户端实例指针（非 NULL）
 * @param state 客户端状态（XMqttClient_State）
 */
void XMqttClient_setState(XMqttClient* client, uint8_t state);
/**
 * @brief 设置错误码
 * @param client 客户端实例指针（非 NULL）
 * @param error 错误码（XMqttClient_Error）
 */
void XMqttClient_setError(XMqttClient* client, uint8_t error);

/******************************************************************************************
 * MQTT 5.0 属性接口
 ******************************************************************************************/

/**
 * @brief 设置连接属性（MQTT 5.0）
 * @param client 客户端实例指针（非 NULL）
 * @param prop 连接属性指针，为 NULL 表示清除
 * @details 设置 CONNECT 报文中的属性，仅在 MQTT 5.0 协议版本下有效
 */
void XMqttClient_setConnectionProperties(XMqttClient* client, const XMqttConnectionProperties* prop);
/**
 * @brief 获取连接属性（深拷贝）
 * @param client 客户端实例指针
 * @return 连接属性的深拷贝，调用者负责释放，未设置返回 NULL
 */
XMqttConnectionProperties* XMqttClient_connectionProperties(const XMqttClient* client);
/**
 * @brief 获取连接属性（常量引用）
 * @param client 客户端实例指针
 * @return 连接属性指针（常量引用，调用者不应释放），未设置返回 NULL
 */
const XMqttConnectionProperties* XMqttClient_connectionProperties_const(const XMqttClient* client);

/**
 * @brief 设置遗嘱属性（MQTT 5.0）
 * @param client 客户端实例指针（非 NULL）
 * @param prop 遗嘱属性指针，为 NULL 表示清除
 * @details 设置遗嘱消息的 MQTT 5.0 属性，包括遗嘱延迟间隔、载荷格式等
 */
void XMqttClient_setLastWillProperties(XMqttClient* client, const XMqttLastWillProperties* prop);
/**
 * @brief 获取遗嘱属性（深拷贝）
 * @param client 客户端实例指针
 * @return 遗嘱属性的深拷贝，调用者负责释放，未设置返回 NULL
 */
XMqttLastWillProperties* XMqttClient_lastWillProperties(const XMqttClient* client);
/**
 * @brief 获取遗嘱属性（常量引用）
 * @param client 客户端实例指针
 * @return 遗嘱属性指针（常量引用，调用者不应释放），未设置返回 NULL
 */
const XMqttLastWillProperties* XMqttClient_lastWillProperties_const(const XMqttClient* client);

/**
 * @brief 获取服务端连接属性（深拷贝）
 * @param client 客户端实例指针
 * @return 服务端连接属性的深拷贝，调用者负责释放，未设置返回 NULL
 * @details 连接建立后，服务端返回的连接属性，包括最大 QoS、保留消息支持等
 */
XMqttServerConnectionProperties* XMqttClient_serverConnectionProperties(const XMqttClient* client);
/**
 * @brief 获取服务端连接属性（常量引用）
 * @param client 客户端实例指针
 * @return 服务端连接属性指针（常量引用，调用者不应释放），未设置返回 NULL
 */
const XMqttServerConnectionProperties* XMqttClient_serverConnectionProperties_const(const XMqttClient* client);

/**
 * @brief 发起 MQTT 5.0 扩展认证
 * @param client 客户端实例指针（非 NULL）
 * @param prop 认证属性指针
 * @details 在连接过程中或连接后发起扩展认证流程，仅在 MQTT 5.0 协议版本下有效
 */
void XMqttClient_authenticate(XMqttClient* client, const XMqttAuthenticationProperties* prop);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

/**
 * @brief 连接成功信号
 * @param client 客户端实例指针（非 NULL）
 * @return 信号发送结果
 */
void* XMqttClient_connected_signal(XMqttClient* client);
/**
 * @brief 断开连接信号
 * @param client 客户端实例指针（非 NULL）
 * @return 信号发送结果
 */
void* XMqttClient_disconnected_signal(XMqttClient* client);
/**
 * @brief 收到消息信号
 * @param client 客户端实例指针（非 NULL）
 * @param message 收到的消息载荷
 * @param topic 消息主题
 * @return 信号发送结果
 */
void* XMqttClient_messageReceived_signal(XMqttClient* client, const XByteArray* message, const XMqttTopicName* topic);
/**
 * @brief 消息状态变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param id 消息 ID
 * @param s 消息状态（XMqtt_MessageStatus）
 * @param properties 消息状态属性
 * @return 信号发送结果
 */
void* XMqttClient_messageStatusChanged_signal(XMqttClient* client, int32_t id, uint8_t s, const XMqttMessageStatusProperties* properties);
/**
 * @brief 消息已发送信号
 * @param client 客户端实例指针（非 NULL）
 * @param id 已发送的消息 ID
 * @return 信号发送结果
 */
void* XMqttClient_messageSent_signal(XMqttClient* client, int32_t id);
/**
 * @brief Ping 响应接收信号
 * @param client 客户端实例指针（非 NULL）
 * @return 信号发送结果
 */
void* XMqttClient_pingResponseReceived_signal(XMqttClient* client);
/**
 * @brief Broker 会话恢复信号
 * @param client 客户端实例指针（非 NULL）
 * @return 信号发送结果
 */
void* XMqttClient_brokerSessionRestored_signal(XMqttClient* client);

/**
 * @brief 主机名变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param hostname 新的主机名
 * @return 信号发送结果
 */
void* XMqttClient_hostnameChanged_signal(XMqttClient* client, const XString* hostname);
/**
 * @brief 端口变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param port 新的端口号
 * @return 信号发送结果
 */
void* XMqttClient_portChanged_signal(XMqttClient* client, uint16_t port);
/**
 * @brief 客户端 ID 变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param clientId 新的客户端 ID
 * @return 信号发送结果
 */
void* XMqttClient_clientIdChanged_signal(XMqttClient* client, const XString* clientId);
/**
 * @brief 保活间隔变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param keepAlive 新的保活间隔（秒）
 * @return 信号发送结果
 */
void* XMqttClient_keepAliveChanged_signal(XMqttClient* client, uint16_t keepAlive);
/**
 * @brief 协议版本变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param protocolVersion 新的协议版本
 * @return 信号发送结果
 */
void* XMqttClient_protocolVersionChanged_signal(XMqttClient* client, uint8_t protocolVersion);
/**
 * @brief 客户端状态变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param state 新的客户端状态
 * @return 信号发送结果
 */
void* XMqttClient_stateChanged_signal(XMqttClient* client, uint8_t state);
/**
 * @brief 错误码变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param error 新的错误码
 * @return 信号发送结果
 */
void* XMqttClient_errorChanged_signal(XMqttClient* client, uint8_t error);
/**
 * @brief 用户名变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param username 新的用户名
 * @return 信号发送结果
 */
void* XMqttClient_usernameChanged_signal(XMqttClient* client, const XString* username);
/**
 * @brief 密码变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param password 新的密码
 * @return 信号发送结果
 */
void* XMqttClient_passwordChanged_signal(XMqttClient* client, const XString* password);
/**
 * @brief 清洁会话设置变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param cleanSession 是否使用清洁会话
 * @return 信号发送结果
 */
void* XMqttClient_cleanSessionChanged_signal(XMqttClient* client, bool cleanSession);
/**
 * @brief 遗嘱主题变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param willTopic 新的遗嘱主题
 * @return 信号发送结果
 */
void* XMqttClient_willTopicChanged_signal(XMqttClient* client, const XString* willTopic);
/**
 * @brief 遗嘱 QoS 变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param willQoS 新的遗嘱 QoS 等级
 * @return 信号发送结果
 */
void* XMqttClient_willQoSChanged_signal(XMqttClient* client, uint8_t willQoS);
/**
 * @brief 遗嘱消息载荷变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param willMessage 新的遗嘱消息载荷
 * @return 信号发送结果
 */
void* XMqttClient_willMessageChanged_signal(XMqttClient* client, const XByteArray* willMessage);
/**
 * @brief 遗嘱保留标志变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param willRetain 是否保留遗嘱消息
 * @return 信号发送结果
 */
void* XMqttClient_willRetainChanged_signal(XMqttClient* client, bool willRetain);
/**
 * @brief 自动保活设置变更信号
 * @param client 客户端实例指针（非 NULL）
 * @param autoKeepAlive 是否自动保活
 * @return 信号发送结果
 */
void* XMqttClient_autoKeepAliveChanged_signal(XMqttClient* client, bool autoKeepAlive);
/**
 * @brief 认证请求信号（MQTT 5.0）
 * @param client 客户端实例指针（非 NULL）
 * @param p 认证属性
 * @return 信号发送结果
 */
void* XMqttClient_authenticationRequested_signal(XMqttClient* client, const XMqttAuthenticationProperties* p);
/**
 * @brief 认证完成信号（MQTT 5.0）
 * @param client 客户端实例指针（非 NULL）
 * @param p 认证属性
 * @return 信号发送结果
 */
void* XMqttClient_authenticationFinished_signal(XMqttClient* client, const XMqttAuthenticationProperties* p);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XMqttClient_deleteLater   XObject_deleteLater
#define XMqttClient_deinitLater   XObject_deinitLater

#ifdef __cplusplus
}
#endif

#endif // XMQTTCLIENT_H
