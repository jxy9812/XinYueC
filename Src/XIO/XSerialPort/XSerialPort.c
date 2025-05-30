#include "XSerialPort.h"
#include "XMemory.h"
#include <string.h>
XSerialPortBase* XSerialPortBase_new(XVtable* vtable)
{
	XSerialPortBase* serial = XMemory_malloc(sizeof(XSerialPortBase));
	if (serial == NULL)
		return serial;
	XSerialPortBase_init(serial, vtable);
	return serial;
}

void XSerialPortBase_init(XSerialPortBase* serial, XVtable* vtable)
{
	if (serial == NULL)
		return ;
	//memset(serial,0,sizeof(XSerialPortBase));
	memset(((XIODeviceBase*)serial)+1, 0, sizeof(XSerialPortBase)-sizeof(XIODeviceBase));
	XIODeviceBase_init(serial, vtable);
	XSerialPortBase_class_init();
	if (vtable == NULL)
		XClassGetVtable(serial) = XSerialPortVtable;
	else
		XClassGetVtable(serial) = vtable;
	
}

bool XSerialPortBase_open_base(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity)
{
	if (ISNULL(serial, "") || ISNULL(XClassGetVtable(serial), ""))
		return false;
	return XClassGetVirtualFunc(serial, EXIODeviceBase_Open, bool(*)(XSerialPortBase*, XIODeviceBaseMode, uint8_t, uint32_t, XSerialPortBaseParity))(serial, mode,portNum,baudRate,parity);
}
