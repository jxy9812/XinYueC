#include"XDataStructTest.h"
#if DEMOTEST
#include"XMap.h"
#include"XCompare.h"
#include"XBalancedBinaryTree.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//static void XMapTest();
static void XFor_each_pair(void* LPVal, void* args)
{
	XPair* pair = (XPair*)LPVal;
	XPrintf("key:%d val:%s\n", XPair_First(pair,int), XPair_Second(pair,char*));
}
void XMapTest()
{
#if XMap_ON
	//while (1)
	{
		XPrintf("XMap 测试\n");
		int arrayint[] = { 1,23,456,5,23 };
		char arraychar[][100] = { "琦神","星小白","章鱼哥","123dfsadsadsad","玩蛇" };
		XMap* map = XMap_Create(int, char*, XCompare_int);

		for (size_t i = 0; i < 5; i++)
		{
			XMap_Insert_Base(map, int, arrayint[i], char*, arraychar[i]);
			//char* str = &arraychar[i];
			//XMap_insert_base(map, &arrayint[i], &str);
		}
		XPrintf("当前Map容器内数据数量:%d\n", XMap_size_base(map));
		XMap_iterator_for_each(map, XFor_each_pair, NULL);

		XMap_remove_base(map, arrayint + 2);
		XMap_Remove_Base(map, int, arrayint[2]);
		XPrintf("当前Map容器内数据数量:%d\n", XMap_size_base(map));
		XMap_reverse_iterator_for_each(map, XFor_each_pair, NULL);

		/*XPair* pair = XMap_find_base(map, arrayint);
		XPrintf("查询到:key:%d val:%s\n", XPair_First(pair, int), XPair_Second(pair, char*));*/
		XMap_clear_base(map);
		XMap_delete_base(map);
	}
#endif
	XCoreApplication_quit();
}
void XMenu_XMapTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XMap(有序映射)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XMapTest);
	}
}
#endif