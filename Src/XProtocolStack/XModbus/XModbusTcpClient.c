#include "XModbusTcpClient.h"
#include "XModbusTcpClient_Protected.h"
#include "XModbusClient_Protected.h"
#include "XModbusReply_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XHashMap.h"
#include "XTimer.h"
#include <string.h>

// =============== 虚函数前置声明 ===============
static bool VXModbusTcpClient_open(XModbusDevice* device);
static void VXModbusTcpClient_close(XModbusDevice* device);
static void VXModbusTcpClient_deinit(XModbusTcpClient* client);
static void VXModbusTcpClient_timerEvent(XObject* obj, XEventTimer* event);
static XModbusReply* VXModbusTcpClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress, XFuncParamType type);

// =============== 槽函数声明 ===============
static void XModbusTcpClient_onReadyRead(XObject* receiver, XVarList* args);
static void XModbusTcpClient_onConnected(XObject* receiver, XVarList* args);
static void XModbusTcpClient_onDisconnected(XObject* receiver, XVarList* args);
static void XModbusTcpClient_onErrorOccurred(XObject* receiver, XVarList* args);

// =============== 内部辅助结构体 ===============
/**
 * @brief TCP待处理请求结构体
 * @details 用于存储等待响应的请求信息
 */
typedef struct XModbusTcpPendingRequest {
    XModbusReply* reply;            ///< 关联的Reply对象
    uint16_t transactionId;         ///< 事务标识符
    uint8_t unitId;                 ///< 单元标识符
    uint8_t retryCount;             ///< 当前重试次数
    XTimerId timeoutTimer;          ///< 超时定时器ID
    //XByteArray* requestData;        ///< 请求数据（用于重试）
} XModbusTcpPendingRequest;

// =============== 字节序辅助函数 ===============
// 从大端序数据读取uint16
static inline uint16_t readUint16BE(const uint8_t* data, size_t offset)
{
    uint16_t value;
    XMemory_read_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&value, sizeof(uint16_t));
    return value;
}

// 写入uint16到大端序数据
static inline void writeUint16BE(uint8_t* data, size_t offset, uint16_t value)
{
    XMemory_write_data(data + offset, XBYTE_ORDER_BIG_ENDIAN, (const uint8_t*)&value, sizeof(uint16_t));
}

// =============== MBAP头部处理 ===============
/**
 * @brief 构建MBAP头部
 * @param buffer 输出缓冲区（至少7字节）
 * @param transactionId 事务标识符
 * @param length PDU长度（不含MBAP头部）
 * @param unitId 单元标识符
 */
static inline void buildMbapHeader(uint8_t* buffer, uint16_t transactionId, uint16_t length, uint8_t unitId)
{
    writeUint16BE(buffer, 0, transactionId);    // 事务标识符
    writeUint16BE(buffer, 2, 0x0000);           // 协议标识符（Modbus = 0）
    writeUint16BE(buffer, 4, (uint16_t)(length + 1)); // 长度（含单元标识符）
    buffer[6] = unitId;                         // 单元标识符
}

/**
 * @brief 解析MBAP头部
 * @param buffer 输入缓冲区
 * @param transactionId 输出事务标识符
 * @param protocolId 输出协议标识符
 * @param length 输出长度
 * @param unitId 输出单元标识符
 */
static inline void parseMbapHeader(const uint8_t* buffer, uint16_t* transactionId, uint16_t* protocolId, uint16_t* length, uint8_t* unitId)
{
    *transactionId = readUint16BE(buffer, 0);
    *protocolId = readUint16BE(buffer, 2);
    *length = readUint16BE(buffer, 4);
    *unitId = buffer[6];
}

