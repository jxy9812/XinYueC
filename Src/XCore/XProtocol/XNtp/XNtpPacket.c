/** @file       XNtpPacket.c
 * @brief      XNtp 报文编解码实现（48 字节大端线序互转 + 时间戳换算）。
 * @details    实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 * @author     XinYueC 团队
 */
#include "XNtpPacket.h"

#if XNTP_ON

/* ========================================================================== */
/*                        大端字节序原语                                     */
/* ========================================================================== */

/** @brief 写大端 u32。buf 须有 >= 4 字节空间。 */
static void xntp_putU32be(uint8_t* buf, uint32_t v)
{
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >> 8);
    buf[3] = (uint8_t)(v);
}

/** @brief 读大端 u32。buf 须有 >= 4 字节。 */
static uint32_t xntp_getU32be(const uint8_t* buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
         | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

/* ========================================================================== */
/*                        报文编解码                                         */
/* ========================================================================== */

bool XNtpPacket_encode(const XNtpPacket* packet, uint8_t* out)
{
    if (!packet || !out)
        return false;

    /* 首字节：LI(2bit) | VN(3bit) | Mode(3bit) */
    out[0] = (uint8_t)(((packet->m_leap & 0x03u) << 6)
                     | ((packet->m_version & 0x07u) << 3)
                     | (packet->m_mode & 0x07u));
    out[1] = packet->m_stratum;
    out[2] = (uint8_t)packet->m_poll;
    out[3] = (uint8_t)packet->m_precision;
    xntp_putU32be(&out[4], packet->m_rootDelay);
    xntp_putU32be(&out[8], packet->m_rootDispersion);
    xntp_putU32be(&out[12], packet->m_referenceId);
    xntp_putU32be(&out[16], packet->m_reference.m_secs);
    xntp_putU32be(&out[20], packet->m_reference.m_frac);
    xntp_putU32be(&out[24], packet->m_originate.m_secs);
    xntp_putU32be(&out[28], packet->m_originate.m_frac);
    xntp_putU32be(&out[32], packet->m_receive.m_secs);
    xntp_putU32be(&out[36], packet->m_receive.m_frac);
    xntp_putU32be(&out[40], packet->m_transmit.m_secs);
    xntp_putU32be(&out[44], packet->m_transmit.m_frac);
    return true;
}

bool XNtpPacket_decode(const uint8_t* buf, size_t length, XNtpPacket* out)
{
    if (!buf || !out || length < XNTP_PACKET_SIZE)
        return false;

    out->m_leap             = (uint8_t)((buf[0] >> 6) & 0x03u);
    out->m_version          = (uint8_t)((buf[0] >> 3) & 0x07u);
    out->m_mode             = (uint8_t)(buf[0] & 0x07u);
    out->m_stratum          = buf[1];
    out->m_poll             = (int8_t)buf[2];
    out->m_precision        = (int8_t)buf[3];
    out->m_rootDelay        = xntp_getU32be(&buf[4]);
    out->m_rootDispersion   = xntp_getU32be(&buf[8]);
    out->m_referenceId      = xntp_getU32be(&buf[12]);
    out->m_reference.m_secs    = xntp_getU32be(&buf[16]);
    out->m_reference.m_frac    = xntp_getU32be(&buf[20]);
    out->m_originate.m_secs    = xntp_getU32be(&buf[24]);
    out->m_originate.m_frac    = xntp_getU32be(&buf[28]);
    out->m_receive.m_secs      = xntp_getU32be(&buf[32]);
    out->m_receive.m_frac      = xntp_getU32be(&buf[36]);
    out->m_transmit.m_secs     = xntp_getU32be(&buf[40]);
    out->m_transmit.m_frac     = xntp_getU32be(&buf[44]);
    return true;
}

/* ========================================================================== */
/*                        时间戳换算                                         */
/* ========================================================================== */

bool XNtpTimestamp_isZero(const XNtpTimestamp* ts)
{
    if (!ts)
        return true;
    return ts->m_secs == 0u && ts->m_frac == 0u;
}

uint64_t XNtpTimestamp_toUnixMs(const XNtpTimestamp* ts)
{
    uint64_t secs;

    if (!ts)
        return 0;
    /* 早于 Unix 纪元（1900..1969 区间）视为无效，钳制为 0。 */
    if (ts->m_secs < XNTP_ERA_OFFSET)
        return 0;
    secs = (uint64_t)ts->m_secs - XNTP_ERA_OFFSET;
    return secs * 1000ULL + (uint64_t)(ts->m_frac / XNTP_FRAC_PER_MS);
}

void XNtpTimestamp_fromUnixMs(XNtpTimestamp* ts, uint64_t unixMs)
{
    if (!ts)
        return;
    ts->m_secs = (uint32_t)(unixMs / 1000ULL) + (uint32_t)XNTP_ERA_OFFSET;
    /* 毫余数转 32 位小数：ms * (2^32/1000)，误差 <= 0.25ms，对本协议足够。 */
    ts->m_frac = (uint32_t)((unixMs % 1000ULL) * (uint64_t)XNTP_FRAC_PER_MS);
}

void XNtpPacket_makeClientRequest(XNtpPacket* packet,
                                  const XNtpTimestamp* originate)
{
    if (!packet)
        return;
    packet->m_leap            = (uint8_t)XNtpLeapIndicator_None;
    packet->m_version         = XNTP_VERSION;
    packet->m_mode            = (uint8_t)XNtpMode_Client;
    packet->m_stratum         = 0;
    packet->m_poll            = 0;
    packet->m_precision       = 0;
    packet->m_rootDelay       = 0;
    packet->m_rootDispersion  = 0;
    packet->m_referenceId     = 0;
    packet->m_reference.m_secs = 0;
    packet->m_reference.m_frac = 0;
    packet->m_originate.m_secs = 0;
    packet->m_originate.m_frac = 0;
    packet->m_receive.m_secs   = 0;
    packet->m_receive.m_frac   = 0;
    /* RFC 5905：客户端把发出时刻 T1 填在 Transmit 字段（不是 Originate），
     * 服务器应答时将请求的 Transmit 原样拷入应答的 Originate 供回显比对。 */
    if (originate) {
        packet->m_transmit = *originate;
    } else {
        packet->m_transmit.m_secs = 0;
        packet->m_transmit.m_frac = 0;
    }
}

bool XNtpPacket_isValidServerReply(const XNtpPacket* packet,
                                   const XNtpTimestamp* expectedOrigin)
{
    if (!packet)
        return false;
    /* 必须是服务器应答 */
    if (packet->m_mode != (uint8_t)XNtpMode_Server)
        return false;
    /* 服务器时钟未同步（LI=3） */
    if (packet->m_leap == (uint8_t)XNtpLeapIndicator_Unsynchronized)
        return false;
    /* Kiss-o'-Death（stratum 0）与未同步（stratum 16+） */
    if (packet->m_stratum == XNTP_STRATUM_KOD || packet->m_stratum > XNTP_STRATUM_MAX)
        return false;
    /* T1 回显必须与请求一致：防错包/重放；零回显同样不可信 */
    if (XNtpTimestamp_isZero(&packet->m_originate))
        return false;
    if (expectedOrigin
        && (packet->m_originate.m_secs != expectedOrigin->m_secs
            || packet->m_originate.m_frac != expectedOrigin->m_frac))
        return false;
    /* 服务器时间戳必须已填充 */
    if (XNtpTimestamp_isZero(&packet->m_receive)
        || XNtpTimestamp_isZero(&packet->m_transmit))
        return false;
    return true;
}

#endif /* XNTP_ON */
