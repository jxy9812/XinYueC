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
/*                        可选项扩展开关                                     */
/* ========================================================================== */

/** @brief 登录认证扩展（XLinkAuth / XLinkUserStore）：挑战应答 + 账户库。
 *  @note  关闭时能力位 Auth 不置位、登录会话子码整体裁空，协议行为与
 *         未引入本扩展前完全一致（向后兼容基线）。 */
#ifndef XLINK_AUTH_ON
#define XLINK_AUTH_ON 1
#endif

/** @brief 文件传输扩展（XLinkFile / XLinkFileServer / XLinkFileClient）。
 *  @note  关闭时消息类 File 整体裁空、能力位 FileTransfer 不置位。 */
#ifndef XLINK_FILE_ON
#define XLINK_FILE_ON 1
#endif

/* ========================================================================== */
/*                        发送队列与超时                                     */
/* ========================================================================== */

/** @brief 每会话发送队列容量（字节）。发送接口把整帧原子入队，pump 续写；
 *         队列不足时发送接口返回 false（背压），绝不产生半帧。
 *  @note  0 表示关闭发送队列（退回旧的一次性直写语义，短写即失败）。
 *         默认 128KB：足以容纳一整窗口文件数据（32KB）的多个块并留出
 *         控制帧余量，避免大文件传输时队列频繁打满。嵌入式可下调。
 */
#ifndef XLINK_TX_QUEUE_BYTES
#define XLINK_TX_QUEUE_BYTES (128u * 1024u)
#endif

/** @brief 握手超时（毫秒）：挂载传输后在此时间内未完成握手（未进入 Ready
 *         或 Authenticating）即判链路无效并断开。
 *  @note  0 表示不检查。默认 5 秒——远大于正常握手往返，只用于回收
 *         「连上但从不说话」的半开连接，防止服务端会话槽位被占满。 */
#ifndef XLINK_HANDSHAKE_TIMEOUT_MS
#define XLINK_HANDSHAKE_TIMEOUT_MS 5000u
#endif

/** @brief 登录认证超时（毫秒）：进入 Authenticating 后在此时间内未完成
 *         认证即断开。0 表示不检查。 */
#ifndef XLINK_AUTH_TIMEOUT_MS
#define XLINK_AUTH_TIMEOUT_MS 5000u
#endif

/** @brief 单连接登录失败次数上限（达到即断链）。 */
#ifndef XLINK_AUTH_FAILURE_LIMIT
#define XLINK_AUTH_FAILURE_LIMIT 3
#endif

/* ========================================================================== */
/*                        登录认证扩展常量                                   */
/* ========================================================================== */

/** @brief 口令盐字节数（随机盐，随账户存储）。 */
#ifndef XLINK_AUTH_SALT_BYTES
#define XLINK_AUTH_SALT_BYTES 16u
#endif

/** @brief 挑战 nonce 与应答摘要字节数（SHA-256 输出宽度）。 */
#ifndef XLINK_AUTH_NONCE_BYTES
#define XLINK_AUTH_NONCE_BYTES 32u
#endif

/** @brief 口令派生的默认迭代次数（可被账户库逐账户覆盖）。 */
#ifndef XLINK_AUTH_HASH_ITERATIONS
#define XLINK_AUTH_HASH_ITERATIONS 10000u
#endif

/** @brief 用户名最大字节数（不含终止符）。 */
#ifndef XLINK_AUTH_NAME_BYTES
#define XLINK_AUTH_NAME_BYTES 32u
#endif

/** @brief 账户库最多容纳的用户数。 */
#ifndef XLINK_AUTH_USER_SLOTS
#define XLINK_AUTH_USER_SLOTS 16u
#endif

/** @brief 认证失败原因/结果文本的最大字节数。 */
#ifndef XLINK_AUTH_TEXT_BYTES
#define XLINK_AUTH_TEXT_BYTES 96u
#endif

/* ========================================================================== */
/*                        文件传输扩展常量                                   */
/* ========================================================================== */

/** @brief 文件传输块大小（字节）：DATA 帧承载的业务数据量。
 *  @note  必须 <= XLINK_MAX_PAYLOAD - 12（DATA 载荷含 u64 偏移 + 头部）。
 *         默认 4KB，兼顾吞吐与嵌入式内存。 */
