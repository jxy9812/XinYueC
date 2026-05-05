#include "XSet.h"
#if XSet_ON
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>

//Set插入数据
static bool VXSet_insert(XSet* this_set, const void* key, XCDataCreatMethod dataCreatMethod);
static void VXSet_erase(XSet* this_set, const XSet_iterator* it, XSet_iterator* next);
//map删除数据
static bool VXSet_remove(XSet* this_set, const void* key);
//查找数据，返回找到的XPair地址，没有返回NULL
static bool VXSet_find(XSet* this_set, const void* key, XSet_iterator* it);
//返回key数组
static XVector* VXSetBase_keys(const XSetBase* this_set);
//清空Set，释放内存
static void VXSet_clear(XSet* this_set);
static void VXClass_copy(XSet* object, const XSet* src);
static void VXClass_move(XSet* object, XSet* src);
static void VXSet_deinit(XSet* this_set);
//static void VXSet_swap(XSet* this_setOne, XSet* this_setTwo);
XVtable* XSet_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XSET_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = {
		VXSet_insert,VXSet_erase,VXSet_remove,VXSet_find,
		VXSetBase_keys
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXSet_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSet_deinit);
	//XVTABLE_OVERLOAD_DEFAULT(EXContainer_Swap, VXSet_swap);
#if SHOWCONTAINERSIZE
	printf("XSet size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}
XVector* VXSetBase_keys(const XSetBase* this_set)
{
	XVector* v = XVector_create(XContainerTypeSize(this_set));
	for_each_iterator(this_set, XSet, it)
	{
		XVector_push_back_base(v, XSet_iterator_data(&it));
	}
	return v;
}
static void XSet_freeNodeData(void* key, XSet* this_set)
{
	if (XContainerDataDeinitMethod(this_set) != NULL)
		XContainerDataDeinitMethod(this_set)(key);
}
void VXSet_clear(XSet* this_set)
{
	if (XSet_isEmpty_base(this_set))
		return;
	XTree_delete(XContainerDataPtr(this_set), XSet_freeNodeData, this_set);
	XContainerCapacity(this_set) = 0;
	XContainerSize(this_set) = 0;
	XContainerDataPtr(this_set) = NULL;
}

void VXClass_copy(XSet* object, const XSet* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XSet_init(object, XContainerTypeSize(src), XContainerCompare(src));
	}
	else if (!XSet_isEmpty_base(object))
	{
		XSet_clear_base(object);
	}
	for_each_iterator(src, XSet, it)
	{
		XSet_insert_base(object, XSet_iterator_data(&it));
	}
}

void VXClass_move(XSet* object, XSet* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XSet_init(object, XContainerTypeSize(src), XContainerCompare(src));
	}
	else if (!XSet_isEmpty_base(object))
	{
		XSet_clear_base(object);
	}
	XSwap(object, src, sizeof(XSet));
}

void VXSet_deinit(XSet* this_set)
{
	XSet_clear_base(this_set);
	//XFree_System(this_set);
}

bool VXSet_insert(XSet* this_set, const void* pvKey, XCDataCreatMethod dataCreatMethod)
{
	//if (ISNULL(this_set, "") || ISNULL(pvKey, "") )
	//	return false;
	if (!XSetBase_contains(this_set, pvKey))//当前没有这个键值
	{
		if (dataCreatMethod)
		{
			void* temp = XCalloc_System(1,XContainerTypeSize(this_set));
			dataCreatMethod(temp, pvKey);
			XRBTree_insert(&XContainerDataPtr(this_set), XContainerCompare(this_set), XCompareRuleTwo_XSet, temp, XContainerTypeSize(this_set));
			XFree_System(temp);
		}
		else
		{
			XRBTree_insert(&XContainerDataPtr(this_set), XContainerCompare(this_set), XCompareRuleTwo_XSet, pvKey, XContainerTypeSize(this_set));
		}
		
		++XContainerCapacity(this_set);
		++XContainerSize(this_set);
	}
	return true;
}

void VXSet_erase(XSet* this_set, const XSet_iterator* it, XSet_iterator* next)
{
	// 检查参数有效性：容器为空、迭代器为空或迭代器已指向末尾
	if (XSet_iterator_isEnd(it))
	{
		if (next != NULL)
			*next = XSet_end(this_set);
		return;
	}

	// 保存当前节点的下一个节点（删除前先获取，避免删除后迭代器失效）
	XSet_iterator next_it = *it;
	XSet_iterator_add(this_set, &next_it);

	// 获取当前迭代器指向的红黑树节点
	XRBTreeNode* current_node = (XRBTreeNode*)it->node;
	if (!current_node)
	{
		if (next != NULL)
			*next = next_it;
		return;
	}

	// 获取当前节点存储的键值
	void* current_key = XBTreeNode_GetDataPtr(current_node);
	if (!current_key)
	{
		if (next != NULL)
			*next = next_it;
		return;
	}

	// 从红黑树中删除当前节点
	XRBTree_remove(
		&XContainerDataPtr(this_set),                   // 红黑树根节点地址
		((XContainer*)this_set)->m_compare,
		XCompareRuleOne_XSet,									// 比较规则
		current_key,										// 要删除的键值
		XContainerTypeSize(this_set),
		XSet_freeNodeData,								// 节点数据释放回调
		this_set											// 传递容器作为额外参数
	);

	// 更新容器大小信息
	--XContainerCapacity(this_set);
	--XContainerSize(this_set);

	// 设置下一个迭代器
	if (next != NULL)
		*next = next_it;
}

bool VXSet_remove(XSet* this_set, const void* pvKey)
{
	if (ISNULL(this_set, "") || ISNULL(pvKey, ""))
		return false;
	if (XSetBase_contains(this_set, pvKey))
	{
		if (XContainerDataDeinitMethod(this_set) != NULL)
			XContainerDataDeinitMethod(this_set)(pvKey);
		XRBTree_remove(&XContainerDataPtr(this_set), ((XContainer*)this_set)->m_compare, XCompareRuleOne_XSet, pvKey, XContainerTypeSize(this_set), XSet_freeNodeData, this_set);

		--XContainerCapacity(this_set);
		--XContainerSize(this_set);
		return true;
	}
	return false;
}

bool VXSet_find(XSet* this_set, const void* key, XSet_iterator* it)
{
	if (XSet_isEmpty_base(this_set))
	{
		if (it)
			*it = XSet_end(this_set);
		return false;
	}
	XTreeNode* node = XRBTree_findNode(XContainerDataPtr(this_set), ((XContainer*)this_set)->m_compare, XCompareRuleOne_XSet, key);
	if (node == NULL)
	{
		if (it)
			*it = XSet_end(this_set);
		return false;
	}
	if (it)
		it->node = node;
	return true;
}


XSet* XSet_create(const size_t keyTypeSize, XCompare compare)
{
	if (keyTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (compare == NULL)
	{
		printf("compare比较函数NULL");
		return NULL;
	}
	XSet* this_set = (XSet*)XMalloc_System(sizeof(XSet));
	XSet_init(this_set, keyTypeSize, compare);
	Set_Class_MemoryFree(this_set, XFree_System);
	return this_set;
}

void XSet_init(XSet* this_set, const size_t keyTypeSize, XCompare compare)
{
	if (ISNULL(this_set, ""))
		return NULL;
	if (keyTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (compare == NULL)
	{
		printf("compare比较函数NULL");
		return NULL;
	}
	XSetBase_init(this_set, keyTypeSize, compare);
	XClassGetVtable(this_set) = XSet_class_init();
}

#endif