// =============== 待处理请求管理 ===============
static XModbusTcpPendingRequest* createPendingRequest(XModbusReply* reply, uint16_t transactionId, uint8_t unitId, XByteArray* requestData)
{
    XModbusTcpPendingRequest* pending = (XModbusTcpPendingRequest*)XMalloc_System(sizeof(XModbusTcpPendingRequest));
    if (!pending) return NULL;

    pending->reply = reply;
    pending->transactionId = transactionId;
    pending->unitId = unitId;
    pending->timeoutTimer = XTIMER_INVALID_ID;
    //pending->requestData = requestData;
    pending->retryCount = 0;

    return pending;
}

static void deletePendingRequest(XModbusTcpPendingRequest* pending)
{
    if (!pending) return;

    // 不删除reply，由外部管理
   /* if (pending->requestData) {
        XByteArray_delete_base(pending->requestData);
    }*/
    XFree_System(pending);
}
// 用于容器自动释放的回调函数
static void pendingRequestDeinit(void* data)
{
    XModbusTcpPendingRequest** ppPending = (XModbusTcpPendingRequest**)data;
    if (ppPending && *ppPending) {
        XModbusTcpPendingRequest* pending = *ppPending;
        // 停止超时定时器
        if (pending->timeoutTimer != XTIMER_INVALID_ID) {
            // 注意：这里无法获取client指针，需要在其他地方处理
        }
        deletePendingRequest(pending);
    }
}
// =============== 辅助函数 ===============
static inline void timeoutTimerStop(XModbusTcpClient* client, XTimerId* timerId)
{
    if (*timerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)client, *timerId);
        *timerId = XTIMER_INVALID_ID;
    }
}

static inline XTimerId timeoutTimerStart(XModbusTcpClient* client, int timeout)
{
    return XObject_startTimer_ms((XObject*)client, timeout, XTimerType_CoarseTimer);
}
/**
 * @brief 构建并发送Modbus TCP请求（内部函数）
 * @param client TCP客户端
 * @param reply 关联的Reply对象
 * @param transactionId 事务ID
 * @param request 请求对象
 * @param serverAddress 服务器地址
 * @return 成功返回true
 */
static bool buildAndSendRequest(XModbusTcpClient* client, XModbusReply* reply,
    uint16_t transactionId, const XModbusRequest* request, int serverAddress)
{
    if (!client || !reply || !request) return false;

    XAbstractSocket* socket = (XAbstractSocket*)XModbusTcpClient_socket(client);
    if (!socket || !XTcpSocket_isOpen(socket)) return false;
    // 获取PDU数据
    XByteArray* pdu = ((XModbusPdu*)request)->m_data;
    size_t pduSize = XByteArray_size_base(pdu);
    const uint8_t* pduData = XContainerDataAddr(pdu);

    // 构建完整请求帧（MBAP头部 + PDU）
    size_t frameSize = 7 + pduSize;
    XByteArray* requestData = client->m_requestData;
    if (!requestData) return false;

    XByteArray_resize_base(requestData, frameSize);
    uint8_t* frameData = XContainerDataAddr(requestData);

    // 构建MBAP头部
    buildMbapHeader(frameData, transactionId, (uint16_t)pduSize, (uint8_t)serverAddress);

    // 复制PDU数据（功能码 + 数据）
    uint8_t fc = (uint8_t)XModbusPdu_functionCode((const XModbusPdu*)request);
    frameData[7] = fc;
    if (pduSize > 0) {
        memcpy(frameData + 8, pduData, pduSize);
    }

    // 发送数据
    int64_t sent = XIODevice_write_2((XIODevice*)socket, requestData);
    //XByteArray_delete_base(requestData);

    if (sent <= 0) return false;

    // 启动超时定时器
    int timeout = XModbusClient_timeout((XModbusClient*)client);
    XTimerId timerId = timeoutTimerStart(client, timeout);

    // 创建待处理请求
    XModbusTcpPendingRequest* pending = createPendingRequest(reply, transactionId, (uint8_t)serverAddress, NULL);
    if (!pending) {
        timeoutTimerStop(client, &timerId);
        return false;
    }
    pending->timeoutTimer = timerId;

    // 添加到待处理映射表
    XHashMap_insert_base(client->m_pendingRequests, &transactionId, &pending);
    // 添加到定时器反向映射
    XHashMap_insert_base(client->m_timerMap, &timerId, &transactionId);
    return true;
}
// =============== 类初始化 ===============
XVtable* XModbusTcpClient_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusTcpClient))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XModbusClient);

    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusTcpClient_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusTcpClient_close);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusTcpClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusTcpClient_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_SendRawRequest, VXModbusTcpClient_sendRawRequest);

