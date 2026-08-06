/**
 * @file XConsoleShellBackendTest.c
 * @brief XConsoleShell XTcpServer 后端的库级 loopback 回归测试。
 * @details
 * 测试通过 XinYueC 的 XTcpServer、XTcpSocket、XCoreApplication 和
 * XConsoleShell 公共 API 建立本机回环连接，不直接依赖平台 socket、线程或
 * 文件接口。服务端适配器负责接收连接和泵入输入，客户端套接字负责验证输出。
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
            XPrintf("[FAIL] XConsoleShell TCP 后端: %s (第%d行)\n", \
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
    XPrintf("[FAIL] TCP 后端未收到期望输出，实际输出: %s\n", output);
    return false;
}

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

bool XConsoleShellBackendTest_runAll(void)
{
    bool ok = xcs_backend_test_roundtrip();
    XPrintf("XConsoleShell TCP 后端测试: %s\n", ok ? "通过" : "失败");
    return ok;
}

#else

bool XConsoleShellBackendTest_runAll(void)
{
    /* 当前裁剪配置没有 TCP 服务端适配器，空测试保持链接契约稳定。 */
    return true;
}

#endif
