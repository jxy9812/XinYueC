#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_TCP_ON
#include "XModbusTcpServer.h"
#include "XModbusServer_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XHashMap.h"
#include "XSignalSlot.h"
#include "XMap.h"
#include "XString.h"
#include <string.h>
#include <errno.h>
#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/socket.h> /* [死链巡检下沉 2026-10-29] MSG_PEEK 假 EOF 复核探测。 */
#endif

// =============== 虚函数前置声明 ================
static bool VXModbusTcpServer_open(XModbusDevice* device);
static void VXModbusTcpServer_close(XModbusDevice* device);
static void VXModbusTcpServer_deinit(XModbusTcpServer* server);
static XModbusResponse* VXModbusTcpServer_processRequest(XModbusServer* server, const XModbusRequest* request);

// =============== 槽函数前置声明 ================
static void XModbusTcpServer_onNewConnection(XObject* receiver, XVarList* args);
static void XModbusTcpServer_onClientDisconnected(XObject* receiver, XVarList* args);
static void XModbusTcpServer_onClientReadyRead(XObject* receiver, XVarList* args);
static void XModbusTcpServer_onSweepTimer(void* userData, XTimerData* timer);

static void XModbusTcpServer_deinitClientBuffer(void* value)
{
    XByteArray** buffer = (XByteArray**)value;
    if (buffer && *buffer) {
        XClassDelete(*buffer);
        *buffer = NULL;
    }
}

// 释放断开/被拒绝的客户端socket：XTcpServer_incomingConnection 将其挂为服务器子对象，
// 只 close 不删除会随断连逐个累积，直到服务器销毁才回收；此处关闭、解除父子关系并延迟销毁
static void XModbusTcpServer_releaseClientSocket(XTcpSocket* client)
{
    if (!client) return;
    XTcpSocket_close_base((XIODevice*)client);
    XObject_setParent((XObject*)client, NULL);
    XObject_deleteLater((XObject*)client);
}

// =============== 字节序辅助函数 ================
static inline uint16_t readUint16BE(const uint8_t* data, size_t offset)
{
    uint16_t value;
    XMemory_read_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&value, sizeof(uint16_t));
    return value;
}

static inline void writeUint16BE(uint8_t* data, size_t offset, uint16_t value)
{
    XMemory_write_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&value, sizeof(uint16_t));
}

// =============== MBAP头部处理 ================
/**
 * @brief 解析MBAP头部
 */
static inline void parseMbapHeader(const uint8_t* buffer,
    uint16_t* transactionId, uint16_t* protocolId,
    uint16_t* length, uint8_t* unitId)
{
    *transactionId = readUint16BE(buffer, 0);
    *protocolId = readUint16BE(buffer, 2);
    *length = readUint16BE(buffer, 4);
    *unitId = buffer[6];
}

/**
 * @brief 构建MBAP头部
 */
static inline void buildMbapHeader(uint8_t* buffer, uint16_t transactionId,
    uint16_t length, uint8_t unitId)
{
    writeUint16BE(buffer, 0, transactionId);
    writeUint16BE(buffer, 2, 0x0000);
    writeUint16BE(buffer, 4, length);
    buffer[6] = unitId;
}

// =============== 辅助函数 ================
/**
 * @brief 处理接收到的完整TCP ADU帧
 */