#if SHOWCONTAINERSIZE
    printf("XModbusTcpClient vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XModbusTcpClient* XModbusTcpClient_create(void)
{
    XModbusTcpClient* client = XMalloc_System(sizeof(XModbusTcpClient));
    if (!client) return NULL;
    XModbusTcpClient_init(client);
    Set_Class_MemoryFree(client, XFree_System);
    return client;
}

void XModbusTcpClient_init(XModbusTcpClient* client)
{
    if (!client) return;

    XModbusClient_init(&client->m_base);
    XClassGetVtable(client) = XModbusTcpClient_class_init();

    client->m_transactionId = 0;
    client->m_pendingRequests = XHashMap_Create(uint16_t, XModbusTcpPendingRequest*, uint16_t_compare);
    XContainerSetDataDeinitMethod(client->m_pendingRequests, pendingRequestDeinit);
    client->m_timerMap = XHashMap_Create(XTimerId, uint16_t, size_t_compare);
    client->m_receiveBuffer = XByteArray_create();
    client->m_requestData = XByteArray_create();
}

// =============== TCP特有属性接口 ===============
XTcpSocket* XModbusTcpClient_socket(const XModbusTcpClient* client)
{
    if (!client) return NULL;
    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (!io) return NULL;
    return (XTcpSocket*)io;
}

uint16_t XModbusTcpClient_nextTransactionId(XModbusTcpClient* client)
{
    if (!client) return 0;

    // 事务ID从1开始，到65535后循环回1
    client->m_transactionId++;
    if (client->m_transactionId == 0) {
        client->m_transactionId = 1;
    }
    return client->m_transactionId;
}

bool XModbusTcpClient_hasPendingRequests(const XModbusTcpClient* client)
{
    if (!client || !client->m_pendingRequests) return false;
    return XHashMap_size_base(client->m_pendingRequests) > 0;
}

size_t XModbusTcpClient_pendingRequestCount(const XModbusTcpClient* client)
{
    if (!client || !client->m_pendingRequests) return 0;
    return XHashMap_size_base(client->m_pendingRequests);
}

// =============== 虚函数实现 ===============
static bool VXModbusTcpClient_open(XModbusDevice* device)
{
    if (!device) return false;

    XModbusTcpClient* client = (XModbusTcpClient*)device;
    XAbstractSocket* socket = (XAbstractSocket*)XModbusTcpClient_socket(client);
    if (!socket)
    {
        socket = XTcpSocket_create();
        XObject_setParent(socket, client);
    }
    if (socket) {
        ((XModbusDevice*)client)->m_ioDevice = (XIODevice*)socket;

        // 连接信号
        XObject_connect_1((XObject*)socket,
            XSignal(XIODevice_readyRead_signal),
            (XObject*)client,
            XModbusTcpClient_onReadyRead,
            XConnectionType_Auto);

        XObject_connect_1((XObject*)socket,
            XSignal(XTcpSocket_connected_signal),
            (XObject*)client,
            XModbusTcpClient_onConnected,
            XConnectionType_Auto);

        XObject_connect_1((XObject*)socket,
            XSignal(XTcpSocket_disconnected_signal),
            (XObject*)client,
            XModbusTcpClient_onDisconnected,
            XConnectionType_Auto);

        XObject_connect_1((XObject*)socket,
            XSignal(XAbstractSocket_errorOccurred_signal),
            (XObject*)client,
            XModbusTcpClient_onErrorOccurred,
            XConnectionType_Auto);
    }
    if (!socket) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "TCP socket not available");
        return false;
    }

    // 获取连接参数
    const char* hostName = NULL;
    char hostNameBuf[256] = { 0 };

    const XVariant* addrVar = XModbusDevice_connectionParameter_const(device, XModbusDevice_NetworkAddressParameter);
    if (addrVar) {
        XString* str = XVariant_toString_const(addrVar);
        if (str) {
            const char* utf8 = XString_toUtf8(str);
            if (utf8) {
                strncpy(hostNameBuf, utf8, sizeof(hostNameBuf) - 1);
                hostName = hostNameBuf;
            }
        }
    }

    if (!hostName || hostName[0] == '\0') {
        XModbusDevice_setError(device, XModbusDevice_ConfigurationError, "Server address not configured");
        return false;
    }

    uint16_t port = 502;
    const XVariant* portVar = XModbusDevice_connectionParameter_const(device, XModbusDevice_NetworkPortParameter);
    if (portVar) {
        port = (uint16_t)XVariant_toInt(portVar);
    }

    XModbusDevice_setState(device, XModbusDevice_ConnectingState);
    XModbusDevice_setError(device, XModbusDevice_NoError, NULL);

    
    XAbstractSocket_connectToHost_base(socket, hostName, port, XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);

    return true;
}

