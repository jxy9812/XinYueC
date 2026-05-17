#include"XHashMap.h"
#if XHashMap_ON
#include"XAlgorithm.h"
#include"XVector.h"
#include"XRedBlackTree.h"
#include<string.h>

// COW分离：如果数据被共享，创建独立副本（深拷贝红黑树）
static bool VXHashMapDetachIfNeeded(XHashMap* this_hash);
static void VXHashMapDataDelete(void* data, XHashMap* this_hash);

//Map插入数据
static bool VXMap_insert(XHashMap* this_hash, const void* pvKey, const void* pvValue, XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod);
static void VXMap_erase(XHashMap* this_hash, const XHashMap_iterator* it, XHashMap_iterator* next);
//map删除数据
static bool VXMap_remove(XHashMap*this_hash, const void* pvKey);
//根据键值返回数据地址
static void* VXMap_value(XHashMap*this_hash, const void* pvKey);
//查找数据，返回找到的XPair地址，没有返回NULL
static bool VXMap_find(XHashMap*this_hash, const void* pvKey,XHashMap_iterator* it);
//返回key数组
static XVector* VXMapBase_keys(const XMapBase* this_hash);
static XVector* VXMapBase_values(const XMapBase* this_hash);
//清空Map，释放内存
static void VXMap_clear(XHashMap*this_hash);
static void VXClass_copy(XHashMap* object, const XHashMap* src);
static void VXClass_move(XHashMap* object, XHashMap* src);
static void VXMap_deinit(XHashMap*this_hash);
//static void VXMap_swap(XHashMap*this_hashOne, XHashMap*this_hashTwo);
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
		XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = {
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find,
		VXMapBase_keys,VXMapBase_values
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXMap_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMap_deinit);
	//XVTABLE_OVERLOAD_DEFAULT(EXContainer_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XHash size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
	}

// COW分离：如果数据被共享，创建独立副本（深拷贝红黑树）
static bool VXHashMapDetachIfNeeded(XHashMap* this_hash)
{
	if (!XContainerSharedData(this_hash) || !XSharedData_isShared(XContainerSharedData(this_hash)))
		return true; // 不共享，无需分离

	size_t capacity = XContainerCapacity(this_hash);
	size_t typeSize = XContainerTypeSize(this_hash);
	size_t keyTypeSize = ((XMapBase*)this_hash)->m_keyTypeSize;

	// 分配新的桶数组
		size_t newSize = capacity * sizeof(XRBTreeNode*);
	XSharedData* newShared = XSharedData_create(NULL, newSize);
	if (!newShared)
		return false;
	XRBTreeNode** newData = (XRBTreeNode**)newShared->data;
	memset(newData, 0, newSize);

	XRBTreeNode** oldData = (XRBTreeNode**)XContainerSharedDataPtr(this_hash);

	// 深拷贝每个桶的红黑树
	for (size_t i = 0; i < capacity; i++)
	{
		XRBTreeNode* root = oldData[i];
		if (root != NULL)
		{
			// 遍历红黑树，拷贝节点到新树
			XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
						if (!nodes)
			{
				XSharedData_release(newShared);
				return false;
			}
			XBTree_TraversingToXVector(root, XBTreePreorder, nodes);
		
			for (size_t j = 0; j < XVector_size_base(nodes); j++)
			{
				XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[j];
				XPair* oldPair = XBTreeNode_GetDataPtr(oldNode);
			
				// 创建新节点
				XRBTreeNode* newNode = XRBTree_create(NULL, XMapBasePairTypeSize(this_hash));
				if (!newNode)
				{
					XVector_delete_base(nodes);
					XSharedData_release(newShared);
					return false;
				}
			
				XPair* newPair = XBTreeNode_GetDataPtr(newNode);
				// 拷贝 key
				if (XMapBaseKeyCopyMethod(this_hash))
					XMapBaseKeyCopyMethod(this_hash)(XPair_first(newPair), XPair_first(oldPair));
				else
					memcpy(XPair_first(newPair), XPair_first(oldPair), keyTypeSize);
				// 拷贝 value
				if (XContainerDataCopyMethod(this_hash))
					XContainerDataCopyMethod(this_hash)(XPair_second(newPair), XPair_second(oldPair));
				else
					memcpy(XPair_second(newPair), XPair_second(oldPair), typeSize);
			
				// 插入到新红黑树
				XRBTree_SetRed(newNode);
				memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
				((XTreeNode*)newNode)->parentNode = NULL;
				XRBTree_insertNode(&newData[i], XContainerCompare(this_hash), XCompareRuleTwo_XMap, newNode);
			}
			XVector_delete_base(nodes);
		}
		}

	// 减少旧引用，设置新引用
	XSharedData_release(XContainerSharedData(this_hash));
	XContainerSharedData(this_hash) = newShared;
	return true;
}

