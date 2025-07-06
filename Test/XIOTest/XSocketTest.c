#include"XIOTest.h"
#include"XSocket.h"
#include"XMemory.h"
#ifdef WIN32
#include <windows.h>
void XSocketTest()
{
	XSocket* socket = XSocket_create();
	XSocket_connectToHost_base(socket,"192.168.1.117",500,XIODeviceBase_ReadWrite);
	//XSocketBase_waitForConnected_base(socket, 3000);
}
#else
void XSerialPortTest()
{

}
#endif