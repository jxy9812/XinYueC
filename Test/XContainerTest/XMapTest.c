#include"XDataStructTest.h"
#if DEMOTEST
#include"XMap.h"
#include"XEquality.h"
#include"XLess.h"
#include"XBalancedBinaryTree.h"
static void XFor_each_pair(void* LPVal, void* args)
{
	XPair* pair = *(XPair**)LPVal;
	printf("key:%d val:%s\n", XPair_First(pair,int), XPair_Second(pair,char*));
}
void XMapTest()
{
	printf("XMap 测试\n");
	int arrayint[] = {1,23,456,5,23};
	char arraychar[][1000]={"琦神","星小白","章鱼哥","123dfsadsadsad","玩蛇"};
	XMap* map = XMap_New(int,char*,XEquality_int, XLess_int);
	
	for (size_t i=0;i<5;i++)
	{
		XMap_Insert(map,int, arrayint[i],char* ,arraychar[i]);
		//char* str = &arraychar[i];
		//XMap_insert(map, &arrayint[i], &str);
	}
	printf("当前Map容器内数据数量:%d\n", XMap_size(map));
	XMap_iterator_for_each(map, XFor_each_pair, NULL);

	//XMap_remove(map, arrayint+2);
	XMap_Remove(map,int,arrayint[2]);
	printf("当前Map容器内数据数量:%d\n", XMap_size(map));
	XMap_reverse_iterator_for_each(map, XFor_each_pair,NULL);
	
	XPair* pair =XMap_find(map, arrayint);
	printf("查询到:key:%d val:%s\n", XPair_First(pair, int), XPair_Second(pair,char*));
	XMap_clear(map);
}
#endif