#include"XMap.h"
#if XMap_ON
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
static void* VXMap_value(XMap* this_map, const void* key);
//查找数据，返回找到的XPair地址，没有返回NULL
static XPair* VXMap_find(XMap* this_map, const void* key);
//清空Map，释放内存
static void VXMap_clear(XMap* this_map);
//释放内存
static void VXMap_free(XMap* this_map);
static void VXMap_swap(XMap* this_mapOne, XMap* this_mapTwo);
XVtable* XMap_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XMAP_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	void* table[] = {
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear,VXMap_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Free,VXMap_free);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XMap size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}

void VXMap_insert(XMap* this_map, const void* key, const void* LpValue)
{
	if (ISNULL(this_map, "")|| ISNULL(key, "")||ISNULL(LpValue, ""))
		return;
	XPair* pair = XMap_find_base(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		XPair* LPpair = XPair_new(this_map->m_keyTypeSize, this_map->m_parent.m_typeSize);
		XPair_insert(LPpair, key, LpValue);

		//printf("创建的xpair key:%d LpValue:%s\n",XPair_First(LPpair,int),XPair_second(LPpair));

		XRBTree_insert(&(this_map->m_parent.m_data), this_map->m_KeyLess, XCompareRuleTwo_XMap, &LPpair, sizeof(XPair*));

		/*XRBTreeNode* root= this_map->object.m_data;
		LPpair= *(XPair**)XVector_at_base(root->XBTNode.values,0);
		printf("根节点，key:%d LpValue:%s\n", XPair_First(LPpair, int), XPair_second(LPpair));*/
		++this_map->m_parent.m_capacity;
		++this_map->m_parent.m_size;
		this_map->m_isModify = true;
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
	VXMap_remove(this_map, (*LPpair)->m_first);
}

void VXMap_remove(XMap* this_map, const void* key)
{
#if XVector_ON
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return ;
	XRBTreeNode* nodes = XBBTree_findData(this_map->m_parent.m_data, this_map->m_KeyLess, this_map->m_KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes != NULL)
	{
		XPair* pair = *((XPair**)XVector_at_base(nodes->XBTNode.values, 0));
		if (XContainerDataFreeMethod(this_map) != NULL)
			XContainerDataFreeMethod(this_map)(pair);
		XRBTree_erase(&(this_map->m_parent.m_data), this_map->m_KeyLess, this_map->m_KeyEquality, XCompareRuleOne_XMap, key);
		XPair_free(pair);
		--XContainerCapacity(this_map);
		--XContainerSize(this_map);
	/*	--this_map->object.m_capacity;
		--this_map->object.m_size;*/
		this_map->m_isModify = true;
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}

void* VXMap_value(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return NULL;
	XPair* pair = XMap_find_base(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		pair = XPair_new(this_map->m_keyTypeSize, this_map->m_parent.m_typeSize);
		XPair_insert(pair, key, NULL);
		XRBTree_insert(&(this_map->m_parent.m_data), this_map->m_KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));
		++this_map->m_parent.m_capacity;
		++this_map->m_parent.m_size;
		this_map->m_isModify = true;
	}
	return XPair_second(pair);
}

XPair* VXMap_find(XMap* this_map, const void* key)
{
#if XVector_ON
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return NULL;
	XBTreeNode* nodes = XBBTree_findData(this_map->m_parent.m_data, this_map->m_KeyLess, this_map->m_KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes == NULL)
		return NULL;
	XPair* pair = *(XPair**)XVector_at_base(nodes->values, 0);
	return pair;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}

static void XMap_freeNodeData(void* LPVal, void* args)
{
	XPair* pair = *(XPair**)LPVal;
	if (XContainerDataFreeMethod(args) != NULL)
		XContainerDataFreeMethod(args)(pair);
	XPair_free(pair);
	*(XPair**)LPVal = NULL;
}
void VXMap_clear(XMap* this_map)
{
#if XVector_ON
	if (XMap_isEmpty_base(this_map))
		return;
	XMap_updataIterator(this_map);
	XMap_iterator_for_each(this_map, XMap_freeNodeData, this_map);
	XBTree_freeNodeAll(this_map->m_parent.m_data);
	if(this_map->m_itArray)
	{
		XVector_free_base(this_map->m_itArray);
		this_map->m_itArray = NULL;
	}
	this_map->m_parent.m_capacity = 0;
	this_map->m_parent.m_size = 0;
	this_map->m_parent.m_data = NULL;
	this_map->m_isModify = false;
#else
	IS_ON_DEBUG(XVector_ON);
#endif;
}

void VXMap_free(XMap* this_map)
{
	XMap_clear_base(this_map);
	XMemory_free(this_map);
}

void VXMap_swap(XMap* this_mapOne, XMap* this_mapTwo)
{
#if XVector_ON
	typedef void (*funcPtr)(XContainerObject*, XContainerObject*);
	XVtableGetFunc(XVector_class_init(), EXContainerObject_Swap, funcPtr)(this_mapOne, this_mapTwo);
	//XContainerObject_swap_base(this_mapOne, this_mapTwo);
	XSwap(&this_mapOne->m_isModify, &this_mapTwo->m_isModify, sizeof(bool));
	XSwap(&this_mapOne->m_itArray, &this_mapTwo->m_itArray, sizeof(XVector*));
	XSwap(&this_mapOne->m_KeyEquality, &this_mapTwo->m_KeyEquality, sizeof(XEquality));
	XSwap(&this_mapOne->m_KeyLess, &this_mapTwo->m_KeyLess, sizeof(XLess));
	XSwap(&this_mapOne->m_keyTypeSize, &this_mapTwo->m_keyTypeSize, sizeof(size_t));
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}

#endif