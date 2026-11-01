#ifndef XS7CONTROL_H
#define XS7CONTROL_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XS7Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CONTROL_ON

/**
 * @file XS7Control.h
 * @brief S7 运维：PLC Run(0x28 热/冷启动)/Stop(0x29) 报文构造与应答判定（纯函数）
 * @details 参数段含 "P_PROGRAM" 程序名；金样对齐 s-pms siemens_s7_private.h：
 * - Stop：func 0x29，参数 ... 00 09 "P_PROGRAM"
 * - Hot Start：func 0x28，参数含 ... FD 00 00 09 "P_PROGRAM"
 * - Cold Start：在 Hot 之后追加 02 43 20（"C "）
 * - 应答判定参考 s-pms g_pdu_already_started=0x02 / g_pdu_already_stopped=0x07，
 *   "已处于该状态"视为成功（写进 errorString 提示，避免操作者重复触发）
 */

/** @brief "P_PROGRAM" 程序名（Run/Stop 参数段固定携带） */
#define XS7_CONTROL_PROGRAM_NAME "P_PROGRAM"

/**
 * @brief 构造 PLC Run 启动请求
 * @param out 输出缓冲（写入 S7 PDU，不含 TPKT/COTP，至少 32 字节）
 * @param pduRef PDU 引用号
 * @param mode 启动模式（XS7RunMode_Hot 热启动 / XS7RunMode_Cold 冷启动）
 * @return 写入字节数，失败返回0
 */
size_t XS7Control_buildRun(uint8_t* out, uint16_t pduRef, XS7RunMode mode);

/**
 * @brief 构造 PLC Stop 停止请求
 * @param out 输出缓冲（写入 S7 PDU，不含 TPKT/COTP，至少 32 字节）
 * @param pduRef PDU 引用号
 * @return 写入字节数，失败返回0
 */
size_t XS7Control_buildStop(uint8_t* out, uint16_t pduRef);

/**
 * @brief 解析 Run/Stop 应答
 * @param s7 S7 PDU 起始
 * @param len 可用长度
 * @param outStatus 输出：应答参数首字节状态码（0x02 已运行 / 0x07 已停止）
 * @return 头部与参数合法返回true，否则false
 */
bool XS7Control_parseAck(const uint8_t* s7, size_t len, uint8_t* outStatus);

/**
 * @brief 判定应答状态码是否视为成功
 * @param status 应答状态码
 * @return 成功返回true（0x02 已运行 / 0x07 已停止按成功处理），否则false
 */
bool XS7Control_statusIsSuccess(uint8_t status);

#endif /* XS7_CONTROL_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7CONTROL_H
