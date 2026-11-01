#ifndef XS7COTP_H
#define XS7COTP_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/**
 * @file XS7Cotp.h
 * @brief COTP 层（RFC905/ISO8073）CR/CC/DT 构造与解析（纯函数，无IO无状态）
 * @details ISO-on-TCP 连接建立与数据传输段；字节模板对齐 s-pms g_plc_head1 金样。
 *
 * @par 字节依据（已核验）
 * - CR 模板 = s-pms `g_plc_head1_s1200`：
 *   03 00 00 16 11 E0 00 00 00 01 00 C0 01 0A C1 02 ll ll C2 02 rr rr
 *   其中目标 TSAP 低字节 = rack*0x20 + slot（siemens_s7.c set_plc_rack/slot）
 * - S7NetPlus ConnectionRequest 参数顺序为 C1..C2..C0、TSAP=(0x03, rack<<5|slot)；
 *   首期以 s-pms 参数顺序为准，connType（TSAP 首字节）缺省 0x01
 */

/** @brief COTP CR（连接请求）PDU 类型码 */
#define XS7COTP_PDU_CR 0xE0
/** @brief COTP CC（连接确认）PDU 类型码 */
#define XS7COTP_PDU_CC 0xD0
/** @brief COTP DT（数据）PDU 类型码 */
#define XS7COTP_PDU_DT 0xF0

/**
 * @brief CC 应答解析结果
 * @details 解析 C0/C1/C2 参数；参数码 0x50 为拒绝原因
 */
typedef struct XS7CotpCcInfo {
    uint8_t  tpduSizeCode;   ///< 协商的 TPDU 大小码（C0 参数）
    uint16_t localTsap;      ///< 本端 TSAP（C1 参数）
    uint16_t remoteTsap;     ///< 目标 TSAP（C2 参数）
} XS7CotpCcInfo;

/**
 * @brief 按机架/槽号计算目标 TSAP
 * @param connType 连接类型（TSAP 高字节，缺省 0x01）
 * @param rack 机架号 0..7
 * @param slot 槽号 0..31
 * @return 目标 TSAP = (connType << 8) | ((rack << 5) | slot)（S7NetPlus TsapPair 公式）
 */
uint16_t XS7Cotp_remoteTsapFromRackSlot(uint8_t connType, uint8_t rack, uint8_t slot);

/**
 * @brief 产出完整 COTP CR 帧（含 TPKT，写入 out 尾部）
 * @param out 输出字节数组（追加写到尾部，非NULL）
 * @param localTsap 本端 TSAP（C1 参数）
 * @param remoteTsap 目标 TSAP（C2 参数，可用 XS7Cotp_remoteTsapFromRackSlot 计算）
 * @param tpduSizeCode TPDU 大小码（C0 参数，s-pms 用 0x0A）
 * @return 成功返回帧总长，失败返回0
 * @note 参数顺序 C0/C1/C2 对齐 s-pms g_plc_head1 金样
 */
size_t XS7Cotp_buildCr(XByteArray* out, uint16_t localTsap, uint16_t remoteTsap, uint8_t tpduSizeCode);

/**
 * @brief 解析 CC（连接确认）
 * @param frame 完整帧（含 TPKT）
 * @param len 帧长
 * @param out 输出解析结果（非NULL）
 * @return 接受返回true；PDU type!=0xD0、长度非法或含拒绝参数（参数码0x50为
 *         拒绝原因）返回false
 */
bool XS7Cotp_parseCc(const uint8_t* frame, size_t len, XS7CotpCcInfo* out);

/**
 * @brief 从 DT 帧提取 S7 PDU
 * @param frame 完整帧（含 TPKT）
 * @param len 帧长
 * @param s7Offset 输出：S7 PDU 在帧内偏移
 * @param s7Len 输出：S7 PDU 长度
 * @return 成功返回true；DT 头不是固定 02 F0 80 或长度非法返回false
 */
bool XS7Cotp_extractDt(const uint8_t* frame, size_t len, size_t* s7Offset, size_t* s7Len);

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7COTP_H
