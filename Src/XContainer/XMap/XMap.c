#include"XMap.h"
#if XMap_ON
#include"XContainerObject.h"
#include"XPair.h"
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
XMap* XMap_create(const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess)
{
	if (keyTypeSize == 0 || valTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (KeyEquality == NULL || KeyLess == NULL)
	{
		printf("KeyEquality相等比较函数NULL或KeyLess小于比较函数NULL");
		return NULL;
	}
	XMap* this_map = (XMap*)XMemory_malloc(sizeof(XMap));
	XMap_init(this_map,keyTypeSize,valTypeSize,KeyEquality,KeyLess);
	return this_map;
}
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess)
{
	if (ISNULL(this_map, ""))
		return NULL;
	if (keyTypeSize == 0 || valTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (KeyEquality == NULL || KeyLess == NULL)
	{
		printf("KeyEquality相等比较函数NULL或KeyLess小于比较函数NULL");
		return NULL;
	}
	XMapBase_init(this_map, keyTypeSize, valTypeSize, KeyEquality);
	XClassGetVtable(this_map) = XMap_class_init();
	this_map->m_KeyLess = KeyLess;
	this_map->m_isModify = true;
	this_map->m_itArray = NULL;
}

static void ForTreeNode(void* LPVal, void* args)
{
#if XVector_ON
	XRBTreeNode* nodes = *(XRBTreeNode**)LPVal;
	XVector_push_back_base(args, XVector_at_base(nodes->XBTNode.values, 0));
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
void XMap_updataIterator(XMap* this_map)
{
#if XVector_ON
	if (ISNULL(this_map, "map不能为NULL"))
		return;
	if (XMap_isEmpty_base(this_map))//map当前是空的
		return;
	if (!this_map->m_isModify)
		return;
	//开始更新迭代器
	if (this_map->m_itArray != NULL)
	{
		//释放原先的数组
		XVector_delete_base(this_map->m_itArray);
		this_map->m_itArray = NULL;
	}
	//已中序遍历获取所有树的节点,临时
	XVector* TreeNode = XBTree_TraversingToXVector(XContainerDataPtr(this_map), XBTreeInorder);
	this_map->m_itArray = XVector_create(sizeof(XPair*));
	//将数据XPair的节点指针插入数组
	XVector_iterator_for_each(TreeNode, ForTreeNode, this_map->m_itArray);
	XVector_delete_base(TreeNode);

	this_map->m_isModify = false;
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
#endif