static void processTcpFrame(XModbusTcpServer* server, XTcpSocket* client, const uint8_t* frame, size_t frameLen)
{
    if (!server || !client || !frame || frameLen < 8) return;

    // 解析MBAP头部
    uint16_t transactionId, protocolId, length;
    uint8_t unitId;
    parseMbapHeader(frame, &transactionId, &protocolId, &length, &unitId);

    // 检查协议标识符（必须是Modbus）
    if (protocolId != 0x0000) return;

    // 提取PDU（从偏移7开始）
    size_t pduLen = frameLen - 7;
    if (pduLen < 1) return;

    // 创建请求PDU
    XModbusRequest* request = XModbusRequest_create();
    if (!request) return;

    XModbusPdu_setFunctionCode(request, (XModbusPdu_FunctionCode)frame[7]);
    if (pduLen > 1) {
        XModbusPdu_setData(request, frame + 8, pduLen - 1);
    }

    // 通过虚函数调用processRequest
    XModbusResponse* response = XClass_Parent(XModbusServer,
        EXModbusServer_ProcessRequest,
        XModbusResponse* (*)(XModbusServer*, const XModbusRequest*))(
            (XModbusServer*)server, request);

    // 如果有响应，发送回客户端
    if (response) {
        // 构建响应帧：MBAP头部 + PDU
        XByteArray* respPdu = ((XModbusPdu*)response)->m_data;
        size_t respPduSize = XByteArray_size_base(respPdu);
        size_t respFrameSize = 7 + 1 + respPduSize; // MBAP(7) + FC(1) + data

        uint8_t* respFrame = (uint8_t*)XMalloc_System(respFrameSize);
        if (respFrame) {
            buildMbapHeader(respFrame, transactionId, (uint16_t)(2 + respPduSize), unitId);
            respFrame[7] = (uint8_t)XModbusPdu_functionCode(&response->m_base);
            if (respPduSize > 0) {
                memcpy(respFrame + 8, XContainerDataAddr(respPdu), respPduSize);
            }

            // 发送响应
            XTcpSocket_write_1((XTcpSocket*)client, (const char*)respFrame, respFrameSize);
            XFree_System(respFrame);
        }

        XClassDelete(response);
    }

    XClassDelete(request);
}

// =============== 类初始化 ================
XVtable* XModbusTcpServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusTcpServer)
    // 继承 XModbusServer
    XVTABLE_INHERIT_XCLASS(XModbusServer);

    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusTcpServer_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusTcpServer_close);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessRequest, VXModbusTcpServer_processRequest);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusTcpServer_deinit);

	XCLASS_SHOW_SIZE(XModbusTcpServer, sizeof(XModbusTcpServer));
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ================
XModbusTcpServer* XModbusTcpServer_create_ex(XMemoryType memory)
{
    XModbusTcpServer* server = (XModbusTcpServer*)XMemory_malloc(sizeof(XModbusTcpServer), memory);
    if (!server) return NULL;
    XModbusTcpServer_init(server);
    Set_Class_Memory(server, memory); Set_Class_IsHeap(server, true);
    return server;
}

void XModbusTcpServer_init(XModbusTcpServer* server)
{
    if (!server) return;

    // 初始化基类
    XModbusServer_init((XModbusServer*)server);
    XClassGetVtable(server) = XModbusTcpServer_class_init();

    // 创建TCP服务器
    server->m_tcpServer = XTcpServer_create();
    if (server->m_tcpServer) {
        // 连接 newConnection 信号
        XObject_connect_1((XObject*)server->m_tcpServer,
            XSignal(XTcpServer_newConnection_signal),
        (XObject*)server,
        XModbusTcpServer_onNewConnection,
        XConnectionType_Auto);
    }

    // 创建客户端映射
    server->m_connectedClients = XMap_Create(XTcpSocket*, XByteArray*, Xuintptr_t_compare);
    if (server->m_connectedClients) {
        XContainerSetDataDeinitMethod(server->m_connectedClients,
            XModbusTcpServer_deinitClientBuffer);
    }

    server->m_observer = NULL;

    // [死链巡检下沉 2026-10-29] 内置 1s 巡检定时器（open 启动/close 停止；
    // 挂为服务器子对象随父子链级联析构；回调自守卫，无客户端表即零开销）。
    server->m_sweepTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (server->m_sweepTimer) {
        XObject_setParent((XObject*)server->m_sweepTimer, (XObject*)server);
        XTimer_setInterval(server->m_sweepTimer, 1000);
        XTimer_setTimerCallback(server->m_sweepTimer,
                                XModbusTcpServer_onSweepTimer);
        XTimer_setUserData(server->m_sweepTimer, server);
    }
}

