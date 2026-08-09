#include"XProtocolTest.h"
#include"XMenu.h"
#include"XAction.h"
void XMenu_XProtocolTest(XMenu* root)
{
	XMenu* menu = XMenu_create("协议栈");
	XMenu_addMenu(root, menu);
	XMenu_XModbusTest(menu);
	XMenu_XMqttTest(menu);
	XMenu_XCanTest(menu);
}
