/** @file XMqtt_config.h
 * @brief MQTT 协议子功能配置文件
 *
 * 通过此配置文件可以裁剪 XMqtt 协议内部的各个子功能：
 *   1. XMQTT_CLIENT_ON        - 客户端核心（XMqttClient / XMqttProtocol / XMqttType /
 *                               XMqttGlobal / XMqttMessage）
 *   2. XMQTT_SERVER_ON        - 服务器核心（XMqttServer / XMqttTcpServer）
 *   3. XMQTT_TOPIC_ON         - 主题（XMqttTopicName / XMqttTopicFilter）
 *   4. XMQTT_SUBSCRIPTION_ON  - 订阅（XMqttSubscription / XMqttSubscriptionProperties）
 *   5. XMQTT_PROPERTIES_ON    - MQTT v5 属性集（XMqttAuthenticationProperties /
 *                               XMqttConnectionProperties / XMqttPublishProperties /
 *                               XMqttSubscriptionProperties）
 *
 * 协议总开关 XMQTT_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * 主题 / 订阅 / 属性集默认随客户端核心开启而启用。
 */

#ifndef XMQTT_CONFIG_H
#define XMQTT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XMQTT_ON
#define XMQTT_ON XPROTOCOL_ON
#endif

#if XMQTT_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief 客户端核心（XMqttClient / XMqttProtocol / XMqttType / XMqttGlobal / XMqttMessage） */
#ifndef XMQTT_CLIENT_ON
#define XMQTT_CLIENT_ON 1
#endif

/** @brief 服务器核心（XMqttServer / XMqttTcpServer） */
#ifndef XMQTT_SERVER_ON
#define XMQTT_SERVER_ON XMQTT_ON
#endif

/** @brief 主题（XMqttTopicName / XMqttTopicFilter） */
#ifndef XMQTT_TOPIC_ON
#define XMQTT_TOPIC_ON (XMQTT_CLIENT_ON || XMQTT_SERVER_ON)
#endif

/** @brief 订阅（XMqttSubscription / XMqttSubscriptionProperties） */
#ifndef XMQTT_SUBSCRIPTION_ON
#define XMQTT_SUBSCRIPTION_ON (XMQTT_CLIENT_ON || XMQTT_SERVER_ON)
#endif

/** @brief MQTT v5 属性集（XMqttAuthenticationProperties / XMqttConnectionProperties / XMqttPublishProperties / XMqttSubscriptionProperties） */
#ifndef XMQTT_PROPERTIES_ON
#define XMQTT_PROPERTIES_ON (XMQTT_CLIENT_ON || XMQTT_SERVER_ON)
#endif

#endif /* XMQTT_ON */

#ifdef __cplusplus
}
#endif

#endif /* XMQTT_CONFIG_H */
