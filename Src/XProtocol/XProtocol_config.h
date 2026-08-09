/** @file XProtocol_config.h
 * @brief XProtocol 协议栈模块配置文件
 *
 * 通过此配置文件可以裁剪 XProtocol 旗下的各个协议：
 *   1. XCAN_ON      - CAN 总线协议（XCan）
 *   2. XMODBUS_ON   - Modbus 协议（XModbus）
 *   3. XMQTT_ON     - MQTT 协议（XMqtt）
 *   4. XFTP_ON      - FTP 协议（XFtp）
 *   5. XHTTP_ON     - HTTP 协议（XHttp）
 *
 * 模块总开关 XPROTOCOL_ON 在 CXinYueConfig.h 中定义，此处仅提供默认值。
 * 关闭后若仍有其它模块无条件引用 XProtocol 符号，需同步裁剪对应依赖。
 */

#ifndef XPROTOCOL_CONFIG_H
#define XPROTOCOL_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        模块总开关                                        */
/* ========================================================================== */
/** @brief XProtocol 模块总开关；置 0 时裁剪整个 XProtocol 旗下所有协议。
 *  @note 总开关在 CXinYueConfig.h 中统一定义，此处仅兜底默认值。
 *        关闭后若仍有其它模块无条件引用 XProtocol 符号，需同步裁剪对应依赖。
 */
#ifndef XPROTOCOL_ON
#define XPROTOCOL_ON 1
#endif

#if XPROTOCOL_ON

/* ========================================================================== */
/*                        旗下协议开关                                      */
/* ========================================================================== */

/** @brief CAN 总线协议（XCan：XCanBus / XCanBusDevice / DBC 解析等） */
#ifndef XCAN_ON
#define XCAN_ON 1
#endif

/** @brief Modbus 协议（XModbus：RTU/TCP 客户端与服务器、PDU/ADU） */
#ifndef XMODBUS_ON
#define XMODBUS_ON 1
#endif

/** @brief MQTT 协议（XMqtt：客户端、主题过滤、属性集等） */
#ifndef XMQTT_ON
#define XMQTT_ON 1
#endif

/** @brief FTP 协议（XFtp：FTP 客户端核心与命令） */
#ifndef XFTP_ON
#define XFTP_ON 1
#endif

/** @brief HTTP 协议（XHttp：HTTP/1、HTTP/2、服务器与访问管理等） */
#ifndef XHTTP_ON
#define XHTTP_ON 1
#endif

/* 引入各协议子配置文件 */
#include "XCan/XCan_config.h"
#include "XModbus/XModbus_config.h"
#include "XMqtt/XMqtt_config.h"
#include "XFtp/XFtp_config.h"
#include "XHttp/XHttp_config.h"

#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif /* XPROTOCOL_CONFIG_H */