/**
 * @file XConsoleShellBackendTest.c
 * @brief XConsoleShell XTcpServer TCP/SSH 后端的库级 loopback 回归测试。
 * @details
 * 测试通过 XinYueC 的 XTcpServer、XTcpSocket、XCoreApplication 和
 * XConsoleShell 公共 API 建立本机回环连接，不直接依赖平台 socket、线程或
 * 文件接口。服务端适配器负责接收连接和泵入输入，客户端套接字负责验证输出
 * 或 SSH 版本 banner。
 */

#include "XConsoleShellBackendTest.h"
#include "CXinYueConfig.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_MULTI_SESSION_ON && \
    XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON

#include "XConsoleShell.h"
#include "XConsoleShell_XTcpServer.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XThread.h"
#include "XTcpServer.h"
#include "XTcpSocket.h"
#include <string.h>

#define XCS_BACKEND_CHECK(condition, text) \
    do { \
        if (!(condition)) { \
            XPrintf("[FAIL] XConsoleShell 后端: %s (第%d行)\n", \
                    text, __LINE__); \
            goto cleanup; \
        } \
    } while (0)

static bool xcs_backend_wait_connected(XTcpSocket* socket, int timeoutMsecs)
{
    uint64_t start;
    if (!socket) return false;
    start = XDateTime_currentMSecsSinceEpoch();
    while (XTcpSocket_state((XAbstractSocket*)socket) != XAbstractSocket_ConnectedState) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XDateTime_currentMSecsSinceEpoch() - start >= (uint64_t)timeoutMsecs)
            return false;
        XThread_msleep(1);
    }
    return true;
}

static bool xcs_backend_read_contains(XTcpSocket* socket, const char* expected,
                                      int timeoutMsecs)
{
    char output[512];
    size_t length = 0;
    uint64_t start;
    if (!socket || !expected) return false;
    memset(output, 0, sizeof(output));
    start = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - start < (uint64_t)timeoutMsecs) {
        int64_t available;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        available = XTcpSocket_bytesAvailable_base((const XIODevice*)socket);
        if (available > 0) {
            size_t capacity = sizeof(output) - length - 1u;
            int64_t count = XTcpSocket_read_1((XIODevice*)socket, output + length,
                                              available < (int64_t)capacity
                                                  ? available : (int64_t)capacity);
            if (count > 0) {
                length += (size_t)count;
                output[length] = '\0';
                if (strstr(output, expected)) return true;
            }
        }
        XThread_msleep(1);
    }
    XPrintf("[FAIL] 后端未收到期望输出，实际输出: %s\n", output);
    return false;
}

#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
static bool xcs_backend_test_ssh_banner(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* client = NULL;
    XConsoleShell* shell = NULL;
    XConsoleShellXTcpServerAdapter adapter;
    bool ok = false;

    memset(&adapter, 0, sizeof(adapter));
    server = XTcpServer_create();
    shell = XConsoleShell_create(NULL);
    XCS_BACKEND_CHECK(server != NULL && shell != NULL,
                      "创建 SSH server 或 Shell 失败");
    XCS_BACKEND_CHECK(XTcpServer_listen(server, NULL, 0),
                      "SSH 回环监听失败");

    XConsoleShellXTcpServerAdapter_init(&adapter, shell, server);
    client = XTcpSocket_create();
    XCS_BACKEND_CHECK(client != NULL, "创建 SSH 客户端套接字失败");
    XTcpSocket_connectToHost_base((XAbstractSocket*)client, "127.0.0.1",
                                   XTcpServer_serverPort(server),
                                   XIODevice_ReadWrite,
                                   XAbstractSocket_AnyIPProtocol);
    XCS_BACKEND_CHECK(xcs_backend_wait_connected(client, 3000),
                      "SSH 客户端连接超时");

    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XConsoleShell_sessionCount(shell) < 2 &&
               XDateTime_currentMSecsSinceEpoch() - start < 3000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            (void)XConsoleShellXTcpServerAdapter_acceptPending(&adapter);
            XThread_msleep(1);
        }
    }
    XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 2 &&
                      adapter.bindings[0].ssh != NULL,
                      "SSH acceptPending 未创建适配器和 Shell 会话");
    XCS_BACKEND_CHECK(xcs_backend_read_contains(client,
                                                 "SSH-2.0-XinYueC_1.0", 3000),
                      "客户端未收到 SSH 版本 banner");
    ok = true;

