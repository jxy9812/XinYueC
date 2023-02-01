#include "XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include<stdlib.h>
#include<string.h>
TreeNode* TreeNode_creation(const size_t TypeSize)
{
	struct TreeNode* node =(struct TreeNode*)malloc(sizeof(struct TreeNode));
	if (isObjectNULL(node,"CreationTreeNode-node"))
		return NULL;
	node->data= calloc(1,TypeSize);//开辟内存并且置为0
	if (isObjectNULL(node->data, "CreationTreeNode-node->data"))
	{
		free(node);
		return NULL;
	}
	node->LeftChild = NULL;
	node->parent = NULL;
	node->RightChild = NULL;
	return node;
}

const bool TreeNode_insertData(struct TreeNode* this_node, const void* LPData, const size_t TypeSize)
{
	if (isObjectNULL(this_node, "InsertTreeNodeData-this_node"))
		return false;
	if (isObjectNULL(LPData, "InsertTreeNodeData-LPData"))
		return false;
	if (isObjectNULL(TypeSize, "InsertTreeNodeData-TypeSize"))
		return false;
	memcpy(this_node->data, LPData, TypeSize);
	return true;
}

const bool TreeNode_free(struct TreeNode* this_node , const bool parentSetNull)
{
	if (isObjectNULL(this_node, "TreeNode_free-this_node"))
		return false;
	//释放数据
	free(this_node->data);
	if(parentSetNull)
	//在父节点将指向此节点的指针置空NULL
	struct TreeNode* LPparent = this_node->parent;
	if (LPparent->LeftChild == this_node)
		LPparent->LeftChild = NULL;
	else if (LPparent->RightChild == this_node)
		LPparent->RightChild = NULL;
	//释放节点
	free(this_node);
	return true;
}

const size_t Tree_freeAll(struct TreeNode* this_root)
{
	if (isObjectNULL(this_root, "Tree_freeAll-this_root"))
		return 0;
	size_t sum = 0;//一共释放了几个节点
	XStack* stack = XStack_init("struct TreeNode*",sizeof(struct TreeNode*));
	XStack_Push(stack,&this_root);
	struct TreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct TreeNode**)XStack_top(stack);
		if(currentNode->LeftChild!=NULL)
			XStack_Push(stack, &currentNode->LeftChild);
		if (currentNode->RightChild != NULL)
			XStack_Push(stack, &currentNode->RightChild);
	}
	return sum;
}
