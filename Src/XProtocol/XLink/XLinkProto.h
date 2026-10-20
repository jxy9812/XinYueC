/** @file       XLinkProto.h
 * @brief      XLink 私有应用层协议——线上格式定义（消息类枚举/帧头/常量）。
 * @details    对齐 MQTT 3.1.1 的消息分类思想（Request/Response/Publish/Subscribe），
 *             但为 XinYueC 自有格式，不兼容任何公开协议规范。实现只依赖 XinYueC
 *             抽象层（XIODevice/XMemory），禁止调用 Win32、POSIX 或其他平台 API。
 *
 *             帧结构（小端线序，定长帧头 12 字节）：
 * @code
 *   偏移  长度  字段        说明
 *   0     4     payloadLen  载荷字节数（不含帧头 12 字节），须 <= XLINK_MAX_PAYLOAD
 *   4     2     msgClass    消息类（XLinkMsgClass），决定语义与应答义务
 *   6     2     msgCode     业务码：Request/Response = 命令号，
 *                            Publish/Subscribe/Unsubscribe = topicId 低 16 位，
 *                            Session = XLinkSessionCode
 *   8     4     requestId   Request/Response 关联 id（两端各自独立自增）；其余类填 0
 *   12    N     payload     载荷（按字段逐个小端序列化，禁止 memcpy 结构体上线）
 * @endcode
 *
 *             【topicId 承载规则】msgCode 线上仅 u16，而业务 topicId 区间自
 *             0x10000 起（已越出 u16 表示范围），故 Publish/Subscribe/Unsubscribe
 *             三类的完整 u32 topicId 一律另置于载荷首 4 字节（小端），msgCode
 *             只承载其低 16 位供快速匹配与统计；解析侧一律以载荷中的 u32 为准。
 *
 *             设计要点：
 *             - 消息类正交而非逐条枚举业务语义。Request/Response 管"我问你要答"
 *               （双向均可发起），Publish 管"我单方面告诉你"，Subscribe/Unsubscribe
 *               管"以后这类推给我"。新增业务形态只表现为新增 msgCode/topicId，
 *               协议层与编解码层零改动。
 *             - requestId 两端各自自增（互不干扰），Response 原样回带对端的 id
 *               做应答关联；待应答由请求侧本地表跟踪与超时（见 XLinkSession.h）。
 */
#ifndef XLINKPROTO_H
#define XLINKPROTO_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入本协议配置，确保 XLINK_ON 已定义（必须早于下方条件编译判定） */
#include "XLink_config.h"

#if XLINK_ON

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/*                        协议常量                                           */
/* ========================================================================== */

/** @brief 协议版本号（写入 HELLO 载荷，线上为 u16）。高字节为主版本、低字节为次版本。
 *  @note  版本不匹配不致命：会话层仅记录到 m_peerVersion，兼容策略由应用层决定。 */
#define XLINK_VERSION 0x0100u

/** @brief 帧头固定长度（字节）：payloadLen(4) + msgClass(2) + msgCode(2) + requestId(4)。
 *  @note  全部字段小端线序（fbdev/网络栈惯例，ARM 与 x86 一致）。 */
#define XLINK_HEADER_SIZE 12u

/** @brief 业务 topicId 起始值（u32）。低于此值保留给协议自身（如 Session 子类型）。
 *  @note  topicId 用 u32 而非字符串：嵌入式端免去解析与分配。两端各自的业务
 *         topic 从 0x10000 起分配，框架层不解释其含义。 */
#define XLINK_TOPIC_USER_BASE 0x00010000u

/* ========================================================================== */
/*                        消息类                                             */
/* ========================================================================== */

/**
 * @brief 消息类：帧的语义类别（线上 msgClass 字段取值）。
 * @details 数值即为线上字节值，0 起连续编号，追加新类只能往后加，不得改动既有值。
 *          非位域，不可按位组合；一帧有且只有一个消息类。
 */
