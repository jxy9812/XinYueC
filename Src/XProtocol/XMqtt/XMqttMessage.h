#ifndef XMQTTMESSAGE_H
#define XMQTTMESSAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XByteArray.h"
#include "XMqttTopicName.h"
#include "XMqttPublishProperties.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttMessage.h
 * @brief MQTT 消息（对齐 Qt 6.8 QMqttMessage）
 * @details 表示一条完整的 MQTT 消息，包含主题、载荷、QoS、保留标志等。
 */

/**
 * @brief MQTT 消息结构体
 * @details 对齐 Qt 6.8 QMqttMessage，基于 XClass 实现值类型语义。
 */
typedef struct XMqttMessage {
    XClass m_class;                         ///< 基类
    XMqttTopicName* m_topic;                ///< 主题
    XByteArray* m_payload;                  ///< 消息载荷
    uint16_t m_id;                          ///< 消息 ID
    uint8_t m_qos;                          ///< 服务质量（0/1/2）
    bool m_duplicate;                       ///< 是否重复投递
    bool m_retain;                          ///< 是否保留
    XMqttPublishProperties* m_publishProperties; ///< 发布属性
} XMqttMessage;

/**
 * @brief 初始化虚函数表
 */
XVtable* XMqttMessage_class_init(void);

/**
 * @brief 创建空消息
 */
XMqttMessage* XMqttMessage_create(void);

/**
 * @brief 创建完整消息
 * @param topic 主题名称
 * @param payload 载荷数据
 * @param payloadLen 载荷长度
 * @param id 消息 ID
 * @param qos QoS 等级
 * @param dup 是否重复
 * @param retain 是否保留
 */
XMqttMessage* XMqttMessage_create_full(const char* topic, const uint8_t* payload, size_t payloadLen,
                                        uint16_t id, uint8_t qos, bool dup, bool retain);

/**
 * @brief 创建深拷贝
 */
XMqttMessage* XMqttMessage_create_copy(const XMqttMessage* other);

/**
 * @brief 初始化空消息
 */
void XMqttMessage_init(XMqttMessage* msg);

/**
 * @brief 初始化完整消息
 */
void XMqttMessage_init_full(XMqttMessage* msg, const char* topic, const uint8_t* payload, size_t payloadLen,
                             uint16_t id, uint8_t qos, bool dup, bool retain);

/**
 * @brief 获取载荷（常量引用）
 */
const XByteArray* XMqttMessage_payload_const(const XMqttMessage* msg);

/**
 * @brief 获取载荷（深拷贝）
 */
XByteArray* XMqttMessage_payload(const XMqttMessage* msg);

/**
 * @brief 获取 QoS
 */
uint8_t XMqttMessage_qos(const XMqttMessage* msg);

/**
 * @brief 获取消息 ID
 */
uint16_t XMqttMessage_id(const XMqttMessage* msg);

/**
 * @brief 获取主题（深拷贝）
 */
XMqttTopicName* XMqttMessage_topic(const XMqttMessage* msg);

/**
 * @brief 获取主题（常量引用）
 */
const XMqttTopicName* XMqttMessage_topic_const(const XMqttMessage* msg);

/**
 * @brief 是否重复
 */
bool XMqttMessage_duplicate(const XMqttMessage* msg);

/**
 * @brief 是否保留
 */
bool XMqttMessage_retain(const XMqttMessage* msg);

/**
 * @brief 获取发布属性（深拷贝）
 */
XMqttPublishProperties* XMqttMessage_publishProperties(const XMqttMessage* msg);

/**
 * @brief 获取发布属性（常量引用）
 */
const XMqttPublishProperties* XMqttMessage_publishProperties_const(const XMqttMessage* msg);

/**
 * @brief 比较两个消息是否相等
 */
bool XMqttMessage_equal(const XMqttMessage* a, const XMqttMessage* b);

#define XMqttMessage_copy_base   XClass_copy_base
#define XMqttMessage_move_base   XClass_move_base
#define XMqttMessage_deinit_base XClass_deinit_base
#define XMqttMessage_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif

#endif // XMQTTMESSAGE_H
