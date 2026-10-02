#include "XPlc_config.h"
#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CONTROL_ON
#include "XS7Control.h"
#include "XS7Pdu.h"
#include <string.h>

/**
 * @file XS7Control.c
 * @brief S7 运维：PLC Run(0x28 热/冷启动)/Stop(0x29) 报文构造与应答判定实现
 * @details 字节金样（双源核验：s-pms siemens_s7_private.h 金样 + snap7
 *          s7_micro_client.cpp opPlcStop/opPlcHotStart/opPlcColdStart）：
 * - Stop 参数（16 字节，paramLen=0x0010，S7 PDU 共 26 字节，帧总长 0x21）：
 *     29 | 00 00 00 00 00 | 09 | "P_PROGRAM"
 *     （snap7 结构：Fun + Uk_5[5] + Len_2 + 9 字节命令串，逐字节一致）
 * - Hot Start 参数（20 字节，paramLen=0x0014，S7 PDU 共 30 字节）：
 *     28 | 00 00 00 00 00 00 FD | 00 00 | 09 | "P_PROGRAM"
 *     （snap7 结构：Fun + Uk_7[6x00+FD] + Len_1=0x0000 + Len_2 + 命令串）
 * - Cold Start：按冻结契约在 Hot 参数尾部追加 02 43 20（"C "，02 为长度），
 *     参数 23 字节，paramLen=0x0017，S7 PDU 共 33 字节。
 *     @note snap7 的冷启动把 [00 02][43 20]（Len_1+SFun）放在 "P_PROGRAM"
 *     之前（22 字节变体）；本实现按冻结契约/金样（Hot 尾部追加）执行，
 *     阶段 0 实测如遇拒绝可在此处切换布局（两种布局均携带 "C "）。
 * - 应答判定：Ack_Data（头 12 字节，errorClass=0x00）；状态字节取应答
 *     数据区首个数据项载荷首字节（0x02 已运行 / 0x07 已停止，
 *     s-pms g_pdu_already_started/g_pdu_already_stopped）。
 */

/** @brief "P_PROGRAM" 的线上字节长度 */
#define XS7_CONTROL_PROGRAM_NAME_LEN 9

// =============== 内部辅助 ===============

/** @brief 组 S7 Job 头（10 字节）：32 | rosctr | 0000 | pduRef | paramLen | dataLen */
static void xs7ControlBuildJobHeader(uint8_t* out, uint16_t pduRef,
                                     uint16_t paramLen, uint16_t dataLen)
{
    out[0] = 0x32;                            /* 协议 ID 固定 */
    out[1] = XS7_ROSCTR_JOB;
    out[2] = 0x00;                            /* 冗余标识（预留） */
    out[3] = 0x00;
    out[4] = (uint8_t)(pduRef >> 8);
    out[5] = (uint8_t)(pduRef & 0xFF);
    out[6] = (uint8_t)(paramLen >> 8);
    out[7] = (uint8_t)(paramLen & 0xFF);
    out[8] = (uint8_t)(dataLen >> 8);
    out[9] = (uint8_t)(dataLen & 0xFF);
}

/** @brief 追加 [长度字节][字符串] 结构到 out 的 off 偏移，返回新偏移 */
static size_t xs7ControlAppendLenString(uint8_t* out, size_t off, const char* text, uint8_t len)
{
    out[off] = len;
    memcpy(out + off + 1, text, len);
    return off + 1 + (size_t)len;
}

// =============== 报文构造 ===============

