/**
 * @file XTestCommand.c
 * @brief 将测试入口转换为 XConsoleShell 的 Test 命令。
 * @details
 * 这里仅调度已有的公开测试入口；旧的 XTestMenuTest 仍保留给历史代码链接，
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
#include "XLinkTest.h"
#include "XLinkDemo.h"
#include "XNtpTest.h"
#include "XDdnsTest.h"
#include "XSshTelnetClientTest.h"
#include "XProcessTest.h"
#include "XTestMenuTest.h"
#include "XPrintf.h"
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
#include "XMemory.h"
#endif
#include <string.h>

static bool xtest_write(XConsoleShell* shell, const char* text)
{
    return shell && text && XConsoleShell_writeUtf8(shell, text);
}

/*
 * 运行测试前暂停 Shell 的异步输入轮询，跑完再恢复。
 *
 * 原因：Shell 的异步输入跑在独立工作线程上，其事件循环与测试线程
 * 共享**全局**的 IOCP 完成端口（XAbstractNetIoRing_global）。测试期间
 * 工作线程会抢走属于测试用例套接字的网络完成包（如 XLink 回环用例的
 * accept/connect 完成），导致用例等不到握手完成而误判失败（实测
 * `Test xlink` 交互态 10 项失败，同一用例走 `--test xlink` 直跑全过）。
 * 菜单路径（xtest_run_menu）本就做了同样的暂停/恢复，命名路径与全量
 * 路径需同口径。
 */
#if XCONSOLE_SHELL_ASYNC_ON
static bool xtest_async_pause(XConsoleShell* shell)
{
    return !shell || !XConsoleShell_isAsyncRunning(shell) ||
           XConsoleShell_stopAsync(shell, UINT32_MAX);
}

static void xtest_async_resume(XConsoleShell* shell, bool restart)
{
    if (restart && shell)
        (void)XConsoleShell_startAsync(shell);
}
#endif

static int xtest_run_named(XConsoleShell* shell, const char* name)
{
    bool result;
    bool restartAsync = false;
#if XCONSOLE_SHELL_ASYNC_ON
    restartAsync = shell && XConsoleShell_isAsyncRunning(shell);
    if (!xtest_async_pause(shell))
        return XConsoleResult_Failed;
#endif
    if (!name) {
#if XCONSOLE_SHELL_ASYNC_ON
        xtest_async_resume(shell, restartAsync);
#endif
        return XConsoleResult_InvalidArgument;
    }
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
    } else if (strcmp(name, "modbus-server-fc") == 0) {
        /* [补充 2026-10-15] Modbus 服务端 FC 全量覆盖（含远程 IO 掉线回归锁） */
        result = XModbusCoverageTest_run();
    } else if (strcmp(name, "xmqtt-tcp-interop") == 0) {
        result = XMqttTcpInteropTest_run();
#if XPROTOCOL_ON && XLINK_ON
    /* XLink 私有应用层协议（控制进程 <-> GUI 进程 消息互通）。 */
    } else if (strcmp(name, "xlink") == 0) {
        result = XLinkTest_run() == 0;
    /* 质量门禁：内存泄漏 / 性能基准 / 边界与对抗。 */
    } else if (strcmp(name, "xlink-quality") == 0) {
        result = XLinkQualityTest_run() == 0;
    /* 实战案例（本进程联调）：服务端角色 + 客户端角色跑真实 TCP 环回。 */
    } else if (strcmp(name, "xlink-demo") == 0) {
        result = XLinkDemo_run() == 0;
    /* 全功能闸门：功能 152 + 质量门禁 41 + 本进程案例联调。 */
    } else if (strcmp(name, "xlink-all") == 0) {
        result = (XLinkTest_run() == 0)
                 && (XLinkQualityTest_run() == 0)
                 && (XLinkDemo_run() == 0);
    /* 客户端包装（自动重连）：真实 TCP 环回全链路
       （原 XLinkClientTest.c 已并入 XLinkTest.c 分组十四，声明在 XLinkTest.h）。 */
#if XLINK_CLIENT_ON
    } else if (strcmp(name, "xlink-client") == 0) {
        result = XLinkClientTest_run() == 0;
#endif
#endif
#if XPROTOCOL_ON && XNTP_ON
    /* XNtp SNTP 客户端（应用内校时）。 */
    } else if (strcmp(name, "ntp") == 0) {
        result = XNtpTest_run() == 0;
#endif
#if XPROTOCOL_ON && XDDNS_ON
    /* XDdns 动态域名解析客户端（厂商抽象 + 阿里云）。 */
    } else if (strcmp(name, "ddns") == 0) {
        result = XDdnsTest_run() == 0;
#endif
    } else {
#if XCONSOLE_SHELL_ASYNC_ON
        xtest_async_resume(shell, restartAsync);
#endif
        return XConsoleResult_UnknownCommand;
    }
