#include"XIOTest.h"
#include"XNetworkTest.h"
#include"XMenu.h"
void XMenu_XIOTest(XMenu* root)
{
	XMenu* menu = XMenu_create("IO");
	XMenu_addMenu(root, menu);
	XMenu_XNetworkTest(menu);
	XMenu_XSerialPortTest(menu);
	XMenu_XSocketTest(menu);
	XMenu_XTcpServerTest(menu);
	XMenu_XUdpSocketTest(menu);
	XMenu_XTcpSocketTest(menu);
	XMenu_XHostInfoTest(menu);
	XMenu_XDirTest(menu);
	XMenu_XFileTest(menu);
	XMenu_XFileInfoTest(menu);
	XMenu_XSaveFileTest(menu);
}