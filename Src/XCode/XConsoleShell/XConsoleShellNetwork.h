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

#include "XConsoleShellCommand.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief `net` 根命令的静态描述。
 * @details
 * 当前提供 `ifconfig`、`hostname`、`resolve` 和 `ping` 四个子命令。命令表
 * 具有静态存储期，仅在网络命令宏开启时参与编译；Shell 只借用，调用方不得
 * 修改、复制后改写或释放其中的字符串。
 */
extern const XConsoleCommand XConsoleShellNetwork_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON */
#endif /* XCONSOLE_SHELL_NETWORK_H */
