#include "XSerialPort.h"
#include "XDevice.h"
#include "XDeviceSerialPort.h"
#include "XEvent.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XIODevicePrivate.h"
#include <string.h>

static bool VXSerialPort_event(XSerialPort* port, XEvent* event);
static void VXSerialPort_timerEvent(XObject* obj, XTimerEvent* event);
static bool setIntProperty(XSerialPort* port, uint32_t property, int value);
static bool setInt64Property(XSerialPort* port, uint32_t property, int64_t value);
static bool setBoolProperty(XSerialPort* port, uint32_t property, bool value);

/**
 * @brief 停止接收轮询定时器
 */
static void VXSerialPort_rxTimerStop(XSerialPort* port)
{
    if (!port) return;
    if (port->m_rxTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)port, port->m_rxTimer);
        port->m_rxTimer = XTIMER_INVALID_ID;
    }
}

/**
 * @brief 启动接收轮询定时器（已启动则先停后启，保证间隔变更生效）
 */
static void VXSerialPort_rxTimerStart(XSerialPort* port)
{
    if (!port) return;
    VXSerialPort_rxTimerStop(port);
    if (port->rxPollIntervalMs <= 0) return;
    port->m_rxTimer = XObject_startTimer_ms((XObject*)port,
        (uint64_t)port->rxPollIntervalMs, XTimerType_CoarseTimer);
}

static bool VXSerialPort_open(XIODevice* io, XIODeviceBaseMode mode)
{
    XSerialPort* port = (XSerialPort*)io;
    XDeviceSerialPortOpenOptions options;
    XString* target;
    int error = XDeviceError_None;

    if (!port || !port->portName || port->portName[0] == '\0') {
        if (port) {
            port->error = XSerialPort_DeviceNotFoundError;
            XSerialPort_errorOccurred_signal(port, port->error);
        }
        return false;
    }
    /* [A8 复核根修] 双开守卫：同一 XSerialPort 实例在已打开状态再次 open
     * 直接拒绝（OpenError）。原实现无条件重开并覆盖 base.m_fd —— 旧 fd 泄漏、
     * 接收轮询定时器被静默重启，且 485 单总线唯一仲裁点被架空：第二个
     * open 调用方会悄悄抢走端口而无任何失败反馈。同一端口同一时刻只能被
     * 一个实例打开（契约见 XSerialPort.h）。
     * 只置 error 不发射 errorOccurred：拒绝性双开不是端口故障，发信号会
     * 误触发上层（如 Modbus 客户端）的错误处理链、把健康连接拉倒。 */
    if (port->isOpen || port->base.m_fd != XFD_INVALID) {
        port->error = XSerialPort_OpenError;
        return false;
    }
    if (mode == XIODevice_NotOpen || (mode & (XIODevice_ReadOnly | XIODevice_WriteOnly)) == 0) {
        port->error = XSerialPort_OpenError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }

    XDeviceSerialPortOpenOptions_init(&options);
    target = XString_create_utf8(port->portName);
    if (!target) {
        port->error = XSerialPort_ResourceError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }
    options.m_base.m_openMode = mode;
    options.m_base.m_target = target;
    options.m_owner = port;
    options.m_baudRate = port->baudRate;
    options.m_dataBits = port->dataBits;
    options.m_parity = port->parity;
    options.m_stopBits = port->stopBits;
    options.m_flowControl = port->flowControl;
    options.m_readBufferSize = port->readBufferSize;

    port->base.m_fd = XDevice_open(XDeviceType_Serial, &options.m_base, &error);
    XClassDelete((XClass*)target);
    if (port->base.m_fd == XFD_INVALID) {
        port->error = error == XDeviceError_NotFound ? XSerialPort_DeviceNotFoundError :
                      XSerialPort_OpenError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }
    port->base.m_openMode = (uint8_t)mode;
    port->isOpen = 1;
    port->error = XSerialPort_NoError;
    VXSerialPort_rxTimerStart(port);
    return true;
}

