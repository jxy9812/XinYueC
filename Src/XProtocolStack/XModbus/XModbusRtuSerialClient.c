#include "XModbusRtuSerialClient.h"
#include "XModbusRtuSerialClient_Protected.h"
#include "XModbusClient_Protected.h"
#include "XModbusReply_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XModbusAdu.h"
#include "XCircularQueue.h"
#include "XHashMap.h"
#include "XMemory.h"
#include "XCrc.h"
#include "XByteArray.h"
#include "XTimer.h"
#include "string.h"

static bool VXModbusRtuSerialClient_open(XModbusDevice* device);
static void VXModbusRtuSerialClient_close(XModbusDevice* device);
static void VXModbusRtuSerialClient_deinit(XModbusRtuSerialClient* client);
static void VXModbusRtuSerialClient_timerEvent(XObject* obj, XTimerEvent* event);
static XModbusReply* VXModbusRtuSerialClient_sendRawRequest(XModbusClient* client,
    const XModbusRequest* request, int serverAddress, XFuncParamType type);

static void XModbusRtuSerialClient_onReadyRead(XObject* receiver, XVarList* args);
static void XModbusRtuSerialClient_onBytesWritten(XObject* receiver, XVarList* args);
static void XModbusRtuSerialClient_onErrorOccurred(XObject* receiver, XVarList* args);

static bool startNewRequest(XModbusRtuSerialClient* client);
static void processReceivedFrame(XModbusRtuSerialClient* client, XByteArray* receiveBuffer);
static void handleRequestTimeout(XModbusRtuSerialClient* client);
static void XModbusRtuSerialClient_attemptReconnect(XModbusRtuSerialClient* client);
static bool canMatchRequestAndResponse(XModbusRtuSerialClient* client,
    const XModbusResponse* response, int serverAddress);
static bool tryCompleteFrame(XModbusRtuSerialClient* client);

