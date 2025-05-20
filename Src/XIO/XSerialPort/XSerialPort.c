#include "XSerialPort.h"
#include <string.h>
XSerialPortDevice* XSerialPort_new(XSerialPortDevice_PortFuncInit* port)
{
	if (port == NULL)
		return NULL;

	XSerialPortDevice* serial = XMemory_malloc(sizeof(XSerialPortDevice));
	if (serial == NULL)
		return serial;
	XSerialPort_init(serial, port);
	return serial;
}

void XSerialPort_init(XSerialPortDevice* serial, XSerialPortDevice_PortFuncInit* port)
{
	if (serial == NULL || port == NULL)
		return ;
	memset(serial, 0, sizeof(XSerialPortDevice));
	XIODevice_init(serial, port);

}

bool XSerialPort_open(XSerialPortDevice* serial, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity)
{
	if(serial ==NULL)
		return false;
	//XSerialPortDevice* serial = (XSerialPortDevice*)io->device;
	serial->m_baudRate = baudRate;
	serial->m_parity= parity;
	serial->m_portNum = portNum;
	return XIODevice_open(serial, mode);
}
