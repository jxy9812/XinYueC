#include"XMap.h"
#if XMap_ON
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//Map插入数据
static bool VXMap_insert(XMap* this_map, const void* pvKey, const void* pvValue, XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod);
static void VXMap_erase(XMap* this_map, const XMap_iterator* it, XMap_iterator* next);
//map删除数据
static bool VXMap_remove(XMap* this_map, const void* key);
//根据键值返回数据地址
static void* VXMap_value(XMap* this_map, const void* key);
//查找数据，返回找到的XPair地址，没有返回NULL
static bool VXMap_find(XMap* this_map, const void* key, XMap_iterator* it);
//返回key数组
static XVector* VXMapBase_keys(const XMapBase* this_map);
static XVector* VXMapBase_values(const XMapBase* this_map);
//清空Map，释放内存
static void VXMap_clear(XMap* this_map);
static void VXClass_copy(XMap* object, const XMap* src);
static void VXClass_move(XMap* object, XMap* src);
static void VXMap_deinit(XMap* this_map);
//static void VXMap_swap(XMap* this_mapOne, XMap* this_mapTwo);

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
	XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = {
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find,
		VXMapBase_keys,VXMapBase_values
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear,VXMap_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit,VXMap_deinit);
	//XVTABLE_OVERLOAD_DEFAULT(EXContainer_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XMap size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}
bool VXMap_insert(XMap* this_map, const void* pvKey, const void* pvValue, XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod)
{
	/*if (ISNULL(this_map, "")|| ISNULL(pvKey, "")||ISNULL(pvValue, ""))
		return false;*/
	XMap_iterator it;
	XPair* pair = NULL;
	if (!XMap_find_base(this_map, pvKey, &it))
	{
		//pair = XPair_create(((XMapBase*)this_map)->m_keyTypeSize, XContainerTypeSize(this_map));
		//if (!pair) {
		//	// XPair_create 失败，直接返回 false
		//	return false;
		//}
		pair = XMapBasePairBuffer(this_map);

		if (keyCreatMethod)
			keyCreatMethod(XPair_first(pair), pvKey);
		else
			XPair_insertFirst(pair, pvKey);
		if (dataCreatMethod)
			dataCreatMethod(XPair_second(pair), pvValue);
		else
			XPair_insertSecond(pair, pvValue);

		//printf("创建的xpair pvKey:%d pvValue:%s\n",XPair_First(pair,int),XPair_second(LPpair));

		XRBTreeNode* inserted_node = XRBTree_insert(&XContainerDataPtr(this_map), XContainerCompare(this_map), XCompareRuleTwo_XMap, pair,
			XMapBasePairTypeSize(this_map));
		if (inserted_node == NULL)
		{
			// 红黑树插入失败！必须释放之前分配的 pair
			XMapBase_deleteNodeData(&pair, this_map);
			return false; // 返回失败
		}
		++XContainerCapacity(this_map);
		++XContainerSize(this_map);
		//return true;
	}
	else//插入的已经存在键了
	{
		pair = XMap_iterator_data(&it);
		if (XMapBaseKeyDeinitMethod(this_map))
			XMapBaseKeyDeinitMethod(this_map)(XPair_first(pair));
		if (keyCreatMethod)
			keyCreatMethod(XPair_first(pair), pvKey);
		else
			XPair_insertFirst(pair, pvKey);
		// 释放旧的值
		if (XContainerDataDeinitMethod(this_map))
			XContainerDataDeinitMethod(this_map)(XPair_second(pair));

		if (dataCreatMethod)
			dataCreatMethod(XPair_second(pair), pvValue);
		else
			XPair_insertSecond(pair, pvValue);
	}
	return true;
}

void VXMap_erase(XMap* this_map, const XMap_iterator* it, XMap_iterator* next)
{
	// 检查参数有效性：容器为空、迭代器为空或迭代器已指向末尾
	if (ISNULL(this_map, "") || ISNULL(it, "") || XMap_iterator_isEnd((XMap_iterator*)it))
	{
		if (next != NULL)
			*next = XMap_end(this_map);
		return;
	}

	// 保存当前节点的下一个节点（删除前先获取，避免删除后迭代器失效）
	XMap_iterator next_it = *it;
	XMap_iterator_add(this_map, &next_it);

	// 获取当前迭代器指向的节点和键值对
	XRBTreeNode* current_node = (XRBTreeNode*)it->node;
	if (!current_node)
	{
		if (next != NULL)
			*next = next_it;
		return;
	}

	// 从红黑树中删除当前节点
	XRBTreeNode* removeNode = XRBTree_removeNode(
		&XContainerDataPtr(this_map),
		((XContainer*)this_map)->m_compare,
		XCompareRuleOne_XMap,
		current_node,
		XMapBasePairTypeSize(this_map)
	);
	if (removeNode)
	{
		XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_map);
		XRBTreeNode_delete(removeNode);
		// 更新容器大小信息
		--XContainerCapacity(this_map);
		--XContainerSize(this_map);
	}

	// 设置下一个迭代器
	if (next != NULL)
		*next = next_it;
}

