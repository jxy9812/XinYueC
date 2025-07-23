#include"XIOTest.h"
#include"XMenu.h"
void XMenu_XIOTest(XMenu* root)
{
	XMenu* menu = XMenu_create("IO设备");
	XMenu_addMenu(root, menu);
	XMenu_XSerialPortTest(menu);
	XMenu_XSocketTest(menu);
}