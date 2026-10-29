#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_RTU_ON
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
// =============== 虚函数前置声明 ===============
static bool VXModbusRtuSerialClient_open(XModbusDevice* device);
static void VXModbusRtuSerialClient_close(XModbusDevice* device);
static void VXModbusRtuSerialClient_deinit(XModbusRtuSerialClient* client);
static void VXModbusRtuSerialClient_timerEvent(XObject* obj, XTimerEvent* event);
static XModbusReply* VXModbusRtuSerialClient_sendRawRequest(XModbusClient* client, const XModbusRequest* request, int serverAddress, XFuncParamType type);
//static bool VXModbusRtuSerialClient_processResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);
//static bool VXModbusRtuSerialClient_processPrivateResponse(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

// =============== 槽函数声明 ===============
static void XModbusRtuSerialClient_onReadyRead(XObject* receiver, XVarList* args);
static void XModbusRtuSerialClient_onErrorOccurred(XObject* receiver, XVarList* args);
static void processReceivedFrame(XModbusRtuSerialClient* client, XByteArray* receiveBuffer);
static bool startNewRequest(XModbusRtuSerialClient* client);
static bool startRequestReply(XModbusRtuSerialClient* client, XModbusReply* reply);
static void handleRequestTimeout(XModbusRtuSerialClient* client);
static void XModbusRtuSerialClient_attemptReconnect(XModbusRtuSerialClient* client);
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
static inline void turnaroundFrameTimerStart(XModbusRtuSerialClient* client)
{
    XModbusClient_timeoutTimerStop(client);
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

    uint16_t calculatedCrc = XCrc16_calculate(XCrc16_Algorithm_Modbus, frame,
                                               frameLen - 2);

    // Modbus RTU CRC 是小端序
    uint16_t receivedCrc;
    XMemory_read_data(frame + frameLen - 2, XBYTE_ORDER_LITTLE_ENDIAN, (uint8_t*)&receivedCrc, sizeof(uint16_t));

    return calculatedCrc == receivedCrc;
}

// =============== 类初始化 ===============
XVtable* XModbusRtuSerialClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusRtuSerialClient)
        XVTABLE_INHERIT_XCLASS(XModbusClient);

    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusRtuSerialClient_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusRtuSerialClient_close);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusRtuSerialClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusRtuSerialClient_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_SendRawRequest, VXModbusRtuSerialClient_sendRawRequest);
    // 重写 ProcessResponse 和 ProcessPrivateResponse
    //XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_ProcessResponse, VXModbusRtuSerialClient_processResponse);
    //XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_ProcessPrivateResponse, VXModbusRtuSerialClient_processPrivateResponse);

    XCLASS_SHOW_SIZE_DEFAULT(XModbusRtuSerialClient);
    return XVTABLE_DEFAULT;
}

// =============== 创建/初始化 ===============
XModbusRtuSerialClient* XModbusRtuSerialClient_create_ex(XMemoryType memory)
{
    XModbusRtuSerialClient* client = XMemory_malloc(sizeof(XModbusRtuSerialClient), memory);
    if (!client) return NULL;
    XModbusRtuSerialClient_init(client);
    Set_Class_Memory(client, memory); Set_Class_IsHeap(client, true);
    return client;
}

