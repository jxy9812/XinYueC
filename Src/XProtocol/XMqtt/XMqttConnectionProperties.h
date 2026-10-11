/**
 * @file       XMqttConnectionProperties.h
 * @brief      MQTT 连接相关属性公开 API（对齐 Qt 6.8 QMqttConnectionProperties /
 *             QMqttLastWillProperties / QMqttServerConnectionProperties）。
 * @details    定义 MQTT 5.0 连接属性、遗嘱属性与服务端连接属性。实现只依赖
 *             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 *             受 XMQTT_PROPERTIES_ON 开关裁剪（见 XMqtt_config.h）。
 */
#ifndef XMQTTCONNECTIONPROPERTIES_H
#define XMQTTCONNECTIONPROPERTIES_H

#include "XMqtt_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XMQTT_ON
#if XMQTT_PROPERTIES_ON

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XByteArray.h"
#include "XMqttGlobal.h"
#include "XMqttType.h"

/* ---------- XMqttLastWillProperties ---------- */

XCLASS_DEFINE_BEGING(XMqttLastWillProperties)
XCLASS_DEFINE_EXTEND_END(XMqttLastWillProperties, XClass)

/**
 * @brief 遗嘱消息属性结构体
 * @details 对齐 Qt 6.8 QMqttLastWillProperties
 */
typedef struct XMqttLastWillProperties {
    XClass m_class;                       ///< 第一个成员、由 XClass 管理、禁止手工修改。
    uint32_t m_willDelayInterval;         ///< 遗嘱延迟间隔，单位秒；有效范围 0-4294967295，0 表示立即发布；调用者可随时修改。
    uint8_t m_payloadFormatIndicator;     ///< 载荷格式指示（XMqtt_PayloadFormatIndicator）；有效值 0（未指定）/1（UTF-8）；调用者可随时修改。
    uint32_t m_messageExpiryInterval;     ///< 消息过期时间，单位秒；0 表示永不过期；调用者可随时修改。
    XString* m_contentType;               ///< 内容类型（MIME）；由对象拥有、可为 NULL，调用者经 setContentType 系列修改，不得直接释放。
    XString* m_responseTopic;             ///< 响应主题；由对象拥有、可为 NULL，调用者经 setResponseTopic 系列修改，不得直接释放。
    XByteArray* m_correlationData;        ///< 关联数据；由对象拥有、可为 NULL，调用者经 setCorrelationData 修改，不得直接释放。
    XMqttUserProperties* m_userProperties; ///< 用户属性列表；由对象拥有、可为 NULL，调用者经 setUserProperties 修改，不得直接释放。
} XMqttLastWillProperties;

/**
 * @brief 初始化 XMqttLastWillProperties 的虚函数表
 * @return 指向初始化完成的 XVtable 指针
 */
XVtable* XMqttLastWillProperties_class_init(void);

/**
 * @brief 创建遗嘱消息属性实例
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttLastWillProperties* XMqttLastWillProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建遗嘱消息属性的深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttLastWillProperties* XMqttLastWillProperties_create_copy(const XMqttLastWillProperties* other);

/**
 * @brief 初始化遗嘱消息属性实例
 * @param prop 待初始化的实例指针（非 NULL）
 */
void XMqttLastWillProperties_init(XMqttLastWillProperties* prop);

/**
 * @brief 获取遗嘱延迟间隔
 * @param prop 实例指针
 * @return 遗嘱延迟间隔（秒）
 */
uint32_t XMqttLastWillProperties_willDelayInterval(const XMqttLastWillProperties* prop);

/**
 * @brief 设置遗嘱延迟间隔
 * @param prop 实例指针（非 NULL）
 * @param delay 遗嘱延迟间隔（秒）
 */
void XMqttLastWillProperties_setWillDelayInterval(XMqttLastWillProperties* prop, uint32_t delay);

/**
 * @brief 获取载荷格式指示
 * @param prop 实例指针
 * @return 载荷格式指示（XMqtt_PayloadFormatIndicator）
 */
uint8_t XMqttLastWillProperties_payloadFormatIndicator(const XMqttLastWillProperties* prop);

/**
 * @brief 设置载荷格式指示
 * @param prop 实例指针（非 NULL）
 * @param p 载荷格式指示
 */
void XMqttLastWillProperties_setPayloadFormatIndicator(XMqttLastWillProperties* prop, uint8_t p);

