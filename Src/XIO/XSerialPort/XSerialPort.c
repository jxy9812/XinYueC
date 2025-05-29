#include "XSerialPort.h"
#include "XMemory.h"
#include <string.h>
XSerialPort* XSerialPort_new(XVtable* vtable)
{
	XSerialPort* serial = XMemory_malloc(sizeof(XSerialPort));
	if (serial == NULL)
		return serial;
	XSerialPort_init(serial, vtable);
	return serial;
}

void XSerialPort_init(XSerialPort* serial, XVtable* vtable)
{
	if (serial == NULL)
		return ;
	//memset(serial,0,sizeof(XSerialPort));
	memset(((XIODeviceBase*)serial)+1, 0, sizeof(XSerialPort)-sizeof(XIODeviceBase));
	XIODevice_init(serial, vtable);
	XSerialPort_class_init();
	if (vtable == NULL)
		XClassGetVtable(serial) = XSerialPortVtable;
	else
		XClassGetVtable(serial) = vtable;
	
}

bool XSerialPort_open_base(XSerialPort* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity)
{
	if (ISNULL(serial, "") || ISNULL(XClassGetVtable(serial), ""))
		return false;
	return XClassGetVirtualFunc(serial, EXIODeviceBase_Open, bool(*)(XSerialPort*, XIODeviceBaseMode, uint8_t, uint32_t, XSerialPortParity))(serial, mode,portNum,baudRate,parity);
}