bool VXMap_remove(XMap* this_map, const void* key)
{
	if (ISNULL(this_map, "") || ISNULL(key, ""))
		return false;
	XRBTreeNode* removeNode = XRBTree_remove(&XContainerDataPtr(this_map), ((XContainer*)this_map)->m_compare, XCompareRuleOne_XMap, key, XMapBasePairTypeSize(this_map));
	if (removeNode != NULL)
	{
		XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_map);
		XRBTreeNode_delete(removeNode);
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
	XMap_iterator it;
	XPair* pair = NULL;
	if (XMap_find_base(this_map, key, &it))
		pair = XMap_iterator_data(&it);
	if (pair)
		return XPair_second(pair);
	return NULL;
}

bool VXMap_find(XMap* this_map, const void* key, XMap_iterator* it)
{
	if (XMap_isEmpty_base(this_map))
	{
		if (it)
			*it = XMap_end(this_map);
		return false;
	}
	XTreeNode* nodes = XRBTree_findNode(XContainerDataPtr(this_map), ((XContainer*)this_map)->m_compare, XCompareRuleOne_XMap, key);
	if (nodes == NULL)
	{
		if (it)
			*it = XMap_end(this_map);
		return false;
	}
	if (it)
		it->node = nodes;
	return true;
}
XVector* VXMapBase_keys(const XMapBase* this_map)
{
	XVector* v = XVector_create(this_map->m_keyTypeSize);
	XVector_resize_base(v, XMapBase_size_base(this_map));
	XVector_clear_base(v);

	XContainerSetDataCopyMethod(v, XMapBaseKeyCopyMethod(this_map));
	XContainerSetDataMoveMethod(v, XMapBaseKeyMoveMethod(this_map));
	XContainerSetDataDeinitMethod(v, XMapBaseKeyDeinitMethod(this_map));

	for_each_iterator(this_map, XMap, it)
	{
		XVector_push_back_base(v, XPair_first(XMap_iterator_data(&it)));
	}
	return v;
}

XVector* VXMapBase_values(const XMapBase* this_map)
{
	XVector* v = XVector_create(XContainerTypeSize(this_map));
	XVector_resize_base(v, XMapBase_size_base(this_map));
	XVector_clear_base(v);

	XContainerSetDataCopyMethod(v, XContainerDataCopyMethod(this_map));
	XContainerSetDataMoveMethod(v, XContainerDataMoveMethod(this_map));
	XContainerSetDataDeinitMethod(v, XContainerDataDeinitMethod(this_map));

	for_each_iterator(this_map, XMap, it)
	{
		XVector_push_back_base(v, XPair_second(XMap_iterator_data(&it)));
	}
	return v;
}

void VXMap_clear(XMap* this_map)
{
	if (XMap_isEmpty_base(this_map))
		return;
	XTree_delete(XContainerDataPtr(this_map), XMapBase_deleteNodeData, this_map);
	XContainerCapacity(this_map) = 0;
	XContainerSize(this_map) = 0;
	XContainerDataPtr(this_map) = NULL;
}

void VXClass_copy(XMap* object, const XMap* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XMapBase* map = src;
		XMap_init(object, map->m_keyTypeSize, XContainerTypeSize(src), XContainerCompare(map));
	}
	else if (!XMap_isEmpty_base(object))
	{
		XMap_clear_base(object);
	}
	XMapBaseSetKeyCopyMethod(object, XMapBaseKeyCopyMethod(src));
	XMapBaseSetKeyMoveMethod(object, XMapBaseKeyMoveMethod(src));
	XMapBaseSetKeyDeinitMethod(object, XMapBaseKeyDeinitMethod(src));

	XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
	XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
	XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
	
	for_each_iterator(src, XMap, it)
	{
		XPair* pair = XMap_iterator_data(&it);
		XMapBase_insert_base(object, XPair_first(pair), XPair_second(pair));
	}
}

void VXClass_move(XMap* object, XMap* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XMapBase* map = src;
		XMap_init(object, map->m_keyTypeSize, XContainerTypeSize(src), XContainerCompare(map));
	}
	else if (!XMapBase_isEmpty_base(object))
	{
		XMapBase_clear_base(object);
	}
	XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XMap) - sizeof(XClass));
}

void VXMap_deinit(XMap* this_map)
{
	XMap_clear_base(this_map);
	if (XMapBasePairBuffer(this_map))
	{
		XPair_delete(XMapBasePairBuffer(this_map));
		XMapBasePairBuffer(this_map) = NULL;
	}
}


#endif