/**
 * @brief 获取消息过期时间
 * @param prop 实例指针
 * @return 消息过期时间（秒）
 */
uint32_t XMqttLastWillProperties_messageExpiryInterval(const XMqttLastWillProperties* prop);

/**
 * @brief 设置消息过期时间
 * @param prop 实例指针（非 NULL）
 * @param expiry 消息过期时间（秒）
 */
void XMqttLastWillProperties_setMessageExpiryInterval(XMqttLastWillProperties* prop, uint32_t expiry);

/**
 * @brief 获取内容类型（常量引用）
 * @param prop 实例指针
 * @return 内容类型字符串（常量引用，调用者不应释放）
 */
const XString* XMqttLastWillProperties_contentType_const(const XMqttLastWillProperties* prop);

/**
 * @brief 获取内容类型（深拷贝）
 * @param prop 实例指针
 * @return 内容类型字符串的深拷贝，调用者负责释放
 */
XString* XMqttLastWillProperties_contentType(const XMqttLastWillProperties* prop);

/**
 * @brief 设置内容类型
 * @param prop 实例指针（非 NULL）
 * @param content 内容类型字符串（UTF-8）
 */
void XMqttLastWillProperties_setContentType(XMqttLastWillProperties* prop, const char* content);

/**
 * @brief 获取响应主题（常量引用）
 * @param prop 实例指针
 * @return 响应主题字符串（常量引用，调用者不应释放）
 */
const XString* XMqttLastWillProperties_responseTopic_const(const XMqttLastWillProperties* prop);

/**
 * @brief 获取响应主题（深拷贝）
 * @param prop 实例指针
 * @return 响应主题字符串的深拷贝，调用者负责释放
 */
XString* XMqttLastWillProperties_responseTopic(const XMqttLastWillProperties* prop);

/**
 * @brief 设置响应主题
 * @param prop 实例指针（非 NULL）
 * @param response 响应主题字符串（UTF-8）
 */
void XMqttLastWillProperties_setResponseTopic(XMqttLastWillProperties* prop, const char* response);

/**
 * @brief 获取关联数据（常量引用）
 * @param prop 实例指针
 * @return 关联数据（常量引用，调用者不应释放）
 */
const XByteArray* XMqttLastWillProperties_correlationData_const(const XMqttLastWillProperties* prop);

/**
 * @brief 获取关联数据（深拷贝）
 * @param prop 实例指针
 * @return 关联数据的深拷贝，调用者负责释放
 */
XByteArray* XMqttLastWillProperties_correlationData(const XMqttLastWillProperties* prop);

/**
 * @brief 设置关联数据
 * @param prop 实例指针（非 NULL）
 * @param data 关联数据缓冲区
 * @param len 关联数据长度
 */
void XMqttLastWillProperties_setCorrelationData(XMqttLastWillProperties* prop, const uint8_t* data, size_t len);

/**
 * @brief 获取用户属性（常量引用）
 * @param prop 实例指针
 * @return 用户属性列表（常量引用，调用者不应释放）
 */
const XMqttUserProperties* XMqttLastWillProperties_userProperties_const(const XMqttLastWillProperties* prop);

/**
 * @brief 获取用户属性（深拷贝）
 * @param prop 实例指针
 * @return 用户属性列表的深拷贝，调用者负责释放
 */
XMqttUserProperties* XMqttLastWillProperties_userProperties(const XMqttLastWillProperties* prop);

/**
 * @brief 设置用户属性
 * @param prop 实例指针（非 NULL）
 * @param props 用户属性列表
 */
void XMqttLastWillProperties_setUserProperties(XMqttLastWillProperties* prop, const XMqttUserProperties* props);


/* ---------- XMqttConnectionProperties ---------- */

XCLASS_DEFINE_BEGING(XMqttConnectionProperties)
XCLASS_DEFINE_EXTEND_END(XMqttConnectionProperties, XClass)

/**
 * @brief 连接属性结构体
 * @details 对齐 Qt 6.8 QMqttConnectionProperties
 */
