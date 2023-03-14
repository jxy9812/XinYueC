#include"XMap.h"
#include"XMap_func.h"
#include"XContainerObject.h"
#include"XPair.h"
#include"XBalancedBinaryTree.h"
#include"XRedBlackTree.h"
#include"XAlgorithm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
static void ForTreeNode(void* LPVal, void* args)
{
	XRBTreeNode* nodes = *(XRBTreeNode**)LPVal;
	XVector_push_back(args, (nodes->XBTNode.values));
}
void XMap_updataIterator(XMap* this_map)
{
	if (isNULL(isNULLInfo(this_map, "")))
		return NULL;
	if (!this_map->isModify)
		return;
	//开始更新迭代器
	if (this_map->itArray != NULL)
	{
		//释放原先的数组
		XVector_free(this_map->itArray);
		this_map->itArray = NULL;
	}
	//已中序遍历获取所有树的节点,临时
	XVector* TreeNode = XBTree_TraversingToXVector(this_map->object._data, XBTreeInorder);
	this_map->itArray = XVector_init("XPair*", sizeof(XPair*));
	//将数据XPair的节点指针插入数组
	XVector_iterator_for_each(TreeNode, ForTreeNode, this_map->itArray);
	XVector_free(TreeNode);

	this_map->isModify = false;
}