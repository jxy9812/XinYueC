#include"XIOTest.h"
#include"XNetworkTest.h"
#include"XTestMenu.h"
void XTestMenu_XIOTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("IO");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XNetworkTest(menu);
	XTestMenu_XSerialPortTest(menu);
	XTestMenu_XSocketTest(menu);
	XTestMenu_XSslTest(menu);
	XTestMenu_XTcpServerTest(menu);
	XTestMenu_XUdpSocketTest(menu);
	XTestMenu_XTcpSocketTest(menu);
	XTestMenu_XHostInfoTest(menu);
	XTestMenu_XDirTest(menu);
	XTestMenu_XFileTest(menu);
	XTestMenu_XFileInfoTest(menu);
	XTestMenu_XSaveFileTest(menu);
	XTestMenu_XFileDescriptorTest(menu);
}
