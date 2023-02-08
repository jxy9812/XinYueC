#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
#include<string.h>
//删除的是叶子节点
static void* leaf_erase(TreeNodeBalance** this_root, TreeNodeBalance* eraseNode)
{

	if (eraseNode->parent == NULL)//是叶子也是根
	{
		XBinaryTreeObject_freeNode(eraseNode, true);
		*this_root = NULL;
		return;
	}
	TreeNodeBalance* temp = eraseNode->parent;
	XBinaryTreeObject_freeNode(eraseNode, true);
	XBalancedBinaryTree_SetLayerNumberAll(this_root, temp);
}
//删除的是只有一个孩子
static void* OneChild_erase(TreeNodeBalance** this_root, TreeNodeBalance* eraseNode)
{
	TreeNodeBalance* ChildNode = NULL;//孩子节点
	if (eraseNode->leftChild != NULL)
		ChildNode = eraseNode->leftChild;
	if (eraseNode->rightChild != NULL)
		ChildNode = eraseNode->rightChild;
	
	if (eraseNode== *this_root)//也是根
	{
		XBinaryTreeObject_freeNode(eraseNode, true);
		*this_root = ChildNode;
		return;
	}
	TreeNodeBalance* currentParent = eraseNode->parent;//要删除节点的父节点
	TreeNodeBalance** temp = XBinaryTreeObject_findChildisParent(eraseNode);
	XBinaryTreeObject_freeNode(eraseNode, true);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	ChildNode->parent = currentParent;//孩子的指针也指向新的父节点
	XBalancedBinaryTree_SetLayerNumberAll(this_root, currentParent);
}
//删除的是有两个孩子
static void* TwoChild_erase(TreeNodeBalance** this_root, TreeNodeBalance* eraseNode)
{
	TreeNodeBalance* LeftChildNode = eraseNode->leftChild;//左孩子节点
	TreeNodeBalance* rightChildNode = eraseNode->rightChild;//右孩子节点
	TreeNodeBalance* preCursor = LeftChildNode; 
	TreeNodeBalance* LPparent = eraseNode;
	

	if (preCursor->rightChild == NULL)//LeftChildNode的孩子不存在右子树的情况
	{
		free(eraseNode->data);//释放其数据
		//与左子树数据交换
		eraseNode->data = preCursor->data;
		preCursor->data = NULL;

		TreeNodeBalance* freeNode = preCursor;
		preCursor = preCursor->leftChild;//左子树的左子树
		XBinaryTreeObject_freeNode(freeNode, false);
		LPparent->leftChild = preCursor;//开始建立新的父子关系
		if (preCursor != NULL)
			preCursor->parent = LPparent;
	}
	else
	{
		while (preCursor->rightChild!=NULL)//向右边找
		{
			preCursor = preCursor->rightChild;
		}
		free(eraseNode->data);//释放其数据
		//与左子树数据交换
		eraseNode->data = preCursor->data;
		preCursor->data = NULL;

		LPparent = preCursor->parent;
		TreeNodeBalance* freeNode = preCursor;
		preCursor = preCursor->leftChild;//左子树的左子树
		XBinaryTreeObject_freeNode(freeNode, false);
		LPparent->rightChild = preCursor;//开始建立新的父子关系
		if (preCursor != NULL)
			preCursor->parent = LPparent;
	}
	
	XBalancedBinaryTree_SetLayerNumberAll(this_root, LPparent);
	//if (currentNode == *this_root)//也是根
	//{
	//	XBinaryTreeObject_freeNode(currentNode, true);
	//	*this_root = ChildNode;
	//	return;
	//}
	//TreeNodeBalance* currentParent = eraseNode->parent;//要删除节点的父节点
	//TreeNodeBalance** temp = XBinaryTreeObject_findChildisParent(currentParent, eraseNode);
	//XBinaryTreeObject_freeNode(eraseNode, true);
	//*temp = ChildNode;//将父节点的指针指向新的孩子;
	//ChildNode->parent = currentParent;//孩子的指针也指向新的父节点
	//XBalancedBinaryTree_SetLayerNumberAll(this_root, currentParent);
}

void* XBalancedBinaryTree_erase(TreeNodeBalance** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	TreeNodeBalance* findRet = XBalancedBinaryTree_find(*this_root, less, equality, LPData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (findRet->leftChild != NULL)
		++count;
	if (findRet->rightChild != NULL)
		++count;
	//if(count==0)//叶子
	//	leaf_erase(this_root,findRet);
	//if (count == 1)//一个孩子
	//	OneChild_erase(this_root, findRet);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findRet);
}