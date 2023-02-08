#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
TreeNodeBalance* XBalancedBinaryTree_creation(const size_t TypeSize)
{
	struct TreeNodeBalance* node = XBinaryTreeObject_creationNode(sizeof(TreeNodeBalance),TypeSize);
	node->maxLayer = 1;
	return node;
}

TreeNodeBalance* XBalancedBinaryTree_find(TreeNodeBalance* this_root, XLess less, XEquality equality, const void* LPData)
{
	if (isNULL(isNULLInfo(this_root,"")))
		return NULL; 
	TreeNodeBalance* CurNode = this_root;//当前节点指针
	while (CurNode!=NULL)
	{
		if (equality(CurNode->data, LPData))
		{
			return CurNode;
		}
		else if (less(CurNode->data, LPData))
		{
			CurNode = CurNode->rightChild;
		}
		else
		{
			CurNode = CurNode->leftChild;
		}
	}
	return NULL;
}