#include"XMap.h"
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdlib.h>
//Map插入数据
static void VXMap_insert(XMap* this_map, const void* key, const void* LpValue);
static void VXMap_erase(XMap* this_map, const XPair** LPpair);
//map删除数据
static void VXMap_remove(XMap* this_map, const void* key);
//根据键值返回数据地址
static void* VXMap_at(XMap* this_map, const void* key);
//查找数据，返回找到的XPair地址，没有返回NULL
static XPair* VXMap_find(XMap* this_map, const void* key);
//清空Map，释放内存
static void VXMap_clear(XMap* this_map);
//释放内存
static void VXMap_free(XMap* this_map);
static void VXMap_swap(XMap* this_mapOne, XMap* this_mapTwo);
XVtable* XMapVtable = NULL;
#if VtableIsStack
	static XVtable vtable;//虚函数类
	static void* vtable_data[12];//虚函数数据
#endif
void XMap_class_init()
{
	if (XMapVtable)
		return;
	void* table[] = {
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_at,VXMap_find
	};
#if !VtableIsStack
	XMapVtable = XVtable_new();
#else
	XMapVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XMapVtable, XContainerObjectVtable);
	//重写的函数
	XVtable_At(XMapVtable, EXContainerObject_Clear) = VXMap_clear;
	XVtable_At(XMapVtable, EXContainerObject_Free) = VXMap_free;
	XVtable_At(XMapVtable, EXContainerObject_Swap) = VXMap_swap;
	//追加函数
	XVtable_append_array(XMapVtable, table, sizeof(table) / sizeof(table[0]));
#if ShowContainerSize
	printf("XMap size:%d\n", XVtable_size(XMapVtable));
#endif // ShowContainerSize
}
void VXMap_insert(XMap* this_map, const void* key, const void* LpValue)
{
	if (ISNULL(this_map, "")|| ISNULL(key, "")||ISNULL(LpValue, ""))
		return;
	XPair* pair = XMap_find(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		XPair* LPpair = XPair_new(this_map->keyTypeSize, this_map->object._typeSize);
		XPair_insert(LPpair, key, LpValue);

		//printf("创建的xpair key:%d LpValue:%s\n",XPair_First(LPpair,int),XPair_second(LPpair));

		XRBTree_insert(&(this_map->object._data), this_map->KeyLess, XCompareRuleTwo_XMap, &LPpair, sizeof(XPair*));

		/*XRBTreeNode* root= this_map->object._data;
		LPpair= *(XPair**)XVector_at(root->XBTNode.values,0);
		printf("根节点，key:%d LpValue:%s\n", XPair_First(LPpair, int), XPair_second(LPpair));*/
		++this_map->object._capacity;
		++this_map->object._size;
		this_map->isModify = true;
	}
	else
	{
		XPair_insertSecond(pair, LpValue);
	}
}

void VXMap_erase(XMap* this_map, const XPair** LPpair)
{
	if (ISNULL(this_map, "") || ISNULL(LPpair, ""))
		return;
	VXMap_remove(this_map, (*LPpair)->first);
}

void VXMap_remove(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return ;
	//XMap_updataIterator(this_map);
	XRBTreeNode* nodes = XRBTree_erase(&(this_map->object._data), this_map->KeyLess, this_map->KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes != NULL)
	{
		--ContainerCapacity(this_map);
		--ContainerSize(this_map);
	/*	--this_map->object._capacity;
		--this_map->object._size;*/
		this_map->isModify = true;
	}
}

void* VXMap_at(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return NULL;
	XPair* pair = XMap_find(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		pair = XPair_new(this_map->keyTypeSize, this_map->object._typeSize);
		XPair_insert(pair, key, NULL);
		XRBTree_insert(&(this_map->object._data), this_map->KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));
		++this_map->object._capacity;
		++this_map->object._size;
		this_map->isModify = true;
	}
	return XPair_second(pair);
}

XPair* VXMap_find(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return NULL;
	XBTreeNode* nodes = XBBTree_findData(this_map->object._data, this_map->KeyLess, this_map->KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes == NULL)
		return NULL;
	XPair* pair = *(XPair**)XVector_at(nodes->values, 0);
	return pair;
}

static void XMap_freeNodeData(void* LPVal, void* args)
{
	XPair* pair = *(XPair**)LPVal;
	XPair_free(pair);
	*(XPair**)LPVal = NULL;
}
void VXMap_clear(XMap* this_map)
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

void VXMap_free(XMap* this_map)
{
	XMap_clear(this_map);
	free(this_map);
}

void VXMap_swap(XMap* this_mapOne, XMap* this_mapTwo)
{
	typedef void (*funcPtr)(XContainerObject*, XContainerObject*);
	VtableFunc(XVectorVtable, EXContainerObject_Swap, funcPtr)(this_mapOne, this_mapTwo);
	//XContainerObject_swap(this_mapOne, this_mapTwo);
	XSwap(&this_mapOne->isModify, &this_mapTwo->isModify, sizeof(bool));
	XSwap(&this_mapOne->itArray, &this_mapTwo->itArray, sizeof(XVector*));
	XSwap(&this_mapOne->KeyEquality, &this_mapTwo->KeyEquality, sizeof(XEquality));
	XSwap(&this_mapOne->KeyLess, &this_mapTwo->KeyLess, sizeof(XLess));
	XSwap(&this_mapOne->keyTypeSize, &this_mapTwo->keyTypeSize, sizeof(size_t));
}
