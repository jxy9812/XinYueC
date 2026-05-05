#include "XSerialPort.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XWaitCondition.h"
#include "XString.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XIODevicePrivate.h"
#include <string.h>
static bool VXObject_event(XSerialPort* port, XEvent* e);
void XSerialPort_platform_XChildEvent_handler(XEventSockAct* event, XSerialPort* receiver);
bool XSerialPort_platform_open(XSerialPort* port, XIODeviceBaseMode mode);
void XSerialPort_platform_close(XSerialPort* port);
int64_t XSerialPort_platform_read(XSerialPort* port, char* data, int64_t maxSize);
int64_t XSerialPort_platform_write(XSerialPort* port, const char* data, int64_t len);
bool XSerialPort_platform_applyConfig(XSerialPort* port);
void XSerialPort_platform_poll(XSerialPort* port);
//bool XSerialPort_platform_waitForReadyRead(XSerialPort* port, int msecs);
//bool XSerialPort_platform_waitForBytesWritten(XSerialPort* port, int msecs);
XHandle XSerialPort_platform_handle(const XSerialPort* port);

bool XSerialPort_setBaudRate(XSerialPort* port, int32_t baudRate, XSerialPort_Direction directions) {
    if (!port) return false;
    if (port->baudRate == baudRate) return true; // Avoid redundant signal
    port->baudRate = baudRate;
    bool ok = true;
    if (XSerialPort_isOpen(port)) {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_baudRateChanged_signal(port, (uint32_t)baudRate, directions);
    }
    return ok;
}
bool VXObject_event(XSerialPort* port, XEvent* e)
{
    if (e->type == XEVENT_TYPE_SOCK_ACT)
    {
        XSerialPort_platform_XChildEvent_handler((XEventSockAct*)e,port);
    }
    return XClass_Parent(XObject, EXObject_Event, bool (*)(XSerialPort*, XEvent*))(port, e);
}

// ========== 虚函数重写 ==========
static void VXSerialPort_poll(XObject* obj) {
    XSerialPort* port = (XSerialPort*)obj;

    // 首先，让平台层处理其特定的轮询逻辑（例如，检查 WaitCommEvent 是否完成）
    XSerialPort_platform_poll(port);

    // 然后，执行通用的可读性检查
    // 只有打开且可读时才检测
    if (!port || !port->isOpen || !XIODevice_isReadable(&port->base)) {
        return;
    }

    // 检查是否有新数据可读
    int64_t available = XSerialPort_bytesAvailable_base(port);
    if (available > 0) {
        XIODevice_readyRead_signal(port);  // emit readyRead
    }
}

static bool VXSerialPort_open(XIODevice* io, XIODeviceBaseMode mode) {
    XSerialPort* port = (XSerialPort*)io;
    if (!port->portName || strlen(port->portName) == 0) {
        port->error = XSerialPort_DeviceNotFoundError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }
    if (!XSerialPort_platform_open(port, mode)) {
        port->error = XSerialPort_OpenError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }
    if (!XSerialPort_platform_applyConfig(port)) {
        XSerialPort_platform_close(port);
        port->error = XSerialPort_UnsupportedOperationError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }
    port->error = XSerialPort_NoError;

    io->m_openMode = mode;
    return true;
}

static void VXSerialPort_close(XIODevice* io) {
    XSerialPort* port = (XSerialPort*)io;
    if (port->isOpen) {
        XSerialPort_platform_close(port);
    }
    XIODevice_close_base(io);
}

static bool VXSerialPort_isSequential(const XIODevice* io) {
    (void)io;
    return true;
}

//static bool VXSerialPort_canReadLine(const XIODevice* io) {
//
//    return XIODevicePrivate_canReadLineFromBuffer(io->m_d);
//}

static bool VXSerialPort_waitForReadyRead(XIODevice* io, int msecs) {
    XSerialPort* port = (XSerialPort*)io;

    if (!XIODevice_isOpen(io)) {
        port->error = XSerialPort_NotOpenError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }

    if (XIODevice_bytesAvailable_base(io) > 0)
        return true;

    return XClass_Parent(XIODevice, EXIODevice_WaitForReadyRead, bool (*)(XIODevice*, int))(io, msecs);
}

