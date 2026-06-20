#include "XIOTest.h"
#include "XUdpSocket.h"
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
static void XUdpSocketBasicTest(void);
static void XUdpSocketBroadcastTest(void);
static void XUdpSocketMulticastTest(void);
static void XUdpSocketClientTest(void);

// ==================== 全局变量 ====================
static XUdpSocket* g_udpServer = NULL;
static XUdpSocket* g_udpClient = NULL;

// ==================== 回调函数 ====================

/**
 * @brief readyRead 信号回调 - 接收数据报
 */
static void onReadyRead(XObject* sender, XVarList* args)
{
    (void)args;
    XUdpSocket* sock = (XUdpSocket*)sender;
    
    while (XUdpSocket_hasPendingDatagrams(sock)) {
        // 方法1: 使用 readDatagram
        int64_t size = XUdpSocket_pendingDatagramSize(sock);
        if (size <= 0) break;
        
        char* buffer = (char*)XMalloc_System((size_t)size + 1);
        if (!buffer) break;
        
        XHostAddress senderAddr;
        XHostAddress_init(&senderAddr);
        uint16_t senderPort = 0;
        
        int64_t bytesRead = XUdpSocket_readDatagram(sock, buffer, size, &senderAddr, &senderPort);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            
            // 打印发送者信息
            XString* addrStr = XHostAddress_toString(&senderAddr);
            XPrintf("[UDP] 收到 %lld 字节，来自 %s:%u\n", 
                   (long long)bytesRead, 
                   XString_toUtf8(addrStr), 
                   senderPort);
            XPrintf("[UDP] 数据: %s\n", buffer);
            
            XString_delete_base(addrStr);
        }
        
        XHostAddress_deinit_base(&senderAddr);
        XFree_System(buffer);
    }
}

/**
 * @brief readyRead 信号回调 - 使用 receiveDatagram
 */
static void onReadyReadDatagram(XObject* sender, XVarList* args)
{
    (void)args;
    XUdpSocket* sock = (XUdpSocket*)sender;
    
    while (XUdpSocket_hasPendingDatagrams(sock)) {
        // 方法2: 使用 receiveDatagram
        XNetworkDatagram* dgram = XUdpSocket_receiveDatagram(sock, -1);
        if (!dgram) break;
        
        const XHostAddress* senderAddr = XNetworkDatagram_senderAddress(dgram);
        int senderPort = XNetworkDatagram_senderPort(dgram);
        XByteArray* data = XNetworkDatagram_data(dgram);
        
        // 打印信息
        XString* addrStr = XHostAddress_toString(senderAddr);
        XPrintf("[UDP] 数据报来自 %s:%d，大小: %d\n",
               XString_toUtf8(addrStr),
               senderPort,
               (int)XByteArray_size_base(data));
        
        // 打印数据
        const char* dataPtr = (const char*)XByteArray_data(data);
        int64_t dataSize = XByteArray_size_base(data);
        XPrintf("[UDP] 数据: %.*s\n", (int)dataSize, dataPtr);
        
        // 创建回复
        XByteArray* replyData = XByteArray_create();
        XByteArray_append_utf8(replyData, "ACK from server");
        XNetworkDatagram* reply = XNetworkDatagram_makeReply(dgram, replyData);
        
        // 发送回复
        XUdpSocket_writeDatagram_3(sock, reply);
        
        XByteArray_delete_base(replyData);
        XNetworkDatagram_delete_base(reply);
        XString_delete_base(addrStr);
        XNetworkDatagram_delete_base(dgram);
    }
}

// ==================== 测试函数实现 ====================

/**
 * @brief UDP 基本收发测试
 */