typedef struct XMqttConnectionProperties {
    XClass m_class;                       ///< 第一个成员、由 XClass 管理、禁止手工修改。
    uint32_t m_sessionExpiryInterval;     ///< 会话过期时间，单位秒；0 表示会话随网络连接断开立即结束；调用者可随时修改。
    uint16_t m_maximumReceive;            ///< 最大接收数（QoS 1/2 在途上限）；有效范围 1-65535，0xFFFFFFFF 表示使用默认；调用者可随时修改。
    uint32_t m_maximumPacketSize;         ///< 最大报文大小，单位字节；有效范围 1-268435455；调用者可随时修改。
    uint16_t m_maximumTopicAlias;         ///< 最大主题别名；0 表示不使用主题别名；调用者可随时修改。
    /* 紧凑标志位（位域优化：两个请求开关各 1 bit） */
    uint32_t m_requestResponseInformation : 1; ///< 是否向服务端请求响应信息，0/1；调用者可随时修改。
    uint32_t m_requestProblemInformation : 1;  ///< 是否允许服务端返回问题信息，0/1；调用者可随时修改。
    XMqttUserProperties* m_userProperties; ///< 用户属性列表；由对象拥有、可为 NULL，调用者经 setUserProperties 修改，不得直接释放。
    XString* m_authenticationMethod;      ///< 认证方法；由对象拥有、可为 NULL，调用者经 setAuthenticationMethod 系列修改，不得直接释放。
    XByteArray* m_authenticationData;     ///< 认证数据；由对象拥有、可为 NULL，调用者经 setAuthenticationData 修改，不得直接释放。
} XMqttConnectionProperties;

/**
 * @brief 初始化 XMqttConnectionProperties 的虚函数表
 * @return 指向初始化完成的 XVtable 指针
 */
XVtable* XMqttConnectionProperties_class_init(void);

/**
 * @brief 创建连接属性实例
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttConnectionProperties* XMqttConnectionProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建连接属性的深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttConnectionProperties* XMqttConnectionProperties_create_copy(const XMqttConnectionProperties* other);

/**
 * @brief 初始化连接属性实例
 * @param prop 待初始化的实例指针（非 NULL）
 */
void XMqttConnectionProperties_init(XMqttConnectionProperties* prop);

/**
 * @brief 获取会话过期时间
 * @param prop 实例指针
 * @return 会话过期时间（秒）
 */
uint32_t XMqttConnectionProperties_sessionExpiryInterval(const XMqttConnectionProperties* prop);

/**
 * @brief 设置会话过期时间
 * @param prop 实例指针（非 NULL）
 * @param expiry 会话过期时间（秒）
 */
void XMqttConnectionProperties_setSessionExpiryInterval(XMqttConnectionProperties* prop, uint32_t expiry);

/**
 * @brief 获取最大接收数
 * @param prop 实例指针
 * @return 最大接收数
 */
uint16_t XMqttConnectionProperties_maximumReceive(const XMqttConnectionProperties* prop);

/**
 * @brief 设置最大接收数
 * @param prop 实例指针（非 NULL）
 * @param maxRecv 最大接收数
 */
void XMqttConnectionProperties_setMaximumReceive(XMqttConnectionProperties* prop, uint16_t maxRecv);

/**
 * @brief 获取最大报文大小
 * @param prop 实例指针
 * @return 最大报文大小（字节）
 */
uint32_t XMqttConnectionProperties_maximumPacketSize(const XMqttConnectionProperties* prop);

/**
 * @brief 设置最大报文大小
 * @param prop 实例指针（非 NULL）
 * @param packetSize 最大报文大小（字节）
 */
void XMqttConnectionProperties_setMaximumPacketSize(XMqttConnectionProperties* prop, uint32_t packetSize);

/**
 * @brief 获取最大主题别名
 * @param prop 实例指针
 * @return 最大主题别名
 */
uint16_t XMqttConnectionProperties_maximumTopicAlias(const XMqttConnectionProperties* prop);

/**
 * @brief 设置最大主题别名
 * @param prop 实例指针（非 NULL）
 * @param alias 最大主题别名
 */
void XMqttConnectionProperties_setMaximumTopicAlias(XMqttConnectionProperties* prop, uint16_t alias);

/**
 * @brief 是否请求响应信息
 * @param prop 实例指针
 * @return 已请求返回 true
 */
bool XMqttConnectionProperties_requestResponseInformation(const XMqttConnectionProperties* prop);

/**
 * @brief 设置是否请求响应信息
 * @param prop 实例指针（非 NULL）
 * @param response true 表示请求
 */
void XMqttConnectionProperties_setRequestResponseInformation(XMqttConnectionProperties* prop, bool response);

