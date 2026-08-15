#ifndef XMQTTCONNECTIONPROPERTIES_H
#define XMQTTCONNECTIONPROPERTIES_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XByteArray.h"
#include "XMqttGlobal.h"
#include "XMqttType.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttConnectionProperties.h
 * @brief MQTT 连接相关属性（对齐 Qt 6.8 QMqttConnectionProperties / QMqttLastWillProperties / QMqttServerConnectionProperties）
 */

/* ---------- XMqttLastWillProperties ---------- */

XCLASS_DEFINE_BEGING(XMqttLastWillProperties)
XCLASS_DEFINE_EXTEND_END(XMqttLastWillProperties, XClass)

/**
 * @brief 遗嘱消息属性结构体
 * @details 对齐 Qt 6.8 QMqttLastWillProperties
 */
typedef struct XMqttLastWillProperties {
    XClass m_class;                       ///< 基类
    uint32_t m_willDelayInterval;         ///< 遗嘱延迟间隔（秒）
    uint8_t m_payloadFormatIndicator;     ///< 载荷格式指示
    uint32_t m_messageExpiryInterval;     ///< 消息过期时间
    XString* m_contentType;               ///< 内容类型
    XString* m_responseTopic;             ///< 响应主题
    XByteArray* m_correlationData;        ///< 关联数据
    XMqttUserProperties* m_userProperties; ///< 用户属性
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

#define XMqttLastWillProperties_copy_base   XClass_copy_base
#define XMqttLastWillProperties_move_base   XClass_move_base
#define XMqttLastWillProperties_deinit_base XClass_deinit_base
#define XMqttLastWillProperties_delete_base XClass_delete_base

/* ---------- XMqttConnectionProperties ---------- */

XCLASS_DEFINE_BEGING(XMqttConnectionProperties)
XCLASS_DEFINE_EXTEND_END(XMqttConnectionProperties, XClass)

/**
 * @brief 连接属性结构体
 * @details 对齐 Qt 6.8 QMqttConnectionProperties
 */
typedef struct XMqttConnectionProperties {
    XClass m_class;                       ///< 基类
    uint32_t m_sessionExpiryInterval;     ///< 会话过期时间
    uint16_t m_maximumReceive;            ///< 最大接收数
    uint32_t m_maximumPacketSize;         ///< 最大报文大小
    uint16_t m_maximumTopicAlias;         ///< 最大主题别名
    bool m_requestResponseInformation;    ///< 请求响应信息
    bool m_requestProblemInformation;     ///< 请求问题信息
    XMqttUserProperties* m_userProperties; ///< 用户属性
    XString* m_authenticationMethod;      ///< 认证方法
    XByteArray* m_authenticationData;     ///< 认证数据
} XMqttConnectionProperties;

XVtable* XMqttConnectionProperties_class_init(void);
XMqttConnectionProperties* XMqttConnectionProperties_create_ex(XMemoryType memory);
XMqttConnectionProperties* XMqttConnectionProperties_create_copy(const XMqttConnectionProperties* other);
void XMqttConnectionProperties_init(XMqttConnectionProperties* prop);

uint32_t XMqttConnectionProperties_sessionExpiryInterval(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setSessionExpiryInterval(XMqttConnectionProperties* prop, uint32_t expiry);
uint16_t XMqttConnectionProperties_maximumReceive(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setMaximumReceive(XMqttConnectionProperties* prop, uint16_t maxRecv);
uint32_t XMqttConnectionProperties_maximumPacketSize(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setMaximumPacketSize(XMqttConnectionProperties* prop, uint32_t packetSize);
uint16_t XMqttConnectionProperties_maximumTopicAlias(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setMaximumTopicAlias(XMqttConnectionProperties* prop, uint16_t alias);
bool XMqttConnectionProperties_requestResponseInformation(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setRequestResponseInformation(XMqttConnectionProperties* prop, bool response);
bool XMqttConnectionProperties_requestProblemInformation(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setRequestProblemInformation(XMqttConnectionProperties* prop, bool problem);
const XMqttUserProperties* XMqttConnectionProperties_userProperties_const(const XMqttConnectionProperties* prop);
XMqttUserProperties* XMqttConnectionProperties_userProperties(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setUserProperties(XMqttConnectionProperties* prop, const XMqttUserProperties* props);
const XString* XMqttConnectionProperties_authenticationMethod_const(const XMqttConnectionProperties* prop);
XString* XMqttConnectionProperties_authenticationMethod(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setAuthenticationMethod(XMqttConnectionProperties* prop, const char* authMethod);
const XByteArray* XMqttConnectionProperties_authenticationData_const(const XMqttConnectionProperties* prop);
XByteArray* XMqttConnectionProperties_authenticationData(const XMqttConnectionProperties* prop);
void XMqttConnectionProperties_setAuthenticationData(XMqttConnectionProperties* prop, const uint8_t* data, size_t len);

#define XMqttConnectionProperties_copy_base   XClass_copy_base
#define XMqttConnectionProperties_move_base   XClass_move_base
#define XMqttConnectionProperties_deinit_base XClass_deinit_base
#define XMqttConnectionProperties_delete_base XClass_delete_base

/* ---------- XMqttServerConnectionProperties ---------- */

/**
 * @brief 服务端连接属性细节枚举（位标志）
 */
typedef enum {
    XMqttServerConnectionProperties_None                          = 0x00000000,
    XMqttServerConnectionProperties_SessionExpiryInterval         = 0x00000001,
    XMqttServerConnectionProperties_MaximumReceive                = 0x00000002,
    XMqttServerConnectionProperties_MaximumQoS                    = 0x00000004,
    XMqttServerConnectionProperties_RetainAvailable               = 0x00000010,
    XMqttServerConnectionProperties_MaximumPacketSize             = 0x00000020,
    XMqttServerConnectionProperties_AssignedClientId              = 0x00000040,
    XMqttServerConnectionProperties_MaximumTopicAlias             = 0x00000080,
    XMqttServerConnectionProperties_ReasonString                  = 0x00000100,
    XMqttServerConnectionProperties_UserProperty                  = 0x00000200,
    XMqttServerConnectionProperties_WildCardSupported             = 0x00000400,
    XMqttServerConnectionProperties_SubscriptionIdentifierSupport = 0x00000800,
    XMqttServerConnectionProperties_SharedSubscriptionSupport     = 0x00001000,
    XMqttServerConnectionProperties_ServerKeepAlive               = 0x00002000,
    XMqttServerConnectionProperties_ResponseInformation           = 0x00004000,
    XMqttServerConnectionProperties_ServerReference               = 0x00008000,
    XMqttServerConnectionProperties_AuthenticationMethod          = 0x00010000,
    XMqttServerConnectionProperties_AuthenticationData            = 0x00020000
} XMqttServerConnectionProperties_Detail;

XCLASS_DEFINE_BEGING(XMqttServerConnectionProperties)
XCLASS_DEFINE_EXTEND_END(XMqttServerConnectionProperties, XMqttConnectionProperties)

/**
 * @brief 服务端连接属性结构体
 * @details 对齐 Qt 6.8 QMqttServerConnectionProperties，继承自 XMqttConnectionProperties
 */
typedef struct XMqttServerConnectionProperties {
    XMqttConnectionProperties m_base;        ///< 继承自连接属性基类
    bool m_valid;                            ///< 是否已收到 CONNACK
    uint32_t m_availableProperties;          ///< 已设置的属性位标志
    uint8_t m_maximumQoS;                    ///< 最大 QoS
    bool m_retainAvailable;                  ///< 是否支持保留消息
    bool m_clientIdAssigned;                 ///< 是否分配了客户端 ID
    XString* m_reason;                       ///< 原因字符串
    uint8_t m_reasonCode;                    ///< 原因码
    bool m_wildcardSupported;                ///< 是否支持通配符
    bool m_subscriptionIdentifierSupported;  ///< 是否支持订阅标识符
    bool m_sharedSubscriptionSupported;      ///< 是否支持共享订阅
    uint16_t m_serverKeepAlive;              ///< 服务端保活时间
    XString* m_responseInformation;          ///< 响应信息
    XString* m_serverReference;              ///< 服务端引用
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
 * @return 已设置的属性位标志
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

#define XMqttServerConnectionProperties_copy_base   XClass_copy_base
#define XMqttServerConnectionProperties_move_base   XClass_move_base
#define XMqttServerConnectionProperties_deinit_base XClass_deinit_base
#define XMqttServerConnectionProperties_delete_base XClass_delete_base

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

#endif // XMQTTCONNECTIONPROPERTIES_H
