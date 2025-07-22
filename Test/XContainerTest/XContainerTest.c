#include"XContainerTest.h"
#include"XMenu.h"
#include"XAction.h"

void XMenu_XContainerTest(XMenu* root)
{
	XMenu* menu = XMenu_create("容器");
	XMenu_addMenu(root, menu);
	XMenu_XListTest(menu);
	XMenu_XVectorTest(menu);
	XMenu_XStackTest(menu);
}

void XMenu_XListTest(XMenu* root)
{
	XMenu* menu = XMenu_create("链表");
	XMenu_addMenu(root, menu);
	XMenu_XListDLinkedTest(menu);
	XMenu_XListSLinkedTest(menu);
}