// 删除HashMap数据
static void VXHashMapDataDelete(void* data, XHashMap* this_hash)
{
	if (this_hash == NULL)
		return;

	XRBTreeNode** buckets = (XRBTreeNode**)data;
	size_t capacity = XContainerCapacity(this_hash);

	// 删除每个桶的红黑树
	for (size_t i = 0; i < capacity; i++)
	{
		if (buckets[i] != NULL)
		{
			XTree_delete(buckets[i], XMapBase_deleteNodeData, this_hash);
		}
	}

	// data 不需要单独释放，XSharedData 的柔性数组会一起释放
	XContainerSize(this_hash) = 0;
	XContainerCapacity(this_hash) = 0;
	XContainerSharedData(this_hash) = NULL;
}

// 私有函数：扩容哈希表
static bool XHashMap_resize(XHashMap* map, size_t new_capacity)
{
	size_t new_size = new_capacity * sizeof(XRBTreeNode*);
	XSharedData* newShared = XSharedData_create(NULL, new_size);
	if (!newShared)
		return false;
	XRBTreeNode** newData = (XRBTreeNode**)newShared->data;
	memset(newData, 0, new_size);

	// 获取原数据
	XSharedData* oldSD = XContainerSharedData(map);
	XRBTreeNode** oldData = oldSD ? (XRBTreeNode**)oldSD->data : NULL;

	// 遍历原哈希表
	for (size_t i = 0; i < XContainerCapacity(map); i++)
	{
		XRBTreeNode* root = oldData ? oldData[i] : NULL;
		if (root != NULL)
		{
			// 遍历红黑树，将节点插入到新哈希表中
			XVector* nodes = XVector_create(sizeof(struct XTreeNode*));
			XVector_resize_base(nodes, XContainerSize(map));
			XBTree_TraversingToXVector(root, XBTreePreorder, nodes);
			if (nodes != NULL)
			{
				for (size_t j = 0; j < XVector_size_base(nodes); j++)
				{
					XRBTreeNode* node = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[j];
					XPair* pair = XBTreeNode_GetDataPtr(node);
					size_t index = map->m_hash(XPair_first(pair), ((XMapBase*)map)->m_keyTypeSize) % new_capacity;

					//初始化节点信息当新节点直接插入
					XRBTree_SetRed(node);
					memset(XTreeNode_GetNodes(node), 0, sizeof(XTreeNode*) * ((XTreeNode*)node)->nodeCount);
					((XTreeNode*)node)->parentNode = NULL;
					XRBTree_insertNode(&newData[index], XContainerCompare(map), XCompareRuleTwo_XMap, node);
				}
				XVector_delete_base(nodes);
			}
		}
	}

	// 释放原共享数据
	if (oldSD)
	{
		// 注意：由于 VXMap_insert 已调用 VXHashMapDetachIfNeeded，
		// 这里数据应该不被共享。但为了安全，仍需正确处理。
		// 如果被共享，只减少引用计数；如果不被共享，释放数据。
				XSharedData_release(oldSD);
	}

	XContainerSharedData(map) = newShared;
	XContainerCapacity(map) = new_capacity;
	return true;
}
bool VXMap_insert(XHashMap* this_hash, const void* pvKey, const void* pvValue, XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod)
{
	// COW分离
	if (!VXHashMapDetachIfNeeded(this_hash))
		return false;
	if(!XContainerSharedData(this_hash))
	{
		XContainerCapacity(this_hash) = DEFAULT_CAPACITY;

		size_t size = sizeof(void*) * XContainerCapacity(this_hash);

		// 创建 XSharedData（一次分配）
		XSharedData* sd = XSharedData_create(NULL, size);
		if (sd == NULL)
		{
			//XFree_System(this_hash);
			return false;
		}
		memset(sd->data, 0, size);
		XContainerSharedData(this_hash) = sd;
	}
	else if ((double)XContainerSize(this_hash) / XContainerCapacity(this_hash) >= DEFAULT_LOAD_FACTOR)
	{
		//printf("XHash 扩容\n");
		size_t new_capacity = XContainerCapacity(this_hash) * 2;
		if (!XHashMap_resize(this_hash, new_capacity))
		{
			//printf("XHash 扩容失败\n");
			return false;
		}
	}

	size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
	XHashMap_iterator it;
	XPair* pair = NULL;
	if (!XHashMap_find_base(this_hash, pvKey, &it))
	{//节点不存在
		pair = XMapBasePairBuffer(this_hash);
		XPair_init(pair, ((XMapBase*)this_hash)->m_keyTypeSize, XContainerTypeSize(this_hash));
		if (keyCreatMethod)
			keyCreatMethod(XPair_first(pair), pvKey);
		else
			XPair_insertFirst(pair, pvKey);
		if (dataCreatMethod)
			dataCreatMethod(XPair_second(pair), pvValue);
		else
			XPair_insertSecond(pair, pvValue);

		XRBTreeNode* inserted_node = XRBTree_insert(
			&((XRBTreeNode**)XContainerSharedDataPtr(this_hash))[index],
			XContainerCompare(this_hash),
			XCompareRuleTwo_XMap,
			pair,
			XMapBasePairTypeSize(this_hash)
		);
		if (inserted_node == NULL)
		{
			// 红黑树插入失败！必须释放之前分配的 pair
			XMapBase_deleteNodeData(&pair, this_hash);
			return false; // 返回失败
		}
		++XContainerSize(this_hash);
	}
	else
	{
		pair = XHashMap_iterator_data(&it);
		if(XMapBaseKeyDeinitMethod(this_hash))
			XMapBaseKeyDeinitMethod(this_hash)(XPair_first(pair));
		if (keyCreatMethod)
			keyCreatMethod(XPair_first(pair), pvKey);
		else
			XPair_insertFirst(pair, pvKey);
		
		if (XContainerDataDeinitMethod(this_hash))
			XContainerDataDeinitMethod(this_hash)(XPair_second(pair));

		if (dataCreatMethod)
			dataCreatMethod(XPair_second(pair), pvValue);
		else
			XPair_insertSecond(pair, pvValue);
	}
	return true;
}

