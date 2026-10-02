#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON
#include "XS7Pdu.h"
#include "XS7Tpkt.h"
#include <string.h>

/**
 * @file XS7Pdu.c
 * @brief S7Comm 组包/解包核心实现
 * @details 全大端字节序；字节依据（已核验）：
 * - 头解析对齐 Wireshark s7comm 解码器：ROSCTR Job(0x01) 头长 10 字节，
 *   Ack(0x02)/Ack_Data(0x03)/UserData(0x07) 头长 12 字节（多 errorClass/errorCode）；
 * - 读请求/写请求 ANY 项：12 0A 10 | transport | count(BE) | db(BE) | area | 3B 位地址(BE)；
 *   字节量传输约定 transport=0x02、count=字节数，位传输 transport=0x01、count=位数，
 *   地址恒为位地址（s-pms build_read_byte_command 金样：DB1.DBW10 → 02 00 02 / 00 00 50；
 *   S7NetPlus WriteReadHeader 同构）；
 * - 读应答/写请求数据项：ret(1) + transport(1) + 长度(2)，Wireshark 规则
 *   （packet-s7comm.c 常量表注释：0x03 "bit access, len is in bits"、
 *   0x04 "byte/word/dword access, len is in bits"、0x09 "octet string,
 *   len is in bytes"）：
 *   传输大小 0x03/0x04 时长度字段为**位计数**，0x09（八位组串）时为**字节数**；
 *   项与项之间偶对齐（长度非偶数倍数时 PLC 在项尾补填充字节）；
 *   【真机实测修正（第 1 轮，S7-1200，协商 PDU=240）】STRING 写改用 0x09 八位组
 *   串（长度=字节数）：0x04+位计数在 8/16/32 位写均被目标 CPU 接受（FF 且生效），
 *   唯 STRING(16) 18 字节写（00 04 00 90）被 CPU 以数据区返回码 0x07 拒绝；
 *   0x09 分支与本文件 XS7Value.h 契约（"String 对应 0x09 八位组分支"）一致。
 * - Setup：请求 32 01 0000 ref 0008 0000 | F0 00 AmQ(BE) AmQ(BE) PduLen(BE)，
 *   应答参数同构，协商结果取 min(首期固定请求值 XS7_REQUESTED_PDU_LEN, 应答值)。
 */

// =============== 内部辅助 ===============

/** @brief 写 2 字节大端整数 */
static void xs7PduWriteBe16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

/** @brief 读 2 字节大端整数 */
static uint16_t xs7PduReadBe16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**
 * @brief 计算 ANY 读项的请求传输大小与 count 字段值
 * @details 对齐设计文档 §3 金样（s-pms build_read_byte_command：
 *          DB1.DBW10 → 10 02 00 02）的字节量传输约定：
 * - Bool → 0x01 BIT，count = 位个数；
 * - 其余类型统一 0x02 BYTE（字节量传输），count = 线上字节数：
 *   Byte ×1；Word ×2；DWord/Real ×4；String n+2（S7 STRING(n) 布局
 *   [max][cur][data]，用户给 count=n，偶数补齐由 PLC 保证）。
 */
static uint8_t xs7PduItemTransport(const XS7Address* item, uint16_t* outCount)
{
    switch (item->type) {
    case XS7Value_Bool:
        *outCount = item->count;
        return XS7_TRANSPORT_BIT;
    case XS7Value_Word:
        *outCount = (uint16_t)(item->count * 2);   /* 字 = 2 字节 */
        return XS7_TRANSPORT_BYTE;
    case XS7Value_DWord:
    case XS7Value_Real:
        *outCount = (uint16_t)(item->count * 4);   /* 双字/real = 4 字节 */
        return XS7_TRANSPORT_BYTE;
    case XS7Value_String:
        *outCount = (uint16_t)(item->count + 2);   /* STRING(n) 头两字节 + 数据 */
        return XS7_TRANSPORT_BYTE;
    case XS7Value_Byte:
    default:
        *outCount = item->count;
        return XS7_TRANSPORT_BYTE;
    }
}

