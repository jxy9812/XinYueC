#include "XModbusRtuSerialClient.h"
#include "XModbusClient_Protected.h"
#include "XModbusReply_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XCircularQueue.h"
#include "XHashMap.h"
#include "XMemory.h"
#include "XCrc.h"
#include "XByteArray.h"
#include "XTimer.h"
#include "string.h"
//typedef struct PoolTask
//{
//    XModbusRequest* request;
//    int serverAddress;
//};
// =============== 虚函数前置声明 ===============
static bool VXModbusRtuSerialClient_open(XModbusDevice* device);
static void VXModbusRtuSerialClient_close(XModbusDevice* device);
static void VXModbusRtuSerialClient_deinit(XModbusRtuSerialClient* client);
static void VXModbusRtuSerialClient_timerEvent(XObject* obj, XEventTimer* event);
static XModbusReply* VXModbusRtuSerialClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress, XFuncParamType type);
//static bool VXModbusRtuSerialClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
//static bool VXModbusRtuSerialClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

// =============== 槽函数声明 ===============
static void XModbusRtuSerialClient_onReadyRead(XObject* receiver, XVarList* args);
static void processReceivedFrame(XModbusRtuSerialClient* client,XByteArray* receiveBuffer);
static bool startNewRequest(XModbusRtuSerialClient* client);
static void handleRequestTimeout(XModbusRtuSerialClient* client);
// =============== 辅助函数 ===============
static inline void interFrameTimerStop(XModbusRtuSerialClient* client)
{
    if (client->m_interFrameTimer != XTIMER_INVALID_ID) {
        //XPrintf("remove timerId:%d\n", client->m_interFrameTimer);
        XObject_killTimer((XObject*)client, client->m_interFrameTimer);
        client->m_interFrameTimer = XTIMER_INVALID_ID;
    }
}
static inline void interFrameTimerStart(XModbusRtuSerialClient* client)
{
    interFrameTimerStop(client);
    int interFrameDelayMs = (client->m_interFrameDelay + 999) / 1000;
    if (interFrameDelayMs < 1) interFrameDelayMs = 1;
    client->m_interFrameTimer = XObject_startTimer_ms((XObject*)client,
        interFrameDelayMs, XTimerType_CoarseTimer);
    //XPrintf("add timerId:%d\n", client->m_interFrameTimer);
}
static inline void timeoutFrameTimerStop(XModbusRtuSerialClient* client)
{
    if (((XModbusClient*)client)->m_timeoutTimer != XTIMER_INVALID_ID) {
       /* XPrintf("remove timerId:%d\n", ((XModbusClient*)client)->m_timeout);*/
        XObject_killTimer((XObject*)client, ((XModbusClient*)client)->m_timeoutTimer);
        ((XModbusClient*)client)->m_timeoutTimer = XTIMER_INVALID_ID;
    }
}
static inline void timeoutFrameTimerStart(XModbusRtuSerialClient* client)
{
    timeoutFrameTimerStop(client);
    ((XModbusClient*)client)->m_timeoutTimer = XObject_startTimer_ms((XObject*)client,
        XModbusClient_timeout(client), XTimerType_CoarseTimer);
}
static inline void turnaroundFrameTimerStart(XModbusRtuSerialClient* client)
{
    timeoutFrameTimerStop(client);
    ((XModbusClient*)client)->m_timeoutTimer = XObject_startTimer_ms((XObject*)client,
        client->m_turnaroundDelay, XTimerType_CoarseTimer);
}
static inline int calculateInterFrameDelay(int baudRate) {
    if (baudRate <= 0) return 1750;
    int delay = (int)(38500000 / baudRate);
    return delay < 1750 ? 1750 : delay;
}

static inline bool validateRtuFrame(const uint8_t* frame, size_t frameLen) {
    if (!frame || frameLen < 4) return false;

    uint16_t calculatedCrc = XCrc_get16((uint8_t*)frame, frameLen - 2);

    // Modbus RTU CRC 是小端序
    uint16_t receivedCrc;
    XMemory_read_data(frame + frameLen - 2, XBYTE_ORDER_LITTLE_ENDIAN, (uint8_t*)&receivedCrc, sizeof(uint16_t));

    return calculatedCrc == receivedCrc;
}

