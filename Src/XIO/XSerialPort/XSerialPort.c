#include "XSerialPort_p.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XWaitCondition.h"
#include "XString.h"
#include "XVariant.h"
#include "XVariantList.h"
#include <string.h>

// ========== 私有数据生命周期 ==========
static XSerialPortPrivate* XSerialPortPrivate_create(void) 
{
	XSerialPortPrivate* d = (XSerialPortPrivate*)XMemory_calloc(1, sizeof(XSerialPortPrivate));
	if (!d) return NULL;

	d->baudRate = XSerialPort_Baud9600;
	d->dataBits = XSerialPort_Data8;
	d->parity = XSerialPort_NoParity;
	d->stopBits = XSerialPort_OneStop;
	d->flowControl = XSerialPort_NoFlowControl;
	d->readBufferSize = 512 * 1024;  // Qt default: 512 KB
	d->platform = NULL;
	d->isOpen = false;

	return d;
}

static void XSerialPortPrivate_delete(XSerialPortPrivate* d)
{
	if (!d) return;
	if (d->portName) XMemory_free(d->portName);
	XMemory_free(d);
}
// Signal handlers for waitFor
static void readyReadHandler(XObject* receiver, void* args, XObject* sender) {
	XSerialPort* port = (XSerialPort*)sender;
	XSerialPortPrivate* d = port->d_ptr;
	XMutex_lock(d->waitMutex);
	d->readyReadTriggered = true;
	XWaitCondition_wakeOne(d->waitCondition);
	XMutex_unlock(d->waitMutex);
}

static void bytesWrittenHandler(XObject* receiver, void* args, XObject* sender) {
	XSerialPort* port = (XSerialPort*)sender;
	XSerialPortPrivate* d = port->d_ptr;
	XMutex_lock(d->waitMutex);
	d->bytesWrittenTriggered = true;
	XWaitCondition_wakeOne(d->waitCondition);
	XMutex_unlock(d->waitMutex);
}
// ========== 虚函数重写 ==========
static void VXSerialPort_poll(XObject* obj) {
	XSerialPort* port = (XSerialPort*)obj;
	XSerialPortPrivate* d = port->d_ptr;

	// 只有打开且启用自动信号时才检测
	if (!d || !d->isOpen || !XIODevice_isReadable(&port->base)) {
		return;
	}

	// 检查是否有新数据可读
	int64_t available = platform_bytesAvailable(d);
	if (available > 0) {
		XIODevice_readyRead_signal(port);  // emit readyRead
	}

	// 可选：检查写缓冲是否清空（用于 bytesWritten）
	// 注意：Qt 通常在 write() 成功后立即 emit，而非轮询
	// 所以这里一般不需要轮询 bytesWritten
}
static bool VXSerialPort_open(XIODevice* io, XIODeviceBaseMode mode) {
	XSerialPort* port = (XSerialPort*)io;
	XSerialPortPrivate* d = port->d_ptr;
	if (!d->portName || strlen(d->portName) == 0) {
		d->error = XSerialPort_DeviceNotFoundError;
		XSerialPort_errorOccurred_signal(port, d->error);
		return false;
	}
	if (!platform_open(d, port, port->d_ptr->portName, mode)) {
		d->error = XSerialPort_OpenError;
		XSerialPort_errorOccurred_signal(port, d->error);
		return false;
	}
	if (!platform_applyConfig(d)) {
		platform_close(d);
		d->error = XSerialPort_UnsupportedOperationError;
		XSerialPort_errorOccurred_signal(port, d->error);
		return false;
	}
	d->error = XSerialPort_NoError;

	// Connect signals for waitFor
	XObject_connect(io, XSignal(XIODevice_readyRead_signal), io, readyReadHandler, XConnectionType_Auto);
	XObject_connect(io, XSignal(XIODevice_bytesWritten_signal), io, bytesWrittenHandler, XConnectionType_Auto);
	io->m_openMode = mode;
	return true;
}

static void VXSerialPort_close(XIODevice* io) {
	XSerialPort* port = (XSerialPort*)io;
	XSerialPortPrivate* d = port->d_ptr;
	if (platform_isOpen(d)) {
		platform_close(d);
	}
	XIODevice_close_base(io);
}

static bool VXSerialPort_isSequential(const XIODevice* io) {
	(void)io;
	return true;
}