/**
 * @brief 是否请求问题信息
 * @param prop 实例指针
 * @return 已请求返回 true
 */
bool XMqttConnectionProperties_requestProblemInformation(const XMqttConnectionProperties* prop);

/**
 * @brief 设置是否请求问题信息
 * @param prop 实例指针（非 NULL）
 * @param problem true 表示请求
 */
void XMqttConnectionProperties_setRequestProblemInformation(XMqttConnectionProperties* prop, bool problem);

/**
 * @brief 获取用户属性（常量引用）
 * @param prop 实例指针
 * @return 用户属性列表（常量引用，调用者不应释放）
 */
const XMqttUserProperties* XMqttConnectionProperties_userProperties_const(const XMqttConnectionProperties* prop);

/**
 * @brief 获取用户属性（深拷贝）
 * @param prop 实例指针
 * @return 用户属性列表的深拷贝，调用者负责释放
 */
XMqttUserProperties* XMqttConnectionProperties_userProperties(const XMqttConnectionProperties* prop);

/**
 * @brief 设置用户属性
 * @param prop 实例指针（非 NULL）
 * @param props 用户属性列表
 */
void XMqttConnectionProperties_setUserProperties(XMqttConnectionProperties* prop, const XMqttUserProperties* props);

/**
 * @brief 获取认证方法（常量引用）
 * @param prop 实例指针
 * @return 认证方法字符串（常量引用，调用者不应释放）
 */
const XString* XMqttConnectionProperties_authenticationMethod_const(const XMqttConnectionProperties* prop);

/**
 * @brief 获取认证方法（深拷贝）
 * @param prop 实例指针
 * @return 认证方法字符串的深拷贝，调用者负责释放
 */
XString* XMqttConnectionProperties_authenticationMethod(const XMqttConnectionProperties* prop);

/**
 * @brief 设置认证方法
 * @param prop 实例指针（非 NULL）
 * @param authMethod 认证方法字符串（UTF-8）
 */
void XMqttConnectionProperties_setAuthenticationMethod(XMqttConnectionProperties* prop, const char* authMethod);

/**
 * @brief 获取认证数据（常量引用）
 * @param prop 实例指针
 * @return 认证数据（常量引用，调用者不应释放）
 */
const XByteArray* XMqttConnectionProperties_authenticationData_const(const XMqttConnectionProperties* prop);

/**
 * @brief 获取认证数据（深拷贝）
 * @param prop 实例指针
 * @return 认证数据的深拷贝，调用者负责释放
 */
XByteArray* XMqttConnectionProperties_authenticationData(const XMqttConnectionProperties* prop);

/**
 * @brief 设置认证数据
 * @param prop 实例指针（非 NULL）
 * @param data 认证数据缓冲区
 * @param len 认证数据长度（字节）
 */
void XMqttConnectionProperties_setAuthenticationData(XMqttConnectionProperties* prop, const uint8_t* data, size_t len);


/* ---------- XMqttServerConnectionProperties ---------- */

/**
 * @brief 服务端连接属性细节枚举（位标志）
 * @details 各值可按位或组合，用于描述服务端在 CONNACK 中实际返回了哪些
 *          属性（对齐 Qt 6.8 QMqttServerConnectionProperties::availableProperties）。
 */
