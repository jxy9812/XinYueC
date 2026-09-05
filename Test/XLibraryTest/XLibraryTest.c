#include"XLibraryTest.h"
#include"XTestMenu.h"
void XTestMenu_XLibraryTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("库");
	XTestMenu_addMenu(root, menu);
	XTestMenu_FindTest(menu);
	XTestMenu_XTreeTest(menu);
	XTestMenu_XBase64Test(menu);
	XTestMenu_zlibTest(menu);
}

void XTestMenu_XTreeTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("树结构");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XBinaryTreeTest(menu);
	XTestMenu_XBalancedBinaryTreeTest(menu);
	XTestMenu_XRedBlackTreeTest(menu);
}

void XTestMenu_FindTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("查询算法");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XBinarySearchTest(menu);
}
