#include"XDataTest.h"
#include"XCharTest.h"
#include"XMenu.h"
void XMenu_XDataTest(XMenu* root)
{
	XMenu* menu = XMenu_create("数据");
	XMenu_addMenu(root, menu);
	XMenu_XCharTest(menu);
}