void XModbusRtuSerialClient_init(XModbusRtuSerialClient* client)
{
    if (!client) return;

    XModbusClient_init(&client->m_base);
    XClassGetVtable(client) = XModbusRtuSerialClient_class_init();



    client->m_interFrameDelay = 0;
    client->m_turnaroundDelay = 100;
    client->m_currentReply = NULL;
    client->m_interFrameTimer = XTIMER_INVALID_ID;
    client->m_currentServerAddress = 0;
    client->m_retryCount = 0;
    client->m_waitingForTurnaround = false;
    client->m_requestData = NULL;
    client->m_receiveBuffer = XByteArray_create();  // 创建接收缓冲区
    /* 发送队列复用基类双优先队列（XModbusClient_serialQueuePush/Pop），
     * RTU 恒串行：请求恒入队，由基类按优先级出队。 */
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
    if (!serialPort)
    {
        serialPort = XSerialPort_create();
        if (serialPort) {
            ((XModbusDevice*)client)->m_ioDevice = (XIODevice*)serialPort;

            XObject_connect_1((XObject*)serialPort,
                XSignal(XIODevice_readyRead_signal),
                (XObject*)client,
                XModbusRtuSerialClient_onReadyRead,
                XConnectionType_Auto);

            XObject_connect_1((XObject*)serialPort,
                XSignal(XSerialPort_errorOccurred_signal),
                (XObject*)client,
                XModbusRtuSerialClient_onErrorOccurred,
                XConnectionType_Auto);
        }
        else
        {
            XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Serial port not available");
            return false;
        }
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

    // [A8 双开守卫配套] open 已开端口现在会返回 false：内部调用方先补 close。
    if (XSerialPort_isOpen(serialPort)) {
        XSerialPort_close_base(serialPort);
    }

    // 打开串口
    bool result = XSerialPort_open_base(serialPort, XIODevice_ReadWrite);
    if (!result) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Failed to open serial port");
        return false;
    }

    // 清除缓冲区
    XByteArray* array = XIODevice_readAll_3((XIODevice*)serialPort);
    if (array) XClassDelete(array);

    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    XModbusDevice_setError(device, XModbusDevice_NoError, NULL);

    // 连接成功，重置重连计数器
    ((XModbusClient*)client)->m_reconnectAttempts = 0;
    XModbusClient_reconnectTimerStop((XModbusClient*)client);

    if (((XModbusRtuSerialClient*)device)->m_interFrameDelay == 0)
        ((XModbusRtuSerialClient*)device)->m_interFrameDelay = XModbusRtuSerialClient_interFrameDelay(client);
    return true;
}

static void VXModbusRtuSerialClient_close(XModbusDevice* device)
{
    if (!device) return;

    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)device;

    /* [A6 复核根修] 原实现把 XModbusDevice* 裸传给子类型参数
     * （interFrameTimerStop 收 XModbusRtuSerialClient*、
     *   XModbusClient_timeoutTimerStop 收 XModbusClient*），
     * 仅因首成员偏移为 0 而碰巧可用，改传正确类型。 */
    interFrameTimerStop(client);
    XModbusClient_timeoutTimerStop((XModbusClient*)client);

    if (client->m_currentReply) {
        /* [A4 不变式 + A5 终态不复活] 先摘引用再置错；finished 已到持有方后
         * 状态机不再回拉 No_Started（原实现 setError 后又 setState(No_Started)
         * 复活终态 reply）。 */
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        XModbusReply_setError(reply, XModbusDevice_ConnectionError, "Device closed");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
    }

    /* [A2/A4 收口] 排空发送队列：close 是终结路径，排队中的 Reply 必须
     * 恰好一次终态收口（对齐 TCP 客户端 close 口径），不再悬挂。 */
    XModbusClient_serialQueueClear((XModbusClient*)client,
        XModbusDevice_ConnectionError, "Device closed");

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
        /* [A4 复核根修] CRC 错是可重试的中间错误：仅记中间错误并走超时重发。
         * 原实现在此 setError —— finished 同步到达持有方后 RTU 层仍解引用
         * m_currentReply 继续重发，违背"终态后绝不解引用"不变式。
         * 终结只发生在重试耗尽的 handleRequestTimeout（恰好一次）。 */
        XModbusReply_addIntermediateError(reply, XModbusDevice_ResponseCrcError);
        handleRequestTimeout(client);//重发请求
        return;
    }

    if (data[0] != client->m_currentServerAddress) {
        /* [A4 复核根修] 同上：地址不匹配走中间错误+重发，不提前终结。 */
        XModbusReply_addIntermediateError(reply, XModbusDevice_ResponseRequestMismatch);
        handleRequestTimeout(client);//重发请求
        return;
    }
    if (!reply->m_rawResult)
        reply->m_rawResult = XModbusResponse_create();
    // 解析响应PDU
   /* XModbusResponse response;
    XModbusResponse_init(&response);*/
    XModbusResponse* response = reply->m_rawResult;
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
            /* [A4 不变式] 先摘 m_currentReply 再置错：setError 会让 finished
             * 同步到达持有方，此后本层绝不再解引用 m_currentReply（全程只用局部
             * reply，且持有方按框架约定以 deleteLater 延迟释放）。 */
            client->m_currentReply = NULL;
            XModbusReply_setError(reply, XModbusDevice_ProtocolError, errorMsg);
        }
        goto cleanup;
    }

    // 正常响应：设置PDU数据（不含地址、功能码和CRC）
    // data + 2 跳过地址和功能码
    // len - 4 = 总长度 - 地址(1) - 功能码(1) - CRC(2)
    if (len > 4) {
        XModbusPdu_setData(response, data + 2, len - 4);
    }




    if (!reply->m_result)
        reply->m_result = XModbusDataUnit_create();

    // 获取结果单元
    //XModbusDataUnit* resultUnit = XModbusDataUnit_create();
    XModbusDataUnit* resultUnit = reply->m_result;

    /* XString* text= XByteArray_to16HexString(receiveBuffer);
     XPrintf_string(text);
     XPrintf("\n");
     XClassDelete(text);*/

     // 通过保护API调用虚函数（会调到子类重写的实现）
    bool success = XModbusClient_processResponse_base((XModbusClient*)client, response, resultUnit);

    if (success) {
        //XModbusReply_setResult_ref(client->m_currentReply, resultUnit);
    }
    else {
        /* [A4 不变式] 同上：先摘引用再置错。 */
        client->m_currentReply = NULL;
        XModbusReply_setError(reply, XModbusDevice_UnknownError, "Response processing failed");
        // 释放临时结果单元
        /*if (resultUnit) {
            XClassDelete(resultUnit);
        }*/
    }
    //XClassDeinit(&response);
