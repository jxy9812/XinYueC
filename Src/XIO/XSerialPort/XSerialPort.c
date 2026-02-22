#include "XSerialPort.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XVariantList.h"
#include <string.h>
void XSerialPortBase_init(XSerialPortBase* serial)
{
	if (serial == NULL)
		return ;
	//memset(serial,0,sizeof(XSerialPortBase));
	memset(((XIODeviceBase*)serial)+1, 0, sizeof(XSerialPortBase)-sizeof(XIODeviceBase));
	XIODeviceBase_init(serial);
	//XClassGetVtable(serial) = vtable;
	serial->m_baudRate = 9600;
	serial->m_dataBits = XSerialPort_Data8;
	serial->m_stopBits = XSerialPort_OneStop;
	serial->m_parity = XSerialPort_NoParity;
	serial->m_flowControl = XSerialPort_NoFlowControl;
}

void XSerialPort_setPortName(XSerialPort* port, const XString* name)
{
	if (!port||!name)
		return;
	if(((XSerialPortBase*)port)->m_portName)
		XString_assign(((XSerialPortBase*)port)->m_portName,name);
}

XString* XSerialPort_portName(const XSerialPort* port)
{
	if (!port || !((XSerialPortBase*)port)->m_portName)
		return NULL;
	return XString_create_copy(((XSerialPortBase*)port)->m_portName);
}

XSerialPort_Error XSerialPort_error(const XSerialPort* port)
{
	const XSerialPortBase* base = (const XSerialPortBase*)port;
	if (base == NULL) return XSerialPort_NoError;
	return base->m_error;
}
void XSerialPort_clearError(XSerialPort* port)
{
	XSerialPortBase* base = (XSerialPortBase*)port;
	if (base == NULL) return;
	base->m_error = XSerialPort_NoError;
}

bool XSerialPort_open_base(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPort_Parity parity)
{
	if (ISNULL(serial, "") || ISNULL(XClassGetVtable(serial), ""))
		return false;
	serial->m_baudRate = baudRate;
	serial->m_parity = parity;
	serial->m_portNum = portNum;
	return XClassGetVirtualFunc(serial, EXIODeviceBase_Open, bool(*)(XSerialPortBase*, XIODeviceBaseMode))(serial, mode);
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
