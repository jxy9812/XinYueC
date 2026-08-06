/**
 * @file XConsoleShellConfig.h
 * @brief XConsoleShell 总开关、功能开关和固定容量配置。
 * @details
 * 本文件可由 CXinYueConfig.h 统一包含，也可被产品配置直接包含。默认关闭
 * 整个 Shell，避免改变既有固件体积；打开总开关后，命令、解析、I/O 和只读
 * 文件命令默认启用。所有容量为编译期常量，运行时达到上限返回资源错误。
 * 本文件不包含平台头文件，不调用平台 API。线程安全由调用方保证，Shell
 * 对象默认只允许一个执行上下文同时调用。
 */

#ifndef XCONSOLE_SHELL_CONFIG_H
#define XCONSOLE_SHELL_CONFIG_H

#include "XContainer/XContainerConfig.h"
#include "XCode/XProcess/XProcessConfig.h"

/** @brief Shell 总开关；置 0 时裁剪整个 Shell 公共 API 和所有子功能。 */
#ifndef XCONSOLE_SHELL_ON
#define XCONSOLE_SHELL_ON 1
#endif

/** @brief 命令模块开关；提供命令注册、查找和执行入口。 */
#ifndef XCONSOLE_SHELL_COMMAND_ON
#define XCONSOLE_SHELL_COMMAND_ON 1
#endif
/** @brief 解析器开关；提供引号、转义和参数分词。 */
#ifndef XCONSOLE_SHELL_PARSER_ON
#define XCONSOLE_SHELL_PARSER_ON 1
#endif
/** @brief help 命令和命令说明输出开关。 */
#ifndef XCONSOLE_SHELL_HELP_ON
#define XCONSOLE_SHELL_HELP_ON 1
#endif
/** @brief 命令别名匹配开关；关闭后仅匹配主命令名。 */
#ifndef XCONSOLE_SHELL_ALIAS_ON
#define XCONSOLE_SHELL_ALIAS_ON 1
#endif
/** @brief 子命令树开关；关闭后命令只能注册为根命令。 */
#ifndef XCONSOLE_SHELL_SUBCOMMAND_ON
#define XCONSOLE_SHELL_SUBCOMMAND_ON 1
#endif
/** @brief 回调命令开关；关闭后不允许注册产品自定义处理回调。 */
#ifndef XCONSOLE_SHELL_CALLBACK_COMMAND_ON
#define XCONSOLE_SHELL_CALLBACK_COMMAND_ON 1
#endif
/** @brief 输入输出回调和 Shell 核心处理开关。 */
#ifndef XCONSOLE_SHELL_IO_ON
#define XCONSOLE_SHELL_IO_ON 1
#endif
/** @brief 异步输出环形缓冲开关；开启后输出可延迟到 flushOutput。 */
#ifndef XCONSOLE_SHELL_ASYNC_OUTPUT_ON
#define XCONSOLE_SHELL_ASYNC_OUTPUT_ON 0
#endif
/** @brief 输出日志观察回调开关；不改变命令执行结果。 */
#ifndef XCONSOLE_SHELL_LOG_ON
#define XCONSOLE_SHELL_LOG_ON 0
#endif
/** @brief 多会话固定槽位开关；开启后可管理默认会话之外的连接。 */
#ifndef XCONSOLE_SHELL_MULTI_SESSION_ON
#define XCONSOLE_SHELL_MULTI_SESSION_ON 0
#endif
/** @brief 通用 XIODevice 传输适配器开关。 */
#ifndef XCONSOLE_SHELL_XIODEVICE_BACKEND_ON
#define XCONSOLE_SHELL_XIODEVICE_BACKEND_ON 0
#endif
/** @brief XSerialPort 传输适配器开关；依赖 XIODevice 适配器。 */
#ifndef XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON
#define XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON 0
#endif
/** @brief XTcpSocket 传输适配器开关；依赖 XIODevice 适配器。 */
#ifndef XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
#define XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON 0
#endif
/** @brief XTcpServer 多会话适配器开关；同时需要多会话和 TCP Socket。 */
#ifndef XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
#define XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON 0
#endif
/** @brief Telnet 协议过滤适配器开关；仅处理字节协议，不创建网络连接。 */
#ifndef XCONSOLE_SHELL_TELNET_PROTOCOL_ON
#define XCONSOLE_SHELL_TELNET_PROTOCOL_ON 0
#endif
/** @brief 基础传输回调适配器开关；read/write 回调由产品提供。 */
#ifndef XCONSOLE_SHELL_TRANSPORT_CALLBACK_ON
#define XCONSOLE_SHELL_TRANSPORT_CALLBACK_ON 1
#endif
/** @brief 文件系统命令树总开关；命令通过 XFileSystem 公共 API 操作。 */
#ifndef XCONSOLE_SHELL_FILESYSTEM_ON
#define XCONSOLE_SHELL_FILESYSTEM_ON 1
#endif
/** @brief 启用 `pwd`，输出 XFileSystem 当前路径。 */
#ifndef XCONSOLE_SHELL_FS_PWD_ON
#define XCONSOLE_SHELL_FS_PWD_ON 1
#endif
/** @brief 启用 `cd`，切换 XFileSystem 当前路径。 */
#ifndef XCONSOLE_SHELL_FS_CD_ON
#define XCONSOLE_SHELL_FS_CD_ON 1
#endif
/** @brief 启用 `ls`，列出文件或目录项。 */
#ifndef XCONSOLE_SHELL_FS_LS_ON
#define XCONSOLE_SHELL_FS_LS_ON 1
#endif
/** @brief 启用 `cat`，读取文件内容到 Shell 输出。 */
#ifndef XCONSOLE_SHELL_FS_CAT_ON
#define XCONSOLE_SHELL_FS_CAT_ON 1
#endif
/** @brief 启用 `stat`，输出文件属性和类型。 */
#ifndef XCONSOLE_SHELL_FS_STAT_ON
#define XCONSOLE_SHELL_FS_STAT_ON 1
#endif
/** @brief 启用 `rm`，删除文件或空目录。 */
#ifndef XCONSOLE_SHELL_FS_RM_ON
#define XCONSOLE_SHELL_FS_RM_ON 0
#endif
/** @brief 启用 `mkdir`，创建目录并支持递归创建。 */
#ifndef XCONSOLE_SHELL_FS_MKDIR_ON
#define XCONSOLE_SHELL_FS_MKDIR_ON 0
#endif
/** @brief 启用 `rmdir`，删除目录并可递归处理子项。 */
#ifndef XCONSOLE_SHELL_FS_RMDIR_ON
#define XCONSOLE_SHELL_FS_RMDIR_ON 0
#endif
/** @brief 启用 `cp`，复制文件或目录内容。 */
#ifndef XCONSOLE_SHELL_FS_CP_ON
#define XCONSOLE_SHELL_FS_CP_ON 0
#endif
/** @brief 启用 `mv`，移动或重命名文件。 */
#ifndef XCONSOLE_SHELL_FS_MV_ON
#define XCONSOLE_SHELL_FS_MV_ON 0
#endif
/** @brief 启用 `write`，将文本或字节写入文件。 */
#ifndef XCONSOLE_SHELL_FS_WRITE_ON
#define XCONSOLE_SHELL_FS_WRITE_ON 0
#endif
/** @brief 启用 `link` 符号链接命令；可用性由文件系统后端决定。 */
#ifndef XCONSOLE_SHELL_FS_LINK_ON
#define XCONSOLE_SHELL_FS_LINK_ON 0
#endif
/** @brief 启用 `ln` 兼容入口；不代表后端一定支持硬链接。 */
#ifndef XCONSOLE_SHELL_FS_LN_ON
#define XCONSOLE_SHELL_FS_LN_ON 0
#endif
/** @brief 启用 `unlink` 删除链接入口。 */
#ifndef XCONSOLE_SHELL_FS_UNLINK_ON
#define XCONSOLE_SHELL_FS_UNLINK_ON 0
#endif
/** @brief 启用 `format` 存储介质格式化入口。 */
#ifndef XCONSOLE_SHELL_FS_FORMAT_ON
#define XCONSOLE_SHELL_FS_FORMAT_ON 0
#endif
/** @brief 启用 `touch` 创建文件或更新文件时间。 */
#ifndef XCONSOLE_SHELL_FS_TOUCH_ON
#define XCONSOLE_SHELL_FS_TOUCH_ON 0
#endif
/** @brief 启用 `chmod` 权限修改入口；后端可能只支持部分权限。 */
#ifndef XCONSOLE_SHELL_FS_CHMOD_ON
#define XCONSOLE_SHELL_FS_CHMOD_ON 0
#endif
/** @brief 启用 `readlink` 读取符号链接目标。 */
#ifndef XCONSOLE_SHELL_FS_READLINK_ON
#define XCONSOLE_SHELL_FS_READLINK_ON 1
#endif
/** @brief 启用 `realpath` 输出规范化路径。 */
#ifndef XCONSOLE_SHELL_FS_REALPATH_ON
#define XCONSOLE_SHELL_FS_REALPATH_ON 1
#endif
/** @brief 启用 `truncate` 调整文件大小。 */
#ifndef XCONSOLE_SHELL_FS_TRUNCATE_ON
#define XCONSOLE_SHELL_FS_TRUNCATE_ON 0
#endif
/** @brief 启用 `df` 输出存储容量和剩余空间。 */
#ifndef XCONSOLE_SHELL_FS_DF_ON
#define XCONSOLE_SHELL_FS_DF_ON 1
#endif
/** @brief 启用 `du` 递归统计目录占用。 */
#ifndef XCONSOLE_SHELL_FS_DU_ON
#define XCONSOLE_SHELL_FS_DU_ON 1
#endif
/** @brief 启用 `wc` 统计文件行、词和字节。 */
#ifndef XCONSOLE_SHELL_FS_WC_ON
#define XCONSOLE_SHELL_FS_WC_ON 1
#endif
/** @brief 启用 `head` 输出文件开头内容。 */
#ifndef XCONSOLE_SHELL_FS_HEAD_ON
#define XCONSOLE_SHELL_FS_HEAD_ON 1
#endif
/** @brief 启用 `tail` 输出文件末尾内容。 */
#ifndef XCONSOLE_SHELL_FS_TAIL_ON
#define XCONSOLE_SHELL_FS_TAIL_ON 1
#endif
/** @brief 启用 `find` 递归查找文件。 */
#ifndef XCONSOLE_SHELL_FS_FIND_ON
#define XCONSOLE_SHELL_FS_FIND_ON 1
#endif
/** @brief 启用 `tree` 递归显示目录树。 */
#ifndef XCONSOLE_SHELL_FS_TREE_ON
#define XCONSOLE_SHELL_FS_TREE_ON 1
#endif
/** @brief 启用 `cmp` 比较两个文件内容。 */
#ifndef XCONSOLE_SHELL_FS_CMP_ON
#define XCONSOLE_SHELL_FS_CMP_ON 1
#endif
/** @brief 启用 `file` 输出文件类型。 */
#ifndef XCONSOLE_SHELL_FS_FILE_ON
#define XCONSOLE_SHELL_FS_FILE_ON 1
#endif
/** @brief 启用 `basename` 提取路径末级名称。 */
#ifndef XCONSOLE_SHELL_FS_BASENAME_ON
#define XCONSOLE_SHELL_FS_BASENAME_ON 1
#endif
/** @brief 启用 `dirname` 提取路径目录部分。 */
#ifndef XCONSOLE_SHELL_FS_DIRNAME_ON
#define XCONSOLE_SHELL_FS_DIRNAME_ON 1
#endif
/** @brief 外部命令执行器总开关；底层使用 XProcess 公共 API。 */
#ifndef XCONSOLE_SHELL_EXECUTOR_ON
#define XCONSOLE_SHELL_EXECUTOR_ON 1
#endif
/** @brief 启用 exec 管道组合；依赖外部进程执行器。 */
#ifndef XCONSOLE_SHELL_PIPE_ON
#define XCONSOLE_SHELL_PIPE_ON 0
#endif
/** @brief 启用 exec 输入输出重定向；依赖外部进程执行器。 */
#ifndef XCONSOLE_SHELL_REDIRECT_ON
#define XCONSOLE_SHELL_REDIRECT_ON 0
#endif
/** @brief 启用脚本或 source 命令；脚本文件通过文件系统命令读取。 */
#ifndef XCONSOLE_SHELL_SCRIPT_ON
#define XCONSOLE_SHELL_SCRIPT_ON 0
#endif
/** @brief 启用外部进程命令；实现通过 XProcess，不直接调用平台 API。 */
#ifndef XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
#define XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 0
#endif
/** @brief 启用异步外部进程槽位和轮询接口。 */
#ifndef XCONSOLE_SHELL_PROCESS_ASYNC_ON
#define XCONSOLE_SHELL_PROCESS_ASYNC_ON 0
#endif
/** @brief 启用运行时动态注册命令；会占用固定动态命令槽。 */
#ifndef XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
#define XCONSOLE_SHELL_DYNAMIC_REGISTER_ON 0
#endif
/** @brief 启用 ANSI 行编辑，包括光标移动和退格处理。 */
#ifndef XCONSOLE_SHELL_LINE_EDITOR_ON
#define XCONSOLE_SHELL_LINE_EDITOR_ON 0
#endif
/** @brief 启用固定容量历史命令缓冲。 */
#ifndef XCONSOLE_SHELL_HISTORY_ON
#define XCONSOLE_SHELL_HISTORY_ON 0
#endif
/** @brief 启用命令补全回调和补全状态。 */
#ifndef XCONSOLE_SHELL_COMPLETION_ON
#define XCONSOLE_SHELL_COMPLETION_ON 0
#endif
/** @brief 启用 Ctrl-C 和取消回调处理。 */
#ifndef XCONSOLE_SHELL_CANCEL_ON
#define XCONSOLE_SHELL_CANCEL_ON 1
#endif
/** @brief 启用认证状态字段；具体认证流程由产品层实现。 */
#ifndef XCONSOLE_SHELL_AUTH_ON
#define XCONSOLE_SHELL_AUTH_ON 0
#endif
/** @brief 启用命令权限检查和危险命令权限位。 */
#ifndef XCONSOLE_SHELL_PERMISSION_ON
#define XCONSOLE_SHELL_PERMISSION_ON 0
#endif
/** @brief 启用命令审计回调；产品负责日志脱敏和存储。 */
#ifndef XCONSOLE_SHELL_AUDIT_ON
#define XCONSOLE_SHELL_AUDIT_ON 0
#endif
/** @brief 启用运行统计字段和统计查询 API。 */
#ifndef XCONSOLE_SHELL_STATS_ON
#define XCONSOLE_SHELL_STATS_ON 0
#endif