// =============== 类初始化 ===============
XVtable* XModbusRtuSerialClient_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XModbusRtuSerialClient))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XModbusClient);

    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusRtuSerialClient_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusRtuSerialClient_close);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusRtuSerialClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusRtuSerialClient_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_SendRawRequest, VXModbusRtuSerialClient_sendRawRequest);
    // 重写 ProcessResponse 和 ProcessPrivateResponse
    //XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_ProcessResponse, VXModbusRtuSerialClient_processResponse);
    //XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_ProcessPrivateResponse, VXModbusRtuSerialClient_processPrivateResponse);

#if SHOWCONTAINERSIZE
    printf("XModbusRtuSerialClient vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XModbusRtuSerialClient* XModbusRtuSerialClient_create(void)
{
    XModbusRtuSerialClient* client = XMalloc_System(sizeof(XModbusRtuSerialClient));
    if (!client) return NULL;
    XModbusRtuSerialClient_init(client);
    Set_Class_MemoryFree(client, XFree_System);
    return client;
}

void XModbusRtuSerialClient_init(XModbusRtuSerialClient* client)
{
    if (!client) return;

    XModbusClient_init(&client->m_base);
    XClassGetVtable(client) = XModbusRtuSerialClient_class_init();

    XSerialPort* serialPort = XSerialPort_create();
    //XPrintf("串口:%p\n", serialPort);
    if (serialPort) {
        ((XModbusDevice*)client)->m_ioDevice = (XIODevice*)serialPort;

        XObject_connect_1((XObject*)serialPort,
            XSignal(XIODevice_readyRead_signal),
            (XObject*)client,
            XModbusRtuSerialClient_onReadyRead,
            XConnectionType_Auto);
    }

    client->m_interFrameDelay = 0;
    client->m_turnaroundDelay = 100;
    client->m_currentReply = NULL;
    client->m_interFrameTimer = XTIMER_INVALID_ID;
    client->m_currentServerAddress = 0;
    client->m_retryCount = 0;
    client->m_waitingForTurnaround = false;
    client->m_requestData = NULL;
    client->m_receiveBuffer = XByteArray_create();  // 创建接收缓冲区
    client->m_queue = XCircularQueue_create(sizeof(XModbusReply*),50);
}

// =============== 帧间延迟 ===============
int XModbusRtuSerialClient_interFrameDelay(const XModbusRtuSerialClient* client)
{
    if (!client) return 1750;

    if (client->m_interFrameDelay > 0) {
        return client->m_interFrameDelay;
    }

    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (serialPort) {
        int baudRate = XSerialPort_baudRate(serialPort, XSerialPort_AllDirections);
        return calculateInterFrameDelay(baudRate);
    }

    return 1750;
}

void XModbusRtuSerialClient_setInterFrameDelay(XModbusRtuSerialClient* client, int microseconds)
{
    if (!client) return;

    if (microseconds < 0) {
        client->m_interFrameDelay = 0;
    }
    else {
        XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
        if (serialPort) {
            int baudRate = XSerialPort_baudRate(serialPort, XSerialPort_AllDirections);
            int autoDelay = calculateInterFrameDelay(baudRate);
            if (microseconds < autoDelay) {
                client->m_interFrameDelay = 0;
            }
            else {
                client->m_interFrameDelay = microseconds;
            }
        }
        else {
            client->m_interFrameDelay = microseconds;
        }
    }
}

// =============== 响应延迟 ===============
int XModbusRtuSerialClient_turnaroundDelay(const XModbusRtuSerialClient* client)
{
    return client ? client->m_turnaroundDelay : 100;
}

void XModbusRtuSerialClient_setTurnaroundDelay(XModbusRtuSerialClient* client, int turnaroundDelay)
{
    if (client) {
        client->m_turnaroundDelay = turnaroundDelay;
    }
}

// =============== 串口对象 ===============
XSerialPort* XModbusRtuSerialClient_serialPort(const XModbusRtuSerialClient* client)
{
    if (!client) return NULL;
    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (!io) return NULL;
    return (XSerialPort*)io;
}

//// =============== ProcessResponse 虚函数重写 ===============
//static bool VXModbusRtuSerialClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
//{
//    // RTU客户端不需要特殊处理，直接调用父类实现
//    return VXModbusClient_processResponse(client, response, data);
//}
//
//// =============== ProcessPrivateResponse 虚函数重写 ===============
//static bool VXModbusRtuSerialClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data)
//{
//    // RTU客户端不需要特殊处理，直接调用父类实现
//    return VXModbusClient_processPrivateResponse(client, response, data);
//}

// =============== 虚函数实现 ===============
static bool VXModbusRtuSerialClient_open(XModbusDevice* device)
{
    if (!device) return false;

    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)device;
    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);

    if (!serialPort) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Serial port not available");
        return false;
    }

    // 设置串口参数
    const XVariant* portNameVar = XModbusDevice_connectionParameter_const(device, XModbusDevice_SerialPortNameParameter);
    if (portNameVar) {
        XString* portName = XVariant_toString_const(portNameVar);
        if (portName) {
            XSerialPort_setPortName(serialPort, XString_toUtf8(portName));
        }
    }

    const XVariant* baudRateVar = XModbusDevice_connectionParameter_const(device, XModbusDevice_SerialBaudRateParameter);
    if (baudRateVar) {
        int baudRate = XVariant_toInt(baudRateVar);
        if (baudRate > 0) {
            XSerialPort_setBaudRate(serialPort, baudRate, XSerialPort_AllDirections);
        }
    }

    const XVariant* dataBitsVar = XModbusDevice_connectionParameter_const(device, XModbusDevice_SerialDataBitsParameter);
    if (dataBitsVar) {
        XSerialPort_setDataBits(serialPort, (XSerialPort_DataBits)XVariant_toInt(dataBitsVar));
    }

    const XVariant* parityVar = XModbusDevice_connectionParameter_const(device, XModbusDevice_SerialParityParameter);
    if (parityVar) {
        XSerialPort_setParity(serialPort, (XSerialPort_Parity)XVariant_toInt(parityVar));
    }

    const XVariant* stopBitsVar = XModbusDevice_connectionParameter_const(device, XModbusDevice_SerialStopBitsParameter);
    if (stopBitsVar) {
        XSerialPort_setStopBits(serialPort, (XSerialPort_StopBits)XVariant_toInt(stopBitsVar));
    }

    // 打开串口
    bool result = XSerialPort_open_base(serialPort, XIODevice_ReadWrite);
    if (!result) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Failed to open serial port");
        return false;
    }

    // 清除缓冲区
    XByteArray* array = XIODevice_readAll_3((XIODevice*)serialPort);
    if (array) XByteArray_delete_base(array);

    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    XModbusDevice_setError(device, XModbusDevice_NoError, NULL);
    if(((XModbusRtuSerialClient*)device)->m_interFrameDelay==0)
        ((XModbusRtuSerialClient*)device)->m_interFrameDelay = XModbusRtuSerialClient_interFrameDelay(client);
    return true;
}

