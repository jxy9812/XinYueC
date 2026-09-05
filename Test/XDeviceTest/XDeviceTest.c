#include "XDeviceTest.h"
#include"XTestMenu.h"
void XTestMenu_XDeviceTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("设备");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XESP8266WifiTest(menu);
}

