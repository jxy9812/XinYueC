#include"XCodeTest.h"
#include"XMenu.h"
void XMenu_XCodeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("代码");
	XMenu_addMenu(root, menu);
	XMenu_XDebugTest(menu);
}