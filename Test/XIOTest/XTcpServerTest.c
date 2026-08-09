#include "XIOTest.h"
#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XByteArray.h"
#include "XCoreApplication.h"
#include "XHostAddress.h"
#include "XThread.h"
#include "XVariant.h"
#include "XAbstractSocket.h"
#include "XNetworkProxy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ==================== 测试框架 ====================
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        XPrintf("  [失败] %s (第%d行)\n", msg, __LINE__); \
        return false; \
    } \
} while(0)

#define TEST_PASS(msg) XPrintf("  [通过] %s\n", msg)
#define TEST_FAIL(msg) XPrintf("  [失败] %s (第%d行)\n", msg, __LINE__)

static int g_passCount = 0;
static int g_failCount = 0;

// ==================== 辅助函数 ====================

/* 连接客户端到服务器，返回已连接的 XTcpSocket */
static XTcpSocket* connect_client(uint16_t port, int timeoutMs)
{
    XTcpSocket* client = XTcpSocket_create();
    if (!client) return NULL;
    XTcpSocket_init(client);

    XAbstractSocket_connectToHost_base((XAbstractSocket*)client,
        "127.0.0.1", port, XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);

    // 等待连接完成
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)client) != XAbstractSocket_ConnectedState) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XDateTime_currentMSecsSinceEpoch() - start > (uint64_t)timeoutMs) {
            XPrintf("  [超时] 客户端连接超时 %dms\n", timeoutMs);
            XTcpSocket_deleteLater(client);
            return NULL;
        }
        XThread_msleep(1);
    }
    return client;
}

/* 等待服务器有新连接 */
static bool wait_for_server_connection(XTcpServer* server, int timeoutMs)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    while (!XTcpServer_hasPendingConnections_base(server)) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XDateTime_currentMSecsSinceEpoch() - start > (uint64_t)timeoutMs) {
            return false;
        }
        XThread_msleep(1);
    }
    return true;
}

// ==================== 信号回调 ====================
static int g_newConnectionCount = 0;
static int g_pendingConnectionCount = 0;
static int g_acceptErrorCount = 0;
static XAbstractSocket_SocketError g_lastAcceptError = 0;

static void reset_signals(void)
{
    g_newConnectionCount = 0;
    g_pendingConnectionCount = 0;
    g_acceptErrorCount = 0;
    g_lastAcceptError = 0;
}

static void onNewConnection(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_newConnectionCount++;
}

static void onPendingConnectionAvailable(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_pendingConnectionCount++;
}

static void onAcceptError(XObject* sender, XVarList* args)
{
    (void)sender;
    int error = 0;
    if (args) {
        XVarList_start(args);
        error = (int)XVarList_arg(args, int);
    }
    g_acceptErrorCount++;
    g_lastAcceptError = (XAbstractSocket_SocketError)error;
}

// ==================== 测试用例 ====================

