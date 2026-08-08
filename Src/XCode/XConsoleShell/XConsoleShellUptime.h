/**
 * @file       XConsoleShellUptime.h
 * @brief      XConsoleShell 内建 `uptime` 命令声明。
 * @details    本模块没有直接对应的 Qt 命令类型。Shell 初始化时记录 XDateTime
 *             公共 API 返回的毫秒时间戳，命令执行时再次读取并计算差值。命令
 *             不直接调用平台时钟接口、不分配堆内存；如果后端没有时间实现或
 *             检测到时钟回拨，则返回不支持。由于底层 API 是日历时间，校时
 *             可能使结果产生偏差，严格单调计时应由平台时间后端保证。
 */

#ifndef XCONSOLE_SHELL_UPTIME_H
#define XCONSOLE_SHELL_UPTIME_H

#include "XConsoleShellConfig.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_UPTIME_ON

#include "XConsoleShellCommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief `uptime` 内建命令的静态描述。
 * @details 对象具有静态存储期，仅在 uptime 命令及 Shell I/O 宏开启时参与
 *          编译；Shell 只借用，调用方不得修改或释放其内容。
 */
extern const XConsoleCommand XConsoleShellUptime_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_UPTIME_ON */
#endif /* XCONSOLE_SHELL_UPTIME_H */
