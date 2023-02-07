#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"

TreeNodeBalance* XBalancedBinaryTree_insert(TreeNodeBalance** this_root, XLess less, const void* LPData, const size_t TypeSize)
{
	if (isObjectNULL(less, "TreeNodeBalance_insert-less"))
		return NULL;
	if (isObjectNULL(LPData, "TreeNodeBalance_insert-LPData"))
		return NULL;
	if (isObjectNULL(TypeSize, "TreeNodeBalance_insert-TypeSize"))
		return NULL;
	//创建一个新的节点
	TreeNodeBalance* NewNode = XBalancedBinaryTree_creation(TypeSize);
	if (!XBinaryTreeObject_insertData(NewNode, LPData, TypeSize))//插入数据
	{
		XBinaryTreeObject_freeNode(NewNode, false);//插入失败释放创建的节点
		return NULL;
	}
	if (this_root == NULL)//如果没有根节点
	{
		return NewNode;
	}
	//开始遍历,插入节点
	size_t currentHeight = 0;//当前高度
	TreeNodeBalance* currentNode = *this_root;
	while (currentNode != NULL)
	{
		//满足小于往左边放
		if (less(NewNode->data, (currentNode)->data))
		{
			if ((currentNode)->leftChild == NULL)//建立关系
			{
				(currentNode)->leftChild = NewNode;
				NewNode->parent = currentNode;
				break;
			}
			else
			{
				currentNode = (currentNode)->leftChild;
			}
		}
		else//满足大于等于的情况
		{
			if ((currentNode)->rightChild == NULL)//建立关系
			{
				(currentNode)->rightChild = NewNode;
				NewNode->parent = currentNode;
				break;
			}
			else
			{
				currentNode = (currentNode)->rightChild;
			}
		}
	}
	XBalancedBinaryTree_SetLayerNumberAll(this_root, currentNode);
	return NewNode;
}