static void VXSerialPort_close(XIODevice* io)
{
    XSerialPort* port = (XSerialPort*)io;
    if (!port) return;
    VXSerialPort_rxTimerStop(port);
    if (port->base.m_fd != XFD_INVALID) {
        XDevice_close(port->base.m_fd);
        port->base.m_fd = XFD_INVALID;
    }
    port->isOpen = 0;
    XClass_Parent(XIODevice, EXIODevice_Close, void (*)(XIODevice*))(io);
}

static bool VXSerialPort_isSequential(const XIODevice* io)
{
    (void)io;
    return true;
}

static int64_t VXSerialPort_bytesAvailable(const XIODevice* io)
{
    const XSerialPort* port = (const XSerialPort*)io;
    XVariant value;
    int64_t result = 0;
    if (!port || port->base.m_fd == XFD_INVALID) return 0;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    if (XDevice_getProperty(port->base.m_fd, XDeviceSerialPortProperty_BytesAvailable, &value))
        result = XVariant_toInt64(&value);
    XClassDeinit((XClass*)&value);
    return result > 0 ? result : 0;
}

static int64_t VXSerialPort_bytesToWrite(const XIODevice* io)
{
    const XSerialPort* port = (const XSerialPort*)io;
    XVariant value;
    int64_t result = 0;
    if (!port || port->base.m_fd == XFD_INVALID) return 0;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    if (XDevice_getProperty(port->base.m_fd, XDeviceSerialPortProperty_BytesToWrite, &value))
        result = XVariant_toInt64(&value);
    XClassDeinit((XClass*)&value);
    return result > 0 ? result : 0;
}

static bool VXSerialPort_waitForReadyRead(XIODevice* io, int msecs)
{
    XSerialPort* port = (XSerialPort*)io;
    if (!port || !port->isOpen) {
        if (port) port->error = XSerialPort_NotOpenError;
        return false;
    }
    if (VXSerialPort_bytesAvailable(io) > 0) return true;
    return XIODevice_waitForReadyRead_base(io, msecs);
}

static bool VXSerialPort_waitForBytesWritten(XIODevice* io, int msecs)
{
    XSerialPort* port = (XSerialPort*)io;
    if (!port || !port->isOpen) {
        if (port) port->error = XSerialPort_NotOpenError;
        return false;
    }
    if (VXSerialPort_bytesToWrite(io) == 0) return true;
    return XClass_Parent(XIODevice, EXIODevice_WaitForBytesWritten,
                         bool (*)(XIODevice*, int))(io, msecs);
}

static int64_t VXSerialPort_readData(XIODevice* io, char* data, int64_t size)
{
    XSerialPort* port = (XSerialPort*)io;
    int64_t result;
    if (!port || port->base.m_fd == XFD_INVALID || !data || size <= 0) return -1;
    result = XDevice_read(port->base.m_fd, data, size);
    if (result < 0) {
        port->error = XSerialPort_ReadError;
        XSerialPort_errorOccurred_signal(port, port->error);
    }
    return result;
}

static int64_t VXSerialPort_writeData(XIODevice* io, const char* data, int64_t size)
{
    XSerialPort* port = (XSerialPort*)io;
    int64_t result;
    if (!port || port->base.m_fd == XFD_INVALID || !data || size <= 0) return -1;
    result = XDevice_write(port->base.m_fd, data, size);
    if (result < 0) {
        port->error = XSerialPort_WriteError;
        XSerialPort_errorOccurred_signal(port, port->error);
    } else if (result > 0) {
        XIODevice_bytesWritten_signal(io, result);
    }
    return result;
}

static int64_t VXSerialPort_readLineData(XIODevice* io, char* data, int64_t size)
{
    int64_t count = 0;
    if (!io || !data || size <= 0) return -1;
    while (count < size) {
        int64_t result = XDevice_read(io->m_fd, data + count, 1);
        if (result < 0) return count > 0 ? count : -1;
        if (result == 0) break;
        ++count;
        if (data[count - 1] == '\n') break;
    }
    return count;
}