/** @brief 单行输入缓冲容量，包含结尾 NUL 的存储空间。 */
#ifndef XCONSOLE_SHELL_LINE_BUFFER_SIZE
#define XCONSOLE_SHELL_LINE_BUFFER_SIZE 256
#endif
/** @brief 单行最多保存的参数指针数量，包含命令名。 */
#ifndef XCONSOLE_SHELL_MAX_ARGUMENTS
#define XCONSOLE_SHELL_MAX_ARGUMENTS 16
#endif
/** @brief 会话逻辑路径缓冲容量，包含结尾 NUL。 */
#ifndef XCONSOLE_SHELL_MAX_PATH
#define XCONSOLE_SHELL_MAX_PATH 256
#endif
/** @brief 静态根命令指针表容量。 */
#ifndef XCONSOLE_SHELL_COMMAND_CAPACITY
#define XCONSOLE_SHELL_COMMAND_CAPACITY 64
#endif
/** @brief 默认单次输出块大小，用于减少小块回调次数。 */
#ifndef XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE
#define XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE 128
#endif
/** @brief 每个 Shell 保存的历史命令条目数。 */
#ifndef XCONSOLE_SHELL_HISTORY_CAPACITY
#define XCONSOLE_SHELL_HISTORY_CAPACITY 8
#endif
/** @brief 异步输出环形缓冲容量，必须覆盖一次最小输出事务。 */
#ifndef XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY
#define XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY 512
#endif
/** @brief 动态注册命令根节点的固定槽位数量。 */
#ifndef XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY
#define XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY 8
#endif
/** @brief 多会话模式下允许的会话总数，包含默认会话。 */
#ifndef XCONSOLE_SHELL_MAX_SESSIONS
#define XCONSOLE_SHELL_MAX_SESSIONS 4
#endif
/** @brief 异步外部进程任务槽位数量。 */
#ifndef XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY
#define XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY 4
#endif

