/**
 * @file XTestCommand.c
 * @brief 将测试入口转换为 XConsoleShell 的 Test 命令。
 * @details
 * 这里仅调度已有的公开测试入口；旧的 XMenuTest 仍保留给历史代码链接，
 * 但不再由 main 默认启动。命令参数和输出均使用 Shell 公共 API。
 */

#include "XTestCommand.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON

#include "XConsoleShell.h"
#include "XConsoleShellTest.h"
#include "XConsoleShellBackendTest.h"
#include "XProcessTest.h"
#include "XMenuTest.h"
#include "XPrintf.h"
#include <string.h>

static bool xtest_write(XConsoleShell* shell, const char* text)
{
    return shell && text && XConsoleShell_writeUtf8(shell, text);
}

static int xtest_run_named(XConsoleShell* shell, const char* name)
{
    bool result;
    if (!name) return XConsoleResult_InvalidArgument;
    if (strcmp(name, "xprocess") == 0) {
        result = XProcessTest_runAll();
    } else if (strcmp(name, "xconsole-shell") == 0) {
        result = XConsoleShellTest_runAll();
    } else if (strcmp(name, "xconsole-shell-backend") == 0) {
        result = XConsoleShellBackendTest_runAll();
    } else {
        return XConsoleResult_UnknownCommand;
    }
    if (!xtest_write(shell, result ? "Test PASS\n" : "Test FAIL\n"))
        return XConsoleResult_IoError;
    return result ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xtest_run_all(XConsoleShell* shell)
{
    bool process = XProcessTest_runAll();
    bool shellTest = XConsoleShellTest_runAll();
    bool backend = XConsoleShellBackendTest_runAll();
    bool result = process && shellTest && backend;
    if (!xtest_write(shell, result ? "Test all PASS\n" : "Test all FAIL\n"))
        return XConsoleResult_IoError;
    return result ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xtest_run_menu(void)
{
    return XMenuTest_run() == 0 ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xtest_execute(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    (void)session;
    (void)userData;
    if (!shell || argc < 0 || (argc && !argv)) return XConsoleResult_InvalidArgument;
    if (argc == 0) return xtest_run_menu();
    if (argc == 1 && strcmp(argv[0], "list") == 0) {
        return xtest_write(shell,
                           "Test commands:\n"
                           "  Test xprocess\n"
                           "  Test xconsole-shell\n"
                           "  Test xconsole-shell-backend\n"
                           "  Test all\n")
                   ? XConsoleResult_Ok : XConsoleResult_IoError;
    }
    if (argc != 1) return XConsoleResult_InvalidArgument;
    if (strcmp(argv[0], "all") == 0) return xtest_run_all(shell);
    return xtest_run_named(shell, argv[0]);
}

const XConsoleCommand XTestCommand = {
    "Test", "test", "运行测试命令", "Test [list|all|name]", 0, 1,
    XConsoleCommandFlag_AllowUnauthenticated, xtest_execute, NULL, 0, NULL
};

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
