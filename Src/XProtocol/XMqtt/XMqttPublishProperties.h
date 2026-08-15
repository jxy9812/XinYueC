#ifndef XMQTTPUBLISHPROPERTIES_H
#define XMQTTPUBLISHPROPERTIES_H

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
 * @file XMqttPublishProperties.h
 * @brief MQTT PUBLISH 报文属性（对齐 Qt 6.8 QMqttPublishProperties）
 * @details 定义 PUBLISH 报文的属性集合，包括载荷格式、消息过期时间、主题别名等。
 */

/**
 * @brief PUBLISH 属性细节枚举（位标志）
 * @details 对齐 Qt 6.8 QMqttPublishProperties::PublishPropertyDetail
 */
typedef enum {
    XMqttPublishProperties_None                   = 0x00000000,
    XMqttPublishProperties_PayloadFormatIndicator = 0x00000001,
    XMqttPublishProperties_MessageExpiryInterval  = 0x00000002,
    XMqttPublishProperties_TopicAlias             = 0x00000004,
    XMqttPublishProperties_ResponseTopic          = 0x00000008,
    XMqttPublishProperties_CorrelationData        = 0x00000010,
    XMqttPublishProperties_UserProperty           = 0x00000020,
    XMqttPublishProperties_SubscriptionIdentifier = 0x00000040,
    XMqttPublishProperties_ContentType            = 0x00000080
} XMqttPublishProperties_Detail;

XCLASS_DEFINE_BEGING(XMqttPublishProperties)
XCLASS_DEFINE_EXTEND_END(XMqttPublishProperties, XClass)

/**
 * @brief PUBLISH 属性结构体
 * @details 对齐 Qt 6.8 QMqttPublishProperties，基于 XClass 实现值类型语义。
 */
typedef struct XMqttPublishProperties {
    XClass m_class;                                    ///< 基类
    uint32_t m_availableProperties;                    ///< 已设置的属性位标志
    uint8_t m_payloadFormatIndicator;                  ///< 载荷格式指示（XMqtt_PayloadFormatIndicator）
    uint32_t m_messageExpiryInterval;                  ///< 消息过期时间（秒）
    uint16_t m_topicAlias;                             ///< 主题别名
    XString* m_responseTopic;                          ///< 响应主题
    XByteArray* m_correlationData;                     ///< 关联数据
    XMqttUserProperties* m_userProperties;             ///< 用户属性列表
    XVector* m_subscriptionIdentifiers;                ///< 订阅标识符列表（XVector<uint32_t>）
    XString* m_contentType;                            ///< 内容类型
} XMqttPublishProperties;

/**
 * @brief 初始化虚函数表
 */
XVtable* XMqttPublishProperties_class_init(void);

/**
 * @brief 创建实例
 */
XMqttPublishProperties* XMqttPublishProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建深拷贝
 */
XMqttPublishProperties* XMqttPublishProperties_create_copy(const XMqttPublishProperties* other);

/**
 * @brief 初始化实例
 */
void XMqttPublishProperties_init(XMqttPublishProperties* prop);

/**
 * @brief 获取已设置的属性位标志
 */
uint32_t XMqttPublishProperties_availableProperties(const XMqttPublishProperties* prop);

/**
 * @brief 获取/设置载荷格式指示
 */
