#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<math.h>
//右旋
XBBTreeNode* XBBTree_SpinRR(XBBTreeNode* this_root)
{
	XBBTreeNode* NewRoot = XBTree_SpinRR(this_root);
	if (isNULL(isNULLInfo(NewRoot, "")))
		return NULL;
	XBBTree_SetLayerNumberThis(this_root);
	XBBTree_SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//左旋
XBBTreeNode* XBBTree_SpinLL(XBBTreeNode* this_root)
{
	XBBTreeNode* NewRoot = XBTree_SpinLL(this_root);
	if (isNULL(isNULLInfo(NewRoot, "")))
		return NULL;
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
	size_t leftLayer = XBBTree_GetLayerNumberThis(XBTree_GetLChild(*this_root));
	size_t rightLayer = XBBTree_GetLayerNumberThis(XBTree_GetRChild(*this_root));
	if (abs(leftLayer - rightLayer) > 1)
	{
		XBBTreeNode* centre = NULL;
		if (leftLayer > rightLayer)//左边比右边高度大于1(右旋/左右旋)
		{
			centre = XBTree_GetLChild(*this_root);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTree_GetLChild(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTree_GetRChild(centre));
			if (centreLeft > centreRight)//左大于右(右旋)
				*this_root = XBBTree_SpinRR(*this_root);
			else
				*this_root = XBBTree_SpinLR(*this_root);
		}
		else//左旋/右左旋
		{
			centre = XBTree_GetRChild(*this_root);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTree_GetLChild(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTree_GetRChild(centre));
			if (centreLeft < centreRight)//右大于左(左旋)
				*this_root = XBBTree_SpinLL(*this_root);
			else
				*this_root = XBBTree_SpinRL(*this_root);
		}

	}
	return *this_root;
}