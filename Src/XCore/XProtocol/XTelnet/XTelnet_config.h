/** @file XTelnet_config.h
 * @brief Telnet 协议栈子功能配置文件
 *
 * 通过此配置文件可以裁剪 XTelnet 协议栈内部的各个子功能：
 *   1. XTELNET_SERVER_ON - Telnet 服务端字节过滤/协商状态机（XTelnetServer）
 *   2. XTELNET_CLIENT_ON - Telnet 客户端 NVT/协商状态机（XTelnetClient）
 *
 * 协议总开关 XTELNET_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * 客户端和服务端默认随协议总开关开启而启用。
 */

#ifndef XTELNET_CONFIG_H
#define XTELNET_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XTELNET_ON
#define XTELNET_ON XPROTOCOL_ON
#endif

#if XTELNET_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief Telnet 服务端状态机（XTelnetServer）；该模块不监听端口，
 *         底层字节传输由 XProtocolIo 回调查询。 */
#ifndef XTELNET_SERVER_ON
#define XTELNET_SERVER_ON 1
#endif

/** @brief Telnet 客户端状态机（XTelnetClient）；连接传输由调用方提供 XIODevice。 */
#ifndef XTELNET_CLIENT_ON
#define XTELNET_CLIENT_ON 1
#endif

/* ========================================================================== */
/*                        公共参数                                          */
/* ========================================================================== */

/** @brief 登录用户名缓冲容量（字节，含结尾 NUL）。 */
#ifndef XTELNET_LOGIN_NAME_SIZE
#define XTELNET_LOGIN_NAME_SIZE 32
#endif

/** @brief 未登录时提示符使用的默认名称。 */
#ifndef XTELNET_DEFAULT_PROMPT_NAME
#define XTELNET_DEFAULT_PROMPT_NAME "XinYueC"
#endif

/** @brief 输出日志观察回调开关；默认继承 XProtocol 总日志开关。 */
#ifndef XTELNET_LOG_ON
#define XTELNET_LOG_ON XPROTOCOL_LOG_ON
#endif

/** @brief 命令审计回调开关；默认继承 XProtocol 总审计开关。 */
#ifndef XTELNET_AUDIT_ON
#define XTELNET_AUDIT_ON XPROTOCOL_AUDIT_ON
#endif

#endif /* XTELNET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XTELNET_CONFIG_H */