// =============== 析构函数 ================
static void VXModbusTcpServer_deinit(XModbusTcpServer* server)
{
    if (!server) return;

    // [死链巡检下沉 2026-10-29] 先停内置巡检定时器（防析构中途再触发）；
    // 定时器为服务器子对象，随父子链级联析构，此处不重复删除。
    if (server->m_sweepTimer) {
        XTimer_stop_base(server->m_sweepTimer);
        server->m_sweepTimer = NULL;
    }

    // 关闭TCP服务器
    if (server->m_tcpServer) {
        XTcpServer_close(server->m_tcpServer);
        XTcpServer_deleteLater(server->m_tcpServer);
        server->m_tcpServer = NULL;
    }

    // 释放客户端映射
    if (server->m_connectedClients) {
        XClassDelete(server->m_connectedClients);
        server->m_connectedClients = NULL;
    }

    server->m_observer = NULL;

    // 调用基类析构（释放XModbusServer的m_dataMap/m_options、XModbusDevice层与XObject层资源）
    XClass_Deinit_Parent(XModbusServer, server);
}

// =============== Open/Close ================
static bool VXModbusTcpServer_open(XModbusDevice* device)
{
    XModbusTcpServer* server = (XModbusTcpServer*)device;
    if (!server || !server->m_tcpServer) return false;

    // 从连接参数中获取端口和地址
    const XVariant* portVar = XModbusDevice_connectionParameter_const(device,
        XModbusDevice_NetworkPortParameter);
    const XVariant* addrVar = XModbusDevice_connectionParameter_const(device,
        XModbusDevice_NetworkAddressParameter);

    uint16_t port = 502; // 默认Modbus TCP端口
    XHostAddress* addr = NULL;
    if (portVar) {
        port = (uint16_t)XVariant_toInt(portVar);
    }
    if (addrVar) {
        XString* addrStr = XVariant_toString(addrVar);
        if (addrStr) {
            addr=XHostAddress_create_fromString(XString_constData(addrStr));
            XClassDelete(addrStr);
        }
    }

    // 开始监听
    bool result = XTcpServer_listen(server->m_tcpServer, addr, port);
    if (result) {
        XModbusDevice_setState(device, XModbusDevice_ConnectedState);
        // [死链巡检下沉 2026-10-29] 监听成功即启动内置死链巡检
        // （posix 探测通道；Windows 未内置探测，定时器不启动）。
#if !defined(_WIN32)
        if (server->m_sweepTimer) XTimer_start_base(server->m_sweepTimer);
#endif
    } else {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError,
            "Failed to start TCP server");
    }
    if(addr)
        XClassDelete(addr);
    return result;
}

