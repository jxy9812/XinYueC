#ifndef XMQTTSERVER_P_H
#define XMQTTSERVER_P_H

#include "XMqttServer.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XMap.h"
#include "XString.h"
#include "XMqttPublishProperties.h"
#include "XEvent.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttServer_p.h
 * @brief XMqttServer 的私有数据结构与内部协议操作。
 * @details 该头文件只供 XMqttServer.c、XMqttTcpServer.c 等实现文件使用，
 *          不属于对外公开 API。公开头文件只通过不完整类型 XMqttServerPrivate
 *          引用这里的核心结构。
 */

/**
 * @brief MQTT 5.0 主题别名条目。
 * @details 用于入站（客户端->服务器）与出站（服务器->客户端）两个方向，
 *          别名取值范围 1..65535，0 不作为有效别名。
 */
typedef struct XMqttServerTopicAlias {
    uint16_t alias;                ///< 主题别名编号，0 表示无效。
    XString* topic;                ///< 与别名绑定的主题名，条目拥有其所有权。
} XMqttServerTopicAlias;

/**
 * @brief 一条会话订阅。
 * @details filter 始终保存客户端发来的原始过滤器（含 $share/... 前缀）；
 *          actualFilter 保存去掉共享订阅前缀后的真实过滤器，普通订阅时
 *          actualFilter 与 filter 指向同一份内容（各自独立拥有）。
 */
typedef struct XMqttServerSubscription {
    XString* filter;               ///< 原始主题过滤器（含共享订阅前缀），条目拥有。
    XString* actualFilter;         ///< 实际匹配用过滤器（共享订阅去掉前缀），条目拥有。
    XString* shareName;            ///< 共享订阅组名，普通订阅为 NULL。
    uint32_t subscriptionId;       ///< MQTT 5.0 订阅标识符，0 表示未设置。
    /* 紧凑标志位（位域优化：qos 2 bit + noLocal 1 bit + retainAsPublished 1 bit + retainHandling 2 bit） */
    uint32_t qos : 2;              ///< 订阅请求的最大 QoS（0/1/2）。
    uint32_t noLocal : 1;          ///< MQTT 5.0 noLocal：不接收自己发布的消息。
    uint32_t retainAsPublished : 1; ///< MQTT 5.0 保留原样标志。
    uint32_t retainHandling : 2;   ///< MQTT 5.0 保留消息处理选项（0/1/2）。
} XMqttServerSubscription;

/**
 * @brief 一条待投递或投递中的消息。
 * @details 持久会话离线时保存 QoS1/2 消息；在线投递时用于跟踪 QoS1/2 握手。
 *          stage：0 等待 PUBACK/PUBREC，1 已发送 PUBLISH 并收到 PUBREC（QoS2
 *          出站，等待 PUBREL 后发送 PUBCOMP —— 实际为已发送 PUBREL 等待 PUBCOMP）。
 */
typedef struct XMqttServerQueuedMessage {
    uint16_t packetId;             ///< 出站报文标识符，0 表示尚未分配。
    XString* topic;                ///< 消息主题，条目拥有。
    XByteArray* payload;           ///< 消息载荷，条目拥有。
    XMqttPublishProperties* properties; ///< MQTT 5.0 发布属性（订阅标识符等），可空。
    /* 紧凑标志位（位域优化：qos 2 bit + retain 1 bit + stage 2 bit） */
    uint32_t qos : 2;              ///< 实际投递 QoS（0/1/2）。
    uint32_t retain : 1;           ///< 投递时是否带 RETAIN 标志。
    uint32_t stage : 2;            ///< QoS 出站握手阶段，由协议实现维护。
} XMqttServerQueuedMessage;

/**
 * @brief 一条已发布并等待延迟投递的遗嘱消息。
 * @details 连接异常断开但配置了 MQTT 5.0 遗嘱延迟时，遗嘱先保存在此结构中，
 *          定时器到期后再真正发布。
 */
typedef struct XMqttServerPendingWill {
    XTimerId timer;                ///< 遗嘱延迟定时器。
    XString* clientId;             ///< 遗嘱所属客户端 ID，用于同 ID 重连时撤销。
    XString* topic;                ///< 遗嘱主题，条目拥有。
    XByteArray* payload;           ///< 遗嘱载荷，条目拥有。
    XMqttPublishProperties* properties; ///< MQTT 5.0 遗嘱属性，可空。
    /* 紧凑标志位（位域优化：qos 2 bit + retain 1 bit） */
    uint32_t qos : 2;              ///< 遗嘱 QoS。
    uint32_t retain : 1;           ///< 遗嘱是否保留。
} XMqttServerPendingWill;

/**
 * @brief 一条保留消息。
 * @details 保留消息表按主题名保存，发布空载荷的保留消息会删除对应条目。
 */
typedef struct XMqttServerRetainedMessage {
    XString* topic;                ///< 主题名，条目拥有。
    XByteArray* payload;           ///< 消息载荷，条目拥有。
    XMqttPublishProperties* properties; ///< MQTT 5.0 发布属性，可空。
    /* 紧凑标志位（位域优化：qos 2 bit） */
    uint32_t qos : 2;              ///< 发布时的 QoS。
} XMqttServerRetainedMessage;

/**
 * @brief MQTT 持久会话（按 clientId 保存）。
 * @details 会话保存订阅表与离线 QoS1/2 消息。cleanSession=0（MQTT 5.0 为
 *          Clean Start=0 且会话过期非 0）时会话在断开后保留，供下次连接恢复。
 */
