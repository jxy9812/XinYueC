#include"XIOTest.h"
#include"XSocket.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
static void XSocketTest();

void XSocketTest()
{
	XSocket* socket = XSocket_create();
	XSocket_connectToHost_base(socket, "192.168.1.117", 500, XIODeviceBase_ReadWrite);
	//XSocketBase_waitForConnected_base(socket, 3000);
}
void XMenu_XSocketTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XSocket(网络客户端)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XSocketTest);
	}
}