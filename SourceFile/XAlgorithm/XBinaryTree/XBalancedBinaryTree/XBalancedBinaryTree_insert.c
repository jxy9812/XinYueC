#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"

XBBTreeNode* XBalancedBinaryTree_insert(XBBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(less, "")))
		return NULL;
	if (isNULL(isNULLInfo(LPData, "")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "")))
		return NULL;
	//创建一个新的节点
	XBBTreeNode* NewNode = XBalancedBinaryTree_creation(TypeSize);
	if (!XBinaryTreeObject_insertData(NewNode, LPData, TypeSize))//插入数据
	{
		XBinaryTreeObject_freeNode(NewNode, false);//插入失败释放创建的节点
		return NULL;
	}
	if (this_root == NULL)//如果没有根节点
	{
		return NewNode;
	}
	//开始遍历,插入节点
	size_t currentHeight = 0;//当前高度
	XBBTreeNode* currentNode = *this_root;
	while (currentNode != NULL)
	{
		//满足小于往左边放
		if (less(NewNode->XBTNode.data, currentNode->XBTNode.data))
		{
			XBinaryTreeNode** ppLChild = XBinaryTreeObject_GetTreeNode(currentNode, XBTreeLChild);
			if (*ppLChild == NULL)//建立关系
			{
				*ppLChild = NewNode;
				*XBinaryTreeObject_GetTreeNode(NewNode, XBTreeParent) = currentNode;
				break;
			}
			else
			{
				currentNode = *ppLChild;
			}
		}
		else//满足大于等于的情况
		{
			XBinaryTreeNode** ppRChild = XBinaryTreeObject_GetTreeNode(currentNode, XBTreeRChild);
			if (*ppRChild == NULL)//建立关系
			{
				*ppRChild = NewNode;
				*XBinaryTreeObject_GetTreeNode(NewNode, XBTreeParent) = currentNode;
				break;
			}
			else
			{
				currentNode = *ppRChild;
			}
		}
	}
	XBalancedBinaryTree_SetLayerNumberAll(this_root, currentNode);
	return NewNode;
}