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
XMap* XMap_new(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess)
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
	if (ISNULL(this_map, ""))
		return NULL;
	XContainerObject_init(&this_map->m_parent, valTypeSize);
	XMap_class_init();
	XClassGetVtable(this_map) = XMapVtable;
	this_map->m_keyTypeSize = keyTypeSize;
	this_map->m_KeyEquality = KeyEquality;
	this_map->m_KeyLess = KeyLess;
	this_map->m_isModify = true;
	this_map->m_itArray = NULL;
}



void XMap_insert_base(XMap* this_map, const void* key, const void* LpValue)
{
	if (ISNULL(this_map, "") || ISNULL(XClassGetVtable(this_map), ""))
		return;
	typedef void (*funcPtr)(XMap*, const void*, const void*);
	XClassGetVirtualFunc(this_map, EXMap_Insert, funcPtr)(this_map, key, LpValue);

}

void XMap_erase_base(XMap* this_map, const XPair** LPpair)
{
	if (ISNULL(this_map, "") || ISNULL(XClassGetVtable(this_map), ""))
		return;
	typedef void (*funcPtr)(XMap*, const XPair**);
	XClassGetVirtualFunc(this_map, EXMap_Erase, funcPtr)(this_map, LPpair);
}

void XMap_remove_base(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(XClassGetVtable(this_map), ""))
		return;
	typedef void (*funcPtr)(XMap*, const void*);
	XClassGetVirtualFunc(this_map, EXMap_Remove, funcPtr)(this_map, key);
}
void* XMap_value_base(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	typedef void* (*funcPtr)(XMap*, const void*);
	return XClassGetVirtualFunc(this_map, EXMap_Value, funcPtr)(this_map, key);
}
//查找数据XPair
XPair* XMap_find_base(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	typedef XPair* (*funcPtr)(XMap*, const void*);
	return XClassGetVirtualFunc(this_map, EXMap_Find, funcPtr)(this_map, key);
}


void XMap_DefaultDerivedClassDataKeyFreeMethod(void* args)
{
	XPair* pair = (XPair*)args;
	XContainerObject* object = *((XContainerObject**)XPair_second(pair));
	XContainerObject_free_base(object);
}

void XMap_DefaultDerivedClassDataValueFreeMethod(void* args)
{
	XPair* pair = (XPair*)args;
	XContainerObject* object = *((XContainerObject**)XPair_first(pair));
	XContainerObject_free_base(object);
}

void XMap_DefaultDerivedClassDataKeyValueFreeMethod(void* args)
{
	XMap_DefaultDerivedClassDataKeyFreeMethod(args);
	XMap_DefaultDerivedClassDataValueFreeMethod(args);
}


static void ForTreeNode(void* LPVal, void* args)
{
#if XVector_ON
	XRBTreeNode* nodes = *(XRBTreeNode**)LPVal;
	XVector_push_back(args, XVector_at(nodes->XBTNode.values, 0));
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
void XMap_updataIterator(XMap* this_map)
{
#if XVector_ON
	if (ISNULL(this_map, "map不能为NULL"))
		return;
	if (XMap_isEmpty_base(this_map))//map当前是空的
		return;
	if (!this_map->m_isModify)
		return;
	//开始更新迭代器
	if (this_map->m_itArray != NULL)
	{
		//释放原先的数组
		XVector_free_base(this_map->m_itArray);
		this_map->m_itArray = NULL;
	}
	//已中序遍历获取所有树的节点,临时
	XVector* TreeNode = XBTree_TraversingToXVector(this_map->m_parent.m_data, XBTreeInorder);
	this_map->m_itArray = XVector_new(sizeof(XPair*));
	//将数据XPair的节点指针插入数组
	XVector_iterator_for_each(TreeNode, ForTreeNode, this_map->m_itArray);
	XVector_free_base(TreeNode);

	this_map->m_isModify = false;
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
#endif