#include"XDataStructTest.h"
#if DEMOTEST
#include"XHashSet.h"
#include"XEquality.h"
#include"XLess.h"
#include"XBalancedBinaryTree.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
//static void XHashSetTest();
static void XFor_each_pair(void* pvVal, void* args)
{
	int val = *((int*)pvVal);
	printf("key:%d\n", val);
}
void XHashSetTest()
{
#if XHashMap_ON
	printf("XHashSet 测试\n");
	//while (true)
	{
		int arrayint[] = { 1,23,456,5,23 };
		XHashSet* set = XHashSet_Create(int, XEquality_int,XLess_int);

		for (size_t i = 0; i < 5; i++)
		{
			XHashSet_insert_base(set, arrayint + i);
		}
		for (int i = 0; i < 5; i++)
		{
			XHashSet_insert_base(set, &i);
		}
		printf("当前XHashSet容器内数据数量:%d\n", XHashSet_size_base(set));

		XHashSet_iterator_for_each(set, XFor_each_pair, NULL);

		XHashSet_remove_base(set, arrayint + 2);
		printf("当前XHashSet容器内数据数量:%d\n", XHashSet_size_base(set));
		XHashSet_iterator_for_each(set, XFor_each_pair, NULL);

		if(XHashSet_find_base(set, arrayint + 1))
			printf("查询到:key:%d \n", arrayint[1]);
		XHashSet_clear_base(set);
		XHashSet_delete_base(set);
	}
#endif
	XCoreApplication_requestQuit();
}
void XMenu_XHashSetTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XHashSet(无序集合)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XHashSetTest);
	}
}
#endif