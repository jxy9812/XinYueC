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
#include <stdio.h>
#include <string.h>

// ==================== 测试函数声明 ====================
static void XTcpServerBasicTest(void);
static void XTcpServerEchoTest(void);

// ==================== 全局变量 ====================
static XTcpServer* g_tcpServer = NULL;
static int g_connectionCount = 0;

// ==================== 回调函数 ====================

/**
 * @brief newConnection 信号回调 - 新连接到达
 */
static void onNewConnection(XObject* sender, XVarList* args)
{
    (void)args;
    (void)sender;
    g_connectionCount++;
    XPrintf("[Server] 新连接到达 (总数: %d)\n", g_connectionCount);
}

/**
 * @brief 基本测试客户端 readyRead 回调
 */
static void onBasicClientReadyRead(XObject* sender, XVarList* args)
{
    (void)args;
    XIODevice* sock = (XIODevice*)sender;
    
    int64_t available = XIODevice_bytesAvailable_base(sock);
    if (available <= 0) return;
    
    char* buffer = (char*)XMalloc_System((size_t)available + 1);
    if (!buffer) return;
    
    int64_t bytesRead = XIODevice_read_1(sock, buffer, available);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        XPrintf("[Server] 收到客户端数据 %lld 字节: %s\n", (long long)bytesRead, buffer);
        
        // 发送回复
        const char* reply = "Hello from server!";
        XIODevice_write_1(sock, reply, strlen(reply));
        XPrintf("[Server] 已发送回复\n");
    }
    
    XFree_System(buffer);
}

/**
 * @brief 基本测试客户端 bytesWritten 回调 - 数据发送完成后断开
 */
static void onBasicClientBytesWritten(XObject* sender, XVarList* args)
{
    (void)args;
    XPrintf("[Server] 数据已写入，准备断开连接\n");
    // 延迟断开，确保数据发送
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)sender);
}

/**
 * @brief 基本测试客户端 disconnected 回调
 */
static void onBasicClientDisconnected(XObject* sender, XVarList* args)
{
    (void)args;
    XPrintf("[Server] 客户端已断开\n");
    XTcpSocket_deleteLater((XTcpSocket*)sender);
}

/**
 * @brief pendingConnectionAvailable 信号回调
 */
static void onPendingConnectionAvailable(XObject* sender, XVarList* args)
{
    (void)args;
    XTcpServer* server = (XTcpServer*)sender;
    
    // 获取待处理连接
    XTcpSocket* client = XTcpServer_nextPendingConnection_base(server);
    if (client) {
        XPrintf("[Server] 获取到待处理连接\n");
        
        // 获取客户端地址
        const XHostAddress* peerAddr = XAbstractSocket_peerAddress((XAbstractSocket*)client);
        uint16_t peerPort = XAbstractSocket_peerPort((XAbstractSocket*)client);
        
        XString* addrStr = XHostAddress_toString(peerAddr);
        const char* addr = XString_toUtf8(addrStr);
        XPrintf("[Server] 客户端地址: %s:%u\n", addr ? addr : "unknown", peerPort);
        if (addrStr) XString_delete_base(addrStr);
        
        // 连接信号 - 等待客户端发送数据后再回复
        XObject_connect_2(client, XSignal(XTcpSocket_readyRead_signal), onBasicClientReadyRead);
        XObject_connect_2(client, XSignal(XTcpSocket_bytesWritten_signal), onBasicClientBytesWritten);
        XObject_connect_2(client, XSignal(XTcpSocket_disconnected_signal), onBasicClientDisconnected);
        
        // 发送欢迎消息
        const char* welcome = "Welcome to XTcpServer!";
        XIODevice_write_1((XIODevice*)client, welcome, strlen(welcome));
        XPrintf("[Server] 已发送欢迎消息\n");
    }
}

/**
 * @brief acceptError 信号回调 - 接受错误
 */
static void onAcceptError(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XAbstractSocket_SocketError, error);
    XPrintf("[Server] 接受错误: %d\n", error);
}

// ==================== Echo服务器回调 ====================

/**
 * @brief Echo客户端 readyRead 回调
 */
static void onEchoClientReadyRead(XObject* sender, XVarList* args)
{
    (void)args;
    XIODevice* sock = (XIODevice*)sender;
    
    int64_t available = XIODevice_bytesAvailable_base(sock);
    if (available <= 0) return;
    
    char* buffer = (char*)XMalloc_System((size_t)available + 1);
    if (!buffer) return;
    
    int64_t bytesRead = XIODevice_read_1(sock, buffer, available);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        XPrintf("[Echo] 收到 %lld 字节: %s\n", (long long)bytesRead, buffer);
        
        // Echo 回去
        XIODevice_write_1(sock, buffer, bytesRead);
    }
    
    XFree_System(buffer);
}

/**
 * @brief Echo客户端 disconnected 回调
 */
static void onEchoClientDisconnected(XObject* sender, XVarList* args)
{
    (void)args;
    XPrintf("[Echo] 客户端断开连接\n");
    XTcpSocket_deleteLater((XTcpSocket*)sender);
}

/**
 * @brief Echo服务器新连接回调
 */
static void onEchoNewConnection(XObject* sender, XVarList* args)
{
    (void)args;
    XTcpServer* server = (XTcpServer*)sender;
    
    XTcpSocket* client = XTcpServer_nextPendingConnection_base(server);
    if (!client) return;
    
    XPrintf("[Echo] 新客户端连接\n");
    
    // 连接信号
    XObject_connect_2(client, XSignal(XTcpSocket_readyRead_signal), onEchoClientReadyRead);
    XObject_connect_2(client, XSignal(XTcpSocket_disconnected_signal), onEchoClientDisconnected);
}

