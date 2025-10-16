#include "XDeviceTest.h"
#include"XMenu.h"
void XMenu_XDeviceTest(XMenu* root)
{
	XMenu* menu = XMenu_create("设备");
	XMenu_addMenu(root, menu);
	XMenu_XESP8266WifiTest(menu);
}