typedef enum {
    XMqttServerConnectionProperties_None                          = 0x00000000, ///< 服务端未返回任何可选属性。
    XMqttServerConnectionProperties_SessionExpiryInterval         = 0x00000001, ///< 服务端返回了会话过期间隔属性。
    XMqttServerConnectionProperties_MaximumReceive                = 0x00000002, ///< 服务端返回了最大接收数属性。
    XMqttServerConnectionProperties_MaximumQoS                    = 0x00000004, ///< 服务端返回了最大 QoS 等级属性。
    XMqttServerConnectionProperties_RetainAvailable               = 0x00000010, ///< 服务端声明了是否支持保留消息。
    XMqttServerConnectionProperties_MaximumPacketSize             = 0x00000020, ///< 服务端返回了最大报文大小属性。
    XMqttServerConnectionProperties_AssignedClientId              = 0x00000040, ///< 服务端分配了客户端 ID。
    XMqttServerConnectionProperties_MaximumTopicAlias             = 0x00000080, ///< 服务端返回了最大主题别名属性。
    XMqttServerConnectionProperties_ReasonString                  = 0x00000100, ///< 服务端返回了原因字符串属性。
    XMqttServerConnectionProperties_UserProperty                  = 0x00000200, ///< 服务端返回了用户属性。
    XMqttServerConnectionProperties_WildCardSupported             = 0x00000400, ///< 服务端声明支持通配符订阅。
    XMqttServerConnectionProperties_SubscriptionIdentifierSupport = 0x00000800, ///< 服务端声明支持订阅标识符。
    XMqttServerConnectionProperties_SharedSubscriptionSupport     = 0x00001000, ///< 服务端声明支持共享订阅。
    XMqttServerConnectionProperties_ServerKeepAlive               = 0x00002000, ///< 服务端返回了保活时间属性。
    XMqttServerConnectionProperties_ResponseInformation           = 0x00004000, ///< 服务端返回了响应信息属性。
    XMqttServerConnectionProperties_ServerReference               = 0x00008000, ///< 服务端返回了服务端引用属性。
    XMqttServerConnectionProperties_AuthenticationMethod          = 0x00010000, ///< 服务端返回了认证方法属性。
    XMqttServerConnectionProperties_AuthenticationData            = 0x00020000  ///< 服务端返回了认证数据属性。
} XMqttServerConnectionProperties_Detail;

XCLASS_DEFINE_BEGING(XMqttServerConnectionProperties)
XCLASS_DEFINE_EXTEND_END(XMqttServerConnectionProperties, XMqttConnectionProperties)

/**
 * @brief 服务端连接属性结构体
 * @details 对齐 Qt 6.8 QMqttServerConnectionProperties，继承自 XMqttConnectionProperties
 */
typedef struct XMqttServerConnectionProperties {
    XMqttConnectionProperties m_base;        ///< 第一个成员、继承自 XMqttConnectionProperties、由 XClass 管理、禁止手工修改。
    uint32_t m_availableProperties;          ///< 已设置的属性位标志（XMqttServerConnectionProperties_Detail 位组合）；由 CONNACK 解析填充，调用者只读。
    XString* m_reason;                       ///< 原因字符串；由对象拥有、可为 NULL；reason_const 返回借用指针，调用者不得释放或修改。
    uint16_t m_serverKeepAlive;              ///< 服务端保活时间，单位秒；0 表示沿用客户端请求值；由 CONNACK 解析填充，调用者只读。
    XString* m_responseInformation;          ///< 响应信息；由对象拥有、可为 NULL；responseInformation_const 返回借用指针，调用者不得释放。
    XString* m_serverReference;              ///< 服务端引用；由对象拥有、可为 NULL；serverReference_const 返回借用指针，调用者不得释放。
    /* 紧凑标志位（位域优化：valid 1 bit + maximumQoS 2 bit + 5 个开关各 1 bit + reasonCode 8 bit） */
    uint32_t m_valid : 1;                    ///< 是否已收到 CONNACK（0/1）；由 CONNACK 解析填充，调用者只读。
    uint32_t m_maximumQoS : 2;               ///< 服务端最大 QoS 等级（0/1/2）；由 CONNACK 解析填充，调用者只读。
    uint32_t m_retainAvailable : 1;          ///< 服务端是否支持保留消息（0/1）；由 CONNACK 解析填充，调用者只读。
    uint32_t m_clientIdAssigned : 1;         ///< 服务端是否分配了客户端 ID（0/1）；由 CONNACK 解析填充，调用者只读。
    uint32_t m_reasonCode : 8;               ///< CONNACK 原因码（XMqtt_ReasonCode）；由 CONNACK 解析填充，调用者只读。
    uint32_t m_wildcardSupported : 1;        ///< 服务端是否支持通配符订阅（0/1）；由 CONNACK 解析填充，调用者只读。
    uint32_t m_subscriptionIdentifierSupported : 1; ///< 服务端是否支持订阅标识符（0/1）；由 CONNACK 解析填充，调用者只读。
    uint32_t m_sharedSubscriptionSupported : 1;     ///< 服务端是否支持共享订阅（0/1）；由 CONNACK 解析填充，调用者只读。
} XMqttServerConnectionProperties;

/**
 * @brief 初始化 XMqttServerConnectionProperties 的虚函数表
 * @return 指向初始化完成的 XVtable 指针
 */
XVtable* XMqttServerConnectionProperties_class_init(void);

