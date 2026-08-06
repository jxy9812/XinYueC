/**
 * @file XConsoleShellCommand.h
 * @brief XConsoleShell 命令描述、处理回调和结果码。
 * @details
 * 命令描述中的字符串、别名和子命令表默认由调用方静态持有，Shell 只保存
 * 指针，不复制也不释放。处理回调在 processLine 的同步调用栈内执行；不得
 * 保存 argv 指针或跨线程使用会话对象。所有名称均为 UTF-8，按 ASCII 区分大小写。
 */

#ifndef XCONSOLE_SHELL_COMMAND_H
#define XCONSOLE_SHELL_COMMAND_H

#include "XConsoleShellConfig.h"
#include <stdint.h>
#include <stddef.h>

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XConsoleShell XConsoleShell;
typedef struct XConsoleShellSession XConsoleShellSession;

/** @brief Shell 命令处理结果；枚举值不可按位组合。 */
typedef enum XConsoleResult {
    XConsoleResult_Ok = 0,              /**< 命令成功完成。 */
    XConsoleResult_MoreOutput = 1,      /**< 已输出一部分，调用方可继续调度。 */
    XConsoleResult_InvalidArgument = -1,/**< 公共参数无效。 */
    XConsoleResult_UnknownCommand = -2, /**< 命令或子命令不存在。 */
    XConsoleResult_InvalidSyntax = -3,  /**< 引号、转义或参数语法错误。 */
    XConsoleResult_PermissionDenied = -4,/**< 会话权限不足。 */
    XConsoleResult_NotSupported = -5,   /**< 功能开关或后端未启用。 */
    XConsoleResult_ResourceLimit = -6,  /**< 行、参数或命令表达到容量。 */
    XConsoleResult_Cancelled = -7,      /**< 由取消回调或 Ctrl-C 取消。 */
    XConsoleResult_IoError = -8,        /**< 输出或文件 I/O 失败。 */
    XConsoleResult_Failed = -9          /**< 处理函数报告未分类失败。 */
} XConsoleResult;

/** @brief 命令权限和行为标志。 */
typedef enum XConsoleCommandFlag {
    XConsoleCommandFlag_None = 0,
    XConsoleCommandFlag_Hidden = 1u << 0,    /**< 不在 help 列表显示。 */
    XConsoleCommandFlag_Dangerous = 1u << 1, /**< 需要产品权限策略确认。 */
    XConsoleCommandFlag_Administrator = 1u << 2 /**< 需要管理员权限。 */
} XConsoleCommandFlag;

/** @brief 会话权限掩码中的标准权限位；产品可在此基础上扩展自定义位。 */
typedef enum XConsoleShellPermission {
    XConsoleShellPermission_Dangerous = 1u << 0,      /**< 允许危险写入和删除命令。 */
    XConsoleShellPermission_Administrator = 1u << 1   /**< 允许管理员命令。 */
} XConsoleShellPermission;

/**
 * @brief Shell 命令处理回调。
 * @param shell 当前 Shell 对象；只在回调期间借用。
 * @param session 当前执行会话；只在回调期间借用。
 * @param argc 参数数量，包含 argv[0] 命令名。
 * @param argv UTF-8 参数数组；只在回调期间有效，不能保存指针。
 * @param userData 命令描述中的私有数据；Shell 不解释也不释放。
 * @return XConsoleResult 或可转换为该结果码的处理结果。
 */
typedef int (*XConsoleCommandHandler)(XConsoleShell* shell,
                                      XConsoleShellSession* session,
                                      int argc,
                                      const char* const* argv,
                                      void* userData);

/**
 * @brief 静态命令描述。
 * @details
 * name 必须是单个命令词；aliases 使用逗号分隔。subcommands 为静态数组，
 * handler 与 subcommands 可二选一；二者同时存在时先执行子命令查找。
 */
typedef struct XConsoleCommand {
    const char* name;                         /**< UTF-8 主名称；借用且不能为空。 */
    const char* aliases;                      /**< 逗号分隔别名；可为 NULL。 */
    const char* description;                  /**< help 描述；可为 NULL。 */
    const char* usage;                        /**< 用法文本；可为 NULL。 */
    int minArgs;                              /**< 最少参数数；不含命令名。 */
    int maxArgs;                              /**< 最多参数数；负数表示不限制。 */
    uint32_t flags;                           /**< XConsoleCommandFlag 位组合。 */
    XConsoleCommandHandler handler;           /**< 处理回调；可为 NULL。 */
    const struct XConsoleCommand* subcommands;/**< 静态子命令数组；可为 NULL。 */
    size_t subcommandCount;                   /**< 子命令数量。 */
    void* userData;                           /**< 回调私有数据；Shell 不释放。 */
} XConsoleCommand;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON */
#endif /* XCONSOLE_SHELL_COMMAND_H */
