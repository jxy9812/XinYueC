/** @file XSsh_config.h
 * @brief SSH 协议栈子功能配置文件
 *
 * 通过此配置文件可以裁剪 XSsh 协议栈内部的各个子功能：
 *   1. XSSH_SERVER_ON   - SSH 服务端状态机（XSshServer）
 *
 * 协议总开关 XSSH_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * 服务端默认随协议总开关开启而启用。
 */

#ifndef XSSH_CONFIG_H
#define XSSH_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XPROTOCOL_ON 主开关已定义 */
#include "CXinYueConfig.h"

#ifndef XSSH_ON
#define XSSH_ON XPROTOCOL_ON
#endif

#if XSSH_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief SSH 服务端状态机（XSshServer）；该模块不监听端口，
 *         底层字节传输由 XProtocolIo 回调查询。 */
#ifndef XSSH_SERVER_ON
#define XSSH_SERVER_ON 1
#endif

/* ========================================================================== */
/*                        公共参数                                          */
/* ========================================================================== */

/** @brief SSH 单连接允许的最大密码认证失败次数；超过后协议栈关闭连接。 */
#ifndef XSSH_MAX_AUTH_ATTEMPTS
#define XSSH_MAX_AUTH_ATTEMPTS 3
#endif

/** @brief SSH 主机密钥持久化文件路径；首次启动生成并落盘，之后保持不变。 */
#ifndef XSSH_HOSTKEY_FILE
#define XSSH_HOSTKEY_FILE "xconsole_ssh_hostkey.bin"
#endif

/** @brief 登录用户名缓冲容量（字节，含结尾 NUL）。 */
#ifndef XSSH_LOGIN_NAME_SIZE
#define XSSH_LOGIN_NAME_SIZE 32
#endif

/** @brief 登录密码缓冲容量（字节，含结尾 NUL）。 */
#ifndef XSSH_LOGIN_PASSWORD_SIZE
#define XSSH_LOGIN_PASSWORD_SIZE 128
#endif

/** @brief 未登录时提示符使用的默认名称。 */
#ifndef XSSH_DEFAULT_PROMPT_NAME
#define XSSH_DEFAULT_PROMPT_NAME "XinYueC"
#endif

/** @brief 输出日志观察回调开关；默认继承 XProtocol 总日志开关。 */
#ifndef XSSH_LOG_ON
#define XSSH_LOG_ON XPROTOCOL_LOG_ON
#endif

/** @brief 命令审计回调开关；默认继承 XProtocol 总审计开关。 */
#ifndef XSSH_AUDIT_ON
#define XSSH_AUDIT_ON XPROTOCOL_AUDIT_ON
#endif

#endif /* XSSH_ON */

#ifdef __cplusplus
}
#endif

#endif /* XSSH_CONFIG_H */