static int64_t VXSerialPort_bytesAvailable(const XIODevice* io) {
	const XSerialPort* port = (const XSerialPort*)io;
	return platform_bytesAvailable(port->d_ptr);
}

static int64_t VXSerialPort_bytesToWrite(const XIODevice* io) {
	const XSerialPort* port = (const XSerialPort*)io;
	return platform_bytesToWrite(port->d_ptr);
}

static bool VXSerialPort_canReadLine(const XIODevice* io) {
	// For sequential devices, we can always attempt to read a line
	return XIODevice_bytesAvailable_base(io) > 0;
}

static bool VXSerialPort_waitForReadyRead(XIODevice* io, int msecs) {
	XSerialPort* port = (XSerialPort*)io;
	XSerialPortPrivate* d = port->d_ptr;

	if (!XIODevice_isOpen(io)) {
		d->error = XSerialPort_NotOpenError;
		XSerialPort_errorOccurred_signal(port, d->error);
		return false;
	}

	if (XIODevice_bytesAvailable_base(io) > 0)
		return true;

	return platform_waitForReadyRead(d, msecs);
}

static bool VXSerialPort_waitForBytesWritten(XIODevice* io, int msecs) {
	XSerialPort* port = (XSerialPort*)io;
	XSerialPortPrivate* d = port->d_ptr;

	if (!XIODevice_isOpen(io)) {
		d->error = XSerialPort_NotOpenError;
		XSerialPort_errorOccurred_signal(port, d->error);
		return false;
	}

	if (XIODevice_bytesToWrite_base(io) == 0)
		return true;

	return platform_waitForBytesWritten(d, msecs);
}

static int64_t VXSerialPort_readData(XIODevice* io, char* data, int64_t maxSize) {
	XSerialPort* port = (XSerialPort*)io;
	XSerialPortPrivate* d = port->d_ptr;
	if (maxSize <= 0) return 0;
	int64_t result = platform_read(d, data, maxSize);
	if (result < 0) {
		d->error = XSerialPort_ReadError;
		XSerialPort_errorOccurred_signal(port, d->error);
	}
	return result;
}

static int64_t VXSerialPort_writeData(XIODevice* io, const char* data, int64_t len) {
	XSerialPort* port = (XSerialPort*)io;
	XSerialPortPrivate* d = port->d_ptr;
	if (len <= 0) return 0;
	int64_t result = platform_write(d, data, len);
	if (result < 0) {
		d->error = XSerialPort_WriteError;
		XSerialPort_errorOccurred_signal(port, d->error);
	}
	//else if (result > 0) 
	//{
	//	XIODevice_bytesWritten_signal(io, result);  // ✅ 必须有这一行
	//}
	return result;
}

static void VXSerialPort_deinit(XObject* obj) {
	XSerialPort* port = (XSerialPort*)obj;
	if (port->d_ptr) {
		if (platform_isOpen(port->d_ptr)) {
			platform_close(port->d_ptr);
		}
		XSerialPortPrivate_delete(port->d_ptr);
		port->d_ptr = NULL;
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
	XVTABLE_INHERIT_DEFAULT(XIODevice_class_init());
	// 重载
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Open, VXSerialPort_open);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close, VXSerialPort_close);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_IsSequential, VXSerialPort_isSequential);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesAvailable, VXSerialPort_bytesAvailable);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_BytesToWrite, VXSerialPort_bytesToWrite);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_CanReadLine, VXSerialPort_canReadLine);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForReadyRead, VXSerialPort_waitForReadyRead);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForBytesWritten, VXSerialPort_waitForBytesWritten);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData, VXSerialPort_readData);
	XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData, VXSerialPort_writeData);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSerialPort_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXSerialPort_poll);

#if SHOWCONTAINERSIZE
	printf("XSerialPort size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void XSerialPort_init(XSerialPort* serial)
{
	if (serial == NULL)
		return ;
	//memset(serial,0,sizeof(XSerialPortBase));
	memset(((XIODevice*)serial)+1, 0, sizeof(XSerialPort)-sizeof(XIODevice));
	XIODevice_init(serial);
	XClassGetVtable(serial) = XSerialPort_class_init();
	serial->d_ptr = XSerialPortPrivate_create();
	
}
XSerialPort* XSerialPort_create() 
{
	XSerialPort* port = (XSerialPort*)XMemory_malloc(sizeof(XSerialPort));
	if (port) XSerialPort_init(port);
	return port;
}


// ========== Public API 实现 ==========
void XSerialPort_setPortName(XSerialPort* port, const char* name) {
	if (!port || !name) return;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->portName) 
		XMemory_free(d->portName);
	int len = strlen(name);
	d->portName = XMemory_malloc(len+1);
	memcpy(d->portName,name, len+1);
}

