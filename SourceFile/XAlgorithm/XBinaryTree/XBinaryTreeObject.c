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
			struct TreeNode*  XListNode = *(struct TreeNode**)XStack_top(stack);
			XVector_push_back(vector, &XListNode);
			currentNode = XListNode->rightChild;
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
void* XBinaryTreeObject_creationNode(const size_t NodeSize, const size_t TypeSize)
{
	TreeNode* node =(TreeNode*)calloc(1,NodeSize);
	if (isNULL(isNULLInfo(node,"")))
		return NULL;
	node->data= calloc(1,TypeSize);//开辟内存并且置为0
	if (isNULL(isNULLInfo(node->data,"")))
	{
		free(node);
		return NULL;
	}
	/*node->leftChild = NULL;
	node->parent = NULL;
	node->rightChild = NULL;*/
	return node;
}

TreeNode* XBinaryTreeObject_creationInsertData(const void* LPData, const size_t TypeSize)
{
	struct TreeNode* node = XBinaryTreeObject_creationNode(sizeof(TreeNode),TypeSize);
	/*if(isObjectNULL(node,"TreeNode_creationInsertData-node"))
		return NULL;*/
	XBinaryTreeObject_insertData(node, LPData, TypeSize);
	return node;
}

const bool XBinaryTreeObject_insertData(struct TreeNode* this_root, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(this_root,"")))
		return false;
	if (isNULL(isNULLInfo(LPData, "")))
		return false;
	if (isNULL(isNULLInfo(TypeSize, "")))
		return false;
	memcpy(this_root->data, LPData, TypeSize);
	return true;
}

const bool XBinaryTreeObject_freeNode(struct TreeNode* this_root , const bool parentSetNull)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return false;
	//释放数据
	free(this_root->data);
	if (parentSetNull)
	{
		//在父节点将指向此节点的指针置空NULL
		*XBinaryTreeObject_findChildisParent(this_root) = NULL;
	}
	//释放节点
	free(this_root);
	return true;
}

XVector* XBinaryTreeObject_TraversingToXVector(TreeNode* this_root, const enum BinaryTreeTraversing Traversing)
{
	if (this_root==NULL)
		return NULL;
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
const size_t XBinaryTreeObject_freeNodeAll(struct TreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
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
		XBinaryTreeObject_freeNode(currentNode,false);//释放当前节点
		sum++;
	}
	XStack_free(stack);
	return sum;
}

TreeNode** XBinaryTreeObject_findChildisParent(struct TreeNode* Child)
{
	if (isNULL(isNULLInfo(Child, "")))
		return NULL;
	TreeNode* Parent = Child->parent;
	if (Parent == NULL)
		return NULL;
	if (Parent->leftChild == Child)
		return &Parent->leftChild;
	if (Parent->rightChild == Child)
		return &Parent->rightChild;
}

bool XBinaryTreeObject_ReplacementChildNode(TreeNode* formerChild, TreeNode* freshChild)
{
	if(isNULL(isNULLInfo(formerChild, "")))
		return false;
	if (isNULL(isNULLInfo(freshChild, "")))
		return false;
	TreeNode* Parent = formerChild->parent;//父节点
	if (Parent == NULL)
		return false;
	TreeNode** ParentPointToChild = XBinaryTreeObject_findChildisParent(formerChild);//父节点指向孩子指针
	if(ParentPointToChild==NULL)
		return false;
	//与新节点互相建立链接
	*ParentPointToChild = freshChild;
	freshChild->parent = Parent;
	//断开旧节点指向父的指针
	formerChild->parent = NULL;
	return true;
}
