#include"XDataStructTest.h"
#if DEMOTEST
#include"XHashMap.h"
#include"XCompare.h"
#include"XBalancedBinaryTree.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//static void XHashMapTest();
static void XFor_each_pair(void* LPVal, void* args)
{
	XPair* pair = (XPair*)LPVal;
	XPrintf("key:%d val:%s\n", XPair_First(pair,int), XPair_Second(pair,char*));
}
void XHashMapTest()
{
#if XHashMap_ON
	XPrintf("XHashMap 测试\n");
	//while (true)
	{
		int arrayint[] = { 1,23,456,5,23 };
		char arraychar[][100] = { "琦神","星小白","章鱼哥","你好啊111hjhj1","玩蛇" };
		XHashMap* map = XHashMap_Create(int, char*, XCompare_int);

		for (size_t i = 0; i < 5; i++)
		{
			size_t p = &arraychar[i];
			XHashMap_insert_base(map, arrayint + i, &p);
		}
		for (int i = 0; i < 5; i++)
		{
			size_t p = &arraychar[i%5];
			XHashMap_insert_base(map, &i, &p);
		}
		XPrintf("当前XHashMap容器内数据数量:%d\n", XHashMap_size_base(map));

		XHashMap_iterator_for_each(map, XFor_each_pair, NULL);

		XHashMap_remove_base(map, arrayint + 2);
		XPrintf("当前XHashMap容器内数据数量:%d\n", XHashMap_size_base(map));
		XHashMap_iterator_for_each(map, XFor_each_pair, NULL);

		/*XPair* pair = XHashMap_find_base(map, arrayint + 1,NULL);
		XPrintf("查询到:key:%d val:%s\n", XPair_First(pair, int), XPair_Second(pair, char*));*/
		XHashMap_clear_base(map);
		XHashMap_delete_base(map);
	}
#endif
	XCoreApplication_quit();
}
void XMenu_XHashMapTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XHashMap(无序映射)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XHashMapTest);
	}
}
#endif