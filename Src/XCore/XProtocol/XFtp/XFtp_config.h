/** @file XFtp_config.h
 * @brief FTP 协议子功能配置文件
 *
 * 通过此配置文件可以裁剪 XFtp 协议内部的各个子功能：
 *   1. XFTP_CLIENT_ON   - FTP 客户端核心（XFtp）
 *   2. XFTP_COMMAND_ON  - FTP 命令（XFtpCommand）
 *
 * 协议总开关 XFTP_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * 命令模块默认随客户端核心开启而启用。
 */

#ifndef XFTP_CONFIG_H
#define XFTP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XFTP_ON
#define XFTP_ON XPROTOCOL_ON
#endif

#if XFTP_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief FTP 客户端核心（XFtp） */
#ifndef XFTP_CLIENT_ON
#define XFTP_CLIENT_ON 1
#endif

/** @brief FTP 命令（XFtpCommand） */
#ifndef XFTP_COMMAND_ON
#define XFTP_COMMAND_ON XFTP_CLIENT_ON
#endif

#endif /* XFTP_ON */

#ifdef __cplusplus
}
#endif

#endif /* XFTP_CONFIG_H */