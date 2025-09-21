#include"XDataStructTest.h"
#if DEMOTEST
#include"XFind.h"
#include"XVector.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
void XBinarySearchTest()
{
#if(XVector_ON)
	//数组
	XVector* VArray = XVector_Create(size_t);
	int count = 10000000;//测试数据量
	for (size_t i = 0; i < count; i++)
	{
		XVector_push_back_base(VArray, &i);
	}
	int findVal = 9999999;
	int* ret=XBinarySearch(XContainerDataPtr(VArray), count,sizeof(size_t),XCompare_int,&findVal);
	XPrintf("二分查找到值:%d", *ret);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}
void XMenu_XBinarySearchTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XBinarySearch(二分查找)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XBinarySearchTest);
	}
}
#endif