const char* XSerialPort_portName(const XSerialPort* port) {
	return port && port->d_ptr ? (port->d_ptr->portName ? port->d_ptr->portName : "") : "";
}

bool XSerialPort_setBaudRate(XSerialPort* port, int32_t baudRate, XSerialPort_Direction directions) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->baudRate == baudRate) return true; // Avoid redundant signal
	d->baudRate = baudRate;
	bool ok = true;
	if (platform_isOpen(d)) {
		ok = platform_applyConfig(d);
	}
	if (ok) {
		XSerialPort_baudRateChanged_signal(port, (uint32_t)baudRate, directions);
	}
	return ok;
}

int32_t XSerialPort_baudRate(const XSerialPort* port, XSerialPort_Direction directions) {
	(void)directions;
	return port ? port->d_ptr->baudRate : 0;
}

bool XSerialPort_setDataBits(XSerialPort* port, XSerialPort_DataBits dataBits) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->dataBits == dataBits) return true;
	d->dataBits = dataBits;
	bool ok = true;
	if (platform_isOpen(d)) {
		ok = platform_applyConfig(d);
	}
	if (ok) {
		XSerialPort_dataBitsChanged_signal(port, dataBits);
	}
	return ok;
}

XSerialPort_DataBits XSerialPort_dataBits(const XSerialPort* port) {
	return port ? port->d_ptr->dataBits : XSerialPort_Data8;
}

bool XSerialPort_setParity(XSerialPort* port, XSerialPort_Parity parity) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->parity == parity) return true;
	d->parity = parity;
	bool ok = true;
	if (platform_isOpen(d)) {
		ok = platform_applyConfig(d);
	}
	if (ok) {
		XSerialPort_parityChanged_signal(port, parity);
	}
	return ok;
}

XSerialPort_Parity XSerialPort_parity(const XSerialPort* port) {
	return port ? port->d_ptr->parity : XSerialPort_NoParity;
}

bool XSerialPort_setStopBits(XSerialPort* port, XSerialPort_StopBits stopBits) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->stopBits == stopBits) return true;
	d->stopBits = stopBits;
	bool ok = true;
	if (platform_isOpen(d)) {
		ok = platform_applyConfig(d);
	}
	if (ok) {
		XSerialPort_stopBitsChanged_signal(port, stopBits);
	}
	return ok;
}

XSerialPort_StopBits XSerialPort_stopBits(const XSerialPort* port) {
	return port ? port->d_ptr->stopBits : XSerialPort_OneStop;
}

bool XSerialPort_setFlowControl(XSerialPort* port, XSerialPort_FlowControl flowControl) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->flowControl == flowControl) return true;
	d->flowControl = flowControl;
	bool ok = true;
	if (platform_isOpen(d)) {
		ok = platform_applyConfig(d);
	}
	if (ok) {
		XSerialPort_flowControlChanged_signal(port, flowControl);
	}
	return ok;
}

XSerialPort_FlowControl XSerialPort_flowControl(const XSerialPort* port) {
	return port ? port->d_ptr->flowControl : XSerialPort_NoFlowControl;
}

// DTR / RTS / Break 由平台 .c 实现（见下文）

XSerialPort_PinoutSignal XSerialPort_pinoutSignals(const XSerialPort* port) {
	if (!port || !port->d_ptr || !port->d_ptr->isOpen) return XSerialPort_NoSignal;
	return platform_pinoutSignals(port->d_ptr);
}

XSerialPort_Error XSerialPort_error(const XSerialPort* port) {
	return port ? port->d_ptr->error : XSerialPort_NoError;
}

void XSerialPort_clearError(XSerialPort* port) {
	if (port) port->d_ptr->error = XSerialPort_NoError;
}

int64_t XSerialPort_readBufferSize(const XSerialPort* port) {
	return port ? port->d_ptr->readBufferSize : 0;
}

