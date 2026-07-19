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

// =============== 虚函数前置声明 ================
static bool VXModbusTcpServer_open(XModbusDevice* device);
static void VXModbusTcpServer_close(XModbusDevice* device);
static void VXModbusTcpServer_deinit(XModbusTcpServer* server);
static XModbusResponse* VXModbusTcpServer_processRequest(XModbusServer* server, const XModbusRequest* request);

// =============== 槽函数前置声明 ================
static void XModbusTcpServer_onNewConnection(XObject* receiver, XVarList* args);
static void XModbusTcpServer_onClientDisconnected(XObject* receiver, XVarList* args);
static void XModbusTcpServer_onClientReadyRead(XObject* receiver, XVarList* args);

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
    XModbusResponse* response = XModbusServer_processRequest_base((XModbusServer*)server, request);

    // 如果有响应，发送回客户端
    if (response) {
        // 构建响应帧：MBAP头部 + PDU
        XByteArray* respPdu = ((XModbusPdu*)response)->m_data;
        size_t respPduSize = XByteArray_size_base(respPdu);
        size_t respFrameSize = 7 + 1 + respPduSize; // MBAP(7) + FC(1) + data

        uint8_t* respFrame = (uint8_t*)XMalloc_System(respFrameSize);
        if (respFrame) {
            buildMbapHeader(respFrame, transactionId, (uint16_t)(1 + respPduSize), unitId);
            respFrame[7] = (uint8_t)XModbusPdu_functionCode(&response->m_base);
            if (respPduSize > 0) {
                memcpy(respFrame + 8, XContainerDataAddr(respPdu), respPduSize);
            }

            // 发送响应
            XTcpSocket_write_1((XTcpSocket*)client, (const char*)respFrame, respFrameSize);
            XFree_System(respFrame);
        }

        XModbusResponse_delete_base(response);
    }

    XModbusRequest_delete_base(request);
}

// =============== 类初始化 ================
XVtable* XModbusTcpServer_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusTcpServer))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承 XModbusServer
    XVTABLE_INHERIT_XCLASS(XModbusServer);

    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusTcpServer_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusTcpServer_close);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessRequest, VXModbusTcpServer_processRequest);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusTcpServer_deinit);

#if SHOWCONTAINERSIZE
    printf("XModbusTcpServer size: %zu\n", sizeof(XModbusTcpServer));
#endif
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ================
XModbusTcpServer* XModbusTcpServer_create(void)
{
    XModbusTcpServer* server = (XModbusTcpServer*)XMalloc_System(sizeof(XModbusTcpServer));
    if (!server) return NULL;
    XModbusTcpServer_init(server);
    Set_Class_MemoryFree(server, XFree_System);
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
    server->m_connectedClients = XMap_Create(XTcpSocket*, XByteArray*, uintptr_t_compare);
    if (server->m_connectedClients) {
        XContainerSetDataDeinitMethod(server->m_connectedClients, XByteArray_deinit_base);
    }

    server->m_observer = NULL;
}

// =============== 析构函数 ================
static void VXModbusTcpServer_deinit(XModbusTcpServer* server)
{
    if (!server) return;

    // 关闭TCP服务器
    if (server->m_tcpServer) {
        XTcpServer_close(server->m_tcpServer);
        XTcpServer_deleteLater(server->m_tcpServer);
        server->m_tcpServer = NULL;
    }

    // 释放客户端映射
    if (server->m_connectedClients) {
        // 断开所有客户端连接
        for_each_iterator(server->m_connectedClients, XMap, it)
        {
            XPair* pair = XMap_iterator_data(&it);
            XTcpSocket* socket = (XTcpSocket*)XPair_first(pair);
            if (socket) {
                XObject_disconnect_1((XObject*)socket, 0, (XObject*)server, NULL);
                XTcpSocket_close_base(socket);
            }
        }
        XMapBase_delete_base(server->m_connectedClients);
        server->m_connectedClients = NULL;
    }

    server->m_observer = NULL;
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
            XString_delete_base(addrStr);
        }
    }

    // 开始监听
    bool result = XTcpServer_listen(server->m_tcpServer, addr, port);
    if (result) {
        XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    } else {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError,
            "Failed to start TCP server");
    }
    if(addr)
        XHostAddress_delete_base(addr);
    return result;
}

static void VXModbusTcpServer_close(XModbusDevice* device)
{
    XModbusTcpServer* server = (XModbusTcpServer*)device;
    if (!server) return;

    // 断开所有客户端
    if (server->m_connectedClients) {
        for_each_iterator(server->m_connectedClients, XMap, it)
        {
            XPair* pair = XMap_iterator_data(&it);
            XTcpSocket* socket = (XTcpSocket*)XPair_first(pair);
            if (socket) {
                XObject_disconnect_1((XObject*)socket, 0, (XObject*)server, NULL);
                XTcpSocket_close_base(socket);
            }
        }
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
    return XModbusServer_processRequest_base(base, request);
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
                XTcpSocket_close_base(client);
                continue;
            }
        }

        // 创建接收缓冲区
        XByteArray* buffer = XByteArray_create();
        if (!buffer) {
            XTcpSocket_close_base(client);
            continue;
        }

        // 保存到客户端映射
        XMap_insert_valueMove_base(server->m_connectedClients, &client, buffer);
        if (buffer)
            XByteArray_delete_base(buffer);
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
    XObject* sender = XObject_sender(args);
    XTcpSocket* client = (XTcpSocket*)sender;
    if (!client) return;

    // 获取对应的接收缓冲区
    XByteArray* buffer = (XByteArray*)XMapBase_value_base(server->m_connectedClients, &client);
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
    XObject* sender = XObject_sender(args);
    XTcpSocket* client = (XTcpSocket*)sender;
    if (!client) return;

    // 从映射中移除
    XByteArray* buffer = (XByteArray*)XMapBase_remove_base(server->m_connectedClients, &client);
    if (buffer) {
        XByteArray_delete_base(buffer);
    }

    // 断开信号连接
    XObject_disconnect_1((XObject*)client, 0, (XObject*)server, NULL);

    // 发射断开信号
    XModbusTcpServer_modbusClientDisconnected_signal(server, client);
}

// =============== 信号 ================
void* XModbusTcpServer_modbusClientDisconnected_signal(XModbusTcpServer* server, XTcpSocket* modbusClient)
{
    XEmitSignal(server, XModbusTcpServer_modbusClientDisconnected_signal,
        XVarList_Create(XVar(XTcpSocket*, modbusClient)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