cleanup:
    //XModbusReply* reply = client->m_currentReply;
    client->m_currentReply = NULL;
    client->m_retryCount = 0;
    //XPrintf("准备调用槽函数\n");
    //XModbusReply_setFinished(reply, true);
    XModbusReply_setState(reply, XModbusReply_State_Finished);
    /* [A2 续泵 + 空闲守卫] 终结后必须续泵（否则超时/广播后整条队列停摆）；
     * 但 finished 是同步直达持有方的——持有方 done 回调可能已提交新请求并经
     * sendRawRequest 的内层泵填住 m_currentReply，此时外层再泵就会弹出无辜
     * 队列条目撞上收口分支（回归实证：v1 设频写帧被错误终结，485 门用例
     * "站 1 输出频率 25.0Hz" 失败）。故必须判定空闲后才续泵。 */
    if (!client->m_currentReply)
        startNewRequest(client);
}
bool startNewRequest(XModbusRtuSerialClient* client)
{
    XModbusReply* reply = NULL;
    /* 基类双优先队列出队：先高优先后普通（485 单总线串行，已发帧不可抢占） */
    if (!XModbusClient_serialQueuePop((XModbusClient*)client, NULL, &reply, NULL))
        return false;
    return startRequestReply(client, reply);
}

/** @brief 取出待发请求并执行发送（出队后的公共发送路径）。
 *  @note  [A3 收口不变式] reply 一经出队就不再回队：任何失败路径都必须先以
 *         error 终态终结该 reply（finished 同步到达持有方）再返回/续泵，
 *         绝不让已出队的 reply 悬空——否则持有方（如链路层 inflight）
 *         永不 finished，单飞永久卡死。 */