#ifndef XLINK_FILE_CHUNK_BYTES
#define XLINK_FILE_CHUNK_BYTES 4096u
#endif

/** @brief 流控窗口（字节）：接收方允许发送方在收到 ACK 前的未确认数据量。 */
#ifndef XLINK_FILE_WINDOW_BYTES
#define XLINK_FILE_WINDOW_BYTES (32u * 1024u)
#endif

/** @brief 服务端同时托管的传输数上限（每会话最多一个传输）。 */
#ifndef XLINK_FILE_MAX_TRANSFERS
#define XLINK_FILE_MAX_TRANSFERS 4u
#endif

/** @brief 单个文件大小上限（字节）。0 表示不限制。 */
#ifndef XLINK_FILE_MAX_BYTES
#define XLINK_FILE_MAX_BYTES (128ull * 1024ull * 1024ull)
#endif

/** @brief 单次传输的空闲超时（毫秒）：超过此时间无任何进展即中止传输。 */
#ifndef XLINK_FILE_IDLE_TIMEOUT_MS
#define XLINK_FILE_IDLE_TIMEOUT_MS 30000u
#endif

/** @brief Open 无应答重发上限（次）：Opening 态每秒重发一次 Open，
 *  超过此次数仍无 OpenAck 即终局失败（Timeout）。
 *  @note  对端不回 OpenAck 时（能力未协商/链路半开/宿主不转事件），
 *         不设上限会把失败形态变成「每秒重发到天荒地老」的无限停滞；
 *         默认 30 次 ≈ 30s，与 XLINK_FILE_IDLE_TIMEOUT_MS 同量级。
 *         注意不能直接套用空闲超时：每次成功重发都会刷新活动戳。 */
#ifndef XLINK_FILE_OPEN_RETRY_MAX
#define XLINK_FILE_OPEN_RETRY_MAX 30u
#endif

/** @brief 传输期间的高频驱动节拍（毫秒）：有活动传输时把会话节拍切到此值。
 *  @note  0 表示不切换（始终使用 XLINK_HEARTBEAT_TICK_MS）。 */
#ifndef XLINK_FILE_TICK_MS
#define XLINK_FILE_TICK_MS 10u
#endif

/** @brief 远程文件名最大字节数（不含终止符）。 */
#ifndef XLINK_FILE_NAME_BYTES
#define XLINK_FILE_NAME_BYTES 128u
#endif

/** @brief 文件传输结果文本最大字节数。 */
#ifndef XLINK_FILE_TEXT_BYTES
#define XLINK_FILE_TEXT_BYTES 128u
#endif

/* ========================================================================== */
/*                        协议常量                                           */
/* ========================================================================== */

/** @brief 协议版本号（写入 Hello 载荷，线上为 u16）。高字节主版本、低字节次版本。
 *  @note  版本不匹配不致命：会话层仅记录到 m_remoteVersion，兼容策略由应用层定。
 *         正本定义在 XLinkProto.h（含 1.1 变更说明）；此处保留同名兜底宏，
 *         以 #ifndef 守护避免与 XLinkProto.h 冲突（C4005 宏重定义）。 */
#ifndef XLINK_VERSION
#define XLINK_VERSION 0x0101u
#endif

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

/** @brief 心跳静默判线阈值（毫秒），超过即认定链路断开。
 *  @note  默认 6000ms（v1.3-dev 起，原 3000）：Windows 平台 IOCP 环回在
 *         大流量突发（如 37KB 文件分块）结束后，两侧挂起的 send/recv
 *         完成可整体停滞数秒（裸 socket 100 行即可复现，触发点在本机
 *         内核/驱动栈，Linux epoll 从不复现），3s 阈值会误杀健康会话
 *         ——实测即"下载 Open 帧已写出但对端收不到、双端先后判线"的
 *         文件传输端到端失败（XLink.md §14.3）。6s 覆盖实测最长停滞
 *         窗口；真断链（拔线/进程退出）的感知延迟相应加倍，厂内网
 *         控制/GUI 链路可接受。 */
#ifndef XLINK_HEARTBEAT_TIMEOUT_MS
#define XLINK_HEARTBEAT_TIMEOUT_MS 6000u
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
