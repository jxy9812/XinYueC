#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include"XStack.h"
XBBTreeNode* XBBTree_creation(const size_t TypeSize)
{
	struct XBBTreeNode* nodes = XBTree_creationNode(sizeof(XBBTreeNode),3,1,TypeSize);
	nodes->maxLayer = 1;
	return nodes;
}

XBBTreeNode* XBBTree_findData(XBBTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne Rule, void* LPData)
{
	/*if (isNULL(isNULLInfo(this_root,"")))
		return NULL; */
	if (this_root == NULL)//树是空的
		return NULL;
	XBBTreeNode* CurNode = this_root;//当前节点指针
	while (CurNode!=NULL)
	{
		//if (equality(CurNode->XBTNode.data, LPData))
		if(Rule(equality, CurNode, LPData))
		{
			return CurNode;
		}
		//else if (less(CurNode->XBTNode.data, LPData))
		else if(Rule(less, CurNode, LPData))
		{
			CurNode = XBTree_GetRChild(CurNode);
		}
		else
		{
			CurNode = XBTree_GetLChild(CurNode);
		}
	}
	return NULL;
}