#include "XSet.h"
#if XSet_ON
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>

//Set插入数据
static bool VXSet_insert(XSet* this_set, const void* key);
static void VXSet_erase(XSet* this_set, const void* key);
//map删除数据
static bool VXSet_remove(XSet* this_set, const void* key);
//查找数据，返回找到的XPair地址，没有返回NULL
static bool VXSet_find(XSet* this_set, const void* key);
//返回key数组
static XVector* VXSetBase_keys(const XSetBase* this_set);
//清空Set，释放内存
static void VXSet_clear(XSet* this_set);
//释放内存
static void VXSet_delete(XSet* this_set);
static void VXSet_swap(XSet* this_setOne, XSet* this_setTwo);
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
		XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	void* table[] = {
		VXSet_insert,VXSet_erase,VXSet_remove,VXSet_find,
		VXSetBase_keys
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXSet_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXSet_delete);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXSet_swap);
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
	if (XContainerDataDeleteMethod(this_set) != NULL)
		XContainerDataDeleteMethod(this_set)(key);
}
void VXSet_clear(XSet* this_set)
{
	if (XSet_isEmpty_base(this_set))
		return;
	XBTree_delete(XContainerDataPtr(this_set), XSet_freeNodeData, this_set);
	XContainerCapacity(this_set) = 0;
	XContainerSize(this_set) = 0;
	XContainerDataPtr(this_set) = NULL;
}

void VXSet_delete(XSet* this_set)
{
	XSet_clear_base(this_set);
	XMemory_free(this_set);
}

void VXSet_swap(XSet* this_setOne, XSet* this_setTwo)
{
	XVtableGetFunc(XVector_class_init(), EXContainerObject_Swap, void (*)(XContainerObject*, XContainerObject*))(this_setOne, this_setTwo);
	XSwap(&((XSetBase*)this_setOne)->m_KeyEquality, &((XSetBase*)this_setTwo)->m_KeyEquality, sizeof(XEquality));
	XSwap(&((XSetBase*)this_setOne)->m_KeyLess, &((XSetBase*)this_setTwo)->m_KeyLess, sizeof(XLess));
	XSwap(&XContainerTypeSize(this_setOne), &XContainerTypeSize(this_setTwo), sizeof(size_t));
}
bool VXSet_insert(XSet* this_set, const void* key)
{
	if (ISNULL(this_set, "") || ISNULL(key, "") )
		return false;
	if (!XSetBase_contains(this_set, key))//当前没有这个键值
	{
		XRBTree_insert(&XContainerDataPtr(this_set), ((XSetBase*)this_set)->m_KeyLess, XCompareRuleTwo_XSet, key, XContainerTypeSize(this_set));
		++XContainerCapacity(this_set);
		++XContainerSize(this_set);
	}
	return true;
}

void VXSet_erase(XSet* this_set, const void* key)
{
}

bool VXSet_remove(XSet* this_set, const void* key)
{
	if (ISNULL(this_set, "") || ISNULL(key, ""))
		return false;
	if (XSetBase_contains(this_set, key))
	{
		if (XContainerDataDeleteMethod(this_set) != NULL)
			XContainerDataDeleteMethod(this_set)(key);
		XRBTree_erase(&XContainerDataPtr(this_set), ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, key);

		--XContainerCapacity(this_set);
		--XContainerSize(this_set);
		return true;
	}
	return false;
}

bool VXSet_find(XSet* this_set, const void* key)
{
	if (ISNULL(this_set, "") || ISNULL(key, ""))
		return NULL;
	XBTreeNode* node = XRBTree_findData(XContainerDataPtr(this_set), ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, key);
	return node != NULL;
}


XSet* XSet_create(const size_t keyTypeSize, XEquality KeyEquality, XLess KeyLess)
{
	if (keyTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (KeyEquality == NULL || KeyLess == NULL)
	{
		printf("KeyEquality相等比较函数NULL或KeyLess小于比较函数NULL");
		return NULL;
	}
	XSet* this_set = (XSet*)XMemory_malloc(sizeof(XSet));
	XSet_init(this_set, keyTypeSize, KeyEquality, KeyLess);
	return this_set;
}

void XSet_init(XSet* this_set, const size_t keyTypeSize, XEquality KeyEquality, XLess KeyLess)
{
	if (ISNULL(this_set, ""))
		return NULL;
	if (keyTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (KeyEquality == NULL || KeyLess == NULL)
	{
		printf("KeyEquality相等比较函数NULL或KeyLess小于比较函数NULL");
		return NULL;
	}
	XSetBase_init(this_set, keyTypeSize, KeyEquality, KeyLess);
	XClassGetVtable(this_set) = XSet_class_init();
}

#endif