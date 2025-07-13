#include"XMap.h"
#if XMap_ON
#include"XContainerObject.h"
#include"XPair.h"
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
XMap* XMap_create(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess)
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
	XMap* this_map = (XMap*)XMemory_malloc(sizeof(XMap));
	XMap_init(this_map,keyTypeSize,valTypeSize,KeyEquality,KeyLess);
	return this_map;
}
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess)
{
	if (ISNULL(this_map, ""))
		return NULL;
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
	XMapBase_init(this_map, keyTypeSize, valTypeSize, KeyEquality);
	XClassGetVtable(this_map) = XMap_class_init();
	this_map->m_KeyLess = KeyLess;
}

XMap* XMap_create_XStringXVariant()
{
	XMap* map = XMap_Create(XString*, XVariant*, XEquality_XString, XLess_XString);
	if (map == NULL)
		return NULL;
	XContainerSetDataDeleteMethod(map, XMapBase_KeyXStringValueXVariantDeleteMethod);
	return map;
}

#endif