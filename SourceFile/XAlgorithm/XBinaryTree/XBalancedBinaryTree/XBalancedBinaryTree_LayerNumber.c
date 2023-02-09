#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
const size_t XBalancedBinaryTree_GetLayerNumberThis(const XBBTreeNode* this_root)
{
	if (this_root != NULL)
		return this_root->maxLayer;
	return 0;
}
const size_t XBalancedBinaryTree_GetLayerNumberChild(const XBBTreeNode* this_root)
{
	size_t left = XBalancedBinaryTree_GetLayerNumberThis(*XBinaryTreeObject_GetTreeNode(this_root, XBTreeLChild));
	size_t right = XBalancedBinaryTree_GetLayerNumberThis(*XBinaryTreeObject_GetTreeNode(this_root, XBTreeRChild));
	return (left > right) ? left : right;
}
const size_t XBalancedBinaryTree_SetLayerNumberThis(XBBTreeNode* this_root)
{
	return this_root->maxLayer = 1 + XBalancedBinaryTree_GetLayerNumberChild(this_root);;
}
const size_t XBalancedBinaryTree_SetLayerNumberAll(XBBTreeNode** this_root, XBBTreeNode* currentNode)
{
	size_t count = 0;//向上调整一共经历了多少个节点
	//循环返回父节点设置层数
	while (currentNode != NULL)
	{
		currentNode->maxLayer = 1 + XBalancedBinaryTree_GetLayerNumberChild(currentNode);
		if (currentNode == *this_root)//如果是根节点
		{
			currentNode = XBalancedBinaryTree_Spin(this_root);//传入根的二级指针
		}
		else//存在父节点
		{//传父节点指向孩子的指针
			currentNode = XBalancedBinaryTree_Spin(XBinaryTreeObject_findChildisParent(currentNode));
		}
		currentNode = *XBinaryTreeObject_GetTreeNode(currentNode, XBTreeParent);
		++count;
	}
	return count;
}