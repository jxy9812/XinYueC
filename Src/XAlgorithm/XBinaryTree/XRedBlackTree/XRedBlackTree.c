#include"XRedBlackTree.h"
#include"XClass.h"
#include"XBalancedBinaryTree.h"
XRBTreeNode* XRBTree_creation(const size_t TypeSize)
{
	//XRBTreeNode* nodes = (XRBTreeNode*)XBTree_creationNode(sizeof(XRBTreeNode),3,1,TypeSize);
	XRBTreeNode* node = XMemory_malloc(sizeof(XRBTreeNode));
	if (ISNULL(node, "创建红黑树节点失败"))
		return NULL;
	XBTreeNode_init(node,3,1,TypeSize);
	XRBTree_SetRed(node);
	return node;
}

