#include"XMap.h"
#if XMap_ON
#include"XContainerObject.h"
#include"XPair.h"
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include"XString.h"
#include"XVariant.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
XMap* XMap_create(const size_t keyTypeSize, const size_t valTypeSize, XCompare compare)
{
	if (keyTypeSize == 0 || valTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (compare == NULL)
	{
		printf("compare比较函数NULL");
		return NULL;
	}
	XMap* this_map = (XMap*)XMemory_malloc(sizeof(XMap));
	XMap_init(this_map,keyTypeSize,valTypeSize, compare);
	return this_map;
}
XMap* XMap_create_copy(const XMap* other)
{
	if (other == NULL)
		return NULL;
	XMap* map = XMap_create(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), XContainerCompare(other));
	if (map == NULL)
		return NULL;
	XMap_copy_base(map, other);
	return map;
}
XMap* XMap_create_move(XMap* other)
{
	if (other == NULL)
		return NULL;
	XMap* map = XMap_create(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), XContainerCompare(other));
	if (map == NULL)
		return NULL;
	XMap_move_base(map, other);
	return map;
}
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare)
{
	if (ISNULL(this_map, ""))
		return NULL;
	if (keyTypeSize == 0 || valTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (compare == NULL)
	{
		printf("compare比较函数NULL");
		return NULL;
	}
	XMapBase_init(this_map, keyTypeSize, valTypeSize, compare);
	XClassGetVtable(this_map) = XMap_class_init();
	//this_map->m_KeyLess = KeyLess;
}
XVariantMap* XMap_create_XVariantMap()
{
	XMap* map = XMap_Create(XString, XVariant, XString_compare);
	if (map == NULL)
		return NULL;
	/*XContainerSetDataCopyMethod(map, XMapBase_XVariantMapCopyMethod);
	XContainerSetDataMoveMethod(map, XMapBase_XVariantMapMoveMethod);
	XContainerSetDataDeinitMethod(map, XMapBase_XVariantMapDeinitMethod);*/

	XMapBaseSetKeyCopyMethod(map, XString_copy_base);
	XMapBaseSetKeyMoveMethod(map, XString_move_base);
	XMapBaseSetKeyDeinitMethod(map, XString_deinit_base);

	XContainerSetDataCopyMethod(map, XVariant_copy_base);
	XContainerSetDataMoveMethod(map, XVariant_move_base);
	XContainerSetDataDeinitMethod(map, XVariant_deinit_base);

	return map;
}

#endif