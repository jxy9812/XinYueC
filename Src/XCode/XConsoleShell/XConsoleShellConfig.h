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


/* ==================================================================== */
/* 1. 总开关与核心模块                                                        */
/* ==================================================================== */

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

/** @brief Shell 输入事件异步调度总开关；开启后由事件队列驱动 pump。 */
#ifndef XCONSOLE_SHELL_ASYNC_ON
#define XCONSOLE_SHELL_ASYNC_ON 1
#endif

/* ==================================================================== */
/* 2. 异步调度配置                                                          */
/* ==================================================================== */

/** @brief 是否在 Shell 创建后自动接入事件调度器；需要 IO 输入附加回调。 */
#ifndef XCONSOLE_SHELL_ASYNC_AUTO_START_ON
#define XCONSOLE_SHELL_ASYNC_AUTO_START_ON 1
#endif

/** @brief 将 Shell 输入事件接入当前线程的事件调度器。 */
#ifndef XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER
#define XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER 0
#endif

/** @brief Shell 独占线程异步模式；线程仍通过专用事件调度器接收输入事件。 */
#ifndef XCONSOLE_SHELL_ASYNC_MODE_THREAD
#define XCONSOLE_SHELL_ASYNC_MODE_THREAD 1
#endif

/** @brief Shell 异步运行模式；当前实现默认使用事件调度器模式。 */
#ifndef XCONSOLE_SHELL_ASYNC_RUN_MODE
#define XCONSOLE_SHELL_ASYNC_RUN_MODE XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER
#endif

/** @brief 单次输入事件最多连续调用 read 的次数，避免事件处理长期占用线程。 */
#ifndef XCONSOLE_SHELL_ASYNC_READ_BUDGET
#define XCONSOLE_SHELL_ASYNC_READ_BUDGET 32
#endif

/** @brief 异步轮询输入的定时器间隔，单位为毫秒。 */
#ifndef XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS
#define XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS 10
#endif

/* ==================================================================== */
/* 3. 日志、多会话与传输后端                                                     */
/* ==================================================================== */

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

/* ==================================================================== */
/* 4. 命令模块总开关                                                         */
/* ==================================================================== */

/** @brief 文件系统命令树总开关；命令通过 XFileSystem 公共 API 操作。 */
#ifndef XCONSOLE_SHELL_FILESYSTEM_ON
#define XCONSOLE_SHELL_FILESYSTEM_ON 1
#endif

/** @brief 网络命令树总开关；命令通过 XNetwork 公共 API 查询网络状态。 */
#ifndef XCONSOLE_SHELL_NETWORK_ON
#define XCONSOLE_SHELL_NETWORK_ON 1
#endif

/** @brief 日期时间命令模块总开关；命令通过 XDateTime 公共 API 查询当前时间。 */
#ifndef XCONSOLE_SHELL_DATETIME_ON
#define XCONSOLE_SHELL_DATETIME_ON 1
#endif

/** @brief 启用 Linux 风格的 `date` 只读时间查看命令。 */
#ifndef XCONSOLE_SHELL_DATE_ON
#define XCONSOLE_SHELL_DATE_ON 1
#endif

/** @brief 内存查询命令模块总开关；命令通过 XMultiPool 公共 API 查询全局池。 */
#ifndef XCONSOLE_SHELL_MEMORY_ON
#define XCONSOLE_SHELL_MEMORY_ON 1
#endif

/** @brief 启用 `info`，输出框架、Shell、平台和核心功能编译信息。 */
#ifndef XCONSOLE_SHELL_INFO_ON
#define XCONSOLE_SHELL_INFO_ON 1
#endif

/** @brief 启用 `uptime`，输出 Shell 初始化后的运行时间。 */
#ifndef XCONSOLE_SHELL_UPTIME_ON
#define XCONSOLE_SHELL_UPTIME_ON 1
#endif

/** @brief 启用 `tasks`，通过产品提供者输出任务快照。 */
#ifndef XCONSOLE_SHELL_TASKS_ON
#define XCONSOLE_SHELL_TASKS_ON 1
#endif

/** @brief 启用 clear 命令，输出 ANSI 终端清屏序列。 */
#ifndef XCONSOLE_SHELL_CLEAR_ON
#define XCONSOLE_SHELL_CLEAR_ON 1
#endif

/** @brief 启用 reset 命令，请求产品后端执行立即系统复位。 */
#ifndef XCONSOLE_SHELL_RESET_ON
#define XCONSOLE_SHELL_RESET_ON 0
#endif

/** @brief 启用 reboot 命令，请求产品后端执行有序系统重启。 */
#ifndef XCONSOLE_SHELL_REBOOT_ON
#define XCONSOLE_SHELL_REBOOT_ON 0
#endif

/** @brief 启用 shutdown 命令；有平台时请求关机，无平台时退出 Shell 程序。 */
#ifndef XCONSOLE_SHELL_SHUTDOWN_ON
#define XCONSOLE_SHELL_SHUTDOWN_ON 0
#endif

/** @brief 启用 exit 命令；退出当前 Shell 和所属应用事件循环。 */
#ifndef XCONSOLE_SHELL_EXIT_ON
#define XCONSOLE_SHELL_EXIT_ON 1
#endif

/* ==================================================================== */
/* 5. 网络与内存子命令                                                        */
/* ==================================================================== */

/** @brief 启用 `mem`，输出全局多级内存池的容量和使用率。 */
#ifndef XCONSOLE_SHELL_MEMORY_POOL_ON
#define XCONSOLE_SHELL_MEMORY_POOL_ON 1
#endif

/** @brief 启用 `net ifconfig`，列出网络接口、状态、MTU 和地址。 */
#ifndef XCONSOLE_SHELL_NET_IFCONFIG_ON
#define XCONSOLE_SHELL_NET_IFCONFIG_ON 1
#endif

/** @brief 启用 `net hostname`，输出本机主机名。 */
#ifndef XCONSOLE_SHELL_NET_HOSTNAME_ON
#define XCONSOLE_SHELL_NET_HOSTNAME_ON 1
#endif

/** @brief 启用 `net resolve`，通过 XHostInfo 查询主机地址。 */
#ifndef XCONSOLE_SHELL_NET_RESOLVE_ON
#define XCONSOLE_SHELL_NET_RESOLVE_ON 1
#endif

/** @brief 启用 `net ping`，通过 XNetwork ICMP Echo 探测 IPv4 主机。 */
#ifndef XCONSOLE_SHELL_NET_PING_ON
#define XCONSOLE_SHELL_NET_PING_ON 1
#endif

/* ==================================================================== */
/* 6. 文件系统子命令与编辑器                                                     */
/* ==================================================================== */

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