static void XUdpSocketBasicTest(void)
{
    XPrintf("\n========== UDP 基本测试 ==========\n");
    
    // 创建服务器端 UDP 套接字
    g_udpServer = XUdpSocket_create();
    if (!g_udpServer) {
        XPrintf("[错误] 创建UDP服务器套接字失败\n");
        return;
    }
    
    // 绑定到本地端口
    XHostAddress serverAddr;
    XHostAddress_init(&serverAddr);
    XHostAddress_setAddressSpecial(&serverAddr, XHostAddress_AnySpecial);
    
    if (!XUdpSocket_bind_base(g_udpServer, &serverAddr, 8888, XAbstractSocket_DefaultForPlatform)) {
        XPrintf("[错误] 绑定UDP服务器到8888端口失败\n");
        XHostAddress_deinit_base(&serverAddr);
        XUdpSocket_abort(g_udpServer);
        XUdpSocket_deleteLater(g_udpServer);
        return;
    }
    XHostAddress_deinit_base(&serverAddr);
    
    XPrintf("[UDP服务器] 已绑定到8888端口\n");
    
    // 连接 readyRead 信号
    XObject_connect_2(g_udpServer, XSignal(XIODevice_readyRead_signal), onReadyReadDatagram);
    
    // 创建客户端 UDP 套接字
    g_udpClient = XUdpSocket_create();
    if (!g_udpClient) {
        XPrintf("[错误] 创建UDP客户端套接字失败\n");
        XUdpSocket_abort(g_udpServer);
        XUdpSocket_deleteLater(g_udpServer);
        return;
    }
    
    // 绑定客户端到任意端口
    if (!XUdpSocket_bindAny(g_udpClient, 0, XAbstractSocket_DefaultForPlatform)) {
        XPrintf("[错误] 绑定UDP客户端失败\n");
        XUdpSocket_abort(g_udpClient);
        XUdpSocket_abort(g_udpServer);
        XUdpSocket_deleteLater(g_udpClient);
        XUdpSocket_deleteLater(g_udpServer);
        return;
    }
    
    XPrintf("[UDP客户端] 已绑定到本地端口 %u\n", XUdpSocket_localPort(g_udpClient));
    
    // 设置目标地址（本地回环）
    XHostAddress destAddr;
    XHostAddress_init(&destAddr);
    XHostAddress_setAddressSpecial(&destAddr, XHostAddress_LocalHostSpecial);
    
    // 发送多条消息
    const char* messages[] = {
        "Hello UDP Server!",
        "This is a test message.",
        "UDP communication works!",
        "Goodbye!"
    };
    
    for (int i = 0; i < 4; i++) {
        XPrintf("[UDP客户端] 发送: %s\n", messages[i]);
        
        int64_t sent = XUdpSocket_writeDatagram(g_udpClient, 
                                                 messages[i], 
                                                 strlen(messages[i]),
                                                 &destAddr, 
                                                 8888);
        
        if (sent < 0) {
            XPrintf("[错误] 发送数据报失败\n");
        } else {
            XPrintf("[UDP客户端] 已发送 %lld 字节\n", (long long)sent);
        }
        
        // 处理事件
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    
    // 等待并处理事件
    XHostAddress_deinit_base(&destAddr);
    XPrintf("\n[信息] 处理事件2秒...\n");
    for (int i = 0; i < 20; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(100);
    }
    
    // 清理
    XUdpSocket_abort(g_udpClient);
    XUdpSocket_abort(g_udpServer);
    XUdpSocket_deleteLater(g_udpClient);
    XUdpSocket_deleteLater(g_udpServer);
    
    XPrintf("========== UDP 基本测试完成 ==========\n\n");
}

/**
 * @brief UDP 广播测试
 */
static void XUdpSocketBroadcastTest(void)
{
    XPrintf("\n========== UDP 广播测试 ==========\n");
    
    // 创建接收端
    XUdpSocket* receiver = XUdpSocket_create();
    if (!receiver) {
        XPrintf("[错误] 创建接收端套接字失败\n");
        return;
    }
    
    // 绑定到任意地址
    XHostAddress anyAddr;
    XHostAddress_init(&anyAddr);
    XHostAddress_setAddressSpecial(&anyAddr, XHostAddress_AnySpecial);
    
    if (!XUdpSocket_bind_base(receiver, &anyAddr, 9999, XAbstractSocket_DefaultForPlatform)) {
        XPrintf("[错误] 绑定接收端失败\n");
        XHostAddress_deinit_base(&anyAddr);
        XUdpSocket_abort(receiver);
        XUdpSocket_deleteLater(receiver);
        return;
    }
    XHostAddress_deinit_base(&anyAddr);
    
    XPrintf("[UDP接收端] 监听端口9999\n");
    XObject_connect_2(receiver, XSignal(XIODevice_readyRead_signal), onReadyRead);
    
    // 创建发送端
    XUdpSocket* sender = XUdpSocket_create();
    if (!sender) {
        XPrintf("[错误] 创建发送端套接字失败\n");
        XUdpSocket_abort(receiver);
        XUdpSocket_deleteLater(receiver);
        return;
    }
    
    // 绑定发送端
    XUdpSocket_bindAny(sender, 0, XAbstractSocket_DefaultForPlatform);
    
    // 启用广播选项
    XVariant* broadcastEnabled = XVariant_create_bool(true);
    XUdpSocket_setSocketOption_base(sender, XAbstractSocket_BroadcastOption, broadcastEnabled);
    XVariant_delete_base(broadcastEnabled);
    
    // 设置广播地址
    XHostAddress broadcastAddr;
    XHostAddress_init(&broadcastAddr);
    XHostAddress_setAddressSpecial(&broadcastAddr, XHostAddress_BroadcastSpecial);
    
    // 发送广播消息
    const char* msg = "Broadcast message from XUdpSocket!";
    XPrintf("[UDP发送端] 广播: %s\n", msg);
    
    int64_t sent = XUdpSocket_writeDatagram(sender, msg, strlen(msg), &broadcastAddr, 9999);
    XHostAddress_deinit_base(&broadcastAddr);
    XPrintf("[UDP发送端] 已发送 %lld 字节到广播地址\n", (long long)sent);
    
    // 处理事件
    for (int i = 0; i < 10; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(100);
    }
    
    // 清理
    XUdpSocket_abort(sender);
    XUdpSocket_abort(receiver);
    XUdpSocket_deleteLater(sender);
    XUdpSocket_deleteLater(receiver);
    
    XPrintf("========== UDP 广播测试完成 ==========\n\n");
}

/**
 * @brief UDP 多播测试
 */
static void XUdpSocketMulticastTest(void)
{
    XPrintf("\n========== UDP 多播测试 ==========\n");
    
    // 多播组地址
    const char* multicastGroup = "239.255.0.1";
    uint16_t multicastPort = 50000;
    
    // 创建多播接收端
    XUdpSocket* multicastReceiver = XUdpSocket_create();
    if (!multicastReceiver) {
        XPrintf("[错误] 创建多播接收端失败\n");
        return;
    }
    
    // 绑定到多播端口
    XHostAddress anyAddr;
    XHostAddress_init(&anyAddr);
    XHostAddress_setAddressSpecial(&anyAddr, XHostAddress_AnyIPv4Special);
    
    if (!XUdpSocket_bind_base(multicastReceiver, &anyAddr, multicastPort, 
                              XAbstractSocket_ShareAddress)) {
        XPrintf("[错误] 绑定多播接收端失败\n");
        XHostAddress_deinit_base(&anyAddr);
        XUdpSocket_abort(multicastReceiver);
        XUdpSocket_deleteLater(multicastReceiver);
        return;
    }
    XHostAddress_deinit_base(&anyAddr);
    
    XPrintf("[多播接收端] 已绑定到端口 %u\n", multicastPort);
    
    // 加入多播组
    XHostAddress groupAddr;
    XHostAddress_init(&groupAddr);
    XHostAddress_setAddress(&groupAddr, multicastGroup);
    
    if (XUdpSocket_joinMulticastGroup(multicastReceiver, &groupAddr)) {
        XPrintf("[多播接收端] 已加入多播组 %s\n", multicastGroup);
    } else {
        XPrintf("[错误] 加入多播组失败\n");
        XHostAddress_deinit_base(&groupAddr);
        XUdpSocket_abort(multicastReceiver);
        XUdpSocket_deleteLater(multicastReceiver);
        return;
    }
    
    // 连接信号
    XObject_connect_2(multicastReceiver, XSignal(XIODevice_readyRead_signal), onReadyRead);
    
    // 创建多播发送端
    XUdpSocket* multicastSender = XUdpSocket_create();
    if (!multicastSender) {
        XPrintf("[错误] 创建多播发送端失败\n");
        XUdpSocket_leaveMulticastGroup(multicastReceiver, &groupAddr);
        XHostAddress_deinit_base(&groupAddr);
        XUdpSocket_abort(multicastReceiver);
        XUdpSocket_deleteLater(multicastReceiver);
        return;
    }
    
    // 绑定发送端
    XUdpSocket_bindAny(multicastSender, 0, XAbstractSocket_DefaultForPlatform);
    
    // 设置多播 TTL
    XAbstractSocket_SocketOption opt = XAbstractSocket_MulticastTtlOption;
    XVariant* ttlValue = XVariant_create_int(1);  // TTL = 1 (本地网络)
    XUdpSocket_setSocketOption_base(multicastSender, opt, ttlValue);
    XVariant_delete_base(ttlValue);
    
    XPrintf("[多播发送端] 准备发送到 %s:%u\n", multicastGroup, multicastPort);
    
    // 发送多播消息
    const char* msg = "Multicast message from XUdpSocket!";
    XPrintf("[多播发送端] 发送: %s\n", msg);
    
    int64_t sent = XUdpSocket_writeDatagram(multicastSender, msg, strlen(msg), &groupAddr, multicastPort);
    XPrintf("[多播发送端] 已发送 %lld 字节\n", (long long)sent);
    
    // 处理事件
    for (int i = 0; i < 10; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(100);
    }
    
    // 离开多播组
    XUdpSocket_leaveMulticastGroup(multicastReceiver, &groupAddr);
    XHostAddress_deinit_base(&groupAddr);
    XPrintf("[多播接收端] 已离开多播组\n");
    
    // 清理
    XUdpSocket_abort(multicastSender);
    XUdpSocket_abort(multicastReceiver);
    XUdpSocket_deleteLater(multicastSender);
    XUdpSocket_deleteLater(multicastReceiver);
    
    XPrintf("========== UDP 多播测试完成 ==========\n\n");
}

// ==================== 菜单注册 ====================

/**
 * @brief 交互式 UDP 客户端测试
 */
static void XUdpSocketClientTest(void)
{
    XPrintf("\n========== UDP 客户端测试 ==========\n");
    
    // 输入服务器地址
    char serverIp[64] = {0};
    uint16_t serverPort = 0;
    
    XPrintf("请输入服务器IP地址(直接回车使用127.0.0.1): ");
    if (scanf("%s",serverIp) == NULL || serverIp[0] == '\n') {
        strcpy(serverIp, "127.0.0.1");
    }
    // 去除换行符
    size_t len = strlen(serverIp);
    if (len > 0 && serverIp[len - 1] == '\n') {
        serverIp[len - 1] = '\0';
    }
    
    XPrintf("请输入服务器端口(直接回车使用8888): ");
    if (scanf("%d",&serverPort) == NULL ) {
        serverPort = 8888;
    } 
    XPrintf("目标服务器: %s:%u\n", serverIp, serverPort);
    
    // 创建 UDP 套接字
    XUdpSocket* client = XUdpSocket_create();
    if (!client) {
        XPrintf("[错误] 创建UDP套接字失败\n");
        return;
    }
    // 绑定到任意端口
    if (!XUdpSocket_bindAny(client, 6653, XAbstractSocket_DefaultForPlatform)) {
        XPrintf("[错误] 绑定端口失败\n");
        XUdpSocket_abort(client);
        XUdpSocket_deleteLater(client);
        return;
    }
    
    XPrintf("[UDP客户端] 已绑定到本地端口 %u\n", XUdpSocket_localPort(client));
    
    // 设置目标地址
    XHostAddress serverAddr;
    XHostAddress_init(&serverAddr);
    XHostAddress_setAddress(&serverAddr, serverIp);
    
    // 连接接收信号
    XObject_connect_2(client, XSignal(XIODevice_readyRead_signal), onReadyRead);
    
    XPrintf("\n========== 开始通信 (输入 quit 退出) ==========\n");
    
    char input[1024] = {0};
    while (1) {
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
            XPrintf("退出客户端...\n");
            break;
        }
        
        // 空输入跳过
        if (len == 0) {
            continue;
        }
        
        // 发送数据
        int64_t sent = XUdpSocket_writeDatagram(client, input, len, &serverAddr, serverPort);
        if (sent < 0) {
            XPrintf("[错误] 发送失败\n");
        } else {
            XPrintf("[发送] %lld 字节\n", (long long)sent);
        }
        
        // 处理事件（接收可能的回复）
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    
    // 清理
    XHostAddress_deinit_base(&serverAddr);
    XUdpSocket_abort(client);
    XUdpSocket_deleteLater(client);
    
    XPrintf("========== UDP 客户端测试完成 ==========\n\n");
}

void XMenu_XUdpSocketTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XUdpSocket(UDP套接字)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "基本收发测试");
        XAction_setAction(action, XUdpSocketBasicTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "广播测试");
        XAction_setAction(action, XUdpSocketBroadcastTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "多播测试");
        XAction_setAction(action, XUdpSocketMulticastTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "UDP客户端");
        XAction_setAction(action, XUdpSocketClientTest);
    }
}