static void VXModbusRtuSerialClient_close(XModbusDevice* device)
{
    if (!device) return;

    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)device;
    
    interFrameTimerStop(device);
    timeoutFrameTimerStop(device);

    if (client->m_currentReply) {
        XModbusReply_setError(client->m_currentReply, XModbusDevice_ConnectionError, "Device closed");
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        XModbusReply_setState(reply, XModbusReply_State_No_Started);
        //XModbusReply_setFinished(reply, true);
    }

    if (client->m_requestData) {
        XByteArray_clear_base(client->m_requestData);
    }

    client->m_retryCount = 0;
    client->m_waitingForTurnaround = false;
    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (serialPort) {
        XSerialPort_close_base(serialPort);
    }

    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

// =============== 响应处理 ===============
static void processReceivedFrame(XModbusRtuSerialClient* client, XByteArray* receiveBuffer) {
    if (!client || !client->m_currentReply) return;
    
    XModbusReply* reply = client->m_currentReply;
    XModbusReply_setState(reply, XModbusReply_State_Responding);
    const uint8_t* data = XContainerDataAddr(receiveBuffer);
    size_t len = XByteArray_size_base(receiveBuffer);

    if (!validateRtuFrame(data, len)) {
        XModbusReply_setError(reply, XModbusDevice_UnknownError, "CRC check failed");
        XModbusReply_addIntermediateError(reply, XModbusDevice_ResponseCrcError);
        handleRequestTimeout(client);//重发请求
        return;
        //goto cleanup;
    }

    if (data[0] != client->m_currentServerAddress) {
        XModbusReply_setError(reply, XModbusDevice_UnknownError, "Server address mismatch");
        XModbusReply_addIntermediateError(reply, XModbusDevice_ResponseRequestMismatch);
        handleRequestTimeout(client);//重发请求
        return;
        //goto cleanup;
    }
    if (!client->m_currentReply->m_rawResult)
        client->m_currentReply->m_rawResult = XModbusResponse_create();
    // 解析响应PDU
   /* XModbusResponse response;
    XModbusResponse_init(&response);*/
    XModbusResponse* response= client->m_currentReply->m_rawResult;
    uint8_t fc = data[1];
    XModbusPdu_setFunctionCode(response, (XModbusPdu_FunctionCode)fc);

    // 检查异常响应
    if (fc & XMODBUS_PDU_EXCEPTION_BYTE) {
        if (len >= 4) {
            uint8_t exceptionCode = data[2];
            XModbusPdu_setData(response, &exceptionCode, 1);

            const char* errorMsg = "Modbus exception";
            switch (exceptionCode) {
            case XModbusPdu_IllegalFunction: errorMsg = "Illegal function"; break;
            case XModbusPdu_IllegalDataAddress: errorMsg = "Illegal data address"; break;
            case XModbusPdu_IllegalDataValue: errorMsg = "Illegal data value"; break;
            case XModbusPdu_ServerDeviceFailure: errorMsg = "Server device failure"; break;
            case XModbusPdu_Acknowledge: errorMsg = "Acknowledge"; break;
            case XModbusPdu_ServerDeviceBusy: errorMsg = "Server device busy"; break;
            case XModbusPdu_MemoryParityError: errorMsg = "Memory parity error"; break;
            case XModbusPdu_GatewayPathUnavailable: errorMsg = "Gateway path unavailable"; break;
            case XModbusPdu_GatewayTargetDeviceFailedToRespond: errorMsg = "Gateway target device failed to respond"; break;
            default: break;
            }
            XModbusReply_setError(client->m_currentReply, XModbusDevice_ProtocolError, errorMsg);
        }
        goto cleanup;
    }

    // 正常响应：设置PDU数据（不含地址、功能码和CRC）
    // data + 2 跳过地址和功能码
    // len - 4 = 总长度 - 地址(1) - 功能码(1) - CRC(2)
    if (len > 4) {
        XModbusPdu_setData(response, data + 2, len - 4);
    }



    if(!client->m_currentReply->m_result)
        client->m_currentReply->m_result= XModbusDataUnit_create();

    // 获取结果单元
    //XModbusDataUnit* resultUnit = XModbusDataUnit_create();
    XModbusDataUnit* resultUnit = client->m_currentReply->m_result;

   /* XString* text= XByteArray_to16HexString(receiveBuffer);
    XPrintf_string(text);
    XPrintf("\n");
    XString_delete_base(text);*/
    
    // 通过保护API调用虚函数（会调到子类重写的实现）
    bool success = XModbusClient_processResponse_base((XModbusClient*)client, response, resultUnit);

    if (success) {
        //XModbusReply_setResult_ref(client->m_currentReply, resultUnit);
    }
    else {
        XModbusReply_setError(client->m_currentReply, XModbusDevice_UnknownError, "Response processing failed");
        // 释放临时结果单元
        /*if (resultUnit) {
            XModbusDataUnit_delete_base(resultUnit);
        }*/
    }
    //XModbusResponse_deinit_base(&response);
cleanup:
    //XModbusReply* reply = client->m_currentReply;
    client->m_currentReply = NULL;
    client->m_retryCount = 0;
    //XPrintf("准备调用槽函数\n");
    //XModbusReply_setFinished(reply, true);
    XModbusReply_setState(reply, XModbusReply_State_Finished);
    if (!client->m_currentReply)//当前是空闲的
           startNewRequest(client);
}
bool startNewRequest(XModbusRtuSerialClient* client)
{
    XModbusReply* reply=NULL;
    if (!XQueueBase_receive_base(client->m_queue, &reply))
        return false;
    if (!client || !reply || !reply->m_request|| reply->m_serverAddress < 0 || reply->m_serverAddress > 247) {
        return false;
    }
    const XModbusRequest* request = reply->m_request;
    int serverAddress = reply->m_serverAddress;

    XModbusRtuSerialClient* rtuClient = (XModbusRtuSerialClient*)client;
    XModbusDevice* device = (XModbusDevice*)client;

    if (XModbusDevice_state(device) != XModbusDevice_ConnectedState) {
        return false;
    }

    XIODevice* io = device->m_ioDevice;
    if (!io || !XIODevice_isOpen(io)) {
        return false;
    }

    if (rtuClient->m_currentReply != NULL) {
        return false;
    }
    XByteArray* pdu = ((XModbusPdu*)request)->m_data;
    if (!pdu || XByteArray_isEmpty_base(pdu))
        return false;
   /* XModbusReply* reply = XModbusClient_createReply(client, request, serverAddress);
    if (!reply) return NULL;*/
    
    XModbusReply_setState(reply, XModbusReply_State_Requesting);

    // 构建PDU数据
    if (!rtuClient->m_requestData) {
        rtuClient->m_requestData = XByteArray_create();
    }
    XByteArray_resize_base(rtuClient->m_requestData, XByteArray_size_base(pdu) + 4);
    XByteArray_clear_base(rtuClient->m_requestData);
    //添加地址
    XByteArray_push_back_1(rtuClient->m_requestData, serverAddress);
    // 添加功能码
    uint8_t fc = (uint8_t)XModbusPdu_functionCode((const XModbusPdu*)request);
    XByteArray_push_back_1(rtuClient->m_requestData, fc);
    // 添加数据
    XByteArray_push_back_2(rtuClient->m_requestData, XContainerDataAddr(pdu), XContainerSize(pdu));
    //添加Crc16
    uint16_t crc = XCrc_get16(XContainerDataAddr(rtuClient->m_requestData), XContainerSize(rtuClient->m_requestData));
    XCrc_set16Data(&XByteArray_back_base(rtuClient->m_requestData) + 1, crc, XCRC_BYTE_ORDER_LITTLE_ENDIAN);
    XContainerSize(rtuClient->m_requestData) += 2;

   /* XString* text= XByteArray_to16HexString(rtuClient->m_requestData);
    XPrintf_string(text);
    XPrintf("\n");
    XString_delete_base(text);*/

    // 发送RTU请求
    int64_t sent = XIODevice_write_2(io, rtuClient->m_requestData);
    if (!sent) {
        return  startNewRequest(client);;
    }
    XModbusReply_clearIntermediateError(reply);

    rtuClient->m_currentReply = reply;
    rtuClient->m_retryCount = 0;

    if (serverAddress == 0) {
        // 广播消息，等待 turnaround 延迟
        rtuClient->m_waitingForTurnaround = true;
        turnaroundFrameTimerStart(client);
    }
    else {
        int timeout = XModbusClient_timeout(client);
        timeoutFrameTimerStart(client);
        interFrameTimerStart(client);

    }
    ((XModbusRtuSerialClient*)client)->m_currentServerAddress = serverAddress;
    XModbusReply_setState(reply, XModbusReply_State_Waiting);
    return true;
}
// =============== 槽函数实现 ===============
static void XModbusRtuSerialClient_onReadyRead(XObject* receiver, XVarList* args) {
    (void)args;
    //XPrintf("收到数据\n");
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)receiver;
    if (!client || !client->m_currentReply) return;

    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (!io) return;

    XIODevice_readAll_2(io, client->m_receiveBuffer, true);

    //XByteArray* data = XIODevice_readAll_3(io);
    //if (!data || XByteArray_size_base(data) == 0) {
    //    if (data) XByteArray_delete_base(data);
    //    return;
    //}

    //// 将数据追加到接收缓冲区
    //XByteArray_push_back_2(client->m_receiveBuffer,
    //    XContainerDataAddr(data),
    //    XByteArray_size_base(data));
    //XByteArray_delete_base(data);

    // 启动/重启帧间延迟定时器
    // 如果在帧间延迟时间内没有新数据，认为一帧接收完成
    //interFrameTimerStop(client);
    //int interFrameDelayUs = XModbusRtuSerialClient_interFrameDelay(client);
    // 微秒转毫秒，至少1ms
    interFrameTimerStart(client);

}
/**
 * @brief 处理帧间延迟超时
 * @param client RTU客户端
 */