static bool VXSerialPort_waitForBytesWritten(XIODevice* io, int msecs) {
    XSerialPort* port = (XSerialPort*)io;

    if (!XIODevice_isOpen(io)) {
        port->error = XSerialPort_NotOpenError;
        XSerialPort_errorOccurred_signal(port, port->error);
        return false;
    }

    if (XIODevice_bytesToWrite_base(io) == 0)
        return true;

    return XClass_Parent(XIODevice,EXIODevice_WaitForBytesWritten, bool (*)(XIODevice * , int ))(io,msecs);
}

static int64_t VXSerialPort_readData(XIODevice* io, char* data, int64_t maxSize) {
    XSerialPort* port = (XSerialPort*)io;
    if (maxSize <= 0) return 0;
    int64_t result = XSerialPort_platform_read(port, data, maxSize);
    if (result < 0) {
        port->error = XSerialPort_ReadError;
        XSerialPort_errorOccurred_signal(port, port->error);
    }
    if (XSerialPort_bytesAvailable_base(io) == 0)
        port->readyReadTriggered = false;
    return result;
}
static int64_t VXIODevice_readLineData(XIODevice* io, char* data, int64_t maxSize)
{
    XSerialPort* port = (XSerialPort*)io;
    if (maxSize <= 0) return 0;
    int64_t result = XIODevicePrivate_readLineFromBuffer(io->m_d, data, maxSize);
    if (result < 0) {
        port->error = XSerialPort_ReadError;
        XSerialPort_errorOccurred_signal(port, port->error);
    }
    if (XSerialPort_bytesAvailable_base(io) == 0)
        port->readyReadTriggered = false;
    return result;
}
static int64_t VXSerialPort_writeData(XIODevice* io, const char* data, int64_t len) {
    XSerialPort* port = (XSerialPort*)io;
    if (len <= 0) return 0;
    int64_t result = XSerialPort_platform_write(port, data, len);
    if (result < 0) {
        port->error = XSerialPort_WriteError;
        XSerialPort_errorOccurred_signal(port, port->error);
    }
    // Note: Qt usually emits bytesWritten immediately after a successful write.
    // If your design requires it, uncomment the next two lines.
     else if (result > 0) {
         XIODevice_bytesWritten_signal(io, result);
     }
    return result;
}

static void VXSerialPort_deinit(XObject* obj) {
    XSerialPort* port = (XSerialPort*)obj;
    if (port->isOpen) {
        XSerialPort_platform_close(port);
    }
    if (port->portName) {
        XFree_System(port->portName);
        port->portName = NULL;
    }
    XClass_Deinit_Parent(XIODevice, obj);
}

XVtable* XSerialPort_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XSERIALPORT_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_XCLASS(XIODevice);
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXSerialPort_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXSerialPort_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, VXSerialPort_isSequential);
    //XVTABLE_OVERLOAD_DEFAULT(EXIODevice_CanReadLine, VXSerialPort_canReadLine);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForReadyRead, VXSerialPort_waitForReadyRead);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForBytesWritten, VXSerialPort_waitForBytesWritten);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VXSerialPort_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadLineData, VXIODevice_readLineData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VXSerialPort_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSerialPort_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXSerialPort_poll);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXObject_event);