cleanup:
    XConsoleShellXTcpServerAdapter_closeAll(&adapter);
    if (client) {
        XTcpSocket_abort((XAbstractSocket*)client);
        XTcpSocket_deleteLater((XObject*)client);
    }
    if (server) {
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
    }
    if (shell) XConsoleShell_delete_base(shell);
    return ok;
}

static bool xcs_backend_test_ssh_multi_connect_close(void)
{
    enum {
        XCS_BACKEND_MULTI_CONN = (int)(XCONSOLE_SHELL_MAX_SESSIONS - 1u),
        XCS_BACKEND_MULTI_ROUNDS = 20
    };
    XTcpServer* server = NULL;
    XConsoleShell* shell = NULL;
    XConsoleShellXTcpServerAdapter adapter;
    XTcpSocket* clients[XCS_BACKEND_MULTI_CONN];
    size_t i;
    unsigned round;
    bool ok = false;

    memset(clients, 0, sizeof(clients));
    memset(&adapter, 0, sizeof(adapter));
    server = XTcpServer_create();
    shell = XConsoleShell_create(NULL);
    XCS_BACKEND_CHECK(server != NULL && shell != NULL,
                      "创建多连接 SSH server 或 Shell 失败");
    XCS_BACKEND_CHECK(XTcpServer_listen(server, NULL, 0),
                      "SSH 多连接回环监听失败");
    XConsoleShellXTcpServerAdapter_init(&adapter, shell, server);

    /* 同一进程内连续多轮：同时建立多个连接、确认会话、再分别走
     * “客户端断开”和“服务端 closeAll”两条释放路径。每轮都检查
     * 会话数与绑定槽已恢复到基线，累计泄漏也会在 sanitizer 退出时暴露。 */
    for (round = 0; round < XCS_BACKEND_MULTI_ROUNDS; ++round) {
        /* 阶段 A：客户端主动断开。 */
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            clients[i] = XTcpSocket_create();
            XCS_BACKEND_CHECK(clients[i] != NULL, "创建多连接 SSH 客户端套接字失败");
            XTcpSocket_connectToHost_base((XAbstractSocket*)clients[i], "127.0.0.1",
                                           XTcpServer_serverPort(server),
                                           XIODevice_ReadWrite,
                                           XAbstractSocket_AnyIPProtocol);
        }
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i)
            XCS_BACKEND_CHECK(xcs_backend_wait_connected(clients[i], 3000),
                              "多连接 SSH 客户端连接超时");
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (XConsoleShell_sessionCount(shell) < XCS_BACKEND_MULTI_CONN + 1u &&
                   XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                (void)XConsoleShellXTcpServerAdapter_acceptPending(&adapter);
                XThread_msleep(1);
            }
        }
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == XCS_BACKEND_MULTI_CONN + 1u,
                          "多连接未全部创建 SSH 会话");
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i)
            XCS_BACKEND_CHECK(xcs_backend_read_contains(clients[i],
                                                         "SSH-2.0-XinYueC_1.0", 3000),
                              "多连接客户端未收到 SSH 版本 banner");
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            XTcpSocket_abort((XAbstractSocket*)clients[i]);
            XTcpSocket_deleteLater((XObject*)clients[i]);
            clients[i] = NULL;
        }
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (XConsoleShell_sessionCount(shell) != 1u &&
                   XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                /* 测试环境没有外层事件循环，deleteLater 的延迟删除需要显式投递，
                 * 否则 socket 占用的 XFd 不会释放，多轮连接/关闭会耗尽表。 */
                XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
                (void)XConsoleShellXTcpServerAdapter_pump(&adapter, 4096);
                XThread_msleep(1);
            }
        }
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 1u,
                          "多连接断开后附加会话未全部释放");
        for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
            XCS_BACKEND_CHECK(adapter.bindings[i].session == NULL &&
                              adapter.bindings[i].socket == NULL &&
                              adapter.bindings[i].ssh == NULL,
                              "多连接断开后绑定槽未清空");
        }

        /* 阶段 B：服务端 closeAll。 */
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            clients[i] = XTcpSocket_create();
            XCS_BACKEND_CHECK(clients[i] != NULL, "创建 closeAll SSH 客户端套接字失败");
            XTcpSocket_connectToHost_base((XAbstractSocket*)clients[i], "127.0.0.1",
                                           XTcpServer_serverPort(server),
                                           XIODevice_ReadWrite,
                                           XAbstractSocket_AnyIPProtocol);
        }
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i)
            XCS_BACKEND_CHECK(xcs_backend_wait_connected(clients[i], 3000),
                              "closeAll 前 SSH 客户端连接超时");
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (XConsoleShell_sessionCount(shell) < XCS_BACKEND_MULTI_CONN + 1u &&
                   XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                (void)XConsoleShellXTcpServerAdapter_acceptPending(&adapter);
                XThread_msleep(1);
            }
        }
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == XCS_BACKEND_MULTI_CONN + 1u,
                          "closeAll 前未全部创建 SSH 会话");
        XConsoleShellXTcpServerAdapter_closeAll(&adapter);
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 1u,
                          "closeAll 后附加会话未全部释放");
        for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
            XCS_BACKEND_CHECK(adapter.bindings[i].session == NULL &&
                              adapter.bindings[i].socket == NULL &&
                              adapter.bindings[i].ssh == NULL,
                              "closeAll 后绑定槽未清空");
        }
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            XTcpSocket_abort((XAbstractSocket*)clients[i]);
            XTcpSocket_deleteLater((XObject*)clients[i]);
            clients[i] = NULL;
        }
    }
    ok = true;

