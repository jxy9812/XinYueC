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
#include "XConsoleShellSystem.h"
#if XCONSOLE_SHELL_LOGIN_ON
#include "XConsoleShellLogin.h"
#endif
#if XCONSOLE_SHELL_EDITOR_ON
#include "XConsoleShellVi.h"
#endif

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
/**
 * @brief 驱动 Shell 拥有的异步进程任务。
 * @param shell Shell 对象；只在调用期间借用，不能为 NULL。
 * @param timeoutMsecs 单次轮询允许等待的毫秒数；不得为负数。
 * @return 本次已回收的异步任务数量。
 */
size_t XConsoleShellExecutor_pollAsync(XConsoleShell* shell, int timeoutMsecs);
/**
 * @brief 终止并释放所有异步进程任务。
 * @param shell Shell 对象；可为 NULL。
 * @return 无。
 */
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
/** @brief 网络根命令静态描述；处理函数只调用 XNetwork 公共 API。 */
extern const XConsoleCommand XConsoleShellNetwork_command;
/** @brief 日期时间根命令静态描述；处理函数只调用 XDateTime 公共 API。 */
extern const XConsoleCommand XConsoleShellDateTime_command;
/** @brief 内存查询根命令静态描述；处理函数只调用 XMultiPool 公共 API。 */
extern const XConsoleCommand XConsoleShellMemory_command;
/** @brief 基础信息根命令静态描述；处理函数只读取编译期配置。 */
extern const XConsoleCommand XConsoleShellInfo_command;
/** @brief 运行时间根命令静态描述；处理函数只调用 XDateTime 公共 API。 */
extern const XConsoleCommand XConsoleShellUptime_command;
/** @brief 任务诊断根命令静态描述；任务数据由产品提供者回调提供。 */
extern const XConsoleCommand XConsoleShellTasks_command;
#if XCONSOLE_SHELL_TASKS_ON
/**
 * @brief 从 XThread 注册表提供任务快照。
 * @param userData 产品上下文；本函数只读借用，不释放。
 * @param shell 当前 Shell；调用期间借用。
 * @param session 当前会话；调用期间借用。
 * @param emit 输出任务信息的回调；不能为 NULL。
 * @param emitUserData 传给 emit 回调的上下文；不转移所有权。
 * @return 所有任务快照输出成功返回 XConsoleResult_Ok，否则返回错误码。
 * @note 不访问平台原生任务列表，任务信息由 XThread 注册表提供。
 */
XConsoleResult XConsoleShellTasks_platformProvider(
    void* userData, XConsoleShell* shell, XConsoleShellSession* session,
    XConsoleShellTaskEmitFn emit, void* emitUserData);
#endif
#if XCONSOLE_SHELL_GPIO_ON
/**
 * @brief 关闭并释放 Shell 持有的全部 GPIO 槽位。
 * @param shell Shell 对象；可为 NULL。
 */
void XConsoleShellGpio_deinit(XConsoleShell* shell);
#endif
#if XCONSOLE_SHELL_I2C_ON
/** @brief I2C 根命令静态描述。 */
extern const XConsoleCommand XConsoleShellI2c_command;
#endif
#if XCONSOLE_SHELL_I2C_ON
/**
 * @brief 关闭并释放 Shell 持有的全部 I2C 槽位。
 * @param shell Shell 对象；可为 NULL。
 */
void XConsoleShellI2c_deinit(XConsoleShell* shell);
#endif
#if XCONSOLE_SHELL_SPI_ON
/** @brief SPI 根命令静态描述。 */
extern const XConsoleCommand XConsoleShellSpi_command;
#endif
#if XCONSOLE_SHELL_SPI_ON
/**
 * @brief 关闭并释放 Shell 持有的全部 SPI 槽位。
 * @param shell Shell 对象；可为 NULL。
 */
void XConsoleShellSpi_deinit(XConsoleShell* shell);
#endif
#if XCONSOLE_SHELL_CAN_ON
/**
 * @brief 关闭并释放 Shell 持有的全部 CAN 槽位。
 * @param shell Shell 对象；可为 NULL。
 */
void XConsoleShellCan_deinit(XConsoleShell* shell);
#endif
#if XCONSOLE_SHELL_ADC_ON
/** @brief ADC 根命令静态描述。 */
extern const XConsoleCommand XConsoleShellAdc_command;
/**
 * @brief 关闭并释放 Shell 持有的全部 ADC 槽位。
 * @param shell Shell 对象；可为 NULL。
 */
void XConsoleShellAdc_deinit(XConsoleShell* shell);
#endif
#if XCONSOLE_SHELL_PWM_ON
/** @brief PWM 根命令静态描述。 */
extern const XConsoleCommand XConsoleShellPwm_command;
/**
 * @brief 停止、关闭并释放 Shell 持有的全部 PWM 槽位。
 * @param shell Shell 对象；可为 NULL。
 */
void XConsoleShellPwm_deinit(XConsoleShell* shell);
#endif
/**
 * @brief 注册文件系统根级命令。
 * @param shell Shell 对象；调用期间借用，不能为 NULL。
 * @return 注册成功返回 true；参数非法、命令重复或容量不足返回 false。
 */
bool XConsoleShellFileSystem_registerRootCommands(XConsoleShell* shell);
#if XCONSOLE_SHELL_FS_LS_ON
/** @brief 文件系统根级 ls 命令静态描述；处理函数复用 fs ls。 */
extern const XConsoleCommand XConsoleShellFileSystem_ls_command;
#endif
/** @brief 外部进程根命令静态描述；关闭时由存根提供。 */
extern const XConsoleCommand XConsoleShellExecutor_command;
#if XCONSOLE_SHELL_LOGIN_ON
/** @brief 读取、验证和写入本地用户 JSON 配置的内部命令模块。 */
bool XConsoleShellLogin_registerCommands(XConsoleShell* shell);
/**
 * @brief 判断会话是否等待登录模块的下一行敏感输入。
 * @param session 当前会话；可为 NULL。
 * @return 等待输入返回 true，否则返回 false。
 */
bool XConsoleShellLogin_isInputPending(const XConsoleShellSession* session);
/**
 * @brief 取消会话等待中的敏感账户输入并清零临时缓冲。
 * @param session 当前会话；可为 NULL。
 */
void XConsoleShellLogin_cancelInput(XConsoleShell* shell,
                                    XConsoleShellSession* session);
/**
 * @brief 提交登录模块等待的敏感输入行。
 * @param shell 当前 Shell；不能为空。
 * @param session 当前会话；不能为空。
 * @param line UTF-8 输入行；只在调用期间借用。
 * @param length 输入行字节数。
 * @return 处理结果码。
 */
XConsoleResult XConsoleShellLogin_submitInput(XConsoleShell* shell,
                                              XConsoleShellSession* session,
                                              const char* line, size_t length);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
#endif /* XCONSOLE_SHELL_PROTECTED_H */