static inline void interFrameTimerStop(XModbusRtuSerialClient* client)
{
    if (client->m_interFrameTimer != XTIMER_INVALID_ID) {
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
}

XModbusRtuSerialClient_State XModbusRtuSerialClient_state(const XModbusRtuSerialClient* client)
{
    if (!client) return XModbusRtuSerialClient_Idle;
    return (XModbusRtuSerialClient_State)client->m_state;
}

void XModbusRtuSerialClient_setState(XModbusRtuSerialClient* client, XModbusRtuSerialClient_State state)
{
    if (!client) return;
    client->m_state = (uint8_t)state;
}

int XModbusRtuSerialClient_calculateInterFrameDelay(int baudRate)
{
    if (baudRate <= 0) return 1750;
    int delay = (int)(38500000 / baudRate);
    return delay < 1750 ? 1750 : delay;
}

void XModbusRtuSerialClient_interFrameTimerStop(XModbusRtuSerialClient* client)
{
    interFrameTimerStop(client);
}

void XModbusRtuSerialClient_interFrameTimerStart(XModbusRtuSerialClient* client)
{
    interFrameTimerStart(client);
}

void XModbusRtuSerialClient_turnaroundTimerStart(XModbusRtuSerialClient* client)
{
    interFrameTimerStop(client);
    if (client->m_turnaroundDelay < 1) client->m_turnaroundDelay = 1;
    client->m_interFrameTimer = XObject_startTimer_ms((XObject*)client,
        client->m_turnaroundDelay, XTimerType_CoarseTimer);
}

bool XModbusRtuSerialClient_validateRtuFrame(const uint8_t* frame, size_t frameLen)
{
    if (!frame || frameLen < 4) return false;
    uint16_t calcCrc = XCrc_get16((uint8_t*)frame, (uint16_t)(frameLen - 2));
    uint16_t rcvCrc;
    XMemory_read_data(frame + frameLen - 2, XBYTE_ORDER_LITTLE_ENDIAN,
        (uint8_t*)&rcvCrc, sizeof(uint16_t));
    return calcCrc == rcvCrc;
}

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
#if SHOWCONTAINERSIZE
    printf("XModbusRtuSerialClient vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

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
    client->m_state = XModbusRtuSerialClient_Idle;
    client->m_retryCount = 0;
    client->m_waitingForTurnaround = 0;
    client->m_interFrameTimer = XTIMER_INVALID_ID;
    client->m_interFrameDelay = 0;
    client->m_turnaroundDelay = 100;
    client->m_currentServerAddress = 0;
    client->m_currentReply = NULL;
    client->m_receiveBuffer = XByteArray_create();
    client->m_requestAdu = NULL;
    client->m_queue = XCircularQueue_create(sizeof(XModbusReply*), 50);
    client->m_bytesWritten = 0;
}

int XModbusRtuSerialClient_interFrameDelay(const XModbusRtuSerialClient* client)
{
    if (!client) return 1750;
    if (client->m_interFrameDelay > 0) return client->m_interFrameDelay;
    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (serialPort) {
        int baudRate = XSerialPort_baudRate(serialPort, XSerialPort_AllDirections);
        return XModbusRtuSerialClient_calculateInterFrameDelay(baudRate);
    }
    return 1750;
}

void XModbusRtuSerialClient_setInterFrameDelay(XModbusRtuSerialClient* client, int microseconds)
{
    if (!client) return;
    if (microseconds < 0) { client->m_interFrameDelay = 0; return; }
    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (serialPort) {
        int baudRate = XSerialPort_baudRate(serialPort, XSerialPort_AllDirections);
        int autoDelay = XModbusRtuSerialClient_calculateInterFrameDelay(baudRate);
        if (microseconds < autoDelay) { client->m_interFrameDelay = 0; return; }
    }
    client->m_interFrameDelay = microseconds;
}

int XModbusRtuSerialClient_turnaroundDelay(const XModbusRtuSerialClient* client)
{
    return client ? client->m_turnaroundDelay : 100;
}

void XModbusRtuSerialClient_setTurnaroundDelay(XModbusRtuSerialClient* client, int turnaroundDelay)
{
    if (client) client->m_turnaroundDelay = turnaroundDelay;
}

XSerialPort* XModbusRtuSerialClient_serialPort(const XModbusRtuSerialClient* client)
{
    if (!client) return NULL;
    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    return (XSerialPort*)io;
}

static bool VXModbusRtuSerialClient_open(XModbusDevice* device)
{
    if (!device) return false;
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)device;
    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (!serialPort) {
        serialPort = XSerialPort_create();
        if (serialPort) {
            ((XModbusDevice*)client)->m_ioDevice = (XIODevice*)serialPort;
            XObject_connect_1((XObject*)serialPort,
                XSignal(XIODevice_readyRead_signal),
                (XObject*)client, XModbusRtuSerialClient_onReadyRead, XConnectionType_Auto);
            XObject_connect_1((XObject*)serialPort,
                XSignal(XIODevice_bytesWritten_signal),
                (XObject*)client, XModbusRtuSerialClient_onBytesWritten, XConnectionType_Auto);
            XObject_connect_1((XObject*)serialPort,
                XSignal(XSerialPort_errorOccurred_signal),
                (XObject*)client, XModbusRtuSerialClient_onErrorOccurred, XConnectionType_Auto);
        } else {
            XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Serial port not available");
            return false;
        }
    }
    const XVariant* portNameVar = XModbusDevice_connectionParameter_const(device,
        XModbusDevice_SerialPortNameParameter);
    if (portNameVar) {
        XString* portName = XVariant_toString_const(portNameVar);
        if (portName) XSerialPort_setPortName(serialPort, XString_toUtf8(portName));
    }
    const XVariant* baudRateVar = XModbusDevice_connectionParameter_const(device,
        XModbusDevice_SerialBaudRateParameter);
    if (baudRateVar) {
        int baudRate = XVariant_toInt(baudRateVar);
        if (baudRate > 0) XSerialPort_setBaudRate(serialPort, baudRate, XSerialPort_AllDirections);
    }
    const XVariant* dataBitsVar = XModbusDevice_connectionParameter_const(device,
        XModbusDevice_SerialDataBitsParameter);
    if (dataBitsVar)
        XSerialPort_setDataBits(serialPort, (XSerialPort_DataBits)XVariant_toInt(dataBitsVar));
    const XVariant* parityVar = XModbusDevice_connectionParameter_const(device,
        XModbusDevice_SerialParityParameter);
    if (parityVar)
        XSerialPort_setParity(serialPort, (XSerialPort_Parity)XVariant_toInt(parityVar));
    const XVariant* stopBitsVar = XModbusDevice_connectionParameter_const(device,
        XModbusDevice_SerialStopBitsParameter);
    if (stopBitsVar)
        XSerialPort_setStopBits(serialPort, (XSerialPort_StopBits)XVariant_toInt(stopBitsVar));
    if (!XSerialPort_open_base(serialPort, XIODevice_ReadWrite)) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Failed to open serial port");
        return false;
    }
    XByteArray* flushBuf = XIODevice_readAll_3((XIODevice*)serialPort);
    if (flushBuf) XByteArray_delete_base(flushBuf);
    if (client->m_interFrameDelay == 0)
        client->m_interFrameDelay = XModbusRtuSerialClient_interFrameDelay(client);
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    XModbusDevice_setError(device, XModbusDevice_NoError, NULL);
    ((XModbusClient*)client)->m_reconnectAttempts = 0;
    XModbusClient_reconnectTimerStop((XModbusClient*)client);
    client->m_state = XModbusRtuSerialClient_Idle;
    client->m_retryCount = 0;
    client->m_waitingForTurnaround = 0;
    client->m_bytesWritten = 0;
    return true;
}

