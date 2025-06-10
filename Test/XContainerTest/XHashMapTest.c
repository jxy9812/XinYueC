#include"XDataStructTest.h"
#if DEMOTEST
#include"XHashMap.h"
#include"XEquality.h"
#include"XLess.h"
#include"XBalancedBinaryTree.h"

static void XFor_each_pair(void* LPVal, void* args)
{
	XPair* pair = (XPair*)LPVal;
	printf("key:%d val:%s\n", XPair_First(pair,int), XPair_Second(pair,char*));
}
void XHashMapTest()
{
#if XHashMap_ON
	printf("XHashMap 测试\n");
	//while (true)
	{
		int arrayint[] = { 1,23,456,5,23 };
		char arraychar[][100] = { "琦神","星小白","章鱼哥","你好啊111hjhj1","玩蛇" };
		XHashMap* map = XHashMap_Create(int, char*, XHash_murmur3_32, XEquality_int);

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
		printf("当前Map容器内数据数量:%d\n", XHashMap_getSize_base(map));

		XHashMap_iterator_for_each(map, XFor_each_pair, NULL);

		XHashMap_remove_base(map, arrayint + 2);
		printf("当前Map容器内数据数量:%d\n", XHashMap_getSize_base(map));
		XHashMap_iterator_for_each(map, XFor_each_pair, NULL);

		XPair* pair = XHashMap_find_base(map, arrayint + 1);
		printf("查询到:key:%d val:%s\n", XPair_First(pair, int), XPair_Second(pair, char*));
		XHashMap_clear_base(map);
		XHashMap_delete_base(map);
	}
	
#endif
}
#endif