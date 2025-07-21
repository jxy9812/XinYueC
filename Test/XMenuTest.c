#include "XDataStructTest.h"
#include "XMenuTest.h"
#include "XMenu.h"


static void XMenu_XTreeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("树结构测试");
	XMenu_addMenu(root, menu);
}
static void XMenu_XContainerTest(XMenu* root)
{
	XMenu* menu = XMenu_create("容器测试");
	XMenu_addMenu(root, menu);
}
static void XMenu_XIOTest(XMenu* root)
{
	XMenu* menu = XMenu_create("IO设备测试");
	XMenu_addMenu(root, menu);
}
static void XMenu_XProtocolStackTest(XMenu* root)
{
	XMenu* menu = XMenu_create("协议栈测试");
	XMenu_addMenu(root, menu);
}
static void XMenu_XTimerTest(XMenu* root)
{
	XMenu* menu = XMenu_create("定时器测试");
	XMenu_addMenu(root, menu);
}
static void XMenu_XAlgorithmTest(XMenu* root)
{
	XMenu* menu = XMenu_create("算法测试测试");
	XMenu_addMenu(root, menu);
	XMenu_XTreeTest(menu);
}
XMenu* XMenuTest_create()
{
	XMenu* root = XMenu_create("测试代码");
	XMenu_XAlgorithmTest(root);
	XMenu_XContainerTest(root);
	XMenu_XIOTest(root);
	XMenu_XProtocolStackTest(root);
	XMenu_XTimerTest(root);
	return root;
}

void XMenuTest_show(XMenu* menu, int column)
{

}

void XMenuTest_run()
{
	XMenu* menu = XMenuTest_create();
	XMenuTest_show(menu,2);
}