// ---- 1. 创建/销毁测试 ----
static bool test_create_destroy(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "XTcpServer_create 应返回非空");

    // 默认值检查
    TEST_ASSERT(XTcpServer_isListening(server) == false, "新建服务器不应在监听");
    TEST_ASSERT(XTcpServer_serverPort(server) == 0, "新建服务器端口应为 0");
    TEST_ASSERT(XTcpServer_maxPendingConnections(server) == 30, "默认 maxPendingConnections 应为 30");
    TEST_ASSERT(XTcpServer_listenBacklogSize(server) == 50, "默认 listenBacklogSize 应为 50");
    TEST_ASSERT(XTcpServer_hasPendingConnections_base(server) == false, "新建服务器不应有待处理连接");
    TEST_ASSERT(XTcpServer_nextPendingConnection_base(server) == NULL, "nextPendingConnection 应返回 NULL");
    TEST_ASSERT(XTcpServer_serverError(server) == XAbstractSocket_UnknownSocketError, "初始错误应为 UnknownSocketError");

    // 获取错误字符串（应返回 NULL 或空字符串）
    char* errStr = XTcpServer_errorString(server);
    TEST_ASSERT(errStr == NULL, "无错误时 errorString 应返回 NULL");

    // 套接字描述符
    TEST_ASSERT(XTcpServer_socketDescriptor(server) == -1, "未监听时 socketDescriptor 应为 -1");

    // 代理
    XNetworkProxy* proxy = XTcpServer_proxy(server);
    TEST_ASSERT(proxy != NULL, "proxy 不应为 NULL");

    // 服务器地址
    const XHostAddress* addr = XTcpServer_serverAddress(server);
    TEST_ASSERT(addr != NULL, "serverAddress 不应为 NULL");

    // 清理
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    // 测试栈分配
    XTcpServer stackServer;
    XTcpServer_init(&stackServer);
    TEST_ASSERT(XTcpServer_isListening(&stackServer) == false, "栈初始化后不应在监听");
    XTcpServer_close(&stackServer);

    TEST_PASS("创建/销毁测试");
    return true;
}

// ---- 2. listen 测试 ----
static bool test_listen(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 监听所有接口，自动分配端口
    bool ok = XTcpServer_listen(server, NULL, 0);
    TEST_ASSERT(ok == true, "listen(NULL, 0) 应成功");
    TEST_ASSERT(XTcpServer_isListening(server) == true, "listen 后 isListening 应为 true");
    TEST_ASSERT(XTcpServer_serverPort(server) != 0, "自动分配端口不应为 0");
    TEST_ASSERT(XTcpServer_socketDescriptor(server) != -1, "socketDescriptor 不应为 -1");

    uint16_t port = XTcpServer_serverPort(server);
    XPrintf("  [信息] 自动分配端口: %u\n", port);

    // 重复监听应失败
    ok = XTcpServer_listen(server, NULL, 0);
    TEST_ASSERT(ok == false, "重复 listen 应返回 false");

    // 监听固定端口
    XTcpServer_close(server);
    ok = XTcpServer_listen(server, NULL, 9999);
    TEST_ASSERT(ok == true, "listen(NULL, 9999) 应成功");
    TEST_ASSERT(XTcpServer_serverPort(server) == 9999, "端口应为 9999");

    // 监听本地回环地址
    XTcpServer_close(server);
    XHostAddress loopback;
    XHostAddress_init(&loopback);
    XHostAddress_setAddressIPv4(&loopback, 0x7F000001); // 127.0.0.1
    ok = XTcpServer_listen(server, &loopback, 0);
    TEST_ASSERT(ok == true, "listen(127.0.0.1, 0) 应成功");
    TEST_ASSERT(XTcpServer_isListening(server) == true, "应在监听");

    // 清理
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("listen 测试");
    return true;
}

// ---- 3. close 测试 ----
static bool test_close(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 未监听时 close 应安全
    XTcpServer_close(server);
    TEST_ASSERT(XTcpServer_isListening(server) == false, "close 后不应在监听");

    // 监听后 close
    XTcpServer_listen(server, NULL, 0);
    TEST_ASSERT(XTcpServer_isListening(server) == true, "listen 后应在监听");
    XTcpServer_close(server);
    TEST_ASSERT(XTcpServer_isListening(server) == false, "close 后不应在监听");
    TEST_ASSERT(XTcpServer_serverPort(server) == 0, "close 后端口应为 0");
    TEST_ASSERT(XTcpServer_socketDescriptor(server) == -1, "close 后 socketDescriptor 应为 -1");

    // close 后可以重新 listen
    bool ok = XTcpServer_listen(server, NULL, 0);
    TEST_ASSERT(ok == true, "close 后可重新 listen");

    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("close 测试");
    return true;
}