/** @brief 启用 `hexdump`，以十六进制和 ASCII 形式分块查看文件。 */
#ifndef XCONSOLE_SHELL_FS_HEXDUMP_ON
#define XCONSOLE_SHELL_FS_HEXDUMP_ON 1
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

/** @brief 启用 `vi`/`vim` 行编辑命令，通过 Shell 输入状态机编辑文件。 */
#ifndef XCONSOLE_SHELL_EDITOR_ON
#define XCONSOLE_SHELL_EDITOR_ON 1
#endif

/** @brief `vi`/`vim` 编辑缓冲的最大行数；超过时打开文件会报错。 */
#ifndef XCONSOLE_SHELL_EDITOR_MAX_LINES
#define XCONSOLE_SHELL_EDITOR_MAX_LINES 64
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

/* ==================================================================== */
/* 7. 执行器、进程与交互增强                                                     */
/* ==================================================================== */

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

/* ==================================================================== */
/* 8. 提示符默认名称                                                         */
/* ==================================================================== */

/**
* @brief 提示符默认用户名。
* @details
* 未启用登录（XCONSOLE_SHELL_LOGIN_ON=0）或当前会话尚未登录时，提示符使用该
* 名称，例如 `XinYueC> `。启用登录后，提示符优先显示当前登录用户名。该宏仅
* 影响提示符显示，不改变命令执行逻辑。
*/
#ifndef XCONSOLE_SHELL_DEFAULT_PROMPT_NAME
#define XCONSOLE_SHELL_DEFAULT_PROMPT_NAME "XinYueC"
#endif

/* ==================================================================== */
/* 9. 登录、认证与权限                                                        */
/* ==================================================================== */

/** @brief 启用登录、用户管理和本地 JSON 账户库。 */
#ifndef XCONSOLE_SHELL_LOGIN_ON
#define XCONSOLE_SHELL_LOGIN_ON 0
#endif

/**
 * @brief 是否强制要求 Shell 会话登录后才能执行普通命令。
 * @details
 * 开启后，未认证会话只允许执行带 AllowUnauthenticated 标志的认证生命周期
 * 命令，例如 login、useradd、passwd/password、logout、help、version 和 exit。
 * 首次创建无密码管理员后，仍可使用 passwd <user> <password> 完成初始化。
 */
#ifndef XCONSOLE_SHELL_LOGIN_REQUIRED_ON
#define XCONSOLE_SHELL_LOGIN_REQUIRED_ON XCONSOLE_SHELL_LOGIN_ON
#endif

/** @brief 启用 Linux 风格用户管理命令。 */
#ifndef XCONSOLE_SHELL_USER_COMMANDS_ON
#define XCONSOLE_SHELL_USER_COMMANDS_ON 1
#endif

/** @brief 用户账户 JSON 文件默认路径；使用 XFileSystem 解析。 */
#ifndef XCONSOLE_SHELL_LOGIN_CONFIG_PATH
#define XCONSOLE_SHELL_LOGIN_CONFIG_PATH "xconsole_users.json"
#endif

/** @brief 用户名最大字节数，包含结尾 NUL。 */
#ifndef XCONSOLE_SHELL_LOGIN_NAME_SIZE
#define XCONSOLE_SHELL_LOGIN_NAME_SIZE 32
#endif

/** @brief 密码最大字节数，不包含结尾 NUL。 */
#ifndef XCONSOLE_SHELL_LOGIN_PASSWORD_SIZE
#define XCONSOLE_SHELL_LOGIN_PASSWORD_SIZE 128
#endif

/** @brief 单个会话最多保存的附加组 ID 数量。 */
#ifndef XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY
#define XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY 8
#endif

/** @brief 用户 JSON 文件允许保存的账户数量。 */
#ifndef XCONSOLE_SHELL_LOGIN_USER_CAPACITY
#define XCONSOLE_SHELL_LOGIN_USER_CAPACITY 16
#endif

/** @brief 账户 JSON 文件最大字节数。 */
#ifndef XCONSOLE_SHELL_LOGIN_CONFIG_MAX_BYTES
#define XCONSOLE_SHELL_LOGIN_CONFIG_MAX_BYTES 8192
#endif

/** @brief 密码摘要迭代次数；使用 XCryptographicHash 的 HMAC-SHA256。 */
#ifndef XCONSOLE_SHELL_LOGIN_HASH_ITERATIONS
#define XCONSOLE_SHELL_LOGIN_HASH_ITERATIONS 1000
#endif

/** @brief 启用认证状态字段；登录模块开启时默认同步开启。 */
#ifndef XCONSOLE_SHELL_AUTH_ON
#define XCONSOLE_SHELL_AUTH_ON XCONSOLE_SHELL_LOGIN_ON
#endif

/** @brief 启用命令权限检查和危险命令权限位；登录模块开启时默认同步开启。 */
#ifndef XCONSOLE_SHELL_PERMISSION_ON
#define XCONSOLE_SHELL_PERMISSION_ON XCONSOLE_SHELL_LOGIN_ON
#endif

/** @brief 启用命令审计回调；产品负责日志脱敏和存储。 */
#ifndef XCONSOLE_SHELL_AUDIT_ON
#define XCONSOLE_SHELL_AUDIT_ON 0
#endif

/** @brief 启用运行统计字段和统计查询 API。 */
#ifndef XCONSOLE_SHELL_STATS_ON
#define XCONSOLE_SHELL_STATS_ON 0
#endif

/* ==================================================================== */
/* 10. 硬件外设命令（GPIO / ADC / PWM / I2C / SPI / CAN）                     */
/* ==================================================================== */

/** @brief GPIO 命令树总开关；所有操作只调用 XGpio 公共 API。 */
#ifndef XCONSOLE_SHELL_GPIO_ON
#define XCONSOLE_SHELL_GPIO_ON 0
#endif

/** @brief 启用 gpio list，列出当前 Shell 持有的 GPIO 槽位。 */
#ifndef XCONSOLE_SHELL_GPIO_LIST_ON
#define XCONSOLE_SHELL_GPIO_LIST_ON 1
#endif

/** @brief 启用 gpio open，创建并打开固定槽位 GPIO。 */
#ifndef XCONSOLE_SHELL_GPIO_OPEN_ON
#define XCONSOLE_SHELL_GPIO_OPEN_ON 1
#endif

/** @brief 启用 gpio close，关闭并释放固定槽位 GPIO。 */
#ifndef XCONSOLE_SHELL_GPIO_CLOSE_ON
#define XCONSOLE_SHELL_GPIO_CLOSE_ON 1
#endif