static bool startRequestReply(XModbusRtuSerialClient* client, XModbusReply* reply)
{
    if (!client) return false;
    if (!reply || !reply->m_request || reply->m_serverAddress < 0 || reply->m_serverAddress > 247) {
        /* 出队条目失效：以错误终态收口，避免静默丢弃 */
        if (reply) {
            XModbusReply_setError(reply, XModbusDevice_UnknownError, "Invalid queued request");
            XModbusReply_setState(reply, XModbusReply_State_Finished);
        }
        return false;
    }
    const XModbusRequest* request = reply->m_request;
    int serverAddress = reply->m_serverAddress;

    XModbusRtuSerialClient* rtuClient = (XModbusRtuSerialClient*)client;
    XModbusDevice* device = (XModbusDevice*)client;

    if (XModbusDevice_state(device) != XModbusDevice_ConnectedState) {
        /* 未连接：出队 reply 以错误终态收口（排队期间的连接断开不再悬空） */
        XModbusReply_setError(reply, XModbusDevice_ConnectionError, "Device not connected");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
        return false;
    }

    XIODevice* io = device->m_ioDevice;
    if (!io || !XIODevice_isOpen(io)) {
        XModbusReply_setError(reply, XModbusDevice_ConnectionError, "I/O device closed");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
        return false;
    }

    if (rtuClient->m_currentReply != NULL) {
        /* 出队时在途占用（所有泵点均已空闲守卫，此分支为防御性收口） */
        XModbusReply_setError(reply, XModbusDevice_UnknownError, "Client busy");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
        return false;
    }
    XByteArray* pdu = ((XModbusPdu*)request)->m_data;
    if (!pdu || XByteArray_isEmpty_base(pdu)) {
        XModbusReply_setError(reply, XModbusDevice_UnknownError, "Empty request PDU");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
        return false;
    }
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
    uint16_t crc = XCrc16_calculate(
        XCrc16_Algorithm_Modbus, XContainerDataAddr(rtuClient->m_requestData),
        XContainerSize(rtuClient->m_requestData));
    XMemory_write_data(&XByteArray_back_base(rtuClient->m_requestData) + 1,
                       XBYTE_ORDER_LITTLE_ENDIAN, (const uint8_t*)&crc,
                       sizeof(crc));
    XContainerSize(rtuClient->m_requestData) += 2;

    /* XString* text= XByteArray_to16HexString(rtuClient->m_requestData);
     XPrintf_string(text);
     XPrintf("\n");
     XClassDelete(text);*/

     // 发送RTU请求
    int64_t sent = XIODevice_write_2(io, rtuClient->m_requestData);
    /* 失败可能是 0（未写）也可能是负数（XIODevice_writeData → -1，如 pty
     * master 已关的 EIO）：只判 !sent 会把 -1 当成功，reply 在发送已失败后
     * 仍被登记在途（此时同步 errorOccurred 已把 state 拉成 Unconnected，
     * 超时定时器又被状态门槛吞掉 → 单飞永久卡死）。 */
    if (sent <= 0) {
        /* [A3 根修] 发送失败：已出队的 reply 先以错误终态收口（finished 同步
         * 到达持有方），再续泵下一条。原实现直接递归泵下一条，本 reply 未置错
         * 未终结——持有方（如 Hub/链路层的 inflight）永不 finished，单飞永久卡死。
         * 续泵须判空闲：终结的 finished 同步链可能已提交并发送新请求。 */
        XModbusReply_setError(reply, XModbusDevice_WriteError, "Failed to write request");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
        if (!rtuClient->m_currentReply)
            return startNewRequest(client);
        return true;
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
        XModbusClient_timeoutTimerStart(client);
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
    //    if (data) XClassDelete(data);
    //    return;
    //}

    //// 将数据追加到接收缓冲区
    //XByteArray_push_back_2(client->m_receiveBuffer,
    //    XContainerDataAddr(data),
    //    XByteArray_size_base(data));
    //XClassDelete(data);

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
static bool handleInterFrameTimeout(XModbusRtuSerialClient* client)
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
        processReceivedFrame(client, client->m_receiveBuffer);
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
    XModbusClient_timeoutTimerStop(client);
    if (!client->m_currentReply) return;

    // 广播消息的 turnaround 延迟结束
    if (client->m_waitingForTurnaround) {
        client->m_waitingForTurnaround = false;
        /* [A4 不变式 + A2 续泵] 先摘引用再进终态，终结后空闲才续泵——
         * 原实现广播回合收口不调 startNewRequest，与正常收口不对称，
         * 广播之后整条发送队列停摆。finished 同步链可能已填住
         * m_currentReply（见 processReceivedFrame 收口注释），须守卫。 */
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        client->m_retryCount = 0;
        XModbusReply_setState(reply, XModbusReply_State_Timeout);
        if (!client->m_currentReply)
            startNewRequest(client);
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

        /* 只认正数：负数是写失败（同上，会同步触发 errorOccurred 把在途回合
         * 收口并摘引用），必须落穿到下方终结分支，不得当成功续命超时。 */
        if (sent > 0) {
            // 清空接收缓冲区
            if (client->m_receiveBuffer) {
                XByteArray_clear_base(client->m_receiveBuffer);
            }
            XModbusClient_timeoutTimerStart(client);
            return;
        }
    }

    // 重试次数用尽或重试失败
    /* [A4 不变式 + A2 续泵] 先摘引用再 setError：finished 同步到达持有方后
     * 本层绝不再解引用 m_currentReply；终结后空闲才续泵下一条——原实现重试
     * 耗尽收口不调 startNewRequest，超时之后整条队列停摆。finished 同步链
     * 可能已提交并发送新请求（守卫原因见 processReceivedFrame 收口注释）。 */
    XModbusReply* reply = client->m_currentReply;
    client->m_currentReply = NULL;
    client->m_retryCount = 0;
    XModbusReply_setError(reply, XModbusDevice_TimeoutError, "Request timeout");
    XModbusReply_setState(reply, XModbusReply_State_Timeout);
    if (!client->m_currentReply)
        startNewRequest(client);
}
// =============== 定时器事件处理 ===============
static void VXModbusRtuSerialClient_timerEvent(XObject* obj, XTimerEvent* event) {
    if (!obj || !event) goto parent_call;

    XEvent_accept(event);
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)obj;
    XTimerId timerId = XTimerEvent_timerId(event);
    /* 处理请求超时定时器（先于状态门槛）：链路掉线（state 已非 Connected）
     * 而回合仍在途时超时也必须收口——若被状态门槛吞掉，在途 reply 永不
     * finished，单飞永久卡死（战役 S4-B 实证）。handleRequestTimeout 内部
     * 对空在途/重试写失败均有终态收口，掉线态下触发是安全路径。 */
    if (timerId == ((XModbusClient*)client)->m_timeoutTimer) {
        handleRequestTimeout(client);
        return;
    }
    if (XModbusDevice_state(client) == XModbusDevice_ConnectedState)
    {
        // 处理帧间延迟定时器 - 一帧接收完成
        if (timerId == client->m_interFrameTimer) {
            handleInterFrameTimeout(client);
            return;
        }

        if(client->m_base.m_poolMap)
        {
            XModbusReply** pReply = XHashMap_value_base(client->m_base.m_poolMap, &timerId);
            if (pReply)
            {
                {
                    /* 重发按原请求优先级回队（request 传 NULL：由 Reply 持有） */
                    uint8_t prio = 0;
                    if (pReply && *pReply && (*pReply)->m_request) prio = (*pReply)->m_request->m_priority;
                    XModbusClient_serialQueuePush((XModbusClient*)client, NULL, *pReply,
                                                  0, (XModbusRequestPriority)prio);
                }
                if (!client->m_currentReply)//当前是空闲的
                    startNewRequest(client);
                return;
            }
        }
    }

    // 检查是否是重连定时器
    if (timerId == ((XModbusClient*)client)->m_reconnectTimer) {
        // 重连定时器只触发一次，停止它
        XModbusClient_reconnectTimerStop((XModbusClient*)client);

        // 执行重连尝试
        XModbusRtuSerialClient_attemptReconnect(client);
        return;
    }

parent_call:
    XClass_Parent(XModbusClient, EXObject_TimerEvent, void (*)(XObject*, XTimerEvent*))(obj, event);
}

