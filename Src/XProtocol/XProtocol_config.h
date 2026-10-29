/** @file XProtocol_config.h
 * @brief XProtocol 协议栈模块配置文件
 *
 * 通过此配置文件可以裁剪 XProtocol 旗下的各个协议：
 *   1. XCAN_ON      - CAN 总线协议（XCan）
 *   2. XMODBUS_ON   - Modbus 协议（XModbus）
 *   3. XMQTT_ON     - MQTT 协议（XMqtt）
 *   4. XFTP_ON      - FTP 协议（XFtp）
 *   5. XHTTP_ON     - HTTP 协议（XHttp）
 *   6. XSSH_ON      - SSH 协议客户端/服务端（XSsh）
 *   7. XTELNET_ON   - Telnet 协议客户端/服务端（XTelnet）
 *   8. XPLC_ON      - PLC 协议族（XPlc 伞层 + 西门子 S7）
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
/*                        公共日志/审计开关                                  */
/* ========================================================================== */

/** @brief XProtocol 旗下协议的输出日志观察回调总开关。 */
#ifndef XPROTOCOL_LOG_ON
#define XPROTOCOL_LOG_ON 0
#endif

/** @brief XProtocol 旗下协议的命令审计回调总开关。 */
#ifndef XPROTOCOL_AUDIT_ON
#define XPROTOCOL_AUDIT_ON 0
#endif

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

/** @brief SSH 协议（XSsh：精简 SSH 服务端状态机） */
#ifndef XSSH_ON
#define XSSH_ON 1
#endif

/** @brief Telnet 协议（XTelnet：Telnet NVT/协商过滤状态机） */
#ifndef XTELNET_ON
#define XTELNET_ON 1
#endif

/** @brief PLC 协议族（XPlc 伞层 + 西门子 S7，后续可扩三菱 MC 等） */
#ifndef XPLC_ON
#define XPLC_ON 1
#endif

/** @brief XLink 私有应用层协议（XLink：帧编解码 + 双向对称会话） */
#ifndef XLINK_ON
#define XLINK_ON 1
#endif

/** @brief XNtp 协议（SNTP 客户端：应用内校时，供无 ntpd 的最小固件使用） */
#ifndef XNTP_ON
#define XNTP_ON 1
#endif

/** @brief XDdns 协议（动态域名解析客户端：厂商抽象 + 阿里云解析实现） */
#ifndef XDDNS_ON
#define XDDNS_ON 1
#endif

/** @brief 通用总线轮询计划表（XPoll：XPollScheduler 纯逻辑排拍/退避表，
 *  XVfd 链路与 XModbus 聚合设备共用；缺省随任一使用方开启）。
 *  @note 缺省表达式引用 XMODBUS_HUB_ON 与 XVFD_ON（见下方子配置 include，
 *  宏展开惰性求值，与 include 顺序无关）。显式置 0 时须同时关闭
 *  XMODBUS_HUB_ON——Hub 的 (设备,从站) 失联退避依赖本组件。 */
#ifndef XPOLL_SCHEDULER_ON
#define XPOLL_SCHEDULER_ON (XMODBUS_HUB_ON || XVFD_ON)
#endif

/* 引入各协议子配置文件 */
#include "XCan_config.h"
#include "XModbus_config.h"
#include "XMqtt_config.h"
#include "XFtp_config.h"
#include "XHttp_config.h"
#include "XSsh_config.h"
#include "XTelnet_config.h"
#include "XPlc_config.h"
#include "XNtp_config.h"
#include "XDdns_config.h"
#include "XLink_config.h"
/* XVfd 子系统配置：XPOLL_SCHEDULER_ON 缺省引用 XVFD_ON，须在此可见。 */
#include "XVfd_config.h"

#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif /* XPROTOCOL_CONFIG_H */