cleanup:
    for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
        if (clients[i]) {
            XTcpSocket_abort((XAbstractSocket*)clients[i]);
            XTcpSocket_deleteLater((XObject*)clients[i]);
        }
    }
    XConsoleShellXTcpServerAdapter_closeAll(&adapter);
    if (server) {
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
    }
    if (shell) XConsoleShell_delete_base(shell);
    return ok;
}
#endif


#if XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON
static bool xcs_backend_test_telnet_basic(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* client = NULL;
    XConsoleShell* shell = NULL;
    XConsoleShellXTcpServerAdapter adapter;
    bool ok = false;
    const uint8_t willEcho[3] = { 255, 251, 1 };
    const char command[] = "echo telnet-ok\n";

    memset(&adapter, 0, sizeof(adapter));
    server = XTcpServer_create();
    shell = XConsoleShell_create(NULL);
    XCS_BACKEND_CHECK(server != NULL && shell != NULL,
                      "创建 Telnet server 或 Shell 失败");
    XCS_BACKEND_CHECK(XTcpServer_listen(server, NULL, 0),
                      "Telnet 回环监听失败");
    XCS_BACKEND_CHECK(XConsoleShellXTcpServerAdapter_initProtocol(
                          &adapter, shell, server,
                          XConsoleShellXTcpServerProtocol_Telnet),
                      "Telnet 协议适配器初始化失败");
    client = XTcpSocket_create();
    XCS_BACKEND_CHECK(client != NULL, "创建 Telnet 客户端套接字失败");
    XTcpSocket_connectToHost_base((XAbstractSocket*)client, "127.0.0.1",
                                   XTcpServer_serverPort(server),
                                   XIODevice_ReadWrite,
                                   XAbstractSocket_AnyIPProtocol);
    XCS_BACKEND_CHECK(xcs_backend_wait_connected(client, 3000),
                      "Telnet 客户端连接超时");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XConsoleShell_sessionCount(shell) < 2 &&
               XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            (void)XConsoleShellXTcpServerAdapter_acceptPending(&adapter);
            XThread_msleep(1);
        }
    }
    XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 2 &&
                      adapter.bindings[0].protocol ==
                          XConsoleShellXTcpServerProtocol_Telnet,
                      "Telnet acceptPending 未创建协议会话");
