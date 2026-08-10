/**
 * @file XConsoleShellVi.h
 * @brief XConsoleShell 的 vi/vim 风格行编辑命令。
 * @details
 * 编辑器通过 Shell 会话状态机复用登录风格的“下一行输入”路由：`vi <path>`
 * 打开文件后进入命令模式，后续输入行由 Shell 路由到编辑器，直到用户执行
 * 保存/退出命令。编辑器不直接读取传输字节，因此可同时用于交互终端、PTY
 * 和测试传输。文件内容只通过 XFileSystem 公共 API 读取和写回。
 */

#ifndef XCONSOLE_SHELL_VI_H
#define XCONSOLE_SHELL_VI_H

#include "XConsoleShellConfig.h"
#include "XConsoleShellCommand.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_EDITOR_ON

#ifdef __cplusplus
extern "C" {
#endif

struct XConsoleShell;
struct XConsoleShellSession;

/**
 * @brief 判断会话当前是否处于 vi/vim 编辑等待状态。
 * @param session 当前会话；可为 NULL。
 * @return 处于编辑状态返回 true，否则返回 false。
 */
bool XConsoleShellVi_isActive(const struct XConsoleShellSession* session);

/**
 * @brief 取消当前会话的 vi/vim 编辑状态并恢复 Shell 提示符。
 * @param shell 当前 Shell；不能为空。
 * @param session 当前会话；可为 NULL。
 * @return 无。
 */
void XConsoleShellVi_cancel(struct XConsoleShell* shell,
                            struct XConsoleShellSession* session);

/**
 * @brief 向 vi/vim 插入模式投递一个原始字节。
 *
 * 插入模式下 Shell 的 feedByte 会直接把每个输入字节交给本函数，
 * 实现逐字符插入、回车换行、退格删除和 ESC 返回命令模式，
 * 行为对齐 Linux vim 的插入模式。
 *
 * @param shell 当前 Shell；不能为空。
 * @param session 当前会话；不能为空且必须处于插入模式。
 * @param byte 输入字节。
 * @return 处理结果码。
 */
XConsoleResult XConsoleShellVi_feedByte(struct XConsoleShell* shell,
                                        struct XConsoleShellSession* session,
                                        uint8_t byte);

/**
 * @brief 提交 vi/vim 编辑器等待的一行输入。
 * @param shell 当前 Shell；不能为空。
 * @param session 当前会话；不能为空。
 * @param line UTF-8 输入行；只在调用期间借用。
 * @param length 输入行字节数。
 * @return 处理结果码；保存并退出时返回 Ok，否则通常返回 MoreOutput。
 */
XConsoleResult XConsoleShellVi_submitLine(struct XConsoleShell* shell,
                                          struct XConsoleShellSession* session,
                                          const char* line, size_t length);

/** @brief `vi` 命令静态描述。 */
extern const XConsoleCommand XConsoleShellVi_command;
/** @brief `vim` 命令静态描述（与 vi 共用同一处理函数）。 */
extern const XConsoleCommand XConsoleShellVim_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_EDITOR_ON */
#endif /* XCONSOLE_SHELL_VI_H */