static void VXModbusTcpClient_close(XModbusDevice* device)
{
    if (!device) return;

    XModbusTcpClient* client = (XModbusTcpClient*)device;
    XAbstractSocket* socket = (XAbstractSocket*)XModbusTcpClient_socket(client);

    if (client->m_receiveBuffer) {
        XByteArray_clear_base(client->m_receiveBuffer);
    }

    // 先停止所有定时器
    if (client->m_pendingRequests) {
        for_each_iterator(client->m_pendingRequests, XHashMap, it)
        {
            XPair* pair = XHashMap_iterator_data(&it);
            XModbusTcpPendingRequest* pending = XPair_Second(pair, XModbusTcpPendingRequest*);
            if (pending) {
                if (pending->reply) {
                    XModbusReply_setError(pending->reply, XModbusDevice_ConnectionError, "Device closed");
                    XModbusReply_setState(pending->reply, XModbusReply_State_No_Started);
                }
            }
        }
        // 容器会自动调用 pendingRequestDeinit 释放元素
        XHashMap_clear_base(client->m_pendingRequests);
    }

    if (socket) {
        XAbstractSocket_disconnectFromHost_base(socket);
    }

    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

// =============== 发送请求 ===============

static XModbusReply* VXModbusTcpClient_sendRawRequest(XModbusClient* baseClient, const XModbusRequest* request, int serverAddress, XFuncParamType type)
{
    if (!baseClient || !request || serverAddress < 0 || serverAddress > 247) {
        return NULL;
    }

    XModbusTcpClient* client = (XModbusTcpClient*)baseClient;
    XModbusDevice* device = (XModbusDevice*)client;

    if (XModbusDevice_state(device) != XModbusDevice_ConnectedState) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Not connected");
        return NULL;
    }

    // 创建Reply对象
    XModbusReply* reply = NULL;
    if (type == XFuncParamType_Copy)
        reply = XModbusClient_createReply(baseClient, request, serverAddress);
    else if (type == XFuncParamType_Move)
        reply = XModbusClient_createReply_move(baseClient, request, serverAddress);
    else if (type == XFuncParamType_Ref)
        reply = XModbusClient_createReply_ref(baseClient, request, serverAddress);

    if (!reply) return NULL;

    // 获取事务ID
    uint16_t transactionId = XModbusTcpClient_nextTransactionId(client);

    XModbusReply_setState(reply, XModbusReply_State_Requesting);

    // 构建并发送请求
    //if (buildAndSendRequest(client, reply, newTransactionId, reply->m_request, reply->m_serverAddress))
    if (!buildAndSendRequest(client, reply, transactionId, reply->m_request, reply->m_serverAddress)) {
        XModbusReply_deleteLater(reply);
        return NULL;
    }
    XModbusReply_setState(reply, XModbusReply_State_Waiting);
    return reply;
}

