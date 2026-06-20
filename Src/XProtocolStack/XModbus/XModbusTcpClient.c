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

// =============== 内部函数前置声明 ===============
static void XModbusTcpClient_attemptReconnect(XModbusTcpClient* client);

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
/**
 * @brief 清理所有待处理请求
 */
static void clearAllPendingRequests(XModbusTcpClient* client, XModbusDevice_Error error, const char* errorMsg)
{
    if (!client || !client->m_pendingRequests) return;

    for_each_iterator(client->m_pendingRequests, XHashMap, it)
    {
        XPair* pair = XHashMap_iterator_data(&it);
        XModbusTcpPendingRequest* pending = XPair_second(pair);
        if (pending) {
            if (pending->timeoutTimer != XTIMER_INVALID_ID) {
                XHashMap_remove_base(client->m_timerMap, &pending->timeoutTimer);
                XObject_killTimer((XObject*)client, pending->timeoutTimer);
            }
            if (pending->reply && error != XModbusDevice_NoError) {
                XModbusReply_setError(pending->reply, error, errorMsg);
                XModbusReply_setState(pending->reply, XModbusReply_State_No_Started);
            }
        }
    }
    XHashMap_clear_base(client->m_pendingRequests);

    // 清空定时器映射
    if (client->m_timerMap) {
        XHashMap_clear_base(client->m_timerMap);
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
    //return 0;//返回0禁用超时定时器
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

    // 构建完整请求帧（MBAP头部 + FC + Data）
    // PDU长度 = FC(1) + Data(pduSize)
    size_t pduTotalLen = 1 + pduSize;
    size_t frameSize = 7 + pduTotalLen;  // MBAP(7) + PDU(pduTotalLen)
    XByteArray* requestData = client->m_requestData;
    if (!requestData) return false;

    XByteArray_resize_base(requestData, frameSize);
    uint8_t* frameData = XContainerDataAddr(requestData);

    // 构建MBAP头部（length参数为PDU总长度：FC + Data）
    buildMbapHeader(frameData, transactionId, (uint16_t)pduTotalLen, (uint8_t)serverAddress);

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
    //XModbusTcpPendingRequest pending = createPendingRequest(reply, transactionId, (uint8_t)serverAddress);
  
    //pending.timeoutTimer = timerId;
    XModbusTcpPendingRequest pending = { .reply = reply,.transactionId = transactionId ,.unitId = serverAddress,.timeoutTimer = timerId ,.retryCount = 0 };
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
    client->m_pendingRequests = XHashMap_Create(uint16_t, XModbusTcpPendingRequest, uint16_t_compare);
    //XContainerSetDataDeinitMethod(client->m_pendingRequests, pendingRequestDeinit);
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
    clearAllPendingRequests(client, XModbusDevice_ConnectionError, "Device closed");

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

    //XString* text= XByteArray_to16HexString(buffer);
    //XPrintf_2(text);
    //XPrintf("\n");
    //XString_delete_base(text);

    while (bufLen >= 7) {
        const uint8_t* data = XContainerDataAddr(buffer);

        uint16_t transactionId, protocolId, length;
        uint8_t unitId;
        parseMbapHeader(data, &transactionId, &protocolId, &length, &unitId);

        // 协议检查
        if (protocolId != 0x0000) {
            XByteArray_clear_base(buffer);
            return;
        }

        // Length 有效性检查
        if (length < 1 || length > 254) {
            XByteArray_clear_base(buffer);
            return;
        }

        size_t expectedLen = 6 + length;  // MBAP 前 6 字节 + Length 字段的值
        if (bufLen < expectedLen) {
            // 半帧，等待更多数据
            return;
        }

        // 查找对应的 pending 请求
        XModbusTcpPendingRequest* pending = (XModbusTcpPendingRequest*)XHashMap_value_base(client->m_pendingRequests, &transactionId);
        if (!pending) {
            XByteArray_remove_base(buffer, 0, expectedLen);
            bufLen = XByteArray_size_base(buffer);
            continue;
        }
        XModbusReply* reply = pending->reply;

        // 先从 timerMap 移除，再停止定时器
        if (pending->timeoutTimer != XTIMER_INVALID_ID) {
            XHashMap_remove_base(client->m_timerMap, &pending->timeoutTimer);
        }
        timeoutTimerStop(client, &pending->timeoutTimer);

        XModbusReply_setState(reply, XModbusReply_State_Responding);

        // Unit ID 检查
        if (unitId != pending->unitId) {
            XModbusReply_setError(reply, XModbusReply_State_Finished, "Unit ID mismatch");
            XModbusReply_setState(reply, XModbusReply_State_Finished);
            XHashMap_remove_base(client->m_pendingRequests, &transactionId);
            XByteArray_remove_base(buffer, 0, expectedLen);
            bufLen = XByteArray_size_base(buffer);
            continue;
        }

        // PDU 数据：跳过 MBAP(6) + Unit ID(1) = 7 字节
        // PDU 长度 = Length - 1 (减去 Unit ID)
        const uint8_t* pduData = data + 7;
        size_t pduLen = length - 1;  // FC + Data 的总长度

        if (pduLen < 1) {
            // 至少要有 FC
            XModbusReply_setError(reply, XModbusDevice_ProtocolError, "Invalid PDU");
            XModbusReply_setState(reply, XModbusReply_State_Finished);
            XHashMap_remove_base(client->m_pendingRequests, &transactionId);
            XByteArray_remove_base(buffer, 0, expectedLen);
            bufLen = XByteArray_size_base(buffer);
            continue;
        }

        // 创建响应对象
        if (!reply->m_rawResult) {
            reply->m_rawResult = XModbusResponse_create();
        }

        XModbusResponse* response = reply->m_rawResult;
        uint8_t fc = pduData[0];

        if (fc & XMODBUS_PDU_EXCEPTION_BYTE) {
            // 异常响应
            XModbusPdu_setFunctionCode(response, (XModbusPdu_FunctionCode)(fc & 0x7F));
            if (pduLen >= 2) {
                uint8_t exceptionCode = pduData[1];
                XModbusPdu_setData(response, &exceptionCode, 1);

                const char* errorMsg = "Modbus exception";
                switch (exceptionCode) {
                case XModbusPdu_IllegalFunction: errorMsg = "Illegal function"; break;
                case XModbusPdu_IllegalDataAddress: errorMsg = "Illegal data address"; break;
                case XModbusPdu_IllegalDataValue: errorMsg = "Illegal data value"; break;
                case XModbusPdu_ServerDeviceFailure: errorMsg = "Server device failure"; break;
                case XModbusPdu_Acknowledge: errorMsg = "Acknowledge"; break;
                case XModbusPdu_ServerDeviceBusy: errorMsg = "Server device busy"; break;
                default: break;
                }
                XModbusReply_setError(reply, XModbusDevice_ProtocolError, errorMsg);
            }
        }
        else {
            // 正常响应
            XModbusPdu_setFunctionCode(response, (XModbusPdu_FunctionCode)fc);

            // 数据部分：跳过 FC(1)，长度 = pduLen - 1
            if (pduLen > 1) {
                XModbusPdu_setData(response, pduData + 1, pduLen - 1);
            }

            // 处理响应数据
            if (!reply->m_result) {
                reply->m_result = XModbusDataUnit_create();
            }
 //            XString* text= XByteArray_to16HexString(response->m_base.m_data);
 //XPrintf_2(text);
 //XPrintf("\n");
 //XString_delete_base(text);
            bool success = XModbusClient_processResponse_base((XModbusClient*)client, response, reply->m_result);
            if (!success) {
                XModbusReply_setError(reply, XModbusDevice_UnknownError, "Response processing failed");
            }
        }

        XHashMap_remove_base(client->m_pendingRequests, &transactionId);
        XByteArray_remove_base(buffer, 0, expectedLen);
        bufLen = XByteArray_size_base(buffer);
        //printf("触发结束信号\n");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
    }
}

// =============== 超时处理 ===============
static void handleRequestTimeout(XModbusTcpClient* client, uint16_t transactionId)
{
    if (!client || !client->m_pendingRequests) return;

    XModbusTcpPendingRequest* pending = (XModbusTcpPendingRequest*)XHashMap_value_base(client->m_pendingRequests, &transactionId);
    if (!pending) return;
    // 从定时器反向映射中移除
    if (pending->timeoutTimer != XTIMER_INVALID_ID) {
        XHashMap_remove_base(client->m_timerMap, &pending->timeoutTimer);
    }
    pending->timeoutTimer = XTIMER_INVALID_ID;

    // 保存reply指针，避免pending在remove后变成悬空指针
    XModbusReply* reply = pending->reply;
    uint8_t retryCount = pending->retryCount;

    // 尝试重试
    int maxRetries = XModbusClient_numberOfRetries((XModbusClient*)client);
    if (retryCount < maxRetries && reply) {
        // 检查连接状态
        XAbstractSocket* socket = (XAbstractSocket*)XModbusTcpClient_socket(client);
        if (socket && XTcpSocket_isOpen(socket) && reply->m_request) {
            
            // 获取新的事务ID
            uint16_t newTransactionId = XModbusTcpClient_nextTransactionId(client);
            
            // 从映射表移除旧的事务ID（必须在buildAndSendRequest之前，因为后者会插入新条目）
            XHashMap_remove_base(client->m_pendingRequests, &transactionId);
            
            // 从Reply获取请求数据重新发送
            if (buildAndSendRequest(client, reply, newTransactionId, reply->m_request, reply->m_serverAddress)) {
                // 恢复重试计数（buildAndSendRequest创建的条目retryCount为0）
                XModbusTcpPendingRequest* newPending = (XModbusTcpPendingRequest*)XHashMap_value_base(client->m_pendingRequests, &newTransactionId);
                if (newPending) {
                    newPending->retryCount = retryCount + 1;
                }
                return;
            }
            // 发送失败，旧条目已在上面remove，直接走超时错误处理
        }
    }

    // 重试次数用尽或重试失败
    if (reply) {
        XModbusReply_setError(reply, XModbusDevice_TimeoutError, "Request timeout");
        XModbusReply_setState(reply, XModbusReply_State_Timeout);
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
    if(XModbusDevice_state(client)== XModbusDevice_ConnectedState)
    {
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
    }

    // 检查是否是重连定时器
    XModbusClient* baseClient = (XModbusClient*)client;
    if (timerId == baseClient->m_reconnectTimer) 
    {
        // 重连定时器只触发一次，停止它
        XModbusClient_reconnectTimerStop(client);
        
        // 执行重连尝试
        XModbusTcpClient_attemptReconnect(client);
        return;
    }

parent_call:
    XClass_Parent(XModbusClient, EXObject_TimerEvent, void (*)(XObject*, XEventTimer*))(obj, event);
}

// =============== 重连处理 ===============
static void XModbusTcpClient_attemptReconnect(XModbusTcpClient* client)
{
    if (!client) return;

    XModbusClient* baseClient = (XModbusClient*)client;
    
    // 停止重连定时器
    XModbusClient_reconnectTimerStop(client);

    // 检查是否已经连接
    if (XModbusDevice_state((XModbusDevice*)client) == XModbusDevice_ConnectedState) {
        baseClient->m_reconnectAttempts = 0;
        return;
    }

    // 增加重连计数
    baseClient->m_reconnectAttempts++;

    // 尝试重新连接
    XModbusDevice_setState((XModbusDevice*)client, XModbusDevice_ConnectingState);
    
    // 获取连接参数
    const char* hostName = NULL;
    char hostNameBuf[256] = { 0 };

    const XVariant* addrVar = XModbusDevice_connectionParameter_const((XModbusDevice*)client, XModbusDevice_NetworkAddressParameter);
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
        // 无法重连，配置丢失
        XModbusDevice_setError((XModbusDevice*)client, XModbusDevice_ConfigurationError, "Server address not configured for reconnection");
        XModbusDevice_setState((XModbusDevice*)client, XModbusDevice_UnconnectedState);
        return;
    }

    uint16_t port = 502;
    const XVariant* portVar = XModbusDevice_connectionParameter_const((XModbusDevice*)client, XModbusDevice_NetworkPortParameter);
    if (portVar) {
        port = (uint16_t)XVariant_toInt(portVar);
    }

    // 发起连接
    XAbstractSocket* socket = (XAbstractSocket*)XModbusTcpClient_socket(client);
    if (socket) {
        XAbstractSocket_connectToHost_base(socket, hostName, port, XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    }
}

// =============== 槽函数实现 ===============
static void XModbusTcpClient_onReadyRead(XObject* receiver, XVarList* args)
{
    
    (void)args;
    XModbusTcpClient* client = (XModbusTcpClient*)receiver;
    if (!client) return;

    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (!io) return;
    
    XIODevice_readAll_2(io, client->m_receiveBuffer, true);
    
    //XByteArray_clear_base(client->m_receiveBuffer);
    //XPrintf("收到数据:%d\n",XContainerSize(client->m_receiveBuffer));
    processReceivedFrame(client);
}

static void XModbusTcpClient_onConnected(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusTcpClient* client = (XModbusTcpClient*)receiver;
    if (!client) return;

    // 连接成功，重置重连计数器
    XModbusClient* baseClient = (XModbusClient*)client;
    baseClient->m_reconnectAttempts = 0;
    
    // 停止可能存在的重连定时器
    XModbusClient_reconnectTimerStop(client);

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

    clearAllPendingRequests(client, XModbusDevice_ConnectionError, "Connection lost");

    XModbusDevice_setState((XModbusDevice*)client, XModbusDevice_UnconnectedState);

    // 检查是否需要自动重连
    XModbusClient* baseClient = (XModbusClient*)client;
    if (baseClient->m_autoReconnect) {
        // 检查是否达到最大重连次数
        if (baseClient->m_maxReconnectAttempts < 0 || 
            baseClient->m_reconnectAttempts < baseClient->m_maxReconnectAttempts) {
            
            // 启动重连定时器
            XModbusClient_reconnectTimerStart(client);
        }
    }
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
  
    clearAllPendingRequests(client, XModbusDevice_ConnectionError, "Connection lost");

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
        XTcpSocket_deleteLater((XTcpSocket*)io);
        ((XModbusDevice*)client)->m_ioDevice = NULL;
    }

    client->m_transactionId = 0;

    XClass_Deinit_Parent(XModbusClient, client);
}