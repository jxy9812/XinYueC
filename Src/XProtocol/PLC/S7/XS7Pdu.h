#ifndef XS7PDU_H
#define XS7PDU_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XS7Types.h"
#include "XS7Address.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/**
 * @file XS7Pdu.h
 * @brief S7Comm 组包/解包核心（纯函数，无IO无状态）
 * @details 10/12 字节头解析、SetupCommunication 请求/应答、
 *          ReadVar(0x04)/WriteVar(0x05) 项构造与应答解析、PDU 分片预算计算。
 *
 * @par 组包依据（已核验）
 * - 读请求：S7 头(32 01 0000 ref paramLen dataLen)+04 itemCount，每 item 12 字节
 *   （s-pms build_read_byte_command；S7NetPlus WriteReadHeader 同构：19+12*amount）
 * - 写请求：func 0x05，数据区尾部 00 04|bitCount(BE)（字节写）或 00 03（位写）、
 *   00 09（计时/计数结构）
 * - 读应答 data：FF 04|bitCnt|data…、FF 09|cnt|3/5 字节结构；错误 05 00/06 00/0A 00
 * - Setup：请求 F0 00 AmQ(BE) AmQ(BE) PduLen(BE)；首期发 AmQ=1、请求 960，
 *   协商结果按结构解析取 min(请求值, 应答值)，不照抄 s-pms "末2字节-28"土办法
 */

/******************************************************************************************
 * 结构体定义
 ******************************************************************************************/

 /**
  * @brief S7Comm 头解析结果
  * @details Job 头长10字节，Ack_Data 头长12字节（多 errorClass/errorCode 两字节）
  */
typedef struct XS7PduHeader {
    uint8_t  rosctr;      ///< ROSCTR（XS7_ROSCTR_*）
    uint16_t pduRef;      ///< PDU 引用号（回填 pending 定位）
    uint16_t paramLen;    ///< 参数段长度
    uint16_t dataLen;     ///< 数据段长度
    uint8_t  errorClass;  ///< 错误类（Ack_Data 有效）
    uint8_t  errorCode;   ///< 错误码（Ack_Data 有效）
} XS7PduHeader;

 /**
  * @brief 读应答单项结果
  * @details 指向应答缓冲内部（借用，帧释放后失效）
  */
typedef struct XS7ReadItem {
    uint8_t  returnCode;     ///< 返回码（XS7_RETURN_*，0xFF 成功）
    uint8_t  transportSize;  ///< 传输大小（XS7_TRANSPORT_*）
    uint16_t bitCount;       ///< 位计数
    const uint8_t* data;     ///< 数据指针（借用，调用者不得释放）
    size_t   dataLen;        ///< 数据字节长度
} XS7ReadItem;

/******************************************************************************************
 * 头解析 / 协商
 ******************************************************************************************/

 /**
  * @brief 解析 S7Comm 头
  * @param s7 S7 PDU 起始（已剥 TPKT/COTP）
  * @param len 可用长度（Job 至少 10，Ack_Data 至少 12）
  * @param out 输出解析结果（非NULL）
  * @return 成功返回true；长度不足、ROSCTR 非法返回false
  */
bool XS7Pdu_parseHeader(const uint8_t* s7, size_t len, XS7PduHeader* out);

/**
 * @brief 构造 SetupCommunication 请求
 * @param out 输出缓冲（至少 16 字节，写入 S7 PDU，不含 TPKT/COTP）
 * @param pduRef PDU 引用号
 * @param amq 请求的最大并行任务数（首期 1）
 * @param pduLength 请求的 PDU 长度（首期 960）
 * @return 写入字节数，失败返回0
 */
size_t XS7Pdu_buildSetupCommunication(uint8_t* out, uint16_t pduRef, uint16_t amq, uint16_t pduLength);

/**
 * @brief 解析 SetupCommunication 应答并计算协商结果
 * @param s7 S7 PDU 起始
 * @param len 可用长度
 * @param negotiatedPduLen 输出：协商后的 PDU 长度 = min(请求值, 应答值)
 * @param amq 输出：协商后的最大并行任务数
 * @return 成功返回true；功能码不符/长度非法/协商值异常小返回false
 */
bool XS7Pdu_parseSetupAck(const uint8_t* s7, size_t len, uint16_t* negotiatedPduLen, uint16_t* amq);

/******************************************************************************************
 * Read Var / Write Var
 ******************************************************************************************/

 /**
  * @brief 构造读请求（单请求多 item）
  * @param out 输出缓冲（写入 S7 PDU，不含 TPKT/COTP）
  * @param pduRef PDU 引用号
  * @param items 地址项数组（ANY: 12 0A 10 | transport | count | db | area | 3B bit-offset）
  * @param itemCount 项数（≥1）
  * @return 写入字节数（19+12*itemCount），失败返回0
  */
size_t XS7Pdu_buildRead(uint8_t* out, uint16_t pduRef, const XS7Address* items, int itemCount);

/**
 * @brief 解析读应答
 * @param s7 S7 PDU 起始
 * @param len 可用长度
 * @param outItems 输出项数组（data 为借用指针）
 * @param maxItems 输出数组容量
 * @return 成功返回true；头非法、项数超容、传输大小/长度不自洽返回false
 * @note 单项错误（0x05/0x06/0x0A）不算解析失败，写入 outItems[i].returnCode 由上层判定
 */
bool XS7Pdu_parseReadAck(const uint8_t* s7, size_t len, XS7ReadItem* outItems, int maxItems);

/**
 * @brief 构造写请求（单请求多 item）
 * @param out 输出缓冲（写入 S7 PDU，不含 TPKT/COTP）
 * @param pduRef PDU 引用号
 * @param items 地址项数组
 * @param itemCount 项数（≥1）
 * @param datas 各项数据（大端字节，位写为打包位）
 * @param bitCounts 各项位计数
 * @return 写入字节数，失败返回0
 */
size_t XS7Pdu_buildWrite(uint8_t* out, uint16_t pduRef, const XS7Address* items, int itemCount,
                         const uint8_t* const* datas, const uint16_t* bitCounts);

/**
 * @brief 解析写应答
 * @param s7 S7 PDU 起始
 * @param len 可用长度
 * @param expectItems 期望项数
 * @param returnCodes 输出：各项返回码（0xFF 成功）
 * @return 头与项结构合法返回true（单项错误经 returnCodes 上层判定）；
 *         头非法/项数不符返回false
 */
bool XS7Pdu_parseWriteAck(const uint8_t* s7, size_t len, int expectItems, uint8_t* returnCodes);

/******************************************************************************************
 * 分片预算（见实施方案 §3.4：单 item、按字节切片）
 ******************************************************************************************/

 /**
  * @brief 单帧可容纳的读 item 数
  * @param pduLen 协商后的 PDU 长度
  * @return 最大 item 数 = (pduLen-19)/12（下限1）
  */
int XS7Pdu_maxReadItems(uint16_t pduLen);

/**
 * @brief 单帧读单 item 的最大字节数（响应侧约束，保守）
 * @param pduLen 协商后的 PDU 长度
 * @return 最大字节数 = pduLen-18（Ack头12+param2+item头4 开销；下限1）
 */
int XS7Pdu_maxReadBytesPerItem(uint16_t pduLen);

/**
 * @brief 单帧写数据的最大字节数（请求侧约束）
 * @param pduLen 协商后的 PDU 长度
 * @return 最大字节数 = pduLen-35（19头+12 ANY+4 数据头；下限1）
 */
int XS7Pdu_maxWriteBytes(uint16_t pduLen);

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7PDU_H
