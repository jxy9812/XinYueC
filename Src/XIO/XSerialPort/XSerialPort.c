#include "XSerialPort.h"
#include "XMemory.h"
#include <string.h>
XSerialPort* XSerialPort_new(XSerialPort_PortFuncInit* port)
{
	if (port == NULL)
		return NULL;

	XSerialPort* serial = XMemory_malloc(sizeof(XSerialPort));
	if (serial == NULL)
		return serial;
	XSerialPort_init(serial, port);
	return serial;
}

void XSerialPort_init(XSerialPort* serial, XSerialPort_PortFuncInit* port)
{
	if (serial == NULL || port == NULL)
		return ;
	memset(serial,0,sizeof(XSerialPort));
	//memset(((XIODevice*)serial)+1, 0, sizeof(XSerialPort)-sizeof(XIODevice));
	XIODevice_init(serial, port);
	XSerialPort_class_init();
	XClassGetVtable(serial) = XSerialPortVtable;
}

bool XSerialPort_open_base(XSerialPort* serial, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity)
{
	if (ISNULL(serial, "") || ISNULL(XClassGetVtable(serial), ""))
		return false;
	return XClassGetVirtualFunc(serial, EXIODevice_Open, bool(*)(XSerialPort*, XIODeviceBase, uint8_t, uint32_t, XSerialPortParity))(serial, mode,portNum,baudRate,parity);
}