/** @brief 启用 gpio info，输出配置、能力和错误状态。 */
#ifndef XCONSOLE_SHELL_GPIO_INFO_ON
#define XCONSOLE_SHELL_GPIO_INFO_ON 1
#endif

/** @brief 启用 gpio read，读取物理电平或有效状态。 */
#ifndef XCONSOLE_SHELL_GPIO_READ_ON
#define XCONSOLE_SHELL_GPIO_READ_ON 1
#endif

/** @brief 启用 gpio write；该命令会改变硬件状态，默认关闭。 */
#ifndef XCONSOLE_SHELL_GPIO_WRITE_ON
#define XCONSOLE_SHELL_GPIO_WRITE_ON 0
#endif

/** @brief 启用 gpio toggle；该命令会改变硬件状态，默认关闭。 */
#ifndef XCONSOLE_SHELL_GPIO_TOGGLE_ON
#define XCONSOLE_SHELL_GPIO_TOGGLE_ON 0
#endif

/** @brief 启用 gpio configure；该命令会重新配置硬件，默认关闭。 */
#ifndef XCONSOLE_SHELL_GPIO_CONFIGURE_ON
#define XCONSOLE_SHELL_GPIO_CONFIGURE_ON 0
#endif

/** @brief 启用 gpio irq 子命令；中断配置和等待默认关闭。 */
#ifndef XCONSOLE_SHELL_GPIO_INTERRUPT_ON
#define XCONSOLE_SHELL_GPIO_INTERRUPT_ON 0
#endif

/** @brief 危险 GPIO 操作是否必须通过产品引脚策略回调。 */
#ifndef XCONSOLE_SHELL_GPIO_REQUIRE_POLICY_ON
#define XCONSOLE_SHELL_GPIO_REQUIRE_POLICY_ON 1
#endif

/** @brief 单个 Shell 可同时持有的 GPIO 固定槽位数量。 */
#ifndef XCONSOLE_SHELL_GPIO_SLOT_CAPACITY
#define XCONSOLE_SHELL_GPIO_SLOT_CAPACITY 8
#endif

/** @brief 中断等待循环单次阻塞毫秒数，用于周期检查取消状态。 */
#ifndef XCONSOLE_SHELL_GPIO_EVENT_SLICE_MS
#define XCONSOLE_SHELL_GPIO_EVENT_SLICE_MS 50
#endif

/** @brief gpio irq wait 单次允许等待的最大事件数量。 */
#ifndef XCONSOLE_SHELL_GPIO_MAX_WAIT_COUNT
#define XCONSOLE_SHELL_GPIO_MAX_WAIT_COUNT 1000
#endif

/** @brief gpio irq wait 允许的最大等待毫秒数。 */
#ifndef XCONSOLE_SHELL_GPIO_MAX_WAIT_MS
#define XCONSOLE_SHELL_GPIO_MAX_WAIT_MS 60000
#endif

/** @brief ADC 命令树总开关；所有操作只调用 XAdc 公共 API。 */
#ifndef XCONSOLE_SHELL_ADC_ON
#define XCONSOLE_SHELL_ADC_ON 0
#endif

/** @brief 启用 adc list，列出当前 Shell 持有的 ADC 槽位。 */
#ifndef XCONSOLE_SHELL_ADC_LIST_ON
#define XCONSOLE_SHELL_ADC_LIST_ON 1
#endif

/** @brief 启用 adc open，创建并打开固定槽位 ADC。 */
#ifndef XCONSOLE_SHELL_ADC_OPEN_ON
#define XCONSOLE_SHELL_ADC_OPEN_ON 1
#endif

/** @brief 启用 adc close，关闭并释放固定槽位 ADC。 */
#ifndef XCONSOLE_SHELL_ADC_CLOSE_ON
#define XCONSOLE_SHELL_ADC_CLOSE_ON 1
#endif

/** @brief 启用 adc info，输出配置、能力和错误状态。 */
#ifndef XCONSOLE_SHELL_ADC_INFO_ON
#define XCONSOLE_SHELL_ADC_INFO_ON 1
#endif

/** @brief 启用 adc read，读取原始值或毫伏值。 */
#ifndef XCONSOLE_SHELL_ADC_READ_ON
#define XCONSOLE_SHELL_ADC_READ_ON 1
#endif

/** @brief 启用 adc configure，重新配置采样参数。 */
#ifndef XCONSOLE_SHELL_ADC_CONFIGURE_ON
#define XCONSOLE_SHELL_ADC_CONFIGURE_ON 0
#endif

/** @brief ADC 危险操作是否必须通过产品策略回调。 */
#ifndef XCONSOLE_SHELL_ADC_REQUIRE_POLICY_ON
#define XCONSOLE_SHELL_ADC_REQUIRE_POLICY_ON 1
#endif

/** @brief 单个 Shell 同时持有的 ADC 固定槽位数量。 */
#ifndef XCONSOLE_SHELL_ADC_SLOT_CAPACITY
#define XCONSOLE_SHELL_ADC_SLOT_CAPACITY 4
#endif

/** @brief ADC 读取命令的最大等待毫秒数。 */
#ifndef XCONSOLE_SHELL_ADC_MAX_WAIT_MS
#define XCONSOLE_SHELL_ADC_MAX_WAIT_MS 60000
#endif

/** @brief PWM 命令树总开关；所有操作只调用 XPwm 公共 API。 */
#ifndef XCONSOLE_SHELL_PWM_ON
#define XCONSOLE_SHELL_PWM_ON 0
#endif

/** @brief 启用 pwm list，列出当前 Shell 持有的 PWM 槽位。 */
#ifndef XCONSOLE_SHELL_PWM_LIST_ON
#define XCONSOLE_SHELL_PWM_LIST_ON 1
#endif

/** @brief 启用 pwm open，创建并打开固定槽位 PWM。 */
#ifndef XCONSOLE_SHELL_PWM_OPEN_ON
#define XCONSOLE_SHELL_PWM_OPEN_ON 1
#endif

/** @brief 启用 pwm close，停止并释放固定槽位 PWM。 */
#ifndef XCONSOLE_SHELL_PWM_CLOSE_ON
#define XCONSOLE_SHELL_PWM_CLOSE_ON 1
#endif

/** @brief 启用 pwm info，输出配置、能力和运行状态。 */
#ifndef XCONSOLE_SHELL_PWM_INFO_ON
#define XCONSOLE_SHELL_PWM_INFO_ON 1
#endif

/** @brief 启用 pwm configure，重新配置频率、占空比和极性。 */
#ifndef XCONSOLE_SHELL_PWM_CONFIGURE_ON
#define XCONSOLE_SHELL_PWM_CONFIGURE_ON 0
#endif

