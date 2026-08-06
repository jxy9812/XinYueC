/**
 * @file XConsoleShellBackendTest.h
 * @brief XConsoleShell TCP 服务端适配器的端到端回归测试入口。
 * @details
 * 测试仅在 Shell、多会话和 XTcpServer 后端同时开启时执行；其余配置下仍
 * 提供稳定的空测试入口，便于测试菜单和不同裁剪配置统一链接。
 */

#ifndef XCONSOLE_SHELL_BACKEND_TEST_H
#define XCONSOLE_SHELL_BACKEND_TEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 运行 XTcpServer Shell 适配器的 loopback 输入、输出和关闭测试。 */
bool XConsoleShellBackendTest_runAll(void);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_BACKEND_TEST_H */