static void VXSerialPort_deinit(XObject* object)
{
    XSerialPort* port = (XSerialPort*)object;
    if (!port) return;
    VXSerialPort_rxTimerStop(port);
    if (port->isOpen) VXSerialPort_close(&port->base);
    if (port->portName) {
        XFree_System(port->portName);
        port->portName = NULL;
    }
    XClass_Deinit_Parent(XIODevice, (XIODevice*)object);
}

/**
 * @brief 定时器事件：接收轮询拍
 * @details 串口设备层（XDeviceSerialPort）在 POSIX/Windows 上只有同步 read/write，
 *          没有 fd 级可读事件，因此这里用毫秒级定时器轮询接收缓冲：
 *          一旦有数据可读就发射 XIODevice 的 readyRead 信号，
 *          由上层（如 XModbusRtuSerialClient）取走数据。
 */
static void VXSerialPort_timerEvent(XObject* obj, XTimerEvent* event)
{
    if (!obj || !event) goto parent_call;

    XEvent_accept(event);
    XSerialPort* port = (XSerialPort*)obj;
    if (XTimerEvent_timerId(event) == port->m_rxTimer) {
        if (port->isOpen && port->base.m_fd != XFD_INVALID) {
            if (VXSerialPort_bytesAvailable((XIODevice*)port) > 0) {
                port->readyReadTriggered = 1;
                XIODevice_readyRead_signal((XIODevice*)port);
            }
        }
        return;
    }

parent_call:
    XClass_Parent(XIODevice, EXObject_TimerEvent, void (*)(XObject*, XTimerEvent*))(obj, event);
}

static bool VXSerialPort_event(XSerialPort* port, XEvent* event)
{
    if (port && event && event->m_type == XEVENT_TYPE_SOCK_ACT &&
        port->base.m_fd != XFD_INVALID) {
        XEvent* eventValue = event;
        XVarList* arguments = XVarList_Create(XVar(XEvent*, eventValue));
        if (arguments) {
            XDevice_control(port->base.m_fd, XDeviceSerialPortCommand_HandleEvent,
                             arguments, NULL);
            XVarList_delete(arguments);
        }
    }
    return XClass_Parent(XObject, EXObject_Event,
                         bool (*)(XObject*, XEvent*))((XObject*)port, event);
}

XVtable* XSerialPort_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(XSERIALPORT_VTABLE_SIZE)
    XVTABLE_INHERIT_XCLASS(XIODevice);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXSerialPort_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXSerialPort_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, VXSerialPort_isSequential);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, VXSerialPort_bytesAvailable);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesToWrite, VXSerialPort_bytesToWrite);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForReadyRead, VXSerialPort_waitForReadyRead);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForBytesWritten, VXSerialPort_waitForBytesWritten);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VXSerialPort_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadLineData, VXSerialPort_readLineData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VXSerialPort_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSerialPort_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXSerialPort_event);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXSerialPort_timerEvent);
    XCLASS_SET_CLASS_NAME_DEFAULT("XSerialPort");
    XCLASS_SHOW_SIZE_DEFAULT(XSerialPort);
    return XVTABLE_DEFAULT;
}

size_t XSerialPort_typetSize(void)
{
    return sizeof(XSerialPort);
}

void XSerialPort_init(XSerialPort* port)
{
    if (!port) return;
    XIODevice_init(&port->base);
    memset((char*)port + sizeof(XIODevice), 0, sizeof(*port) - sizeof(XIODevice));
    XClassSetVtable(port, XSerialPort);
    port->baudRate = XSerialPort_Baud9600;
    port->dataBits = XSerialPort_Data8;
    port->parity = XSerialPort_NoParity;
    port->stopBits = XSerialPort_OneStop;
    port->flowControl = XSerialPort_NoFlowControl;
    port->error = XSerialPort_NoError;
    port->readBufferSize = 512 * 1024;
    port->m_rxTimer = XTIMER_INVALID_ID;
    port->rxPollIntervalMs = XSerialPort_DefaultRxPollIntervalMs;
}

