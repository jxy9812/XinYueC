/** @file       XLink_config.h
 * @brief      XLink 私有应用层协议配置文件。
 *
 * 通过此配置文件裁剪 XLink 协议的内部子功能：
 *   1. XLINK_CODEC_ON   - 帧编解码（XLinkCodec：小端序列化 + 帧头打包/校验）
 *   2. XLINK_SESSION_ON - 会话层（XLinkSession：收发/分发/订阅/待应答/心跳）
 *   3. XLINK_SSL_ON     - TLS 扩展（经 XSslSocket 为链路提供加密传输）
 *
 * XLink 是 XinYueC 自有的应用层私有协议，不依赖任何公开协议规范（对比同目录
 * XMqtt/XModbus/XFtp 等标准协议实现）。设计目标：两端对称、传输无关、通用可扩展
 * ——用于「控制进程 <-> GUI 进程」等自研程序之间的消息互通：
 *   - 消息类正交：Request/Response（请求-响应）、Publish（单推）、Subscribe/
 *     Unsubscribe（订阅），任意一端既是请求方也是服务方、既可发布也可订阅；
 *   - 帧编解码与传输解耦：会话层只见 XIODevice*（TCP/TLS/回环/自定义管道一视
 *     同仁），换传输不动协议；
 *   - 加密是传输层扩展而非协议层特性：开启 XLINK_SSL_ON 后由 XSslSocket 承载，
 *     协议层仅通过 Hello 能力位 XLinkCapability_Tls 声明"链路已加密"，
 *     应用层据此决定是否可明文交互；
 *   - 业务零侵入：新增数据形态 = 加一个 topic 或命令号，框架层不改。
 *
 * 协议总开关 XLINK_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 */
#ifndef XLINK_CONFIG_H
#define XLINK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XLINK_ON
#define XLINK_ON XPROTOCOL_ON
#endif

#if XLINK_ON

/* ========================================================================== */
/*                        子功能开关                                         */
/* ========================================================================== */

/** @brief 帧编解码（XLinkCodec.h/.c）。会话层强依赖，开则随会话层启用。 */
#ifndef XLINK_CODEC_ON
#define XLINK_CODEC_ON 1
#endif

/** @brief 会话层（XLinkSession.h/.c）：收发、分发、订阅表、待应答表、心跳。 */
#ifndef XLINK_SESSION_ON
#define XLINK_SESSION_ON 1
#endif

/** @brief TLS 加密扩展：开启后 XLink 可把会话挂在 XSslSocket 上跑加密链路。
 *  @note 加密由传输层（XSslSocket + mbedTLS）承担，XLink 协议层不实现任何
 *        密码学；本开关只决定 XLink 是否感知/声明 TLS 能力位
 *        （XLinkCapability_Tls）。关闭时仍可由调用方自行挂任意 XIODevice
 *        （含调用方自建的 TLS 设备），不影响协议功能完整性。 */
#ifndef XLINK_SSL_ON
#define XLINK_SSL_ON 1
#endif

/** @brief 服务端包装（XLinkServer.h/.c）：内嵌 XTcpServer 监听，新连接自动
 *         建会话（套能力位/分发回调模板），Inactive 会话自动回收。
 *  @note  依赖 XNetwork 的 XTcpServer；纯手动管理连接的用法可关闭本开关。 */
#ifndef XLINK_SERVER_ON
#define XLINK_SERVER_ON 1
#endif

/** @brief 客户端包装（XLinkClient.h/.c）：内嵌 XTcpSocket + XLinkSession，
 *         支持自动重连（默认关）。XLinkClient 任务专用，勿改（统筹预处理）。 */
#ifndef XLINK_CLIENT_ON
#define XLINK_CLIENT_ON 1
#endif

/** @brief 请求超时自动重试：默认重试次数（0 = 不重试，仅报超时）。
 *  @note  XLinkSession 任务专用，勿改（统筹预处理）。 */
#ifndef XLINK_RETRY_MAX_DEFAULT
#define XLINK_RETRY_MAX_DEFAULT 0
#endif