/* 以下检查在预处理阶段拒绝无法工作的容量组合，避免运行时静默截断。 */
#if XCONSOLE_SHELL_HISTORY_ON && XCONSOLE_SHELL_HISTORY_CAPACITY < 1
#error "XCONSOLE_SHELL_HISTORY_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON && XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY < 1
#error "XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON && XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY < 1
#error "XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_MAX_SESSIONS < 2
#error "XCONSOLE_SHELL_MAX_SESSIONS must be at least two when MULTI_SESSION is enabled"
#endif
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON && XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY < 1
#error "XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY must be greater than zero"
#endif

#if !XCONSOLE_SHELL_ON
#undef XCONSOLE_SHELL_COMMAND_ON
#define XCONSOLE_SHELL_COMMAND_ON 0
#undef XCONSOLE_SHELL_PARSER_ON
#define XCONSOLE_SHELL_PARSER_ON 0
#undef XCONSOLE_SHELL_HELP_ON
#define XCONSOLE_SHELL_HELP_ON 0
#undef XCONSOLE_SHELL_ALIAS_ON
#define XCONSOLE_SHELL_ALIAS_ON 0
#undef XCONSOLE_SHELL_SUBCOMMAND_ON
#define XCONSOLE_SHELL_SUBCOMMAND_ON 0
#undef XCONSOLE_SHELL_CALLBACK_COMMAND_ON
#define XCONSOLE_SHELL_CALLBACK_COMMAND_ON 0
#undef XCONSOLE_SHELL_IO_ON
#define XCONSOLE_SHELL_IO_ON 0
#undef XCONSOLE_SHELL_ASYNC_OUTPUT_ON
#define XCONSOLE_SHELL_ASYNC_OUTPUT_ON 0
#undef XCONSOLE_SHELL_LOG_ON
#define XCONSOLE_SHELL_LOG_ON 0
#undef XCONSOLE_SHELL_MULTI_SESSION_ON
#define XCONSOLE_SHELL_MULTI_SESSION_ON 0
#undef XCONSOLE_SHELL_XIODEVICE_BACKEND_ON
#define XCONSOLE_SHELL_XIODEVICE_BACKEND_ON 0
#undef XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON
#define XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON 0
#undef XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
#define XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON 0
#undef XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
#define XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON 0
#undef XCONSOLE_SHELL_TELNET_PROTOCOL_ON
#define XCONSOLE_SHELL_TELNET_PROTOCOL_ON 0
#undef XCONSOLE_SHELL_TRANSPORT_CALLBACK_ON
#define XCONSOLE_SHELL_TRANSPORT_CALLBACK_ON 0
#undef XCONSOLE_SHELL_FILESYSTEM_ON
#define XCONSOLE_SHELL_FILESYSTEM_ON 0
#undef XCONSOLE_SHELL_EXECUTOR_ON
#define XCONSOLE_SHELL_EXECUTOR_ON 0
#undef XCONSOLE_SHELL_PIPE_ON
#define XCONSOLE_SHELL_PIPE_ON 0
#undef XCONSOLE_SHELL_REDIRECT_ON
#define XCONSOLE_SHELL_REDIRECT_ON 0
#undef XCONSOLE_SHELL_SCRIPT_ON
#define XCONSOLE_SHELL_SCRIPT_ON 0
#undef XCONSOLE_SHELL_AUTH_ON
#define XCONSOLE_SHELL_AUTH_ON 0
#undef XCONSOLE_SHELL_PERMISSION_ON
#define XCONSOLE_SHELL_PERMISSION_ON 0
#undef XCONSOLE_SHELL_AUDIT_ON
#define XCONSOLE_SHELL_AUDIT_ON 0
#undef XCONSOLE_SHELL_STATS_ON
#define XCONSOLE_SHELL_STATS_ON 0
#undef XCONSOLE_SHELL_FS_PWD_ON
#define XCONSOLE_SHELL_FS_PWD_ON 0
#undef XCONSOLE_SHELL_FS_CD_ON
#define XCONSOLE_SHELL_FS_CD_ON 0
#undef XCONSOLE_SHELL_FS_LS_ON
#define XCONSOLE_SHELL_FS_LS_ON 0
#undef XCONSOLE_SHELL_FS_CAT_ON
#define XCONSOLE_SHELL_FS_CAT_ON 0
#undef XCONSOLE_SHELL_FS_STAT_ON
#define XCONSOLE_SHELL_FS_STAT_ON 0
#undef XCONSOLE_SHELL_FS_RM_ON
#define XCONSOLE_SHELL_FS_RM_ON 0
#undef XCONSOLE_SHELL_FS_MKDIR_ON
#define XCONSOLE_SHELL_FS_MKDIR_ON 0
#undef XCONSOLE_SHELL_FS_RMDIR_ON
#define XCONSOLE_SHELL_FS_RMDIR_ON 0
#undef XCONSOLE_SHELL_FS_CP_ON
#define XCONSOLE_SHELL_FS_CP_ON 0
#undef XCONSOLE_SHELL_FS_MV_ON
#define XCONSOLE_SHELL_FS_MV_ON 0
#undef XCONSOLE_SHELL_FS_WRITE_ON
#define XCONSOLE_SHELL_FS_WRITE_ON 0
#undef XCONSOLE_SHELL_FS_LINK_ON
#define XCONSOLE_SHELL_FS_LINK_ON 0
#undef XCONSOLE_SHELL_FS_LN_ON
#define XCONSOLE_SHELL_FS_LN_ON 0
#undef XCONSOLE_SHELL_FS_UNLINK_ON
#define XCONSOLE_SHELL_FS_UNLINK_ON 0
#undef XCONSOLE_SHELL_FS_FORMAT_ON
#define XCONSOLE_SHELL_FS_FORMAT_ON 0
#undef XCONSOLE_SHELL_FS_TOUCH_ON
#define XCONSOLE_SHELL_FS_TOUCH_ON 0
#undef XCONSOLE_SHELL_FS_CHMOD_ON
#define XCONSOLE_SHELL_FS_CHMOD_ON 0
#undef XCONSOLE_SHELL_FS_READLINK_ON
#define XCONSOLE_SHELL_FS_READLINK_ON 0
#undef XCONSOLE_SHELL_FS_REALPATH_ON
#define XCONSOLE_SHELL_FS_REALPATH_ON 0
#undef XCONSOLE_SHELL_FS_TRUNCATE_ON
#define XCONSOLE_SHELL_FS_TRUNCATE_ON 0
#undef XCONSOLE_SHELL_FS_DF_ON
#define XCONSOLE_SHELL_FS_DF_ON 0
#undef XCONSOLE_SHELL_FS_DU_ON
#define XCONSOLE_SHELL_FS_DU_ON 0
#undef XCONSOLE_SHELL_FS_WC_ON
#define XCONSOLE_SHELL_FS_WC_ON 0
#undef XCONSOLE_SHELL_FS_HEAD_ON
#define XCONSOLE_SHELL_FS_HEAD_ON 0
#undef XCONSOLE_SHELL_FS_TAIL_ON
#define XCONSOLE_SHELL_FS_TAIL_ON 0
#undef XCONSOLE_SHELL_FS_FIND_ON
#define XCONSOLE_SHELL_FS_FIND_ON 0
#undef XCONSOLE_SHELL_FS_TREE_ON
#define XCONSOLE_SHELL_FS_TREE_ON 0
#undef XCONSOLE_SHELL_FS_CMP_ON
#define XCONSOLE_SHELL_FS_CMP_ON 0
#undef XCONSOLE_SHELL_FS_FILE_ON
#define XCONSOLE_SHELL_FS_FILE_ON 0
#undef XCONSOLE_SHELL_FS_BASENAME_ON
#define XCONSOLE_SHELL_FS_BASENAME_ON 0
#undef XCONSOLE_SHELL_FS_DIRNAME_ON
#define XCONSOLE_SHELL_FS_DIRNAME_ON 0
#undef XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
#define XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 0
#undef XCONSOLE_SHELL_PROCESS_ASYNC_ON
#define XCONSOLE_SHELL_PROCESS_ASYNC_ON 0
#undef XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
#define XCONSOLE_SHELL_DYNAMIC_REGISTER_ON 0
#endif

