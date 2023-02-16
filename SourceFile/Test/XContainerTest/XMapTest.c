#include"Test.h"
#include"XMap.h"
#include"XEquality.h"
#include"XLess.h"
#include"XBalancedBinaryTree.h"
static void XFor_each_pair(void* LPVal, void* args)
{
	XPair* pair = *(XPair**)LPVal;
	printf("key:%d val:%s\n", XPair_First(pair,int), XPair_second(pair));
}
void XMapTest()
{
	int arryint[] = {1,23,456,5};
	char arraychar[][100]={"琦神","小白","666","私房菜"};
	XMap* map = XMap_init(sizeof(int),sizeof(char*),XEquality_int, XLess_int);
	
	for (size_t i=0;i<4;i++)
	{
		XMap_insert(map, &arryint[i], arraychar[i]);
	}

	XMap_iterator_for_each(map, XFor_each_pair,NULL);
	XPair* pair =XMap_find(map, arryint);
	printf("查询到:key: % d val : % s\n", XPair_First(pair, int), XPair_second(pair));
}