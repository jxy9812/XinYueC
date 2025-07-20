#include"XRedBlackTree.h"
#include"XClass.h"
#include"XBalancedBinaryTree.h"
XRBTreeNode* XRBTree_create(const size_t dataTypeSize)
{
	XRBTreeNode* node = XMemory_malloc(sizeof(XRBTreeNode));
	if (ISNULL(node, "创建红黑树节点失败"))
		return NULL;
	XRBTree_init(node,dataTypeSize);
	return node;
}
void XRBTree_init(XRBTreeNode* this_root, const size_t dataTypeSize)
{
	if (this_root == NULL)
		return;
	XBTreeNode_init(this_root, dataTypeSize);
	XRBTree_SetRed(this_root);
}
//当前节点的父节点是红色，且当前节点的祖父节点的另一一个子节点(叔叔节点)也是红色
static bool XRBTree_AdjustNoOne(XRBTreeNode** currentNode, XRBTreeNode* LPpater, XRBTreeNode* LPgrandpa, XRBTreeNode* LPuncle)
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
/*                                                  删除函数                                          */
//删除调整树
static void eraseAdjustTree(XRBTreeNode** this_root, XRBTreeNode* nodes, XRBTreeNode* LPpater)
{
	XRBTreeNode* LPbrother = NULL;//兄弟节点
	while ((nodes == NULL || XRBTree_IsBlack(nodes)) && nodes != *this_root)
	{
		if (XBTreeNode_GetLChild(LPpater) == nodes)
		{
			LPbrother = XBTreeNode_GetRChild(LPpater);
			if (LPbrother != NULL && XRBTree_IsRed(LPbrother))
			{
				XRBTree_SetBlack(LPbrother);
				XRBTree_SetRed(LPpater);
				XBTree_SpinLL(this_root, LPpater);
				LPbrother = XBTreeNode_GetRChild(LPpater);
			}
			if (LPbrother != NULL)
			{
				XRBTreeNode* LPbrotherLChild = XBTreeNode_GetLChild(LPbrother);//兄弟节点的左孩子
				XRBTreeNode* LPbrotherRChild = XBTreeNode_GetRChild(LPbrother);//兄弟节点的右孩子
				if ((LPbrotherLChild == NULL || XRBTree_IsBlack(LPbrotherLChild))
					&& (LPbrotherRChild == NULL || XRBTree_IsBlack(LPbrotherRChild)))
				{
					XRBTree_SetRed(LPbrother);
					nodes = LPpater;
					LPpater = XBTreeNode_GetParent(nodes);
				}
				else
				{
					if ((XBTreeNode_GetRChild(LPbrother) == NULL || XRBTree_IsBlack((XRBTreeNode*)XBTreeNode_GetRChild(LPbrother))))
					{
						XRBTree_SetBlack((XRBTreeNode*)XBTreeNode_GetLChild(LPbrother));
						XRBTree_SetRed(LPbrother);
						XBTree_SpinRR(this_root, LPbrother);
						LPbrother = XBTreeNode_GetRChild(LPpater);
					}
					XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
					XRBTree_SetBlack(LPpater);
					XRBTree_SetBlack((XRBTreeNode*)XBTreeNode_GetRChild(LPbrother));
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
			LPbrother = XBTreeNode_GetLChild(LPpater);
			if (XRBTree_IsRed(LPbrother))
			{
				XRBTree_SetBlack(LPbrother);
				XRBTree_SetRed(LPpater);
				XBTree_SpinLL(this_root, LPpater);
				LPbrother = XBTreeNode_GetLChild(LPpater);
			}
			if (LPbrother != NULL)
			{
				XRBTreeNode* LPbrotherLChild = XBTreeNode_GetLChild(LPbrother);//兄弟节点的左孩子
				XRBTreeNode* LPbrotherRChild = XBTreeNode_GetRChild(LPbrother);//兄弟节点的右孩子
				if ((LPbrotherLChild == NULL || XRBTree_IsBlack(LPbrotherLChild))
					&& (LPbrotherRChild == NULL || XRBTree_IsBlack(LPbrotherRChild)))
				{
					XRBTree_SetRed(LPbrother);
					nodes = LPpater;
					LPpater = XBTreeNode_GetParent(nodes);
				}
				else
				{
					if ((XBTreeNode_GetLChild(LPbrother) == NULL || XRBTree_IsBlack((XRBTreeNode*)XBTreeNode_GetLChild(LPbrother))))
					{
						XRBTree_SetBlack((XRBTreeNode*)XBTreeNode_GetRChild(LPbrother));
						XRBTree_SetRed(LPbrother);
						XBTree_SpinLL(this_root, LPbrother);
						LPbrother = XBTreeNode_GetLChild(LPpater);
					}
					XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
					XRBTree_SetBlack(LPpater);
					XRBTree_SetBlack((XRBTreeNode*)XBTreeNode_GetLChild(LPbrother));
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
	XRBTreeNode* Pchild = XBTreeNode_GetLChild(eraseNode);//孩子节点
	if (Pchild == NULL)
	{
		Pchild = XBTreeNode_GetRChild(eraseNode);
	}
	XRBTreeNode* parent = XBTreeNode_GetParent(eraseNode);//获取父节点

	enum XRBTreeColor color = XRBTree_GetColor(eraseNode);//颜色
	if (parent != NULL)
	{
		XRBTreeNode** LPpaterToEraseNode = XTreeNode_getChildrenParentRef(eraseNode);//孩子在父节点位置
		*LPpaterToEraseNode = Pchild;
	}
	else
	{
		//删除节点为根节点
		*this_root = Pchild;
	}
	if (Pchild != NULL)
		XBTreeNode_SetParent(Pchild, parent);
	XTreeNode_delete(eraseNode);
	if (color == XRBTreeBlack && *this_root != NULL)
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

	LPreplace = XBTreeNode_GetRChild(LPreplace);//从右子树中取最左边
	while (XBTreeNode_GetLChild(LPreplace) != NULL)//找替换的节点
	{
		LPreplace = XBTreeNode_GetLChild(LPreplace);
	}
	if (XTreeNode_GetDataPtr(eraseNode))
		XMemory_free(XTreeNode_GetDataPtr(eraseNode));
	XTreeNode_SetDataPtr(eraseNode, XTreeNode_GetDataPtr(LPreplace));
	//eraseNode->XBTNode.value = LPreplace->XBTNode.value;
	XTreeNode_SetDataTypeSize(eraseNode, XTreeNode_GetDataTypeSize(LPreplace));
	//eraseNode->XBTNode.valueTypeSize = LPreplace->XBTNode.valueTypeSize;
	XTreeNode_SetDataPtr(LPreplace, NULL);
	//LPreplace->XBTNode.value = NULL;

	LPchild = XBTreeNode_GetRChild(LPreplace);
	LPparent = XBTreeNode_GetParent(LPreplace);
	enum XRBTreeColor color = XRBTree_GetColor(LPreplace);
	if (LPparent == eraseNode)
	{
		XBTreeNode_SetRChild(LPparent, LPchild);
	}
	else
	{
		XBTreeNode_SetLChild(LPparent, LPchild);
	}

	if (LPchild != NULL)
	{
		XBTreeNode_SetParent(LPchild, LPparent);
	}

	XBBTreeNode** parent = XTreeNode_getChildrenParentRef(LPreplace);
	if (parent)
		*parent = NULL;
	XTreeNode_delete(LPreplace);
	if (color == XRBTreeBlack)
	{
		//调整树：
		eraseAdjustTree(this_root, LPchild, LPparent);
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
XRBTreeNode* XRBTree_erase(XRBTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule, const void* pvData)
{
	if (ISNULL(this_root, ""))
		return NULL;
	XRBTreeNode* findErase = XBBTree_findData(*this_root, less, equality, Rule, pvData);//删除的节点
	//DEBUG_PRINTF("findErase=%p", findErase);
	if (findErase == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTreeNode_GetLChild(findErase) != NULL)
		++count;
	if (XBTreeNode_GetRChild(findErase) != NULL)
		++count;
	if (count < 2)//零或一个孩子
		OneChild_erase(this_root, findErase);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findErase);
	return findErase;
}

XRBTreeNode* XRBTree_findData(XRBTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne equalityRule, void* pvData)
{
	return XBBTree_findData(this_root,less,equality,equalityRule,pvData);
	//if (this_root == NULL)//树是空的
	//	return NULL;
	//XRBTreeNode* CurNode = this_root;//当前节点指针
	//while (CurNode != NULL)
	//{
	//	if (equalityRule(equality, CurNode, pvData))
	//	{
	//		return CurNode;
	//	}
	//	else if (equalityRule(less, CurNode, pvData))
	//	{
	//		CurNode = XBTreeNode_GetRChild(CurNode);
	//	}
	//	else
	//	{
	//		CurNode = XBTreeNode_GetLChild(CurNode);
	//	}
	//}
	//return NULL;
}
 
/*                                                  插入函数                                           */
//调整成为红黑树
static void XRBTree_insertAdjust(XRBTreeNode** this_root, XRBTreeNode* currentNode)
{
	XRBTreeNode* LPpater = NULL;//父节点
	XRBTreeNode* LPgrandpa = NULL;//祖父节点
	XRBTreeNode* LPuncle = NULL;//叔叔节点
	while ((LPpater = XBTreeNode_GetParent(currentNode)) && XRBTree_IsRed(LPpater))
	{
		LPgrandpa = XBTreeNode_GetParent(LPpater);
		if (LPpater == XBTreeNode_GetLChild(LPgrandpa))//父节点是祖父的左孩子
		{
			LPuncle = XBTreeNode_GetRChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle))//叔叔节点是红色
				continue;
			//NO.2当前节点是其父节点的右孩子
			if (currentNode == XBTreeNode_GetRChild(LPpater))
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
			LPuncle = XBTreeNode_GetLChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle))//叔叔节点是红色
				continue;
			//NO.2当前节点是其父节点的左孩子
			if (currentNode == XBTreeNode_GetLChild(LPpater))
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
		//LPpater = XBTreeNode_GetParent(currentNode);
	}
	XRBTree_SetBlack(*this_root);
}
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* pvData, const size_t TypeSize)
{
	//DEBUG_PRINTF("less=%p pvData=%p dataTypeSize=%u\n",less,pvData,dataTypeSize);
	if (ISNULL(less, ""))
		return NULL;
	if (ISNULL(pvData, ""))
		return NULL;
	if (ISNULL(TypeSize, ""))
		return NULL;
	XRBTreeNode* nodes = XRBTree_create(TypeSize);//创建一个红黑树节点并且初始化,默认红色
	if (ISNULL(nodes, ""))
		return NULL;
	//DEBUG_PRINTF("nodes=%p\n",nodes);
	bool flag = XBBTree_insertAlign(this_root, nodes, less, lessRule, pvData, TypeSize);//将数据插入到节点，并且链接
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