#include"XMap.h"
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
	XContainerObject_init(&this_map->object, valTypeSize);
	XMap_class_init();
	ObjectVtable(this_map) = XMapVtable;
	this_map->keyTypeSize = keyTypeSize;
	this_map->KeyEquality = KeyEquality;
	this_map->KeyLess = KeyLess;
	this_map->isModify = true;
	this_map->itArray = NULL;
}



void XMap_insert(XMap* this_map, const void* key, const void* LpValue)
{
	if (ISNULL(this_map, "") || ISNULL(ObjectVtable(this_map), ""))
		return;
	typedef void (*funcPtr)(XMap*, const void*, const void*);
	ObjectVirtualFunc(this_map, EXMap_Insert, funcPtr)(this_map, key, LpValue);

}

void XMap_erase(XMap* this_map, const XPair** LPpair)
{
	if (ISNULL(this_map, "") || ISNULL(ObjectVtable(this_map), ""))
		return;
	typedef void (*funcPtr)(XMap*, const XPair**);
	ObjectVirtualFunc(this_map, EXMap_Erase, funcPtr)(this_map, LPpair);
}

void XMap_remove(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(ObjectVtable(this_map), ""))
		return;
	typedef void (*funcPtr)(XMap*, const void*);
	ObjectVirtualFunc(this_map, EXMap_Remove, funcPtr)(this_map, key);
}
void* XMap_at(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(ObjectVtable(this_map), ""))
		return NULL;
	typedef void* (*funcPtr)(XMap*, const void*);
	return ObjectVirtualFunc(this_map, EXMap_At, funcPtr)(this_map, key);
}
//查找数据XPair
XPair* XMap_find(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(ObjectVtable(this_map), ""))
		return NULL;
	typedef XPair* (*funcPtr)(XMap*, const void*);
	return ObjectVirtualFunc(this_map, EXMap_Find, funcPtr)(this_map, key);
}

void XMap_clear(XMap* this_map)
{
	XContainerObject_clear(this_map);
}

void XMap_free(XMap* this_map)
{
	XContainerObject_free(this_map);
}

bool XMap_empty(const XMap* this_map)
{
	return XContainerObject_empty(this_map);
}

size_t XMap_size(const XMap* this_map)
{
	return XContainerObject_size(this_map);
}

void XMap_swap(XMap* this_mapOne, XMap* this_mapTwo)
{
	if (ISNULL(this_mapOne, "") || ISNULL(ObjectVtable(this_mapOne), "")|| ISNULL(this_mapTwo, ""))
		return ;
	typedef XPair* (*funcPtr)(XMap*, XMap*);
	ObjectVirtualFunc(this_mapOne, EXContainerObject_Swap, funcPtr)(this_mapOne, this_mapTwo);
}


static void ForTreeNode(void* LPVal, void* args)
{
	XRBTreeNode* nodes = *(XRBTreeNode**)LPVal;
	XVector_push_back(args, XVector_at(nodes->XBTNode.values, 0));
}
void XMap_updataIterator(XMap* this_map)
{
	if (ISNULL(this_map, "map不能为NULL"))
		return;
	if (XMap_empty(this_map))//map当前是空的
		return;
	if (!this_map->isModify)
		return;
	//开始更新迭代器
	if (this_map->itArray != NULL)
	{
		//释放原先的数组
		XVector_free(this_map->itArray);
		this_map->itArray = NULL;
	}
	//已中序遍历获取所有树的节点,临时
	XVector* TreeNode = XBTree_TraversingToXVector(this_map->object._data, XBTreeInorder);
	this_map->itArray = XVector_new(sizeof(XPair*));
	//将数据XPair的节点指针插入数组
	XVector_iterator_for_each(TreeNode, ForTreeNode, this_map->itArray);
	XVector_free(TreeNode);

	this_map->isModify = false;
}