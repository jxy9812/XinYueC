#include"XLibraryTest.h"
#include"XMenu.h"
void XMenu_XLibraryTest(XMenu* root)
{
	XMenu* menu = XMenu_create("库");
	XMenu_addMenu(root, menu);
	XMenu_FindTest(menu);
	XMenu_XTreeTest(menu);
	XMenu_CJsonTest(menu);
	XMenu_XBase64Test(menu);
	XMenu_zlibTest(menu);
}

void XMenu_XTreeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("树结构");
	XMenu_addMenu(root, menu);
	XMenu_XBinaryTreeTest(menu);
	XMenu_XBalancedBinaryTreeTest(menu);
	XMenu_XHuffmanTreeTest(menu);
	XMenu_XRedBlackTreeTest(menu);
}

void XMenu_FindTest(XMenu* root)
{
	XMenu* menu = XMenu_create("查询算法");
	XMenu_addMenu(root, menu);
	XMenu_XBinarySearchTest(menu);
}