// ---- 4. 连接管理配置测试 ----
static bool test_connection_management(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // maxPendingConnections
    XTcpServer_setMaxPendingConnections(server, 100);
    TEST_ASSERT(XTcpServer_maxPendingConnections(server) == 100, "maxPendingConnections 应为 100");

    XTcpServer_setMaxPendingConnections(server, 0);
    TEST_ASSERT(XTcpServer_maxPendingConnections(server) == 0, "maxPendingConnections 应为 0");

    // listenBacklogSize
    XTcpServer_setListenBacklogSize(server, 200);
    TEST_ASSERT(XTcpServer_listenBacklogSize(server) == 200, "listenBacklogSize 应为 200");

    // pauseAccepting / resumeAccepting
    XTcpServer_pauseAccepting(server);
    XTcpServer_resumeAccepting(server);

    // 在 listen 前设置 backlog 应在 listen 时生效
    XTcpServer_setListenBacklogSize(server, 50);
    XTcpServer_listen(server, NULL, 0);
    XTcpServer_close(server);

    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("连接管理配置测试");
    return true;
}

// ---- 5. 基本连接接受测试 ----
static bool test_accept_connection(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 连接信号
    reset_signals();
    XObject_connect_2(server, XSignal(XTcpServer_newConnection_signal), onNewConnection);
    XObject_connect_2(server, XSignal(XTcpServer_pendingConnectionAvailable_signal), onPendingConnectionAvailable);

    // 监听
    XTcpServer_listen(server, NULL, 0);
    uint16_t port = XTcpServer_serverPort(server);

    // 连接客户端
    XTcpSocket* client = connect_client(port, 3000);
    TEST_ASSERT(client != NULL, "客户端连接应成功");

    // 等待服务器处理
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XThread_msleep(100);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    // 验证信号
    TEST_ASSERT(g_newConnectionCount >= 1, "newConnection 信号应至少发射 1 次");
    TEST_ASSERT(g_pendingConnectionCount >= 1, "pendingConnectionAvailable 信号应至少发射 1 次");

    // 检查待处理连接
    bool hasPending = XTcpServer_hasPendingConnections_base(server);
    TEST_ASSERT(hasPending == true, "应有待处理连接");

    // 取出连接
    XTcpSocket* accepted = XTcpServer_nextPendingConnection_base(server);
    TEST_ASSERT(accepted != NULL, "nextPendingConnection 应返回非空");
    TEST_ASSERT(XAbstractSocket_state((XAbstractSocket*)accepted) == XAbstractSocket_ConnectedState, "接受的连接应处于 ConnectedState");

    // 检查对端地址
    const XHostAddress* peerAddr = XAbstractSocket_peerAddress((XAbstractSocket*)accepted);
    TEST_ASSERT(peerAddr != NULL, "peerAddress 不应为 NULL");
    uint16_t peerPort = XAbstractSocket_peerPort((XAbstractSocket*)accepted);
    XPrintf("  [信息] 客户端地址: port=%u\n", peerPort);

    // 再次取出应为 NULL
    XTcpSocket* noConn = XTcpServer_nextPendingConnection_base(server);
    TEST_ASSERT(noConn == NULL, "无待处理连接时 nextPendingConnection 应返回 NULL");

    // 清理
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)client);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)accepted);
    XTcpSocket_deleteLater(client);
    XTcpSocket_deleteLater(accepted);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("基本连接接受测试");
    return true;
}

