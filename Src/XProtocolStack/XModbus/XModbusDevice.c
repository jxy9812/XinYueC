#include"XModbusDevice.h"
#include"XVariant.h"
#include"XString.h"
#include"XIODeviceBase.h"
#include"XSerialPort.h"
#include<string.h>
// 私有数据结构
struct XModbusDevicePrivate {
	XVariant* params[8]; // 对应 ConnectionParameter 枚举
};
// 初始化私有数据
static void XModbusDevicePrivate_init(XModbusDevicePrivate* d) {
	if (!d) return;
	memset(d->params, 0, sizeof(d->params));
}
// 销毁私有数据
static void XModbusDevicePrivate_destroy(XModbusDevicePrivate* d) {
	if (!d) return;
	for (int i = 0; i < 8; ++i) {
		if (d->params[i]) {
			XVariant_delete_base(d->params[i]);
			d->params[i] = NULL;
		}
	}
	XMemory_free(d);
}
// 错误字符串映射
static const char* errorToString(XModbusDevice_Error err) {
	switch (err) {
	case XModbusDevice_NoError: return "No error";
	case XModbusDevice_ReadError: return "Read error";
	case XModbusDevice_WriteError: return "Write error";
	case XModbusDevice_ConnectionError: return "Connection error";
	case XModbusDevice_ConfigurationError: return "Configuration error";
	case XModbusDevice_TimeoutError: return "Timeout error";
	case XModbusDevice_ProtocolError: return "Protocol error";
	case XModbusDevice_ReplyAbortedError: return "Reply aborted";
	case XModbusDevice_UnknownError: return "Unknown error";
	case XModbusDevice_InvalidResponseError: return "Invalid response";
	default: return "Undefined error";
	}
}
static bool VXIODevice_open(XIODeviceBase* io, XIODeviceBaseMode mode);
static bool VXIODevice_close(XIODeviceBase* io);
static void VXModbusDevice_move(XModbusDevice* dev, XVariant* src);
static void VXModbusDevice_copy(XModbusDevice* dev, const XVariant* src);
static void VXModbusDevice_deinit(XModbusDevice* dev);

XVtable* XModbusDevice_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XObject))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//	void* table[] = { VXClass_copy,VXClass_move,VXClass_deinit };
		//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
		//重载
		XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXModbusDevice_move);
		XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXModbusDevice_copy);
		XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDevice_deinit);

		XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXIODevice_open);
		XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);
