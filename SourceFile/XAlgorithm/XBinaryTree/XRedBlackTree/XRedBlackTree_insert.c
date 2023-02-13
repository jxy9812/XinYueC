#include"XRedBlackTree.h"
//#include"XBalancedBinaryTree.h"
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
void XRBTree_insertAdjust(XRBTreeNode* currentNode)
{
	XRBTreeNode* LPpater = NULL;//父节点
	XRBTreeNode* LPgrandpa = NULL;//祖父节点
	XRBTreeNode* LPuncle = NULL;//叔叔节点
	while ((LPpater = XBTree_GetParent(currentNode))&& XRBTree_IsRed(LPpater))
	{
		LPgrandpa = XBTree_GetParent(LPpater);
		if (LPpater == XBTree_GetLChild(LPgrandpa))//父节点是祖父的左孩子
		{
			LPuncle = XBTree_GetRChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle))//叔叔节点是红色
				continue;
			if (LPuncle != NULL && XRBTree_IsBlack(LPuncle))//叔叔节点是黑色
			{
				//NO.2当前节点是其父节点的右孩子
				if (currentNode == XBTree_GetRChild(LPpater))
				{
					currentNode = LPpater;
					LPpater = XBTree_SpinLL(LPpater);
				}
				//NO.3当前节点是其父节点的左孩子
				{
					XRBTree_SetBlack(LPpater);
					XRBTree_SetRed(LPgrandpa);
					XBTree_SpinRR(LPgrandpa);
				}
			}
		}
		else//父节点是祖父的右孩子
		{
			LPuncle = XBTree_GetLChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle))//叔叔节点是红色
				continue;
			if (LPuncle != NULL && XRBTree_IsBlack(LPuncle))//叔叔节点是黑色
			{
				//NO.2当前节点是其父节点的左孩子
				if (currentNode == XBTree_GetLChild(LPpater))
				{
					currentNode = LPpater;
					LPpater = XBTree_SpinRR(LPpater);
				}
				//NO.3当前节点是其父节点的左孩子
				{
					XRBTree_SetBlack(LPpater);
					XRBTree_SetRed(LPgrandpa);
					XBTree_SpinLL(LPgrandpa);
				}
			}
		}
	}
	XRBTree_SetBlack(currentNode);
}
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(less, "")))
		return NULL;
	if (isNULL(isNULLInfo(LPData, "")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "")))
		return NULL;
	XRBTreeNode* node = XRBTree_creation(TypeSize);//创建一个红黑树节点并且初始化,默认红色
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	bool flag = XBBTree_insertAlign(this_root, node, less, LPData, TypeSize);//将数据插入到节点，并且链接
	if (isNULL(isNULLInfo(flag, "节点插入失败")))
		return NULL;
	XRBTree_insertAdjust( node);
	return node;
}