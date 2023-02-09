#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include"XStack.h"
XBBTreeNode* XBBTree_creation(const size_t TypeSize)
{
	struct XBBTreeNode* node = XBTree_creationNode(sizeof(XBBTreeNode),3,TypeSize);
	node->maxLayer = 1;
	return node;
}

XBBTreeNode* XBBTree_findData(XBBTreeNode* this_root, XLess less, XEquality equality, const void* LPData)
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
			CurNode = XBTREE_GET_RCHILD(CurNode);
		}
		else
		{
			CurNode = XBTREE_GET_LCHILD(CurNode);
		}
	}
	return NULL;
}