size_t XS7Control_buildRun(uint8_t* out, uint16_t pduRef, XS7RunMode mode)
{
    if (!out) return 0;

    /* 参数（Hot 20 字节 / Cold 23 字节）：
     *   Hot ：28 | 00 00 00 00 00 00 FD | 00 00 | 09 | "P_PROGRAM"
     *   Cold：Hot 参数尾部追加 02 43 20（"C "，02 为长度，冻结契约布局） */
    const uint16_t paramLen = (mode == XS7RunMode_Cold)
                              ? (uint16_t)(XS7_CONTROL_PROGRAM_NAME_LEN + 14)
                              : (uint16_t)(XS7_CONTROL_PROGRAM_NAME_LEN + 11);
    size_t off;

    xs7ControlBuildJobHeader(out, pduRef, paramLen, 0x0000);

    out[10] = XS7_FUNC_RUN;                   /* 0x28 */
    out[11] = 0x00;
    out[12] = 0x00;
    out[13] = 0x00;
    out[14] = 0x00;
    out[15] = 0x00;
    out[16] = 0x00;
    out[17] = 0xFD;                           /* 未知字节 7：热启动标识（snap7 Uk_7[6] 同值） */
    out[18] = 0x00;                           /* 参数块长度（Hot/Cold 均无前置参数块） */
    out[19] = 0x00;
    /* [09]["P_PROGRAM"]（冻结契约布局：命令串位于尾部） */
    off = xs7ControlAppendLenString(out, 20, XS7_CONTROL_PROGRAM_NAME, XS7_CONTROL_PROGRAM_NAME_LEN);
    if (mode == XS7RunMode_Cold) {
        /* 冻结契约：Cold 在 Hot 参数尾部追加 02 43 20（"C "） */
        off = xs7ControlAppendLenString(out, off, "C ", 2);
    }
    return 10 + (size_t)paramLen;
}

size_t XS7Control_buildStop(uint8_t* out, uint16_t pduRef)
{
    if (!out) return 0;

    /* 参数（16 字节）：29 | 00 00 00 00 00 | 09 | "P_PROGRAM" */
    const uint16_t paramLen = (uint16_t)(XS7_CONTROL_PROGRAM_NAME_LEN + 7);

    xs7ControlBuildJobHeader(out, pduRef, paramLen, 0x0000);

    out[10] = XS7_FUNC_STOP;                  /* 0x29 */
    out[11] = 0x00;
    out[12] = 0x00;
    out[13] = 0x00;
    out[14] = 0x00;
    out[15] = 0x00;
    (void)xs7ControlAppendLenString(out, 16, XS7_CONTROL_PROGRAM_NAME, XS7_CONTROL_PROGRAM_NAME_LEN);
    return 10 + (size_t)paramLen;
}

// =============== 应答判定 ===============

bool XS7Control_parseAck(const uint8_t* s7, size_t len, uint8_t* outStatus)
{
    XS7PduHeader hdr;
    if (!s7) return false;
    if (outStatus) *outStatus = 0;

    if (!XS7Pdu_parseHeader(s7, len, &hdr)) return false;
    /* 正常成功应答为 Ack_Data；CPU 拒绝时可能回 rosctr=ACK（0x02，头带错误域，
     * 真机实测 S7-1200 Stop/Run 拒绝即此形态 81 04）——ACK 形态交上层头错误
     * 分支报 ProtocolError，此处仅对错误域为 0 的 Ack_Data 做状态解析 */
    if (hdr.rosctr != XS7_ROSCTR_ACK_DATA) return false;
    if (hdr.errorClass != 0x00) return false;              /* 头级错误（参数非法/拒绝执行） */
    if (hdr.paramLen < 1) return false;                    /* 参数段至少含功能码回显 */
    if ((size_t)12 + (size_t)hdr.paramLen > len) return false;

    /* 状态字节：应答数据区首个数据项（ret | transport | 长度 | 载荷）的
     * 载荷首字节。无数据段或空载荷时保持 0（视为常规成功）。 */
    if (outStatus && hdr.dataLen >= 5) {
        size_t off = 12 + (size_t)hdr.paramLen;            /* 数据区起点 */
        if (off + 4 <= len) {                              /* 先保证 4 字节项头可读（帧截断时防越界） */
            size_t itemLen = (size_t)(((uint16_t)s7[off + 2] << 8) | (uint16_t)s7[off + 3]);
            if (off + 4 + itemLen <= len && itemLen >= 1) {
                *outStatus = s7[off + 4];
            }
        }
    }
    return true;
}

bool XS7Control_statusIsSuccess(uint8_t status)
{
    /* 常规成功：0x00/0xFF；
     * "已处于目标状态"（s-pms g_pdu_already_started=0x02 / g_pdu_already_stopped=0x07）
     * 按成功处理，避免操作者误判失败而重复触发（上层写 errorString 提示）。 */
    return (status == 0x00 || status == 0xFF ||
            status == XS7_CONTROL_STATUS_ALREADY_RUNNING ||
            status == XS7_CONTROL_STATUS_ALREADY_STOPPED);
}

#endif /* XS7_CONTROL_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */
