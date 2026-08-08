/**
 * @file       XConsoleShellSystem.h
 * @brief      XConsoleShell 的 clear、reset、reboot、shutdown 和 exit 命令描述。
 * @details    本模块没有直接对应的 Qt 命令类型。clear 只发送 ANSI 终端清屏
 *             序列；reset、reboot 和 shutdown 分别调用 XSystem 的系统复位、
 *             有序重启与关机公共 API。无操作系统或无关机后端时，shutdown
 *             结束当前 Shell。命令模块不包含操作系统头文件、不直接调用平台
 *             API，也不拥有产品注册的系统回调。真实系统操作命令默认关闭。
 */

#ifndef XCONSOLE_SHELL_SYSTEM_H
#define XCONSOLE_SHELL_SYSTEM_H

#include "XConsoleShellCommand.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON

#ifdef __cplusplus
extern "C" {
#endif

#if XCONSOLE_SHELL_CLEAR_ON
/**
 * @brief clear 命令的静态描述；执行时只清理 ANSI 终端显示。
 * @details 对象具有静态存储期，仅在 XCONSOLE_SHELL_CLEAR_ON 开启时参与编译；
 *          Shell 只借用，调用方不得修改或释放。
 */
extern const XConsoleCommand XConsoleShellClear_command;
#endif
#if XCONSOLE_SHELL_RESET_ON
/**
 * @brief reset 命令的静态描述；执行时请求立即系统复位。
 * @details 对象具有静态存储期，仅在 XCONSOLE_SHELL_RESET_ON 开启时参与编译；
 *          请求失败时命令返回对应错误，不绕过 XSystem 公共抽象。调用方不得
 *          修改或释放该描述。
 */
extern const XConsoleCommand XConsoleShellReset_command;
#endif
#if XCONSOLE_SHELL_REBOOT_ON
/**
 * @brief reboot 命令的静态描述；执行时请求有序系统重启。
 * @details 对象具有静态存储期，仅在 XCONSOLE_SHELL_REBOOT_ON 开启时参与编译；
 *          请求失败时命令返回对应错误，不绕过 XSystem 公共抽象。调用方不得
 *          修改或释放该描述。
 */
extern const XConsoleCommand XConsoleShellReboot_command;
#endif
#if XCONSOLE_SHELL_SHUTDOWN_ON
/**
 * @brief shutdown 命令的静态描述；执行时请求关机或退出当前 Shell。
 * @details 对象具有静态存储期，仅在 XCONSOLE_SHELL_SHUTDOWN_ON 开启时参与编译；
 *          有操作系统时调用 XSystem_shutdown，无平台时将 Shell 标记为停止。
 *          调用方只读借用，不得修改或释放该描述。
 */
extern const XConsoleCommand XConsoleShellShutdown_command;
#endif
#if XCONSOLE_SHELL_EXIT_ON
/**
 * @brief exit 命令的静态描述；执行时退出当前 Shell 和应用事件循环。
 * @details 对象具有静态存储期，仅在 XCONSOLE_SHELL_EXIT_ON 开启时参与编译；
 *          调用方只读借用，不得修改或释放该描述。
 */
extern const XConsoleCommand XConsoleShellExit_command;
#endif

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
#endif /* XCONSOLE_SHELL_SYSTEM_H */