// =============== 响应处理 ===============
static void processReceivedFrame(XModbusTcpClient* client)
{
    if (!client || !client->m_receiveBuffer) return;

    XByteArray* buffer = client->m_receiveBuffer;
    size_t bufLen = XByteArray_size_base(buffer);

    while (bufLen >= 7) {
        const uint8_t* data = XContainerDataAddr(buffer);

        uint16_t transactionId, protocolId, length;
        uint8_t unitId;
        parseMbapHeader(data, &transactionId, &protocolId, &length, &unitId);

        if (protocolId != 0x0000) {
            XByteArray_clear_base(buffer);
            return;
        }

        size_t expectedLen = 6 + length;
        if (bufLen < expectedLen) {
            return;
        }

        XModbusTcpPendingRequest** ppPending = (XModbusTcpPendingRequest**)XMapBase_value_base(client->m_pendingRequests, &transactionId);
        if (!ppPending || !*ppPending) {
            XByteArray_remove_base(buffer, 0, expectedLen);
            bufLen = XByteArray_size_base(buffer);
            continue;
        }

        XModbusTcpPendingRequest* pending = *ppPending;
        XModbusReply* reply = pending->reply;

        // 在停止超时定时器后
        timeoutTimerStop(client, &pending->timeoutTimer);

        // 从定时器反向映射中移除
        if (pending->timeoutTimer != XTIMER_INVALID_ID) {
            XHashMap_remove_base(client->m_timerMap, &pending->timeoutTimer);
        }
        XModbusReply_setState(reply, XModbusReply_State_Responding);

        if (unitId != pending->unitId) {
            XModbusReply_setError(reply, XModbusDevice_ResponseRequestMismatch, "Unit ID mismatch");
            XModbusReply_setState(reply, XModbusReply_State_Finished);
            XHashMap_remove_base(client->m_pendingRequests, &transactionId);
            //deletePendingRequest(pending);
            XByteArray_remove_base(buffer, 0, expectedLen);
            bufLen = XByteArray_size_base(buffer);
            continue;
        }

        const uint8_t* pduData = data + 7;
        size_t pduLen = length - 1;

        if (!reply->m_rawResult) {
            reply->m_rawResult = XModbusResponse_create();
        }

        XModbusResponse* response = reply->m_rawResult;
        uint8_t fc = pduData[0];

        if (fc & XMODBUS_PDU_EXCEPTION_BYTE) {
            XModbusPdu_setFunctionCode(response, (XModbusPdu_FunctionCode)(fc & 0x7F));
            if (pduLen >= 2) {
                uint8_t exceptionCode = pduData[1];
                XModbusPdu_setData(response, &exceptionCode, 1);
                XModbusReply_setError(reply, XModbusDevice_ProtocolError, "Modbus exception");
            }
        }
        else {
            XModbusPdu_setFunctionCode(response, (XModbusPdu_FunctionCode)fc);
            if (pduLen > 1) {
                XModbusPdu_setData(response, pduData + 1, pduLen - 1);
            }

            if (!reply->m_result) {
                reply->m_result = XModbusDataUnit_create();
            }
            XModbusClient_processResponse_base((XModbusClient*)client, response, reply->m_result);
        }

        XModbusReply_setState(reply, XModbusReply_State_Finished);
        //deletePendingRequest(pending);
        XHashMap_remove_base(client->m_pendingRequests, &transactionId);
        XByteArray_remove_base(buffer, 0, expectedLen);
        bufLen = XByteArray_size_base(buffer);
    }
}