#if XCONSOLE_SHELL_ASYNC_ON
    xtest_async_resume(shell, restartAsync);
#endif
    if (!xtest_write(shell, result ? "Test PASS\n" : "Test FAIL\n"))
        return XConsoleResult_IoError;
    return result ? XConsoleResult_Ok : XConsoleResult_Failed;
}
static int xtest_run_all(XConsoleShell* shell)
{
    bool process;
    bool shellTest;
    bool backend;
    bool protocolClients;
    bool cryptographic;
    bool ssl;
    bool mqtt;
    bool result;
    bool restartAsync = false;
#if XCONSOLE_SHELL_ASYNC_ON
    restartAsync = shell && XConsoleShell_isAsyncRunning(shell);
    if (!xtest_async_pause(shell))
        return XConsoleResult_Failed;
#endif
    process = XProcessTest_runAll();
    shellTest = XConsoleShellTest_runAll();
    backend = XConsoleShellBackendTest_runAll();
    protocolClients = XSshTelnetClientTest_runAll();
    cryptographic = XCryptographicPrimitiveTest_runAll();
    ssl = XSslTest_runAll();
    mqtt = XMqttTest_runAll();
    result = process && shellTest && backend && protocolClients && cryptographic && ssl && mqtt;
#if XPROTOCOL_ON && XLINK_ON
    /* XLink 私有协议回归并入全量闸门：功能 + 质量 + 本进程案例联调。 */
    result = result && (XLinkTest_run() == 0);
    result = result && (XLinkQualityTest_run() == 0);
    result = result && (XLinkDemo_run() == 0);
#endif
#if XCONSOLE_SHELL_ASYNC_ON
    xtest_async_resume(shell, restartAsync);
#endif
    if (!xtest_write(shell, result ? "Test all PASS\n" : "Test all FAIL\n"))
        return XConsoleResult_IoError;
    return result ? XConsoleResult_Ok : XConsoleResult_Failed;
}

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
typedef struct XTestRemoteMenuContext {
    XTestMenuTestRemoteSession* menu;
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
    XTestMenuTestRemoteSession_destroy(context->menu);
    XFree_System(context);
}

static XConsoleResult xtest_remote_menu_line(
    XConsoleShell* shell, XConsoleShellSession* session,
    const char* line, size_t length, void* userData)
{
    XTestRemoteMenuContext* context = (XTestRemoteMenuContext*)userData;
    XTestMenuTestRemoteResult result;
    if (!shell || !session || !context || !context->menu)
        return XConsoleResult_InvalidArgument;
    result = XTestMenuTestRemoteSession_processLine(context->menu, line, length);
    if (result == XTestMenuTestRemoteResult_Active)
        return XConsoleResult_MoreOutput;
    xtest_remote_menu_destroy(shell, session, context);
    return result == XTestMenuTestRemoteResult_Finished ?
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
    result = XTestMenuTest_run() == 0 ? XConsoleResult_Ok : XConsoleResult_Failed;
    if (restartAsync && !XConsoleShell_startAsync(shell))
        return XConsoleResult_Failed;
    return result;
#else
    (void)shell;
    return XTestMenuTest_run() == 0 ? XConsoleResult_Ok : XConsoleResult_Failed;
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
            context->menu = XTestMenuTestRemoteSession_create();
            if (!context->menu ||
                !XConsoleShell_setInputLineHandler(
                    shell, session, xtest_remote_menu_line, context)) {
                xtest_remote_menu_destroy(shell, session, context);
                return XConsoleResult_Failed;
            }
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
            session->suppressPrompt = true;
#endif
            XTestMenuTestRemoteSession_show(context->menu);
            return XConsoleResult_MoreOutput;
        }
#endif
        return xtest_run_menu(shell, session);
    }
    if (argc == 1 && strcmp(argv[0], "list") == 0) {
        return xtest_write(shell,
                           "Test commands:\n"
                           "  Test modbus-server-fc\n"
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
