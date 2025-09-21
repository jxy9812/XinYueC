#include"XDataStructTest.h"
#if DEMOTEST
#include"XSet.h"
#include"XBalancedBinaryTree.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//static void XSetTest();
static void XFor_each_pair(void* LPVal, void* args)
{
	int val = *((int*)LPVal);
	XPrintf("key:%d\n", val);
}
void XSetTest()
{
#if XHashMap_ON
	XPrintf("XSet 测试\n");
	//while (true)
	{
		int arrayint[] = { 1,23,456,5,23 };
		XSet* set = XSet_Create(int, XCompare_int);

		for (size_t i = 0; i < 5; i++)
		{
			XSet_insert_base(set, arrayint + i);
		}
		for (int i = 0; i < 5; i++)
		{
			XSet_insert_base(set, &i);
		}
		XPrintf("当前Set容器内数据数量:%d\n", XSet_size_base(set));

		XSet_iterator_for_each(set, XFor_each_pair, NULL);

		XSet_remove_base(set, arrayint + 2);
		XPrintf("当前Set容器内数据数量:%d\n", XSet_size_base(set));
		XSet_iterator_for_each(set, XFor_each_pair, NULL);

		if (XSet_contains(set, arrayint + 1))
			XPrintf("查询到:key:%d \n", arrayint[1]);
		XSet_clear_base(set);
		XSet_delete_base(set);
	}
	XCoreApplication_quit();
#endif
}
void XMenu_XSetTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XSet(有序集合)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XSetTest);
	}
}
#endif