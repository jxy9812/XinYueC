#include"XMap.h"
#if XMap_ON
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//Map插入数据
static bool VXMap_insert(XMap* this_map, const void* key, const void* pvValue);
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

bool VXMap_insert(XMap* this_map, const void* key, const void* pvValue)
{
	if (ISNULL(this_map, "")|| ISNULL(key, "")||ISNULL(pvValue, ""))
		return false;
	XPair* pair = XMap_find_base(this_map, key);
	if (pair == NULL)//当前没有这个键值对
	{
		XPair* pair = XPair_create(((XMapBase*)this_map)->m_keyTypeSize, XContainerTypeSize(this_map));
		XPair_insert(pair, key, pvValue);

		//printf("创建的xpair key:%d pvValue:%s\n",XPair_First(pair,int),XPair_second(LPpair));

		XRBTree_insert(&XContainerDataPtr(this_map), ((XMapBase*)this_map)->m_KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));

		/*XRBTreeNode* root= this_map->object.m_data;
		pair= *(XPair**)XVector_at_base(root->XBTNode.values,0);
		printf("根节点，key:%d pvValue:%s\n", XPair_First(pair, int), XPair_second(LPpair));*/
		++XContainerCapacity(this_map);
		++XContainerSize(this_map);
	}
	else//插入的已经存在键了
	{
		if (XContainerDataDeleteMethod(this_map) != NULL)
			XContainerDataDeleteMethod(this_map)(pair);
		XPair_insertSecond(pair, pvValue);
		//拷贝键值
		memcpy(((uint8_t*)(&(pair->m_first))), key, pair->m_firstTypeSize);
	}
	return true;
}

void VXMap_erase(XMap* this_map, const XPair* pPair)
{
	/*if (ISNULL(this_map, "") || ISNULL(pair, ""))
		return;*/
	XMap_remove_base(this_map, XPair_first(pPair));
}

bool VXMap_remove(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return false;
	XRBTreeNode* nodes = XRBTree_findData(XContainerDataPtr(this_map), ((XMapBase*)this_map)->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes != NULL)
	{
		XPair* pair = XBTreeNode_GetData(nodes, XPair*);
		if (XContainerDataDeleteMethod(this_map) != NULL)
			XContainerDataDeleteMethod(this_map)(pair);
		XRBTree_erase(&XContainerDataPtr(this_map), ((XMapBase*)this_map)->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, key);
		XPair_delete(pair);
		--XContainerCapacity(this_map);
		--XContainerSize(this_map);
		return true;
	}
	return false;
}

void* VXMap_value(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return NULL;
	XPair* pair = XMap_find_base(this_map, key);
	if (pair)
		return XPair_second(pair);
	return NULL;
}

XPair* VXMap_find(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return NULL;
	XBTreeNode* nodes = XRBTree_findData(XContainerDataPtr(this_map), ((XMapBase*)this_map)->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, key);
	if (nodes == NULL)
		return NULL;
	XPair* pair = XBTreeNode_GetData(nodes, XPair*);
	return pair;
}
XVector* VXMapBase_keys(const XMapBase* this_map)
{
	XVector* v = XVector_create(this_map->m_keyTypeSize);
	for_each_iterator(this_map, XMap, it)
	{
		XVector_push_back_base(v, XPair_first(XMap_iterator_data(&it)));
	}
	return v;
}
static void XMap_freeNodeData(XPair** pair, XMap* this_map)
{
	if (XContainerDataDeleteMethod(this_map) != NULL)
		XContainerDataDeleteMethod(this_map)(*pair);
	XPair_delete(*pair);
}
void VXMap_clear(XMap* this_map)
{
	if (XMap_isEmpty_base(this_map))
		return;
	//XMap_iterator_for_each(this_map, XMap_freeNodeData, this_map);
	XBTree_delete(XContainerDataPtr(this_map), XMap_freeNodeData, this_map);
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
	XSwap(&((XMapBase*)this_mapOne)->m_KeyLess, &((XMapBase*)this_mapTwo)->m_KeyLess, sizeof(XLess));
	XSwap(&((XMapBase*)this_mapOne)->m_keyTypeSize, &((XMapBase*)this_mapTwo)->m_keyTypeSize, sizeof(size_t));
}

#endif