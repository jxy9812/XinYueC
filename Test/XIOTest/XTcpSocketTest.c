#include "XIOTest.h"
#include "XTcpSocket.h"
#include "XMemory.h"
#include "XTestMenu.h"
#include "XByteArray.h"
#include "XCoreApplication.h"
#include "XHostAddress.h"
#include "XThread.h"
#include "XVariant.h"
#include <stdio.h>
#include <string.h>

// ==================== 测试函数声明 ====================
static void XTcpSocketClientTest(void);
static void XTcpSocketConnectTest(void);

// ==================== 全局变量 ====================
static XTcpSocket* g_tcpClient = NULL;
static bool g_connected = false;

// ==================== 回调函数 ====================

/**
 * @brief connected 信号回调 - 连接成功
 */
static void onConnected(XObject* sender, XVarList* args)
{
    (void)args;
    (void)sender;
    g_connected = true;
    XPrintf("[TCP] 已连接到服务器\n");
}

/**
 * @brief disconnected 信号回调 - 连接断开
 */
static void onDisconnected(XObject* sender, XVarList* args)
{
    (void)args;
    (void)sender;
    g_connected = false;
    XPrintf("[TCP] 连接已断开\n");
}

/**
 * @brief readyRead 信号回调 - 接收数据
 */
static void onReadyRead(XObject* sender, XVarList* args)
{
    (void)args;
    XIODevice* sock = (XIODevice*)sender;
    
    // 读取所有可用数据
    int64_t available = XIODevice_bytesAvailable_base(sock);
    if (available <= 0) return;
    
    char* buffer = (char*)XMalloc_System((size_t)available + 1);
    if (!buffer) return;
    
    int64_t bytesRead = XIODevice_read_1(sock, buffer, available);
    if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        XPrintf("[TCP接收] %lld 字节: %s\n", (long long)bytesRead, buffer);
    }
    
    XFree_System(buffer);
}

/**
 * @brief errorOccurred 信号回调 - 错误发生
 */
static void onErrorOccurred(XObject* sender, XVarList* args)
{
    (void)args;
    XAbstractSocket* sock = (XAbstractSocket*)sender;
    
    XAbstractSocket_SocketError error = XAbstractSocket_error(sock);
    const char* errorString = XAbstractSocket_errorString(sock);
    
    XPrintf("[TCP错误] 错误码: %d, 描述: %s\n", error, errorString ? errorString : "未知错误");
}

/**
 * @brief bytesWritten 信号回调 - 数据发送完成
 */
static void onBytesWritten(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int64_t, bytes);
    XPrintf("[TCP] 已发送 %lld 字节\n", (long long)bytes);
}

// ==================== 测试函数实现 ====================

/**
 * @brief TCP 连接测试
 */
static void XTcpSocketConnectTest(void)
{
    XPrintf("\n========== TCP 连接测试 ==========\n");
    
    // 创建 TCP 套接字
    g_tcpClient = XTcpSocket_create();
    if (!g_tcpClient) {
        XPrintf("[错误] 创建TCP套接字失败\n");
        return;
    }
    
    g_connected = false;
    
    // 连接信号
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_connected_signal), onConnected);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_disconnected_signal), onDisconnected);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_readyRead_signal), onReadyRead);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_errorOccurred_signal), onErrorOccurred);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_bytesWritten_signal), onBytesWritten);
    
    // 连接到服务器
    XPrintf("[TCP] 正在连接到 127.0.0.1:8080...\n");
    XTcpSocket_connectToHost_base(g_tcpClient, "127.0.0.1", 8080, XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    
    // 等待连接
    XPrintf("[TCP] 等待连接...\n");
    for (int i = 0; i < 50 && !g_connected; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(100);
    }
    
    if (!g_connected) {
        XPrintf("[错误] 连接超时\n");
    } else {
        // 发送测试数据
        const char* msg = "Hello TCP Server!";
        int64_t sent = XIODevice_write_1((XIODevice*)g_tcpClient, msg, strlen(msg));
        XPrintf("[TCP] 发送 %lld 字节\n", (long long)sent);
        
        // 等待回复
        for (int i = 0; i < 20; i++) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(100);
        }
        
        // 断开连接
        XTcpSocket_disconnectFromHost_base(g_tcpClient);
        
        // 等待断开
        for (int i = 0; i < 10 && g_connected; i++) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(100);
        }
    }
    
    // 清理
    XTcpSocket_abort(g_tcpClient);
    XTcpSocket_deleteLater(g_tcpClient);
    g_tcpClient = NULL;
    
    XPrintf("========== TCP 连接测试完成 ==========\n\n");
}

