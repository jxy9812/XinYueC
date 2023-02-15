#include"XMap.h"
#include"XMap_func.h"
#include"XContainerObject.h"
#include"XPair.h"
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

XMap* XMap_init(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess)
{
	if (keyTypeSize == 0 || valTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (KeyEquality == NULL || KeyLess == NULL)
	{
		printf("KeyEquality相等比较函数NULL或KeyLess小于比较函数NULL");
		return NULL;
	}
	XMap* map =(XMap*)malloc(sizeof(XMap));
	if (isNULL(isNULLInfo(map, "")))
		return NULL;
	XContainerObject_init(&map->object, keyTypeSize);
	map->secondTypeSize = valTypeSize;
	map->KeyEquality = KeyEquality;
	map->KeyLess = KeyLess;
	return map;
}
void XMap_insert(XMap* this_map, const void* key, const void* val)
{
	if (isNULL(isNULLInfo(this_map, "")))
		return;
	XPair* LPpair = XPair_init(this_map->object._type,this_map->secondTypeSize);
	XPair_insert(LPpair, key, val);
	if (!XContainerObject_empty(this_map))//现在是空的
	{
		this_map->object._data = XRBTree_insert(NULL, this_map->KeyLess, LPpair,sizeof(XPair));
	}
	else
	{
		XRBTree_insert(&this_map->object._data, this_map->KeyLess, LPpair, sizeof(XPair));
	}
	++this_map->object._capacity;
	++this_map->object._size;
}

void* XMap_find(XMap* this_map, const void* key)
{
	return XPair_second(XBBTree_findData(this_map->object._data, this_map->KeyLess, this_map->KeyEquality, key)->XBTNode.data);
}