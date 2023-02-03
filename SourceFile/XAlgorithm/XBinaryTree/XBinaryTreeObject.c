#include "XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//前序
static XVector* BinaryTreeTraversingToXVector_Preorder(struct TreeNode* this_root)
{
	XVector* vector = XVector_init("struct TreeNode*", sizeof(struct TreeNode*));
	XStack* stack = XStack_init("struct TreeNode*", sizeof(struct TreeNode*));
	XStack_Push(stack, &this_root);
	struct TreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct TreeNode**)XStack_top(stack);
		XStack_pop(stack);
		if (currentNode->rightChild != NULL)
			XStack_Push(stack, &currentNode->rightChild);
		if (currentNode->leftChild != NULL)
			XStack_Push(stack, &currentNode->leftChild);
		XVector_push_back(vector, &currentNode);
	}
	XStack_free(stack);
	return vector;
}
//中序
static XVector* BinaryTreeTraversingToXVector_Inorder(struct TreeNode* this_root)
{
	XVector* vector = XVector_init("struct TreeNode*", sizeof(struct TreeNode*));
	XStack* stack = XStack_init("struct TreeNode*", sizeof(struct TreeNode*));
	struct TreeNode* currentNode = this_root;//当前节点指针
	while (!XStack_empty(stack)|| currentNode!=NULL)
	{
		if (currentNode != NULL)
		{
			XStack_Push(stack, &currentNode);
			currentNode = currentNode->leftChild;
		}
		else
		{
			struct TreeNode*  Node = *(struct TreeNode**)XStack_top(stack);
			XVector_push_back(vector, &Node);
			currentNode = Node->rightChild;
			XStack_pop(stack);
		}
	}
	XStack_free(stack);
	return vector;
}
//后序
static XVector* BinaryTreeTraversingToXVector_Postorder(struct TreeNode* this_root)
{
	XVector* vector = XVector_init("struct TreeNode*", sizeof(struct TreeNode*));
	XStack* stack = XStack_init("struct TreeNode*", sizeof(struct TreeNode*));
	XStack* stackTraversing = XStack_init("struct TreeNode*", sizeof(struct TreeNode*));
	XStack_Push(stack, &this_root);
	struct TreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct TreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XStack_Push(stackTraversing, &currentNode);
		
		if (currentNode->leftChild != NULL)
			XStack_Push(stack, &currentNode->leftChild);
		if (currentNode->rightChild != NULL)
			XStack_Push(stack, &currentNode->rightChild);
	}
	XStack_free(stack);
	XStackCopyXVector(stackTraversing, vector);
	XStack_free(stackTraversing);
	return vector;
}
void* TreeNode_creation(const size_t NodeSize, const size_t TypeSize)
{
	TreeNode* node =(TreeNode*)malloc(NodeSize);
	if (isObjectNULL(node,"TreeNode_creation-node"))
		return NULL;
	node->data= calloc(1,TypeSize);//开辟内存并且置为0
	if (isObjectNULL(node->data, "TreeNode_creation-node->data"))
	{
		free(node);
		return NULL;
	}
	node->leftChild = NULL;
	node->parent = NULL;
	node->rightChild = NULL;
	return node;
}

TreeNode* TreeNode_creationInsertData(const void* LPData, const size_t TypeSize)
{
	struct TreeNode* node = TreeNode_creation(sizeof(TreeNode),TypeSize);
	/*if(isObjectNULL(node,"TreeNode_creationInsertData-node"))
		return NULL;*/
	TreeNode_insertData(node, LPData, TypeSize);
	return node;
}

const bool TreeNode_insertData(struct TreeNode* this_root, const void* LPData, const size_t TypeSize)
{
	if (isObjectNULL(this_root, "InsertTreeNodeData-this_root"))
		return false;
	if (isObjectNULL(LPData, "InsertTreeNodeData-LPData"))
		return false;
	if (isObjectNULL(TypeSize, "InsertTreeNodeData-TypeSize"))
		return false;
	memcpy(this_root->data, LPData, TypeSize);
	return true;
}

const bool TreeNode_free(struct TreeNode* this_root , const bool parentSetNull)
{
	if (isObjectNULL(this_root, "TreeNode_free-this_root"))
		return false;
	//释放数据
	free(this_root->data);
	if (parentSetNull)
	{
		//在父节点将指向此节点的指针置空NULL
		struct TreeNode* LPparent = this_root->parent;
		if (LPparent->leftChild == this_root)
			LPparent->leftChild = NULL;
		else if (LPparent->rightChild == this_root)
			LPparent->rightChild = NULL;
	}
	//释放节点
	free(this_root);
	return true;
}

XVector* BinaryTreeTraversingToXVector(TreeNode* this_root, const enum BinaryTreeTraversing Traversing)
{
	if (isObjectNULL(this_root, "BinaryTreeTraversingToXVector-this_root"))
		return false;
	switch (Traversing)
	{
	case  BinaryTreePreorder:
		return BinaryTreeTraversingToXVector_Preorder(this_root);
	case BinaryTreeInorder:
		return BinaryTreeTraversingToXVector_Inorder(this_root);
	case BinaryTreePostorder:
		return BinaryTreeTraversingToXVector_Postorder(this_root);
	default:
		return NULL;
	}
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
		XStack_pop(stack);
		if(currentNode->leftChild!=NULL)
			XStack_Push(stack, &currentNode->leftChild);
		if (currentNode->rightChild != NULL)
			XStack_Push(stack, &currentNode->rightChild);
		TreeNode_free(currentNode,false);//释放当前节点
		sum++;
	}
	XStack_free(stack);
	return sum;
}