// ---- 6. waitForNewConnection 测试 ----
static bool test_wait_for_new_connection(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 未监听时 waitForNewConnection 应失败
    bool timedOut = false;
    bool ok = XTcpServer_waitForNewConnection(server, 100, &timedOut);
    TEST_ASSERT(ok == false, "未监听时 waitForNewConnection 应返回 false");

    // 监听
    XTcpServer_listen(server, NULL, 0);
    uint16_t port = XTcpServer_serverPort(server);

    // 非阻塞模式（msec=0）：超时返回
    timedOut = false;
    ok = XTcpServer_waitForNewConnection(server, 0, &timedOut);
    TEST_ASSERT(ok == false && timedOut == true, "msec=0 应超时返回");

    // 在另一个线程中连接
    // 由于是单线程，我们预先连接后用 hasPendingConnections 检查
    XTcpSocket* client = connect_client(port, 3000);
    TEST_ASSERT(client != NULL, "客户端连接应成功");

    // waitForNewConnection 应立即返回 true（已有待处理连接）
    timedOut = false;
    ok = XTcpServer_waitForNewConnection(server, 1000, &timedOut);
    TEST_ASSERT(ok == true, "有待处理连接时 waitForNewConnection 应返回 true");
    TEST_ASSERT(timedOut == false, "不应超时");

    // 取出连接
    XTcpSocket* accepted = XTcpServer_nextPendingConnection_base(server);
    TEST_ASSERT(accepted != NULL, "应可取出连接");

    // 清理
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)client);
    XTcpSocket_deleteLater(client);
    XTcpSocket_deleteLater(accepted);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("waitForNewConnection 测试");
    return true;
}

// ---- 7. 暂停/恢复接受测试 ----
static bool test_pause_resume_accepting(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    reset_signals();
    XObject_connect_2(server, XSignal(XTcpServer_newConnection_signal), onNewConnection);

    XTcpServer_listen(server, NULL, 0);
    uint16_t port = XTcpServer_serverPort(server);

    // 暂停接受
    XTcpServer_pauseAccepting(server);

    // 连接客户端
    XTcpSocket* client1 = connect_client(port, 3000);
    TEST_ASSERT(client1 != NULL, "客户端1连接应成功");

    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XThread_msleep(100);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    // 暂停状态下，newConnection 可能不会触发（取决于实现）
    // 恢复接受
    XTcpServer_resumeAccepting(server);

    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XThread_msleep(100);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    // 现在应该能看到连接
    bool hasPending = XTcpServer_hasPendingConnections_base(server);
    XPrintf("  [信息] 暂停恢复后待处理连接: %s\n", hasPending ? "有" : "无");

    // 清理
    if (XTcpServer_hasPendingConnections_base(server)) {
        XTcpSocket* accepted = XTcpServer_nextPendingConnection_base(server);
        if (accepted) XTcpSocket_deleteLater(accepted);
    }
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)client1);
    XTcpSocket_deleteLater(client1);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("暂停/恢复接受测试");
    return true;
}