/**
 * @brief 计算 ANY 项线上 3 字节地址
 * @details S7 ANY 项地址恒为位地址：普通区即 XS7Address.bitOffset
 *          （byte*8+bit，解析层已换算）原样上线（金样 DB1.DBW10 → 00 00 50）；
 *          T/C 区（s-pms 字访问编码 0x1E/0x1F）地址为元件序号原样（同存于 bitOffset）。
 */
static uint32_t xs7PduItemWireAddress(const XS7Address* item)
{
    return item->bitOffset;
}

/** @brief 组 12 字节 ANY 项（读请求/写请求同构） */
static void xs7PduBuildAnyItem(uint8_t* p, const XS7Address* item)
{
    uint16_t count = 0;
    uint8_t transport = xs7PduItemTransport(item, &count);
    uint32_t addr = xs7PduItemWireAddress(item);

    p[0] = 0x12;                              /* 项类型：变量规范 */
    p[1] = 0x0A;                              /* 后续长度 = 10 */
    p[2] = 0x10;                              /* 寻址方式：ANY */
    p[3] = transport;
    xs7PduWriteBe16(p + 4, count);
    xs7PduWriteBe16(p + 6, item->dbNumber);
    p[8] = item->area;
    p[9]  = (uint8_t)((addr >> 16) & 0xFF);
    p[10] = (uint8_t)((addr >> 8) & 0xFF);
    p[11] = (uint8_t)(addr & 0xFF);
}

/** @brief 组 S7 Job 头（10 字节）：32 | rosctr | 0000 | pduRef | paramLen | dataLen */
static void xs7PduBuildJobHeader(uint8_t* out, uint16_t pduRef,
                                 uint16_t paramLen, uint16_t dataLen)
{
    out[0] = 0x32;                            /* 协议 ID 固定 */
    out[1] = XS7_ROSCTR_JOB;
    out[2] = 0x00;                            /* 冗余标识（预留） */
    out[3] = 0x00;
    xs7PduWriteBe16(out + 4, pduRef);
    xs7PduWriteBe16(out + 6, paramLen);
    xs7PduWriteBe16(out + 8, dataLen);
}

// =============== 头解析 / 协商 ===============

bool XS7Pdu_parseHeader(const uint8_t* s7, size_t len, XS7PduHeader* out)
{
    if (!s7 || !out) return false;
    if (len < 10) return false;               /* 最短 Job 头 10 字节 */
    if (s7[0] != 0x32) return false;          /* 协议 ID */

    out->rosctr = s7[1];
    if (out->rosctr == XS7_ROSCTR_JOB) {
        /* Job：10 字节头，无错误字段 */
        out->pduRef   = xs7PduReadBe16(s7 + 4);
        out->paramLen = xs7PduReadBe16(s7 + 6);
        out->dataLen  = xs7PduReadBe16(s7 + 8);
        out->errorClass = 0;
        out->errorCode  = 0;
        return true;
    }
    if (out->rosctr == XS7_ROSCTR_ACK || out->rosctr == XS7_ROSCTR_ACK_DATA ||
        out->rosctr == XS7_ROSCTR_USERDATA) {
        /* Ack/Ack_Data/UserData：12 字节头（Wireshark s7comm 同规则） */
        if (len < 12) return false;
        out->pduRef   = xs7PduReadBe16(s7 + 4);
        out->paramLen = xs7PduReadBe16(s7 + 6);
        out->dataLen  = xs7PduReadBe16(s7 + 8);
        out->errorClass = s7[10];
        out->errorCode  = s7[11];
        return true;
    }
    return false;                             /* ROSCTR 非法 */
}

size_t XS7Pdu_buildSetupCommunication(uint8_t* out, uint16_t pduRef, uint16_t amq, uint16_t pduLength)
{
    if (!out) return 0;

    /* 头：32 01 0000 ref 0008 0000（参数段 8 字节、无数据段） */
    xs7PduBuildJobHeader(out, pduRef, 0x0008, 0x0000);
    /* 参数：F0 00 | AmQ-caller(BE) | AmQ-callee(BE) | PduLen(BE) */
    out[10] = XS7_FUNC_SETUP;
    out[11] = 0x00;
    xs7PduWriteBe16(out + 12, amq);
    xs7PduWriteBe16(out + 14, amq);
    xs7PduWriteBe16(out + 16, pduLength);
    return 18;
}