/** @brief 启用 pwm start，启动硬件输出。 */
#ifndef XCONSOLE_SHELL_PWM_START_ON
#define XCONSOLE_SHELL_PWM_START_ON 1
#endif

/** @brief 启用 pwm stop，停止硬件输出。 */
#ifndef XCONSOLE_SHELL_PWM_STOP_ON
#define XCONSOLE_SHELL_PWM_STOP_ON 1
#endif

/** @brief 启用 pwm set-frequency，动态修改频率。 */
#ifndef XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON
#define XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON 1
#endif

/** @brief 启用 pwm set-duty，动态修改占空比。 */
#ifndef XCONSOLE_SHELL_PWM_SET_DUTY_ON
#define XCONSOLE_SHELL_PWM_SET_DUTY_ON 1
#endif

/** @brief PWM 危险操作是否必须通过产品策略回调。 */
#ifndef XCONSOLE_SHELL_PWM_REQUIRE_POLICY_ON
#define XCONSOLE_SHELL_PWM_REQUIRE_POLICY_ON 1
#endif

/** @brief 单个 Shell 同时持有的 PWM 固定槽位数量。 */
#ifndef XCONSOLE_SHELL_PWM_SLOT_CAPACITY
#define XCONSOLE_SHELL_PWM_SLOT_CAPACITY 4
#endif

/** @brief I2C 命令树总开关；所有访问通过 XI2c 公共接口完成。 */
#ifndef XCONSOLE_SHELL_I2C_ON
#define XCONSOLE_SHELL_I2C_ON 0
#endif

/** @brief 启用 i2c list，列出当前 Shell 持有的 I2C 槽位。 */
#ifndef XCONSOLE_SHELL_I2C_LIST_ON
#define XCONSOLE_SHELL_I2C_LIST_ON 1
#endif

/** @brief 启用 i2c open，打开指定控制器和从设备地址。 */
#ifndef XCONSOLE_SHELL_I2C_OPEN_ON
#define XCONSOLE_SHELL_I2C_OPEN_ON 1
#endif

/** @brief 启用 i2c close，关闭并释放 I2C 槽位。 */
#ifndef XCONSOLE_SHELL_I2C_CLOSE_ON
#define XCONSOLE_SHELL_I2C_CLOSE_ON 1
#endif

/** @brief 启用 i2c info，输出配置、能力和错误状态。 */
#ifndef XCONSOLE_SHELL_I2C_INFO_ON
#define XCONSOLE_SHELL_I2C_INFO_ON 1
#endif

/** @brief 启用 i2c read，读取指定长度的字节。 */
#ifndef XCONSOLE_SHELL_I2C_READ_ON
#define XCONSOLE_SHELL_I2C_READ_ON 1
#endif

/** @brief 启用 i2c write，向从设备写入数据；默认关闭。 */
#ifndef XCONSOLE_SHELL_I2C_WRITE_ON
#define XCONSOLE_SHELL_I2C_WRITE_ON 0
#endif

/** @brief 启用 i2c writeread，执行写后重复起始再读；默认关闭。 */
#ifndef XCONSOLE_SHELL_I2C_WRITEREAD_ON
#define XCONSOLE_SHELL_I2C_WRITEREAD_ON 0
#endif

/** @brief 危险 I2C 写操作是否必须通过产品授权回调。 */
#ifndef XCONSOLE_SHELL_I2C_REQUIRE_POLICY_ON
#define XCONSOLE_SHELL_I2C_REQUIRE_POLICY_ON 1
#endif

/** @brief Shell 同时持有的 I2C 固定槽位数量。 */
#ifndef XCONSOLE_SHELL_I2C_SLOT_CAPACITY
#define XCONSOLE_SHELL_I2C_SLOT_CAPACITY 4
#endif

/** @brief I2C 单次读写允许的最大字节数。 */
#ifndef XCONSOLE_SHELL_I2C_MAX_TRANSFER
#define XCONSOLE_SHELL_I2C_MAX_TRANSFER 256
#endif

/** @brief SPI 命令树总开关；所有访问通过 XSpi 公共接口完成。 */
#ifndef XCONSOLE_SHELL_SPI_ON
#define XCONSOLE_SHELL_SPI_ON 0
#endif

/** @brief 启用 spi list，列出当前 Shell 持有的 SPI 槽位。 */
#ifndef XCONSOLE_SHELL_SPI_LIST_ON
#define XCONSOLE_SHELL_SPI_LIST_ON 1
#endif

/** @brief 启用 spi open，打开指定控制器和片选。 */
#ifndef XCONSOLE_SHELL_SPI_OPEN_ON
#define XCONSOLE_SHELL_SPI_OPEN_ON 1
#endif

/** @brief 启用 spi close，关闭并释放 SPI 槽位。 */
#ifndef XCONSOLE_SHELL_SPI_CLOSE_ON
#define XCONSOLE_SHELL_SPI_CLOSE_ON 1
#endif

/** @brief 启用 spi info，输出配置、能力和错误状态。 */
#ifndef XCONSOLE_SHELL_SPI_INFO_ON
#define XCONSOLE_SHELL_SPI_INFO_ON 1
#endif

/** @brief 启用 spi transfer，全双工交换十六进制字节；默认关闭。 */
#ifndef XCONSOLE_SHELL_SPI_TRANSFER_ON
#define XCONSOLE_SHELL_SPI_TRANSFER_ON 0
#endif

/** @brief 危险 SPI 传输是否必须通过产品授权回调。 */
#ifndef XCONSOLE_SHELL_SPI_REQUIRE_POLICY_ON
#define XCONSOLE_SHELL_SPI_REQUIRE_POLICY_ON 1
#endif

/** @brief Shell 同时持有的 SPI 固定槽位数量。 */
#ifndef XCONSOLE_SHELL_SPI_SLOT_CAPACITY
#define XCONSOLE_SHELL_SPI_SLOT_CAPACITY 4
#endif

/** @brief SPI 单次全双工交换允许的最大字节数。 */
#ifndef XCONSOLE_SHELL_SPI_MAX_TRANSFER
#define XCONSOLE_SHELL_SPI_MAX_TRANSFER 256
#endif

/** @brief CAN 命令树总开关；所有访问通过 XCan 公共接口完成。 */
#ifndef XCONSOLE_SHELL_CAN_ON
#define XCONSOLE_SHELL_CAN_ON 0
#endif

/** @brief 启用 can list，列出当前 Shell 持有的 CAN 控制器。 */
#ifndef XCONSOLE_SHELL_CAN_LIST_ON
#define XCONSOLE_SHELL_CAN_LIST_ON 1
#endif

