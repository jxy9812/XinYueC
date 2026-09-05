/**
 * @file XTestCommand.h
 * @brief 默认控制台中的 Test 测试命令。
 * @details
 * 该命令替代旧的交互式 XTestMenuTest 入口，测试通过 Shell 命令参数选择，
 * 不保存会话参数，也不取得 Shell 或会话对象的所有权。
 */

#ifndef XTESTCOMMAND_H
#define XTESTCOMMAND_H

#include "CXinYueConfig.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON
#include "XConsoleShellCommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Test 根命令；支持 Test list、Test all 和指定测试名称。 */
extern const XConsoleCommand XTestCommand;

#ifdef __cplusplus
}
#endif
#endif

#endif /* XTESTCOMMAND_H */
