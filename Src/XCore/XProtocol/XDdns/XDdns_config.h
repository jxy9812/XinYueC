/** @file       XDdns_config.h
 * @brief      XDdns 动态域名解析（DDNS）配置文件。
 *
 * 通过此配置文件可以裁剪 XDdns 的内部子功能：
 *   1. XDDNS_CLIENT_ON   - 客户端核心（XDdnsClient / 编排逻辑）
 *   2. XDDNS_ALIYUN_ON   - 阿里云解析（Alibaba Cloud DNS）厂商实现
 *
 * XDdns 的职责：定时探测本机公网 IP（HTTP 探测接口或手动注入），与云厂商
 * DNS 记录比对，变化时自动更新 A/AAAA 记录。厂商通过 provider ops 抽象层
 * 接入，首个实现为阿里云（POP 签名 + alidns API）。
 *
 * 协议总开关 XDDNS_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 */
#ifndef XDDNS_CONFIG_H
#define XDDNS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XDDNS_ON
#define XDDNS_ON XPROTOCOL_ON
#endif

#if XDDNS_ON

/* ========================================================================== */
/*                        子功能开关                                         */
/* ========================================================================== */

/** @brief 客户端核心（XDdnsClient：探测/比对/更新编排）。 */
#ifndef XDDNS_CLIENT_ON
#define XDDNS_CLIENT_ON 1
#endif

/** @brief 阿里云解析厂商实现（POP 签名 + alidns.aliyuncs.com API）。 */
#ifndef XDDNS_ALIYUN_ON
#define XDDNS_ALIYUN_ON 1
#endif

/* ========================================================================== */
/*                        协议常量                                           */
/* ========================================================================== */

/** @brief 阿里云解析 API 公开端点（可用 setEndpoint 覆盖：测试指向本地
 *          mock，专有云指向私有端点）。 */
#define XDDNS_ALIYUN_ENDPOINT "https://alidns.aliyuncs.com"

/** @brief 阿里云 POP API 版本号（云解析 DNS 2015-01-09）。 */
#define XDDNS_ALIYUN_VERSION "2015-01-09"

/** @brief 单次 HTTP 请求超时（毫秒）。 */
#define XDDNS_HTTP_TIMEOUT_MS 10000u

/** @brief 默认自动检查周期（毫秒）：10 分钟。 */
#define XDDNS_DEFAULT_CHECK_INTERVAL_MS (10u * 60u * 1000u)

/** @brief 新记录默认 TTL（秒）；AddDomainRecord 时随请求提交。 */
#define XDDNS_DEFAULT_TTL 600u

/** @brief 自定义探测 URL 列表最大条数（v4/v6 各自独立计数）。 */
#define XDDNS_MAX_PROBE_URLS 4u

/** @brief 各缓冲上限：URL/域名/记录值。 */
#define XDDNS_URL_MAX 2048u
#define XDDNS_NAME_MAX 256u
#define XDDNS_VALUE_MAX 64u

/** @brief 内置公网 IP 探测源（IPv4）。应答体须为纯 IP 文本。
 *  @note  前两个为国际源（HTTPS），第三个为国内源（HTTP，明文仅用于探测
 *         无敏感信息）；按槽位顺序尝试，首个成功者生效。 */
#define XDDNS_PROBE_V4_DEFAULT                                              \
    {                                                                       \
        "https://api4.ipify.org/",                                          \
        "https://ipv4.icanhazip.com/",                                      \
        "http://ip.3322.net/"                                               \
    }
/** @brief 内置公网 IP 探测源（IPv6）。 */
#define XDDNS_PROBE_V6_DEFAULT                                              \
    {                                                                       \
        "https://api6.ipify.org/",                                          \
        "https://ipv6.icanhazip.com/"                                       \
    }
/** @brief 内置探测源数量。 */
#define XDDNS_PROBE_V4_COUNT 3u
#define XDDNS_PROBE_V6_COUNT 2u

#endif /* XDDNS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDDNS_CONFIG_H */