#if !XCONSOLE_SHELL_PARSER_ON
#undef XCONSOLE_SHELL_COMMAND_ON
#define XCONSOLE_SHELL_COMMAND_ON 0
#undef XCONSOLE_SHELL_HELP_ON
#define XCONSOLE_SHELL_HELP_ON 0
#undef XCONSOLE_SHELL_ALIAS_ON
#define XCONSOLE_SHELL_ALIAS_ON 0
#undef XCONSOLE_SHELL_SUBCOMMAND_ON
#define XCONSOLE_SHELL_SUBCOMMAND_ON 0
#undef XCONSOLE_SHELL_COMPLETION_ON
#define XCONSOLE_SHELL_COMPLETION_ON 0
#endif

#if !XCONSOLE_SHELL_COMMAND_ON
#undef XCONSOLE_SHELL_FILESYSTEM_ON
#define XCONSOLE_SHELL_FILESYSTEM_ON 0
#undef XCONSOLE_SHELL_EXECUTOR_ON
#define XCONSOLE_SHELL_EXECUTOR_ON 0
#undef XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
#define XCONSOLE_SHELL_DYNAMIC_REGISTER_ON 0
#endif

/* 内置命令不依赖此开关；关闭后仅禁止产品注册带回调的扩展命令。 */
#if !XCONSOLE_SHELL_CALLBACK_COMMAND_ON
#undef XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
#define XCONSOLE_SHELL_DYNAMIC_REGISTER_ON 0
#endif

