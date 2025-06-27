#include"XIOTest.h"
#include"XSocketBase.h"
#include"XMemory.h"
#ifdef WIN32
#include <windows.h>
void XSocketTest()
{
	XSocketBase* socket = XSocketWin32_create();
	XSocketBase_connectToHost_base(socket,"192.168.1.117",500,XIODeviceBase_ReadWrite);
	//XSocketBase_waitForConnected_base(socket, 3000);
	while (true)
	{
		XSocketBase_poll_base(socket);
	}
}
#else
void XSerialPortTest()
{

}
#endif