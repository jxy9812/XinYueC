#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<math.h>
//右旋
XBBTreeNode* XBBTree_SpinRR(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode* NewRoot = XBTREE_GET_LCHILD(this_root);

	XBBTreeNode** ppThis_rootParent= XBTree_GetTreeNode(this_root, XBTreeParent);
	XBTREE_SET_PARENT(NewRoot,*ppThis_rootParent);
	*ppThis_rootParent = NewRoot;

	XBBTreeNode** ppNewRootRightChild= XBTree_GetTreeNode(NewRoot, XBTreeRChild);
	XBTREE_SET_LCHILD(this_root,*ppNewRootRightChild);
	if (*ppNewRootRightChild != NULL)
		XBTREE_SET_PARENT(*ppNewRootRightChild,this_root);

	*ppNewRootRightChild = this_root;
	XBBTree_SetLayerNumberThis(this_root);
	XBBTree_SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//左旋
XBBTreeNode* XBBTree_SpinLL(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode* NewRoot = XBTREE_GET_RCHILD(this_root);

	XBBTreeNode** ppThis_rootParent = XBTree_GetTreeNode(this_root, XBTreeParent);
	XBTREE_SET_PARENT(NewRoot,*ppThis_rootParent);
	*ppThis_rootParent = NewRoot;

	XBBTreeNode** ppNewRootLeftChild = XBTree_GetTreeNode(NewRoot, XBTreeLChild);
	XBTREE_SET_RCHILD(this_root,*ppNewRootLeftChild);
	if(*ppNewRootLeftChild !=NULL)
		XBTREE_SET_PARENT(*ppNewRootLeftChild,this_root);

	*ppNewRootLeftChild = this_root;

	XBBTree_SetLayerNumberThis(this_root);
	XBBTree_SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//右左旋
XBBTreeNode* XBBTree_SpinRL(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode** ppThisRootRightChild = XBTree_GetTreeNode(this_root, XBTreeRChild);
	*ppThisRootRightChild = XBBTree_SpinRR(*ppThisRootRightChild);
	return XBBTree_SpinLL(this_root);
}
//左右旋
XBBTreeNode* XBBTree_SpinLR(XBBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode** ppThisRootLeftChild = XBTree_GetTreeNode(this_root, XBTreeLChild);
	*ppThisRootLeftChild = XBBTree_SpinLL(*ppThisRootLeftChild);
	return XBBTree_SpinRR(this_root);
}

XBBTreeNode* XBBTree_Spin(const XBBTreeNode** this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	if (isNULL(isNULLInfo(*this_root, "")))
		return NULL;
	size_t leftLayer = XBBTree_GetLayerNumberThis(XBTREE_GET_LCHILD(*this_root));
	size_t rightLayer = XBBTree_GetLayerNumberThis(XBTREE_GET_RCHILD(*this_root));
	if (abs(leftLayer - rightLayer) > 1)
	{
		XBBTreeNode* centre = NULL;
		if (leftLayer > rightLayer)//左边比右边高度大于1(右旋/左右旋)
		{
			centre = XBTREE_GET_LCHILD(*this_root);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTREE_GET_LCHILD(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTREE_GET_RCHILD(centre));
			if (centreLeft > centreRight)//左大于右(右旋)
				*this_root = XBBTree_SpinRR(*this_root);
			else
				*this_root = XBBTree_SpinLR(*this_root);
		}
		else//左旋/右左旋
		{
			centre = XBTREE_GET_RCHILD(*this_root);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTREE_GET_LCHILD(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTREE_GET_RCHILD(centre));
			if (centreLeft < centreRight)//右大于左(左旋)
				*this_root = XBBTree_SpinLL(*this_root);
			else
				*this_root = XBBTree_SpinRL(*this_root);
		}

	}
	return *this_root;
}