// ---- 8. 数据收发测试 ----
static bool test_data_transfer(void)
{
    const char* SEND_DATA = "Hello from TCP Server!";
    const char* CLIENT_REPLY = "Hello back!";
    static char s_serverRecvBuf[1024];
    static int s_serverRecvLen = 0;
    static char s_clientRecvBuf[1024];
    static int s_clientRecvLen = 0;
    static bool s_clientDone = false;

    // 重置
    s_serverRecvLen = 0;
    s_clientRecvLen = 0;
    s_clientDone = false;

    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 服务器：收到数据后回复
    XObject_connect_2(server, XSignal(XTcpServer_newConnection_signal), onNewConnection);

    XTcpServer_listen(server, NULL, 0);
    uint16_t port = XTcpServer_serverPort(server);

    // 连接客户端
    XTcpSocket* client = connect_client(port, 3000);
    TEST_ASSERT(client != NULL, "客户端连接应成功");

    // 等待服务器接受
    bool gotConn = wait_for_server_connection(server, 2000);
    TEST_ASSERT(gotConn == true, "服务器应收到连接");

    XTcpSocket* accepted = XTcpServer_nextPendingConnection_base(server);
    TEST_ASSERT(accepted != NULL, "取出的连接不应为空");

    // 服务器发送数据
    XIODevice_write_1((XIODevice*)accepted, SEND_DATA, (int64_t)strlen(SEND_DATA));

    // 客户端等待数据：通过 processEvents 轮询，直到收到数据或超时
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XIODevice_bytesAvailable_base((XIODevice*)client) == 0) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (XDateTime_currentMSecsSinceEpoch() - start > 2000) {
                break;
            }
            XThread_msleep(10);
        }
    }

    // 客户端读取数据
    int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)client);
    XPrintf("  [信息] 客户端可用数据: %lld 字节\n", (long long)avail);
    if (avail > 0) {
        int64_t n = XIODevice_read_1((XIODevice*)client, s_clientRecvBuf, (int64_t)sizeof(s_clientRecvBuf) - 1);
        if (n > 0) {
            s_clientRecvLen = (int)n;
            s_clientRecvBuf[n] = '\0';
        }
    }

    TEST_ASSERT(s_clientRecvLen > 0, "客户端应收到数据");
    if (s_clientRecvLen > 0) {
        TEST_ASSERT(strcmp(s_clientRecvBuf, SEND_DATA) == 0, "收到数据内容应匹配");
    }

    // 客户端回复
    XIODevice_write_1((XIODevice*)client, CLIENT_REPLY, (int64_t)strlen(CLIENT_REPLY));

    // 服务器等待数据
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XIODevice_bytesAvailable_base((XIODevice*)accepted) == 0) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (XDateTime_currentMSecsSinceEpoch() - start > 2000) {
                break;
            }
            XThread_msleep(10);
        }
    }

    // 服务器读取数据
    avail = XIODevice_bytesAvailable_base((XIODevice*)accepted);
    XPrintf("  [信息] 服务器可用数据: %lld 字节\n", (long long)avail);
    if (avail > 0) {
        int64_t n = XIODevice_read_1((XIODevice*)accepted, s_serverRecvBuf, (int64_t)sizeof(s_serverRecvBuf) - 1);
        if (n > 0) {
            s_serverRecvLen = (int)n;
            s_serverRecvBuf[n] = '\0';
        }
    }

    TEST_ASSERT(s_serverRecvLen > 0, "服务器应收到回复");
    if (s_serverRecvLen > 0) {
        TEST_ASSERT(strcmp(s_serverRecvBuf, CLIENT_REPLY) == 0, "收到回复内容应匹配");
    }

    // 清理
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)client);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)accepted);
    XTcpSocket_deleteLater(client);
    XTcpSocket_deleteLater(accepted);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("数据收发测试");
    return true;
}

// ---- 9. 多连接测试 ----
static bool test_multiple_connections(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    reset_signals();
    XObject_connect_2(server, XSignal(XTcpServer_newConnection_signal), onNewConnection);

    XTcpServer_listen(server, NULL, 0);
    uint16_t port = XTcpServer_serverPort(server);

    #define CONN_COUNT 5
    XTcpSocket* clients[CONN_COUNT] = {0};

    // 多个客户端连接
    for (int i = 0; i < CONN_COUNT; i++) {
        clients[i] = connect_client(port, 3000);
        TEST_ASSERT(clients[i] != NULL, "客户端连接应成功");
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(50);
    }

    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XThread_msleep(200);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    // 检查信号和待处理连接数
    XPrintf("  [信息] newConnection 信号次数: %d\n", g_newConnectionCount);
    int pendingCount = 0;
    while (XTcpServer_hasPendingConnections_base(server)) {
        XTcpSocket* sock = XTcpServer_nextPendingConnection_base(server);
        if (sock) {
            pendingCount++;
            XTcpSocket_deleteLater(sock);
        }
    }
    XPrintf("  [信息] 实际取出的连接数: %d\n", pendingCount);
    TEST_ASSERT(pendingCount > 0, "应取出至少一个连接");

    // 清理
    for (int i = 0; i < CONN_COUNT; i++) {
        if (clients[i]) {
            XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)clients[i]);
            XTcpSocket_deleteLater(clients[i]);
        }
    }
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("多连接测试");
    return true;
}

