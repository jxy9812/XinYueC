#include"XHashMap.h"
#if XHashMap_ON
#include"XAlgorithm.h"
#include"XVector.h"
#include"XRedBlackTree.h"
#include<string.h>
//Map插入数据
static bool VXMap_insert(XHashMap*this_map, const void* pvKey, const void* pvValue);
static bool VXMap_insert_move(XHashMap* this_map, const void* pvKey, const void* pvValue);
static void VXMap_erase(XHashMap* this_map, const XHashMap_iterator* it, XHashMap_iterator* next);
//map删除数据
static bool VXMap_remove(XHashMap*this_map, const void* pvKey);
//根据键值返回数据地址
static void* VXMap_value(XHashMap*this_map, const void* pvKey);
//查找数据，返回找到的XPair地址，没有返回NULL
static XPair* VXMap_find(XHashMap*this_map, const void* pvKey);
//返回key数组
static XVector* VXMapBase_keys(const XMapBase* this_map);
//清空Map，释放内存
static void VXMap_clear(XHashMap*this_map);
static void VXClass_copy(XHashMap* object, const XHashMap* src);
static void VXClass_move(XHashMap* object, XHashMap* src);
static void VXMap_deinit(XHashMap*this_map);
static void VXMap_swap(XHashMap*this_mapOne, XHashMap*this_mapTwo);
// 私有函数：扩容哈希表
static bool XHashMap_resize(XHashMap*map, size_t new_capacity);

XVtable* XHashMap_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XHASHMAP_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	void* table[] = {
		VXMap_insert,VXMap_insert_move,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find,
		VXMapBase_keys
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXMap_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMap_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XHash size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}
// 私有函数：扩容哈希表
static bool XHashMap_resize(XHashMap* map, size_t new_capacity)
{
	//printf("进入扩容\n");
	size_t new_size = new_capacity * sizeof(XRBTreeNode*);
	XRBTreeNode** newData = XMemory_malloc(new_size);
	memset(newData, 0, new_size);
	if (newData == NULL)
		return false;

	// 遍历原哈希表
	for (size_t i = 0; i < XContainerCapacity(map); i++)
	{
		XRBTreeNode* root = ((XRBTreeNode**)XContainerDataPtr(map))[i];
		if (root != NULL)
		{
			// 遍历红黑树，将节点插入到新哈希表中
			XVector* nodes = XBTree_TraversingToXVector(root, XBTreeInorder);
			if (nodes != NULL)
			{
				for (size_t j = 0; j < XVector_size_base(nodes); j++)
				{
					XRBTreeNode* node = ((XRBTreeNode**)XContainerDataPtr(nodes))[j];
					XPair* pair = XRBTree_GetData(node, XPair*);
					size_t index = map->m_hash(XPair_first(pair), ((XMapBase*)map)->m_keyTypeSize) % new_capacity;

					// 将节点插入到新哈希表的相应红黑树中
					XRBTree_insert(&newData[index], ((XMapBase*)map)->m_KeyLess, XCompareRuleTwo_XMap, XTreeNode_getData(node), sizeof(XPair*));
				}
				XVector_delete_base(nodes);
			}
			// 删除原红黑树
			XRBTree_delete(root, NULL, NULL);
		}
	}

	// 释放原哈希表数组
	XMemory_free(XContainerDataPtr(map));
	XContainerDataPtr(map) = newData;
	XContainerCapacity(map) = new_capacity;
	return true;
}
static bool insert(XHashMap* this_map, const void* pvKey, const void* pvValue, XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod)
{
	if ((double)XContainerSize(this_map) / XContainerCapacity(this_map) >= DEFAULT_LOAD_FACTOR)
	{
		//printf("XHash 扩容\n");
		size_t new_capacity = XContainerCapacity(this_map) * 2;
		if (!XHashMap_resize(this_map, new_capacity))
		{
			printf("XHash 扩容失败\n");
			return false;
		}
	}

	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XPair* pair = XHashMap_find_base(this_map, pvKey);
	if (pair == NULL)
	{//节点不存在
		XPair* pair = XPair_create(((XMapBase*)this_map)->m_keyTypeSize, XContainerTypeSize(this_map));
		if (keyCreatMethod)
			keyCreatMethod(XPair_first(pair), pvKey);
		else
			XPair_insertFirst(pair, pvKey);
		if (dataCreatMethod)
			dataCreatMethod(XPair_second(pair), pvValue);
		else
			XPair_insertSecond(pair, pvValue);
		XRBTree_insert(((XRBTreeNode**)XContainerDataPtr(this_map)) + index, ((XMapBase*)this_map)->m_KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));
		//++XContainerCapacity(this_map);
		++XContainerSize(this_map);
	}
	else
	{
		if (XContainerDataCopyMethod(this_map))
		{
			keyCreatMethod(XPair_second(pair), pvValue);
		}
		else
		{
			if (XContainerDataDeinitMethod(this_map) != NULL)
				XContainerDataDeinitMethod(this_map)(pair);
			XPair_insert(pair, pvKey, pvValue);
		}

	}
	return true;
}
bool VXMap_insert(XHashMap*this_map, const void* pvKey, const void* pvValue)
{
	return insert(this_map, pvKey, pvValue, XMapBaseKeyCopyMethod(this_map), XContainerDataCopyMethod(this_map));
}