/** @brief 自动重试可缓存的请求载荷上限（字节）：超过此长度的请求超时后
 *         不重试（直接报超时），避免定容待应答表膨胀。 */
#ifndef XLINK_RETRY_PAYLOAD_MAX
#define XLINK_RETRY_PAYLOAD_MAX 128
#endif

/** @brief 请求自动超时判定阈值（毫秒）；0 = 被动模式（默认，超时不由
 *         库自动处理）。运行时可经 XLinkSession_setRequestTimeout 调整，
 *         终局结果经 requestFinished 信号上报。 */
#ifndef XLINK_REQUEST_TIMEOUT_MS
#define XLINK_REQUEST_TIMEOUT_MS 0
#endif

/* ========================================================================== */
/*                        协议常量                                           */
/* ========================================================================== */

/** @brief 协议版本号（写入 Hello 载荷，线上为 u16）。高字节主版本、低字节次版本。
 *  @note 版本不匹配不致命：会话层仅记录到 m_remoteVersion，兼容策略由应用层定。 */
#define XLINK_VERSION 0x0100u

/** @brief 帧头固定长度（字节）：payloadLen(4) + msgClass(2) + msgCode(2) + requestId(4)。
 *  @note 全部字段小端线序（fbdev/网络栈惯例，ARM 与 x86 一致）。 */
#define XLINK_HEADER_SIZE 12u

/** @brief 业务 topicId 起始值（u32）。低于此值保留给协议自身（如会话子类型）。
 *  @note  topicId 用 u32 而非字符串：嵌入式端免去解析与分配。两端各自的业务
 *         topic 从 0x10000 起分配，框架层不解释其含义。 */
#define XLINK_TOPIC_USER_BASE 0x00010000u

/** @brief 单帧载荷上限（字节）。超过视为协议违例，会话层判线。
 *  @note  同时是会话层接收缓冲的容量依据，默认取保守值：嵌入式端（如 RK3506
 *         128MB 级）单帧不宜超过 16KB，超大内容应走分片或文件通道
 *         （XUpgrade 等）而非塞进协议帧。 */
#ifndef XLINK_MAX_PAYLOAD
#define XLINK_MAX_PAYLOAD (16u * 1024u)
#endif

/** @brief 待应答表槽位（定容，稳态零堆分配）。 */
#ifndef XLINK_PENDING_SLOTS
#define XLINK_PENDING_SLOTS 8
#endif

/** @brief 订阅表槽位（定容，稳态零堆分配）。 */
#ifndef XLINK_TOPIC_SLOTS
#define XLINK_TOPIC_SLOTS 16
#endif

/** @brief 心跳发送间隔（毫秒）；0 表示不发心跳（仅被动判线）。 */
#ifndef XLINK_HEARTBEAT_INTERVAL_MS
#define XLINK_HEARTBEAT_INTERVAL_MS 1000u
#endif

/** @brief 心跳静默判线阈值（毫秒），超过即认定链路断开。 */
#ifndef XLINK_HEARTBEAT_TIMEOUT_MS
#define XLINK_HEARTBEAT_TIMEOUT_MS 3000u
#endif

/** @brief 异步驱动节拍（毫秒）：会话挂载传输后经 XObject 内建定时器
 *         （XObject_startTimer_ms）周期驱动的间隔，用于心跳发送与静默判线。
 *  @note  心跳仍按 XLINK_HEARTBEAT_INTERVAL_MS 的间隔门限发送，本节拍只决定
 *         判线/心跳检查的密度——默认 250ms 使 3 秒静默超时的判定误差不超过
 *         一个节拍。置 0 表示不启动定时器（收包仍由 readyRead 事件驱动，
 *         判线退化为手动 pump 模式）。 */
#ifndef XLINK_HEARTBEAT_TICK_MS
#define XLINK_HEARTBEAT_TICK_MS 250u
#endif

#endif /* XLINK_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINK_CONFIG_H */
