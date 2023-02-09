#include "XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//前序
static XVector* BinaryTreeTraversingToXVector_Preorder(struct XBinaryTreeNode* this_root)
{
	XVector* vector = XVector_init("struct XBinaryTreeNode*", sizeof(struct XBinaryTreeNode*));
	XStack* stack = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBinaryTreeNode*));
	XStack_Push(stack, &this_root);
	struct XBinaryTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBinaryTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XBinaryTreeNode* LChild = *XBinaryTreeObject_GetTreeNode(currentNode, XBTreeLChild);
		XBinaryTreeNode* RChild = *XBinaryTreeObject_GetTreeNode(currentNode, XBTreeRChild);
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
static XVector* BinaryTreeTraversingToXVector_Inorder(struct XBinaryTreeNode* this_root)
{
	XVector* vector = XVector_init("struct XBinaryTreeNode*", sizeof(struct XBinaryTreeNode*));
	XStack* stack = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBinaryTreeNode*));
	struct XBinaryTreeNode* currentNode = this_root;//当前节点指针
	while (!XStack_empty(stack)|| currentNode!=NULL)
	{
		if (currentNode != NULL)
		{
			XStack_Push(stack, &currentNode);
			currentNode = *XBinaryTreeObject_GetTreeNode(currentNode, XBTreeLChild);
		}
		else
		{
			struct XBinaryTreeNode*  XListNode = *(struct XBinaryTreeNode**)XStack_top(stack);
			XVector_push_back(vector, &XListNode);
			currentNode = *XBinaryTreeObject_GetTreeNode(XListNode, XBTreeRChild);
			XStack_pop(stack);
		}
	}
	XStack_free(stack);
	return vector;
}
//后序
static XVector* BinaryTreeTraversingToXVector_Postorder(struct XBinaryTreeNode* this_root)
{
	XVector* vector = XVector_init("struct XBinaryTreeNode*", sizeof(struct XBinaryTreeNode*));
	XStack* stack = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBinaryTreeNode*));
	XStack* stackTraversing = XStack_init("struct XBinaryTreeNode*", sizeof(struct XBinaryTreeNode*));
	XStack_Push(stack, &this_root);
	struct XBinaryTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBinaryTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XStack_Push(stackTraversing, &currentNode);
		
		XBinaryTreeNode* LChild = *XBinaryTreeObject_GetTreeNode(currentNode, XBTreeLChild);
		XBinaryTreeNode* RChild = *XBinaryTreeObject_GetTreeNode(currentNode, XBTreeRChild);
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
void* XBinaryTreeObject_creationNode(const size_t NodeSize, const size_t nodeArrySize, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(NodeSize, "节点大小不能为空")))
		return NULL;
	if (isNULL(isNULLInfo(nodeArrySize, "节点数组不能为空")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "数据大小不能为空")))
		return NULL;
	XBinaryTreeNode* node =(XBinaryTreeNode*)calloc(1,NodeSize);
	if (isNULL(isNULLInfo(node,"节点申请内存失败")))
		return NULL;
	node->data= calloc(1,TypeSize);//开辟内存并且置为0
	if (isNULL(isNULLInfo(node->data,"节点数据申请内存失败")))
	{
		free(node);
		return NULL;
	}
	node->node = (XBinaryTreeNode*)XVector_init("XBinaryTreeNode*", sizeof(XBinaryTreeNode*));
	if (isNULL(isNULLInfo(node->node, "节点数组申请内存失败")))
	{
		free(node);
		return NULL;
	}
	XBinaryTreeNode* NULLNode = NULL;
	for (size_t i = 0; i < nodeArrySize; i++)
	{
		XVector_push_back(node->node,&NULLNode);
	}
	/*node->leftChild = NULL;
	node->parent = NULL;
	node->rightChild = NULL;*/
	return node;
}

XBinaryTreeNode* XBinaryTreeObject_creationInsertData(const void* LPData, const size_t nodeArrySize, const size_t TypeSize)
{
	struct XBinaryTreeNode* node = XBinaryTreeObject_creationNode(sizeof(XBinaryTreeNode), nodeArrySize,TypeSize);
	if(isNULL(isNULLInfo( node,"创建节点失败")))
		return NULL;
	XBinaryTreeObject_insertData(node, LPData, TypeSize);
	return node;
}

