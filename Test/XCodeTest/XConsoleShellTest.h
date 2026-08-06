/**
 * @file XConsoleShellTest.h
 * @brief XConsoleShell 配置开启时的核心和文件命令测试入口。
 */

#ifndef XCONSOLE_SHELL_TEST_H
#define XCONSOLE_SHELL_TEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 运行 Shell 分词、命令注册、短写输出、会话目录和文件命令测试。 */
bool XConsoleShellTest_runAll(void);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_TEST_H */
