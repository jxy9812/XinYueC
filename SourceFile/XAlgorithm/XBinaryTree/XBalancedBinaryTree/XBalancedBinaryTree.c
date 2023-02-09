#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include"XStack.h"
XBBTreeNode* XBalancedBinaryTree_creation(const size_t TypeSize)
{
	struct XBBTreeNode* node = XBinaryTreeObject_creationNode(sizeof(XBBTreeNode),3,TypeSize);
	node->maxLayer = 1;
	return node;
}

XBBTreeNode* XBalancedBinaryTree_find(XBBTreeNode* this_root, XLess less, XEquality equality, const void* LPData)
{
	if (isNULL(isNULLInfo(this_root,"")))
		return NULL; 
	XBBTreeNode* CurNode = this_root;//当前节点指针
	while (CurNode!=NULL)
	{
		if (equality(CurNode->XBTNode.data, LPData))
		{
			return CurNode;
		}
		else if (less(CurNode->XBTNode.data, LPData))
		{
			CurNode = *XBinaryTreeObject_GetTreeNode(CurNode, XBTreeRChild);
		}
		else
		{
			CurNode = *XBinaryTreeObject_GetTreeNode(CurNode, XBTreeLChild);
		}
	}
	return NULL;
}