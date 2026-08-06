/**
 * @file XConsoleShellIo.h
 * @brief XConsoleShell 的输入、输出、刷新和取消回调契约。
 * @details
 * 回调由传输适配层提供，Shell 不取得 userData 所有权。write 允许短写，
 * Shell_write 会循环推进偏移并在失败时返回 IoError。回调必须在调用线程
 * 上完成或由产品自行保证串行化；本文件不包含 UART、TCP 或操作系统头文件。
 */

#ifndef XCONSOLE_SHELL_IO_H
#define XCONSOLE_SHELL_IO_H

#include "XConsoleShellConfig.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON

#ifdef __cplusplus
extern "C" {
#endif

struct XConsoleShellSession;
struct XConsoleCommand;

/**
 * @brief 读取传输数据。
 * @param userData XConsoleShellIo::userData 借用上下文。
 * @param data 输出缓冲；size 非零时不能为空。
 * @param size 缓冲区容量。
 * @return 实际读取字节数；0 表示暂无数据，负数表示传输错误。
 */
typedef int64_t (*XConsoleReadFn)(void* userData, void* data, size_t size);
/**
 * @brief 写出传输数据。
 * @param userData XConsoleShellIo::userData 借用上下文。
 * @param data 输入数据；只在调用期间借用。
 * @param size 待读取写出字节数。
 * @return 实际写出字节数；允许短写，负数表示错误。
 */
typedef int64_t (*XConsoleWriteFn)(void* userData, const void* data, size_t size);
/**
 * @brief 刷新传输缓存。
 * @param userData XConsoleShellIo::userData 借用上下文。
 * @return 刷新成功返回 true；无刷新能力时回调可为 NULL。
 */
typedef bool (*XConsoleFlushFn)(void* userData);
/**
 * @brief 查询当前会话或命令是否已取消。
 * @param userData XConsoleShellIo::userData 借用上下文。
 * @return 已取消返回 true。
 */
typedef bool (*XConsoleCancelFn)(void* userData);
#if XCONSOLE_SHELL_LOG_ON
/**
 * @brief 输出日志观察回调。
 * @param userData 回调上下文；Shell 不释放。
 * @param session 当前会话；只在回调期间借用。
 * @param data 输出数据；只在回调期间借用。
 * @param size 数据字节数。
 */
typedef void (*XConsoleLogFn)(void* userData,
                              const struct XConsoleShellSession* session,
                              const void* data, size_t size);
#endif
#if XCONSOLE_SHELL_AUDIT_ON
/**
 * @brief 记录一条命令审计结果。
 * @param userData 回调上下文；Shell 不释放。
 * @param session 当前会话；只在回调期间借用。
 * @param command 命令描述；NULL 表示解析或查找阶段失败。
 * @param result 命令处理结果码。
 */
typedef void (*XConsoleAuditFn)(void* userData,
                                const struct XConsoleShellSession* session,
                                const struct XConsoleCommand* command,
                                int result);
#endif

/** @brief Shell 传输回调集合；所有函数指针和 userData 均为借用。 */
typedef struct XConsoleShellIo {
    XConsoleReadFn read;       /**< 输入回调；可为 NULL。 */
    XConsoleWriteFn write;     /**< 输出回调；无输出能力时 processLine 返回 IoError。 */
    XConsoleFlushFn flush;     /**< 刷新回调；可为 NULL。 */
    XConsoleCancelFn cancelled;/**< 取消回调；可为 NULL。 */
#if XCONSOLE_SHELL_LOG_ON
    XConsoleLogFn log;         /**< 输出观察回调；可为 NULL，不参与写入成功判定。 */
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    XConsoleAuditFn audit;     /**< 命令结果审计回调；可为 NULL。 */
#endif
    void* userData;            /**< 回调上下文；Shell 不释放。 */
} XConsoleShellIo;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON */
#endif /* XCONSOLE_SHELL_IO_H */
