#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include<string.h>
//删除的是叶子节点
static void* leaf_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{

	if (XBTREE_GET_PARENT(eraseNode)== NULL)//是叶子也是根
	{
		XBTree_freeNode(eraseNode, false);
		*this_root = NULL;
		return;
	}
	XBBTreeNode* temp = XBTREE_GET_PARENT(eraseNode);
	XBTree_freeNode(eraseNode, true);
	XBBTree_SetLayerNumberAll(this_root, temp);
}
//删除的是只有一个孩子
static void* OneChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* ChildNode = NULL;//孩子节点
	XBBTreeNode* eraseLeft = XBTREE_GET_LCHILD(eraseNode);
	XBBTreeNode* eraseRight = XBTREE_GET_RCHILD(eraseNode);
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
	XBBTreeNode* currentParent = XBTREE_GET_PARENT(eraseNode);//要删除节点的父节点
	XBBTreeNode** temp = XBTree_findChildisParent(eraseNode);
	XBTree_freeNode(eraseNode, true);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	XBTREE_GET_PARENT(ChildNode) = currentParent;//孩子的指针也指向新的父节点
	XBBTree_SetLayerNumberAll(this_root, currentParent);
}
//删除的是有两个孩子
static void* TwoChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* LeftChildNode = XBTREE_GET_LCHILD(eraseNode);//左孩子节点
	XBBTreeNode* rightChildNode = XBTREE_GET_RCHILD(eraseNode);//右孩子节点
	XBBTreeNode* preCursor = LeftChildNode; 
	XBBTreeNode* LPparent = eraseNode;
	

	if (XBTREE_GET_RCHILD(preCursor)== NULL)//LeftChildNode的孩子不存在右子树的情况
	{
		free(eraseNode->XBTNode.data);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.data = preCursor->XBTNode.data;
		preCursor->XBTNode.data = NULL;

		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTREE_GET_LCHILD(preCursor);//左子树的左子树
		XBTree_freeNode(freeNode, false);
		XBTREE_SET_LCHILD(LPparent,preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTREE_SET_PARENT(preCursor,LPparent);
	}
	else
	{
		while (XBTREE_GET_RCHILD(preCursor) != NULL)//向右边找
		{
			preCursor = XBTREE_GET_RCHILD(preCursor);
		}
		free(eraseNode->XBTNode.data);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.data = preCursor->XBTNode.data;
		preCursor->XBTNode.data = NULL;

		LPparent = XBTREE_GET_PARENT(preCursor);
		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTREE_GET_LCHILD(preCursor);//左子树的左子树
		XBTree_freeNode(freeNode, false);
		XBTREE_SET_RCHILD(LPparent,preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTREE_SET_PARENT(preCursor,LPparent);
	}
	
	XBBTree_SetLayerNumberAll(this_root, LPparent);

}

void* XBBTree_erase(XBBTreeNode** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	XBBTreeNode* findRet = XBBTree_findData(*this_root, less, equality, LPData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTREE_GET_LCHILD(findRet) != NULL)
		++count;
	if (XBTREE_GET_RCHILD(findRet) != NULL)
		++count;
	if(count==0)//叶子
		leaf_erase(this_root,findRet);
	if (count == 1)//一个孩子
		OneChild_erase(this_root, findRet);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findRet);
}