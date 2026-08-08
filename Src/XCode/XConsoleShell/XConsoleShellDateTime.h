/**
 * @file       XConsoleShellDateTime.h
 * @brief      XConsoleShell 日期时间命令描述。
 * @details    本模块没有直接对应的 Qt 命令类型，只公开静态的 `date` 命令表，
 *             不暴露日期时间格式化内部函数或平台句柄。命令实现通过 XDateTime
 *             公共 API 获取本地时间和 UTC 时间，不直接调用平台 API。Shell
 *             仅借用该命令表，不复制或释放其中的静态字符串。
 */

#ifndef XCONSOLE_SHELL_DATETIME_H
#define XCONSOLE_SHELL_DATETIME_H

#include "XConsoleShellCommand.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief `date` 根命令的静态描述。
 * @details
 * 支持 `date`、`date -u`、`date -I` 和 `date +FORMAT`。命令只读取当前时间，
 * 不实现 Linux `date -s` 等设置系统时间的写操作。该对象具有静态存储期，
 * 仅在日期时间命令宏开启时参与编译；调用方不得修改或释放。
 */
extern const XConsoleCommand XConsoleShellDateTime_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON */
#endif /* XCONSOLE_SHELL_DATETIME_H */
