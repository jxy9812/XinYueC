#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON
#include "XS7Cotp.h"
#include "XS7Tpkt.h"
#include <string.h>

/**
 * @file XS7Cotp.c
 * @brief COTP 层（ISO8073）CR 构造、CC 解析、DT 提取实现
 * @details 字节模板对齐 s-pms g_plc_head1 金样（已核验）：
 *   03 00 00 16 11 E0 00 00 00 01 00 C0 01 0A C1 02 ll lh C2 02 rl rh
 *   参数顺序 C0(tpdu大小)/C1(本端TSAP)/C2(目标TSAP)；
 *   目标 TSAP 低字节 = rack*0x20 + slot。
 */

uint16_t XS7Cotp_remoteTsapFromRackSlot(uint8_t connType, uint8_t rack, uint8_t slot)
{
    /* S7NetPlus TsapPair 公式：(connType << 8) | ((rack << 5) | slot) */
    uint16_t low = (uint16_t)(((rack & 0x07) << 5) | (slot & 0x1F));
    return (uint16_t)(((uint16_t)connType << 8) | low);
}

size_t XS7Cotp_buildCr(XByteArray* out, uint16_t localTsap, uint16_t remoteTsap, uint8_t tpduSizeCode)
{
    if (!out) return 0;

    /* COTP CR 段（不含 TPKT）：LI + PDU + dstRef + srcRef + class + 参数 */
    uint8_t cotp[18];
    cotp[0] = 0x11;                      /* LI = 17（后续字节数） */
    cotp[1] = XS7COTP_PDU_CR;            /* 0xE0 连接请求 */
    cotp[2] = 0x00;                      /* dstRef 高 */
    cotp[3] = 0x00;                      /* dstRef 低 */
    cotp[4] = 0x00;                      /* srcRef 高 */
    cotp[5] = 0x01;                      /* srcRef 低（固定 1，对齐金样） */
    cotp[6] = 0x00;                      /* class 0 */
    /* C0: TPDU 大小码（对齐金样 0x0A = 1024） */
    cotp[7] = 0xC0;
    cotp[8] = 0x01;
    cotp[9] = tpduSizeCode;
    /* C1: 本端 TSAP（BE16，对齐金样 0x0102） */
    cotp[10] = 0xC1;
    cotp[11] = 0x02;
    cotp[12] = (uint8_t)(localTsap >> 8);
    cotp[13] = (uint8_t)(localTsap & 0xFF);
    /* C2: 目标 TSAP（BE16，低字节 = rack*0x20+slot） */
    cotp[14] = 0xC2;
    cotp[15] = 0x02;
    cotp[16] = (uint8_t)(remoteTsap >> 8);
    cotp[17] = (uint8_t)(remoteTsap & 0xFF);

    /* 外层包 TPKT，整帧写入 out 尾部 */
    return XS7Tpkt_wrap(out, cotp, sizeof(cotp));
}

bool XS7Cotp_parseCc(const uint8_t* frame, size_t len, XS7CotpCcInfo* out)
{
    if (!frame || !out) return false;
    /* 完整帧含 TPKT（4）+ COTP 头至少 6 字节 */
    if (len < 10) return false;

    /* TPKT 校验 */
    if (frame[0] != 0x03) return false;
    size_t total = ((size_t)frame[2] << 8) | (size_t)frame[3];
    if (total != len || total < 10) return false;

    /* COTP CC：LI(1) + type(1) ... */
    uint8_t pduType = frame[5] & 0xF0;   /* 高 4 位为 PDU 类型码 */
    if (pduType != XS7COTP_PDU_CC) return false;

    /* 解析参数区：LI 在 frame[4]，覆盖 type..class 及参数 */
    uint8_t li = frame[4];
    if ((size_t)li + 5 > len) return false;
    if (li < 6) return false;            /* LI 至少覆盖 type+dst+src+class */

    /* TPKT(4)+LI(1)+type(1)+dst(2)+src(2)+class(1) → 参数自 frame[11] 起，
       参数区末尾 = 5 + LI（相对帧起点） */
    size_t paramOff = 11;
    size_t paramEnd = 5 + (size_t)li;
    if (paramEnd > len) return false;

    memset(out, 0, sizeof(*out));
    bool hasC0 = false;
    size_t off = paramOff;
    while (off + 2 <= paramEnd) {
        uint8_t code = frame[off];
        uint8_t plen = frame[off + 1];
        if (off + 2 + plen > paramEnd) return false;

        if (code == 0x50) {
            /* 拒绝原因参数（对齐头文件契约） */
            return false;
        } else if (code == 0xC0 && plen >= 1) {
            out->tpduSizeCode = frame[off + 2];
            hasC0 = true;
        } else if (code == 0xC1 && plen >= 2) {
            out->localTsap = (uint16_t)((frame[off + 2] << 8) | frame[off + 3]);
        } else if (code == 0xC2 && plen >= 2) {
            out->remoteTsap = (uint16_t)((frame[off + 2] << 8) | frame[off + 3]);
        }
        off += 2 + plen;
    }
    return hasC0;   /* CC 合法性：至少含 TPDU 大小参数 */
}

bool XS7Cotp_extractDt(const uint8_t* frame, size_t len, size_t* s7Offset, size_t* s7Len)
{
    if (!frame || !s7Offset || !s7Len) return false;
    /* TPKT(4) + DT 头 02 F0 80(3) = 7 字节起为 S7 PDU */
    if (len < 8) return false;

    if (frame[0] != 0x03) return false;
    size_t total = ((size_t)frame[2] << 8) | (size_t)frame[3];
    if (total != len) return false;

    /* DT 固定头：LI=02, type=F0, EOT=80（对齐 s-pms s7_read_response 校验） */
    if (frame[4] != 0x02 || frame[5] != XS7COTP_PDU_DT || frame[6] != 0x80) {
        return false;
    }

    *s7Offset = 7;
    *s7Len = len - 7;
    return *s7Len > 0;
}

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