static void VXModbusTcpServer_close(XModbusDevice* device)
{
    XModbusTcpServer* server = (XModbusTcpServer*)device;
    if (!server) return;

    // [死链巡检下沉 2026-10-29] 先停巡检定时器（防清表过程中再触发）。
    if (server->m_sweepTimer) {
        XTimer_stop_base(server->m_sweepTimer);
    }

    // 断开所有客户端
    if (server->m_connectedClients) {
        XMap_clear_base(server->m_connectedClients);
    }

    // 关闭TCP服务器
    if (server->m_tcpServer) {
        XTcpServer_close(server->m_tcpServer);
    }

    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

// =============== 处理请求 ================
static XModbusResponse* VXModbusTcpServer_processRequest(XModbusServer* base, const XModbusRequest* request)
{
    // TCP服务器默认委托给基类处理
    // 子类可以重写此函数实现自定义逻辑
    return XClass_Parent(XModbusServer, EXModbusServer_ProcessRequest,
        XModbusResponse* (*)(XModbusServer*, const XModbusRequest*))(base, request);
}

// =============== 连接观察器 ================
void XModbusTcpServer_installConnectionObserver(XModbusTcpServer* server,
    XModbusTcpConnectionObserver* observer)
{
    if (!server) return;
    server->m_observer = observer;
}

// =============== 槽函数 ================

/**
 * @brief 处理新连接
 */
static void XModbusTcpServer_onNewConnection(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusTcpServer* server = (XModbusTcpServer*)receiver;
    if (!server || !server->m_tcpServer) return;

    // 获取所有待处理连接
    while (XTcpServer_hasPendingConnections_base(server->m_tcpServer)) {
        XTcpSocket* client = XTcpServer_nextPendingConnection_base(server->m_tcpServer);
        if (!client) continue;

        // 检查观察器是否允许连接
        if (server->m_observer && server->m_observer->acceptNewConnection) {
            if (!server->m_observer->acceptNewConnection(server->m_observer->context, client)) {
                XModbusTcpServer_releaseClientSocket(client);
                continue;
            }
        }

        // 创建接收缓冲区
        XByteArray* buffer = XByteArray_create();
        if (!buffer) {
            XModbusTcpServer_releaseClientSocket(client);
            continue;
        }

        // 保存到客户端映射
        XMap_insert_valueMove_base(server->m_connectedClients, &client, &buffer);
        // 连接信号
        XObject_connect_1((XObject*)client,
            XSignal(XTcpSocket_readyRead_signal),
        (XObject*)server,
        XModbusTcpServer_onClientReadyRead,
        XConnectionType_Auto);

        XObject_connect_1((XObject*)client,
            XSignal(XTcpSocket_disconnected_signal),
        (XObject*)server,
        XModbusTcpServer_onClientDisconnected,
        XConnectionType_Auto);
    }
}

/**
 * @brief 处理客户端数据到达
 */
static void XModbusTcpServer_onClientReadyRead(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusTcpServer* server = (XModbusTcpServer*)receiver;
    if (!server || !server->m_connectedClients) return;

    // 获取客户端socket（从信号发送者获取）
    XObject* sender = XObject_sender((XObject*)server);
    XTcpSocket* client = (XTcpSocket*)sender;
    if (!client) return;

    // 获取对应的接收缓冲区
    XByteArray** bufferSlot = (XByteArray**)XMapBase_value_base(
        (XMapBase*)server->m_connectedClients, &client);
    XByteArray* buffer = bufferSlot ? *bufferSlot : NULL;
    if (!buffer) return;

    // 读取数据
    char tempBuf[4096];
    int64_t bytesRead = XTcpSocket_read_1(client, tempBuf, sizeof(tempBuf));
    while (bytesRead > 0) {
        XByteArray_push_back_2(buffer, (const uint8_t*)tempBuf, (size_t)bytesRead);
        bytesRead = XTcpSocket_read_1(client, tempBuf, sizeof(tempBuf));
    }

    // 处理完整帧
    // Modbus TCP最小帧长度：MBAP头部(7) + PDU(至少1字节功能码)
    const uint8_t* data = XContainerDataAddr(buffer);
    size_t dataSize = XByteArray_size_base(buffer);

    while (dataSize >= 8) {
        // 解析MBAP头部获取长度
        uint16_t length = readUint16BE(data, 4);
        size_t totalFrameSize = 6 + length; // MBAP(6) + length字段指定的字节数

        if (dataSize < totalFrameSize) {
            break; // 数据不完整，等待更多数据
        }

        // 如果有观察器，通过观察器检查
        if (server->m_observer && server->m_observer->acceptNewConnection) {
            if (!server->m_observer->acceptNewConnection(server->m_observer->context, client)) {
                // 观察器拒绝处理，跳过
                XByteArray_remove_base(buffer, 0, totalFrameSize);
                data = XContainerDataAddr(buffer);
                dataSize = XByteArray_size_base(buffer);
                continue;
            }
        }

        // 处理完整帧
        processTcpFrame(server, client, data, totalFrameSize);

        // 移除已处理的数据
        XByteArray_remove_base(buffer, 0, totalFrameSize);
        data = XContainerDataAddr(buffer);
        dataSize = XByteArray_size_base(buffer);
    }
}

/**
 * @brief 处理客户端断开连接
 */
static void XModbusTcpServer_onClientDisconnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusTcpServer* server = (XModbusTcpServer*)receiver;
    if (!server || !server->m_connectedClients) return;

    // 获取断开的客户端socket
    XObject* sender = XObject_sender((XObject*)server);
    XTcpSocket* client = (XTcpSocket*)sender;
    if (!client) return;

    // 从映射中移除
    XMapBase_remove_base((XMapBase*)server->m_connectedClients, &client);

    // 断开信号连接
    XObject_disconnect_1((XObject*)client, 0, (XObject*)server, NULL);

    // 先发射断开信号（观察者仍可见socket），再释放socket；
    // 处于信号发射链上，必须用延迟删除，避免其他连接方访问已释放内存
    XModbusTcpServer_modbusClientDisconnected_signal(server, client);
    XModbusTcpServer_releaseClientSocket(client);
}