// =============== 超时处理 ===============
static void handleRequestTimeout(XModbusTcpClient* client, uint16_t transactionId)
{
    if (!client || !client->m_pendingRequests) return;

    XModbusTcpPendingRequest** ppPending = (XModbusTcpPendingRequest**)XHashMap_value_base(client->m_pendingRequests, &transactionId);
    if (!ppPending || !*ppPending) return;

    XModbusTcpPendingRequest* pending = *ppPending;
    // 从定时器反向映射中移除
    if (pending->timeoutTimer != XTIMER_INVALID_ID) {
        XHashMap_remove_base(client->m_timerMap, &pending->timeoutTimer);
    }
    pending->timeoutTimer = XTIMER_INVALID_ID;

    // 尝试重试
    int maxRetries = XModbusClient_numberOfRetries((XModbusClient*)client);
    if (pending->retryCount < maxRetries && pending->reply) {
        XModbusReply* reply = pending->reply;
        
        // 检查连接状态
        XAbstractSocket* socket = (XAbstractSocket*)XModbusTcpClient_socket(client);
        if (socket && XTcpSocket_isOpen(socket) && reply->m_request) {
            pending->retryCount++;
            
            // 获取新的事务ID
            uint16_t newTransactionId = XModbusTcpClient_nextTransactionId(client);
            
            // 从Reply获取请求数据重新发送
            if (buildAndSendRequest(client, reply, newTransactionId, reply->m_request, reply->m_serverAddress)) {
                // 从映射表移除旧的事务ID
                XHashMap_remove_base(client->m_pendingRequests, &transactionId);
                return;
            }
        }
    }

    // 重试次数用尽或重试失败
    if (pending->reply) {
        XModbusReply_setError(pending->reply, XModbusDevice_TimeoutError, "Request timeout");
        XModbusReply_setState(pending->reply, XModbusReply_State_Timeout);
    }

    XHashMap_remove_base(client->m_pendingRequests, &transactionId);
    //deletePendingRequest(pending);
}

// =============== 定时器事件处理 ===============
static void VXModbusTcpClient_timerEvent(XObject* obj, XEventTimer* event)
{
    if (!obj || !event) goto parent_call;

    XEvent_accept(event);
    XModbusTcpClient* client = (XModbusTcpClient*)obj;
    XTimerId timerId = XEventTimer_timerId(event);

    // 通过反向映射快速查找（O(1)）
    uint16_t* pTransactionId = (uint16_t*)XHashMap_value_base(client->m_timerMap, &timerId);
    if (pTransactionId) {
        uint16_t transactionId = *pTransactionId;
        // 从反向映射中移除
        XHashMap_remove_base(client->m_timerMap, &timerId);
        handleRequestTimeout(client, transactionId);
        return;
    }
    //return;
    // 检查是否是轮询定时器
    if (client->m_base.m_poolMap) {
        XModbusReply** ppReply = (XModbusReply**)XHashMap_value_base(client->m_base.m_poolMap, &timerId);
        if (ppReply && *ppReply) {
            XModbusReply* reply = *ppReply;

            // 检查上一个请求是否已完成
            XModbusReply_State state = XModbusReply_state(reply);
            if (state == XModbusReply_State_Waiting || state == XModbusReply_State_Requesting) {
                return;
            }

            // 重新发送轮询请求（复用原有Reply）
            if (reply->m_request && reply->m_serverAddress >= 0) {
                // 重置Reply状态
                XModbusReply_setState(reply, XModbusReply_State_Requesting);
                XModbusReply_setError(reply, XModbusDevice_NoError, NULL);
                XModbusReply_clearIntermediateError(reply);

                // 清理旧的结果
                if (reply->m_rawResult) {
                    XModbusResponse_delete_base(reply->m_rawResult);
                    reply->m_rawResult = NULL;
                }
                if (reply->m_result) {
                    XModbusDataUnit_delete_base(reply->m_result);
                    reply->m_result = NULL;
                }

                // 获取新的交易ID并发送请求
                uint16_t transactionId = XModbusTcpClient_nextTransactionId(client);
                if (buildAndSendRequest(client, reply, transactionId, reply->m_request, reply->m_serverAddress)) {
                    XModbusReply_setState(reply, XModbusReply_State_Waiting);
                }
            }
            return;
        }
    }

parent_call:
    XClass_Parent(XModbusClient, EXObject_TimerEvent, void (*)(XObject*, XEventTimer*))(obj, event);
}

