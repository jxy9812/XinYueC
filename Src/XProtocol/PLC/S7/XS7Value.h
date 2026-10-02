#ifndef XS7VALUE_H
#define XS7VALUE_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XVariant.h"
#include "XByteArray.h"
#include "XS7Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/**
 * @file XS7Value.h
 * @brief S7 值编解码（类型 ⇄ 大端字节，纯函数，无IO无状态）
 * @details 编解码规则：
 * - 大端一律经 XMemory_read_data/write_data(XBYTE_ORDER_BIG_ENDIAN)
 * - float 经 uint32 位型中转（禁 *(float*)&u32 strict-aliasing 强转）
 * - S7 STRING(n) 布局 = [maxLen][curLen][data...]，总长 n+2 且偶数补齐；
 *   读长度 = n+2（用户给 count=n）
 * - bool 单元素为 1 字节（0x00/0x01）；多元素按"LSB 在前位打包"承载，
 *   载荷长 ceil(N/8) 字节（bit k → [k>>3] 字节的 k&7 位），与 CPU 位读回、
 *   XS7Pdu 0x03/0x04 数据分支同口径
 * - bool 走位传输（请求 ANY transport=0x01、写数据 transport=0x03）
 */

/**
 * @brief 值编码（XVariant → 大端字节）
 * @param type 值类型
 * @param count 元素个数（String 时为 STRING 容量 n）
 * @param value 源值（bool/int/real/string 等，按 type 解释；非NULL）
 * @param out 输出字节数组（追加写到尾部，非NULL）
 * @return 编码成功返回true；类型不支持/参数非法/容量不足返回false
 */
bool XS7Value_encode(XS7ValueType type, int count, const XVariant* value, XByteArray* out);

/**
 * @brief 值解码（大端字节 → XVariant）
 * @param type 值类型
 * @param count 元素个数（String 时为 STRING 容量 n）
 * @param data 源字节（非NULL）
 * @param dataLen 源字节长度
 * @param out 输出值（非NULL，内部写入）
 * @return 解码成功返回true；长度不足/类型不支持返回false
 */
bool XS7Value_decode(XS7ValueType type, int count, const uint8_t* data, size_t dataLen, XVariant* out);

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7VALUE_H
