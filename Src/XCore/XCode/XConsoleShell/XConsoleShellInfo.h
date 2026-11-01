/**
 * @file       XConsoleShellInfo.h
 * @brief      XConsoleShell 内建 `info` 命令声明。
 * @details    本模块没有直接对应的 Qt 命令类型。命令只输出 XinYueC、
 *             XConsoleShell、编译语言、目标平台和编译期开关信息，不访问
 *             寄存器、操作系统接口或外部进程。产品板卡、固件和芯片信息应由
 *             产品在关闭本命令后注册同名扩展命令提供。命令输出使用 Shell
 *             I/O，调用期间不分配堆内存，适合裸机、RTOS、POSIX 和 Windows。
 */

#ifndef XCONSOLE_SHELL_INFO_H
#define XCONSOLE_SHELL_INFO_H

#include "XConsoleShellConfig.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_INFO_ON

#include "XConsoleShellCommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief `info` 内建命令的静态描述。
 * @details 对象具有静态存储期，仅在 info 命令及 Shell I/O 宏开启时参与编译；
 *          Shell 只借用该描述，调用方不得修改或释放其内容。
 */
extern const XConsoleCommand XConsoleShellInfo_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_INFO_ON */
#endif /* XCONSOLE_SHELL_INFO_H */