void XSerialPort_setReadBufferSize(XSerialPort* port, int64_t size) {
	if (port && size > 0) {
		port->d_ptr->readBufferSize = size;
		// Note: In real implementation, this would affect the internal buffer size
		// For Windows, this is handled by the OS, so we just store the value
	}
}
bool XSerialPort_setDataTerminalReady(XSerialPort* port, bool set) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->dataTerminalReady == set) return true;

	if (platform_setDataTerminalReady(d, set))
	{
		d->dataTerminalReady = set;
		XSerialPort_dataTerminalReadyChanged_signal(port, set);
		return true;
	}
	return false;
}
bool XSerialPort_isDataTerminalReady(const XSerialPort* port) {
	return port ? port->d_ptr->dataTerminalReady : false;
}
bool XSerialPort_setRequestToSend(XSerialPort* port, bool set) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->requestToSend == set) return true;
	
	if (platform_setRequestToSend(d, set))
	{
		d->requestToSend = set;
		XSerialPort_requestToSendChanged_signal(port, set);
		return true;
	}
	return false;
}
bool XSerialPort_isRequestToSend(const XSerialPort* port) {
	return port ? port->d_ptr->requestToSend : false;
}
bool XSerialPort_setBreakEnabled(XSerialPort* port, bool enable) {
	if (!port) return false;
	XSerialPortPrivate* d = port->d_ptr;
	if (d->breakEnabled == enable) return true;
	if(platform_setBreakEnabled(d, enable))
	{
		d->breakEnabled = enable;
		XSerialPort_breakEnabledChanged_signal(port, enable);
		return true;
	}
	return false;
}
bool XSerialPort_isBreakEnabled(const XSerialPort* port) {
	return port ? port->d_ptr->breakEnabled : false;
}
bool XSerialPort_flush(XSerialPort* port) {
	if (!port || !port->d_ptr || !port->d_ptr->isOpen) {
		if (port && port->d_ptr) port->d_ptr->error = XSerialPort_NotOpenError;
		return false;
	}
	return platform_flush(port->d_ptr);
}

bool XSerialPort_clear(XSerialPort* port, XSerialPort_Direction directions)
{
	if (!port || !port->d_ptr || !port->d_ptr->isOpen) {
		if (port && port->d_ptr) port->d_ptr->error = XSerialPort_NotOpenError;
		return false;
	}
	return platform_clear(port->d_ptr, directions);
}
void* XSerialPort_errorOccurred_signal(XSerialPort* port, XSerialPort_Error error)
{
	XEmitSignal(port, XSerialPort_errorOccurred_signal, XVariant_create_int(error), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_baudRateChanged_signal(XSerialPort* port, uint32_t baudRate, XSerialPort_Direction dir)
{
	XVariant* var= XVariant_create_null();
	XVariantList* list=XVariantList_create();

	XVariant_setValue_uint32(var,baudRate);
	XVariantList_push_back_move_base(list,var);

	XVariant_setValue_int32(var, dir);
	XVariantList_push_back_move_base(list, var);

	XVariant_delete_base(var);
	XEmitSignal(port, XSerialPort_baudRateChanged_signal, list, XVariantList_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_dataBitsChanged_signal(XSerialPort* port, XSerialPort_DataBits bits)
{
	XEmitSignal(port, XSerialPort_dataBitsChanged_signal, XVariant_create_int(bits), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_parityChanged_signal(XSerialPort* port, XSerialPort_Parity parity)
{
	XEmitSignal(port, XSerialPort_parityChanged_signal, XVariant_create_int(parity), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_stopBitsChanged_signal(XSerialPort* port, XSerialPort_StopBits bits)
{
	XEmitSignal(port, XSerialPort_stopBitsChanged_signal, XVariant_create_int(bits), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_flowControlChanged_signal(XSerialPort* port, XSerialPort_FlowControl control)
{
	XEmitSignal(port, XSerialPort_flowControlChanged_signal, XVariant_create_int(control), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_dataTerminalReadyChanged_signal(XSerialPort* port, bool set)
{
	XEmitSignal(port, XSerialPort_dataTerminalReadyChanged_signal, XVariant_create_bool(set), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_requestToSendChanged_signal(XSerialPort* port, bool set)
{
	XEmitSignal(port, XSerialPort_requestToSendChanged_signal, XVariant_create_bool(set), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSerialPort_breakEnabledChanged_signal(XSerialPort* port, bool enabled)
{
	XEmitSignal(port, XSerialPort_breakEnabledChanged_signal, XVariant_create_bool(enabled), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
}