static void VXModbusRtuSerialClient_close(XModbusDevice* device)
{
    if (!device) return;
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)device;
    interFrameTimerStop(client);
    XModbusClient_timeoutTimerStop((XModbusClient*)client);
    if (client->m_currentReply) {
        XModbusReply_setError(client->m_currentReply, XModbusDevice_ReplyAbortedError, "Device closed");
        XModbusReply_setState(client->m_currentReply, XModbusReply_State_No_Started);
        client->m_currentReply = NULL;
    }
    XModbusReply* reply = NULL;
    while (XQueueBase_receive_base(client->m_queue, &reply)) {
        if (reply) {
            XModbusReply_setError(reply, XModbusDevice_ReplyAbortedError,
                "Reply aborted due to connection closure");
            XModbusReply_setState(reply, XModbusReply_State_No_Started);
        }
    }
    if (client->m_requestAdu) XByteArray_clear_base(client->m_requestAdu);
    if (client->m_receiveBuffer) XByteArray_clear_base(client->m_receiveBuffer);
    client->m_state = XModbusRtuSerialClient_Idle;
    client->m_retryCount = 0;
    client->m_waitingForTurnaround = 0;
    client->m_bytesWritten = 0;
    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (serialPort) XSerialPort_close_base(serialPort);
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

static void VXModbusRtuSerialClient_deinit(XModbusRtuSerialClient* client)
{
    if (!client) return;
    if (XModbusDevice_state((XModbusDevice*)client) != XModbusDevice_UnconnectedState)
        VXModbusRtuSerialClient_close((XModbusDevice*)client);
    if (client->m_receiveBuffer) { XByteArray_delete_base(client->m_receiveBuffer); client->m_receiveBuffer = NULL; }
    if (client->m_requestAdu) { XByteArray_delete_base(client->m_requestAdu); client->m_requestAdu = NULL; }
    if (client->m_queue) { XQueueBase_delete_base(client->m_queue); client->m_queue = NULL; }
    XModbusClient_deinitLater(&client->m_base);
}

static XModbusReply* VXModbusRtuSerialClient_sendRawRequest(XModbusClient* client,
    const XModbusRequest* request, int serverAddress, XFuncParamType type)
{
    if (!client || !request || serverAddress < 0 || serverAddress > 247) return NULL;
    XModbusRtuSerialClient* rtuClient = (XModbusRtuSerialClient*)client;
    XModbusReply* reply = NULL;
    if (type == XFuncParamType_Copy)
        reply = XModbusClient_createReply(client, request, serverAddress);
    else if (type == XFuncParamType_Move)
        reply = XModbusClient_createReply_move(client, request, serverAddress);
    else if (type == XFuncParamType_Ref)
        reply = XModbusClient_createReply_ref(client, request, serverAddress);
    if (!reply) return NULL;
    if (!XQueueBase_push_base(rtuClient->m_queue, &reply)) {
        XModbusReply_deleteLater(reply);
        return NULL;
    }
    if (rtuClient->m_state == XModbusRtuSerialClient_Idle && !rtuClient->m_currentReply)
        startNewRequest(rtuClient);
    return reply;
}

