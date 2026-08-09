/** @file XCan_config.h
 * @brief CAN 总线协议子功能配置文件
 *
 * 通过此配置文件可以裁剪 XCan 协议内部的各个子功能：
 *   1. XCAN_BUS_ON      - CAN 总线单例工厂（XCanBus）
 *   2. XCAN_DEVICE_ON   - CAN 设备（XCanBusDevice / XCanBusDeviceInfo / XCanBusFactory）
 *   3. XCAN_FRAME_ON    - CAN 数据帧（XCanBusFrame / XCanCommonDefinitions）
 *   4. XCAN_DBC_ON      - DBC 解析与报文/信号描述（XCanDbcFileParser / XCanFrameProcessor /
 *                         XCanMessageDescription / XCanSignalDescription / XCanUniqueIdDescription）
 *
 * 协议总开关 XCAN_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * 关闭后若仍有其它模块无条件引用 XCan 符号，需同步裁剪对应依赖。
 */

#ifndef XCAN_CONFIG_H
#define XCAN_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XCAN_ON
#define XCAN_ON XPROTOCOL_ON
#endif

#if XCAN_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief CAN 总线单例工厂（XCanBus） */
#ifndef XCAN_BUS_ON
#define XCAN_BUS_ON 1
#endif

/** @brief CAN 设备（XCanBusDevice / XCanBusDeviceInfo / XCanBusFactory） */
#ifndef XCAN_DEVICE_ON
#define XCAN_DEVICE_ON 1
#endif

/** @brief CAN 数据帧（XCanBusFrame / XCanCommonDefinitions） */
#ifndef XCAN_FRAME_ON
#define XCAN_FRAME_ON 1
#endif

/** @brief DBC 解析与报文/信号描述（XCanDbcFileParser / XCanFrameProcessor / XCanMessageDescription / XCanSignalDescription / XCanUniqueIdDescription） */
#ifndef XCAN_DBC_ON
#define XCAN_DBC_ON 1
#endif

#endif /* XCAN_ON */

#ifdef __cplusplus
}
#endif

#endif /* XCAN_CONFIG_H */
