#include "XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//前序
static XVector* BinaryTreeTraversingToXVector_Preorder(struct XBTreeNode* this_root)
{
	XVector* vector = XVector_init("struct XBinaryTreeNode*", sizeof(struct XBTreeNode*));
	XStack* stack = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBTreeNode*));
	XStack_Push(stack, &this_root);
	struct XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XBTreeNode* LChild = XBTREE_GET_LCHILD(currentNode);
		XBTreeNode* RChild = XBTREE_GET_RCHILD(currentNode);
		if (LChild != NULL)
			XStack_Push(stack, &LChild);
		if (RChild != NULL)
			XStack_Push(stack, &RChild);
		XVector_push_back(vector, &currentNode);
	}
	XStack_free(stack);
	return vector;
}
//中序
static XVector* BinaryTreeTraversingToXVector_Inorder(struct XBTreeNode* this_root)
{
	XVector* vector = XVector_init("struct XBinaryTreeNode*", sizeof(struct XBTreeNode*));
	XStack* stack = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBTreeNode*));
	struct XBTreeNode* currentNode = this_root;//当前节点指针
	while (!XStack_empty(stack)|| currentNode!=NULL)
	{
		if (currentNode != NULL)
		{
			XStack_Push(stack, &currentNode);
			currentNode = XBTREE_GET_LCHILD(currentNode);
		}
		else
		{
			struct XBTreeNode*  XListNode = *(struct XBTreeNode**)XStack_top(stack);
			XVector_push_back(vector, &XListNode);
			currentNode = XBTREE_GET_RCHILD(XListNode);
			XStack_pop(stack);
		}
	}
	XStack_free(stack);
	return vector;
}
//后序
static XVector* BinaryTreeTraversingToXVector_Postorder(struct XBTreeNode* this_root)
{
	XVector* vector = XVector_init("struct XBinaryTreeNode*", sizeof(struct XBTreeNode*));
	XStack* stack = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBTreeNode*));
	XStack* stackTraversing = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBTreeNode*));
	XStack_Push(stack, &this_root);
	struct XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XStack_Push(stackTraversing, &currentNode);
		
		XBTreeNode* LChild = XBTREE_GET_LCHILD(currentNode);
		XBTreeNode* RChild = XBTREE_GET_RCHILD(currentNode);
		if (LChild != NULL)
			XStack_Push(stack, &LChild);
		if (RChild != NULL)
			XStack_Push(stack, &RChild);
	}
	XStack_free(stack);
	XStackCopyXVector(stackTraversing, vector);
	XStack_free(stackTraversing);
	return vector;
}
void* XBTree_creationNode(const size_t NodeSize, const size_t nodeArrySize, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(NodeSize, "节点大小不能为空")))
		return NULL;
	if (isNULL(isNULLInfo(nodeArrySize, "节点数组不能为空")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "数据大小不能为空")))
		return NULL;
	XBTreeNode* node =(XBTreeNode*)calloc(1,NodeSize);
	if (isNULL(isNULLInfo(node,"节点申请内存失败")))
		return NULL;
	node->data= calloc(1,TypeSize);//开辟内存并且置为0
	if (isNULL(isNULLInfo(node->data,"节点数据申请内存失败")))
	{
		free(node);
		return NULL;
	}
	node->node = (XBTreeNode*)XVector_init("XBinaryTreeNode*", sizeof(XBTreeNode*));
	if (isNULL(isNULLInfo(node->node, "节点数组申请内存失败")))
	{
		free(node);
		return NULL;
	}
	XBTreeNode* NULLNode = NULL;
	for (size_t i = 0; i < nodeArrySize; i++)
	{
		XVector_push_back(node->node,&NULLNode);
	}
	return node;
}

