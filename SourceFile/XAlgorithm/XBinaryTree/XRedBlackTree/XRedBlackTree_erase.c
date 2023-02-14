#include"XRedBlackTree.h"
#include"XBalancedBinaryTree.h"
//删除调整树
static void eraseAdjustTree(XRBTreeNode** this_root, XRBTreeNode* node, XRBTreeNode* LPpater)
{
	XRBTreeNode* LPbrother = NULL;//兄弟节点
	while ((node==NULL||XRBTree_IsBlack(node))&& node!= *this_root)
	{
		if(XBTree_GetLChild(LPpater)==node)
		{
			LPbrother = XBTree_GetRChild(LPpater);
			if (LPbrother!=NULL&&XRBTree_IsRed(LPbrother))
			{
				XRBTree_SetBlack(LPbrother);
				XRBTree_SetRed(LPpater);
				XBTree_SpinLL(this_root, LPpater);
				LPbrother = XBTree_GetRChild(LPpater);
			}
			if (LPbrother!=NULL)
			{
				XRBTreeNode* LPbrotherLChild = XBTree_GetLChild(LPbrother);//兄弟节点的左孩子
				XRBTreeNode* LPbrotherRChild = XBTree_GetRChild(LPbrother);//兄弟节点的右孩子
				if ((LPbrotherLChild==NULL || XRBTree_IsBlack(LPbrotherLChild))
					&& (LPbrotherRChild==NULL|| XRBTree_IsBlack(LPbrotherRChild)))
				{
					XRBTree_SetRed(LPbrother);
					node = LPpater;
					LPpater = XBTree_GetParent(node);
				}
				else
				{
					if ((XBTree_GetRChild(LPbrother) == NULL || XRBTree_IsBlack((XRBTreeNode*)XBTree_GetRChild(LPbrother))))
					{
						XRBTree_SetBlack((XRBTreeNode*)XBTree_GetLChild(LPbrother));
						XRBTree_SetRed(LPbrother);
						XBTree_SpinRR(this_root, LPbrother);
						LPbrother = XBTree_GetRChild(LPpater);
					}
					XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
					XRBTree_SetBlack(LPpater);
					XRBTree_SetBlack((XRBTreeNode*)XBTree_GetRChild(LPbrother));
					XBTree_SpinLL(this_root, LPpater);
					node = *this_root;
					break;
				}
			}
			else
			{
				node = LPpater;
			}
			
		}
		else
		{
			LPbrother = XBTree_GetLChild(LPpater);
			if (XRBTree_IsRed(LPbrother))
			{
				XRBTree_SetBlack(LPbrother);
				XRBTree_SetRed(LPpater);
				XBTree_SpinLL(this_root, LPpater);
				LPbrother = XBTree_GetLChild(LPpater);
			}
			if (LPbrother!=NULL)
			{
				XRBTreeNode* LPbrotherLChild = XBTree_GetLChild(LPbrother);//兄弟节点的左孩子
				XRBTreeNode* LPbrotherRChild = XBTree_GetRChild(LPbrother);//兄弟节点的右孩子
				if ((LPbrotherLChild == NULL || XRBTree_IsBlack(LPbrotherLChild))
					&& (LPbrotherRChild == NULL || XRBTree_IsBlack(LPbrotherRChild)))
				{
					XRBTree_SetRed(LPbrother);
					node = LPpater;
					LPpater = XBTree_GetParent(node);
				}
				else
				{
					if ((XBTree_GetLChild(LPbrother) == NULL || XRBTree_IsBlack((XRBTreeNode*)XBTree_GetLChild(LPbrother))))
					{
						XRBTree_SetBlack((XRBTreeNode*)XBTree_GetRChild(LPbrother));
						XRBTree_SetRed(LPbrother);
						XBTree_SpinLL(this_root, LPbrother);
						LPbrother = XBTree_GetLChild(LPpater);
					}
					XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
					XRBTree_SetBlack(LPpater);
					XRBTree_SetBlack((XRBTreeNode*)XBTree_GetLChild(LPbrother));
					XBTree_SpinRR(this_root, LPpater);
					node = *this_root;
					break;
				}
			}
			else
			{
				node = LPpater;
			}
		}
	}
	if (node)
	{
		XRBTree_SetBlack(node);
	}
}
//删除的是有一个孩子
static void OneChild_erase(XRBTreeNode** this_root, XRBTreeNode* eraseNode)
{
	XRBTreeNode* LPchild = XBTree_GetLChild(eraseNode);//孩子节点
	if (LPchild == NULL)
	{
		LPchild= XBTree_GetRChild(eraseNode);
	}
	XRBTreeNode* LPpater = XBTree_GetParent(eraseNode);
	
	enum XRBTreeColor color = XRBTree_GetColor(eraseNode);//颜色
	if (LPpater != NULL)
	{
		XRBTreeNode** LPpaterToEraseNode = XBTree_findChildisParent(eraseNode);//孩子在父节点位置
		*LPpaterToEraseNode = LPchild;
	}
	else
	{
		//删除节点为根节点
		*this_root = LPchild;
	}
	if (LPchild != NULL)
		XBTree_SetParent(LPchild, LPpater);
	XBTree_freeNode(eraseNode, false);
	if (color == XRBTreeBlack&& *this_root!=NULL)
	{
		//调整树：
		eraseAdjustTree(this_root, LPchild, LPpater);
	}
	
}
//删除的是有两个孩子
static void TwoChild_erase(XRBTreeNode** this_root, XRBTreeNode* eraseNode)
{
	XRBTreeNode* LPchild = NULL;//孩子节点
	XRBTreeNode* LPreplace = eraseNode;//替换节点
	XRBTreeNode* LPpater = NULL;//父节点
	
	LPreplace = XBTree_GetRChild(LPreplace);//从右子树中取最左边
	while (XBTree_GetLChild(LPreplace) != NULL)//找替换的节点
	{
		LPreplace = XBTree_GetLChild(LPreplace);
	}

	free(eraseNode->XBTNode.data);
	eraseNode->XBTNode.data = LPreplace->XBTNode.data;
	LPreplace->XBTNode.data = NULL;

	LPchild = XBTree_GetRChild(LPreplace);
	LPpater = XBTree_GetParent(LPreplace);
	enum XRBTreeColor color = XRBTree_GetColor(LPreplace);
	if (LPpater == eraseNode)
	{
		XBTree_SetRChild(LPpater, LPchild);
	}
	else
	{
		XBTree_SetLChild(LPpater, LPchild);
	}
	
	if (LPchild != NULL)
	{
		XBTree_SetParent(LPchild, LPpater);
	}
	XBTree_freeNode(LPreplace, false);
	if (color == XRBTreeBlack)
	{
		//调整树：
		eraseAdjustTree(this_root, LPchild, LPpater);
	}
	
}
XRBTreeNode* XRBTree_erase(XRBTreeNode** this_root,XLess less, XEquality equality, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XRBTreeNode* findErase = XBBTree_findData(*this_root, less, equality, LPData);//删除的节点
	if (findErase == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTree_GetLChild(findErase) != NULL)
		++count;
	if (XBTree_GetRChild(findErase) != NULL)
		++count;
	if (count <2)//零或一个孩子
		OneChild_erase(this_root, findErase);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findErase);
}