XSerialPort* XSerialPort_create_ex(XMemoryType memory)
{
    XSerialPort* port = (XSerialPort*)XMemory_malloc(sizeof(XSerialPort), memory);
    if (!port) return NULL;
    XSerialPort_init(port);
    Set_Class_Memory(port, memory);
    Set_Class_IsHeap(port, true);
    return port;
}

void XSerialPort_setPortName(XSerialPort* port, const char* name)
{
    size_t length;
    if (!port || !name) return;
    length = strlen(name);
    XFree_System(port->portName);
    port->portName = (char*)XMalloc_System(length + 1);
    if (port->portName) memcpy(port->portName, name, length + 1);
}

const char* XSerialPort_portName(const XSerialPort* port)
{
    return port ? port->portName : NULL;
}

static bool setIntProperty(XSerialPort* port, uint32_t property, int value)
{
    XVariant variant;
    bool ok;
    memset(&variant, 0, sizeof(variant));
    XVariant_init(&variant, NULL, 0, XVariantType_NULL);
    XVariant_setValue_int(&variant, value);
    ok = port->isOpen ? XDevice_setProperty(port->base.m_fd, (XDeviceProperty)property, &variant) : true;
    XClassDeinit((XClass*)&variant);
    return ok;
}

static bool setInt64Property(XSerialPort* port, uint32_t property, int64_t value)
{
    XVariant variant;
    bool ok;
    memset(&variant, 0, sizeof(variant));
    XVariant_init(&variant, NULL, 0, XVariantType_NULL);
    XVariant_setValue_int64(&variant, value);
    ok = port->isOpen ? XDevice_setProperty(port->base.m_fd, (XDeviceProperty)property, &variant) : true;
    XClassDeinit((XClass*)&variant);
    return ok;
}

static bool setBoolProperty(XSerialPort* port, uint32_t property, bool value)
{
    XVariant variant;
    bool ok;
    memset(&variant, 0, sizeof(variant));
    XVariant_init(&variant, NULL, 0, XVariantType_NULL);
    XVariant_setValue_bool(&variant, value);
    ok = port->isOpen ? XDevice_setProperty(port->base.m_fd, (XDeviceProperty)property, &variant) : true;
    XClassDeinit((XClass*)&variant);
    return ok;
}

bool XSerialPort_setBaudRate(XSerialPort* port, int32_t value, XSerialPort_Direction directions)
{
    int32_t old;
    if (!port || value <= 0) return false;
    old = port->baudRate;
    port->baudRate = value;
    if (!setIntProperty(port, XDeviceSerialPortProperty_BaudRate, value)) {
        port->baudRate = old;
        return false;
    }
    XSerialPort_baudRateChanged_signal(port, (uint32_t)value, directions);
    return true;
}

uint32_t XSerialPort_baudRate(const XSerialPort* port, XSerialPort_Direction directions)
{
    (void)directions;
    return port ? (uint32_t)port->baudRate : 0;
}

bool XSerialPort_setDataBits(XSerialPort* port, XSerialPort_DataBits value)
{
    XSerialPort_DataBits old;
    if (!port) return false;
    old = port->dataBits; port->dataBits = value;
    if (!setIntProperty(port, XDeviceSerialPortProperty_DataBits, value)) { port->dataBits = old; return false; }
    XSerialPort_dataBitsChanged_signal(port, value); return true;
}

XSerialPort_DataBits XSerialPort_dataBits(const XSerialPort* port)
{
    return port ? port->dataBits : XSerialPort_Data8;
}

bool XSerialPort_setParity(XSerialPort* port, XSerialPort_Parity value)
{
    XSerialPort_Parity old;
    if (!port) return false;
    old = port->parity; port->parity = value;
    if (!setIntProperty(port, XDeviceSerialPortProperty_Parity, value)) { port->parity = old; return false; }
    XSerialPort_parityChanged_signal(port, value); return true;
}

XSerialPort_Parity XSerialPort_parity(const XSerialPort* port)
{
    return port ? port->parity : XSerialPort_NoParity;
}

