#include"XMap.h"
#include"XMap_func.h"
#include"XContainerObject.h"
#include"XPair.h"
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
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
	XMap* this_map = (XMap*)malloc(sizeof(XMap));
	if (isNULL(isNULLInfo(this_map, "")))
		return NULL;
	XContainerObject_init(&this_map->object, valTypeSize);
	this_map->keyTypeSize = keyTypeSize;
	this_map->KeyEquality = KeyEquality;
	this_map->KeyLess = KeyLess;
	this_map->isModify = true;
	this_map->itArray = NULL;
	return this_map;
}


void XMap_insert(XMap* this_map, const void* key, const void* val)
{
	if (isNULL(isNULLInfo(this_map, "")))
		return;
	XPair* pair = XMap_find(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		XPair* LPpair = XPair_init(this_map->keyTypeSize, this_map->object._typeSize);
		XPair_insert(LPpair, key, val);

		//printf("创建的xpair key:%d val:%s\n",XPair_First(LPpair,int),XPair_second(LPpair));

		XRBTree_insert(&(this_map->object._data), this_map->KeyLess, XCompareRuleTwo_XMap, &LPpair, sizeof(XPair*));

		/*XRBTreeNode* root= this_map->object._data;
		LPpair= *(XPair**)XVector_at(root->XBTNode.values,0);
		printf("根节点，key:%d val:%s\n", XPair_First(LPpair, int), XPair_second(LPpair));*/
		++this_map->object._capacity;
		++this_map->object._size;
		this_map->isModify = true;
	}
	else
	{
		XPair_insertSecond(pair, val);
	}

}

void XMap_erase(XMap* this_map, const void* key)
{
	if (isNULL(isNULLInfo(this_map, "")))
		return NULL;
	//XMap_updataIterator(this_map);
	XRBTreeNode* nodes = XRBTree_erase(&(this_map->object._data), this_map->KeyLess, this_map->KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes != NULL)
	{
		--this_map->object._capacity;
		--this_map->object._size;
		this_map->isModify = true;
	}
}
void* XMap_at(XMap* this_map, const void* key)
{
	if (isNULL(isNULLInfo(this_map, "")))
		return NULL;
	XPair* pair = XMap_find(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		pair = XPair_init(this_map->keyTypeSize, this_map->object._typeSize);
		XPair_insert(pair, key, NULL);
		XRBTree_insert(&(this_map->object._data), this_map->KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));
		++this_map->object._capacity;
		++this_map->object._size;
		this_map->isModify = true;
	}
	return XPair_second(pair);
}
//查找数据XPair
XPair* XMap_find(XMap* this_map, const void* key)
{
	if (isNULL(isNULLInfo(this_map, "")))
		return NULL;
	XBTreeNode* nodes = XBBTree_findData(this_map->object._data, this_map->KeyLess, this_map->KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes == NULL)
		return NULL;
	XPair* pair = *(XPair**)XVector_at(nodes->values,0);
	return pair;
}
static void XMap_freeNodeData(void* LPVal, void* args)
{
	XPair* pair = *(XPair**)LPVal;
	XPair_free(pair);
	*(XPair**)LPVal = NULL;
}
void XMap_clear(XMap* this_map)
{
	if (XMap_empty(this_map))
		return;
	XMap_updataIterator(this_map);
	XMap_iterator_for_each(this_map, XMap_freeNodeData, NULL);
	XBTree_freeNodeAll(this_map->object._data);
	XVector_free(this_map->itArray);
	this_map->itArray = NULL;
	this_map->object._capacity = 0;
	this_map->object._size = 0;
	this_map->object._data = NULL;
	this_map->isModify = false;
}

void XMap_free(XMap* this_map)
{
	XMap_clear(this_map);
	free(this_map);
}

bool XMap_empty(const XMap* this_map)
{
	return XContainerObject_empty(this_map);
}

int XMap_size(const XMap* this_map)
{
	return XContainerObject_size(this_map);
}

void XMap_swap(XMap* this_mapOne, XMap* this_mapTwo)
{
	XContainerObject_swap(this_mapOne, this_mapTwo);
	XSwap(&this_mapOne->isModify, &this_mapTwo->isModify, sizeof(bool));
	XSwap(&this_mapOne->itArray, &this_mapTwo->itArray, sizeof(XVector*));
	XSwap(&this_mapOne->KeyEquality, &this_mapTwo->KeyEquality, sizeof(XEquality));
	XSwap(&this_mapOne->KeyLess, &this_mapTwo->KeyLess, sizeof(XLess));
	XSwap(&this_mapOne->keyTypeSize, &this_mapTwo->keyTypeSize, sizeof(size_t));
}