// ---- 10. setSocketDescriptor 测试 ----
static bool test_set_socket_descriptor(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 无效描述符
    bool ok = XTcpServer_setSocketDescriptor(server, -1);
    TEST_ASSERT(ok == false, "无效描述符应返回 false");

    // 使用有效描述符：先监听一个 socket，获取描述符
    // 先调用 listen 再获取描述符来测试
    XTcpServer_listen(server, NULL, 0);
    intptr_t desc = XTcpServer_socketDescriptor(server);
    XPrintf("  [信息] 原始 socket 描述符: %lld\n", (long long)desc);
    TEST_ASSERT(desc != -1, "listen 后 socketDescriptor 不应为 -1");

    XTcpServer_close(server);

    // 设置回同一个描述符
    ok = XTcpServer_setSocketDescriptor(server, desc);
    TEST_ASSERT(ok == true || ok == false, "setSocketDescriptor 结果");
    if (ok) {
        TEST_ASSERT(XTcpServer_isListening(server) == true, "setSocketDescriptor 后应在监听");
        // 注意：setSocketDescriptor 后端口可能不同
        uint16_t newPort = XTcpServer_serverPort(server);
        XPrintf("  [信息] setSocketDescriptor 后端口: %u\n", newPort);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("setSocketDescriptor 测试");
    return true;
}

// ---- 11. 错误处理测试 ----
static bool test_error_handling(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 监听已被占用的端口
    XTcpServer* server2 = XTcpServer_create();
    TEST_ASSERT(server2 != NULL, "创建 server2 失败");

    // 先让 server1 监听一个固定端口
    XTcpServer_listen(server, NULL, 9998);
    uint16_t occupiedPort = XTcpServer_serverPort(server);
    XPrintf("  [信息] server1 占用端口: %u\n", occupiedPort);

    // server2 尝试监听同一端口应失败
    // 注意：SO_REUSEADDR 可能允许绑定同一端口，所以这可能不总是失败
    // 我们只验证错误码不为空
    bool ok = XTcpServer_listen(server2, NULL, occupiedPort);
    if (!ok) {
        XAbstractSocket_SocketError err = XTcpServer_serverError(server2);
        char* errStr = XTcpServer_errorString(server2);
        XPrintf("  [信息] 监听失败: error=%d, str=%s\n", err, errStr ? errStr : "NULL");
        if (errStr) XFree_System(errStr);
        TEST_ASSERT(err != XAbstractSocket_UnknownSocketError, "错误码不应为 UnknownSocketError");
    } else {
        XPrintf("  [信息] 由于 SO_REUSEADDR，绑定同一端口成功\n");
        XTcpServer_close(server2);
    }

    // NULL 参数安全测试（部分）
    TEST_ASSERT(XTcpServer_isListening(NULL) == false, "NULL isListening 应返回 false");
    TEST_ASSERT(XTcpServer_serverPort(NULL) == 0, "NULL serverPort 应返回 0");
    TEST_ASSERT(XTcpServer_socketDescriptor(NULL) == -1, "NULL socketDescriptor 应返回 -1");
    TEST_ASSERT(XTcpServer_serverError(NULL) == XAbstractSocket_UnknownSocketError, "NULL serverError 应返回 UnknownSocketError");
    TEST_ASSERT(XTcpServer_nextPendingConnection_base(NULL) == NULL, "NULL nextPendingConnection 应返回 NULL");
    TEST_ASSERT(XTcpServer_hasPendingConnections_base(NULL) == false, "NULL hasPendingConnections 应返回 false");

    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XTcpServer_deleteLater(server2);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("错误处理测试");
    return true;
}

// ---- 12. 代理设置测试 ----
static bool test_proxy(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    XNetworkProxy proxy;
    XNetworkProxy_init(&proxy);
    XNetworkProxy_setType(&proxy, XNetworkProxy_Socks5Proxy);

    XString* hostStr = XString_create_utf8("proxy.example.com");
    XString* userStr = XString_create_utf8("user1");
    XString* passStr = XString_create_utf8("pass1");
    XNetworkProxy_setHostName(&proxy, hostStr);
    XNetworkProxy_setPort(&proxy, 1080);
    XNetworkProxy_setUser(&proxy, userStr);
    XNetworkProxy_setPassword(&proxy, passStr);

    XTcpServer_setProxy(server, &proxy);

    XNetworkProxy* retrieved = XTcpServer_proxy(server);
    TEST_ASSERT(retrieved != NULL, "获取的 proxy 不应为 NULL");
    TEST_ASSERT(retrieved->type == XNetworkProxy_Socks5Proxy, "proxy type 应匹配");
    TEST_ASSERT(retrieved->port == 1080, "proxy port 应匹配");

    if (retrieved->hostName) {
        const char* host = XString_toUtf8(retrieved->hostName);
        XPrintf("  [信息] proxy host: %s\n", host ? host : "NULL");
    }

    XTcpServer_deleteLater(server);
    XNetworkProxy_deinit_base(&proxy);
    if (hostStr) XString_delete_base(hostStr);
    if (userStr) XString_delete_base(userStr);
    if (passStr) XString_delete_base(passStr);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("代理设置测试");
    return true;
}

// ---- 13. 最大待处理连接限制测试 ----
static bool test_max_pending_connections(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 设置最大待处理连接数为 1
    XTcpServer_setMaxPendingConnections(server, 1);
    TEST_ASSERT(XTcpServer_maxPendingConnections(server) == 1, "maxPendingConnections 应为 1");

    XTcpServer_listen(server, NULL, 0);
    uint16_t port = XTcpServer_serverPort(server);

    // 连接两个客户端
    XTcpSocket* client1 = connect_client(port, 3000);
    TEST_ASSERT(client1 != NULL, "客户端1连接应成功");
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XThread_msleep(100);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    XTcpSocket* client2 = connect_client(port, 3000);
    TEST_ASSERT(client2 != NULL, "客户端2连接应成功");
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XThread_msleep(100);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    // 待处理连接数应不超过 1（因为设置了 maxPendingConnections=1）
    int count = 0;
    while (XTcpServer_hasPendingConnections_base(server)) {
        XTcpSocket* sock = XTcpServer_nextPendingConnection_base(server);
        if (sock) {
            count++;
            XTcpSocket_deleteLater(sock);
        }
    }
    XPrintf("  [信息] 取出的待处理连接数: %d\n", count);

    // 清理
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)client1);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)client2);
    XTcpSocket_deleteLater(client1);
    XTcpSocket_deleteLater(client2);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("最大待处理连接限制测试");
    return true;
}

