/** @file       XDdnsClient_Protected.h
 * @brief      XDdns 客户端内部实现接口（仅供实现使用，不对外公开）。
 */
#ifndef XDDNSCLIENT_PROTECTED_H
#define XDDNSCLIENT_PROTECTED_H

#include "XDdnsClient.h"
#include "XNetworkAccessManager.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XDDNS_ON && XDDNS_CLIENT_ON

/**
 * @brief XDdns 客户端私有状态。
 * @note  全部字段仅供实现使用。字符串成员均为对象拥有的 XString*。
 */
struct XDdnsClientPrivate {
    const XDdnsProviderOps* m_provider; /**< 厂商操作表（静态存储期借用）。 */

    XString* m_endpoint;   /**< 端点覆盖；NULL/空 = 厂商默认端点。 */
    XString* m_keyId;      /**< AccessKey ID。 */
    XString* m_keySecret;  /**< AccessKey Secret。 */
    XString* m_domain;     /**< 主域名。 */
    XString* m_rr;         /**< 主机记录。 */
    XDdnsRecordType m_type; /**< 记录类型。 */

    XString* m_probeUrls[XDDNS_MAX_PROBE_URLS]; /**< 自定义探测列表（可空槽）。 */
    XString* m_manualIp;   /**< 手动注入 IP；NULL = 自动探测。 */
    XString* m_currentIp;  /**< 最近一次探测/注入成功的 IP。 */
    XString* m_pushedIp;   /**< 最近一次确认与云端一致的 IP。 */

    XNetworkAccessManager* m_http; /**< HTTP 管理器（懒创建，对象拥有）。 */
    uint32_t m_timeoutMs;          /**< 单次 HTTP 超时（毫秒）。 */
    bool m_verifyPeer;             /**< HTTPS 对端证书校验（默认 false）。 */

    XTimerId m_checkTimer;         /**< 周期检查定时器（0 = 未启用）。 */
    uint32_t m_checkIntervalMs;    /**< 检查周期（0 = 关闭）。 */

    /* 状态位域 */
    uint32_t m_state : 8;          /**< XDdnsClientState。 */
    uint32_t m_lastError : 8;      /**< XDdnsError。 */
    char m_lastApiCode[64];   /**< 云端错误码（如 SignatureDoesNotMatch）。 */
    int32_t m_lastHttpStatus;      /**< 最近一次 HTTP 应答状态码（0=未收到应答；
                                       400 多为 POP 签名/权限被云端拒绝，
                                       供诊断与 GUI 提示使用）。 */
};

#endif /* XDDNS_ON && XDDNS_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDDNSCLIENT_PROTECTED_H */
