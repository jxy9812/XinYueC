#include"XRedBlackTree.h"
#include"XBalancedBinaryTree.h"
static void traverse(void* LPVal, void* args)
{
	struct XRBTreeNode* currentNode = *(struct XRBTreeNode**)LPVal;
	printf("%-3d ", XBTree_GetData(LPVal, int));
	if (*XBTree_GetTreeNode(currentNode, XBTreeParent) == NULL)
		return;
	if (XBTree_findChildisParent(currentNode) == NULL)
		printf("找不到：%d \n\n\n\n\n\n", *(int*)currentNode->XBTNode.data);

}
//删除调整树
static void eraseAdjustTree(XRBTreeNode** this_root, XRBTreeNode* node, XRBTreeNode* LPpater)
{
	printf("调整前:");
	XVector* TreePreorder = XBTree_TraversingToXVector(*this_root, XBTreeInorder);
	XVector_iterator_for_each(TreePreorder, traverse, NULL);
	printf("\t");
	XVector_free(TreePreorder);
	//XRBTreeNode* LPpater = NULL;//父节点
	//if (node != NULL)
	//	LPpater=XBTree_GetParent(node);
	XRBTreeNode* LPbrother = NULL;//兄弟节点
	while (!node||XRBTree_IsBlack(node)&& node!= *this_root)
	{
		if(XBTree_GetLChild(LPpater)==node)
		{
			LPbrother = XBTree_GetRChild(LPpater);
			if (LPbrother!=NULL&&XRBTree_IsRed(LPbrother))
			{
				XRBTree_SetBlack(LPbrother);
				XRBTree_SetRed(LPpater);
				XBTree_SpinLL(LPpater);
				LPbrother = XBTree_GetRChild(LPpater);
			}
			if (LPbrother)
			{
				XRBTreeNode* LPbrotherLChild = XBTree_GetLChild(LPbrother);
				XRBTreeNode* LPbrotherRChild = XBTree_GetRChild(LPbrother);
				if (!LPbrotherLChild || XRBTree_IsBlack(LPbrotherLChild)
					&& !LPbrotherRChild|| XRBTree_IsBlack(LPbrotherRChild))
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
						XBTree_SpinRR(LPbrother);
						LPbrother = XBTree_GetRChild(LPpater);
					}
					XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
					XRBTree_SetBlack(LPpater);
					XRBTree_SetBlack((XRBTreeNode*)XBTree_GetRChild(LPbrother));
					XBTree_SpinLL(LPpater);
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
				XBTree_SpinLL(LPpater);
				LPbrother = XBTree_GetLChild(LPpater);
			}
			if ((XBTree_GetLChild(LPbrother) == NULL || XRBTree_IsBlack((XRBTreeNode*)XBTree_GetLChild(LPbrother))) && (XBTree_GetRChild(LPbrother) == NULL || XRBTree_IsBlack((XRBTreeNode*)XBTree_GetRChild(LPbrother))))
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
					XBTree_SpinLL(LPbrother);
					LPbrother = XBTree_GetLChild(LPpater);
				}
				XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
				XRBTree_SetBlack(LPpater);
				XRBTree_SetBlack((XRBTreeNode*)XBTree_GetLChild(LPbrother));
				XBTree_SpinRR(LPpater);
				node = *this_root;
				break;
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
	
	//free(eraseNode->XBTNode.data);
	/*eraseNode->XBTNode.data = LPchild->XBTNode.data;
	LPreplace->XBTNode.data = NULL;*/
	enum XRBTreeColor color = XRBTree_GetColor(eraseNode);//颜色
	if (LPpater != NULL)
	{
		XRBTreeNode** LPpaterToEraseNode = XBTree_findChildisParent(eraseNode);//孩子在父节点位置
		*LPpaterToEraseNode = LPchild;
		
		/*if (XBTree_GetLChild(LPpater) == eraseNode)
		{
			XBTree_SetLChild(LPpater, LPchild);
		}
		else
		{
			XBTree_SetRChild(LPpater, LPchild);
		}*/
	}
	else
	{
		//删除节点为根节点
		*this_root = LPchild;
	}
	if (LPchild != NULL)
		XBTree_SetParent(LPchild, LPpater);
	XBTree_freeNode(eraseNode, false);
	if (color == XRBTreeBlack)
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
	//LPpater = XBTree_GetParent(eraseNode);

	free(eraseNode->XBTNode.data);
	eraseNode->XBTNode.data = LPreplace->XBTNode.data;
	LPreplace->XBTNode.data = NULL;

	if (LPpater != NULL)//删除的节点是不是根节点
	{
		//不是根节点
		/*if (XBTree_GetLChild(LPpater) == eraseNode)
		{
			XBTree_SetLChild(LPpater, LPreplace);
		}
		else
		{
			XBTree_SetRChild(LPpater, LPreplace);
		}*/
	}
	else
	{
		//*this_root = LPreplace;
	}
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
	//if (LPpater == eraseNode)
	//{
	//	LPpater = LPreplace;
	//}
	//else
	//{
	//	if (LPchild != NULL)
	//	{
	//		XBTree_SetParent(LPchild, LPpater);
	//	}
	//	XBTree_SetLChild(LPpater, LPchild);
	//	XBTree_SetRChild(LPreplace, XBTree_GetRChild(eraseNode));
	//	XBTree_SetParent(XBTree_GetRChild(eraseNode), LPreplace);
	//}
	////调整替换节点父节点
	//XBTree_SetParent(LPreplace, XBTree_GetParent(eraseNode));
	//XRBTree_SetColor(LPreplace, XRBTree_GetColor(eraseNode));
	//XBTree_SetLChild(LPreplace, XBTree_GetLChild(eraseNode));
	////调整删除节点父节点
	//XBTree_SetParent(XBTree_GetLChild(eraseNode), LPreplace);

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
	printf("调整后:");
	XVector* TreePreorder = XBTree_TraversingToXVector(*this_root, XBTreeInorder);
	XVector_iterator_for_each(TreePreorder, traverse, NULL);
	printf("\n");
	XVector_free(TreePreorder);
}