/* exec 及其管道、重定向和异步调度均由 Executor 子模块提供。 */
#if !XCONSOLE_SHELL_EXECUTOR_ON
#undef XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
#define XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 0
#undef XCONSOLE_SHELL_PIPE_ON
#define XCONSOLE_SHELL_PIPE_ON 0
#undef XCONSOLE_SHELL_REDIRECT_ON
#define XCONSOLE_SHELL_REDIRECT_ON 0
#undef XCONSOLE_SHELL_PROCESS_ASYNC_ON
#define XCONSOLE_SHELL_PROCESS_ASYNC_ON 0
#endif

#if XCONSOLE_SHELL_XSERIALPORT_BACKEND_ON || XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON || \
    XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
#undef XCONSOLE_SHELL_XIODEVICE_BACKEND_ON
#define XCONSOLE_SHELL_XIODEVICE_BACKEND_ON 1
#endif

#if XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
#undef XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON
#define XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON 1
#endif

#if XCONSOLE_SHELL_EXTERNAL_PROCESS_ON && !XProcess_ON
#error "XConsoleShell: external process requires XProcess_ON"
#endif
#if XCONSOLE_SHELL_PIPE_ON && !XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
#error "XConsoleShell: PIPE requires EXTERNAL_PROCESS"
#endif
#if XCONSOLE_SHELL_REDIRECT_ON && !XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
#error "XConsoleShell: REDIRECT requires EXTERNAL_PROCESS"
#endif
#if XCONSOLE_SHELL_SCRIPT_ON && !XCONSOLE_SHELL_FILESYSTEM_ON
#error "XConsoleShell: SCRIPT requires FILESYSTEM"
#endif
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON && !XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
#error "XConsoleShell: PROCESS_ASYNC requires EXTERNAL_PROCESS"
#endif
#if XCONSOLE_SHELL_FILESYSTEM_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: FILESYSTEM requires IO"
#endif
#if XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON && !XCONSOLE_SHELL_MULTI_SESSION_ON
#error "XConsoleShell: XTCPSERVER backend requires MULTI_SESSION"
#endif
#if XCONSOLE_SHELL_MAX_ARGUMENTS < 1 || XCONSOLE_SHELL_MAX_ARGUMENTS > 255
#error "XConsoleShell: MAX_ARGUMENTS must be in range 1..255"
#endif
#if XCONSOLE_SHELL_LINE_BUFFER_SIZE < 32
#error "XConsoleShell: LINE_BUFFER_SIZE must be at least 32"
#endif
#if XCONSOLE_SHELL_MAX_PATH < 2
#error "XConsoleShell: MAX_PATH must be at least 2"
#endif

#endif /* XCONSOLE_SHELL_CONFIG_H */