// ==================== 测试函数实现 ====================

/**
 * @brief 基本服务器测试
 */
static void XTcpServerBasicTest(void)
{
    XPrintf("\n========== TCP Server 基本测试 ==========\n");
    
    // 创建服务器
    g_tcpServer = XTcpServer_create();
    if (!g_tcpServer) {
        XPrintf("[错误] 创建TCP服务器失败\n");
        return;
    }
    
    // 连接信号
    XObject_connect_2(g_tcpServer, XSignal(XTcpServer_newConnection_signal), onNewConnection);
    XObject_connect_2(g_tcpServer, XSignal(XTcpServer_pendingConnectionAvailable_signal), onPendingConnectionAvailable);
    XObject_connect_2(g_tcpServer, XSignal(XTcpServer_acceptError_signal), onAcceptError);
    
    // 设置最大待处理连接数
    XTcpServer_setMaxPendingConnections(g_tcpServer, 10);
    XPrintf("[Server] 最大待处理连接数: %d\n", XTcpServer_maxPendingConnections(g_tcpServer));
    
    // 设置监听队列大小
    XTcpServer_setListenBacklogSize(g_tcpServer, 50);
    XPrintf("[Server] 监听队列大小: %d\n", XTcpServer_listenBacklogSize(g_tcpServer));
    
    // 监听所有地址的 8080 端口
    XPrintf("[Server] 正在监听 0.0.0.0:8080...\n");
    if (!XTcpServer_listen(g_tcpServer, NULL, 8080)) {
        XPrintf("[错误] 监听失败\n");
        char* errStr = XTcpServer_errorString(g_tcpServer);
        if (errStr) {
            XPrintf("[错误] %s\n", errStr);
            XFree_System(errStr);
        }
        XTcpServer_deleteLater(g_tcpServer);
        g_tcpServer = NULL;
        return;
    }
    
    XPrintf("[Server] 监听成功!\n");
    XPrintf("[Server] 服务器端口: %u\n", XTcpServer_serverPort(g_tcpServer));
    
    // 获取服务器地址
    const XHostAddress* serverAddr = XTcpServer_serverAddress(g_tcpServer);
    XString* addrStr = XHostAddress_toString(serverAddr);
    const char* addr = XString_toUtf8(addrStr);
    XPrintf("[Server] 服务器地址: %s\n", addr ? addr : "unknown");
    if (addrStr) XString_delete_base(addrStr);
    
    // 获取套接字描述符
    intptr_t sockDesc = XTcpServer_socketDescriptor(g_tcpServer);
    XPrintf("[Server] 套接字描述符: %lld\n", (long long)sockDesc);
    
    // 等待连接
    XPrintf("[Server] 等待客户端连接 (30秒超时)...\n");
    bool timedOut = false;
    bool hasConn = XTcpServer_waitForNewConnection(g_tcpServer, 30000, &timedOut);
    
    if (timedOut) {
        XPrintf("[Server] 等待超时\n");
    } else if (hasConn) {
        XPrintf("[Server] 有新连接到达\n");
    }
    
    // 关闭服务器
    XTcpServer_close(g_tcpServer);
    XPrintf("[Server] 服务器已关闭\n");
    
    // 检查状态
    XPrintf("[Server] isListening: %s\n", XTcpServer_isListening(g_tcpServer) ? "true" : "false");
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    // 清理
    XTcpServer_deleteLater(g_tcpServer);
    g_tcpServer = NULL;
 
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XPrintf("========== TCP Server 基本测试完成 ==========\n\n");
}

/**
 * @brief Echo 服务器测试
 */
static void XTcpServerEchoTest(void)
{
    XPrintf("\n========== TCP Server Echo 测试 ==========\n");
    
    // 创建服务器
    XTcpServer* server = XTcpServer_create();
    if (!server) {
        XPrintf("[错误] 创建TCP服务器失败\n");
        return;
    }
    
    // 连接信号
    XObject_connect_2(server, XSignal(XTcpServer_newConnection_signal), onEchoNewConnection);
    
    // 监听
    XPrintf("[Echo] 正在监听 0.0.0.0:8888...\n");
    if (!XTcpServer_listen(server, NULL, 8888)) {
        XPrintf("[错误] 监听失败\n");
        char* errStr = XTcpServer_errorString(server);
        if (errStr) {
            XPrintf("[错误] %s\n", errStr);
            XFree_System(errStr);
        }
        XTcpServer_deleteLater(server);
        return;
    }
    
    XPrintf("[Echo] Echo服务器启动成功!\n");
    XPrintf("[Echo] 端口: %u\n", XTcpServer_serverPort(server));
    XPrintf("[Echo] 等待客户端连接 (输入 q 退出)...\n");
    
    // 运行事件循环
    char input[16] = { 0 };
    while (true) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        
        //// 非阻塞检查输入
        //if (fgets(input, sizeof(input), stdin)) {
        //    if (input[0] == 'q' || input[0] == 'Q') {
        //        break;
        //    }
        //}
        
        XThread_msleep(10);
    }
    
    // 关闭服务器
    XTcpServer_close(server);
    XPrintf("[Echo] 服务器已关闭\n");
    
    // 清理
    XTcpServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XPrintf("========== TCP Server Echo 测试完成 ==========\n\n");
}

// ==================== 菜单注册 ====================

void XMenu_XTcpServerTest(XMenu* root)
{
    XMenu* menu = XMenu_create("TCP Server 测试");
    if (!menu) return;
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "基本测试");
        XAction_setAction(action, XTcpServerBasicTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "服务器测试");
        XAction_setAction(action, XTcpServerEchoTest);
    }
}