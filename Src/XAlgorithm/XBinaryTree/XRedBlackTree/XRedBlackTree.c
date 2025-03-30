#include"XRedBlackTree.h"
#include"XClass.h"
#include"XBalancedBinaryTree.h"
XRBTreeNode* XRBTree_creation(const size_t TypeSize)
{
	XRBTreeNode* nodes = (XRBTreeNode*)XBTree_creationNode(sizeof(XRBTreeNode),3,1,TypeSize);
	if (ISNULL(nodes, "创建红黑树节点失败"))
		return NULL;
	XRBTree_SetRed(nodes);
	return nodes;
}

