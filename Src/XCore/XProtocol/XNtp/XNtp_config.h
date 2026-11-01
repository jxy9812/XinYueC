/** @file       XNtp_config.h
 * @brief      XNtp 协议（SNTP 客户端）配置文件。
 *
 * 通过此配置文件可以裁剪 XNtp 的内部子功能：
 *   1. XNTP_CLIENT_ON - 客户端核心（XNtpClient / XNtpPacket）
 *
 * XNtp 是 RFC 5905（NTPv4）的最小客户端实现（SNTP 口径：单次请求-应答校时，
 * 不做时钟滤波/驯服）。定位：最小嵌入式 Linux 固件（无 ntpd/chrony）的应用
 * 内校时；需要完整时钟管理时请使用系统 ntpd。
 *
 * 协议总开关 XNTP_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 */
#ifndef XNTP_CONFIG_H
#define XNTP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XNTP_ON
#define XNTP_ON XPROTOCOL_ON
#endif

#if XNTP_ON

/* ========================================================================== */
/*                        子功能开关                                         */
/* ========================================================================== */

/** @brief 客户端核心（XNtpClient / XNtpPacket / XNtpProto）。 */
#ifndef XNTP_CLIENT_ON
#define XNTP_CLIENT_ON 1
#endif

/* ========================================================================== */
/*                        协议常量                                           */
/* ========================================================================== */

/** @brief NTP 标准端口（RFC 5905）。 */
#define XNTP_DEFAULT_PORT 123u

/** @brief NTP 基本报文长度（字节）。时间戳 MAC 之外的最小完整报文。 */
#define XNTP_PACKET_SIZE 48u

/** @brief NTP 纪元偏移：NTP 时戳自 1900-01-01 起算，Unix 自 1970-01-01。 */
#define XNTP_ERA_OFFSET 2208988800ULL

/** @brief 32 位定点小数中 1 毫秒对应的刻度数（2^32 / 1000，用于 frac 换算）。 */
#define XNTP_FRAC_PER_MS 4294967u

/** @brief 单次同步的默认应答超时（毫秒）。 */
#define XNTP_DEFAULT_TIMEOUT_MS 5000u

/** @brief 默认重试次数（超时后重发，总尝试 = 1 + 重试次数）。 */
#define XNTP_DEFAULT_RETRIES 2u

/** @brief 默认自动同步周期（毫秒）：60 分钟一次，对无漂移补偿的嵌入式
 *  RTC 足够；更短的周期对服务器不礼貌（RFC 4635 礼节）。 */
#define XNTP_DEFAULT_SYNC_INTERVAL_MS (60u * 60u * 1000u)

/** @brief NTP 协议版本号（客户端请求中填充）。 */
#define XNTP_VERSION 4u

#endif /* XNTP_ON */

#ifdef __cplusplus
}
#endif
#endif /* XNTP_CONFIG_H */