/** @brief 启用 can open，创建并打开 CAN 控制器。 */
#ifndef XCONSOLE_SHELL_CAN_OPEN_ON
#define XCONSOLE_SHELL_CAN_OPEN_ON 1
#endif

/** @brief 启用 can close，关闭并释放 CAN 控制器。 */
#ifndef XCONSOLE_SHELL_CAN_CLOSE_ON
#define XCONSOLE_SHELL_CAN_CLOSE_ON 1
#endif

/** @brief 启用 can info，输出配置、能力和错误状态。 */
#ifndef XCONSOLE_SHELL_CAN_INFO_ON
#define XCONSOLE_SHELL_CAN_INFO_ON 1
#endif

/** @brief 启用 can status，输出总线状态和错误计数。 */
#ifndef XCONSOLE_SHELL_CAN_STATUS_ON
#define XCONSOLE_SHELL_CAN_STATUS_ON 1
#endif

/** @brief 启用 can start，启动已打开的 CAN 控制器。 */
#ifndef XCONSOLE_SHELL_CAN_START_ON
#define XCONSOLE_SHELL_CAN_START_ON 1
#endif

/** @brief 启用 can stop，停止 CAN 控制器但保留句柄。 */
#ifndef XCONSOLE_SHELL_CAN_STOP_ON
#define XCONSOLE_SHELL_CAN_STOP_ON 1
#endif

/** @brief 启用 can send，发送 CAN/CAN FD 帧；默认关闭。 */
#ifndef XCONSOLE_SHELL_CAN_SEND_ON
#define XCONSOLE_SHELL_CAN_SEND_ON 0
#endif

/** @brief 启用 can receive，同步接收 CAN/CAN FD 帧。 */
#ifndef XCONSOLE_SHELL_CAN_RECEIVE_ON
#define XCONSOLE_SHELL_CAN_RECEIVE_ON 1
#endif

/** @brief 启用 can recover，请求 Bus Off 恢复；默认关闭。 */
#ifndef XCONSOLE_SHELL_CAN_RECOVER_ON
#define XCONSOLE_SHELL_CAN_RECOVER_ON 0
#endif

/** @brief 启用 can filter，管理接收过滤器；默认关闭。 */
#ifndef XCONSOLE_SHELL_CAN_FILTER_ON
#define XCONSOLE_SHELL_CAN_FILTER_ON 0
#endif

/** @brief CAN 危险操作是否必须通过产品授权回调。 */
#ifndef XCONSOLE_SHELL_CAN_REQUIRE_POLICY_ON
#define XCONSOLE_SHELL_CAN_REQUIRE_POLICY_ON 1
#endif

/** @brief Shell 同时持有的 CAN 固定槽位数量。 */
#ifndef XCONSOLE_SHELL_CAN_SLOT_CAPACITY
#define XCONSOLE_SHELL_CAN_SLOT_CAPACITY 2
#endif

/** @brief can receive 单次最多接收的帧数。 */
#ifndef XCONSOLE_SHELL_CAN_MAX_RECEIVE_COUNT
#define XCONSOLE_SHELL_CAN_MAX_RECEIVE_COUNT 16
#endif

/** @brief CAN 命令允许的最大等待毫秒数。 */
#ifndef XCONSOLE_SHELL_CAN_MAX_TIMEOUT_MS
#define XCONSOLE_SHELL_CAN_MAX_TIMEOUT_MS 60000
#endif

/** @brief CAN 通道名称的固定缓存容量，包含结尾空字符。 */
#ifndef XCONSOLE_SHELL_CAN_NAME_SIZE
#define XCONSOLE_SHELL_CAN_NAME_SIZE 32
#endif

/* ==================================================================== */
/* 11. 容量配置                                                           */
/* ==================================================================== */

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

