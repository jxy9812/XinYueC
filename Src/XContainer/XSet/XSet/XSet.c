#include "XSet.h"
#if XSet_ON
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>

// COW分离：如果数据被共享，创建独立副本（深拷贝红黑树）
static bool VXSetDetachIfNeeded(XSet* this_set);
static void VXSetDataDelete(void* data, XSet* this_set);

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
// COW分离：如果数据被共享，创建独立副本（深拷贝红黑树）
static bool VXSetDetachIfNeeded(XSet* this_set)
{
	if (!XContainerSharedData(this_set) || !XSharedData_isShared(XContainerSharedData(this_set)))
		return true; // 不共享，无需分离
	size_t typeSize = XContainerTypeSize(this_set);
	// 获取旧红黑树根节点
	XRBTreeNode* oldRoot = (XRBTreeNode*)XContainerDataPtr(this_set);
	if (oldRoot == NULL)
		return true;
	// 创建新红黑树（深拷贝）
	XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
	if (!nodes)
		return false;
	XBTree_TraversingToXVector(oldRoot, XBTreePreorder, nodes);
	XRBTreeNode* newRoot = NULL;
	for (size_t i = 0; i < XVector_size_base(nodes); i++)
	{
		XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerDataPtr(nodes))[i];
		void* oldData = XBTreeNode_GetDataPtr(oldNode);
		// 创建新节点
		XRBTreeNode* newNode = XRBTree_create(NULL, typeSize);
		if (!newNode)
		{
			XVector_delete_base(nodes);
			return false;
		}
		void* newData = XBTreeNode_GetDataPtr(newNode);
		// 拷贝数据
		if (XContainerDataCopyMethod(this_set))
			XContainerDataCopyMethod(this_set)(newData, oldData);
		else
			memcpy(newData, oldData, typeSize);
		// 插入到新红黑树
		XRBTree_SetRed(newNode);
		memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
		((XTreeNode*)newNode)->parentNode = NULL;
		XRBTree_insertNode(&newRoot, XContainerCompare(this_set), XCompareRuleTwo_XSet, newNode);
	}
	XVector_delete_base(nodes);
	// 创建新的 XSharedData
	XSharedData* newShared = XSharedData_create(newRoot);
	if (!newShared)
	{
		// 释放已创建的新树
		XTree_delete(newRoot, NULL, NULL);
		return false;
	}
	// 减少旧引用，设置新引用
	XSharedData_release(XContainerSharedData(this_set));
	XContainerSharedData(this_set) = newShared;
	return true;
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
static void XSet_deleteNodeData(void* key, XSet* this_set)
{
	if (XContainerDataDeinitMethod(this_set) != NULL)
		XContainerDataDeinitMethod(this_set)(key);
}
// 删除Set数据
static void VXSetDataDelete(void* data, XSet* this_set)
{
	if (data == NULL || this_set == NULL)
		return;
	XRBTreeNode* root = (XRBTreeNode*)data;
	XTree_delete(root, XSet_deleteNodeData, this_set);
	XContainerSize(this_set) = 0;
	XContainerCapacity(this_set) = 0;
	XContainerSharedData(this_set) = NULL;
}
void VXSet_clear(XSet* this_set)
{
	if (XSet_isEmpty_base(this_set))
		return;

	// 如果数据被共享，减少引用并创建空数据
	if (XContainerSharedData(this_set) && XSharedData_isShared(XContainerSharedData(this_set)))
	{
		XSharedData_release(XContainerSharedData(this_set));
		XContainerSharedData(this_set) = NULL;
		XContainerCapacity(this_set) = 0;
		XContainerSize(this_set) = 0;
		return;
	}

	// 不共享，直接删除数据
	XTree_delete(XContainerDataPtr(this_set), XSet_deleteNodeData, this_set);
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
	else if (XContainerSharedData(object))
	{
		XSharedData_release_with(XContainerSharedData(object), VXSetDataDelete, object);
	}

	// 复制回调函数
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

void VXClass_move(XSet* object, XSet* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XSet_init(object, XContainerTypeSize(src), XContainerCompare(src));
	}
	else if (XContainerSharedData(object))
	{
		XSharedData_release_with(XContainerSharedData(object), VXSetDataDelete, object);
	}

	// 转移所有权
	XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XSet) - sizeof(XClass));

	// 清空源对象的共享数据指针
	XContainerSharedData(src) = NULL;
	XContainerCapacity(src) = 0;
	XContainerSize(src) = 0;
}

void VXSet_deinit(XSet* this_set)
{
	XSharedData_release_with(XContainerSharedData(this_set), VXSetDataDelete, this_set);
	XContainerSize(this_set) = 0;
	XContainerCapacity(this_set) = 0;
	XContainerSharedData(this_set) = NULL;
}

bool VXSet_insert(XSet* this_set, const void* pvKey, XCDataCreatMethod dataCreatMethod)
{
	// COW分离
	if (!VXSetDetachIfNeeded(this_set))
		return false;

	if (!XSetBase_contains(this_set, pvKey))//当前没有这个键值
	{
		if(!XContainerSharedData(this_set))
			XContainerSharedData(this_set) = XSharedData_create(NULL);
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

	// COW分离
	if (!VXSetDetachIfNeeded(this_set))
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

	// 从红黑树中删除当前节点
	XRBTreeNode* removeNode = XRBTree_removeNode(
		&XContainerDataPtr(this_set),                   // 红黑树根节点地址
		current_node,										// 要删除的键值
		XContainerTypeSize(this_set)
	);
	if (removeNode)
	{
		XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
		XRBTreeNode_delete(removeNode);
		// 更新容器大小信息
		--XContainerCapacity(this_set);
		--XContainerSize(this_set);
	}

	// 设置下一个迭代器
	if (next != NULL)
		*next = next_it;
}

bool VXSet_remove(XSet* this_set, const void* pvKey)
{
	if (XSet_isEmpty_base(this_set))
		return false;

	// COW分离
	if (!VXSetDetachIfNeeded(this_set))
		return false;
	XRBTreeNode* removeNode = XRBTree_remove(&XContainerDataPtr(this_set), ((XContainer*)this_set)->m_compare, XCompareRuleOne_XSet, pvKey, XContainerTypeSize(this_set));
	if (removeNode != NULL)
	{
		XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
		XRBTreeNode_delete(removeNode);
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
	XClassSetVtable(this_set, XSet);
	
}

#endif