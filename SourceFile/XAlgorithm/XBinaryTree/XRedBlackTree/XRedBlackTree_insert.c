#include"XRedBlackTree.h"
#include"XBalancedBinaryTree.h"
//调整成为红黑树
void XRBTree_Adjust(XRBTreeNode** this_root, XRBTreeNode* currentNode, XLess less, const void* LPData, const size_t TypeSize)
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
			if (LPuncle != NULL && XRBTree_IsRed(LPuncle))//叔叔节点是红色
			{
				XRBTree_SetBlack(LPpater);
				XRBTree_SetBlack(LPuncle);
				XRBTree_SetRed(LPgrandpa);
				currentNode = LPgrandpa;
				continue;
			}
			else if (LPuncle != NULL && XRBTree_IsBlack(LPuncle))//叔叔节点是黑色
			{
				if (currentNode == XBTree_GetRChild(LPpater))
				{
					currentNode = LPpater;
					LPpater = XBTree_SpinLL(LPpater);
				}
				//else
				{
					XRBTree_SetBlack(LPpater);
					XRBTree_SetRed(LPgrandpa);
					XBTree_SpinRR(LPgrandpa);
				}

			}
		}
	}

	if (LPpater == NULL)//情况说明：被插入的节点是根节点。
	{
		XRBTree_SetBlack(currentNode);
		return;//    处理方法：直接把此节点涂为黑色。
	}
	if (XRBTree_IsBlack(LPpater))//情况说明：被插入的节点的父节点是黑色。
	{
		return;//处理方法：什么也不需要做。节点被插入后，仍然是红黑树。
	}
	if (XRBTree_IsRed(LPpater))//情况说明：被插入的节点的父节点是红色。
	{
		XRBTreeNode* LPgrandpa= XBTree_GetParent(LPpater);//祖父节点

		XRBTreeNode* LPuncle =NULL;//叔叔节点
		if (LPpater == XBTree_GetLChild(LPgrandpa))//父节点是祖父的左孩子
		{
			LPuncle = XBTree_GetRChild(LPgrandpa);
			if (LPuncle!=NULL&&XRBTree_IsRed(LPuncle))//叔叔节点是红色
			{
				XRBTree_SetBlack(LPpater);
				XRBTree_SetBlack(LPuncle);
				XRBTree_SetRed(LPgrandpa);
				currentNode = LPgrandpa;
				con
			}
			else//叔叔节点是黑色
			{

			}
		}
		else
		{
			LPuncle = XBTree_GetLChild(LPgrandpa);
		}
		
		return;//处理方法：那么，该情况与红黑树的“特性(5)”相冲突。
	}

}
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(less, "")))
		return NULL;
	if (isNULL(isNULLInfo(LPData, "")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "")))
		return NULL;
	XRBTreeNode* node = XRBTree_creation(TypeSize);//创建一个红黑树节点并且初始化
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	bool flag = XBBTree_insertAlign(this_root, node, less, LPData, TypeSize);//将数据插入到节点，并且链接
	if (isNULL(isNULLInfo(flag, "节点插入失败")))
		return NULL;
		
	return node;
}