bool VXMap_insert_move(XHashMap* this_map, const void* pvKey, const void* pvValue)
{
	return insert(this_map, pvKey, pvValue, XMapBaseKeyMoveMethod(this_map), XContainerDataMoveMethod(this_map));
}

void VXMap_erase(XHashMap*this_map, const XHashMap_iterator* it, XHashMap_iterator* next)
{
	/*if (XMapBase_isEmpty_base(this_map))
		return;*/
	//XHashMap_remove_base(this_map,XPair_first(pPair));
}

bool VXMap_remove(XHashMap*this_map, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_map))
		return false;
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XRBTreeNode* nodes = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_map))[index], ((XMapBase*)this_map)->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, pvKey);
	if (nodes != NULL)
	{
		XRBTree_remove(((XRBTreeNode**)XContainerDataPtr(this_map)) + index, ((XMapBase*)this_map)->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, pvKey, XMapBase_deleteNodeData,this_map);
		--XContainerSize(this_map);
		return true;
	}
	return false;
}

void* VXMap_value(XHashMap*this_map, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_map))
		return NULL;
	//size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);

	XPair* pair = XHashMap_find_base(this_map, pvKey);
	if (pair)
		return XPair_second(pair);
	return NULL;
	/*XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[index];

	while (current) 
	{
		if (((XMapBase*)this_map)->m_KeyEquality(XPair_first(current->pair), pvKey))
		{
			return XPair_second(current->pair);
		}
		current = current->next;
	}*/
	return NULL;
}

XPair* VXMap_find(XHashMap*this_map, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_map))
		return NULL;
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XRBTreeNode* nodes = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_map))[index], ((XMapBase*)this_map)->m_KeyLess, ((XMapBase*)this_map)->m_KeyEquality, XCompareRuleOne_XMap, pvKey);
	if (nodes == NULL)
		return NULL;
	XPair* pair = XTreeNode_GetData(nodes, XPair*);
	return pair;

	/*XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[index];

	while (current)
	{
		if (this_map->m_parent.m_KeyEquality(XPair_first(current->pair), pvKey))
		{
			return current->pair;
		}
		current = current->next;
	}*/
	return NULL;
}

