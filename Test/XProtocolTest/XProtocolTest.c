#include"XProtocolTest.h"
#include"XTestMenu.h"
#include"XAction.h"
void XTestMenu_XProtocolTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("协议栈");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XModbusTest(menu);
	XTestMenu_XMqttTest(menu);
	XTestMenu_XCanTest(menu);
}
