/** @file XModbus_config.h
 * @brief Modbus 协议子功能配置文件
 *
 * 通过此配置文件可以裁剪 XModbus 协议内部的各个子功能：
 *   1. XMODBUS_CORE_ON    - 核心数据单元（XModbusAdu / XModbusPdu / XModbusDataUnit /
 *                           XModbusCommEvent / XModbusDeviceIdentification）
 *   2. XMODBUS_CLIENT_ON  - 客户端（XModbusClient / XModbusDevice / XModbusReply）
 *   3. XMODBUS_SERVER_ON  - 服务器（XModbusServer）
 *   4. XMODBUS_RTU_ON     - RTU 串口传输（XModbusRtuSerialClient / XModbusRtuSerialServer）
 *   5. XMODBUS_TCP_ON     - TCP 传输（XModbusTcpClient / XModbusTcpServer）
 *
 * 协议总开关 XMODBUS_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * RTU / TCP 传输默认随客户端或服务器任一开启而启用。
 */

#ifndef XMODBUS_CONFIG_H
#define XMODBUS_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XMODBUS_ON
#define XMODBUS_ON XPROTOCOL_ON
#endif

#if XMODBUS_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief 核心数据单元（XModbusAdu / XModbusPdu / XModbusDataUnit / XModbusCommEvent / XModbusDeviceIdentification） */
#ifndef XMODBUS_CORE_ON
#define XMODBUS_CORE_ON 1
#endif

/** @brief 客户端核心（XModbusClient / XModbusDevice / XModbusReply） */
#ifndef XMODBUS_CLIENT_ON
#define XMODBUS_CLIENT_ON XMODBUS_CORE_ON
#endif

/** @brief 服务器核心（XModbusServer） */
#ifndef XMODBUS_SERVER_ON
#define XMODBUS_SERVER_ON XMODBUS_CORE_ON
#endif

/** @brief RTU 串口传输（XModbusRtuSerialClient / XModbusRtuSerialServer） */
#ifndef XMODBUS_RTU_ON
#define XMODBUS_RTU_ON (XMODBUS_CLIENT_ON || XMODBUS_SERVER_ON)
#endif

/** @brief TCP 传输（XModbusTcpClient / XModbusTcpServer） */
#ifndef XMODBUS_TCP_ON
#define XMODBUS_TCP_ON (XMODBUS_CLIENT_ON || XMODBUS_SERVER_ON)
#endif

#endif /* XMODBUS_ON */

#ifdef __cplusplus
}
#endif

#endif /* XMODBUS_CONFIG_H */
