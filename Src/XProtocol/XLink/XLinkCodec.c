/** @file       XLinkCodec.c
 * @brief      XLink 帧编解码实现（小端序列化 + 帧头编解码）。
 * @details    实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 * @author     XinYueC 团队
 */
#include "XLinkCodec.h"
#include "XMemory.h"

#if XLINK_ON && XLINK_CODEC_ON

/* ========================================================================== */
/*                        小端序列化原语                                     */
/* ========================================================================== */

void XLinkCodec_putU16(uint8_t* buf, uint16_t value)
{
    if (ISNULL(buf, "XLinkCodec_putU16 buf"))
        return;
    XMemory_write_data(buf, XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&value, sizeof(value));
}

uint16_t XLinkCodec_getU16(const uint8_t* buf)
{
    uint16_t value = 0;
    if (ISNULL(buf, "XLinkCodec_getU16 buf"))
        return 0;
    XMemory_read_data(buf, XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&value, sizeof(value));
    return value;
}

void XLinkCodec_putU32(uint8_t* buf, uint32_t value)
{
    if (ISNULL(buf, "XLinkCodec_putU32 buf"))
        return;
    XMemory_write_data(buf, XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&value, sizeof(value));
}

uint32_t XLinkCodec_getU32(const uint8_t* buf)
{
    uint32_t value = 0;
    if (ISNULL(buf, "XLinkCodec_getU32 buf"))
        return 0;
    XMemory_read_data(buf, XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&value, sizeof(value));
    return value;
}

/* ========================================================================== */
/*                        会话载荷                                           */
/* ========================================================================== */

bool XLinkCodec_encodeHello(const XLinkHelloPayload* payload, uint8_t* buf,
                            size_t capacity, size_t* outLen)
{
    if (ISNULL(payload, "XLinkCodec_encodeHello payload")
        || ISNULL(buf, "XLinkCodec_encodeHello buf"))
        return false;
    if (capacity < 4)
        return false;

    XLinkCodec_putU16(&buf[0], payload->m_version);
    XLinkCodec_putU16(&buf[2], payload->m_capability);
    if (outLen)
        *outLen = 4;
    return true;
}

bool XLinkCodec_decodeHello(const uint8_t* buf, size_t length,
                            XLinkHelloPayload* out)
{
    if (ISNULL(buf, "XLinkCodec_decodeHello buf")
        || ISNULL(out, "XLinkCodec_decodeHello out"))
        return false;
    if (length < 4)
        return false;

    out->m_version    = XLinkCodec_getU16(&buf[0]);
    out->m_capability = XLinkCodec_getU16(&buf[2]);
    return true;
}

/* ========================================================================== */
/*                        帧头                                               */
/* ========================================================================== */

bool XLinkCodec_encodeHeader(const XLinkFrameHeader* header, uint8_t* buf)
{
    if (ISNULL(header, "XLinkCodec_encodeHeader header")
        || ISNULL(buf, "XLinkCodec_encodeHeader buf"))
        return false;

    XLinkCodec_putU32(&buf[0], header->m_payloadLen);
    XLinkCodec_putU16(&buf[4], header->m_msgClass);
    XLinkCodec_putU16(&buf[6], header->m_msgCode);
    XLinkCodec_putU32(&buf[8], header->m_requestId);
    return true;
}

bool XLinkCodec_decodeHeader(const uint8_t* buf, size_t length,
                             XLinkFrameHeader* out)
{
    if (ISNULL(buf, "XLinkCodec_decodeHeader buf")
        || ISNULL(out, "XLinkCodec_decodeHeader out"))
        return false;
    if (length < XLINK_HEADER_SIZE)
        return false;

    out->m_payloadLen = XLinkCodec_getU32(&buf[0]);
    out->m_msgClass   = XLinkCodec_getU16(&buf[4]);
    out->m_msgCode    = XLinkCodec_getU16(&buf[6]);
    out->m_requestId  = XLinkCodec_getU32(&buf[8]);

    /* 载荷上限校验：越界即协议违例，交由会话层断链（防畸形帧撑爆接收缓冲）。 */
    if (out->m_payloadLen > XLINK_MAX_PAYLOAD)
        return false;

    return true;
}

#endif /* XLINK_ON && XLINK_CODEC_ON */