/**
 * @brief 创建服务端连接属性实例
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttServerConnectionProperties* XMqttServerConnectionProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建服务端连接属性的深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttServerConnectionProperties* XMqttServerConnectionProperties_create_copy(const XMqttServerConnectionProperties* other);

/**
 * @brief 初始化服务端连接属性实例
 * @param prop 待初始化的实例指针（非 NULL）
 */
void XMqttServerConnectionProperties_init(XMqttServerConnectionProperties* prop);

/**
 * @brief 获取已设置的属性位标志
 * @param prop 实例指针
 * @return 已设置的属性位标志（XMqttServerConnectionProperties_Detail 位组合）
 */
uint32_t XMqttServerConnectionProperties_availableProperties(const XMqttServerConnectionProperties* prop);

/**
 * @brief 判断服务端连接属性是否有效
 * @param prop 实例指针
 * @return 有效返回 true
 */
bool XMqttServerConnectionProperties_isValid(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取最大 QoS
 * @param prop 实例指针
 * @return 最大 QoS 等级
 */
uint8_t XMqttServerConnectionProperties_maximumQoS(const XMqttServerConnectionProperties* prop);

/**
 * @brief 是否支持保留消息
 * @param prop 实例指针
 * @return 支持返回 true
 */
bool XMqttServerConnectionProperties_retainAvailable(const XMqttServerConnectionProperties* prop);

/**
 * @brief 是否分配了客户端 ID
 * @param prop 实例指针
 * @return 已分配返回 true
 */
bool XMqttServerConnectionProperties_clientIdAssigned(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取原因字符串（常量引用）
 * @param prop 实例指针
 * @return 原因字符串（常量引用，调用者不应释放）
 */
const XString* XMqttServerConnectionProperties_reason_const(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取原因字符串（深拷贝）
 * @param prop 实例指针
 * @return 原因字符串的深拷贝，调用者负责释放
 */
XString* XMqttServerConnectionProperties_reason(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取原因码
 * @param prop 实例指针
 * @return 原因码（XMqtt_ReasonCode）
 */
uint8_t XMqttServerConnectionProperties_reasonCode(const XMqttServerConnectionProperties* prop);

/**
 * @brief 是否支持通配符
 * @param prop 实例指针
 * @return 支持返回 true
 */
bool XMqttServerConnectionProperties_wildcardSupported(const XMqttServerConnectionProperties* prop);

/**
 * @brief 是否支持订阅标识符
 * @param prop 实例指针
 * @return 支持返回 true
 */
bool XMqttServerConnectionProperties_subscriptionIdentifierSupported(const XMqttServerConnectionProperties* prop);

/**
 * @brief 是否支持共享订阅
 * @param prop 实例指针
 * @return 支持返回 true
 */
bool XMqttServerConnectionProperties_sharedSubscriptionSupported(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取服务端保活时间
 * @param prop 实例指针
 * @return 服务端保活时间（秒）
 */
uint16_t XMqttServerConnectionProperties_serverKeepAlive(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取响应信息（常量引用）
 * @param prop 实例指针
 * @return 响应信息字符串（常量引用，调用者不应释放）
 */
const XString* XMqttServerConnectionProperties_responseInformation_const(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取响应信息（深拷贝）
 * @param prop 实例指针
 * @return 响应信息字符串的深拷贝，调用者负责释放
 */
XString* XMqttServerConnectionProperties_responseInformation(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取服务端引用（常量引用）
 * @param prop 实例指针
 * @return 服务端引用字符串（常量引用，调用者不应释放）
 */
const XString* XMqttServerConnectionProperties_serverReference_const(const XMqttServerConnectionProperties* prop);

/**
 * @brief 获取服务端引用（深拷贝）
 * @param prop 实例指针
 * @return 服务端引用字符串的深拷贝，调用者负责释放
 */
XString* XMqttServerConnectionProperties_serverReference(const XMqttServerConnectionProperties* prop);


#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttConnectionProperties_create
#define XMqttConnectionProperties_create() XMqttConnectionProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XMqttLastWillProperties_create
#define XMqttLastWillProperties_create() XMqttLastWillProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XMqttServerConnectionProperties_create
#define XMqttServerConnectionProperties_create() XMqttServerConnectionProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XMQTT_PROPERTIES_ON */
#endif /* XMQTT_ON */

#endif // XMQTTCONNECTIONPROPERTIES_H
