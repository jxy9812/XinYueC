#include"XRedBlackTree.h"
#include"XBalancedBinaryTree.h"
XRBTreeNode* XRBTree_creation(const size_t TypeSize)
{
	XRBTreeNode* node = (XRBTreeNode*)XBTree_creationNode(sizeof(XRBTreeNode),3,TypeSize);
	if (isNULL(isNULLInfo(node, "创建红黑树节点失败")))
		return NULL;
	XRBTree_SetRed(node);
	return node;
}

