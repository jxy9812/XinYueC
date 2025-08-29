#include"XHashMap.h"
#if XHashMap_ON
#include"XAlgorithm.h"
#include"XVector.h"
#include"XRedBlackTree.h"
#include<string.h>
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
static void VXMap_swap(XHashMap*this_hashOne, XHashMap*this_hashTwo);
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
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find,
		VXMapBase_keys,VXMapBase_values
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
bool VXMap_insert(XHashMap* this_hash, const void* pvKey, const void* pvValue, XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod)
{
	if ((double)XContainerSize(this_hash) / XContainerCapacity(this_hash) >= DEFAULT_LOAD_FACTOR)
	{
		//printf("XHash 扩容\n");
		size_t new_capacity = XContainerCapacity(this_hash) * 2;
		if (!XHashMap_resize(this_hash, new_capacity))
		{
			printf("XHash 扩容失败\n");
			return false;
		}
	}

	size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
	XHashMap_iterator it;
	XPair* pair = NULL;
	if (!XHashMap_find_base(this_hash, pvKey, &it))
	{//节点不存在
		pair = XPair_create(((XMapBase*)this_hash)->m_keyTypeSize, XContainerTypeSize(this_hash));
		if (keyCreatMethod)
			keyCreatMethod(XPair_first(pair), pvKey);
		else
			XPair_insertFirst(pair, pvKey);
		if (dataCreatMethod)
			dataCreatMethod(XPair_second(pair), pvValue);
		else
			XPair_insertSecond(pair, pvValue);
		XRBTree_insert(((XRBTreeNode**)XContainerDataPtr(this_hash)) + index, ((XMapBase*)this_hash)->m_KeyLess, XCompareRuleTwo_XMap, &pair, sizeof(XPair*));
		//++XContainerCapacity(this_hash);
		++XContainerSize(this_hash);
	}
	else
	{
		pair = XHashMap_iterator_data(&it);
		if (XContainerDataCopyMethod(this_hash))
		{
			keyCreatMethod(XPair_second(pair), pvValue);
		}
		else
		{
			if (XContainerDataDeinitMethod(this_hash) != NULL)
				XContainerDataDeinitMethod(this_hash)(pair);
			XPair_insert(pair, pvKey, pvValue);
		}

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

	// 预存下一个迭代器（删除当前节点前先获取）
	XHashMap_iterator next_it = *it;
	XHashMap_iterator_add(this_hash, &next_it);

	// 获取当前节点数据
	XRBTreeNode* current_node = (XRBTreeNode*)it->node;
	XPair* current_pair = XBTreeNode_GetData(current_node, XPair*);
	if (ISNULL(current_pair, ""))
	{
		if (next != NULL)
			*next = next_it;
		return;
	}

	// 从哈希表的对应红黑树中删除节点
	XRBTree_remove(
		&((XRBTreeNode**)XContainerDataPtr(this_hash))[it->index],  // 对应桶的红黑树根节点地址
		((XMapBase*)this_hash)->m_KeyLess,                          // 键比较函数
		((XMapBase*)this_hash)->m_KeyEquality,                      // 键相等判断函数
		XCompareRuleOne_XMap,                                       // 比较规则
		XPair_first(current_pair),                                 // 要删除的键
		XMapBase_deleteNodeData,                                   // 节点数据释放回调
		this_hash                                                   // 传递容器作为额外参数
	);

	// 更新容器大小
	--XContainerSize(this_hash);

	// 设置下一个迭代器
	if (next != NULL)
		*next = next_it;
}

bool VXMap_remove(XHashMap*this_hash, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_hash))
		return false;
	size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
	XRBTreeNode* nodes = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_hash))[index], ((XMapBase*)this_hash)->m_KeyLess, ((XMapBase*)this_hash)->m_KeyEquality, XCompareRuleOne_XMap, pvKey);
	if (nodes != NULL)
	{
		XRBTree_remove(((XRBTreeNode**)XContainerDataPtr(this_hash)) + index, ((XMapBase*)this_hash)->m_KeyLess, ((XMapBase*)this_hash)->m_KeyEquality, XCompareRuleOne_XMap, pvKey, XMapBase_deleteNodeData,this_hash);
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
	XRBTreeNode* nodes = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_hash))[index], ((XMapBase*)this_hash)->m_KeyLess, ((XMapBase*)this_hash)->m_KeyEquality, XCompareRuleOne_XMap, pvKey);
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
	//XHashNode* deleteNode = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_hash); i++)
	{
		XRBTreeNode* root= ((XRBTreeNode**)XContainerDataPtr(this_hash))[i];
		XTree_delete(root, XMapBase_deleteNodeData, this_hash);
	}
	memset(XContainerDataPtr(this_hash),0, sizeof(XRBTreeNode*) * XContainerCapacity(this_hash));
	XContainerSize(this_hash)=0;
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

void VXMap_deinit(XHashMap*this_hash)
{

	XHashMap_clear_base(this_hash);
	void* data = XContainerDataPtr(this_hash);
	if (data)
	{
		XMemory_free(data);
		XContainerDataPtr(this_hash) = NULL;
	}
	//XMemory_free(this_hash);
}

void VXMap_swap(XHashMap*this_hashOne, XHashMap*this_hashTwo)
{
	XSwap(this_hashOne, this_hashTwo,sizeof(XHashMap));
	////调用父类交换一部分
	//XVtableGetFunc(XContainerObject_class_init(), EXContainerObject_Swap, void(*)(XHashMap*, XHashMap*))(this_hashOne, this_hashTwo);
	//XSwap(&(this_hashOne->m_parent.m_KeyEquality), &(this_hashTwo->m_parent.m_KeyEquality), sizeof(XEquality));
	//XSwap(&(this_hashOne->m_parent.m_keyTypeSize), &(this_hashTwo->m_parent.m_keyTypeSize), sizeof(size_t));
	//XSwap(&(this_hashOne->m_hash), &(this_hashTwo->m_hash), sizeof(XHashFunc));
}
#endif