#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
const size_t XBBTree_GetLayerNumberThis(const XBBTreeNode* this_root)
{
	if (this_root != NULL)
		return this_root->maxLayer;
	return 0;
}
const size_t XBBTree_GetLayerNumberChild(const XBBTreeNode* this_root)
{
	size_t left = XBBTree_GetLayerNumberThis(XBTREE_GET_LCHILD(this_root));
	size_t right = XBBTree_GetLayerNumberThis(XBTREE_GET_RCHILD(this_root));
	return (left > right) ? left : right;
}
const size_t XBBTree_SetLayerNumberThis(XBBTreeNode* this_root)
{
	return this_root->maxLayer = 1 + XBBTree_GetLayerNumberChild(this_root);;
}
const size_t XBBTree_SetLayerNumberAll(XBBTreeNode** this_root, XBBTreeNode* currentNode)
{
	size_t count = 0;//向上调整一共经历了多少个节点
	//循环返回父节点设置层数
	while (currentNode != NULL)
	{
		currentNode->maxLayer = 1 + XBBTree_GetLayerNumberChild(currentNode);
		if (currentNode == *this_root)//如果是根节点
		{
			currentNode = XBBTree_Spin(this_root);//传入根的二级指针
		}
		else//存在父节点
		{//传父节点指向孩子的指针
			currentNode = XBBTree_Spin(XBTree_findChildisParent(currentNode));
		}
		currentNode = XBTREE_GET_PARENT(currentNode);
		++count;
	}
	return count;
}