// =============== 析构函数 ===============
static void VXModbusRtuSerialClient_deinit(XModbusRtuSerialClient* client)
{
    if (!client) return;
    XModbusDevice_disconnectDevice(client);

    if (client->m_requestData) {
        XClassDelete(client->m_requestData);
        client->m_requestData = NULL;
    }
    if (client->m_receiveBuffer) {
        XClassDelete(client->m_receiveBuffer);
        client->m_receiveBuffer = NULL;
    }
    // 释放请求队列容器（init 中创建；队列内的 XModbusReply* 归调用方所有，
    // 这里只销毁容器本身，不触碰队列元素）
    /* 发送队列由基类持有并释放（此处不重复释放） */
    if (client->m_currentReply) {
        /* [A4 不变式 + A5 终态不复活] 先摘引用再置错并进终态；
         * 原实现 setError（已触发 finished）后又拉回 No_Started。 */
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        XModbusReply_setError(reply, XModbusDevice_TimeoutError, "Device closed");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
    }

    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (io) {
        XIODevice_deleteLater((XSerialPort*)io);
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
    /* 入基类双优先队列：request 传 NULL（请求所有权随 Reply，避免双重释放）；
     * 优先级取自请求（控制命令=High 由上层打标）。 */
    if (!XModbusClient_serialQueuePush((XModbusClient*)rtuClient, NULL, reply,
                                       serverAddress, (XModbusRequestPriority)request->m_priority))
    {
        XModbusReply_deleteLater(reply);
        return NULL;
    }
    if (!rtuClient->m_currentReply)//当前是空闲的
        startNewRequest(client);
    return reply;
}

// =============== 错误处理槽函数 ===============
static void XModbusRtuSerialClient_onErrorOccurred(XObject* receiver, XVarList* args)
{
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)receiver;
    if (!client) return;

    (void)args;  // 可从中获取具体的串口错误码

    XModbusDevice* device = (XModbusDevice*)client;

    // 停止当前请求
    interFrameTimerStop(client);
    XModbusClient_timeoutTimerStop((XModbusClient*)client);

    if (client->m_currentReply)
    {
        /* [A4 复核根修 + A5] 原实现先按重试余量把 m_currentReply 置 NULL，
         * 再对可能为 NULL 的指针 setError —— setError 直接返回，该 reply
         * 永不 finished，上层持有方永久卡死；未置 NULL 分支又把已置错的
         * reply 拉回 No_Started（终态复活）。现统一：先摘引用，再以
         * ConnectionError 恰好一次终态收口（串口故障时在途回合不可续）。 */
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        client->m_retryCount = 0;
        XModbusReply_setError(reply, XModbusDevice_ConnectionError, "Serial port error");
        XModbusReply_setState(reply, XModbusReply_State_Finished);
    }

    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
    XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Serial port error");

    // 检查是否需要自动重连
    XModbusClient* baseClient = (XModbusClient*)client;
    if (baseClient->m_autoReconnect) {
        // 检查是否达到最大重连次数
        if (baseClient->m_maxReconnectAttempts < 0 ||
            baseClient->m_reconnectAttempts < baseClient->m_maxReconnectAttempts) {

            // 启动重连定时器
            XModbusClient_reconnectTimerStart((XModbusClient*)client);
        }
    }
}