static void handleInterFrameTimeout(XModbusRtuSerialClient* client) 
{
    interFrameTimerStop(client);
    size_t bufLen = XByteArray_size_base(client->m_receiveBuffer);
    if (bufLen < 5) return false;  // 最小帧长度：地址(1) + 功能码(1) + 数据(1) + CRC(2)

    const uint8_t* data = XContainerDataAddr(client->m_receiveBuffer);
    uint8_t fc = data[1];
    size_t expectedLen = 0;

    // 异常响应固定5字节
    if (fc & XMODBUS_PDU_EXCEPTION_BYTE) {
        expectedLen = 5;
    }
    else {
        switch (fc) {
        case XModbusPdu_ReadCoils:
        case XModbusPdu_ReadDiscreteInputs:
        case XModbusPdu_ReadHoldingRegisters:
        case XModbusPdu_ReadInputRegisters:
        case XModbusPdu_ReadWriteMultipleRegisters:
            // 读响应：地址(1) + 功能码(1) + 字节数(1) + 数据(N) + CRC(2)
            if (bufLen >= 3) {
                uint8_t byteCount = data[2];
                expectedLen = 3 + byteCount + 2;
            }
            break;
        case XModbusPdu_WriteSingleCoil:
        case XModbusPdu_WriteSingleRegister:
        case XModbusPdu_WriteMultipleCoils:
        case XModbusPdu_WriteMultipleRegisters:
            // 写响应：8字节
            expectedLen = 8;
            break;
        default:
            // 未知功能码，无法确定长度
            break;
        }
    }

    if (expectedLen > 0 && bufLen >= expectedLen) {
        processReceivedFrame(client,  client->m_receiveBuffer);
        XByteArray_clear_base(client->m_receiveBuffer);
        return true;
    }

    return false;
}

