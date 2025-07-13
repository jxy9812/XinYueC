#include"XMap.h"
#if XMap_ON
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdlib.h>
//Map插入数据
static bool VXMap_insert(XMap* this_map, const void* key, const void* LpValue);
static void VXMap_erase(XMap* this_map, const XPair* LPpair);
//map删除数据
static bool VXMap_remove(XMap* this_map, const void* key);
//根据键值返回数据地址
static void* VXMap_value(XMap* this_map, const void* key);
//查找数据，返回找到的XPair地址，没有返回NULL
static XPair* VXMap_find(XMap* this_map, const void* key);
//返回key数组
static XVector* VXMapBase_keys(const XMapBase* this_map);
//清空Map，释放内存
static void VXMap_clear(XMap* this_map);
//释放内存
static void VXMap_delete(XMap* this_map);
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
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find,
		VXMapBase_keys
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear,VXMap_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete,VXMap_delete);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XMap size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}

bool VXMap_insert(XMap* this_map, const void* key, const void* LpValue)
{
	if (ISNULL(this_map, "")|| ISNULL(key, "")||ISNULL(LpValue, ""))
		return false;
	XPair* pair = XMap_find_base(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		XPair* pair = XPair_create(((XMapBase*)this_map)->m_keyTypeSize, XContainerTypeSize(this_map));
		XPair_insert(pair, key, LpValue);

		//printf("创建的xpair key:%d LpValue:%s\n",XPair_First(pair,int),XPair_second(LPpair));

		XRBTree_insert(&XContainerDataPtr(this_map), this_map->m_KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));

		/*XRBTreeNode* root= this_map->object.m_data;
		pair= *(XPair**)XVector_at_base(root->XBTNode.values,0);
		printf("根节点，key:%d LpValue:%s\n", XPair_First(pair, int), XPair_second(LPpair));*/
		++XContainerCapacity(this_map);
		++XContainerSize(this_map);
	}
	else
	{
		XPair_insertSecond(pair, LpValue);
	}
	return true;
}

void VXMap_erase(XMap* this_map, const XPair* LPpair)
{
	/*if (ISNULL(this_map, "") || ISNULL(pair, ""))
		return;*/
	XMap_remove_base(this_map, (LPpair)->m_first);
}

bool VXMap_remove(XMap* this_map, const void* key)
{
#if XVector_ON
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return false;
	XRBTreeNode* nodes = XBBTree_findData(XContainerDataPtr(this_map), this_map->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes != NULL)
	{
		XPair* pair = *((XPair**)XVector_at_base(nodes->XBTNode.values, 0));
		if (XContainerDataDeleteMethod(this_map) != NULL)
			XContainerDataDeleteMethod(this_map)(pair);
		XRBTree_erase(&XContainerDataPtr(this_map), this_map->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, key);
		XPair_delete(pair);
		--XContainerCapacity(this_map);
		--XContainerSize(this_map);
		return true;
	}
	return false;
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
		pair = XPair_create(((XMapBase*)this_map)->m_keyTypeSize,XContainerTypeSize(this_map));
		XPair_insert(pair, key, NULL);
		XRBTree_insert(&XContainerDataPtr(this_map), this_map->m_KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));
		++XContainerCapacity(this_map);
		++XContainerSize(this_map);
	}
	return XPair_second(pair);
}

XPair* VXMap_find(XMap* this_map, const void* key)
{
#if XVector_ON
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return NULL;
	XBTreeNode* nodes = XBBTree_findData(XContainerDataPtr(this_map), this_map->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes == NULL)
		return NULL;
	XPair* pair = *(XPair**)XVector_at_base(nodes->values, 0);
	return pair;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}
XVector* VXMapBase_keys(const XMapBase* this_map)
{

	XVector* v = XVector_create(this_map->m_keyTypeSize);
	for_each_iterator(this_map, XMap, it)
	{
		XVector_push_back_base(v, XPair_first(XHash_iterator_data(&it)));
	}
	return v;
}
static void XMap_freeNodeData(XPair* pair, void* args)
{
	if (XContainerDataDeleteMethod(args) != NULL)
		XContainerDataDeleteMethod(args)(pair);
	XPair_delete(pair);
}
void VXMap_clear(XMap* this_map)
{
	if (XMap_isEmpty_base(this_map))
		return;
	XMap_iterator_for_each(this_map, XMap_freeNodeData, this_map);
	XBTree_freeNodeAll(XContainerDataPtr(this_map));
	XContainerCapacity(this_map) = 0;
	XContainerSize(this_map) = 0;
	XContainerDataPtr(this_map) = NULL;
}

void VXMap_delete(XMap* this_map)
{
	XMap_clear_base(this_map);
	XMemory_free(this_map);
}

void VXMap_swap(XMap* this_mapOne, XMap* this_mapTwo)
{
	XVtableGetFunc(XVector_class_init(), EXContainerObject_Swap, void (*)(XContainerObject*, XContainerObject*))(this_mapOne, this_mapTwo);
	//XContainerObject_swap_base(this_mapOne, this_mapTwo);
	XSwap(&((XMapBase*)this_mapOne)->m_KeyEquality, &((XMapBase*)this_mapTwo)->m_KeyEquality, sizeof(XEquality));
	XSwap(&this_mapOne->m_KeyLess, &this_mapTwo->m_KeyLess, sizeof(XLess));
	XSwap(&((XMapBase*)this_mapOne)->m_keyTypeSize, &((XMapBase*)this_mapTwo)->m_keyTypeSize, sizeof(size_t));
}

#endif