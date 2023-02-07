#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
#include<math.h>
//右旋
static TreeNodeBalance* RR(TreeNodeBalance* root)
{
	if (isObjectNULL(root, "TreeNodeBalance_RR"))
		return NULL;
	TreeNodeBalance* NewRoot = root->leftChild;
	////存在父节点，重新定义父节点的孩子指针
	//if (root->parent != NULL)
	//{
	//	TreeNodeBalance* root_par = root->parent;
	//	if (root_par->leftChild == root)
	//		root_par->leftChild = NewRoot;
	//	else if (root_par->rightChild == root)
	//		root_par->rightChild = NewRoot;
	//}

	NewRoot->parent = root->parent;
	root->parent = NewRoot;

	root->leftChild = NewRoot->rightChild;
	NewRoot->rightChild = root;
	XBalancedBinaryTree_SetLayerNumberThis(root);
	XBalancedBinaryTree_SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//左旋
static TreeNodeBalance* LL(TreeNodeBalance* this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_LL"))
		return NULL;
	TreeNodeBalance* NewRoot = this_root->rightChild;

	NewRoot->parent = this_root->parent;
	this_root->parent = NewRoot;

	this_root->rightChild = NewRoot->leftChild;
	NewRoot->leftChild = this_root;

	XBalancedBinaryTree_SetLayerNumberThis(this_root);
	XBalancedBinaryTree_SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//右左旋
static TreeNodeBalance* RL(TreeNodeBalance* this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_RL"))
		return NULL;
	this_root->rightChild = RR(this_root->rightChild);
	return LL(this_root);
}
//左右旋
static TreeNodeBalance* LR(TreeNodeBalance* this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_LR"))
		return NULL;
	this_root->leftChild = LL(this_root->leftChild);
	return RR(this_root);
}

void XBalancedBinaryTree_Spin(const TreeNodeBalance** this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_Spin-this_root"))
		return NULL;
	if (isObjectNULL(*this_root, "TreeNodeBalance_Spin-this_root"))
		return NULL;
	size_t leftLayer = XBalancedBinaryTree_GetLayerNumberThis((*this_root)->leftChild);
	size_t rightLayer = XBalancedBinaryTree_GetLayerNumberThis((*this_root)->rightChild);
	if (abs(leftLayer - rightLayer) > 1)
	{
		TreeNodeBalance* centre = NULL;
		if (leftLayer > rightLayer)//左边比右边高度大于1(右旋/左右旋)
		{
			centre = (*this_root)->leftChild;
			size_t centreLeft = XBalancedBinaryTree_GetLayerNumberThis(centre->leftChild);
			size_t centreRight = XBalancedBinaryTree_GetLayerNumberThis(centre->rightChild);
			if (centreLeft > centreRight)//左大于右(右旋)
				*this_root = RR(*this_root);
			else
				*this_root = LR(*this_root);
		}
		else//左旋/右左旋
		{
			centre = (*this_root)->rightChild;
			size_t centreLeft = XBalancedBinaryTree_GetLayerNumberThis(centre->leftChild);
			size_t centreRight = XBalancedBinaryTree_GetLayerNumberThis(centre->rightChild);
			if (centreLeft < centreRight)//右大于左(左旋)
				*this_root = LL(*this_root);
			else
				*this_root = RL(*this_root);
		}

	}
}