#if SHOWCONTAINERSIZE
	printf("XModbusDevice size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XModbusDevice* XModbusDevice_create()
{
	XModbusDevice* dev = XMemory_malloc(sizeof(XModbusDevice));
	XModbusDevice_init(dev);
	return dev;
}

void XModbusDevice_init(XModbusDevice* dev)
{
	XObject_init(dev);
	XClassGetVtable(dev) = XModbusDevice_class_init();
	memset(((XObject*)dev) + 1, 0, sizeof(XModbusDevice) - sizeof(XClass));

	dev->m_state = XModbusDevice_UnconnectedState;
	dev->m_error = XModbusDevice_NoError;
	dev->error_string = NULL;
	dev->m_io_device = NULL;
	dev->m_d = (XModbusDevicePrivate*)XMemory_malloc(sizeof(XModbusDevicePrivate));
	XModbusDevicePrivate_init(dev->m_d);
}

void VXModbusDevice_move(XModbusDevice* dev, XVariant* src)
{
}

void VXModbusDevice_copy(XModbusDevice* dev, const XVariant* src)
{
}

void VXModbusDevice_deinit(XModbusDevice* dev)
{
	if (!dev) return;
	if (dev->error_string) {
		XString_delete_base(dev->error_string);
		dev->error_string = NULL;
	}
	if (dev->m_io_device) {
		XIODeviceBase_delete_base(dev->m_io_device);
		dev->m_io_device = NULL;
	}
	if (dev->m_d) {
		XModbusDevicePrivate_destroy(dev->m_d);
		dev->m_d = NULL;
	}
}

XVariant* XModbusDevice_connectionParameter(const XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter)
{
	if (!dev || !dev->m_d || parameter < 0 || parameter >= 8) return NULL;
	if (!dev->m_d->params[parameter]) return NULL;
	return XVariant_create_copy(dev->m_d->params[parameter]);
}

void XModbusDevice_setConnectionParameter(XModbusDevice* dev, XModbusDevice_ConnectionParameter parameter, XVariant* value)
{
	if (!dev || !dev->m_d || !value || parameter < 0 || parameter >= 8) return;
	if (dev->m_d->params[parameter]) {
		XVariant_delete_base(dev->m_d->params[parameter]);
	}
	dev->m_d->params[parameter] = XVariant_create_copy(value);
}
// 内部：创建 IO 设备
static bool createIODevice(XModbusDevice* dev) {
	if (!dev || !dev->m_d) return false;

	// 释放旧设备
	if (dev->m_io_device) {
		XIODeviceBase_delete_base(dev->m_io_device);
		dev->m_io_device = NULL;
	}

	// 串口模式？
	if (dev->m_d->params[XModbusDevice_SerialPortNameParameter]) {
		XSerialPort* serial = XSerialPort_create();
		if (!serial) {
			dev->m_error = XModbusDevice_ConfigurationError;
			return false;
		}

		XVariant* val;
		if ((val = dev->m_d->params[XModbusDevice_SerialPortNameParameter])) {
			XString* name = XVariant_toString(val);
			XSerialPort_setPortName(serial, name);
			XString_delete_base(name);
		}
		if ((val = dev->m_d->params[XModbusDevice_SerialBaudRateParameter])) {
			XSerialPort_setBaudRate(serial, (int)XVariant_toInt(val), XSerialPort_AllDirections);
		}
		if ((val = dev->m_d->params[XModbusDevice_SerialDataBitsParameter])) {
			XSerialPort_setDataBits(serial, (int)XVariant_toInt(val));
		}
		if ((val = dev->m_d->params[XModbusDevice_SerialStopBitsParameter])) {
			XSerialPort_setStopBits(serial, (int)XVariant_toInt(val));
		}
		if ((val = dev->m_d->params[XModbusDevice_SerialParityParameter])) {
			XSerialPort_setParity(serial, (int)XVariant_toInt(val));
		}
		dev->m_io_device = (XIODeviceBase*)serial;
		return true;
	}
	// 网络模式？
	else if (dev->m_d->params[XModbusDevice_NetworkAddressParameter]) 
	{
		/*XSocket* socket = XSocket_create();
		if (!socket) {
			dev->m_error = XModbusDevice_ConfigurationError;
			return false;
		}

		int port = 502;
		if (dev->m_d->params[XModbusDevice_NetworkPortParameter]) {
			port = (int)XVariant_toInt(dev->m_d->params[XModbusDevice_NetworkPortParameter]);
		}
		XString* addr = XVariant_toString(dev->m_d->params[XModbusDevice_NetworkAddressParameter]);
		XSocket_setHost(socket, XString_cstr(addr));
		XSocket_setPort(socket, port);
		XString_destroy(addr);
		dev->m_io_device = (XIODeviceBase*)socket;*/
		return true;
	}
	else {
		dev->m_error = XModbusDevice_ConfigurationError;
		return false;
	}
}
bool XModbusDevice_connectDevice(XModbusDevice* dev)
{
	if (!dev || !dev->m_io_device)
		return false;
	if (dev->m_state == XModbusDevice_ConnectedState)
		return true;

	return XIODeviceBase_open_base(dev->m_io_device,XIODeviceBase_ReadWrite);
}
bool VXIODevice_open(XIODeviceBase* io, XIODeviceBaseMode mode)
{
	XModbusDevice* dev = io;
	if (!dev) return false;
	if (dev->m_state == XModbusDevice_ConnectedState) return true;

	dev->m_state = XModbusDevice_ConnectingState;
	XModbusDevice_stateChanged_signal(dev, dev->m_state);

	if (!createIODevice(dev)) {
		dev->m_error = XModbusDevice_ConfigurationError;
		dev->error_string = XString_create_fmt_utf8(errorToString(dev->m_error));
		dev->m_state = XModbusDevice_UnconnectedState;
		XModbusDevice_errorOccurred_signal(dev, dev->m_error);
		XModbusDevice_stateChanged_signal(dev, dev->m_state);
		return false;
	}

	if (!XIODeviceBase_open_base(dev->m_io_device, XIODeviceBase_ReadWrite)) {
		dev->m_error = XModbusDevice_ConnectionError;
		dev->error_string = XString_create_fmt_utf8(errorToString(dev->m_error));
		dev->m_state = XModbusDevice_UnconnectedState;
		XModbusDevice_errorOccurred_signal(dev, dev->m_error);
		XModbusDevice_stateChanged_signal(dev, dev->m_state);
		return false;
	}

	dev->m_error = XModbusDevice_NoError;
	if (dev->error_string) {
		XString_delete_base(dev->error_string);
		dev->error_string = NULL;
	}
	dev->m_state = XModbusDevice_ConnectedState;
	XModbusDevice_stateChanged_signal(dev, dev->m_state);
	io->m_mode = mode;
	return true;

}
void XModbusDevice_disconnectDevice(XModbusDevice* dev)
{
	if(dev&&dev->m_io_device)
		XIODeviceBase_close_base(dev->m_io_device);
}
bool VXIODevice_close(XIODeviceBase* io)
{
	XModbusDevice* dev = io;
	if (!dev) return;
	if (dev->m_state == XModbusDevice_UnconnectedState) return;

	dev->m_state = XModbusDevice_ClosingState;
	XModbusDevice_stateChanged_signal(dev, dev->m_state);

	if (dev->m_io_device) {
		XIODeviceBase_close_base(dev->m_io_device);
		XIODeviceBase_delete_base(dev->m_io_device);
		dev->m_io_device = NULL;
	}

	dev->m_state = XModbusDevice_UnconnectedState;
	XModbusDevice_stateChanged_signal(dev, dev->m_state);
	io->m_mode = XIODeviceBase_NotOpen;
}
XModbusDevice_State XModbusDevice_state(const XModbusDevice* dev)
{
	return dev ? dev->m_state : XModbusDevice_UnconnectedState;
}

XModbusDevice_Error XModbusDevice_error(const XModbusDevice* dev)
{
	return dev ? dev->m_error : XModbusDevice_UnknownError;
}

XString* XModbusDevice_errorString(const XModbusDevice* dev)
{
	if (!dev) return NULL;
	if (dev->error_string) {
		return XString_create_copy(dev->error_string);
	}
	return XString_create_fmt_utf8(errorToString(dev->m_error));
}

XIODeviceBase* device(const XModbusDevice* dev)
{
	return dev ? dev->m_io_device : NULL;
}

void* XModbusDevice_errorOccurred_signal(XModbusDevice* dev,XModbusDevice_Error error)
{
	XEmitSignal(dev, XModbusDevice_errorOccurred_signal, XVariant_create_int(error), XVariant_delete_base, NULL, XEVENT_PRIORITY_LOWEST);
}

void* XModbusDevice_stateChanged_signal(XModbusDevice* dev,XModbusDevice_State state)
{
	XEmitSignal(dev, XModbusDevice_stateChanged_signal, XVariant_create_int(state), XVariant_delete_base, NULL, XEVENT_PRIORITY_LOWEST);
}