// =============== 死链巡检（[下沉 2026-10-29] CLOSE-WAIT/半开回收） ===============
// 对端 FIN 的 EOF 在 IO 环→XAbstractSocket 事件层被吞（0 字节读只
// ContinueRead，不发 readyRead/disconnected/不置错），服务端
// onClientDisconnected 永不触发 ⇒ accepted fd 滞留 CLOSE-WAIT。探测手法
// 与 posix 平台「假 EOF 复核」同款（XDeviceNetwork_posix.c）：对裸 fd 做
// recv(MSG_PEEK|MSG_DONTWAIT)——不消费字节，与 IO 环在途读无竞态：
// pr>0=有数据待读（活链，交正常 readyRead 路径）；pr<0 且
// EAGAIN/EWOULDBLOCK=连接存活空闲；pr==0=对端真 FIN（回收）；其余硬错误
// （ECONNRESET/ENOTSOCK/EBADF 等，含已 close 残留表项）一并回收。回收
// 动作=onClientDisconnected+releaseClientSocket 同语义：摘映射表
// （return true 交 removeIf）→断本 socket 到服务器信号→发
// modbusClientDisconnected（公开契约保持）→close+脱父链+deleteLater。
#if !defined(_WIN32)
static bool XModbusTcpServer_deadClientPred(const void* pvKey, void* pvValue,
                                            void* args)
{
    XTcpSocket* client = *(XTcpSocket* const*)pvKey;
    XModbusTcpServer* server = (XModbusTcpServer*)args;
    intptr_t fd;
    ssize_t pr;
    unsigned char b;
    (void)pvValue;
    if (!client || !server) return false;
    fd = XAbstractSocket_socketDescriptor_base((XAbstractSocket*)client);
    if (fd <= 0) return false; /* 未持描述符（理论不达），保守不回收。 */
    pr = recv((int)fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (pr > 0) return false;              /* 有数据：活链，交正常路径。 */
    if (pr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return false;                      /* 空闲活链。 */
    /* 真 FIN（pr==0）或硬错误：回收。 */
    XObject_disconnect_1((XObject*)client, 0, (XObject*)server, NULL);
    XModbusTcpServer_modbusClientDisconnected_signal(server, client);
    XModbusTcpServer_releaseClientSocket(client);
    return true;
}
#endif /* !_WIN32 */

void XModbusTcpServer_sweepDeadClients(XModbusTcpServer* server)
{
    if (!server || !server->m_connectedClients) return;
#if !defined(_WIN32)
    XMapBase_removeIf_base((XMapBase*)server->m_connectedClients,
                           XModbusTcpServer_deadClientPred, server);
#endif
}

/** @brief 内置死链巡检定时器槽（1s 周期；open 启动/close 停止）。 */
static void XModbusTcpServer_onSweepTimer(void* userData, XTimerData* timer)
{
    (void)timer;
    XModbusTcpServer_sweepDeadClients((XModbusTcpServer*)userData);
}

// =============== 信号 ================
void* XModbusTcpServer_modbusClientDisconnected_signal(XModbusTcpServer* server, XTcpSocket* modbusClient)
{
    XEmitSignal(server, XModbusTcpServer_modbusClientDisconnected_signal,
        XVarList_Create(XVar(XTcpSocket*, modbusClient)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XMODBUS_TCP_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
