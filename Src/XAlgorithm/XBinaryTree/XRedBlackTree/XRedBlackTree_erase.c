#include"XRedBlackTree.h"
#include"XClass.h"
#include"XBalancedBinaryTree.h"
//删除调整树
static void eraseAdjustTree(XRBTreeNode** this_root, XRBTreeNode* nodes, XRBTreeNode* LPpater)
{
	XRBTreeNode* LPbrother = NULL;//兄弟节点
	while ((nodes==NULL||XRBTree_IsBlack(nodes))&& nodes!= *this_root)
	{
		if(XBTree_GetLChild(LPpater)==nodes)
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
					nodes = LPpater;
					LPpater = XBTree_GetParent(nodes);
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
					nodes = *this_root;
					break;
				}
			}
			else
			{
				nodes = LPpater;
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
					nodes = LPpater;
					LPpater = XBTree_GetParent(nodes);
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
					nodes = *this_root;
					break;
				}
			}
			else
			{
				nodes = LPpater;
			}
		}
	}
	if (nodes)
	{
		XRBTree_SetBlack(nodes);
	}
}
//删除的是有一个孩子
static void OneChild_erase(XRBTreeNode** this_root, XRBTreeNode* eraseNode)
{
	XRBTreeNode* Pchild = XBTree_GetLChild(eraseNode);//孩子节点
	if (Pchild == NULL)
	{
		Pchild= XBTree_GetRChild(eraseNode);
	}
	XRBTreeNode* parent = XBTree_GetParent(eraseNode);//获取父节点
	
	enum XRBTreeColor color = XRBTree_GetColor(eraseNode);//颜色
	if (parent != NULL)
	{
		XRBTreeNode** LPpaterToEraseNode = XBTree_findChildisParent(eraseNode);//孩子在父节点位置
		*LPpaterToEraseNode = Pchild;
	}
	else
	{
		//删除节点为根节点
		*this_root = Pchild;
	}
	if (Pchild != NULL)
		XBTree_SetParent(Pchild, parent);
	XBTree_freeNode(eraseNode, false);
	if (color == XRBTreeBlack&& *this_root!=NULL)
	{
		//调整树：
		eraseAdjustTree(this_root, Pchild, parent);
	}
	
}
//删除的是有两个孩子
static void TwoChild_erase(XRBTreeNode** this_root, XRBTreeNode* eraseNode)
{
#if XVector_ON
	XRBTreeNode* LPchild = NULL;//孩子节点
	XRBTreeNode* LPreplace = eraseNode;//替换节点
	XRBTreeNode* LPparent = NULL;//父节点
	
	LPreplace = XBTree_GetRChild(LPreplace);//从右子树中取最左边
	while (XBTree_GetLChild(LPreplace) != NULL)//找替换的节点
	{
		LPreplace = XBTree_GetLChild(LPreplace);
	}
	if(eraseNode->XBTNode.values)
		XVector_delete_base(eraseNode->XBTNode.values);
	eraseNode->XBTNode.values = LPreplace->XBTNode.values;
	LPreplace->XBTNode.values = NULL;

	LPchild = XBTree_GetRChild(LPreplace);
	LPparent = XBTree_GetParent(LPreplace);
	enum XRBTreeColor color = XRBTree_GetColor(LPreplace);
	if (LPparent == eraseNode)
	{
		XBTree_SetRChild(LPparent, LPchild);
	}
	else
	{
		XBTree_SetLChild(LPparent, LPchild);
	}
	
	if (LPchild != NULL)
	{
		XBTree_SetParent(LPchild, LPparent);
	}
	XBTree_freeNode(LPreplace, true);
	if (color == XRBTreeBlack)
	{
		//调整树：
		eraseAdjustTree(this_root, LPchild, LPparent);
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
XRBTreeNode* XRBTree_erase(XRBTreeNode** this_root, XLess less,XEquality equality, XCompareRuleOne Rule, const void* LPData)
{
	if (ISNULL(this_root, ""))
		return NULL;
	XRBTreeNode* findErase = XBBTree_findData(*this_root, less,equality, Rule, LPData);//删除的节点
	//DEBUG_PRINTF("findErase=%p", findErase);
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
	return findErase;
}