bool XS7Pdu_parseSetupAck(const uint8_t* s7, size_t len, uint16_t* negotiatedPduLen, uint16_t* amq)
{
    XS7PduHeader hdr;
    if (!negotiatedPduLen || !amq) return false;
    if (!XS7Pdu_parseHeader(s7, len, &hdr)) return false;
    if (hdr.rosctr != XS7_ROSCTR_ACK_DATA) return false;   /* 应答必为 Ack_Data */
    if (hdr.errorClass != 0x00) return false;              /* 头级错误（区别于业务结果） */
    if (hdr.paramLen < 8) return false;                    /* 参数段至少 8 字节 */
    if (len < 12 + 8) return false;

    if (s7[12] != XS7_FUNC_SETUP) return false;            /* 功能码 F0 */

    /* 参数：F0(12) 00(13) AmQCaller(14-15) AmQCallee(16-17) PduLen(18-19) */
    {
        uint16_t ackCaller = xs7PduReadBe16(s7 + 14);
        uint16_t ackCallee = xs7PduReadBe16(s7 + 16);
        uint16_t ackPdu    = xs7PduReadBe16(s7 + 18);

        if (ackPdu < 64) return false;    /* 协商值异常小（连最小 Job 头+参数都容不下） */

        /* 协商 PDU = min(请求值, 应答值)。本函数无请求值入参，请求值按首期固定
         * XS7_REQUESTED_PDU_LEN（960）计；若调用方修改了请求 PDU 长度，
         * 需在其侧再取一次 min（契约限制，见返回 notes）。 */
        *negotiatedPduLen = (ackPdu < (uint16_t)XS7_REQUESTED_PDU_LEN)
                            ? ackPdu : (uint16_t)XS7_REQUESTED_PDU_LEN;

        /* 协商 AmQ = min(首期 XS7_AMQ, 应答 caller/callee 两侧较小值)，
         * 正常应答两侧均 ≥1，结果即 XS7_AMQ=1（首期发送侧必须串行）。 */
        {
            uint16_t ackAmq = (ackCaller <= ackCallee) ? ackCaller : ackCallee;
            *amq = (ackAmq < (uint16_t)XS7_AMQ) ? ackAmq : (uint16_t)XS7_AMQ;
        }
    }
    return true;
}

// =============== Read Var ===============

size_t XS7Pdu_buildRead(uint8_t* out, uint16_t pduRef, const XS7Address* items, int itemCount)
{
    if (!out || !items || itemCount < 1) return 0;
    if (itemCount > 0xFF) return 0;           /* 项数占 1 字节 */

    {
        size_t paramLen = 2 + (size_t)itemCount * 12;      /* 04 + itemCount + N*ANY */
        size_t total = 10 + paramLen;
        int i;

        if (total > XS7_MAX_FRAME) return 0;

        xs7PduBuildJobHeader(out, pduRef, (uint16_t)paramLen, 0x0000);
        out[10] = XS7_FUNC_READ;
        out[11] = (uint8_t)itemCount;
        for (i = 0; i < itemCount; ++i) {
            xs7PduBuildAnyItem(out + 12 + (size_t)i * 12, &items[i]);
        }
        return total;
    }
}

