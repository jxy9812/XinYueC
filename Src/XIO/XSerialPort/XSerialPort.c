#include "XSerialPort.h"
#include "XMemory.h"
#include <string.h>


void XSerialPortBase_init(XSerialPortBase* serial, XVtable* vtable)
{
	if (serial == NULL)
		return ;
	//memset(serial,0,sizeof(XSerialPortBase));
	memset(((XIODeviceBase*)serial)+1, 0, sizeof(XSerialPortBase)-sizeof(XIODeviceBase));
	XIODeviceBase_init(serial, vtable);
	XClassGetVtable(serial) = vtable;
	serial->m_baudRate = 9600;
	serial->m_dataBits = SP_DB_Eight;
	serial->m_stopBits = SP_ST_One;
	serial->m_parity = SP_PAR_NONE;
	serial->m_flowControl = SP_FC_None;
}

bool XSerialPort_open_base(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity)
{
	if (ISNULL(serial, "") || ISNULL(XClassGetVtable(serial), ""))
		return false;
	serial->m_baudRate = baudRate;
	serial->m_parity = parity;
	serial->m_portNum = portNum;
	return XClassGetVirtualFunc(serial, EXIODeviceBase_Open, bool(*)(XSerialPortBase*, XIODeviceBaseMode))(serial, mode);
}
