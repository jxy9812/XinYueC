/**
 * @file XConsoleShell_Protected.h
 * @brief XConsoleShell 内部子模块契约。
 * @details
 * 仅 XConsoleShell 实现文件和可选文件/进程执行子模块包含。声明不暴露
 * 平台句柄，文件操作由 XConsoleShellFileSystem 通过 XFileSystem 公共 API
 * 完成，外部进程由 XConsoleShellExecutor 通过 XProcess 公共 API 完成。
 */

#ifndef XCONSOLE_SHELL_PROTECTED_H
#define XCONSOLE_SHELL_PROTECTED_H

#include "XConsoleShell.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 执行 fs 命令树。
 * @param shell 当前 Shell；只在调用期间借用。
 * @param session 当前会话；只在调用期间借用。
 * @param argc 参数数量。
 * @param argv UTF-8 参数数组；不跨调用保存。
 * @param userData 静态命令私有数据；Shell 不释放。
 * @return XConsoleResult 结果码。
 */
int XConsoleShellFileSystem_execute(XConsoleShell* shell,
                                    XConsoleShellSession* session,
                                    int argc, const char* const* argv,
                                    void* userData);
/**
 * @brief 执行可选 exec 命令。
 * @param shell 当前 Shell；只在调用期间借用。
 * @param session 当前会话；只在调用期间借用。
 * @param argc 参数数量。
 * @param argv UTF-8 参数数组；不跨调用保存。
 * @param userData 静态命令私有数据；Shell 不释放。
 * @return XConsoleResult 结果码。
 */
int XConsoleShellExecutor_execute(XConsoleShell* shell,
                                  XConsoleShellSession* session,
                                  int argc, const char* const* argv,
                                  void* userData);
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
/** @brief 驱动 Shell 拥有的异步进程任务。 @param shell Shell 对象。 @param timeoutMsecs 轮询等待毫秒数。 @return 回收任务数量。 */
size_t XConsoleShellExecutor_pollAsync(XConsoleShell* shell, int timeoutMsecs);
/** @brief 终止并释放所有异步进程任务。 @param shell Shell 对象。 */
void XConsoleShellExecutor_abortAsync(XConsoleShell* shell);
#endif

/**
 * @brief 将一行复制并分词到 Shell 固定缓冲。
 * @param shell Shell 对象；不能为空。
 * @param line UTF-8 输入行；只在调用期间借用。
 * @param length 行字节数。
 * @return 分词结果码。
 */
XConsoleResult XConsoleShellParser_tokenize(XConsoleShell* shell,
                                            const char* line, size_t length);
/**
 * @brief 使用调用方提供的固定 scratch 缓冲完成分词。
 * @param line UTF-8 输入行；只在调用期间借用。
 * @param length 行字节数。
 * @param buffer 分词工作缓冲。
 * @param bufferSize 工作缓冲容量。
 * @param arguments 输出参数指针数组。
 * @param maxArguments 参数数组容量。
 * @param argumentCount 输出参数数量。
 * @return 分词结果码。
 */
XConsoleResult XConsoleShellParser_tokenizeBuffer(const char* line, size_t length,
                                                  char* buffer, size_t bufferSize,
                                                  const char** arguments,
                                                  size_t maxArguments,
                                                  size_t* argumentCount);
/**
 * @brief 在命令表和子命令树中查找 token 序列对应的叶命令。
 * @param shell Shell 对象；不能为空。
 * @param tokens UTF-8 token 数组；只在调用期间借用。
 * @param tokenCount token 数量。
 * @param consumed 输出已消费 token 数量，可为 NULL。
 * @return 命令借用指针；未找到返回 NULL。
 */
const XConsoleCommand* XConsoleShellParser_find(const XConsoleShell* shell,
                                                const char* const* tokens,
                                                size_t tokenCount,
                                                size_t* consumed);

/** @brief 文件系统根命令静态描述。 */
extern const XConsoleCommand XConsoleShellFileSystem_command;
/** @brief 注册文件系统根级命令。 @param shell Shell 对象。 @return 成功返回 true。 */
bool XConsoleShellFileSystem_registerRootCommands(XConsoleShell* shell);
#if XCONSOLE_SHELL_FS_LS_ON
/** @brief 文件系统根级 ls 命令静态描述；处理函数复用 fs ls。 */
extern const XConsoleCommand XConsoleShellFileSystem_ls_command;
#endif
/** @brief 外部进程根命令静态描述；关闭时由存根提供。 */
extern const XConsoleCommand XConsoleShellExecutor_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
#endif /* XCONSOLE_SHELL_PROTECTED_H */