/**
 * @brief 处理请求超时
 * @param client RTU客户端
 */
static void handleRequestTimeout(XModbusRtuSerialClient* client) 
{
    timeoutFrameTimerStop(client);
    if (!client->m_currentReply) return;

    // 广播消息的 turnaround 延迟结束
    if (client->m_waitingForTurnaround) {
        client->m_waitingForTurnaround = false;
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        XModbusReply_setState(reply, XModbusReply_State_Timeout);
        //XModbusReply_setFinished(reply, true);
        return;
    }

    // 尝试重试
    int maxRetries = XModbusClient_numberOfRetries((XModbusClient*)client);
    if (client->m_retryCount < maxRetries &&
        client->m_requestData && !XByteArray_isEmpty_base(client->m_requestData)) {
        client->m_retryCount++;

        // 重新发送
        int64_t sent = XIODevice_write_2(
            ((XModbusDevice*)client)->m_ioDevice,
            client->m_requestData);

        if (sent) {
            // 清空接收缓冲区
            if (client->m_receiveBuffer) {
                XByteArray_clear_base(client->m_receiveBuffer);
            }
            timeoutFrameTimerStart(client);
            return;
        }
    }

    // 重试次数用尽或重试失败
    XModbusReply_setError(client->m_currentReply, XModbusDevice_TimeoutError, "Request timeout");
    XModbusReply* reply = client->m_currentReply;
    client->m_currentReply = NULL;
    client->m_retryCount = 0;
    //XModbusReply_setFinished(reply, true);
    XModbusReply_setState(reply, XModbusReply_State_Timeout);
}
// =============== 定时器事件处理 ===============
static void VXModbusRtuSerialClient_timerEvent(XObject* obj, XEventTimer* event) {
    if (!obj || !event) goto parent_call;

    XEvent_accept(event);
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)obj;
    XTimerId timerId = XEventTimer_timerId(event);

    // 处理帧间延迟定时器 - 一帧接收完成
    if (timerId == client->m_interFrameTimer) {
        handleInterFrameTimeout(client);
        return;
    }

    // 处理请求超时定时器
    if (timerId == ((XModbusClient*)client)->m_timeoutTimer) {
        //timeoutFrameTimerStop(client);
        handleRequestTimeout(client);
        return;
    }
    //XModbusReply* reply = *((XModbusReply**)XHashMap_value_base(client->m_base.m_poolMap, &timerId));
    XQueueBase_push_base(client->m_queue, XHashMap_value_base(client->m_base.m_poolMap, &timerId));
    if(!client->m_currentReply)//当前是空闲的
        startNewRequest(client);
    return;
