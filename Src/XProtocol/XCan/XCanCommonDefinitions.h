#ifndef XCANCOMMONDEFINITIONS_H
#define XCANCOMMONDEFINITIONS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanCommonDefinitions.h
 * @brief CAN 公共定义头文件（对齐 Qt6 QtCanBus 命名空间）
 * @details 定义 CAN 协议栈中通用的枚举类型和常量，包括数据源、数据格式、
 *          多路复用状态和唯一 ID 类型等。对应 Qt 的 QtCanBus 命名空间。
 */

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

/**
 * @brief CAN 数据源枚举
 * @details 定义 CAN 信号的原始数据来源，对齐 Qt6 QtCanBus::DataSource
 */
typedef enum {
    XCanBus_Payload = 0,    ///< 数据来自帧的 Payload 负载区
    XCanBus_FrameId         ///< 数据来自帧的 ID 标识符区
} XCanBus_DataSource;

/**
 * @brief CAN 数据格式枚举
 * @details 定义 CAN 信号的编码格式，对齐 Qt6 QtCanBus::DataFormat
 */
typedef enum {
    XCanBus_SignedInteger = 0,      ///< 有符号整数
    XCanBus_UnsignedInteger,        ///< 无符号整数
    XCanBus_Float,                  ///< 单精度浮点数
    XCanBus_Double,                 ///< 双精度浮点数
    XCanBus_AsciiString             ///< ASCII 字符串
} XCanBus_DataFormat;

/**
 * @brief CAN 多路复用状态枚举
 * @details 定义 CAN 信号的多路复用属性，对齐 Qt6 QtCanBus::MultiplexState
 */
typedef enum {
    XCanBus_MultiplexState_None = 0x00,                     ///< 非多路复用信号
    XCanBus_MultiplexState_MultiplexorSwitch = 0x01,        ///< 多路复用器开关信号
    XCanBus_MultiplexState_MultiplexedSignal = 0x02,        ///< 被多路复用的信号
    XCanBus_MultiplexState_SwitchAndSignal = 0x03           ///< 既是开关又是被复用信号
} XCanBus_MultiplexState;

/**
 * @brief CAN 唯一 ID 类型
 * @details 使用 uint32_t 表示 CAN 消息的唯一标识符，对齐 Qt6 QtCanBus::UniqueId
 */
typedef uint32_t XCanBus_UniqueId;

#ifdef __cplusplus
}
#endif

#endif // XCANCOMMONDEFINITIONS_H