/* ==================================================================== */
/* 12. 依赖检查与强制裁剪                                                      */
/* ==================================================================== */


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
#undef XCONSOLE_SHELL_ASYNC_ON
#define XCONSOLE_SHELL_ASYNC_ON 0
#undef XCONSOLE_SHELL_ASYNC_AUTO_START_ON
#define XCONSOLE_SHELL_ASYNC_AUTO_START_ON 0
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
#undef XCONSOLE_SHELL_NETWORK_ON
#define XCONSOLE_SHELL_NETWORK_ON 0
#undef XCONSOLE_SHELL_DATETIME_ON
#define XCONSOLE_SHELL_DATETIME_ON 0
#undef XCONSOLE_SHELL_DATE_ON
#define XCONSOLE_SHELL_DATE_ON 0
#undef XCONSOLE_SHELL_MEMORY_ON
#define XCONSOLE_SHELL_MEMORY_ON 0
#undef XCONSOLE_SHELL_MEMORY_POOL_ON
#define XCONSOLE_SHELL_MEMORY_POOL_ON 0
#undef XCONSOLE_SHELL_INFO_ON
#define XCONSOLE_SHELL_INFO_ON 0
#undef XCONSOLE_SHELL_UPTIME_ON
#define XCONSOLE_SHELL_UPTIME_ON 0
#undef XCONSOLE_SHELL_TASKS_ON
#define XCONSOLE_SHELL_TASKS_ON 0
#undef XCONSOLE_SHELL_CLEAR_ON
#define XCONSOLE_SHELL_CLEAR_ON 0
#undef XCONSOLE_SHELL_RESET_ON
#define XCONSOLE_SHELL_RESET_ON 0
#undef XCONSOLE_SHELL_REBOOT_ON
#define XCONSOLE_SHELL_REBOOT_ON 0
#undef XCONSOLE_SHELL_SHUTDOWN_ON
#define XCONSOLE_SHELL_SHUTDOWN_ON 0
#undef XCONSOLE_SHELL_EXIT_ON
#define XCONSOLE_SHELL_EXIT_ON 0
#undef XCONSOLE_SHELL_ASYNC_ON
#define XCONSOLE_SHELL_ASYNC_ON 0
#undef XCONSOLE_SHELL_ASYNC_AUTO_START_ON
#define XCONSOLE_SHELL_ASYNC_AUTO_START_ON 0
#undef XCONSOLE_SHELL_GPIO_ON
#define XCONSOLE_SHELL_GPIO_ON 0
#undef XCONSOLE_SHELL_ADC_ON
#define XCONSOLE_SHELL_ADC_ON 0
#undef XCONSOLE_SHELL_PWM_ON
#define XCONSOLE_SHELL_PWM_ON 0
#undef XCONSOLE_SHELL_I2C_ON
#define XCONSOLE_SHELL_I2C_ON 0
#undef XCONSOLE_SHELL_SPI_ON
#define XCONSOLE_SHELL_SPI_ON 0
#undef XCONSOLE_SHELL_CAN_ON
#define XCONSOLE_SHELL_CAN_ON 0
#undef XCONSOLE_SHELL_EXECUTOR_ON
#define XCONSOLE_SHELL_EXECUTOR_ON 0
#undef XCONSOLE_SHELL_PIPE_ON
#define XCONSOLE_SHELL_PIPE_ON 0
#undef XCONSOLE_SHELL_REDIRECT_ON
#define XCONSOLE_SHELL_REDIRECT_ON 0
#undef XCONSOLE_SHELL_SCRIPT_ON
#define XCONSOLE_SHELL_SCRIPT_ON 0
#undef XCONSOLE_SHELL_LOGIN_ON
#define XCONSOLE_SHELL_LOGIN_ON 0
#undef XCONSOLE_SHELL_LOGIN_REQUIRED_ON
#define XCONSOLE_SHELL_LOGIN_REQUIRED_ON 0
#undef XCONSOLE_SHELL_USER_COMMANDS_ON
#define XCONSOLE_SHELL_USER_COMMANDS_ON 0
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
#undef XCONSOLE_SHELL_FS_HEXDUMP_ON
#define XCONSOLE_SHELL_FS_HEXDUMP_ON 0
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
#undef XCONSOLE_SHELL_EDITOR_ON
#define XCONSOLE_SHELL_EDITOR_ON 0
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
#undef XCONSOLE_SHELL_NETWORK_ON
#define XCONSOLE_SHELL_NETWORK_ON 0
#undef XCONSOLE_SHELL_DATETIME_ON
#define XCONSOLE_SHELL_DATETIME_ON 0
#undef XCONSOLE_SHELL_DATE_ON
#define XCONSOLE_SHELL_DATE_ON 0
#undef XCONSOLE_SHELL_MEMORY_ON
#define XCONSOLE_SHELL_MEMORY_ON 0
#undef XCONSOLE_SHELL_MEMORY_POOL_ON
#define XCONSOLE_SHELL_MEMORY_POOL_ON 0
#undef XCONSOLE_SHELL_INFO_ON
#define XCONSOLE_SHELL_INFO_ON 0
#undef XCONSOLE_SHELL_UPTIME_ON
#define XCONSOLE_SHELL_UPTIME_ON 0
#undef XCONSOLE_SHELL_TASKS_ON
#define XCONSOLE_SHELL_TASKS_ON 0
#undef XCONSOLE_SHELL_CLEAR_ON
#define XCONSOLE_SHELL_CLEAR_ON 0
#undef XCONSOLE_SHELL_RESET_ON
#define XCONSOLE_SHELL_RESET_ON 0
#undef XCONSOLE_SHELL_REBOOT_ON
#define XCONSOLE_SHELL_REBOOT_ON 0
#undef XCONSOLE_SHELL_SHUTDOWN_ON
#define XCONSOLE_SHELL_SHUTDOWN_ON 0
#undef XCONSOLE_SHELL_EXIT_ON
#define XCONSOLE_SHELL_EXIT_ON 0
#undef XCONSOLE_SHELL_LOGIN_ON
#define XCONSOLE_SHELL_LOGIN_ON 0
#undef XCONSOLE_SHELL_LOGIN_REQUIRED_ON
#define XCONSOLE_SHELL_LOGIN_REQUIRED_ON 0
#undef XCONSOLE_SHELL_USER_COMMANDS_ON
#define XCONSOLE_SHELL_USER_COMMANDS_ON 0
#undef XCONSOLE_SHELL_GPIO_ON
#define XCONSOLE_SHELL_GPIO_ON 0
#undef XCONSOLE_SHELL_ADC_ON
#define XCONSOLE_SHELL_ADC_ON 0
#undef XCONSOLE_SHELL_PWM_ON
#define XCONSOLE_SHELL_PWM_ON 0
#undef XCONSOLE_SHELL_I2C_ON
#define XCONSOLE_SHELL_I2C_ON 0
#undef XCONSOLE_SHELL_SPI_ON
#define XCONSOLE_SHELL_SPI_ON 0
#undef XCONSOLE_SHELL_CAN_ON
#define XCONSOLE_SHELL_CAN_ON 0
#undef XCONSOLE_SHELL_EXECUTOR_ON
#define XCONSOLE_SHELL_EXECUTOR_ON 0
#undef XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
#define XCONSOLE_SHELL_DYNAMIC_REGISTER_ON 0
#endif

#if !XCONSOLE_SHELL_NETWORK_ON
#undef XCONSOLE_SHELL_NET_IFCONFIG_ON
#define XCONSOLE_SHELL_NET_IFCONFIG_ON 0
#undef XCONSOLE_SHELL_NET_HOSTNAME_ON
#define XCONSOLE_SHELL_NET_HOSTNAME_ON 0
#undef XCONSOLE_SHELL_NET_RESOLVE_ON
#define XCONSOLE_SHELL_NET_RESOLVE_ON 0
#undef XCONSOLE_SHELL_NET_PING_ON
#define XCONSOLE_SHELL_NET_PING_ON 0
#endif

#if !XCONSOLE_SHELL_DATETIME_ON
#undef XCONSOLE_SHELL_DATE_ON
#define XCONSOLE_SHELL_DATE_ON 0
#endif

#if !XCONSOLE_SHELL_MEMORY_ON
#undef XCONSOLE_SHELL_MEMORY_POOL_ON
#define XCONSOLE_SHELL_MEMORY_POOL_ON 0
#endif

#if !XCONSOLE_SHELL_GPIO_ON
#undef XCONSOLE_SHELL_GPIO_LIST_ON
#define XCONSOLE_SHELL_GPIO_LIST_ON 0
#undef XCONSOLE_SHELL_GPIO_OPEN_ON
#define XCONSOLE_SHELL_GPIO_OPEN_ON 0
#undef XCONSOLE_SHELL_GPIO_CLOSE_ON
#define XCONSOLE_SHELL_GPIO_CLOSE_ON 0
#undef XCONSOLE_SHELL_GPIO_INFO_ON
#define XCONSOLE_SHELL_GPIO_INFO_ON 0
#undef XCONSOLE_SHELL_GPIO_READ_ON
#define XCONSOLE_SHELL_GPIO_READ_ON 0
#undef XCONSOLE_SHELL_GPIO_WRITE_ON
#define XCONSOLE_SHELL_GPIO_WRITE_ON 0
#undef XCONSOLE_SHELL_GPIO_TOGGLE_ON
#define XCONSOLE_SHELL_GPIO_TOGGLE_ON 0
#undef XCONSOLE_SHELL_GPIO_CONFIGURE_ON
#define XCONSOLE_SHELL_GPIO_CONFIGURE_ON 0
#undef XCONSOLE_SHELL_GPIO_INTERRUPT_ON
#define XCONSOLE_SHELL_GPIO_INTERRUPT_ON 0
#endif

