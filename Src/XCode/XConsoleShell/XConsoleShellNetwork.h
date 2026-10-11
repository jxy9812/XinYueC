/**
 * @file       XConsoleShellNetwork.h
 * @brief      XConsoleShell 内建网络命令描述。
 * @details    本模块没有直接对应的 Qt 命令类型，只公开静态命令表，不暴露
 *             套接字、平台句柄或后端私有结构。命令通过 XNetwork、
 *             XNetworkInterface 和 XHostInfo 公共 API 访问网络，不直接调用
 *             平台 API，并可随 Shell 网络开关一起裁剪。调用方不应直接调用
 *             本模块的内部处理函数。
 */

#ifndef XCONSOLE_SHELL_NETWORK_H
#define XCONSOLE_SHELL_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON

#include "XConsoleShellCommand.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief `net` 根命令的静态描述。
 * @details
 * 当前提供 `ifconfig`、`hostname`、`resolve` 和 `ping` 四个子命令。命令表
 * 具有静态存储期，仅在网络命令宏开启时参与编译；Shell 只借用，调用方不得
 * 修改、复制后改写或释放其中的字符串。
 */
extern const XConsoleCommand XConsoleShellNetwork_command;

/**
 * @brief `ping` 顶层命令描述，转发到 xcsn_ping 处理函数。
 * @details
 * 该处理函数同时注册为 `net` 的子命令；此顶层别名保证 Linux 风格的
 * `ping <host>` 用法继续可用。
 */
extern const XConsoleCommand XConsoleShellNetwork_ping_command;

#if XCONSOLE_SHELL_NETWORK_ON && XCONSOLE_SHELL_NET_PING_ON
typedef struct XHostInfo XHostInfo;
typedef struct XHostAddress XHostAddress;
typedef struct XString XString;

/** @brief ping 异步运行状态；同一时间仅允许一个顶层 ping。 */
typedef struct XConsoleShellPingState {
    XHostInfo* m_info;        /**< 借用解析结果；Shell 拥有并负责释放。 */
    const XHostAddress* m_target; /**< 目标 IPv4 地址；指向 m_info 地址表中的借用指针。 */
    XString* m_targetText;    /**< 目标地址文本；Shell 拥有并负责释放。 */
    unsigned m_count;         /**< 总发送次数。 */
    unsigned m_timeout;       /**< 单包超时（毫秒）。 */
    unsigned m_sent;          /**< 已发送包数。 */
    unsigned m_received;      /**< 已收到回复包数。 */
    unsigned m_minimum;       /**< 最小 RTT（毫秒）。 */
    unsigned m_maximum;       /**< 最大 RTT（毫秒）。 */
    uint64_t m_sum;           /**< RTT 总和（毫秒）。 */
    bool m_active;            /**< 是否正在异步 ping。 */
} XConsoleShellPingState;

/**
 * @brief 定时器驱动发送下一包；发完最后一包后输出统计并恢复提示符。
 * @param shell 当前 Shell 对象；不能为空，仅在调用期间借用。
 * @return 无。shell 为空或当前没有进行中的 ping 时不执行任何操作。
 */
void XConsoleShellNetwork_pingTick(XConsoleShell* shell);
/**
 * @brief 取消并清理正在运行的 ping；不输出统计。
 * @param shell 当前 Shell 对象；不能为空，仅在调用期间借用。
 * @return 无。shell 为空或当前没有进行中的 ping 时不执行任何操作。
 */
void XConsoleShellNetwork_cancelPing(XConsoleShell* shell);
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON */

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_NETWORK_H */