bool XSerialPort_setStopBits(XSerialPort* port, XSerialPort_StopBits value)
{
    XSerialPort_StopBits old;
    if (!port) return false;
    old = port->stopBits; port->stopBits = value;
    if (!setIntProperty(port, XDeviceSerialPortProperty_StopBits, value)) { port->stopBits = old; return false; }
    XSerialPort_stopBitsChanged_signal(port, value); return true;
}

XSerialPort_StopBits XSerialPort_stopBits(const XSerialPort* port)
{
    return port ? port->stopBits : XSerialPort_OneStop;
}

bool XSerialPort_setFlowControl(XSerialPort* port, XSerialPort_FlowControl value)
{
    XSerialPort_FlowControl old;
    if (!port) return false;
    old = port->flowControl; port->flowControl = value;
    if (!setIntProperty(port, XDeviceSerialPortProperty_FlowControl, value)) { port->flowControl = old; return false; }
    XSerialPort_flowControlChanged_signal(port, value); return true;
}

XSerialPort_FlowControl XSerialPort_flowControl(const XSerialPort* port)
{
    return port ? port->flowControl : XSerialPort_NoFlowControl;
}

void XSerialPort_setRxPollInterval(XSerialPort* port, int32_t intervalMs)
{
    if (!port) return;
    port->rxPollIntervalMs = intervalMs;
    if (port->isOpen) VXSerialPort_rxTimerStart(port);
}

int32_t XSerialPort_rxPollInterval(const XSerialPort* port)
{
    return port ? port->rxPollIntervalMs : 0;
}

XHandle XSerialPort_handle(const XSerialPort* port)
{
    XVariant value;
    void* handle = NULL;
    void** handleRef;
    if (!port || !port->isOpen) return (XHandle)-1;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    if (XDevice_getProperty(port->base.m_fd, XDeviceProperty_NativeHandle, &value)) {
        handleRef = (void**)XVariant_toRef(&value, XVariantType_Ptr);
        if (handleRef) handle = *handleRef;
    }
    XClassDeinit((XClass*)&value);
    return handle ? (XHandle)handle : (XHandle)-1;
}

bool XSerialPort_setDataTerminalReady(XSerialPort* port, bool value)
{
    if (!port || !setBoolProperty(port, XDeviceSerialPortProperty_DataTerminalReady, value)) return false;
    port->dataTerminalReady = value;
    XSerialPort_dataTerminalReadyChanged_signal(port, value);
    return true;
}

bool XSerialPort_isDataTerminalReady(const XSerialPort* port) { return port && port->dataTerminalReady; }

bool XSerialPort_setRequestToSend(XSerialPort* port, bool value)
{
    if (!port || !setBoolProperty(port, XDeviceSerialPortProperty_RequestToSend, value)) return false;
    port->requestToSend = value;
    XSerialPort_requestToSendChanged_signal(port, value);
    return true;
}

bool XSerialPort_isRequestToSend(const XSerialPort* port) { return port && port->requestToSend; }

bool XSerialPort_setBreakEnabled(XSerialPort* port, bool value)
{
    if (!port || !setBoolProperty(port, XDeviceSerialPortProperty_BreakEnabled, value)) return false;
    port->breakEnabled = value;
    XSerialPort_breakEnabledChanged_signal(port, value);
    return true;
}

bool XSerialPort_isBreakEnabled(const XSerialPort* port) { return port && port->breakEnabled; }

XSerialPort_PinoutSignal XSerialPort_pinoutSignals(const XSerialPort* port)
{
    XVariant value;
    int result = XSerialPort_NoSignal;
    if (!port || !port->isOpen) return XSerialPort_NoSignal;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    if (XDevice_getProperty(port->base.m_fd, XDeviceSerialPortProperty_PinoutSignals, &value))
        result = XVariant_toInt(&value);
    XClassDeinit((XClass*)&value);
    return (XSerialPort_PinoutSignal)result;
}

XSerialPort_Error XSerialPort_error(const XSerialPort* port)
{
    return port ? port->error : XSerialPort_NoError;
}

