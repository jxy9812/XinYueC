#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<string.h>
//删除的是叶子节点
static void* leaf_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{

	if (XBTreeGetParent(eraseNode)== NULL)//是叶子也是根
	{
		XBinaryTreeObject_freeNode(eraseNode, false);
		*this_root = NULL;
		return;
	}
	XBBTreeNode* temp = XBTreeGetParent(eraseNode);
	XBinaryTreeObject_freeNode(eraseNode, true);
	XBalancedBinaryTree_SetLayerNumberAll(this_root, temp);
}
//删除的是只有一个孩子
static void* OneChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* ChildNode = NULL;//孩子节点
	XBBTreeNode* eraseLeft = XBTreeGetLChild(eraseNode);
	XBBTreeNode* eraseRight = XBTreeGetRChild(eraseNode);
	if (eraseLeft != NULL)
		ChildNode = eraseLeft;
	if (eraseRight != NULL)
		ChildNode = eraseRight;
	
	if (eraseNode== *this_root)//也是根
	{
		XBinaryTreeObject_freeNode(eraseNode, true);
		*this_root = ChildNode;
		return;
	}
	XBBTreeNode* currentParent = XBTreeGetParent(eraseNode);//要删除节点的父节点
	XBBTreeNode** temp = XBinaryTreeObject_findChildisParent(eraseNode);
	XBinaryTreeObject_freeNode(eraseNode, true);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	XBTreeGetParent(ChildNode) = currentParent;//孩子的指针也指向新的父节点
	XBalancedBinaryTree_SetLayerNumberAll(this_root, currentParent);
}
//删除的是有两个孩子
static void* TwoChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* LeftChildNode = XBTreeGetLChild(eraseNode);//左孩子节点
	XBBTreeNode* rightChildNode = XBTreeGetRChild(eraseNode);//右孩子节点
	XBBTreeNode* preCursor = LeftChildNode; 
	XBBTreeNode* LPparent = eraseNode;
	

	if (XBTreeGetRChild(preCursor)== NULL)//LeftChildNode的孩子不存在右子树的情况
	{
		free(eraseNode->XBTNode.data);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.data = preCursor->XBTNode.data;
		preCursor->XBTNode.data = NULL;

		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeGetLChild(preCursor);//左子树的左子树
		XBinaryTreeObject_freeNode(freeNode, false);
		XBTreeGetLChild(LPparent) = preCursor;//开始建立新的父子关系
		if (preCursor != NULL)
			XBTreeGetParent(preCursor) = LPparent;
	}
	else
	{
		while (XBTreeGetRChild(preCursor) != NULL)//向右边找
		{
			preCursor = XBTreeGetRChild(preCursor);
		}
		free(eraseNode->XBTNode.data);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.data = preCursor->XBTNode.data;
		preCursor->XBTNode.data = NULL;

		LPparent = XBTreeGetParent(preCursor);
		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeGetLChild(preCursor);//左子树的左子树
		XBinaryTreeObject_freeNode(freeNode, false);
		XBTreeGetRChild(LPparent) = preCursor;//开始建立新的父子关系
		if (preCursor != NULL)
			XBTreeGetParent(preCursor) = LPparent;
	}
	
	XBalancedBinaryTree_SetLayerNumberAll(this_root, LPparent);

}

void* XBalancedBinaryTree_erase(XBBTreeNode** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode* findRet = XBalancedBinaryTree_find(*this_root, less, equality, LPData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTreeGetLChild(findRet) != NULL)
		++count;
	if (XBTreeGetRChild(findRet) != NULL)
		++count;
	if(count==0)//叶子
		leaf_erase(this_root,findRet);
	if (count == 1)//一个孩子
		OneChild_erase(this_root, findRet);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findRet);
}