#if SHOWCONTAINERSIZE
    printf("XSerialPort size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}



// ========== Public API 实现 ==========
void XSerialPort_setPortName(XSerialPort* port, const char* name) {
	if (!port || !name) return;
	
	if (port->portName)
		XFree_System(port->portName);
	int len = strlen(name);
	port->portName = XMalloc_System(len+1);
	memcpy(port->portName,name, len+1);
}

const char* XSerialPort_portName(const XSerialPort* port) {
	return port ? port->portName : NULL;
}



int32_t XSerialPort_baudRate(const XSerialPort* port, XSerialPort_Direction directions) {
	(void)directions;
	return port ? port->baudRate : 0;
}



XSerialPort_DataBits XSerialPort_dataBits(const XSerialPort* port) {
	return port ? port->dataBits : XSerialPort_Data8;
}



XSerialPort_Parity XSerialPort_parity(const XSerialPort* port) {
	return port ? port->parity : XSerialPort_NoParity;
}



XSerialPort_StopBits XSerialPort_stopBits(const XSerialPort* port) {
	return port ? port->stopBits : XSerialPort_OneStop;
}



XHandle XSerialPort_handle(const XSerialPort* port)
{
    if (!port || !port->isOpen) {
        return -1;
    }
    // 委托给平台层实现
    return XSerialPort_platform_handle(port);
}

XSerialPort_FlowControl XSerialPort_flowControl(const XSerialPort* port) {
	return port ? port->flowControl : XSerialPort_NoFlowControl;
}

// DTR / RTS / Break 由平台 .c 实现（见下文）

XSerialPort_Error XSerialPort_error(const XSerialPort* port) {
	return port ? port->error : XSerialPort_NoError;
}

void XSerialPort_clearError(XSerialPort* port) {
	if (port) port->error = XSerialPort_NoError;
}

int64_t XSerialPort_readBufferSize(const XSerialPort* port) {
	return port ? port->readBufferSize : 0;
}

void XSerialPort_setReadBufferSize(XSerialPort* port, int64_t size) {
	if (port && size > 0) {
		port->readBufferSize = size;
		// Note: In real implementation, this would affect the internal buffer size
		// For Windows, this is handled by the OS, so we just store the value
	}
}

bool XSerialPort_isDataTerminalReady(const XSerialPort* port) {
	return port ? port->dataTerminalReady : false;
}

bool XSerialPort_isRequestToSend(const XSerialPort* port) {
	return port ? port->requestToSend : false;
}

bool XSerialPort_isBreakEnabled(const XSerialPort* port) {
	return port ? port->breakEnabled : false;
}
void* XSerialPort_errorOccurred_signal(XSerialPort* port, XSerialPort_Error error)
{
	XEmitSignal(port, XSerialPort_errorOccurred_signal, XVarList_Create(XVar(XSerialPort_Error, error)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_baudRateChanged_signal(XSerialPort* port, uint32_t baudRate, XSerialPort_Direction dir)
{
    XEmitSignal(port, XSerialPort_errorOccurred_signal, XVarList_Create(XVar(uint32_t, baudRate), XVar(XSerialPort_Direction, dir)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_dataBitsChanged_signal(XSerialPort* port, XSerialPort_DataBits bits)
{
	XEmitSignal(port, XSerialPort_dataBitsChanged_signal, XVarList_Create(XVar(XSerialPort_DataBits, bits)),NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_parityChanged_signal(XSerialPort* port, XSerialPort_Parity parity)
{
	XEmitSignal(port, XSerialPort_parityChanged_signal, XVarList_Create(XVar(XSerialPort_Parity, parity)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_stopBitsChanged_signal(XSerialPort* port, XSerialPort_StopBits bits)
{
	XEmitSignal(port, XSerialPort_stopBitsChanged_signal, XVarList_Create(XVar(XSerialPort_StopBits, bits)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_flowControlChanged_signal(XSerialPort* port, XSerialPort_FlowControl control)
{
	XEmitSignal(port, XSerialPort_flowControlChanged_signal, XVarList_Create(XVar(XSerialPort_FlowControl, control)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_dataTerminalReadyChanged_signal(XSerialPort* port, bool set)
{
	XEmitSignal(port, XSerialPort_dataTerminalReadyChanged_signal, XVarList_Create(XVar(bool, set)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_requestToSendChanged_signal(XSerialPort* port, bool set)
{
	XEmitSignal(port, XSerialPort_requestToSendChanged_signal, XVarList_Create(XVar(bool, set)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_breakEnabledChanged_signal(XSerialPort* port, bool enabled)
{
	XEmitSignal(port, XSerialPort_breakEnabledChanged_signal, XVarList_Create(XVar(bool, enabled)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}