#if XCONSOLE_SHELL_AUTH_ON
    adapter.bindings[0].session->authenticated = true;
    adapter.bindings[0].session->permissionMask = UINT32_MAX;
#endif
    XCS_BACKEND_CHECK(XTcpSocket_write_1((XIODevice*)client,
                                         willEcho, sizeof(willEcho)) ==
                          (int64_t)sizeof(willEcho),
                      "Telnet 客户端发送 WILL ECHO 失败");
    XCS_BACKEND_CHECK(XTcpSocket_write_1((XIODevice*)client,
                                         command, (int64_t)strlen(command)) ==
                          (int64_t)strlen(command),
                      "Telnet 客户端发送命令失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XDateTime_currentMSecsSinceEpoch() - start < 3000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            (void)XConsoleShellXTcpServerAdapter_pump(&adapter, 128);
            if (XTcpSocket_bytesAvailable_base((const XIODevice*)client) > 0)
                break;
            XThread_msleep(1);
        }
    }
    XCS_BACKEND_CHECK(xcs_backend_read_contains(client, "telnet-ok", 3000),
                      "Telnet 协商过滤或命令执行失败");
    ok = true;

cleanup:
    XConsoleShellXTcpServerAdapter_closeAll(&adapter);
    if (client) {
        XTcpSocket_abort((XAbstractSocket*)client);
        XTcpSocket_deleteLater((XObject*)client);
    }
    if (server) {
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
    }
    if (shell) XConsoleShell_delete_base(shell);
    return ok;
}

static bool xcs_backend_test_telnet_multi_connect_close(void)
{
    enum {
        XCS_BACKEND_MULTI_CONN = (int)(XCONSOLE_SHELL_MAX_SESSIONS - 1u),
        XCS_BACKEND_MULTI_ROUNDS = 20
    };
    XTcpServer* server = NULL;
    XConsoleShell* shell = NULL;
    XConsoleShellXTcpServerAdapter adapter;
    XTcpSocket* clients[XCS_BACKEND_MULTI_CONN];
    size_t i;
    unsigned round;
    bool ok = false;

    memset(clients, 0, sizeof(clients));
    memset(&adapter, 0, sizeof(adapter));
    server = XTcpServer_create();
    shell = XConsoleShell_create(NULL);
    XCS_BACKEND_CHECK(server != NULL && shell != NULL,
                      "创建多连接 Telnet server 或 Shell 失败");
    XCS_BACKEND_CHECK(XTcpServer_listen(server, NULL, 0),
                      "Telnet 多连接回环监听失败");
    XCS_BACKEND_CHECK(XConsoleShellXTcpServerAdapter_initProtocol(
                          &adapter, shell, server,
                          XConsoleShellXTcpServerProtocol_Telnet),
                      "Telnet 多连接协议初始化失败");
    for (round = 0; round < XCS_BACKEND_MULTI_ROUNDS; ++round) {
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            clients[i] = XTcpSocket_create();
            XCS_BACKEND_CHECK(clients[i] != NULL, "创建多连接 Telnet 客户端套接字失败");
            XTcpSocket_connectToHost_base((XAbstractSocket*)clients[i], "127.0.0.1",
                                           XTcpServer_serverPort(server),
                                           XIODevice_ReadWrite,
                                           XAbstractSocket_AnyIPProtocol);
        }
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i)
            XCS_BACKEND_CHECK(xcs_backend_wait_connected(clients[i], 3000),
                              "多连接 Telnet 客户端连接超时");
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (XConsoleShell_sessionCount(shell) < XCS_BACKEND_MULTI_CONN + 1u &&
                   XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                (void)XConsoleShellXTcpServerAdapter_acceptPending(&adapter);
                XThread_msleep(1);
            }
        }
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == XCS_BACKEND_MULTI_CONN + 1u,
                          "多连接 Telnet 未全部创建会话");
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            XTcpSocket_abort((XAbstractSocket*)clients[i]);
            XTcpSocket_deleteLater((XObject*)clients[i]);
            clients[i] = NULL;
        }
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (XConsoleShell_sessionCount(shell) != 1u &&
                   XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
                (void)XConsoleShellXTcpServerAdapter_pump(&adapter, 4096);
                XThread_msleep(1);
            }
        }
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 1u,
                          "多连接 Telnet 断开后附加会话未全部释放");
        for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
            XCS_BACKEND_CHECK(adapter.bindings[i].session == NULL &&
                              adapter.bindings[i].socket == NULL,
                              "多连接 Telnet 断开后绑定槽未清空");
        }
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            clients[i] = XTcpSocket_create();
            XCS_BACKEND_CHECK(clients[i] != NULL, "创建 closeAll Telnet 客户端套接字失败");
            XTcpSocket_connectToHost_base((XAbstractSocket*)clients[i], "127.0.0.1",
                                           XTcpServer_serverPort(server),
                                           XIODevice_ReadWrite,
                                           XAbstractSocket_AnyIPProtocol);
        }
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i)
            XCS_BACKEND_CHECK(xcs_backend_wait_connected(clients[i], 3000),
                              "closeAll 前 Telnet 客户端连接超时");
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (XConsoleShell_sessionCount(shell) < XCS_BACKEND_MULTI_CONN + 1u &&
                   XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                (void)XConsoleShellXTcpServerAdapter_acceptPending(&adapter);
                XThread_msleep(1);
            }
        }
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == XCS_BACKEND_MULTI_CONN + 1u,
                          "closeAll 前 Telnet 未全部创建会话");
        XConsoleShellXTcpServerAdapter_closeAll(&adapter);
        XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 1u,
                          "closeAll 后 Telnet 附加会话未全部释放");
        for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
            XCS_BACKEND_CHECK(adapter.bindings[i].session == NULL &&
                              adapter.bindings[i].socket == NULL,
                              "closeAll 后 Telnet 绑定槽未清空");
        }
        for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
            XTcpSocket_abort((XAbstractSocket*)clients[i]);
            XTcpSocket_deleteLater((XObject*)clients[i]);
            clients[i] = NULL;
        }
    }
    ok = true;