static bool startNewRequest(XModbusRtuSerialClient* client)
{
    if (!client) return false;
    if (client->m_state != XModbusRtuSerialClient_Idle) return false;
    if (client->m_currentReply != NULL) return false;
    XModbusDevice* device = (XModbusDevice*)client;
    if (XModbusDevice_state(device) != XModbusDevice_ConnectedState) return false;
    XIODevice* io = device->m_ioDevice;
    if (!io || !XIODevice_isOpen(io)) return false;

    XModbusReply* reply = NULL;
    if (!XQueueBase_receive_base(client->m_queue, &reply)) return false;
    if (!reply || !reply->m_request) return false;

    const XModbusRequest* request = reply->m_request;
    int serverAddress = reply->m_serverAddress;
    XByteArray* pdu = ((XModbusPdu*)request)->m_data;
    if (!pdu || XByteArray_isEmpty_base(pdu)) {
        XModbusReply_setError(reply, XModbusDevice_InvalidResponseError, "Empty request PDU");
        XModbusReply_setState(reply, XModbusReply_State_No_Started);
        return false;
    }

    client->m_currentReply = reply;
    client->m_currentServerAddress = (uint8_t)serverAddress;
    client->m_retryCount = 0;
    client->m_bytesWritten = 0;

    if (!client->m_requestAdu)
        client->m_requestAdu = XByteArray_create();
    else
        XByteArray_clear_base(client->m_requestAdu);

    XByteArray* aduFrame = XModbusAdu_createRtuFrame(serverAddress, (const XModbusPdu*)request);
    if (!aduFrame) {
        XModbusReply_setError(reply, XModbusDevice_InvalidResponseError, "Failed to create ADU");
        XModbusReply_setState(reply, XModbusReply_State_No_Started);
        client->m_currentReply = NULL;
        return false;
    }
    XByteArray_push_back_2(client->m_requestAdu,
        XByteArray_data_base(aduFrame), XByteArray_size_base(aduFrame));
    XByteArray_delete_base(aduFrame);

    XByteArray_clear_base(client->m_receiveBuffer);
    XModbusReply_clearIntermediateError(reply);
    XModbusReply_setState(reply, XModbusReply_State_Requesting);
    client->m_state = XModbusRtuSerialClient_WaitingForReply;

    int64_t sent = XIODevice_write_2(io, client->m_requestAdu);
    client->m_bytesWritten = sent;
    if (sent <= 0) {
        client->m_state = XModbusRtuSerialClient_Idle;
        XModbusReply_setError(reply, XModbusDevice_WriteError, "Write failed");
        XModbusReply_setState(reply, XModbusReply_State_No_Started);
        client->m_currentReply = NULL;
        return false;
    }

    if (serverAddress == 0) {
        client->m_waitingForTurnaround = 1;
        XModbusRtuSerialClient_turnaroundTimerStart(client);
    } else {
        client->m_waitingForTurnaround = 0;
        XModbusClient_timeoutTimerStart((XModbusClient*)client);
    }
    return true;
}

static bool canMatchRequestAndResponse(XModbusRtuSerialClient* client,
    const XModbusResponse* response, int serverAddress)
{
    if (!client || !client->m_currentReply || !response) return false;
    if ((int)client->m_currentReply->m_serverAddress != serverAddress) return false;
    XModbusPdu_FunctionCode reqFc = XModbusPdu_functionCode(
        (const XModbusPdu*)client->m_currentReply->m_request);
    XModbusPdu_FunctionCode respFc = XModbusPdu_functionCode((const XModbusPdu*)response);
    if (respFc & XMODBUS_PDU_EXCEPTION_BYTE)
        return (reqFc == (respFc & ~XMODBUS_PDU_EXCEPTION_BYTE));
    return reqFc == respFc;
}