/**
 * @brief 交互式 TCP 客户端测试
 */
static void XTcpSocketClientTest(void)
{
    XPrintf("\n========== TCP 客户端测试 ==========\n");
    
    // 输入服务器地址
    char serverIp[64] = { 0 };
    uint32_t serverPort = 0;

    XPrintf("请输入服务器IP地址(直接回车使用127.0.0.1): ");
    if (scanf("%s", serverIp) == NULL || serverIp[0] == '\n') {
        strcpy(serverIp, "127.0.0.1");
    }

    // 去除换行符
    size_t len = strlen(serverIp);
    if (len > 0 && serverIp[len - 1] == '\n') {
        serverIp[len - 1] = '\0';
    }
    /*return;*/
    XPrintf("请输入服务器端口(直接回车使用8888): ");
    if (scanf("%d", &serverPort) == NULL) {
        serverPort = 8888;
    }
    XPrintf("目标服务器: %s:%u\n", serverIp, serverPort);
    
    // 创建 TCP 套接字
    g_tcpClient = XTcpSocket_create();
    if (!g_tcpClient) {
        XPrintf("[错误] 创建TCP套接字失败\n");
        return;
    }
    
    g_connected = false;
    
    // 连接信号
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_connected_signal), onConnected);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_disconnected_signal), onDisconnected);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_readyRead_signal), onReadyRead);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_errorOccurred_signal), onErrorOccurred);
    XObject_connect_2(g_tcpClient, XSignal(XTcpSocket_bytesWritten_signal), onBytesWritten);
    
    // 连接到服务器
    XPrintf("[TCP] 正在连接到 %s:%u...\n", serverIp, serverPort);
    XTcpSocket_connectToHost_base(g_tcpClient, serverIp, serverPort, XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    
    // 等待连接
    XPrintf("[TCP] 等待连接...\n");
    for (int i = 0; i < 50 && !g_connected; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(100);
    }
    
    if (!g_connected) {
        XPrintf("[错误] 连接超时或失败\n");
        XTcpSocket_abort(g_tcpClient);
        XTcpSocket_deleteLater(g_tcpClient);
        g_tcpClient = NULL;
        return;
    }
    
    XPrintf("\n========== 开始通信 (输入 quit 退出) ==========\n");
    
    char input[1024] = {0};
    while (g_connected) {
        XPrintf("> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        // 去除换行符
        len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
            len--;
        }
        
        // 检查退出命令
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            XPrintf("断开连接...\n");
            XTcpSocket_disconnectFromHost_base(g_tcpClient);
            break;
        }
        
        // 空输入跳过
        if (len == 0) {
            continue;
        }
        
        // 发送数据
        int64_t sent = XIODevice_write_1((XIODevice*)g_tcpClient, input, len);
        if (sent < 0) {
            XPrintf("[错误] 发送失败\n");
        } else {
            XPrintf("[发送] %lld 字节\n", (long long)sent);
        }
        
        // 处理事件（接收可能的回复）
        for (int i = 0; i < 5; i++) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(10);
        }
    }
    
    // 等待断开完成
    for (int i = 0; i < 10 && g_connected; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(100);
    }
    
    // 清理
    XTcpSocket_abort(g_tcpClient);
    XTcpSocket_deleteLater(g_tcpClient);
    g_tcpClient = NULL;
    
    XPrintf("========== TCP 客户端测试完成 ==========\n\n");
}

// ==================== 菜单注册 ====================

void XTestMenu_XTcpSocketTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XTcpSocket(TCP套接字)");
    XTestMenu_addMenu(root, menu);
    {
        XAction* action = XTestMenu_addAction(menu, "TCP连接测试");
        XTestMenu_setActionFunction(action, XTcpSocketConnectTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "TCP客户端");
        XTestMenu_setActionFunction(action, XTcpSocketClientTest);
    }
}
