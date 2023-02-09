#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<math.h>
//右旋
static XBBTreeNode* RR(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode* NewRoot = XBTreeGetLChild(this_root);

	XBBTreeNode** ppThis_rootParent= XBinaryTreeObject_GetTreeNode(this_root, XBTreeParent);
	XBTreeGetParent(NewRoot) = *ppThis_rootParent;
	*ppThis_rootParent = NewRoot;

	XBBTreeNode** ppNewRootRightChild= XBinaryTreeObject_GetTreeNode(NewRoot, XBTreeRChild);
	XBTreeGetLChild(this_root) = *ppNewRootRightChild;
	if (*ppNewRootRightChild != NULL)
		XBTreeGetParent(*ppNewRootRightChild) = this_root;

	*ppNewRootRightChild = this_root;
	XBalancedBinaryTree_SetLayerNumberThis(this_root);
	XBalancedBinaryTree_SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//左旋
static XBBTreeNode* LL(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode* NewRoot = XBTreeGetRChild(this_root);

	XBBTreeNode** ppThis_rootParent = XBinaryTreeObject_GetTreeNode(this_root, XBTreeParent);
	XBTreeGetParent(NewRoot) = *ppThis_rootParent;
	*ppThis_rootParent = NewRoot;

	XBBTreeNode** ppNewRootLeftChild = XBinaryTreeObject_GetTreeNode(NewRoot, XBTreeLChild);
	XBTreeGetRChild(this_root) = *ppNewRootLeftChild;
	if(*ppNewRootLeftChild !=NULL)
		XBTreeGetParent(*ppNewRootLeftChild) = this_root;

	*ppNewRootLeftChild = this_root;

	XBalancedBinaryTree_SetLayerNumberThis(this_root);
	XBalancedBinaryTree_SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//右左旋
static XBBTreeNode* RL(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode** ppThisRootRightChild = XBinaryTreeObject_GetTreeNode(this_root, XBTreeRChild);
	*ppThisRootRightChild = RR(*ppThisRootRightChild);
	return LL(this_root);
}
//左右旋
static XBBTreeNode* LR(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode** ppThisRootLeftChild = XBinaryTreeObject_GetTreeNode(this_root, XBTreeLChild);
	*ppThisRootLeftChild = LL(*ppThisRootLeftChild);
	return RR(this_root);
}

XBBTreeNode* XBalancedBinaryTree_Spin(const XBBTreeNode** this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	if (isNULL(isNULLInfo(*this_root, "")))
		return NULL;
	size_t leftLayer = XBalancedBinaryTree_GetLayerNumberThis(XBTreeGetLChild(*this_root));
	size_t rightLayer = XBalancedBinaryTree_GetLayerNumberThis(XBTreeGetRChild(*this_root));
	if (abs(leftLayer - rightLayer) > 1)
	{
		XBBTreeNode* centre = NULL;
		if (leftLayer > rightLayer)//左边比右边高度大于1(右旋/左右旋)
		{
			centre = XBTreeGetLChild(*this_root);
			size_t centreLeft = XBalancedBinaryTree_GetLayerNumberThis(XBTreeGetLChild(centre));
			size_t centreRight = XBalancedBinaryTree_GetLayerNumberThis(XBTreeGetRChild(centre));
			if (centreLeft > centreRight)//左大于右(右旋)
				*this_root = RR(*this_root);
			else
				*this_root = LR(*this_root);
		}
		else//左旋/右左旋
		{
			centre = XBTreeGetRChild(*this_root);
			size_t centreLeft = XBalancedBinaryTree_GetLayerNumberThis(XBTreeGetLChild(centre));
			size_t centreRight = XBalancedBinaryTree_GetLayerNumberThis(XBTreeGetRChild(centre));
			if (centreLeft < centreRight)//右大于左(左旋)
				*this_root = LL(*this_root);
			else
				*this_root = RL(*this_root);
		}

	}
	return *this_root;
}