typedef struct XMqttServerSession {
    XString* clientId;             ///< 客户端 ID，会话拥有。
    XVector* subscriptions;        ///< XMqttServerSubscription 数组。
    XVector* queuedMessages;       ///< 离线/在途 XMqttServerQueuedMessage 数组。
    int64_t expireAt;              ///< 会话过期时间（自纪元毫秒），0 表示立即过期，-1 表示永久。
    XTimerId expiryTimer;          ///< 会话过期定时器，无定时器时为 XTIMER_INVALID_ID。
    struct XMqttServerClient* client; ///< 当前连接该会话的客户端，离线为 NULL（借用）。
    /* 紧凑标志位（位域优化：persistent 1 bit） */
    uint32_t persistent : 1;       ///< 断开后是否保留会话。
} XMqttServerSession;

/**
 * @brief 一条客户端连接（传输无关）。
 * @details 每个活动 TCP 连接（或测试用 mock 传输）对应一个实例。transport
 *          为借用指针，由 TCP 子类或调用方负责其生命周期。
 */
typedef struct XMqttServerClient {
    void* transport;               ///< 传输设备指针（借用，不拥有）。
    XByteArray* input;             ///< 尚未组成完整报文的入站字节缓存。
    XString* clientId;             ///< 客户端 ID，条目拥有。
    XString* assignedClientId;     ///< 服务器分配的客户端 ID（MQTT 5.0 空 ID 时经 CONNACK 回传），可空。
    XString* username;             ///< 用户名，条目拥有，可为 NULL。
    XString* password;             ///< 密码，条目拥有，可为 NULL。
    uint16_t keepAlive;            ///< 保活间隔（秒），0 表示不检查。
    uint32_t sessionExpiry;        ///< MQTT 5.0 会话过期间隔（秒）。
    uint32_t maxPacketSize;        ///< 客户端声明的最大报文大小（MQTT 5.0），0 表示不限制。
    uint16_t receiveMaximum;       ///< 客户端声明的接收最大 QoS1/2 在途报文数，默认 65535。
    uint16_t inboundAliasMaximum;   ///< 客户端声明的入站主题别名上限（MQTT 5.0），0 表示不支持。
    uint16_t nextPacketId;         ///< 下一个出站报文标识符，自动跳过 0。
    XTimerId keepAliveTimer;       ///< 保活检测定时器，无定时器时为 XTIMER_INVALID_ID。
    int64_t lastActivity;          ///< 最后一次收到报文的时间（自纪元毫秒）。
    /* MQTT 5.0 遗嘱 */
    XString* willTopic;            ///< 遗嘱主题，条目拥有，可为 NULL。
    XByteArray* willMessage;       ///< 遗嘱载荷，条目拥有，可为 NULL。
    uint32_t willDelay;            ///< MQTT 5.0 遗嘱延迟间隔（秒）。
    XMqttPublishProperties* willProperties; ///< MQTT 5.0 遗嘱属性，可空。
    /* 入站 QoS2 状态：已投递但尚未 PUBREL 的报文标识符 */
    XVector* incomingQos2;         ///< uint16_t 数组。
    /* 入站主题别名表（客户端 -> 服务器） */
    XVector* inboundAliases;       ///< XMqttServerTopicAlias 数组。
    /* 出站主题别名表（服务器 -> 客户端） */
    XVector* outboundAliases;      ///< XMqttServerTopicAlias 数组。
    XMqttServerSession* session;   ///< 当前会话（借用，由会话表拥有）。
    /* 紧凑标志位（位域优化：protocolVersion 3 bit + cleanSession 1 bit + connected 1 bit
     * + disconnectReceived 1 bit + willQoS 2 bit + willRetain 1 bit，合计 9 bit） */
    uint32_t protocolVersion : 3;  ///< 协议版本（3/4/5）。
    uint32_t cleanSession : 1;     ///< 清洁会话/清洁开始标志。
    uint32_t connected : 1;        ///< CONNECT 是否已处理完成。
    uint32_t disconnectReceived : 1; ///< 是否已收到 DISCONNECT 报文。
    uint32_t willQoS : 2;          ///< 遗嘱 QoS。
    uint32_t willRetain : 1;       ///< 遗嘱是否保留。
} XMqttServerClient;

/**
 * @brief XMqttServer 的私有运行时状态。
 * @warning 该结构不是公开 API，字段布局可随协议实现变化。
 */
typedef struct XMqttServerPrivate {

    XMap* m_clients;               ///< transport(void*) -> XMqttServerClient*。
    XMap* m_sessions;              ///< clientId(XString*) -> XMqttServerSession*。
    XMap* m_retained;              ///< topic(XString*) -> XMqttServerRetainedMessage*。
    XMap* m_sharedIndex;           ///< 共享订阅轮转索引（XString* -> uint32_t）。
    XVector* m_pendingWills;       ///< XMqttServerPendingWill 数组。
    XTimerId m_purgeTimer;         ///< 过期会话清理定时器，无效时为 XTIMER_INVALID_ID。
    /* 认证回调 */
    void* m_authContext;           ///< 认证回调上下文（借用）。
    XMqttServer_Authenticator m_authenticator; ///< 认证回调，可空。
} XMqttServerPrivate;

#ifdef __cplusplus
}
#endif

#endif /* XMQTTSERVER_P_H */
