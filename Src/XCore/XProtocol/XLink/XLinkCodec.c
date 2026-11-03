/** @file       XLinkCodec.c
 * @brief      XLink 帧编解码实现（载荷 + 帧头编解码，小端序列化直调 XMemory）。
 * @details    实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX 或其他平台 API。
 *             多字节字段一律经 XMemory_write_data/read_data 按小端线序读写
 *             （本机小端即 memcpy），字段偏移与字节宽属线上格式。
 * @author     XinYueC 团队
 */
#include "XLinkCodec.h"
#include "XMemory.h"
#include <string.h>

#if XLINK_ON && XLINK_CODEC_ON

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

    XMemory_write_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&payload->m_version,
                       sizeof(payload->m_version));
    XMemory_write_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&payload->m_capability,
                       sizeof(payload->m_capability));
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

    XMemory_read_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_version, sizeof(out->m_version));
    XMemory_read_data(&buf[2], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&out->m_capability, sizeof(out->m_capability));
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

    XMemory_write_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&header->m_payloadLen,
                       sizeof(header->m_payloadLen));
    XMemory_write_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&header->m_msgClass,
                       sizeof(header->m_msgClass));
    XMemory_write_data(&buf[6], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&header->m_msgCode,
                       sizeof(header->m_msgCode));
    XMemory_write_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&header->m_requestId,
                       sizeof(header->m_requestId));
    /* dataType 单字节，无字节序问题；保留位一律写 0（见 XLinkProto.h 帧结构）。 */
    buf[12] = header->m_dataType;
    buf[13] = 0;
    buf[14] = 0;
    buf[15] = 0;
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

    /* 全字段先解到局部，全部校验通过才写 out：失败时 out 保持不变
       （见 XLinkCodec.h 返回值契约）。 */
    XLinkFrameHeader decoded;
    memset(&decoded, 0, sizeof(decoded));

    XMemory_read_data(&buf[0], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&decoded.m_payloadLen,
                      sizeof(decoded.m_payloadLen));
    XMemory_read_data(&buf[4], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&decoded.m_msgClass,
                      sizeof(decoded.m_msgClass));
    XMemory_read_data(&buf[6], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&decoded.m_msgCode,
                      sizeof(decoded.m_msgCode));
    XMemory_read_data(&buf[8], XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&decoded.m_requestId,
                      sizeof(decoded.m_requestId));
    decoded.m_dataType = buf[12];

    /* 载荷上限校验：越界即协议违例，交由会话层断链（防畸形帧撑爆接收缓冲）。 */
    if (decoded.m_payloadLen > XLINK_MAX_PAYLOAD)
        return false;

    /* 数据类型取值校验：未知值属协议违例（老对端/畸形帧），不得进业务层。
       与载荷上限同口径——解码层是唯一违例入口，返回 false 由会话层判线。 */
    if (decoded.m_dataType != XLinkDataType_Raw
        && decoded.m_dataType != XLinkDataType_Json
        && decoded.m_dataType != XLinkDataType_Bson)
        return false;

    /* 保留位不校验（发送方应写 0，未来可能承载新字段）。 */
    *out = decoded;
    return true;
}

#endif /* XLINK_ON && XLINK_CODEC_ON */