XBTreeNode* XBTree_creationInsertData(const void* LPData, const size_t nodeArrySize, const size_t TypeSize)
{
	struct XBTreeNode* node = XBTree_creationNode(sizeof(XBTreeNode), nodeArrySize,TypeSize);
	if(isNULL(isNULLInfo( node,"创建节点失败")))
		return NULL;
	XBTree_insertData(node, LPData, TypeSize);
	return node;
}

const bool XBTree_insertData(struct XBTreeNode* this_root, const void* LPData, const size_t TypeSize)
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

const bool XBTree_freeNode(struct XBTreeNode* this_root , const bool parentSetNull)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return false;
	//释放数据
	free(this_root->data);
	
	if (parentSetNull)
	{
		//在父节点将指向此节点的指针置空NULL
		*XBTree_findChildisParent(this_root) = NULL;
	}
	//释放节点数组
	XVector_free(this_root->node);
	//释放节点
	free(this_root);
	return true;
}

XBTreeNode** XBTree_GetTreeNode(XBTreeNode* this_root, const size_t nSel)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	return (XBTreeNode**)XVector_at(this_root->node, nSel);
}

XVector* XBTree_TraversingToXVector(XBTreeNode* this_root, const enum XBTreeTraversing Traversing)
{
	if (this_root==NULL)
		return NULL;
	switch (Traversing)
	{
	case  XBTreePreorder:
		return BinaryTreeTraversingToXVector_Preorder(this_root);
	case XBTreeInorder:
		return BinaryTreeTraversingToXVector_Inorder(this_root);
	case XBTreePostorder:
		return BinaryTreeTraversingToXVector_Postorder(this_root);
	default:
		return NULL;
	}
}
const size_t XBTree_freeNodeAll(struct XBTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return 0;
	size_t sum = 0;//一共释放了几个节点
	XStack* stack = XStack_init("struct XBinaryTreeNode*",sizeof(struct XBTreeNode*));
	XStack_Push(stack,&this_root);
	struct XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XVector_iterator* it = XVector_begin(currentNode->node);
		it = XVector_iterator_add(currentNode->node, it);
		for ( ; it != XVector_end(currentNode->node); it= XVector_iterator_add(currentNode->node,it))
		{
			XStack_Push(stack, it);
		}
		XBTree_freeNode(currentNode,false);//释放当前节点
		sum++;
	}
	XStack_free(stack);
	return sum;
}

XBTreeNode** XBTree_findChildisParent(struct XBTreeNode* Child)
{
	if (isNULL(isNULLInfo(Child, "")))
		return NULL;
	XBTreeNode* Parent = XBTREE_GET_PARENT(Child);
	XBTreeNode* ParentToLChild = XBTREE_GET_LCHILD(Parent);
	XBTreeNode* ParentToRChild = XBTREE_GET_RCHILD(Parent);

	if (Parent == NULL)
		return NULL;
	if (ParentToLChild == Child)
		return XBTree_GetTreeNode(Parent, XBTreeLChild);
	if (ParentToRChild == Child)
		return XBTree_GetTreeNode(Parent, XBTreeRChild);
	isNULL(isNULLInfo(0, "在父节点找不到孩子"));
	return NULL;
}

bool XBTree_ReplacementChildNode(XBTreeNode* formerChild, XBTreeNode* freshChild)
{
	if(isNULL(isNULLInfo(formerChild, "")))
		return false;
	if (isNULL(isNULLInfo(freshChild, "")))
		return false;
	XBTreeNode* Parent = XBTREE_GET_PARENT(formerChild);//父节点
	if (Parent == NULL)
		return false;
	XBTreeNode** ParentPointToChild = XBTree_findChildisParent(formerChild);//父节点指向孩子指针
	if(ParentPointToChild==NULL)
		return false;
	//与新节点互相建立链接
	*ParentPointToChild = freshChild;
	XBTREE_SET_PARENT(freshChild,Parent);
	//断开旧节点指向父的指针
	XBTREE_SET_PARENT(formerChild, NULL);
	return true;
}
