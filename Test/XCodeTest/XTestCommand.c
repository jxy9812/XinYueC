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
#include "XCryptographicPrimitiveTest.h"
#include "XSslTest.h"
#include "XProtocolTest.h"
#include "XSshTelnetClientTest.h"
#include "XProcessTest.h"
#include "XMenuTest.h"
#include "XPrintf.h"
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
#include "XMemory.h"
#endif
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
    } else if (strcmp(name, "ssh-telnet-client") == 0) {
        result = XSshTelnetClientTest_runAll();
    } else if (strcmp(name, "xcryptographic") == 0) {
        result = XCryptographicPrimitiveTest_runAll();
    } else if (strcmp(name, "xssl") == 0) {
        result = XSslTest_runAll();
    } else if (strcmp(name, "xmqtt") == 0) {
        result = XMqttTest_runAll();
    } else if (strcmp(name, "xmqtt-tcp-api") == 0) {
        result = XMqttTcpServerApiUnitTest_run();
    } else if (strcmp(name, "xmqtt-unit") == 0) {
        result = XMqttServerUnitTest_run();
    } else if (strcmp(name, "xmqtt-tcp-server") == 0) {
        result = XMqttTcpServerProcess_run();
    } else if (strcmp(name, "xmqtt-tcp-client") == 0) {
        result = XMqttTcpClientProcess_run();
    } else if (strcmp(name, "xmqtt-tcp-interop") == 0) {
        result = XMqttTcpInteropTest_run();
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
    bool protocolClients = XSshTelnetClientTest_runAll();
    bool cryptographic = XCryptographicPrimitiveTest_runAll();
    bool ssl = XSslTest_runAll();
    bool mqtt = XMqttTest_runAll();
    bool result = process && shellTest && backend && protocolClients && cryptographic && ssl && mqtt;
    if (!xtest_write(shell, result ? "Test all PASS\n" : "Test all FAIL\n"))
        return XConsoleResult_IoError;
    return result ? XConsoleResult_Ok : XConsoleResult_Failed;
}

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
typedef struct XTestRemoteMenuContext {
    XMenuTestRemoteSession* menu;
} XTestRemoteMenuContext;

static void xtest_remote_menu_destroy(XConsoleShell* shell,
                                      XConsoleShellSession* session,
                                      XTestRemoteMenuContext* context)
{
    if (!context) return;
    if (shell && session)
        (void)XConsoleShell_setInputLineHandler(shell, session, NULL, NULL);
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
    if (session) session->suppressPrompt = false;
#endif
    XMenuTestRemoteSession_destroy(context->menu);
    XFree_System(context);
}

static XConsoleResult xtest_remote_menu_line(
    XConsoleShell* shell, XConsoleShellSession* session,
    const char* line, size_t length, void* userData)
{
    XTestRemoteMenuContext* context = (XTestRemoteMenuContext*)userData;
    XMenuTestRemoteResult result;
    if (!shell || !session || !context || !context->menu)
        return XConsoleResult_InvalidArgument;
    result = XMenuTestRemoteSession_processLine(context->menu, line, length);
    if (result == XMenuTestRemoteResult_Active)
        return XConsoleResult_MoreOutput;
    xtest_remote_menu_destroy(shell, session, context);
    return result == XMenuTestRemoteResult_Finished ?
               XConsoleResult_Ok : XConsoleResult_Failed;
}
#endif

static int xtest_run_menu(XConsoleShell* shell, XConsoleShellSession* session)
{
    (void)session;
#if XCONSOLE_SHELL_ASYNC_ON
    bool restartAsync = shell && XConsoleShell_isAsyncRunning(shell);
    int result;

    /*
     * 菜单会暂时接管标准输入；如果 Shell 的异步轮询仍在运行，菜单项目
     * 中调用 processEvents() 时可能由 Shell 抢先读走 q 或回车。暂停异步
     * 输入还会关闭 Shell 自己的输入描述符，确保菜单期间只有一个读者。
     */
    if (restartAsync && !XConsoleShell_stopAsync(shell, UINT32_MAX))
        return XConsoleResult_Failed;
    result = XMenuTest_run() == 0 ? XConsoleResult_Ok : XConsoleResult_Failed;
    if (restartAsync && !XConsoleShell_startAsync(shell))
        return XConsoleResult_Failed;
    return result;
#else
    (void)shell;
    return XMenuTest_run() == 0 ? XConsoleResult_Ok : XConsoleResult_Failed;
#endif
}

static int xtest_execute(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
#if !XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
    (void)session;
#endif
    (void)userData;
    if (!shell || argc < 0 || (argc && !argv)) return XConsoleResult_InvalidArgument;
    if (argc == 0) {
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
        if (session && session->id != 1u) {
            XTestRemoteMenuContext* context =
                (XTestRemoteMenuContext*)XCalloc_System(1, sizeof(*context));
            if (!context) return XConsoleResult_ResourceLimit;
            context->menu = XMenuTestRemoteSession_create();
            if (!context->menu ||
                !XConsoleShell_setInputLineHandler(
                    shell, session, xtest_remote_menu_line, context)) {
                xtest_remote_menu_destroy(shell, session, context);
                return XConsoleResult_Failed;
            }
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
            session->suppressPrompt = true;
#endif
            XMenuTestRemoteSession_show(context->menu);
            return XConsoleResult_MoreOutput;
        }
#endif
        return xtest_run_menu(shell, session);
    }
    if (argc == 1 && strcmp(argv[0], "list") == 0) {
        return xtest_write(shell,
                           "Test commands:\n"
                           "  Test xprocess\n"
                           "  Test xconsole-shell\n"
                           "  Test xconsole-shell-backend\n"
                           "  Test ssh-telnet-client\n"
                           "  Test xcryptographic\n"
                           "  Test xssl\n"
                           "  Test xmqtt\n"
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
