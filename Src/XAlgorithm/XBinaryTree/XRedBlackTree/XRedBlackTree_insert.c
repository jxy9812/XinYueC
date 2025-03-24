#include"XRedBlackTree.h"
#include"XBalancedBinaryTree.h"
//当前节点的父节点是红色，且当前节点的祖父节点的另一一个子节点(叔叔节点)也是红色
bool XRBTree_AdjustNoOne(XRBTreeNode** currentNode,XRBTreeNode* LPpater, XRBTreeNode* LPgrandpa, XRBTreeNode* LPuncle)
{
	if (LPuncle != NULL && XRBTree_IsRed(LPuncle))//叔叔节点是红色
	{
		XRBTree_SetBlack(LPpater);
		XRBTree_SetBlack(LPuncle);
		XRBTree_SetRed(LPgrandpa);
		*currentNode = LPgrandpa;
		return true;
	}
	return false;
}

//调整成为红黑树
void XRBTree_insertAdjust(XRBTreeNode** this_root, XRBTreeNode* currentNode)
{
	XRBTreeNode* LPpater = NULL;//父节点
	XRBTreeNode* LPgrandpa = NULL;//祖父节点
	XRBTreeNode* LPuncle = NULL;//叔叔节点
	while ((LPpater=XBTree_GetParent(currentNode))&& XRBTree_IsRed(LPpater))
	{
		LPgrandpa = XBTree_GetParent(LPpater);
		if (LPpater == XBTree_GetLChild(LPgrandpa))//父节点是祖父的左孩子
		{
			LPuncle = XBTree_GetRChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle))//叔叔节点是红色
				continue;
			//NO.2当前节点是其父节点的右孩子
			if (currentNode == XBTree_GetRChild(LPpater))
			{
				currentNode = LPpater;
				LPpater = XBTree_SpinLL(this_root, LPpater);
			}
			//NO.3当前节点是其父节点的左孩子
			{
				XRBTree_SetBlack(LPpater);
				XRBTree_SetRed(LPgrandpa);
				XBTree_SpinRR(this_root, LPgrandpa);
			}
		}
		else//父节点是祖父的右孩子
		{
			LPuncle = XBTree_GetLChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle))//叔叔节点是红色
				continue;
			//NO.2当前节点是其父节点的左孩子
			if (currentNode == XBTree_GetLChild(LPpater))
			{
				currentNode = LPpater;
				LPpater = XBTree_SpinRR(this_root, LPpater);
			}
			//NO.3当前节点是其父节点的左孩子
			{
				XRBTree_SetBlack(LPpater);
				XRBTree_SetRed(LPgrandpa);
				XBTree_SpinLL(this_root, LPgrandpa);
			}
		}
		//LPpater = XBTree_GetParent(currentNode);
	}
	XRBTree_SetBlack(*this_root);
}
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* LPData, const size_t TypeSize)
{
	//DEBUG_PRINTF("less=%p LPData=%p TypeSize=%u\n",less,LPData,TypeSize);
	if (ISNULL(less, ""))
		return NULL;
	if (ISNULL(LPData, ""))
		return NULL;
	if (ISNULL(TypeSize, ""))
		return NULL;
	XRBTreeNode* nodes = XRBTree_creation(TypeSize);//创建一个红黑树节点并且初始化,默认红色
	if (ISNULL(nodes, ""))
		return NULL;
	//DEBUG_PRINTF("nodes=%p\n",nodes);
	bool flag = XBBTree_insertAlign(this_root, nodes, less, lessRule, LPData, TypeSize);//将数据插入到节点，并且链接
	if (!flag)
	{	
		printf("节点插入失败\n");
		return NULL;
	}
	if (this_root == NULL)//根节点，无内存开辟
	{
		XRBTree_SetBlack(nodes);
		return nodes;
	}
	else if (*this_root == NULL)//根节点为空
	{
		*this_root = nodes;
		XRBTree_SetBlack(nodes);
		return nodes;
	}
	XRBTree_insertAdjust(this_root, nodes);
	return nodes;
}