void XSerialPort_clearError(XSerialPort* port)
{
    if (!port) return;
    port->error = XSerialPort_NoError;
    if (port->isOpen) XDevice_control(port->base.m_fd, XDeviceSerialPortCommand_ClearError, NULL, NULL);
}

int64_t XSerialPort_readBufferSize(const XSerialPort* port)
{
    return port ? port->readBufferSize : 0;
}

void XSerialPort_setReadBufferSize(XSerialPort* port, int64_t size)
{
    int64_t old;
    if (!port || size <= 0) return;
    old = port->readBufferSize; port->readBufferSize = size;
    if (!setInt64Property(port, XDeviceSerialPortProperty_ReadBufferSize, size))
        port->readBufferSize = old;
}

bool XSerialPort_flush(XSerialPort* port)
{
    if (!port || !port->isOpen || !XDevice_flush(port->base.m_fd)) {
        if (port) port->error = XSerialPort_WriteError;
        return false;
    }
    return true;
}

bool XSerialPort_clear(XSerialPort* port, XSerialPort_Direction directions)
{
    XVarList* arguments;
    if (!port || !port->isOpen || (directions & XSerialPort_AllDirections) == 0) return false;
    arguments = XVarList_Create(XVar(XSerialPort_Direction, directions));
    if (!arguments) return false;
    if (!XDevice_control(port->base.m_fd, XDeviceSerialPortCommand_Clear, arguments, NULL)) {
        XVarList_delete(arguments); return false;
    }
    XVarList_delete(arguments);
    return true;
}

void* XSerialPort_errorOccurred_signal(XSerialPort* port, XSerialPort_Error error)
{
    XEmitSignal((XObject*)port, XSerialPort_errorOccurred_signal,
                XVarList_Create(XVar(XSerialPort_Error, error)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XSerialPort_baudRateChanged_signal(XSerialPort* port, uint32_t baudRate, XSerialPort_Direction dir)
{
    XEmitSignal((XObject*)port, XSerialPort_baudRateChanged_signal,
                XVarList_Create(XVar(uint32_t, baudRate), XVar(XSerialPort_Direction, dir)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XSerialPort_dataBitsChanged_signal(XSerialPort* port, XSerialPort_DataBits bits)
{
    XEmitSignal((XObject*)port, XSerialPort_dataBitsChanged_signal, XVarList_Create(XVar(XSerialPort_DataBits, bits)), NULL, NULL, XEVENT_PRIORITY_NORMAL); return NULL;
}

void* XSerialPort_parityChanged_signal(XSerialPort* port, XSerialPort_Parity value)
{
    XEmitSignal((XObject*)port, XSerialPort_parityChanged_signal, XVarList_Create(XVar(XSerialPort_Parity, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); return NULL;
}

void* XSerialPort_stopBitsChanged_signal(XSerialPort* port, XSerialPort_StopBits value)
{
    XEmitSignal((XObject*)port, XSerialPort_stopBitsChanged_signal, XVarList_Create(XVar(XSerialPort_StopBits, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); return NULL;
}

void* XSerialPort_flowControlChanged_signal(XSerialPort* port, XSerialPort_FlowControl value)
{
    XEmitSignal((XObject*)port, XSerialPort_flowControlChanged_signal, XVarList_Create(XVar(XSerialPort_FlowControl, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); return NULL;
}

void* XSerialPort_dataTerminalReadyChanged_signal(XSerialPort* port, bool value)
{
    XEmitSignal((XObject*)port, XSerialPort_dataTerminalReadyChanged_signal, XVarList_Create(XVar(bool, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); return NULL;
}

void* XSerialPort_requestToSendChanged_signal(XSerialPort* port, bool value)
{
    XEmitSignal((XObject*)port, XSerialPort_requestToSendChanged_signal, XVarList_Create(XVar(bool, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); return NULL;
}

void* XSerialPort_breakEnabledChanged_signal(XSerialPort* port, bool value)
{
    XEmitSignal((XObject*)port, XSerialPort_breakEnabledChanged_signal, XVarList_Create(XVar(bool, value)), NULL, NULL, XEVENT_PRIORITY_NORMAL); return NULL;
}
