#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
const size_t XBalancedBinaryTree_GetLayerNumberThis(const TreeNodeBalance* this_root)
{
	if (this_root != NULL)
		return this_root->maxLayer;
	return 0;
}
const size_t XBalancedBinaryTree_GetLayerNumberChild(const TreeNodeBalance* this_root)
{
	size_t left = XBalancedBinaryTree_GetLayerNumberThis(this_root->leftChild);
	size_t right = XBalancedBinaryTree_GetLayerNumberThis(this_root->rightChild);
	return (left > right) ? left : right;
}
const size_t XBalancedBinaryTree_SetLayerNumberThis(TreeNodeBalance* this_root)
{
	return this_root->maxLayer = 1 + XBalancedBinaryTree_GetLayerNumberChild(this_root);;
}
const size_t XBalancedBinaryTree_SetLayerNumberAll(TreeNodeBalance** this_root, TreeNodeBalance* currentNode)
{
	size_t count = 0;//向上调整一共经历了多少个节点
	//循环返回父节点设置层数
	while (currentNode != NULL)
	{
		(currentNode)->maxLayer = 1 + XBalancedBinaryTree_GetLayerNumberChild(currentNode);
		if (currentNode == *this_root)//如果是根节点
		{
			XBalancedBinaryTree_Spin(this_root);//传入根的二级指针
		}
		else//存在父节点
		{//传父节点指向孩子的指针
			TreeNodeBalance* root_par = currentNode->parent;
			if (root_par->leftChild == currentNode)
				XBalancedBinaryTree_Spin(&(root_par->leftChild));//
			else if (root_par->rightChild == currentNode)
				XBalancedBinaryTree_Spin(&(root_par->rightChild));
		}
		currentNode = (currentNode)->parent;
		++count;
	}
	return count;
}