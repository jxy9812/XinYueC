/** @file XPlc_config.h
 * @brief PLC 协议族伞配置文件（XPlc 伞层 + 西门子 S7）
 *
 * 通过此配置文件可以裁剪 PLC 协议族的子功能：
 *   1. XPLC_ON       - PLC 协议族总开关（默认随 XPROTOCOL_ON）
 *   2. XPLC_CORE_ON  - 伞层三类（XPlcDevice / XPlcClient / XPlcReply）
 *
 * 子协议开关经文件末尾 #include "XS7_config.h" 引入（同 XProtocol_config 的登记模式）：
 *   XS7_ON -> XS7_CORE_ON / XS7_CONTROL_ON / XS7_BLOCK_ON
 *
 * 协议族总开关 XPLC_ON 在 XProtocol_config.h 中登记默认值，此处仅提供兜底默认。
 * 未来第二协议（如三菱 MC）：在本文件末尾追加一行 #include "XMc_config.h" 即可，
 * 不再修改 XProtocol_config.h，伞层零改动。
 *
 * @note 裁剪约束：XS7_ON 默认等于 XPLC_CORE_ON（XS7TcpClient 继承伞层），
 *       保持二者一致；若 XPLC_CORE_ON=0 而强制 XS7_ON=1，伞类消失而 S7 类
 *       引用伞类类型将导致编译失败。
 */

#ifndef XPLC_CONFIG_H
#define XPLC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XPLC_ON
#define XPLC_ON XPROTOCOL_ON
#endif

#if XPLC_ON

/* ========================================================================== */
/*                        子功能开关                                          */
/* ========================================================================== */

/** @brief 伞层三类（XPlcDevice / XPlcClient / XPlcReply，协议无关机制层） */
#ifndef XPLC_CORE_ON
#define XPLC_CORE_ON 1
#endif

/* 引入 S7 子协议开关（子协议自行登记在自身 config 中） */
#include "XS7_config.h"

#endif /* XPLC_ON */

#ifdef __cplusplus
}
#endif

#endif /* XPLC_CONFIG_H */
