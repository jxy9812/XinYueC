#include"XDataStructTest.h"
#if DEMOTEST
#include"XHashMap.h"
#include"XCompare.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include<string.h>
//static void XHashMapTest();
static void XFor_each_pair(void* LPVal, void* args)
{
	XPair* pair = (XPair*)LPVal;
	XPrintf("key:%d val:%s\n", XPair_First(pair,int), XPair_Second(pair,char*));
}
static void map_dataDelete(char**lpStr)
{
	if (!lpStr)return;
	char* str = *lpStr;
		XFree_System(str);
}
void XHashMapTest()
{
#if XHashMap_ON
	XPrintf("XHashMap 测试\n");
	while (true)
	{
		int arrayint[] = { 1,23,456,5,23 };
		char arraychar[][100] = { "aaaa","bbbbbbbb","78777777","12131313","dsadcxzccxzcxzc" };
		XHashMap* map = XHashMap_Create(int, char*, int_compare);
		XContainerSetDataDeinitMethod(map, map_dataDelete);
		for (size_t i = 0; i < 500; i++)
		{
			char* str = XMalloc_System(strlen(arraychar[i % 5]) + 10);
			strcpy(str, arraychar[i % 5]);
			XHashMap_insert_base(map, &i, &str);
		}
		/*for (int i = 0; i < 5; i++)
		{
			size_t p = &arraychar[i%5];
			XHashMap_insert_base(map, &i, &p);
		}*/
		XPrintf("当前XHashMap容器内数据数量:%d\n", XHashMap_size_base(map));

		XHashMap_iterator_for_each(map, XFor_each_pair, NULL);

		XHashMap_remove_base(map, arrayint + 2);
		XPrintf("当前XHashMap容器内数据数量:%d\n", XHashMap_size_base(map));
		XHashMap_iterator_for_each(map, XFor_each_pair, NULL);

		/*XPair* pair = XHashMap_find_base(map, arrayint + 1,NULL);
		XPrintf("查询到:key:%d val:%s\n", XPair_First(pair, int), XPair_Second(pair, char*));*/
		XHashMap* copy=XHashMap_create_copy(map);
		XHashMap_delete_base(map);
		XHashMap_delete_base(copy);
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