cleanup:
    for (i = 0; i < XCS_BACKEND_MULTI_CONN; ++i) {
        if (clients[i]) {
            XTcpSocket_abort((XAbstractSocket*)clients[i]);
            XTcpSocket_deleteLater((XObject*)clients[i]);
        }
    }
    XConsoleShellXTcpServerAdapter_closeAll(&adapter);
    if (server) {
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
    }
    if (shell) XConsoleShell_delete_base(shell);
    return ok;
}
#endif

#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON && XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON
static bool xcs_backend_test_ssh_telnet_simultaneous(void)
{
    XTcpServer* sshServer = NULL;
    XTcpServer* telnetServer = NULL;
    XTcpSocket* sshClient = NULL;
    XTcpSocket* telnetClient = NULL;
    XConsoleShell* shell = NULL;
    XConsoleShellXTcpServerAdapter sshAdapter;
    XConsoleShellXTcpServerAdapter telnetAdapter;
    bool ok = false;

    memset(&sshAdapter, 0, sizeof(sshAdapter));
    memset(&telnetAdapter, 0, sizeof(telnetAdapter));
    shell = XConsoleShell_create(NULL);
    sshServer = XTcpServer_create();
    telnetServer = XTcpServer_create();
    XCS_BACKEND_CHECK(shell != NULL && sshServer != NULL && telnetServer != NULL,
                      "创建 SSH/Telnet 双服务端或 Shell 失败");
    XCS_BACKEND_CHECK(XTcpServer_listen(sshServer, NULL, 0),
                      "SSH 双服务端监听失败");
    XCS_BACKEND_CHECK(XTcpServer_listen(telnetServer, NULL, 0),
                      "Telnet 双服务端监听失败");
    XCS_BACKEND_CHECK(XConsoleShellXTcpServerAdapter_initProtocol(
                          &sshAdapter, shell, sshServer,
                          XConsoleShellXTcpServerProtocol_Ssh),
                      "SSH 协议初始化失败");
    XCS_BACKEND_CHECK(XConsoleShellXTcpServerAdapter_initProtocol(
                          &telnetAdapter, shell, telnetServer,
                          XConsoleShellXTcpServerProtocol_Telnet),
                      "Telnet 协议初始化失败");
    sshClient = XTcpSocket_create();
    telnetClient = XTcpSocket_create();
    XCS_BACKEND_CHECK(sshClient != NULL && telnetClient != NULL,
                      "创建 SSH/Telnet 双客户端失败");
    XTcpSocket_connectToHost_base((XAbstractSocket*)sshClient, "127.0.0.1",
                                   XTcpServer_serverPort(sshServer),
                                   XIODevice_ReadWrite,
                                   XAbstractSocket_AnyIPProtocol);
    XTcpSocket_connectToHost_base((XAbstractSocket*)telnetClient, "127.0.0.1",
                                   XTcpServer_serverPort(telnetServer),
                                   XIODevice_ReadWrite,
                                   XAbstractSocket_AnyIPProtocol);
    XCS_BACKEND_CHECK(xcs_backend_wait_connected(sshClient, 3000),
                      "SSH 双服务端连接超时");
    XCS_BACKEND_CHECK(xcs_backend_wait_connected(telnetClient, 3000),
                      "Telnet 双服务端连接超时");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XConsoleShell_sessionCount(shell) < 3 &&
               XDateTime_currentMSecsSinceEpoch() - start < 5000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            (void)XConsoleShellXTcpServerAdapter_acceptPending(&sshAdapter);
            (void)XConsoleShellXTcpServerAdapter_acceptPending(&telnetAdapter);
            XThread_msleep(1);
        }
    }
    XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 3,
                      "SSH/Telnet 双服务端未同时创建会话");
    XCS_BACKEND_CHECK(xcs_backend_read_contains(sshClient,
                                                 "SSH-2.0-XinYueC_1.0", 3000),
                      "SSH 双服务端未收到 banner");
    XCS_BACKEND_CHECK(sshAdapter.bindings[0].protocol ==
                          XConsoleShellXTcpServerProtocol_Ssh &&
                      telnetAdapter.bindings[0].protocol ==
                          XConsoleShellXTcpServerProtocol_Telnet,
                      "SSH/Telnet 双服务端协议绑定错误");
    ok = true;