typedef enum XLinkMsgClass {
    XLinkMsgClass_Session     = 0, /**< 会话管理帧（双向对称）：Hello/HelloAck/Heartbeat/Goodbye，
                                       子类型见 XLinkSessionCode。 */
    XLinkMsgClass_Request     = 1, /**< 请求帧（双向对称）：发出后必须收到同 requestId 的
                                       Response 才算闭环；待应答跟踪与超时由请求侧负责。 */
    XLinkMsgClass_Response    = 2, /**< 应答帧（双向对称）：回带对端的 requestId 与相同 msgCode，
                                       载荷承载返回值或错误码。 */
    XLinkMsgClass_Publish     = 3, /**< 推送帧（双向对称）：单方面通知，不等应答，产生即发。 */
    XLinkMsgClass_Subscribe   = 4, /**< 订阅帧（双向对称）：订阅方请求对端按 topicId 推送。 */
    XLinkMsgClass_Unsubscribe = 5  /**< 退订帧（双向对称）：取消此前建立的订阅关系。 */
} XLinkMsgClass;

/* ========================================================================== */
/*                        会话子类型                                         */
/* ========================================================================== */

/**
 * @brief 会话管理子类型：线上 msgClass=XLinkMsgClass_Session 时的 msgCode 取值。
 * @details 数值即为线上字节值。非位域，不可按位组合。
 */
typedef enum XLinkSessionCode {
    XLinkSessionCode_Hello     = 1, /**< 建链后首帧（双向对称）：携版本与能力位。 */
    XLinkSessionCode_HelloAck  = 2, /**< 对 Hello 的应答；能力位交集在此确定，
                                        约定可携带全量快照（可选载荷）。 */
    XLinkSessionCode_Heartbeat = 3, /**< 心跳（双向对称）：静默超时即认定链路断开。 */
    XLinkSessionCode_Goodbye   = 4  /**< 主动断链通知（双向对称）：正常关闭前发出，便于对端快速感知。 */
} XLinkSessionCode;

/* ========================================================================== */
/*                        能力位                                             */
/* ========================================================================== */

/**
 * @brief 能力位：Hello/HelloAck 载荷中的 m_capability（线上 u16），按位或组合。
 * @details 双方各自声明己身能力，握手后取按位与作为协商结果；未置位的特性对端
 *           不得依赖（如未协商 XLINK_CAP_Zlib 则载荷必须为原始字节）。
 */
typedef enum XLinkCapability {
    XLinkCapability_None           = 0x0000u, /**< 无任何可选能力（等价于未置任何位）。 */
    XLinkCapability_Zlib           = 0x0001u, /**< 支持 zlib 载荷压缩（协商后生效）。 */
    XLinkCapability_Tls            = 0x0002u, /**< 链路已运行在 TLS 传输之上
                                                 （由传输层保证，非本层实现）。 */
    XLinkCapability_Snapshot       = 0x0004u, /**< HelloAck 载荷内携带全量快照（重连即恢复画面）。 */
    XLinkCapability_RequestTimeout = 0x0008u  /**< 支持请求超时通知（对端待应答表到期上报）。 */
} XLinkCapability;

/* ========================================================================== */
/*                        载荷结构                                           */
/* ========================================================================== */

/**
 * @brief Hello / HelloAck 载荷。
 * @details 线上为 version(2) + capability(2) 共 4 字节小端；可选在其后追加
 *          快照字节（长度由帧头 payloadLen 推导）。禁止 memcpy 本结构体上线。
 */
typedef struct XLinkHelloPayload {
    uint16_t m_version;    /**< 协议版本，见 XLINK_VERSION。 */
    uint16_t m_capability; /**< 己身能力位，按位或，见 XLinkCapability。 */
} XLinkHelloPayload;

/**
 * @brief 解析/构造后的帧头（已脱离线上字节序，编解码见 XLinkCodec.h）。
 */
typedef struct XLinkFrameHeader {
    uint32_t m_payloadLen; /**< 载荷字节数；须 <= XLINK_MAX_PAYLOAD，解码时校验。 */
    uint16_t m_msgClass;   /**< 消息类，见 XLinkMsgClass。 */
    uint16_t m_msgCode;    /**< 业务码：Request/Response 为命令号，Publish/Subscribe
                                为 topicId，Session 为 XLinkSessionCode。 */
    uint32_t m_requestId;  /**< Request/Response 关联 id；其余消息类填 0。 */
} XLinkFrameHeader;

#endif /* XLINK_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKPROTO_H */
