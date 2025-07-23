#include"XTimerTest.h"
#include"XMenu.h"
#include"XAction.h"
void XMenu_XTimerTest(XMenu* root)
{
	XMenu* menu = XMenu_create("定时器");
	XMenu_addMenu(root, menu);
	XMenu_XTimerWheelTest(menu);
}