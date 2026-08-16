#ifndef XMQTTGLOBAL_H
#define XMQTTGLOBAL_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttGlobal.h
 * @brief MQTT 全局枚举和常量定义（对齐 Qt 6.8 QMqtt）
 * @details 定义 MQTT 协议相关的枚举类型，包括载荷格式指示、消息状态和原因码。
 */

/**
 * @brief 载荷格式指示枚举
 * @details 对齐 Qt 6.8 QMqtt::PayloadFormatIndicator
 */
typedef enum {
    XMqtt_PayloadFormatIndicator_Unspecified = 0,  ///< 未指定格式
    XMqtt_PayloadFormatIndicator_UTF8Encoded = 1   ///< UTF-8 编码
} XMqtt_PayloadFormatIndicator;

/**
 * @brief 消息状态枚举
 * @details 对齐 Qt 6.8 QMqtt::MessageStatus
 */
typedef enum {
    XMqtt_MessageStatus_Unknown = 0,       ///< 未知状态
    XMqtt_MessageStatus_Published,         ///< 已发布
    XMqtt_MessageStatus_Acknowledged,      ///< 已确认
    XMqtt_MessageStatus_Received,          ///< 已接收
    XMqtt_MessageStatus_Released,          ///< 已释放
    XMqtt_MessageStatus_Completed          ///< 已完成
} XMqtt_MessageStatus;

/**
 * @brief MQTT 5.0 原因码枚举
 * @details 对齐 Qt 6.8 QMqtt::ReasonCode
 */
typedef enum {
    XMqtt_ReasonCode_Success = 0,
    XMqtt_ReasonCode_SubscriptionQoSLevel0 = 0,
    XMqtt_ReasonCode_SubscriptionQoSLevel1 = 0x01,
    XMqtt_ReasonCode_SubscriptionQoSLevel2 = 0x02,
    XMqtt_ReasonCode_NoMatchingSubscriber = 0x10,
    XMqtt_ReasonCode_NoSubscriptionExisted = 0x11,
    XMqtt_ReasonCode_ContinueAuthentication = 0x18,
    XMqtt_ReasonCode_ReAuthenticate = 0x19,
    XMqtt_ReasonCode_UnspecifiedError = 0x80,
    XMqtt_ReasonCode_MalformedPacket = 0x81,
    XMqtt_ReasonCode_ProtocolError = 0x82,
    XMqtt_ReasonCode_ImplementationSpecificError = 0x83,
    XMqtt_ReasonCode_UnsupportedProtocolVersion = 0x84,
    XMqtt_ReasonCode_InvalidClientId = 0x85,
    XMqtt_ReasonCode_InvalidUserNameOrPassword = 0x86,
    XMqtt_ReasonCode_NotAuthorized = 0x87,
    XMqtt_ReasonCode_ServerNotAvailable = 0x88,
    XMqtt_ReasonCode_ServerBusy = 0x89,
    XMqtt_ReasonCode_ClientBanned = 0x8A,
    XMqtt_ReasonCode_InvalidAuthenticationMethod = 0x8C,
    XMqtt_ReasonCode_InvalidTopicFilter = 0x8F,
    XMqtt_ReasonCode_InvalidTopicName = 0x90,
    XMqtt_ReasonCode_MessageIdInUse = 0x91,
    XMqtt_ReasonCode_MessageIdNotFound = 0x92,
    XMqtt_ReasonCode_PacketTooLarge = 0x95,
    XMqtt_ReasonCode_QuotaExceeded = 0x97,
    XMqtt_ReasonCode_InvalidPayloadFormat = 0x99,
    XMqtt_ReasonCode_RetainNotSupported = 0x9A,
    XMqtt_ReasonCode_QoSNotSupported = 0x9B,
    XMqtt_ReasonCode_UseAnotherServer = 0x9C,
    XMqtt_ReasonCode_ServerMoved = 0x9D,
    XMqtt_ReasonCode_SharedSubscriptionsNotSupported = 0x9E,
    XMqtt_ReasonCode_ExceededConnectionRate = 0x9F,
    XMqtt_ReasonCode_SubscriptionIdsNotSupported = 0xA1,
    XMqtt_ReasonCode_WildCardSubscriptionsNotSupported = 0xA2
} XMqtt_ReasonCode;

/**
 * @brief MQTT 报文固定头第一字节联合体（位域解析）。
 * @details 用于以位域方式读取或构造 MQTT 报文固定头的第一个字节，避免手工
 *          移位/掩码。线上字节与各标志位的对应关系：
 *          - bits.retain：bit0，RETAIN 标志；
 *          - bits.qos：bit1-2，QoS 等级（0/1/2）；
 *          - bits.dup：bit3，DUP 重发标志；
 *          - bits.type：bit4-7，报文类型（低 4 位为 0x0 的报文类型常量，
 *            例如 PUBLISH 的类型值为 0x3）。
 * @note 位域声明顺序依赖小端序实现（x86/ARM 等主流平台均为小端，首个字段
 *       占据最低位）；跨大端平台移植时需重新核对位序。
 * @see XMqttProtocol.c、XMqttServer.c 中 MQTT_* 报文类型常量。
 */
typedef union XMqttFixedHeader {
    uint8_t byte;                       ///< 固定头原始字节（线上格式）
    struct {
        uint8_t retain : 1;             ///< bit0：RETAIN 标志
        uint8_t qos : 2;                ///< bit1-2：QoS 等级（0/1/2）
        uint8_t dup : 1;                ///< bit3：DUP 重发标志
        uint8_t type : 4;               ///< bit4-7：报文类型
    } bits;
} XMqttFixedHeader;

#ifdef __cplusplus
}
#endif

#endif // XMQTTGLOBAL_H
