#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<math.h>
//右旋
XBBTreeNode* XBBTree_SpinRR(XBBTreeNode** this_root, XBBTreeNode* node)
{
	XBBTreeNode* NewNode = XBTree_SpinRR(this_root,node);
	if (isNULL(isNULLInfo(NewNode, "")))
		return NULL;
	XBBTree_SetLayerNumberThis(node);
	XBBTree_SetLayerNumberThis(NewNode);
	return NewNode;
}
//左旋
XBBTreeNode* XBBTree_SpinLL(XBBTreeNode** this_root, XBBTreeNode* node)
{
	XBBTreeNode* NewNode = XBTree_SpinLL(this_root, node);
	if (isNULL(isNULLInfo(NewNode, "")))
		return NULL;
	XBBTree_SetLayerNumberThis(node);
	XBBTree_SetLayerNumberThis(NewNode);
	return NewNode;
}
//右左旋
XBBTreeNode* XBBTree_SpinRL(XBBTreeNode** this_root, XBBTreeNode* node)
{
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	XBBTreeNode** ppThisRootRightChild = XBTree_GetTreeNode(node, XBTreeRChild);
	/**ppThisRootRightChild = */XBBTree_SpinRR(this_root ,*ppThisRootRightChild);
	return XBBTree_SpinLL(this_root, node);
}
//左右旋
XBBTreeNode* XBBTree_SpinLR(XBBTreeNode** this_root, XBBTreeNode* node)
{
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	XBBTreeNode** ppThisRootLeftChild = XBTree_GetTreeNode(node, XBTreeLChild);
	/**ppThisRootLeftChild =*/ XBBTree_SpinLL(this_root ,*ppThisRootLeftChild);
	return XBBTree_SpinRR(this_root, node);
}

XBBTreeNode* XBBTree_Spin(XBBTreeNode** this_root, XBBTreeNode* node)
{
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	size_t leftLayer = XBBTree_GetLayerNumberThis(XBTree_GetLChild(node));
	size_t rightLayer = XBBTree_GetLayerNumberThis(XBTree_GetRChild(node));
	XBBTreeNode* newNode = node;
	if (abs(leftLayer - rightLayer) > 1)
	{
		XBBTreeNode* centre = NULL;
		if (leftLayer > rightLayer)//左边比右边高度大于1(右旋/左右旋)
		{
			centre = XBTree_GetLChild(node);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTree_GetLChild(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTree_GetRChild(centre));
			if (centreLeft > centreRight)//左大于右(右旋)
				newNode=XBBTree_SpinRR(this_root,node);
			else
				newNode = XBBTree_SpinLR(this_root, node);
		}
		else//左旋/右左旋
		{
			centre = XBTree_GetRChild(node);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTree_GetLChild(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTree_GetRChild(centre));
			if (centreLeft < centreRight)//右大于左(左旋)
				newNode = XBBTree_SpinLL(this_root, node);
			else
				newNode = XBBTree_SpinRL(this_root, node);
		}

	}
	return newNode;
}