void VXMap_erase(XHashMap* this_hash, const XHashMap_iterator* it, XHashMap_iterator* next)
{
	// 检查参数有效性
	if (ISNULL(this_hash, "") || ISNULL(it, "") ||
		XHashMap_iterator_isEnd((XHashMap_iterator*)it))
	{
		if (next != NULL)
			*next = XHashMap_end(this_hash);
		return;
	}

	// COW分离
	if (!VXHashMapDetachIfNeeded(this_hash))
	{
		if (next != NULL)
			*next = XHashMap_end(this_hash);
		return;
	}

	// 预存下一个迭代器（删除当前节点前先获取）
	XHashMap_iterator next_it = *it;
	XHashMap_iterator_add(this_hash, &next_it);

	// 获取当前节点数据
	XRBTreeNode* current_node = (XRBTreeNode*)it->node;
	if (!current_node)
	{
		if (next != NULL)
			*next = next_it;
		return;
	}
	
	// 从哈希表的对应红黑树中删除节点
	XRBTreeNode* removeNode=XRBTree_removeNode(
		&((XRBTreeNode**)XContainerSharedDataPtr(this_hash))[it->index],  // 对应桶的红黑树根节点地址
		current_node,                                 // 要删除的键
		XMapBasePairTypeSize(this_hash)
	);
	if(removeNode)
	{
		XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(current_node), this_hash);
		XRBTreeNode_delete(current_node);
		// 更新容器大小
		--XContainerSize(this_hash);
	}
	if (next != NULL)
		*next = next_it;
}

bool VXMap_remove(XHashMap*this_hash, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_hash))
		return false;
	
	// COW分离
	if (!VXHashMapDetachIfNeeded(this_hash))
		return false;
	size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
	XRBTreeNode* removeNode = XRBTree_remove(((XRBTreeNode**)XContainerSharedDataPtr(this_hash)) + index, ((XContainer*)this_hash)->m_compare, XCompareRuleOne_XMap, pvKey, XMapBasePairTypeSize(this_hash));
	if (removeNode != NULL)
	{
		XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(removeNode),this_hash);
		XRBTreeNode_delete(removeNode);
		--XContainerSize(this_hash);
		return true;
	}
	return false;
}

void* VXMap_value(XHashMap*this_hash, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_hash))
		return NULL;
	//size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);

	XHashMap_iterator it;
	XPair* pair = NULL;
	if(XHashMap_find_base(this_hash, pvKey, &it))
		pair = XHashMap_iterator_data(&it);
	if (pair)
		return XPair_second(pair);
	return NULL;
}

bool VXMap_find(XHashMap*this_hash, const void* pvKey, XHashMap_iterator* it)
{
	if (XMapBase_isEmpty_base(this_hash))
	{
		if (it)
			*it = XHashMap_end(this_hash);
		return false;
	}
	size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
	XRBTreeNode* nodes = XRBTree_findNode(((XRBTreeNode**)XContainerSharedDataPtr(this_hash))[index], ((XContainer*)this_hash)->m_compare, XCompareRuleOne_XMap, pvKey);
	if (nodes == NULL)
	{
		if (it)
			*it = XHashMap_end(this_hash);
		return false;
	}
	if (it)
	{
		it->node = nodes;
		it->index = index;
	}
	return true;
}