bool XS7Pdu_parseReadAck(const uint8_t* s7, size_t len, XS7ReadItem* outItems, int maxItems)
{
    XS7PduHeader hdr;
    if (!outItems || maxItems < 1) return false;
    if (!XS7Pdu_parseHeader(s7, len, &hdr)) return false;
    if (hdr.rosctr != XS7_ROSCTR_ACK_DATA) return false;
    if (hdr.errorClass != 0x00) return false;  /* 头级错误（非单项错误） */
    if (hdr.paramLen < 2) return false;
    if ((size_t)12 + (size_t)hdr.paramLen + (size_t)hdr.dataLen > len) return false;

    if (s7[12] != XS7_FUNC_READ) return false;             /* 功能码 04 */
    {
        int itemCount = s7[13];
        int i;
        size_t off = 12 + (size_t)hdr.paramLen;            /* 数据区起点 */
        size_t end = off + (size_t)hdr.dataLen;

        if (itemCount < 1) return false;
        if (itemCount > maxItems) return false;            /* 项数超容 */

        for (i = 0; i < itemCount; ++i) {
            uint8_t rc;
            uint8_t transport;
            uint16_t length;

            if (off & 1) ++off;                            /* 项起始偶对齐 */
            if (off + 4 > end) return false;               /* 项头不完整 */

            rc        = s7[off];
            transport = s7[off + 1];
            length    = xs7PduReadBe16(s7 + off + 2);      /* 0x03/0x04 位计数，0x09 字节数 */

            outItems[i].returnCode = rc;
            outItems[i].transportSize = transport;
            outItems[i].bitCount = length;
            outItems[i].data = NULL;
            outItems[i].dataLen = 0;

            if (rc == XS7_RETURN_OK) {
                /* 成功项：FF 03/04 → 长度为位计数（按字节向上取整）；
                 *         FF 09（八位组串）→ 长度为字节数 */
                size_t dataBytes;
                if (transport == XS7_TRANSPORT_DATA_BIT ||
                    transport == XS7_TRANSPORT_DATA_BYTE) {
                    dataBytes = ((size_t)length + 7) / 8;
                } else if (transport == XS7_TRANSPORT_DATA_OCTET) {
                    dataBytes = (size_t)length;
                } else {
                    return false;                          /* 传输大小不支持 */
                }
                if (off + 4 + dataBytes > end) return false;   /* 长度不自洽 */
                outItems[i].data = s7 + off + 4;           /* 借用指针，帧释放后失效 */
                outItems[i].dataLen = dataBytes;
                off += 4 + dataBytes;
            } else {
                /* 其余全部非 0xFF 返回码（含 05/06/0A 及保留码 0x07/0x09/0x0B
                 * 等，真机实测 CPU 对数据类型不一致即回 0x07，见文件头注释）：
                 * 错误项同为固定 4 字节项头（长度字段通常 0x0000），按项头
                 * 透传给上层按 returnCode 精确判定，不判整包解析失败 */
                off += 4;
            }
        }
    }
    return true;
}

// =============== Write Var ===============

/**
 * @brief 计算写数据项的数据区传输大小
 * @details 计时/计数区写值与 STRING 写用八位组串结构（00 09，长度=字节数）；
 *          位写 00 03；其余（字节/字/双字/real）统一按字节串 00 04。
 *          （STRING 用 0x09 的真机依据见文件头【真机实测修正】注释）
 */
static uint8_t xs7PduWriteTransport(const XS7Address* item)
{
    if (item->area == (uint8_t)XS7Area_T || item->area == (uint8_t)XS7Area_C) {
        return XS7_TRANSPORT_DATA_OCTET;
    }
    if (item->type == XS7Value_Bool) {
        return XS7_TRANSPORT_DATA_BIT;
    }
    if (item->type == XS7Value_String) {
        return XS7_TRANSPORT_DATA_OCTET;
    }
    return XS7_TRANSPORT_DATA_BYTE;
}

/**
 * @brief 计算写数据项长度字段的线上值
 * @param item 地址项（决定传输大小）
 * @param bitCount 写入位计数（调用方口径恒为位；0x09 分支内部折算为字节数）
 * @return 长度字段值（0x03/0x04 为位计数，0x09 为字节数）
 */
static uint16_t xs7PduWriteLengthField(const XS7Address* item, uint16_t bitCount)
{
    if (xs7PduWriteTransport(item) == XS7_TRANSPORT_DATA_OCTET) {
        return (uint16_t)(((uint32_t)bitCount + 7u) / 8u);   /* 八位组串：字节数 */
    }
    return bitCount;                                          /* 0x03/0x04：位计数 */
}

/**
 * @brief 计算写数据项携带的线上数据字节数
 * @param item 地址项（决定传输大小，此处仅用作文档语义）
 * @param bitCount 写入位计数（调用方口径恒为位）
 * @return 线上数据字节数（各传输大小分支均为位计数向上取整字节；
 *         0x09 八位组串的长度字段值即本字节数，见 xs7PduWriteLengthField）
 */
static size_t xs7PduWriteDataBytes(const XS7Address* item, uint16_t bitCount)
{
    (void)item;
    return ((size_t)bitCount + 7u) / 8u;
}