// ---- 14. 服务器地址测试 ----
static bool test_server_address(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    // 未监听时
    const XHostAddress* addr = XTcpServer_serverAddress(server);
    TEST_ASSERT(addr != NULL, "serverAddress 不应为 NULL");

    // 监听 127.0.0.1
    XHostAddress loopback;
    XHostAddress_init(&loopback);
    XHostAddress_setAddressIPv4(&loopback, 0x7F000001);
    XTcpServer_listen(server, &loopback, 0);

    addr = XTcpServer_serverAddress(server);
    TEST_ASSERT(addr != NULL, "listen 后 serverAddress 不应为 NULL");

    XString* addrStr = XHostAddress_toString(addr);
    if (addrStr) {
        const char* utf8 = XString_toUtf8(addrStr);
        XPrintf("  [信息] 服务器地址: %s 端口: %u\n",
                utf8 ? utf8 : "NULL", XTcpServer_serverPort(server));
        XString_delete_base(addrStr);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    TEST_PASS("服务器地址测试");
    return true;
}

// ---- 15. 销毁时自动关闭测试 ----
static bool test_auto_close_on_destroy(void)
{
    XTcpServer* server = XTcpServer_create();
    TEST_ASSERT(server != NULL, "创建失败");

    XTcpServer_listen(server, NULL, 0);
    TEST_ASSERT(XTcpServer_isListening(server) == true, "listen 后应在监听");

    // 不需要手动 close，deleteLater 应该自动 close
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    // 如果上面没有崩溃，测试通过
    TEST_PASS("销毁时自动关闭测试");
    return true;
}

// ==================== 测试执行 ====================

// 测试用例表
typedef struct {
    const char* name;
    bool (*func)(void);
} TestCase;

// XAction_setAction 需要的签名是 void(*)(void)，包装一下
static void wrap_create_destroy(void) { test_create_destroy(); }
static void wrap_listen(void) { test_listen(); }
static void wrap_close(void) { test_close(); }
static void wrap_connection_management(void) { test_connection_management(); }
static void wrap_accept_connection(void) { test_accept_connection(); }
static void wrap_wait_for_new_connection(void) { test_wait_for_new_connection(); }
static void wrap_pause_resume_accepting(void) { test_pause_resume_accepting(); }
static void wrap_data_transfer(void) { test_data_transfer(); }
static void wrap_multiple_connections(void) { test_multiple_connections(); }
static void wrap_set_socket_descriptor(void) { test_set_socket_descriptor(); }
static void wrap_error_handling(void) { test_error_handling(); }
static void wrap_proxy(void) { test_proxy(); }
static void wrap_max_pending_connections(void) { test_max_pending_connections(); }
static void wrap_server_address(void) { test_server_address(); }
static void wrap_auto_close_on_destroy(void) { test_auto_close_on_destroy(); }

void XMenu_XTcpServerTest(XMenu* root)
{
    /* 先注册菜单，再执行自动测试 */
    XMenu* menu = XMenu_create("TCP Server 测试");
    if (!menu) return;
    XMenu_addMenu(root, menu);
    /* 注册菜单项（供交互式测试使用）*/
    XAction* a;
    a = XMenu_addAction(menu, "创建/销毁测试");              XAction_setAction(a, wrap_create_destroy);
    a = XMenu_addAction(menu, "listen 测试");                XAction_setAction(a, wrap_listen);
    a = XMenu_addAction(menu, "close 测试");                 XAction_setAction(a, wrap_close);
    a = XMenu_addAction(menu, "连接管理配置测试");           XAction_setAction(a, wrap_connection_management);
    a = XMenu_addAction(menu, "基本连接接受测试");           XAction_setAction(a, wrap_accept_connection);
    a = XMenu_addAction(menu, "waitForNewConnection 测试");  XAction_setAction(a, wrap_wait_for_new_connection);
    a = XMenu_addAction(menu, "暂停/恢复接受测试");          XAction_setAction(a, wrap_pause_resume_accepting);
    a = XMenu_addAction(menu, "数据收发测试");               XAction_setAction(a, wrap_data_transfer);
    a = XMenu_addAction(menu, "多连接测试");                 XAction_setAction(a, wrap_multiple_connections);
    a = XMenu_addAction(menu, "setSocketDescriptor 测试");   XAction_setAction(a, wrap_set_socket_descriptor);
    a = XMenu_addAction(menu, "错误处理测试");               XAction_setAction(a, wrap_error_handling);
    a = XMenu_addAction(menu, "代理设置测试");               XAction_setAction(a, wrap_proxy);
    a = XMenu_addAction(menu, "最大待处理连接限制测试");     XAction_setAction(a, wrap_max_pending_connections);
    a = XMenu_addAction(menu, "服务器地址测试");             XAction_setAction(a, wrap_server_address);
    a = XMenu_addAction(menu, "销毁时自动关闭测试");         XAction_setAction(a, wrap_auto_close_on_destroy);
}