XVector* VXMapBase_keys(const XMapBase* this_hash)
{
	XVector* v=XVector_create(this_hash->m_keyTypeSize);
	XVector_resize_base(v,XMapBase_size_base(this_hash));
	XVector_clear_base(v);

	XContainerSetDataCopyMethod(v, XMapBaseKeyCopyMethod(this_hash));
	XContainerSetDataMoveMethod(v, XMapBaseKeyMoveMethod(this_hash));
	XContainerSetDataDeinitMethod(v, XMapBaseKeyDeinitMethod(this_hash));

	for_each_iterator(this_hash, XHashMap, it)
	{
		XVector_push_back_base(v,XPair_first(XHashMap_iterator_data(&it)));
	}
	return v;
}
XVector* VXMapBase_values(const XMapBase* this_hash)
{
	XVector* v = XVector_create(XContainerTypeSize(this_hash));
	XVector_resize_base(v, XMapBase_size_base(this_hash));
	XVector_clear_base(v);

	XContainerSetDataCopyMethod(v, XContainerDataCopyMethod(this_hash));
	XContainerSetDataMoveMethod(v, XContainerDataMoveMethod(this_hash));
	XContainerSetDataDeinitMethod(v, XContainerDataDeinitMethod(this_hash));

	for_each_iterator(this_hash, XHashMap, it)
	{
		XVector_push_back_base(v, XPair_second(XMap_iterator_data(&it)));
	}
	return v;
}
void VXMap_clear(XHashMap*this_hash)
{
	if (XHashMap_isEmpty_base(this_hash))
		return;
	
	// 如果数据被共享，减少引用并创建空数据
	if (XContainerSharedData(this_hash) && XSharedData_isShared(XContainerSharedData(this_hash)))
	{
		XSharedData_release(XContainerSharedData(this_hash));
		XContainerSharedData(this_hash) = NULL;
		XContainerCapacity(this_hash) = 0;
		XContainerSize(this_hash) = 0;
		return;
	}
	
	// 不共享，直接删除数据
	for (size_t i = 0; i < XContainerCapacity(this_hash); i++)
	{
		XRBTreeNode* root= ((XRBTreeNode**)XContainerSharedDataPtr(this_hash))[i];
		XTree_delete(root, XMapBase_deleteNodeData, this_hash);
	}
	memset(XContainerSharedDataPtr(this_hash),0, sizeof(XRBTreeNode*) * XContainerCapacity(this_hash));
	XContainerSize(this_hash)=0;
}

void VXClass_copy(XHashMap* object, const XHashMap* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XMapBase* map = src;
		XHashMap_init(object, map->m_keyTypeSize, XContainerTypeSize(src), src->m_hash, XContainerCompare(map));
	}
	else if (XContainerSharedData(object))// 释放目标原有数据
	{
		XSharedData_release_with(XContainerSharedData(object), VXHashMapDataDelete, object);
	}
	
	// 复制回调函数
	XMapBaseSetKeyCopyMethod(object, XMapBaseKeyCopyMethod(src));
	XMapBaseSetKeyMoveMethod(object, XMapBaseKeyMoveMethod(src));
	XMapBaseSetKeyDeinitMethod(object, XMapBaseKeyDeinitMethod(src));

	XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
	XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
	XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

		// 共享源数据的 XSharedData（COW 机制）
	XContainerSharedData(object) = XContainerSharedData(src);
	if (XContainerSharedData(object))
	{
		XSharedData_addRef(XContainerSharedData(object));
	}
	
	XContainerSize(object) = XContainerSize(src);
	XContainerCapacity(object) = XContainerCapacity(src);
}

void VXClass_move(XHashMap* object, XHashMap* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XMapBase* map = src;
		XHashMap_init(object, map->m_keyTypeSize, XContainerTypeSize(src), src->m_hash, XContainerCompare(map));
	}
	else if (XContainerSharedData(object))// 释放目标原有数据
	{
		XSharedData_release_with(XContainerSharedData(object), VXHashMapDataDelete, object);
	}
	
		// 转移所有权
	XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XHashMap) - sizeof(XClass));
	
	// 清空源对象的共享数据指针
	XContainerSharedData(src) = NULL;
	XContainerCapacity(src) = 0;
	XContainerSize(src) = 0;
}

void VXMap_deinit(XHashMap*this_hash)
{
	//XHashMap_clear_base(this_hash);
	if(XContainerSharedData(this_hash))
	{
		XSharedData_release_with(XContainerSharedData(this_hash), VXHashMapDataDelete, this_hash);
		XContainerSize(this_hash) = 0;
		XContainerCapacity(this_hash) = 0;
		XContainerSharedData(this_hash) = NULL;
	}

	if (XMapBasePairBuffer(this_hash))
	{
		XPair_delete(XMapBasePairBuffer(this_hash));
		XMapBasePairBuffer(this_hash) = NULL;
	}
}

#endif