// =============== 重连处理 ===============
static void XModbusRtuSerialClient_attemptReconnect(XModbusRtuSerialClient* client)
{
    if (!client) return;

    XModbusClient* baseClient = (XModbusClient*)client;
    XModbusDevice* device = (XModbusDevice*)client;

    // 停止重连定时器
    XModbusClient_reconnectTimerStop(baseClient);

    // 检查是否已经连接
    if (XModbusDevice_state(device) == XModbusDevice_ConnectedState) {
        baseClient->m_reconnectAttempts = 0;
        return;
    }

    // 增加重连计数
    baseClient->m_reconnectAttempts++;

    // 尝试重新连接
    XModbusDevice_setState(device, XModbusDevice_ConnectingState);

    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (!serialPort) {
        XModbusDevice_setError(device, XModbusDevice_ConfigurationError, "Serial port not available for reconnection");
        XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
        return;
    }

    // 如果串口之前是打开的，先关闭
    if (XSerialPort_isOpen(serialPort)) {
        XSerialPort_close_base(serialPort);
    }

    // 重新打开串口
    bool result = XSerialPort_open_base(serialPort, XIODevice_ReadWrite);
    if (!result) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Failed to reopen serial port");
        XModbusDevice_setState(device, XModbusDevice_UnconnectedState);

        // 重连失败，检查是否需要继续尝试
        if (baseClient->m_autoReconnect &&
            (baseClient->m_maxReconnectAttempts < 0 ||
                baseClient->m_reconnectAttempts < baseClient->m_maxReconnectAttempts)) {
            XModbusClient_reconnectTimerStart(baseClient);
        }
        return;
    }

    // 清除缓冲区
    XByteArray* array = XIODevice_readAll_3((XIODevice*)serialPort);
    if (array) XClassDelete(array);

    // 重连成功
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    XModbusDevice_setError(device, XModbusDevice_NoError, NULL);
    baseClient->m_reconnectAttempts = 0;

    // 更新帧间延迟
    if (client->m_interFrameDelay == 0) {
        client->m_interFrameDelay = XModbusRtuSerialClient_interFrameDelay(client);
    }

    /* [A2 续泵] 重连成功后续泵发送队列：串口错误终结在途回合时队列中排队
     * 的请求保留到重连成功后上线（后续回合完成再逐条续泵）。 */
    if (!client->m_currentReply) {
        startNewRequest(client);
    }
}

#endif /* XMODBUS_RTU_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
