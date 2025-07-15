#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<string.h>
//删除的是叶子节点
static void* leaf_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{

	if (XBTreeNode_GetParent(eraseNode)== NULL)//是叶子也是根
	{
		XBTreeNode_delete(eraseNode);
		*this_root = NULL;
		return;
	}
	XBBTreeNode* temp = XBTreeNode_GetParent(eraseNode);
	
	XBBTreeNode** parent = XBTreeNode_getChildrenParentRef(eraseNode);
	if (parent)
		*parent = NULL;
	XBTreeNode_delete(eraseNode);
	XBBTree_SetLayerNumberAll(this_root, temp);
}
//删除的是只有一个孩子
static void* OneChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* ChildNode = NULL;//孩子节点
	XBBTreeNode* eraseLeft = XBTreeNode_GetLChild(eraseNode);
	XBBTreeNode* eraseRight = XBTreeNode_GetRChild(eraseNode);
	if (eraseLeft != NULL)
		ChildNode = eraseLeft;
	if (eraseRight != NULL)
		ChildNode = eraseRight;
	
	if (eraseNode== *this_root)//也是根
	{
		XBBTreeNode** parent = XBTreeNode_getChildrenParentRef(eraseNode);
		if (parent)
			*parent = NULL;
		XBTreeNode_delete(eraseNode);
		*this_root = ChildNode;
		return;
	}
	XBBTreeNode* currentParent = XBTreeNode_GetParent(eraseNode);//要删除节点的父节点
	XBBTreeNode** temp = XBTreeNode_getChildrenParentRef(eraseNode);
	
	XBBTreeNode** parent = XBTreeNode_getChildrenParentRef(eraseNode);
	if (parent)
		*parent = NULL;
	XBTreeNode_delete(eraseNode);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	XBTreeNode_GetParent(ChildNode) = currentParent;//孩子的指针也指向新的父节点
	XBBTree_SetLayerNumberAll(this_root, currentParent);
}
//删除的是有两个孩子
static void* TwoChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* LeftChildNode = XBTreeNode_GetLChild(eraseNode);//左孩子节点
	XBBTreeNode* rightChildNode = XBTreeNode_GetRChild(eraseNode);//右孩子节点
	XBBTreeNode* preCursor = LeftChildNode; 
	XBBTreeNode* LPparent = eraseNode;
	

	if (XBTreeNode_GetRChild(preCursor)== NULL)//LeftChildNode的孩子不存在右子树的情况
	{
		XMemory_free(eraseNode->XBTNode.values);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.values = preCursor->XBTNode.values;
		preCursor->XBTNode.values = NULL;

		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeNode_GetLChild(preCursor);//左子树的左子树
		XBTreeNode_delete(freeNode);
		XBTreeNode_SetLChild(LPparent,preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTreeNode_SetParent(preCursor,LPparent);
	}
	else
	{
		while (XBTreeNode_GetRChild(preCursor) != NULL)//向右边找
		{
			preCursor = XBTreeNode_GetRChild(preCursor);
		}
		XMemory_free(eraseNode->XBTNode.values);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.values = preCursor->XBTNode.values;
		preCursor->XBTNode.values = NULL;

		LPparent = XBTreeNode_GetParent(preCursor);
		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeNode_GetLChild(preCursor);//左子树的左子树
		XBTreeNode_delete(freeNode);
		XBTreeNode_SetRChild(LPparent,preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTreeNode_SetParent(preCursor,LPparent);
	}
	
	XBBTree_SetLayerNumberAll(this_root, LPparent);

}

void* XBBTree_erase(XBBTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule,const void* LPData, const size_t TypeSize)
{
	if (ISNULL(this_root, ""))
		return NULL;
	XBBTreeNode* findRet = XBBTree_findData(*this_root, less,equality, Rule, LPData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTreeNode_GetLChild(findRet) != NULL)
		++count;
	if (XBTreeNode_GetRChild(findRet) != NULL)
		++count;
	if(count==0)//叶子
		leaf_erase(this_root,findRet);
	if (count == 1)//一个孩子
		OneChild_erase(this_root, findRet);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findRet);
}