static bool tryCompleteFrame(XModbusRtuSerialClient* client)
{
    XByteArray* buf = client->m_receiveBuffer;
    if (!buf) return false;
    const uint8_t* data = XByteArray_data_base(buf);
    size_t size = XByteArray_size_base(buf);
    if (!data || size < 4) return false;

    XModbusPdu_FunctionCode fc = (XModbusPdu_FunctionCode)data[1];
    XModbusResponse* tempResp = XModbusResponse_create_with_code(
        fc & ~XMODBUS_PDU_EXCEPTION_BYTE);
    if (!tempResp) return false;

    size_t dataSize = size - 2;
    if (dataSize > 2)
        XModbusPdu_setData(tempResp, data + 2, (uint16_t)(dataSize - 2));

    int16_t pduSize = XModbusResponse_calculateDataSize(tempResp);
    XModbusResponse_delete_base(tempResp);
    if (pduSize < 0) return false;
    size_t expectedAduSize = 2 + (size_t)pduSize + 2;
    return size >= expectedAduSize;
}

static void processReceivedFrame(XModbusRtuSerialClient* client, XByteArray* receiveBuffer)
{
    if (!client || !client->m_currentReply) return;
    XModbusReply* reply = client->m_currentReply;
    XModbusReply_setState(reply, XModbusReply_State_Responding);
    const uint8_t* data = XByteArray_data_base(receiveBuffer);
    size_t len = XByteArray_size_base(receiveBuffer);
    if (!data || len < 4) {
        XModbusReply_setError(reply, XModbusDevice_InvalidResponseError, "Frame too short");
        goto cleanup;
    }
    if (!XModbusRtuSerialClient_validateRtuFrame(data, len)) {
        XModbusReply_addIntermediateError(reply, XModbusDevice_ResponseCrcError);
        goto cleanup;
    }
    int serverAddress = data[0];
    uint8_t fc = data[1];
    XModbusResponse* response = XModbusResponse_create_with_code(
        (XModbusPdu_FunctionCode)(fc & ~XMODBUS_PDU_EXCEPTION_BYTE));
    if (!response) {
        XModbusReply_setError(reply, XModbusDevice_UnknownError, "Failed to create response");
        goto cleanup;
    }
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
            XModbusReply_setError(reply, XModbusDevice_ProtocolError, errorMsg);
        }
        XModbusResponse_delete_base(response);
        goto cleanup;
    }
    if (!canMatchRequestAndResponse(client, response, serverAddress)) {
        XModbusReply_addIntermediateError(reply, XModbusDevice_ResponseCrcError);
        XModbusResponse_delete_base(response);
        goto cleanup;
    }
    if (len > 4)
        XModbusPdu_setData(response, data + 2, (uint16_t)(len - 4));
    if (!reply->m_result)
        reply->m_result = XModbusDataUnit_create();
    XModbusDataUnit* resultUnit = reply->m_result;
    bool success = XModbusClient_processResponse_base((XModbusClient*)client, response, resultUnit);
    if (!success)
        XModbusReply_setError(reply, XModbusDevice_UnknownError, "Response processing failed");
    XModbusResponse_delete_base(response);
cleanup:
    client->m_currentReply = NULL;
    client->m_retryCount = 0;
    client->m_bytesWritten = 0;
    XModbusClient_timeoutTimerStop((XModbusClient*)client);
    XModbusReply_setState(reply, XModbusReply_State_Finished);
    client->m_state = XModbusRtuSerialClient_Idle;
    startNewRequest(client);
}