const bool XBinaryTreeObject_insertData(struct XBinaryTreeNode* this_root, const void* LPData, const size_t TypeSize)
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

const bool XBinaryTreeObject_freeNode(struct XBinaryTreeNode* this_root , const bool parentSetNull)
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
	//释放节点数组
	XVector_free(this_root->node);
	//释放节点
	free(this_root);
	return true;
}

XBinaryTreeNode** XBinaryTreeObject_GetTreeNode(XBinaryTreeNode* this_root, const size_t nSel)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	return (XBinaryTreeNode**)XVector_at(this_root->node, nSel);
}

XVector* XBinaryTreeObject_TraversingToXVector(XBinaryTreeNode* this_root, const enum XBinaryTreeTraversing Traversing)
{
	if (this_root==NULL)
		return NULL;
	switch (Traversing)
	{
	case  XBinaryTreePreorder:
		return BinaryTreeTraversingToXVector_Preorder(this_root);
	case XBinaryTreeInorder:
		return BinaryTreeTraversingToXVector_Inorder(this_root);
	case XBinaryTreePostorder:
		return BinaryTreeTraversingToXVector_Postorder(this_root);
	default:
		return NULL;
	}
}
const size_t XBinaryTreeObject_freeNodeAll(struct XBinaryTreeNode* this_root)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return 0;
	size_t sum = 0;//一共释放了几个节点
	XStack* stack = XStack_init("struct XBinaryTreeNode*",sizeof(struct XBinaryTreeNode*));
	XStack_Push(stack,&this_root);
	struct XBinaryTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBinaryTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XVector_iterator* it = XVector_begin(currentNode->node);
		it = XVector_iterator_add(currentNode->node, it);
		for ( ; it != XVector_end(currentNode->node); it= XVector_iterator_add(currentNode->node,it))
		{
			XStack_Push(stack, it);
		}
		XBinaryTreeObject_freeNode(currentNode,false);//释放当前节点
		sum++;
	}
	XStack_free(stack);
	return sum;
}

XBinaryTreeNode** XBinaryTreeObject_findChildisParent(struct XBinaryTreeNode* Child)
{
	if (isNULL(isNULLInfo(Child, "")))
		return NULL;
	XBinaryTreeNode* Parent = *XBinaryTreeObject_GetTreeNode(Child, XBTreeParent);
	XBinaryTreeNode* ParentToLChild = *XBinaryTreeObject_GetTreeNode(Parent, XBTreeLChild);
	XBinaryTreeNode* ParentToRChild = *XBinaryTreeObject_GetTreeNode(Parent, XBTreeRChild);

	if (Parent == NULL)
		return NULL;
	if (ParentToLChild == Child)
		return XBinaryTreeObject_GetTreeNode(Parent, XBTreeLChild);
	if (ParentToRChild == Child)
		return XBinaryTreeObject_GetTreeNode(Parent, XBTreeRChild);
	isNULL(isNULLInfo(0, "在父节点找不到孩子"));
	return NULL;
}

bool XBinaryTreeObject_ReplacementChildNode(XBinaryTreeNode* formerChild, XBinaryTreeNode* freshChild)
{
	if(isNULL(isNULLInfo(formerChild, "")))
		return false;
	if (isNULL(isNULLInfo(freshChild, "")))
		return false;
	XBinaryTreeNode* Parent = *XBinaryTreeObject_GetTreeNode(formerChild, XBTreeParent);//父节点
	if (Parent == NULL)
		return false;
	XBinaryTreeNode** ParentPointToChild = XBinaryTreeObject_findChildisParent(formerChild);//父节点指向孩子指针
	if(ParentPointToChild==NULL)
		return false;
	//与新节点互相建立链接
	*ParentPointToChild = freshChild;
	*XBinaryTreeObject_GetTreeNode(freshChild, XBTreeParent) = Parent;
	//断开旧节点指向父的指针
	*XBinaryTreeObject_GetTreeNode(formerChild, XBTreeParent) = NULL;
	return true;
}