cleanup:
    XConsoleShellXTcpServerAdapter_closeAll(&sshAdapter);
    XConsoleShellXTcpServerAdapter_closeAll(&telnetAdapter);
    if (sshClient) {
        XTcpSocket_abort((XAbstractSocket*)sshClient);
        XTcpSocket_deleteLater((XObject*)sshClient);
    }
    if (telnetClient) {
        XTcpSocket_abort((XAbstractSocket*)telnetClient);
        XTcpSocket_deleteLater((XObject*)telnetClient);
    }
    if (sshServer) {
        XTcpServer_close(sshServer);
        XTcpServer_deleteLater((XObject*)sshServer);
    }
    if (telnetServer) {
        XTcpServer_close(telnetServer);
        XTcpServer_deleteLater((XObject*)telnetServer);
    }
    if (shell) XConsoleShell_delete_base(shell);
    return ok;
}
#endif

#if !XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
static bool xcs_backend_test_roundtrip(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* client = NULL;
    XConsoleShell* shell = NULL;
    XConsoleShellXTcpServerAdapter adapter;
    XConsoleShellSession* session = NULL;
    const char command[] = "echo tcp-shell\n";
    bool ok = false;

    memset(&adapter, 0, sizeof(adapter));
    server = XTcpServer_create();
    shell = XConsoleShell_create(NULL);
    XCS_BACKEND_CHECK(server != NULL && shell != NULL, "创建 server 或 Shell 失败");
    XCS_BACKEND_CHECK(XTcpServer_listen(server, NULL, 0), "回环监听失败");

    XConsoleShellXTcpServerAdapter_init(&adapter, shell, server);
    client = XTcpSocket_create();
    XCS_BACKEND_CHECK(client != NULL, "创建客户端套接字失败");
    XTcpSocket_connectToHost_base((XAbstractSocket*)client, "127.0.0.1",
                                   XTcpServer_serverPort(server),
                                   XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    XCS_BACKEND_CHECK(xcs_backend_wait_connected(client, 3000), "客户端连接超时");

    /* 事件循环驱动 XTcpServer 的接收事件，再由适配器转为 Shell 会话。 */
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XConsoleShell_sessionCount(shell) < 2 &&
               XDateTime_currentMSecsSinceEpoch() - start < 3000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            (void)XConsoleShellXTcpServerAdapter_acceptPending(&adapter);
            XThread_msleep(1);
        }
    }
    XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 2,
                      "acceptPending 未创建附加 Shell 会话");
    XCS_BACKEND_CHECK(adapter.bindings[0].session != NULL &&
                      adapter.bindings[0].socket != NULL,
                      "TCP 绑定槽未保存会话和套接字");
    session = adapter.bindings[0].session;