#if !XCONSOLE_SHELL_ADC_ON
#undef XCONSOLE_SHELL_ADC_LIST_ON
#define XCONSOLE_SHELL_ADC_LIST_ON 0
#undef XCONSOLE_SHELL_ADC_OPEN_ON
#define XCONSOLE_SHELL_ADC_OPEN_ON 0
#undef XCONSOLE_SHELL_ADC_CLOSE_ON
#define XCONSOLE_SHELL_ADC_CLOSE_ON 0
#undef XCONSOLE_SHELL_ADC_INFO_ON
#define XCONSOLE_SHELL_ADC_INFO_ON 0
#undef XCONSOLE_SHELL_ADC_READ_ON
#define XCONSOLE_SHELL_ADC_READ_ON 0
#undef XCONSOLE_SHELL_ADC_CONFIGURE_ON
#define XCONSOLE_SHELL_ADC_CONFIGURE_ON 0
#endif

#if !XCONSOLE_SHELL_PWM_ON
#undef XCONSOLE_SHELL_PWM_LIST_ON
#define XCONSOLE_SHELL_PWM_LIST_ON 0
#undef XCONSOLE_SHELL_PWM_OPEN_ON
#define XCONSOLE_SHELL_PWM_OPEN_ON 0
#undef XCONSOLE_SHELL_PWM_CLOSE_ON
#define XCONSOLE_SHELL_PWM_CLOSE_ON 0
#undef XCONSOLE_SHELL_PWM_INFO_ON
#define XCONSOLE_SHELL_PWM_INFO_ON 0
#undef XCONSOLE_SHELL_PWM_CONFIGURE_ON
#define XCONSOLE_SHELL_PWM_CONFIGURE_ON 0
#undef XCONSOLE_SHELL_PWM_START_ON
#define XCONSOLE_SHELL_PWM_START_ON 0
#undef XCONSOLE_SHELL_PWM_STOP_ON
#define XCONSOLE_SHELL_PWM_STOP_ON 0
#undef XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON
#define XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON 0
#undef XCONSOLE_SHELL_PWM_SET_DUTY_ON
#define XCONSOLE_SHELL_PWM_SET_DUTY_ON 0
#endif

#if !XCONSOLE_SHELL_I2C_ON
#undef XCONSOLE_SHELL_I2C_LIST_ON
#define XCONSOLE_SHELL_I2C_LIST_ON 0
#undef XCONSOLE_SHELL_I2C_OPEN_ON
#define XCONSOLE_SHELL_I2C_OPEN_ON 0
#undef XCONSOLE_SHELL_I2C_CLOSE_ON
#define XCONSOLE_SHELL_I2C_CLOSE_ON 0
#undef XCONSOLE_SHELL_I2C_INFO_ON
#define XCONSOLE_SHELL_I2C_INFO_ON 0
#undef XCONSOLE_SHELL_I2C_READ_ON
#define XCONSOLE_SHELL_I2C_READ_ON 0
#undef XCONSOLE_SHELL_I2C_WRITE_ON
#define XCONSOLE_SHELL_I2C_WRITE_ON 0
#undef XCONSOLE_SHELL_I2C_WRITEREAD_ON
#define XCONSOLE_SHELL_I2C_WRITEREAD_ON 0
#endif

#if !XCONSOLE_SHELL_SPI_ON
#undef XCONSOLE_SHELL_SPI_LIST_ON
#define XCONSOLE_SHELL_SPI_LIST_ON 0
#undef XCONSOLE_SHELL_SPI_OPEN_ON
#define XCONSOLE_SHELL_SPI_OPEN_ON 0
#undef XCONSOLE_SHELL_SPI_CLOSE_ON
#define XCONSOLE_SHELL_SPI_CLOSE_ON 0
#undef XCONSOLE_SHELL_SPI_INFO_ON
#define XCONSOLE_SHELL_SPI_INFO_ON 0
#undef XCONSOLE_SHELL_SPI_TRANSFER_ON
#define XCONSOLE_SHELL_SPI_TRANSFER_ON 0
#endif

