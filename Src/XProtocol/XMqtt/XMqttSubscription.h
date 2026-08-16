#ifndef XMQTTSUBSCRIPTION_H
#define XMQTTSUBSCRIPTION_H

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XMqttGlobal.h"
#include "XMqttMessage.h"
#include "XMqttTopicFilter.h"
#include "XMqttType.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttSubscription.h
 * @brief MQTT 订阅（对齐 Qt 6.8 QMqttSubscription）
 * @details 表示一个 MQTT 主题订阅，继承自 XObject，管理订阅状态和消息接收。
 */

/**
 * @brief 订阅状态枚举
 * @details 对齐 Qt 6.8 QMqttSubscription::SubscriptionState
 */
typedef enum {
    XMqttSubscription_Unsubscribed = 0,          ///< 未订阅
    XMqttSubscription_SubscriptionPending,       ///< 订阅中
    XMqttSubscription_Subscribed,                ///< 已订阅
    XMqttSubscription_UnsubscriptionPending,     ///< 取消订阅中
    XMqttSubscription_Error                      ///< 错误状态
} XMqttSubscription_State;

/**
 * @brief 订阅虚函数表枚举
 */
XCLASS_DEFINE_BEGING(XMqttSubscription)
XCLASS_DEFINE_ENUM(XMqttSubscription, Unsubscribe) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XMqttSubscription)

/**
 * @brief MQTT 订阅结构体
 * @details 对齐 Qt 6.8 QMqttSubscription，继承自 XObject。
 */
typedef struct XMqttSubscription {
    XObject m_class;                              ///< 继承自 XObject
    XMqttTopicFilter* m_topic;                    ///< 订阅的主题过滤器
    XString* m_reason;                            ///< 原因字符串
    XString* m_sharedSubscriptionName;            ///< 共享订阅名称
    /* 紧凑标志位（位域优化：state 3 bit + qos 2 bit + reasonCode 8 bit + shared 1 bit） */
    uint32_t m_state : 3;                         ///< 订阅状态（XMqttSubscription_State）
    uint32_t m_qos : 2;                           ///< 订阅的 QoS
    uint32_t m_reasonCode : 8;                    ///< 原因码（XMqtt_ReasonCode）
    uint32_t m_sharedSubscription : 1;            ///< 是否为共享订阅
    XMqttUserProperties* m_userProperties;        ///< 用户属性
    void* m_client;                               ///< 关联的 XMqttClient 指针（不拥有）
} XMqttSubscription;

/**
 * @brief 初始化虚函数表
 */
XVtable* XMqttSubscription_class_init(void);

/**
 * @brief 创建订阅实例（由 XMqttClient 内部调用）
 * @param topic 主题过滤器
 * @param qos QoS 等级
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttSubscription* XMqttSubscription_create_ex(XMemoryType memory,  const XMqttTopicFilter* topic, uint8_t qos);

/**
 * @brief 初始化订阅实例
 * @param sub 待初始化的实例指针（非 NULL）
 * @param topic 主题过滤器
 * @param qos QoS 等级
 */
void XMqttSubscription_init(XMqttSubscription* sub, const XMqttTopicFilter* topic, uint8_t qos);

/**
 * @brief 获取订阅状态
 */
XMqttSubscription_State XMqttSubscription_state(const XMqttSubscription* sub);

/**
 * @brief 获取主题过滤器（深拷贝）
 */
XMqttTopicFilter* XMqttSubscription_topic(const XMqttSubscription* sub);

/**
 * @brief 获取主题过滤器（常量引用）
 */
const XMqttTopicFilter* XMqttSubscription_topic_const(const XMqttSubscription* sub);

/**
 * @brief 获取 QoS
 */
uint8_t XMqttSubscription_qos(const XMqttSubscription* sub);

/**
 * @brief 获取原因字符串（深拷贝）
 */
XString* XMqttSubscription_reason(const XMqttSubscription* sub);

/**
 * @brief 获取原因字符串（常量引用）
 */
const XString* XMqttSubscription_reason_const(const XMqttSubscription* sub);

/**
 * @brief 获取原因码
 */
uint8_t XMqttSubscription_reasonCode(const XMqttSubscription* sub);

/**
 * @brief 获取用户属性（深拷贝）
 */
XMqttUserProperties* XMqttSubscription_userProperties(const XMqttSubscription* sub);

/**
 * @brief 获取用户属性（常量引用）
 */
const XMqttUserProperties* XMqttSubscription_userProperties_const(const XMqttSubscription* sub);

/**
 * @brief 是否为共享订阅
 */
bool XMqttSubscription_isSharedSubscription(const XMqttSubscription* sub);

/**
 * @brief 获取共享订阅名称（深拷贝）
 */
XString* XMqttSubscription_sharedSubscriptionName(const XMqttSubscription* sub);

/**
 * @brief 获取共享订阅名称（常量引用）
 */
const XString* XMqttSubscription_sharedSubscriptionName_const(const XMqttSubscription* sub);

/**
 * @brief 取消订阅
 */
void XMqttSubscription_unsubscribe_base(XMqttSubscription* sub);

/* ---------- 信号 ---------- */

/**
 * @brief 状态改变信号
 */
void* XMqttSubscription_stateChanged_signal(XMqttSubscription* sub, XMqttSubscription_State state);

/**
 * @brief QoS 改变信号
 */
void* XMqttSubscription_qosChanged_signal(XMqttSubscription* sub, uint8_t qos);

/**
 * @brief 消息接收信号
 */
void* XMqttSubscription_messageReceived_signal(XMqttSubscription* sub, XMqttMessage* msg);

/* ---------- 受保护接口（供 XMqttClient 使用） ---------- */

/**
 * @brief 设置订阅状态（受保护，供 XMqttClient 使用）
 * @param sub 订阅实例指针（非 NULL）
 * @param state 新的订阅状态
 */
void XMqttSubscription_setState(XMqttSubscription* sub, XMqttSubscription_State state);

/**
 * @brief 设置 QoS（受保护，供 XMqttClient 使用）
 * @param sub 订阅实例指针（非 NULL）
 * @param qos 新的 QoS 等级
 */
void XMqttSubscription_setQos(XMqttSubscription* sub, uint8_t qos);

/**
 * @brief 设置关联的客户端（受保护，供 XMqttClient 使用）
 * @param sub 订阅实例指针（非 NULL）
 * @param client 关联的 XMqttClient 指针
 */
void XMqttSubscription_setClient(XMqttSubscription* sub, void* client);

#define XMqttSubscription_deleteLater   XObject_deleteLater
#define XMqttSubscription_deinitLater   XObject_deinitLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttSubscription_create
#define XMqttSubscription_create(...) XMqttSubscription_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XMQTTSUBSCRIPTION_H
