#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<string.h>
//删除的是叶子节点
static void* leaf_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{

	if (XBTree_GetParent(eraseNode)== NULL)//是叶子也是根
	{
		XBTree_freeNode(eraseNode, false);
		*this_root = NULL;
		return;
	}
	XBBTreeNode* temp = XBTree_GetParent(eraseNode);
	XBTree_freeNode(eraseNode, true);
	XBBTree_SetLayerNumberAll(this_root, temp);
}
//删除的是只有一个孩子
static void* OneChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* ChildNode = NULL;//孩子节点
	XBBTreeNode* eraseLeft = XBTree_GetLChild(eraseNode);
	XBBTreeNode* eraseRight = XBTree_GetRChild(eraseNode);
	if (eraseLeft != NULL)
		ChildNode = eraseLeft;
	if (eraseRight != NULL)
		ChildNode = eraseRight;
	
	if (eraseNode== *this_root)//也是根
	{
		XBTree_freeNode(eraseNode, true);
		*this_root = ChildNode;
		return;
	}
	XBBTreeNode* currentParent = XBTree_GetParent(eraseNode);//要删除节点的父节点
	XBBTreeNode** temp = XBTree_findChildisParent(eraseNode);
	XBTree_freeNode(eraseNode, true);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	XBTree_GetParent(ChildNode) = currentParent;//孩子的指针也指向新的父节点
	XBBTree_SetLayerNumberAll(this_root, currentParent);
}
//删除的是有两个孩子
static void* TwoChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* LeftChildNode = XBTree_GetLChild(eraseNode);//左孩子节点
	XBBTreeNode* rightChildNode = XBTree_GetRChild(eraseNode);//右孩子节点
	XBBTreeNode* preCursor = LeftChildNode; 
	XBBTreeNode* LPparent = eraseNode;
	

	if (XBTree_GetRChild(preCursor)== NULL)//LeftChildNode的孩子不存在右子树的情况
	{
		free(eraseNode->XBTNode.value);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.value = preCursor->XBTNode.value;
		preCursor->XBTNode.value = NULL;

		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTree_GetLChild(preCursor);//左子树的左子树
		XBTree_freeNode(freeNode, false);
		XBTree_SetLChild(LPparent,preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTree_SetParent(preCursor,LPparent);
	}
	else
	{
		while (XBTree_GetRChild(preCursor) != NULL)//向右边找
		{
			preCursor = XBTree_GetRChild(preCursor);
		}
		free(eraseNode->XBTNode.value);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.value = preCursor->XBTNode.value;
		preCursor->XBTNode.value = NULL;

		LPparent = XBTree_GetParent(preCursor);
		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTree_GetLChild(preCursor);//左子树的左子树
		XBTree_freeNode(freeNode, false);
		XBTree_SetRChild(LPparent,preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTree_SetParent(preCursor,LPparent);
	}
	
	XBBTree_SetLayerNumberAll(this_root, LPparent);

}

void* XBBTree_erase(XBBTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule,const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode* findRet = XBBTree_findData(*this_root, less,equality, Rule, LPData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTree_GetLChild(findRet) != NULL)
		++count;
	if (XBTree_GetRChild(findRet) != NULL)
		++count;
	if(count==0)//叶子
		leaf_erase(this_root,findRet);
	if (count == 1)//一个孩子
		OneChild_erase(this_root, findRet);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findRet);
}