#if !XCONSOLE_SHELL_CAN_ON
#undef XCONSOLE_SHELL_CAN_LIST_ON
#define XCONSOLE_SHELL_CAN_LIST_ON 0
#undef XCONSOLE_SHELL_CAN_OPEN_ON
#define XCONSOLE_SHELL_CAN_OPEN_ON 0
#undef XCONSOLE_SHELL_CAN_CLOSE_ON
#define XCONSOLE_SHELL_CAN_CLOSE_ON 0
#undef XCONSOLE_SHELL_CAN_INFO_ON
#define XCONSOLE_SHELL_CAN_INFO_ON 0
#undef XCONSOLE_SHELL_CAN_STATUS_ON
#define XCONSOLE_SHELL_CAN_STATUS_ON 0
#undef XCONSOLE_SHELL_CAN_START_ON
#define XCONSOLE_SHELL_CAN_START_ON 0
#undef XCONSOLE_SHELL_CAN_STOP_ON
#define XCONSOLE_SHELL_CAN_STOP_ON 0
#undef XCONSOLE_SHELL_CAN_SEND_ON
#define XCONSOLE_SHELL_CAN_SEND_ON 0
#undef XCONSOLE_SHELL_CAN_RECEIVE_ON
#define XCONSOLE_SHELL_CAN_RECEIVE_ON 0
#undef XCONSOLE_SHELL_CAN_RECOVER_ON
#define XCONSOLE_SHELL_CAN_RECOVER_ON 0
#undef XCONSOLE_SHELL_CAN_FILTER_ON
#define XCONSOLE_SHELL_CAN_FILTER_ON 0
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
#if XCONSOLE_SHELL_NETWORK_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: NETWORK requires IO"
#endif
#if XCONSOLE_SHELL_DATETIME_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: DATETIME requires IO"
#endif
#if XCONSOLE_SHELL_MEMORY_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: MEMORY requires IO"
#endif
#if XCONSOLE_SHELL_INFO_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: INFO requires IO"
#endif
#if XCONSOLE_SHELL_UPTIME_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: UPTIME requires IO"
#endif
#if XCONSOLE_SHELL_TASKS_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: TASKS requires IO"
#endif
#if XCONSOLE_SHELL_CLEAR_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: CLEAR requires IO"
#endif
#if XCONSOLE_SHELL_RESET_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: RESET requires IO"
#endif
#if XCONSOLE_SHELL_REBOOT_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: REBOOT requires IO"
#endif
#if XCONSOLE_SHELL_SHUTDOWN_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: SHUTDOWN requires IO"
#endif
#if XCONSOLE_SHELL_LOGIN_ON && !XCONSOLE_SHELL_AUTH_ON
#error "XConsoleShell: LOGIN requires AUTH"
#endif
#if XCONSOLE_SHELL_LOGIN_ON && !XCONSOLE_SHELL_PERMISSION_ON
#error "XConsoleShell: LOGIN requires PERMISSION"
#endif
#if XCONSOLE_SHELL_LOGIN_REQUIRED_ON && !XCONSOLE_SHELL_LOGIN_ON
#error "XConsoleShell: LOGIN_REQUIRED requires LOGIN"
#endif
#if XCONSOLE_SHELL_LOGIN_ON && XCONSOLE_SHELL_LOGIN_NAME_SIZE < 2
#error "XConsoleShell: LOGIN_NAME_SIZE must be greater than one"
#endif
#if XCONSOLE_SHELL_LOGIN_ON && XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY < 1
#error "XConsoleShell: LOGIN_GROUP_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_LOGIN_ON && XCONSOLE_SHELL_LOGIN_USER_CAPACITY < 1
#error "XConsoleShell: LOGIN_USER_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_EXIT_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: EXIT requires IO"
#endif
#if XCONSOLE_SHELL_ASYNC_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: ASYNC requires IO"
#endif
#if XCONSOLE_SHELL_ASYNC_ON && XCONSOLE_SHELL_ASYNC_READ_BUDGET < 1
#error "XConsoleShell: ASYNC_READ_BUDGET must be greater than zero"
#endif
#if XCONSOLE_SHELL_ASYNC_ON && XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS < 1
#error "XConsoleShell: ASYNC_POLL_INTERVAL_MS must be greater than zero"
#endif
#if !XCONSOLE_SHELL_ASYNC_ON
#undef XCONSOLE_SHELL_ASYNC_AUTO_START_ON
#define XCONSOLE_SHELL_ASYNC_AUTO_START_ON 0
#endif
#if XCONSOLE_SHELL_ASYNC_RUN_MODE != XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER && \
    XCONSOLE_SHELL_ASYNC_RUN_MODE != XCONSOLE_SHELL_ASYNC_MODE_THREAD
#error "XConsoleShell: ASYNC_RUN_MODE is invalid"
#endif
#if XCONSOLE_SHELL_GPIO_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: GPIO requires IO"
#endif
#if XCONSOLE_SHELL_GPIO_ON && XCONSOLE_SHELL_GPIO_SLOT_CAPACITY < 1
#error "XConsoleShell: GPIO_SLOT_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_GPIO_ON && XCONSOLE_SHELL_GPIO_EVENT_SLICE_MS < 1
#error "XConsoleShell: GPIO_EVENT_SLICE_MS must be greater than zero"
#endif
#if XCONSOLE_SHELL_GPIO_ON && XCONSOLE_SHELL_GPIO_MAX_WAIT_COUNT < 1
#error "XConsoleShell: GPIO_MAX_WAIT_COUNT must be greater than zero"
#endif
#if XCONSOLE_SHELL_GPIO_ON && XCONSOLE_SHELL_GPIO_MAX_WAIT_MS < 1
#error "XConsoleShell: GPIO_MAX_WAIT_MS must be greater than zero"
#endif
#if XCONSOLE_SHELL_ADC_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: ADC requires IO"
#endif
#if XCONSOLE_SHELL_ADC_ON && XCONSOLE_SHELL_ADC_SLOT_CAPACITY < 1
#error "XConsoleShell: ADC_SLOT_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_ADC_ON && XCONSOLE_SHELL_ADC_MAX_WAIT_MS < 1
#error "XConsoleShell: ADC_MAX_WAIT_MS must be greater than zero"
#endif
#if XCONSOLE_SHELL_PWM_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: PWM requires IO"
#endif
#if XCONSOLE_SHELL_PWM_ON && XCONSOLE_SHELL_PWM_SLOT_CAPACITY < 1
#error "XConsoleShell: PWM_SLOT_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_I2C_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: I2C requires IO"
#endif
#if XCONSOLE_SHELL_I2C_ON && XCONSOLE_SHELL_I2C_SLOT_CAPACITY < 1
#error "XConsoleShell: I2C_SLOT_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_I2C_ON && XCONSOLE_SHELL_I2C_MAX_TRANSFER < 1
#error "XConsoleShell: I2C_MAX_TRANSFER must be greater than zero"
#endif
#if XCONSOLE_SHELL_I2C_ON && XCONSOLE_SHELL_I2C_MAX_TRANSFER > 1024
#error "XConsoleShell: I2C_MAX_TRANSFER must not exceed 1024"
#endif
#if XCONSOLE_SHELL_SPI_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: SPI requires IO"
#endif
#if XCONSOLE_SHELL_SPI_ON && XCONSOLE_SHELL_SPI_SLOT_CAPACITY < 1
#error "XConsoleShell: SPI_SLOT_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_SPI_ON && XCONSOLE_SHELL_SPI_MAX_TRANSFER < 1
#error "XConsoleShell: SPI_MAX_TRANSFER must be greater than zero"
#endif
#if XCONSOLE_SHELL_SPI_ON && XCONSOLE_SHELL_SPI_MAX_TRANSFER > 1024
#error "XConsoleShell: SPI_MAX_TRANSFER must not exceed 1024"
#endif
#if XCONSOLE_SHELL_CAN_ON && !XCONSOLE_SHELL_IO_ON
#error "XConsoleShell: CAN requires IO"
#endif
#if XCONSOLE_SHELL_CAN_ON && XCONSOLE_SHELL_CAN_SLOT_CAPACITY < 1
#error "XConsoleShell: CAN_SLOT_CAPACITY must be greater than zero"
#endif
#if XCONSOLE_SHELL_CAN_ON && XCONSOLE_SHELL_CAN_MAX_RECEIVE_COUNT < 1
#error "XConsoleShell: CAN_MAX_RECEIVE_COUNT must be greater than zero"
#endif
#if XCONSOLE_SHELL_CAN_ON && XCONSOLE_SHELL_CAN_MAX_TIMEOUT_MS < 1
#error "XConsoleShell: CAN_MAX_TIMEOUT_MS must be greater than zero"
#endif
#if XCONSOLE_SHELL_CAN_ON && XCONSOLE_SHELL_CAN_NAME_SIZE < 2
#error "XConsoleShell: CAN_NAME_SIZE must be at least 2"
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