// =============== 槽函数实现 ===============
static void XModbusTcpClient_onReadyRead(XObject* receiver, XVarList* args)
{
    //XPrintf("收到数据\n");
    (void)args;
    XModbusTcpClient* client = (XModbusTcpClient*)receiver;
    if (!client) return;

    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (!io) return;
    
    XIODevice_readAll_2(io, client->m_receiveBuffer, true);
    
    //XByteArray_clear_base(client->m_receiveBuffer);
    processReceivedFrame(client);
}

static void XModbusTcpClient_onConnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusTcpClient* client = (XModbusTcpClient*)receiver;
    if (!client) return;

    XModbusDevice_setState((XModbusDevice*)client, XModbusDevice_ConnectedState);
    XModbusDevice_setError((XModbusDevice*)client, XModbusDevice_NoError, NULL);
}

static void XModbusTcpClient_onDisconnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusTcpClient* client = (XModbusTcpClient*)receiver;
    if (!client) return;

    if (client->m_receiveBuffer) {
        XByteArray_clear_base(client->m_receiveBuffer);
    }

    if (client->m_pendingRequests) {
        for_each_iterator(client->m_pendingRequests, XHashMap, it)
        {
            XPair* pair = XHashMap_iterator_data(&it);
            XModbusTcpPendingRequest* pending = XPair_Second(pair, XModbusTcpPendingRequest*);
            if (pending && pending->reply) {
                XModbusReply_setError(pending->reply, XModbusDevice_ConnectionError, "Connection lost");
                XModbusReply_setState(pending->reply, XModbusReply_State_No_Started);
            }
        }
        // 容器会自动调用 pendingRequestDeinit 释放元素
        XHashMap_clear_base(client->m_pendingRequests);
    }

    XModbusDevice_setState((XModbusDevice*)client, XModbusDevice_UnconnectedState);
}

static void XModbusTcpClient_onErrorOccurred(XObject* receiver, XVarList* args)
{
    XModbusTcpClient* client = (XModbusTcpClient*)receiver;
    if (!client) return;

    (void)args;
    XModbusDevice_setError((XModbusDevice*)client, XModbusDevice_ConnectionError, "Socket error");
}

// =============== 析构函数 ===============
static void VXModbusTcpClient_deinit(XModbusTcpClient* client)
{
    if (!client) return;

    XModbusDevice_disconnectDevice(client);

    if (client->m_receiveBuffer) {
        XByteArray_delete_base(client->m_receiveBuffer);
        client->m_receiveBuffer = NULL;
    }
    if (client->m_requestData) {
        XByteArray_delete_base(client->m_requestData);
        client->m_requestData = NULL;
    }
  
    // 先停止所有定时器
    if (client->m_pendingRequests) {
        for_each_iterator(client->m_pendingRequests, XHashMap, it)
        {
            XPair* pair = XHashMap_iterator_data(&it);
            XModbusTcpPendingRequest* pending = XPair_Second(pair, XModbusTcpPendingRequest*);
            if (pending && pending->timeoutTimer != XTIMER_INVALID_ID) {
                XObject_killTimer((XObject*)client, pending->timeoutTimer);
            }
        }
    }

    // 容器会自动调用 pendingRequestDeinit 释放元素
    if (client->m_pendingRequests) {
        XMapBase_delete_base(client->m_pendingRequests);
        client->m_pendingRequests = NULL;
    }
    if (client->m_timerMap) {
        XHashMap_delete_base(client->m_timerMap);
        client->m_timerMap = NULL;
    }

    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (io) {
        XTcpSocket_delete_base((XTcpSocket*)io);
        ((XModbusDevice*)client)->m_ioDevice = NULL;
    }

    client->m_transactionId = 0;

    XClass_Deinit_Parent(XModbusClient, client);
}