parent_call:
    XClass_Parent(XModbusClient, EXObject_TimerEvent, void (*)(XObject*, XEventTimer*))(obj, event);
}

// =============== 析构函数 ===============
static void VXModbusRtuSerialClient_deinit(XModbusRtuSerialClient* client)
{
    if (!client) return;
    XModbusDevice_disconnectDevice(client);

    if (client->m_requestData) {
        XByteArray_delete_base(client->m_requestData);
        client->m_requestData = NULL;
    }
    if (client->m_receiveBuffer) {
        XByteArray_delete_base(client->m_receiveBuffer);
        client->m_receiveBuffer = NULL;
    }
    if (client->m_currentReply) {
        XModbusReply_setError(client->m_currentReply, XModbusDevice_TimeoutError, "Device closed");
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        XModbusReply_setState(reply, XModbusReply_State_No_Started);
        //XModbusReply_setFinished(reply, true);
    }

    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (io) {
        XSerialPort_delete_base((XSerialPort*)io);
        ((XModbusDevice*)client)->m_ioDevice = NULL;
    }

    client->m_interFrameDelay = 0;
    client->m_turnaroundDelay = 0;
    client->m_retryCount = 0;
    client->m_waitingForTurnaround = false;

    XClass_Deinit_Parent(XModbusClient, client);
}

// =============== sendRawRequest 虚函数实现 ===============
static XModbusReply* VXModbusRtuSerialClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress, XFuncParamType type) 
{
    if (!client || !request || serverAddress < 0 || serverAddress > 247) {
        return NULL;
    }

    XModbusRtuSerialClient* rtuClient = (XModbusRtuSerialClient*)client;
    XModbusDevice* device = (XModbusDevice*)client;

    XModbusReply* reply = NULL;
    if (type == XFuncParamType_Copy)
        reply = XModbusClient_createReply(client, request, serverAddress);
    else if (type == XFuncParamType_Move)
        reply = XModbusClient_createReply_move(client, request, serverAddress);
    else if (type == XFuncParamType_Ref)
        reply = XModbusClient_createReply_ref(client, request, serverAddress);
    if (!reply) return NULL;
    if (!XQueueBase_push_base(rtuClient->m_queue, &reply))
    {
        XModbusReply_deleteLater(reply);
        return NULL;
    }
    if (!rtuClient->m_currentReply)//当前是空闲的
        startNewRequest(client);
    return reply;
}