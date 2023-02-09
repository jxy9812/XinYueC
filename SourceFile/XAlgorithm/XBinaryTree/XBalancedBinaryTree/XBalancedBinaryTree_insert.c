#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
XBBTreeNode* XBBTree_insertAlign(XBBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(less, "")))
		return NULL;
	if (isNULL(isNULLInfo(LPData, "")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "")))
		return NULL;
	//创建一个新的节点
	XBBTreeNode* NewNode = XBBTree_creation(TypeSize);
	if (!XBTree_insertData(NewNode, LPData, TypeSize))//插入数据
	{
		XBTree_freeNode(NewNode, false);//插入失败释放创建的节点
		return NULL;
	}
	if (this_root == NULL)//如果没有根节点
	{
		return NewNode;
	}
	//开始遍历,插入节点
	//size_t currentHeight = 0;//当前高度
	XBBTreeNode* currentNode = *this_root;
	while (currentNode != NULL)
	{
		//满足小于往左边放
		if (less(NewNode->XBTNode.data, currentNode->XBTNode.data))
		{
			XBTreeNode** ppLChild = XBTree_GetTreeNode(currentNode, XBTreeLChild);
			if (*ppLChild == NULL)//建立关系
			{
				*ppLChild = NewNode;
				XBTREE_SET_PARENT(NewNode, currentNode);
				break;
			}
			else
			{
				currentNode = *ppLChild;
			}
		}
		else//满足大于等于的情况
		{
			XBTreeNode** ppRChild = XBTree_GetTreeNode(currentNode, XBTreeRChild);
			if (*ppRChild == NULL)//建立关系
			{
				*ppRChild = NewNode;
				XBTREE_SET_PARENT(NewNode,currentNode);
				break;
			}
			else
			{
				currentNode = *ppRChild;
			}
		}
	}
	return NewNode;
}
XBBTreeNode* XBBTree_insert(XBBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize)
{
	XBBTreeNode* NewNode =XBBTree_insertAlign(this_root, less, LPData, TypeSize);
	if (isNULL(isNULLInfo(NewNode, "")))
		return NULL;
	XBBTree_SetLayerNumberAll(this_root, XBTREE_GET_PARENT(NewNode));
	return NewNode;
}