uint8_t XMqttPublishProperties_payloadFormatIndicator(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setPayloadFormatIndicator(XMqttPublishProperties* prop, uint8_t indicator);

/**
 * @brief 获取/设置消息过期时间
 */
uint32_t XMqttPublishProperties_messageExpiryInterval(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setMessageExpiryInterval(XMqttPublishProperties* prop, uint32_t interval);

/**
 * @brief 获取/设置主题别名
 */
uint16_t XMqttPublishProperties_topicAlias(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setTopicAlias(XMqttPublishProperties* prop, uint16_t alias);

/**
 * @brief 获取/设置响应主题
 */
const XString* XMqttPublishProperties_responseTopic_const(const XMqttPublishProperties* prop);
XString* XMqttPublishProperties_responseTopic(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setResponseTopic(XMqttPublishProperties* prop, const char* topic);

/**
 * @brief 获取/设置关联数据
 */
const XByteArray* XMqttPublishProperties_correlationData_const(const XMqttPublishProperties* prop);
XByteArray* XMqttPublishProperties_correlationData(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setCorrelationData(XMqttPublishProperties* prop, const uint8_t* data, size_t len);

/**
 * @brief 获取/设置用户属性
 */
const XMqttUserProperties* XMqttPublishProperties_userProperties_const(const XMqttPublishProperties* prop);
XMqttUserProperties* XMqttPublishProperties_userProperties(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setUserProperties(XMqttPublishProperties* prop, const XMqttUserProperties* props);

/**
 * @brief 获取/设置订阅标识符列表
 */
const XVector* XMqttPublishProperties_subscriptionIdentifiers_const(const XMqttPublishProperties* prop);
XVector* XMqttPublishProperties_subscriptionIdentifiers(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setSubscriptionIdentifiers(XMqttPublishProperties* prop, const XVector* ids);

/**
 * @brief 获取/设置内容类型
 */
const XString* XMqttPublishProperties_contentType_const(const XMqttPublishProperties* prop);
XString* XMqttPublishProperties_contentType(const XMqttPublishProperties* prop);
void XMqttPublishProperties_setContentType(XMqttPublishProperties* prop, const char* type);

#define XMqttPublishProperties_copy_base   XClass_copy_base
#define XMqttPublishProperties_move_base   XClass_move_base
#define XMqttPublishProperties_deinit_base XClass_deinit_base
#define XMqttPublishProperties_delete_base XClass_delete_base

/* ---------- XMqttMessageStatusProperties ---------- */

/**
 * @brief 消息状态属性结构体
 * @details 对齐 Qt 6.8 QMqttMessageStatusProperties
 */
XCLASS_DEFINE_BEGING(XMqttMessageStatusProperties)
XCLASS_DEFINE_EXTEND_END(XMqttMessageStatusProperties, XClass)

typedef struct XMqttMessageStatusProperties {
    XClass m_class;                        ///< 基类
    uint8_t m_reasonCode;                  ///< 原因码（XMqtt_ReasonCode）
    XString* m_reason;                     ///< 原因字符串
    XMqttUserProperties* m_userProperties; ///< 用户属性列表
} XMqttMessageStatusProperties;

/**
 * @brief 初始化虚函数表
 */
XVtable* XMqttMessageStatusProperties_class_init(void);

/**
 * @brief 创建实例
 */
XMqttMessageStatusProperties* XMqttMessageStatusProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建深拷贝
 */
XMqttMessageStatusProperties* XMqttMessageStatusProperties_create_copy(const XMqttMessageStatusProperties* other);

/**
 * @brief 初始化实例
 */
void XMqttMessageStatusProperties_init(XMqttMessageStatusProperties* prop);

/**
 * @brief 获取原因码
 */
uint8_t XMqttMessageStatusProperties_reasonCode(const XMqttMessageStatusProperties* prop);

/**
 * @brief 获取原因字符串
 */
const XString* XMqttMessageStatusProperties_reason_const(const XMqttMessageStatusProperties* prop);
XString* XMqttMessageStatusProperties_reason(const XMqttMessageStatusProperties* prop);

/**
 * @brief 获取用户属性
 */
const XMqttUserProperties* XMqttMessageStatusProperties_userProperties_const(const XMqttMessageStatusProperties* prop);
XMqttUserProperties* XMqttMessageStatusProperties_userProperties(const XMqttMessageStatusProperties* prop);

#define XMqttMessageStatusProperties_copy_base   XClass_copy_base
#define XMqttMessageStatusProperties_move_base   XClass_move_base
#define XMqttMessageStatusProperties_deinit_base XClass_deinit_base
#define XMqttMessageStatusProperties_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttMessageStatusProperties_create
#define XMqttMessageStatusProperties_create(...) XMqttMessageStatusProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)
#undef XMqttPublishProperties_create
#define XMqttPublishProperties_create(...) XMqttPublishProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XMQTTPUBLISHPROPERTIES_H
