#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
//删除的是叶子节点
static void* leaf_erase(TreeNodeBalance** this_root, TreeNodeBalance* currentNode)
{

	if (currentNode->parent == NULL)//是叶子也是根
	{
		XBinaryTreeObject_freeNode(currentNode, true);
		*this_root = NULL;
		return;
	}
	TreeNodeBalance* temp = currentNode->parent;
	XBinaryTreeObject_freeNode(currentNode, true);
	XBalancedBinaryTree_SetLayerNumberAll(this_root, temp);
}
//删除的是只有一个孩子
static void* OneChild_erase(TreeNodeBalance** this_root, TreeNodeBalance* currentNode)
{
	TreeNodeBalance* ChildNode = NULL;//孩子节点
	if (currentNode->leftChild != NULL)
		ChildNode = currentNode->leftChild;
	if (currentNode->rightChild != NULL)
		ChildNode = currentNode->rightChild;
	
	if (currentNode== *this_root)//也是根
	{
		XBinaryTreeObject_freeNode(currentNode, true);
		*this_root = ChildNode;
		return;
	}
	TreeNodeBalance* currentParent = currentNode->parent;//要删除节点的父节点
	TreeNodeBalance** temp = XBinaryTreeObject_findChildisParent(currentParent, currentNode);
	XBinaryTreeObject_freeNode(currentNode, true);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	ChildNode->parent = currentParent;//孩子的指针也指向新的父节点
	XBalancedBinaryTree_SetLayerNumberAll(this_root, currentParent);
}
void* XBalancedBinaryTree_erase(TreeNodeBalance** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_erase-this_root"))
		return NULL;
	TreeNodeBalance* findRet = XBalancedBinaryTree_find(*this_root, less, equality, LPData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (findRet->leftChild != NULL)
		++count;
	if (findRet->rightChild != NULL)
		++count;
	if(count==0)//叶子
		leaf_erase(this_root,findRet);
	else if (count == 1)//一个孩子
		OneChild_erase(this_root, findRet);
}