#if XCONSOLE_SHELL_AUTH_ON
    session->authenticated = true;
    session->permissionMask = UINT32_MAX;
#endif

    XCS_BACKEND_CHECK(XTcpSocket_write_1((XIODevice*)client, command,
                                         sizeof(command) - 1u) ==
                      (int64_t)(sizeof(command) - 1u),
                      "客户端命令写入失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        size_t pumped = 0;
        while (XDateTime_currentMSecsSinceEpoch() - start < 3000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            pumped += XConsoleShellXTcpServerAdapter_pump(&adapter, 128);
            if (pumped >= sizeof(command) - 1u) break;
            XThread_msleep(1);
        }
        XCS_BACKEND_CHECK(pumped == sizeof(command) - 1u,
                          "pump 未读取完整 Shell 命令");
    }
    XCS_BACKEND_CHECK(xcs_backend_read_contains(client, "tcp-shell", 3000),
                      "客户端未收到 Shell echo 输出");
    XTcpSocket_abort((XAbstractSocket*)client);
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XConsoleShell_sessionCount(shell) != 1 &&
               XDateTime_currentMSecsSinceEpoch() - start < 3000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            (void)XConsoleShellXTcpServerAdapter_pump(&adapter, 128);
            XThread_msleep(1);
        }
    }
    XCS_BACKEND_CHECK(XConsoleShell_sessionCount(shell) == 1 &&
                      adapter.bindings[0].session == NULL,
                      "远端断开未释放附加会话");
    ok = true;

cleanup:
    XConsoleShellXTcpServerAdapter_closeAll(&adapter);
    if (client) {
        XTcpSocket_abort((XAbstractSocket*)client);
        XTcpSocket_deleteLater((XObject*)client);
    }
    if (server) {
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
    }
    if (shell) XConsoleShell_delete_base(shell);
    return ok;
}
#endif

bool XConsoleShellBackendTest_runAll(void)
{
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
    bool ok = xcs_backend_test_ssh_banner();
    ok = xcs_backend_test_ssh_multi_connect_close() && ok;
    XPrintf("XConsoleShell SSH 后端测试: %s\n", ok ? "通过" : "失败");
#else
    bool ok = xcs_backend_test_roundtrip();
    XPrintf("XConsoleShell TCP 后端测试: %s\n", ok ? "通过" : "失败");
#endif
#if XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON
    ok = xcs_backend_test_telnet_basic() && ok;
    ok = xcs_backend_test_telnet_multi_connect_close() && ok;
    XPrintf("XConsoleShell Telnet 后端测试: %s\n", ok ? "通过" : "失败");
#endif
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON && XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON
    ok = xcs_backend_test_ssh_telnet_simultaneous() && ok;
    XPrintf("XConsoleShell SSH/Telnet 双后端测试: %s\n", ok ? "通过" : "失败");
#endif
    return ok;
}

#else

bool XConsoleShellBackendTest_runAll(void)
{
    /* 当前裁剪配置没有 TCP 服务端适配器，空测试保持链接契约稳定。 */
    return true;
}

#endif
