#include"XDataStructTest.h"
#if DEMOTEST
#include"XMap.h"
#include"XCompare.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XSort.h"
#include<string.h>
static void XFor_each_pair(void* LPVal, void* args)
{
	XPair* pair = (XPair*)LPVal;
	XPrintf("key:%d val:%s\n", XPair_First(pair,int), XPair_Second(pair,char*));
}
static void map_dataDelete(char** lpStr)
{
	if (!lpStr)return;
	char* str = *lpStr;
	XFree(str);
}
void XMapTest()
{
#if XMap_ON
	XMap* map = XMap_Create(int, char*, int_compare);
	XContainerSetDataDeinitMethod(map, map_dataDelete);
	//while (1)
	{
		XPrintf("XMap 测试\n");
		int arrayint[] = { 1,23,456,5,23 };
		char arraychar[][100] = { "琦神","星小白","章鱼哥","jjjjjjj","玩蛇" };
		
		for (size_t i = 0; i < 500; i++)
		{
			//XMap_Insert_Base(map, int, i, char*, arraychar[i&5]);
			char* str = XMemory_malloc(strlen(arraychar[i%5]) + 10);
			strcpy(str, arraychar[i%5]);
			XMap_insert_base(map, &i, &str);
		}
		XPrintf("当前Map容器内数据数量:%d\n", XMap_size_base(map));
		XMap_iterator_for_each(map, XFor_each_pair, NULL);

		XVector* list = XMapBase_keys_base(map);
		XDerangement(XContainerDataPtr(list), XContainerSize(list), XContainerTypeSize(list));
		for_each_iterator(list, XVector, it)
		{
			int* key = XVector_iterator_data(&it);
			//printf("Trying to remove key: %d\n", *key); // 打印要删除的键
			if (!XMap_remove_base(map, key))
			{
				XPrintf("删除失败了\n");
			}
		}
		
		XVector_delete_base(list);
		//XMap_Remove_Base(map, int, arrayint[2]);
		//XPrintf("当前Map容器内数据数量:%d\n", XMap_size_base(map));
		//XMap_reverse_iterator_for_each(map, XFor_each_pair, NULL);

		/*XPair* pair = XMap_find_base(map, arrayint);
		XPrintf("查询到:key:%d val:%s\n", XPair_First(pair, int), XPair_Second(pair, char*));*/
		
	}
	XMap_clear_base(map);
	XMap_delete_base(map);
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