static void XModbusRtuSerialClient_onReadyRead(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)receiver;
    if (!client) return;
    XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
    if (!io) return;
    XByteArray* newData = XIODevice_readAll_4(io);
    if (!newData || XByteArray_isEmpty_base(newData)) {
        if (newData) XByteArray_delete_base(newData);
        return;
    }
    XByteArray_push_back_2(client->m_receiveBuffer,
        XByteArray_data_base(newData), XByteArray_size_base(newData));
    XByteArray_delete_base(newData);
    if (client->m_state != XModbusRtuSerialClient_WaitingForReply) return;
    if (!client->m_currentReply) return;
    if (!tryCompleteFrame(client)) return;
    if (client->m_waitingForTurnaround) return;

    XByteArray* buf = client->m_receiveBuffer;
    const uint8_t* data = XByteArray_data_base(buf);
    size_t size = XByteArray_size_base(buf);
    XModbusPdu_FunctionCode fc = (XModbusPdu_FunctionCode)data[1];
    XModbusResponse* tempResp = XModbusResponse_create_with_code(
        fc & ~XMODBUS_PDU_EXCEPTION_BYTE);
    if (!tempResp) return;
    size_t dataSize = size - 2;
    if (dataSize > 2)
        XModbusPdu_setData(tempResp, data + 2, (uint16_t)(dataSize - 2));
    int16_t pduSize = XModbusResponse_calculateDataSize(tempResp);
    XModbusResponse_delete_base(tempResp);
    if (pduSize < 0) return;
    size_t frameSize = 2 + (size_t)pduSize + 2;
    XByteArray* frameBuf = XByteArray_create();
    XByteArray_push_back_2(frameBuf, data, frameSize);
    size_t remaining = size - frameSize;
    XByteArray* newBuf = XByteArray_create();
    if (remaining > 0)
        XByteArray_push_back_2(newBuf, data + frameSize, remaining);
    XByteArray_swap_base(client->m_receiveBuffer, newBuf);
    XByteArray_delete_base(newBuf);
    client->m_state = XModbusRtuSerialClient_ProcessReply;
    processReceivedFrame(client, frameBuf);
    XByteArray_delete_base(frameBuf);
}

static void XModbusRtuSerialClient_onBytesWritten(XObject* receiver, XVarList* args)
{
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)receiver;
    if (!client || !args) return;
    XVariant* var = XVarList_front_base(args);
    if (var) client->m_bytesWritten += XVariant_toInt64(var);
}

static void handleRequestTimeout(XModbusRtuSerialClient* client)
{
    if (!client) return;
    XModbusClient* baseClient = (XModbusClient*)client;
    XModbusClient_timeoutTimerStop(baseClient);
    if (!client->m_currentReply) return;
    XModbusReply* reply = client->m_currentReply;
    client->m_retryCount++;
    int maxRetries = XModbusClient_numberOfRetries(baseClient);
    if (client->m_retryCount <= maxRetries) {
        XIODevice* io = ((XModbusDevice*)client)->m_ioDevice;
        if (io && XIODevice_isOpen(io) && client->m_requestAdu &&
            !XByteArray_isEmpty_base(client->m_requestAdu)) {
            XModbusReply_addIntermediateError(reply, XModbusDevice_TimeoutError);
            XModbusReply_setState(reply, XModbusReply_State_Requesting);
            XByteArray_clear_base(client->m_receiveBuffer);
            int64_t sent = XIODevice_write_2(io, client->m_requestAdu);
            client->m_bytesWritten = sent;
            if (sent > 0) { XModbusClient_timeoutTimerStart(baseClient); return; }
        }
    }
    client->m_currentReply = NULL;
    client->m_retryCount = 0;
    client->m_bytesWritten = 0;
    XModbusReply_setError(reply, XModbusDevice_TimeoutError, "Response timeout");
    XModbusReply_setState(reply, XModbusReply_State_Timeout);
    client->m_state = XModbusRtuSerialClient_Idle;
    startNewRequest(client);
}

