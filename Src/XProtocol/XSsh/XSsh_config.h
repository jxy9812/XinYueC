/** @file XSsh_config.h
 * @brief SSH 协议栈子功能配置文件
 *
 * 通过此配置文件可以裁剪 XSsh 协议栈内部的各个子功能：
 *   1. XSSH_SERVER_ON   - SSH 服务端状态机（XSshServer）
 *   2. XSSH_CLIENT_ON   - SSH 客户端状态机（XSshClient）
 *
 * 算法能力由 XCode/XCryptographic/XCryptographic_config.h 统一配置。
 * 协议总开关 XSSH_ON 在 XProtocol_config.h 中定义，此处仅提供默认值。
 * 客户端和服务端默认随协议总开关开启而启用。
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

/** @brief SSH 客户端状态机（XSshClient）；连接传输由调用方提供 XIODevice。 */
#ifndef XSSH_CLIENT_ON
#define XSSH_CLIENT_ON 1
#endif

/** @brief 启用 RFC 3526 MODP 有限域 DH 密钥交换（diffie-hellman-group14-sha256），
 *         用于兼容 Xshell 等客户端的默认密钥交换列表。
 *
 *         实现为仓库内自研 2048 位大数 + Montgomery 模幂，不依赖外部库
 *         （不包含 mbedTLS PSA 头文件）；group16-sha512 预留枚举但暂未实现。 */
#ifndef XSSH_FFDH_GROUPS_ON
#define XSSH_FFDH_GROUPS_ON 1
#endif

/** @brief SSH 接收缓冲容量（字节）。未加密阶段缓存版本行和明文包，
 *         加密阶段缓存完整密文包（含 4 字节长度和 MAC）。 */
#ifndef XSSH_CFG_RX_CAPACITY
#define XSSH_CFG_RX_CAPACITY 4096
#endif

/** @brief SSH 发送缓冲容量（字节）。停靠待冲刷的协议输出。 */
#ifndef XSSH_CFG_TX_OUT_CAPACITY
#define XSSH_CFG_TX_OUT_CAPACITY 4096
#endif

/** @brief SSH 单包最大长度（字节）。包含 packet_length 之后的
 *         padding_length、payload 和 padding，不含 4 字节长度字段。
 *
 *         常见客户端（含 Xshell 默认 KEXINIT 约 2068 字节）可被
 *         3072 覆盖；嵌入式资源紧张时仍可调小，但需 >= 客户端握手包。 */
#ifndef XSSH_CFG_MAX_PACKET
#define XSSH_CFG_MAX_PACKET 3072
#endif

/** @brief 单包 payload 上限（字节），用于接收缓冲与发送组装。 */
#ifndef XSSH_CFG_MAX_PAYLOAD
#define XSSH_CFG_MAX_PAYLOAD 3072
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