XVector* VXMapBase_keys(const XMapBase* this_map)
{
	XVector* v=XVector_create(this_map->m_keyTypeSize);
	XVector_resize_base(v,XMapBase_size_base(this_map));
	XVector_clear_base(v);

	XContainerSetDataCopyMethod(v, XMapBaseKeyCopyMethod(this_map));
	XContainerSetDataMoveMethod(v, XMapBaseKeyMoveMethod(this_map));
	XContainerSetDataDeinitMethod(v, XMapBaseKeyDeinitMethod(this_map));

	for_each_iterator(this_map, XHashMap, it)
	{
		XVector_push_back_base(v,XPair_first(XHashMap_iterator_data(&it)));
	}
	return v;
}

void VXMap_clear(XHashMap*this_map)
{
	if (XHashMap_isEmpty_base(this_map))
		return;
	//XHashNode* deleteNode = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_map); i++)
	{
		XRBTreeNode* root= ((XRBTreeNode**)XContainerDataPtr(this_map))[i];
		XTree_delete(root, XMapBase_deleteNodeData, this_map);
		//XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[i];

		/*while (current)
		{
			deleteNode = current;
			current = current->next;
			if (XContainerDataDeinitMethod(this_map) != NULL)
				XContainerDataDeinitMethod(this_map)(deleteNode->pair);
			XPair_delete(deleteNode->pair);
			XMemory_free(deleteNode);
		}*/
	}
	memset(XContainerDataPtr(this_map),0, sizeof(XRBTreeNode*) * XContainerCapacity(this_map));
	XContainerSize(this_map)=0;
}

void VXClass_copy(XHashMap* object, const XHashMap* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XMapBase* map = src;
		XHashMap_init(object, map->m_keyTypeSize, XContainerTypeSize(src), src->m_hash, map->m_KeyEquality, map->m_KeyLess);
	}
	else if (!XHashMap_isEmpty_base(object))
	{
		XHashMap_clear_base(object);
	}
	XMapBaseSetKeyCopyMethod(object, XMapBaseKeyCopyMethod(src));
	XMapBaseSetKeyMoveMethod(object, XMapBaseKeyMoveMethod(src));
	XMapBaseSetKeyDeinitMethod(object, XMapBaseKeyDeinitMethod(src));

	XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
	XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
	XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
	for_each_iterator(src, XHashMap, it)
	{
		XPair* pair = XHashMap_iterator_data(&it);
		XMapBase_insert_base (object,XPair_first(pair),XPair_second(pair));
	}
}

void VXClass_move(XHashMap* object, XHashMap* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XMapBase* map = src;
		XHashMap_init(object, map->m_keyTypeSize ,XContainerTypeSize(src),src->m_hash,map->m_KeyEquality,map->m_KeyLess);
	}
	else if (!XMapBase_isEmpty_base(object))
	{
		XMapBase_clear_base(object);
	}
	XSwap(object,src, sizeof(XHashMap));
}

void VXMap_deinit(XHashMap*this_map)
{

	XHashMap_clear_base(this_map);
	void* data = XContainerDataPtr(this_map);
	if (data)
	{
		XMemory_free(data);
		XContainerDataPtr(this_map) = NULL;
	}
	//XMemory_free(this_map);
}

void VXMap_swap(XHashMap*this_mapOne, XHashMap*this_mapTwo)
{
	XSwap(this_mapOne, this_mapTwo,sizeof(XHashMap));
	////调用父类交换一部分
	//XVtableGetFunc(XContainerObject_class_init(), EXContainerObject_Swap, void(*)(XHashMap*, XHashMap*))(this_mapOne, this_mapTwo);
	//XSwap(&(this_mapOne->m_parent.m_KeyEquality), &(this_mapTwo->m_parent.m_KeyEquality), sizeof(XEquality));
	//XSwap(&(this_mapOne->m_parent.m_keyTypeSize), &(this_mapTwo->m_parent.m_keyTypeSize), sizeof(size_t));
	//XSwap(&(this_mapOne->m_hash), &(this_mapTwo->m_hash), sizeof(XHashFunc));
}
#endif