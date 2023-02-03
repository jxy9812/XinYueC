#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include<math.h>

TreeNodeBalance* TreeNodeBalance_creation(const size_t TypeSize)
{
	struct TreeNodeBalance* node = TreeNode_creation(sizeof(TreeNodeBalance),TypeSize);
	node->maxLayer = 1;
	return node;
}

TreeNodeBalance* TreeNodeBalance_insert(TreeNodeBalance** this_root, compare less, const void* LPData, const size_t TypeSize)
{
	if (isObjectNULL(TypeSize, "TreeNodeBalance_insert-TypeSize"))
		return NULL;
	//创建一个新的节点
	TreeNodeBalance* NewNode = TreeNodeBalance_creation(TypeSize);
	if (!TreeNode_insertData(NewNode, LPData, TypeSize))//插入数据
	{
		TreeNode_free(NewNode, false);//插入失败释放创建的节点
		return NULL;
	}
	if (this_root == NULL)//如果没有根节点
	{
		return NewNode;
	}
	if(isObjectNULL(less,"TreeNodeBalance_insert-less"))
		return NULL;
	if (isObjectNULL(LPData, "TreeNodeBalance_insert-LPData"))
		return NULL;
	//开始遍历,插入节点
	size_t currentHeight = 0;//当前高度
	TreeNodeBalance* currentNode = *this_root;
	while (currentNode!=NULL)
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
	//循环返回父节点设置层数
	while (currentNode != NULL)
	{
		(currentNode)->maxLayer =1+ GetLayerNumberChild(currentNode);
		if (currentNode == *this_root)
			TreeNodeBalance_Spin(this_root, less, LPData);
		else
		TreeNodeBalance_Spin(&currentNode,less,LPData);
		currentNode = (currentNode)->parent;
		
	}
    return NewNode;
}

const size_t GetLayerNumberThis(const TreeNodeBalance* this_root)
{
	if (this_root != NULL)
		return this_root->maxLayer;
	return 0;
}
const size_t GetLayerNumberChild(const TreeNodeBalance* this_root)
{
	size_t left = GetLayerNumberThis(this_root->leftChild);
	size_t right = GetLayerNumberThis(this_root->rightChild);
	return (left > right) ? left : right;
}
const size_t SetLayerNumberThis(TreeNodeBalance* this_root)
{
	return this_root->maxLayer = 1 + GetLayerNumberChild(this_root);;
}
//右旋
static TreeNodeBalance*  RR(TreeNodeBalance** this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_RR"))
		return NULL;
	if (isObjectNULL(*this_root, "TreeNodeBalance_RR"))
		return NULL;
	TreeNodeBalance* root = *this_root;
	TreeNodeBalance* NewRoot = root->leftChild;
	if (root->parent != NULL)
	{
		TreeNodeBalance* root_par = root->parent;
		if (root_par->leftChild == root)
			root_par->leftChild = NewRoot;
		else if (root_par->rightChild == root)
			root_par->rightChild = NewRoot;
	}

	NewRoot->parent = root->parent;
	root->parent = NewRoot;

	root->leftChild = (NewRoot)->rightChild;
	(NewRoot)->rightChild = root;
	
	//TreeNodeBalance_Modifi_LParent(this_root->parent, this_root, NewRoot);
	SetLayerNumberThis(root);
	SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//左旋
static TreeNodeBalance* LL(TreeNodeBalance** this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_LL"))
		return NULL;
	if (isObjectNULL(*this_root, "TreeNodeBalance_LL"))
		return NULL;
	TreeNodeBalance* root = *this_root;
	TreeNodeBalance* NewRoot = root->rightChild;
	if (root->parent != NULL)
	{
		TreeNodeBalance* root_par = root->parent;
		if (root_par->leftChild == root)
			root_par->leftChild = NewRoot;
		else if (root_par->rightChild == root)
			root_par->rightChild = NewRoot;
	}

	NewRoot->parent = root->parent;
	root->parent = NewRoot;
	
	root->rightChild = (NewRoot)->leftChild;
	(NewRoot)->leftChild = root;
	
	//TreeNodeBalance_Modifi_LParent(this_root->parent, this_root, NewRoot);
	SetLayerNumberThis(root);
	SetLayerNumberThis(NewRoot);
	return NewRoot;
}
//右左旋
static TreeNodeBalance*  RL(TreeNodeBalance** this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_RL"))
		return NULL;
	(*this_root)->rightChild =RR(&((*this_root)->rightChild));
	return LL(this_root);
}
//左右旋
static TreeNodeBalance*  LR(TreeNodeBalance** this_root)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_LR"))
		return NULL;
	(*this_root)->leftChild = LL(&((*this_root)->leftChild));
	return RR(this_root);
}

void TreeNodeBalance_Spin(const TreeNodeBalance** this_root, compare less, const void* LPData)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_Spin-this_root"))
		return NULL;
	if (isObjectNULL(*this_root, "TreeNodeBalance_Spin-this_root"))
		return NULL;
	size_t leftLayer = GetLayerNumberThis((*this_root)->leftChild);
	size_t rightLayer = GetLayerNumberThis((*this_root)->rightChild);
	//TreeNodeBalance* ret = NULL;
	if (abs(leftLayer - rightLayer) > 1)//左边比右边高度大于1
	{
		if(leftLayer> rightLayer)
		{
			if (less(LPData, (*this_root)->leftChild->data))
				*this_root = RR(this_root);
			else
				*this_root = LR(this_root);
		}
		else
		{
			if (!less(LPData, (*this_root)->rightChild->data))
				*this_root = LL(this_root);
			else
				*this_root = RL(this_root);
		}

	}
}
