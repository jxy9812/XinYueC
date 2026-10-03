#ifndef XS7ADDRESS_H
#define XS7ADDRESS_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include "XString.h"
#include "XS7Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/**
 * @file XS7Address.h
 * @brief S7 地址语法解析（纯函数，无IO无状态）
 * @details 支持语法（对齐 s-pms s7_analysis_address，其 2026-03 边界用例
 *          直接作单测集）：
 *          `DB1.DBW10`、`DB1.DBX0.1`、`D10`(=DB)、`M100/MX0.1/MB/MW/MD`、
 *          `I/IB/IW/ID`、`Q*`、`V*`、`T100`、`C100`；
 *          拒绝：空串、`MX0.8`（bit>7）、`MX0.A`、多点号、非数字。
 * @note 大小写不敏感（解析前转大写副本；按规范用 ASCII 表，
 *       禁 ctype.h toupper、禁照抄 s-pms str_toupper）
 */

/**
 * @brief S7 地址结构体
 * @details 解析结果；非法地址可用 XS7Address_isValid 判定
 */
typedef struct XS7Address {
    uint8_t  area;       ///< 区域线码（XS7Area：DB=0x84 I=0x81 Q=0x82 M=0x83 T/C=0x1E/0x1F，V 映射 0x84）
    uint16_t dbNumber;   ///< 数据块号（仅 DB/V 有效，否则 0）
    uint32_t bitOffset;  ///< 位偏移（普通区 = byte*8+bit；T/C = 序号，字访问）
    XS7ValueType type;   ///< 值类型（由 DBW/DBD/X/B/W/D 后缀或调用参数推导）
    uint16_t count;      ///< 元素个数（默认 1）
} XS7Address;

/**
 * @brief 解析地址字符串（主版本，XString）
 * @param out 输出解析结果（非NULL）
 * @param addr 地址字符串（非NULL）
 * @return 解析成功返回true；空串/非法字符/位号越界/多点号返回false（out 不可信）
 */
bool XS7Address_parse(XS7Address* out, const XString* addr);

/**
 * @brief 解析地址字符串（UTF-8 转发版本）
 * @param out 输出解析结果（非NULL）
 * @param addrUtf8 UTF-8 地址字符串（非NULL）
 * @return 解析成功返回true，否则false
 * @note 主/转发关系同规范"字符串主次顺序"约定
 */
bool XS7Address_parse_2(XS7Address* out, const char* addrUtf8);

/**
 * @brief 地址转回字符串（诊断用）
 * @param addr 地址结构（非NULL）
 * @return 新建的 XString（调用者负责 XClassDelete），失败返回NULL
 */
XString* XS7Address_toString(const XS7Address* addr);

/**
 * @brief 判定地址结构是否有效
 * @param addr 地址结构（非NULL）
 * @return 有效返回true；area 非法、DB/V 缺块号、位偏移越界等返回false
 */
bool XS7Address_isValid(const XS7Address* addr);

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7ADDRESS_H