static void VXModbusRtuSerialClient_timerEvent(XObject* obj, XTimerEvent* event)
{
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)obj;
    if (!client) return;
    int timerId = XTimerEvent_getTimerId(event);
    if (timerId == client->m_interFrameTimer) {
        client->m_interFrameTimer = XTIMER_INVALID_ID;
        if (client->m_waitingForTurnaround) {
            client->m_waitingForTurnaround = 0;
            if (client->m_currentReply) {
                XModbusReply_setState(client->m_currentReply, XModbusReply_State_Finished);
                client->m_currentReply = NULL;
            }
            client->m_retryCount = 0;
            client->m_state = XModbusRtuSerialClient_Idle;
            startNewRequest(client);
            return;
        }
        if (client->m_state == XModbusRtuSerialClient_Idle && !client->m_currentReply)
            startNewRequest(client);
        return;
    }
    if (timerId == ((XModbusClient*)client)->m_timeoutTimer) {
        ((XModbusClient*)client)->m_timeoutTimer = XTIMER_INVALID_ID;
        if (client->m_state == XModbusRtuSerialClient_WaitingForReply)
            handleRequestTimeout(client);
        return;
    }
    if (timerId == ((XModbusClient*)client)->m_reconnectTimer) {
        ((XModbusClient*)client)->m_reconnectTimer = XTIMER_INVALID_ID;
        XModbusRtuSerialClient_attemptReconnect(client);
        return;
    }
}

static void XModbusRtuSerialClient_onErrorOccurred(XObject* receiver, XVarList* args)
{
    XModbusRtuSerialClient* client = (XModbusRtuSerialClient*)receiver;
    if (!client) return;
    (void)args;
    XModbusDevice* device = (XModbusDevice*)client;
    interFrameTimerStop(client);
    XModbusClient_timeoutTimerStop((XModbusClient*)client);
    if (client->m_currentReply) {
        XModbusReply* reply = client->m_currentReply;
        client->m_currentReply = NULL;
        XModbusReply_setError(reply, XModbusDevice_ConnectionError, "Serial port error");
        XModbusReply_setState(reply, XModbusReply_State_No_Started);
    }
    client->m_state = XModbusRtuSerialClient_Idle;
    client->m_retryCount = 0;
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
    XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Serial port error");
    XModbusClient* baseClient = (XModbusClient*)client;
    if (baseClient->m_autoReconnect) {
        if (baseClient->m_maxReconnectAttempts < 0 ||
            baseClient->m_reconnectAttempts < baseClient->m_maxReconnectAttempts) {
            XModbusClient_reconnectTimerStart(baseClient);
        }
    }
}

static void XModbusRtuSerialClient_attemptReconnect(XModbusRtuSerialClient* client)
{
    if (!client) return;
    XModbusClient* baseClient = (XModbusClient*)client;
    XModbusDevice* device = (XModbusDevice*)client;
    XModbusClient_reconnectTimerStop(baseClient);
    if (XModbusDevice_state(device) == XModbusDevice_ConnectedState) {
        baseClient->m_reconnectAttempts = 0;
        return;
    }
    baseClient->m_reconnectAttempts++;
    XModbusDevice_setState(device, XModbusDevice_ConnectingState);
    XSerialPort* serialPort = XModbusRtuSerialClient_serialPort(client);
    if (!serialPort) {
        XModbusDevice_setError(device, XModbusDevice_ConfigurationError,
            "Serial port not available");
        XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
        goto schedule_retry;
    }
    if (XSerialPort_isOpen(serialPort))
        XSerialPort_close_base(serialPort);
    if (!XSerialPort_open_base(serialPort, XIODevice_ReadWrite)) {
        XModbusDevice_setError(device, XModbusDevice_ConnectionError, "Failed to reopen serial port");
        XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
        goto schedule_retry;
    }
    XByteArray* flushBuf = XIODevice_readAll_3((XIODevice*)serialPort);
    if (flushBuf) XByteArray_delete_base(flushBuf);
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    XModbusDevice_setError(device, XModbusDevice_NoError, NULL);
    baseClient->m_reconnectAttempts = 0;
    client->m_state = XModbusRtuSerialClient_Idle;
    client->m_retryCount = 0;
    client->m_waitingForTurnaround = 0;
    client->m_bytesWritten = 0;
    XByteArray_clear_base(client->m_receiveBuffer);
    if (client->m_interFrameDelay == 0)
        client->m_interFrameDelay = XModbusRtuSerialClient_interFrameDelay(client);
    return;
schedule_retry:
    if (baseClient->m_autoReconnect &&
        (baseClient->m_maxReconnectAttempts < 0 ||
         baseClient->m_reconnectAttempts < baseClient->m_maxReconnectAttempts)) {
        XModbusClient_reconnectTimerStart(baseClient);
    }
}

