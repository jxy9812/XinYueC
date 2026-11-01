/**
 * @file       XConsoleShellMemory.h
 * @brief      XConsoleShell 内存池查询命令描述。
 * @details    本模块没有直接对应的 Qt 命令类型，只公开静态的 `mem` 命令表，
 *             不暴露内存池内部结构或平台句柄。命令通过 XMultiPool 和
 *             XFixedPool 的只读统计 API 输出容量和占用率，不直接调用平台
 *             API；`-v` 追加每个子池的统计。Shell 仅借用命令表。
 */

#ifndef XCONSOLE_SHELL_MEMORY_H
#define XCONSOLE_SHELL_MEMORY_H

#include "XConsoleShellCommand.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief `mem` 根命令的静态描述。
 * @details
 * 命令只查询全局 XMultiPool，不执行分配、释放或整理操作；系统 malloc、
 * 外部 VariablePool 和产品自定义池没有全局注册表时不会被伪造为可查询对象。
 * 不带参数输出汇总；`-v/--verbose` 追加各个 XFixedPool 子池的统计。对象具有
 * 静态存储期，仅在内存命令宏开启时参与编译；调用方不得修改或释放。
 */
extern const XConsoleCommand XConsoleShellMemory_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON */
#endif /* XCONSOLE_SHELL_MEMORY_H */
