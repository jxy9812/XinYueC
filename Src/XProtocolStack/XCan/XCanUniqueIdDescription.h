#ifndef XCANUNIQUEIDDESCRIPTION_H
#define XCANUNIQUEIDDESCRIPTION_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XCanCommonDefinitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanUniqueIdDescription.h
 * @brief CAN 唯一 ID 描述头文件（对齐 Qt6 QCanUniqueIdDescription）
 * @details 定义 CAN 消息唯一 ID 的描述信息，包括数据源、起始位、
 *          位长度和字节序。用于从 CAN 帧中提取唯一标识符。
 *          这是一个值类型结构体，不继承 XClass。
 *
 * @par 功能特性
 * - 数据源选择（Payload 或 FrameId）
 * - 起始位和位长度配置
 * - 字节序配置（大端/小端）
 *
 * @par 使用示例
 * @code
 * XCanUniqueIdDescription desc;
 * XCanUniqueIdDescription_init(&desc);
 * XCanUniqueIdDescription_setSource(&desc, XCanBus_Payload);
 * XCanUniqueIdDescription_setStartBit(&desc, 0);
 * XCanUniqueIdDescription_setBitLength(&desc, 29);
 * XCanUniqueIdDescription_setEndian(&desc, 1); // 大端
 * @endcode
 */

/******************************************************************************************
 * CAN 唯一 ID 描述结构体
 ******************************************************************************************/

/**
 * @brief CAN 唯一 ID 描述结构体
 * @details 描述如何从 CAN 帧中提取唯一标识符。包括数据来源
 *          （Payload 或 FrameId）、起始位位置、位长度和字节序。
 */
typedef struct XCanUniqueIdDescription {
    XCanBus_DataSource m_source;    ///< 数据源（Payload 或 FrameId）
    uint16_t m_startBit;            ///< 起始位位置
    uint8_t m_bitLength;            ///< 位长度
    uint8_t m_endian;               ///< 字节序（0=小端, 1=大端, 对应 QSysInfo::Endian）
} XCanUniqueIdDescription;

/******************************************************************************************
 * 初始化与清理
 ******************************************************************************************/

/**
 * @brief 初始化唯一 ID 描述结构体
 * @param desc 待初始化的描述指针（不可为 NULL）
 */
void XCanUniqueIdDescription_init(XCanUniqueIdDescription* desc);

/**
 * @brief 检查描述是否有效
 * @param desc 描述指针（不可为 NULL）
 * @return 有效返回 true，否则返回 false
 */
bool XCanUniqueIdDescription_isValid(const XCanUniqueIdDescription* desc);

/******************************************************************************************
 * 属性访问（对齐 QCanUniqueIdDescription）
 ******************************************************************************************/

/**
 * @brief 获取数据源
 * @param desc 描述指针（不可为 NULL）
 * @return 数据源枚举值
 */
XCanBus_DataSource XCanUniqueIdDescription_source(const XCanUniqueIdDescription* desc);

/**
 * @brief 设置数据源
 * @param desc 描述指针（不可为 NULL）
 * @param source 数据源
 */
void XCanUniqueIdDescription_setSource(XCanUniqueIdDescription* desc, XCanBus_DataSource source);

/**
 * @brief 获取起始位
 * @param desc 描述指针（不可为 NULL）
 * @return 起始位位置
 */
uint16_t XCanUniqueIdDescription_startBit(const XCanUniqueIdDescription* desc);

/**
 * @brief 设置起始位
 * @param desc 描述指针（不可为 NULL）
 * @param bit 起始位位置
 */
void XCanUniqueIdDescription_setStartBit(XCanUniqueIdDescription* desc, uint16_t bit);

/**
 * @brief 获取位长度
 * @param desc 描述指针（不可为 NULL）
 * @return 位长度
 */
uint8_t XCanUniqueIdDescription_bitLength(const XCanUniqueIdDescription* desc);

/**
 * @brief 设置位长度
 * @param desc 描述指针（不可为 NULL）
 * @param length 位长度
 */
void XCanUniqueIdDescription_setBitLength(XCanUniqueIdDescription* desc, uint8_t length);

/**
 * @brief 获取字节序
 * @param desc 描述指针（不可为 NULL）
 * @return 字节序（0=小端, 1=大端）
 */
uint8_t XCanUniqueIdDescription_endian(const XCanUniqueIdDescription* desc);

/**
 * @brief 设置字节序
 * @param desc 描述指针（不可为 NULL）
 * @param endian 字节序（0=小端, 1=大端）
 */
void XCanUniqueIdDescription_setEndian(XCanUniqueIdDescription* desc, uint8_t endian);

#ifdef __cplusplus
}
#endif

#endif // XCANUNIQUEIDDESCRIPTION_H
