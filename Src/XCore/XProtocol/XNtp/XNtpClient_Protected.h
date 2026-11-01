/** @file       XNtpClient_Protected.h
 * @brief      XNtp 客户端内部实现接口（仅供子类与内部实现使用，不对外公开）。
 * @details    集中放置私有状态结构与内部辅助函数，遵循仓库保护头 7 条约定：
 *             自包含、独立保护宏、注释标准同公开头。
 */
#ifndef XNTPCLIENT_PROTECTED_H
#define XNTPCLIENT_PROTECTED_H

#include "XNtpClient.h"
#include "XUdpSocket.h"
#include "XHostAddress.h"
#include "XObject.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNTP_ON && XNTP_CLIENT_ON

/* ========================================================================== */
/*                        私有状态                                           */
/* ========================================================================== */

/**
 * @brief XNtp 客户端私有状态。
 * @note  全部字段仅供实现使用；外部代码不得直接访问。
 * @note  类型名（typedef）已在公开头 XNtpClient.h 前置声明，此处只补全
 *        结构体定义，避免 C99 下的重复 typedef。
 */
struct XNtpClientPrivate {
    XUdpSocket* m_socket;          /**< 数据报套接字（对象拥有，首次同步懒创建）。 */
    XString* m_serverName;         /**< 服务器名（域名或点分 IP，对象拥有）。 */
    XHostAddress m_serverAddr;     /**< 已解析的服务器地址（最近一次成功解析）。 */
    uint16_t m_serverPort;         /**< 服务器端口（默认 XNTP_DEFAULT_PORT）。 */
    bool m_addrResolved;           /**< 服务器地址是否已成功解析。 */

    XNtpTimestamp m_t1;            /**< 本次请求的 T1（发出时刻，用于回显比对）。 */
    int64_t m_t1WallMs;            /**< T1 的墙钟毫秒（本机口径，算 offset 用）。 */
    int64_t m_t4WallMs;            /**< T4 的墙钟毫秒（收到应答时刻）。 */

    int64_t m_offsetMs;            /**< 最近一次成功的时钟偏移（服务器 - 本地，毫秒）。 */
    int64_t m_delayMs;             /**< 最近一次成功的往返时延（毫秒）。 */
    uint64_t m_serverSecs;         /**< 最近一次成功同步的服务器 UTC 纪元秒。 */
    uint8_t m_stratum;             /**< 最近一次应答的服务器层级。 */

    XTimerId m_timeoutTimer;       /**< 单次应答超时定时器。 */
    XTimerId m_syncTimer;          /**< 自动同步周期定时器（0 = 未启用）。 */
    int m_retriesLeft;             /**< 本次同步剩余重试次数。 */

    uint32_t m_timeoutMs;          /**< 应答超时（毫秒）。 */
    uint32_t m_syncIntervalMs;     /**< 自动同步周期（毫秒）。 */
    int m_maxRetries;              /**< 最大重试次数。 */

    /* 状态位域 */
    uint32_t m_state : 8;          /**< XNtpClientState。 */
    uint32_t m_lastError : 8;      /**< XNtpClientError（最近一次失败原因）。 */
    uint32_t m_autoSync : 1;       /**< 是否启用自动周期同步。 */
    uint32_t m_synchronized : 1;   /**< 是否至少成功同步过一次。 */
};

/* ========================================================================== */
/*                        内部辅助（供实现与测试白盒使用）                    */
/* ========================================================================== */

/**
 * @brief  取私有状态结构（实现内部用）。
 * @param  client 客户端实例（借用）。
 * @return 私有状态指针；client 为 NULL 返回 NULL。
 */
XNtpClientPrivate* XNtpClient_private(const XNtpClient* client);

#endif /* XNTP_ON && XNTP_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XNTPCLIENT_PROTECTED_H */
