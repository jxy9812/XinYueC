#include "XSerialPort.h"

XIODevice* XSerialPort_new(XIODevice_PortFunc* port)
{
	XIODevice* io = XIODevice_new(port);
	if(io)
	{
		io->device = XMemory_malloc(sizeof(XSerialPortDevice));
		if (io->device == NULL)
			XIODevice_free(io);
	}

	return io;
}

bool XSerialPort_open(XIODevice* io, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity)
{
	if(io==NULL|| io->device==NULL)
		return false;
	XSerialPortDevice* serial = (XSerialPortDevice*)io->device;
	serial->m_baudRate = baudRate;
	serial->m_parity= parity;
	serial->m_portNum = portNum;
	return XIODevice_open(io, mode);
}
