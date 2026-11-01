/** @file       XNtpProto.h
 * @brief      XNtp 协议——线上格式定义（RFC 5905 NTPv4/SNTP 报文结构与常量）。
 * @details    对齐 RFC 5905 第 6 节的报文图。实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX 或其他平台 API；不引入 <netinet/ntp.h>
 *             等系统头（嵌入式交叉环境不可依赖）。
 *
 *             NTP 报文线上布局（48 字节，全部大端）：
 * @code
 *   偏移  长度  字段
 *   0     1     LI(2bit) | VN(3bit) | Mode(3bit)
 *   1     1     Stratum
 *   2     1     Poll（2 的幂秒，有符号）
 *   3     1     Precision（2 的幂秒，有符号）
 *   4     4     Root Delay（32 位有符号定点小数秒）
 *   8     4     Root Dispersion
 *   12    4     Reference ID
 *   16    8     Reference Timestamp（64 位：32 秒 + 32 小数）
 *   24    8     Originate Timestamp（T1：客户端发出时刻，服务器须原样回显）
 *   32    8     Receive Timestamp（T2：服务器收到请求的时刻）
 *   40    8     Transmit Timestamp（T3：服务器发出应答的时刻）
 * @endcode
 *
 *             64 位时间戳 = 32 位秒（自 1900-01-01）+ 32 位小数（2^-32 秒
 *             为单位），换算 Unix 纪元减 XNTP_ERA_OFFSET。
 */
#ifndef XNTPPROTO_H
#define XNTPPROTO_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入本协议配置，确保 XNTP_ON 已定义（必须早于下方条件编译判定） */
#include "XNtp_config.h"

#if XNTP_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XNtp_config.h"

/* ========================================================================== */
/*                        字段常量                                           */
/* ========================================================================== */

/**
 * @brief Leap Indicator（报文首字节高 2 位）。
 */
typedef enum XNtpLeapIndicator {
    XNtpLeapIndicator_None           = 0, /**< 无闰秒预告（正常）。 */
    XNtpLeapIndicator_InsertPending  = 1, /**< 最后一分钟插入闰秒。 */
    XNtpLeapIndicator_DeletePending  = 2, /**< 最后一分钟删除闰秒。 */
    XNtpLeapIndicator_Unsynchronized = 3  /**< 服务器时钟未同步（应答不可信，须拒绝）。 */
} XNtpLeapIndicator;

/**
 * @brief Mode（报文首字节低 3 位）。
 */
typedef enum XNtpMode {
    XNtpMode_Reserved   = 0, /**< 保留。 */
    XNtpMode_SymmetricActive = 1, /**< 对称主动。 */
    XNtpMode_SymmetricPassive = 2, /**< 对称被动。 */
    XNtpMode_Client     = 3, /**< 客户端请求。 */
    XNtpMode_Server     = 4, /**< 服务器应答。 */
    XNtpMode_Broadcast  = 5  /**< 广播/多播。 */
} XNtpMode;

/** @brief Stratum = 0：Kiss-o'-Death 包（服务器拒绝服务，须停止请求该服务器）。 */
#define XNTP_STRATUM_KOD 0u
/** @brief Stratum 有效上限（1..15 有效；16 = 未同步；17..255 保留）。 */
#define XNTP_STRATUM_MAX 15u

/* ========================================================================== */
/*                        结构定义                                           */
/* ========================================================================== */

/**
 * @brief 64 位 NTP 时间戳（32 位秒 + 32 位小数）。
 */
typedef struct XNtpTimestamp {
    uint32_t m_secs; /**< 秒部分：自 1900-01-01T00:00:00 起算。 */
    uint32_t m_frac; /**< 小数部分：以 2^-32 秒为单位。 */
} XNtpTimestamp;

/**
 * @brief NTP 报文的内存形态（与线上大端字节序解耦，编解码见 XNtpPacket）。
 */
typedef struct XNtpPacket {
    uint8_t  m_leap;       /**< LI，见 XNtpLeapIndicator。 */
    uint8_t  m_version;    /**< 协议版本（当前 XNTP_VERSION = 4）。 */
    uint8_t  m_mode;       /**< 模式，见 XNtpMode。 */
    uint8_t  m_stratum;    /**< 层级：0=KoD，1=主钟，2..15=次级。 */
    int8_t   m_poll;       /**< 轮询间隔（2 的幂秒）。 */
    int8_t   m_precision;  /**< 时钟精度（2 的幂秒，负值）。 */
    uint32_t m_rootDelay;  /**< 到主钟的总往返时延（32 位定点小数秒）。 */
    uint32_t m_rootDispersion; /**< 到主钟的总离散（32 位定点小数秒）。 */
    uint32_t m_referenceId;    /**< 参考源标识（IPv4 地址或 KoD 代码）。 */
    XNtpTimestamp m_reference; /**< 上次校时时刻。 */
    XNtpTimestamp m_originate; /**< T1：客户端发出时刻（服务器须原样回显）。 */
    XNtpTimestamp m_receive;   /**< T2：服务器收到请求的时刻。 */
    XNtpTimestamp m_transmit;  /**< T3：服务器发出应答的时刻。 */
} XNtpPacket;

#endif /* XNTP_ON */

#ifdef __cplusplus
}
#endif
#endif /* XNTPPROTO_H */
