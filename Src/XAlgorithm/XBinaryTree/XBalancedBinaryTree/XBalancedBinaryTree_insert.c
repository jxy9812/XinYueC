#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
bool XBBTree_insertAlign(XBBTreeNode** this_root, XBBTreeNode* insertNode, XLess less, XCompareRuleTwo lessRule, const void* pvData, const size_t TypeSize)
{
	//if (!XBTree_insertData(insertNode, LPData, 0))//插入数据
	if(!XBTreeNode_setData(insertNode,0,pvData))
	{
		XBTreeNode_delete(insertNode);//插入失败释放创建的节点
		return false;
	}
	if (this_root == NULL|| *this_root == NULL)//如果没有根节点或根为空
	{
		return true;
	}
	//开始遍历,插入节点
	XBBTreeNode* currentNode = *this_root;

	while (currentNode != NULL)
	{
		//满足小于往左边放
		if(lessRule(less, insertNode, currentNode))
		//if (less(insertNode->XBTNode.data, currentNode->XBTNode.data))
		{
			XBTreeNode** ppLChild = XBTreeNode_getNodeRef(currentNode, XBTreeLChild);
			if (*ppLChild == NULL)//建立关系
			{
				*ppLChild = insertNode;
				XBTreeNode_SetParent(insertNode, currentNode);
				return true;
			}
			else
			{
				currentNode = *ppLChild;
			}
		}
		else//满足大于等于的情况
		{
			XBTreeNode** ppRChild = XBTreeNode_getNodeRef(currentNode, XBTreeRChild);
			if (*ppRChild == NULL)//建立关系
			{
				*ppRChild = insertNode;
				XBTreeNode_SetParent(insertNode,currentNode);
				return true;
			}
			else
			{
				currentNode = *ppRChild;
			}
		}
	}
	XBTreeNode_delete(insertNode);
	return false;
}
XBBTreeNode* XBBTree_insert(XBBTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* LPData, const size_t TypeSize)
{
	if (ISNULL(less, ""))
		return NULL;
	if (ISNULL(LPData, ""))
		return NULL;
	if (ISNULL(TypeSize, ""))
		return NULL;
	//创建一个新的节点
	XBBTreeNode* NewNode = XBBTree_creation(TypeSize);
	if (ISNULL(NewNode, ""))
		return NULL;
	bool flag=XBBTree_insertAlign(this_root, NewNode, less, lessRule, LPData, TypeSize);
	if (!flag)
	{	
		printf("节点插入失败\n");
		return NULL;
	}
	XBBTree_SetLayerNumberAll(this_root, XBTreeNode_GetParent(NewNode));
	return NewNode;
}