size_t XS7Pdu_buildWrite(uint8_t* out, uint16_t pduRef, const XS7Address* items, int itemCount,
                         const uint8_t* const* datas, const uint16_t* bitCounts)
{
    if (!out || !items || itemCount < 1 || !datas || !bitCounts) return 0;
    if (itemCount > 0xFF) return 0;           /* 项数占 1 字节 */

    {
        size_t paramLen = 2 + (size_t)itemCount * 12;
        size_t dataLen = 0;
        size_t off;
        int i;
        size_t total;

        /* 先校验入参并预算数据区长度（项起始偶对齐：非偶数长度项尾补 1 字节） */
        for (i = 0; i < itemCount; ++i) {
            size_t itemLen;
            if (!datas[i] || bitCounts[i] == 0) return 0;
            itemLen = 4 + xs7PduWriteDataBytes(&items[i], bitCounts[i]);
            if (dataLen & 1) ++dataLen;
            dataLen += itemLen;
        }
        total = 10 + paramLen + dataLen;
        if (total > XS7_MAX_FRAME) return 0;

        xs7PduBuildJobHeader(out, pduRef, (uint16_t)paramLen, (uint16_t)dataLen);

        /* 参数段：05 | itemCount | N*ANY（与读同构） */
        out[10] = XS7_FUNC_WRITE;
        out[11] = (uint8_t)itemCount;
        for (i = 0; i < itemCount; ++i) {
            xs7PduBuildAnyItem(out + 12 + (size_t)i * 12, &items[i]);
        }

        /* 数据段：逐项 00 | transport | 长度(BE) | 数据（项起始偶对齐）；
         * 长度字段按传输大小分支取值：0x03/0x04=位计数、0x09=字节数 */
        off = 10 + paramLen;
        for (i = 0; i < itemCount; ++i) {
            size_t n;
            if (off & 1) out[off++] = 0x00;   /* 偶对齐填充 */
            out[off] = 0x00;                  /* 返回码保留位（请求侧恒 0） */
            out[off + 1] = xs7PduWriteTransport(&items[i]);
            xs7PduWriteBe16(out + off + 2, xs7PduWriteLengthField(&items[i], bitCounts[i]));
            off += 4;
            n = xs7PduWriteDataBytes(&items[i], bitCounts[i]);
            memcpy(out + off, datas[i], n);
            off += n;
        }
        return total;
    }
}

bool XS7Pdu_parseWriteAck(const uint8_t* s7, size_t len, int expectItems, uint8_t* returnCodes)
{
    XS7PduHeader hdr;
    if (!returnCodes || expectItems < 1) return false;
    if (!XS7Pdu_parseHeader(s7, len, &hdr)) return false;
    if (hdr.rosctr != XS7_ROSCTR_ACK_DATA) return false;
    if (hdr.errorClass != 0x00) return false;  /* 头级错误 */
    if (hdr.paramLen < 2) return false;
    if ((size_t)12 + (size_t)hdr.paramLen > len) return false;

    if (s7[12] != XS7_FUNC_WRITE) return false;            /* 功能码 05 */
    if (s7[13] != (uint8_t)expectItems) return false;      /* 项数不符 */

    /* 写应答数据区：逐项 1 字节返回码 */
    {
        size_t off = 12 + (size_t)hdr.paramLen;
        int i;
        if (off + (size_t)expectItems > len) return false;
        for (i = 0; i < expectItems; ++i) {
            returnCodes[i] = s7[off + (size_t)i];
        }
    }
    return true;
}

// =============== 分片预算 ===============

int XS7Pdu_maxReadItems(uint16_t pduLen)
{
    int v = ((int)pduLen - 19) / 12;          /* 头 10 + 参数 2 + item 12 */
    return (v < 1) ? 1 : v;
}

int XS7Pdu_maxReadBytesPerItem(uint16_t pduLen)
{
    int v = (int)pduLen - 18;                 /* Ack 头 12 + 参数 2 + 项头 4 */
    return (v < 1) ? 1 : v;
}

int XS7Pdu_maxWriteBytes(uint16_t pduLen)
{
    int v = (int)pduLen - 35;                 /* 头 19 + ANY 12 + 数据头 4 */
    return (v < 1) ? 1 : v;
}

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
