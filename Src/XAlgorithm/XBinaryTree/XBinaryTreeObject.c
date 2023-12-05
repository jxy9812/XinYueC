#include "XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//前序
static XVector* BinaryTreeTraversingToXVector_Preorder(struct XBTreeNode* this_root)
{
	XVector* vector = XVector_new(sizeof(struct XBTreeNode*));
	XStack* stack = XStack_new(sizeof(struct XBTreeNode*));
	XStack_push(stack, &this_root);
	struct XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XBTreeNode* LChild = XBTree_GetLChild(currentNode);
		XBTreeNode* RChild = XBTree_GetRChild(currentNode);
		if (LChild != NULL)
			XStack_push(stack, &LChild);
		if (RChild != NULL)
			XStack_push(stack, &RChild);
		XVector_push_back(vector, &currentNode);
	}
	XStack_free(stack);
	return vector;
}
//中序
static XVector* BinaryTreeTraversingToXVector_Inorder(struct XBTreeNode* this_root)
{
	XVector* vector = XVector_new(sizeof(struct XBTreeNode*));
	XStack* stack = XStack_new(sizeof(struct XBTreeNode*));
	struct XBTreeNode* currentNode = this_root;//当前节点指针
	while (!XStack_empty(stack)|| currentNode!=NULL)
	{
		if (currentNode != NULL)
		{
			XStack_push(stack, &currentNode);
			currentNode = XBTree_GetLChild(currentNode);
		}
		else
		{
			struct XBTreeNode*  XListNode = *(struct XBTreeNode**)XStack_top(stack);
			XVector_push_back(vector, &XListNode);
			currentNode = XBTree_GetRChild(XListNode);
			XStack_pop(stack);
		}
	}
	XStack_free(stack);
	return vector;
}
//后序
static XVector* BinaryTreeTraversingToXVector_Postorder(struct XBTreeNode* this_root)
{
	XVector* vector = XVector_new( sizeof(struct XBTreeNode*));
	XStack* stack = XStack_new(sizeof(struct XBTreeNode*));
	XStack* stackTraversing = XStack_new(sizeof(struct XBTreeNode*));
	XStack_push(stack, &this_root);
	struct XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		XStack_push(stackTraversing, &currentNode);
		
		XBTreeNode* LChild = XBTree_GetLChild(currentNode);
		XBTreeNode* RChild = XBTree_GetRChild(currentNode);
		if (LChild != NULL)
			XStack_push(stack, &LChild);
		if (RChild != NULL)
			XStack_push(stack, &RChild);
	}
	XStack_free(stack);
	XStackCopyXVector(stackTraversing, vector);
	XStack_free(stackTraversing);
	return vector;
}
void* XBTree_creationNode(const size_t NodeTypeSize, const size_t nodeCount, const size_t dataCount, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(NodeTypeSize, "节点大小不能为空")))
		return NULL;
	if (isNULL(isNULLInfo(nodeCount, "节点数组不能为空")))
		return NULL;
	if (isNULL(isNULLInfo(dataCount, "数据数量")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "数据大小不能为空")))
		return NULL;
	XBTreeNode* nodes =(XBTreeNode*)calloc(1,NodeTypeSize);
	if (isNULL(isNULLInfo(nodes,"节点申请内存失败")))
		return NULL;

	//nodes->values= calloc(1,TypeSize);//开辟内存并且置为0
	//if (isNULL(isNULLInfo(nodes->values,"节点数据申请内存失败")))
	//{
	//	free(nodes);
	//	return NULL;
	//}
	//申请节点数组
	nodes->nodes = XVector_new( sizeof(XBTreeNode*));
	if (isNULL(isNULLInfo(nodes->nodes, "节点数组申请内存失败")))
	{
		free(nodes);
		return NULL;
	}
	XVector_resize(nodes->nodes, nodeCount);
	//申请数据数组
	nodes->values = XVector_new(TypeSize);
	if (isNULL(isNULLInfo(nodes->values, "数据数组申请内存失败")))
	{
		XVector_free(nodes->values);
		free(nodes);
		return NULL;
	}
	XVector_resize(nodes->values, dataCount);
	return nodes;
}

XBTreeNode* XBTree_creationInsertData(const void* LPData, const size_t nodeArrySize, const size_t TypeSize)
{
	struct XBTreeNode* nodes = XBTree_creationNode(sizeof(XBTreeNode), nodeArrySize,1,TypeSize);
	if(isNULL(isNULLInfo( nodes,"创建节点失败")))
		return NULL;
	XBTree_insertData(nodes, LPData, 0,TypeSize);
	//printf("插入的:%d\n", XBTree_GetData(nodes, 0, int));
	return nodes;
}

const bool XBTree_insertData(struct XBTreeNode* this_root, const void* LPData, const size_t nSel, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(this_root,"")))
		return false;
	if (isNULL(isNULLInfo(LPData, "")))
		return false;
	if (isNULL(isNULLInfo(TypeSize, "")))
		return false;
	void* data=XVector_at(this_root->values,nSel);
	memcpy(data, LPData, TypeSize);
	return true;
}

const bool XBTree_freeNode(struct XBTreeNode* this_root , const bool parentSetNull)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return false;
	
	//释放数据数组
	if(this_root->values!=NULL)
		XVector_free(this_root->values);
	
	if (parentSetNull)
	{
		//在父节点将指向此节点的指针置空NULL
		*XBTree_findChildisParent(this_root) = NULL;
	}
	//释放节点数组
	XVector_free(this_root->nodes);
	//释放节点
	free(this_root);
	return true;
}

XBTreeNode** XBTree_GetTreeNode(XBTreeNode* this_root, const size_t nSel)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	size_t count = XVector_size(this_root->nodes);
	if (nSel >= count)
	{
		PRINT("nSel:%d>=总量:%d", nSel, count);
		return;
	}
	return (XBTreeNode**)XVector_at(this_root->nodes, nSel);
}

void* XBTree_Getdata(XBTreeNode* this_root, const size_t nSel)
{
	if (isNULL(isNULLInfo(this_root, "")))
		return NULL;
	size_t count = XVector_size(this_root->values);
	if (nSel >= count)
	{
		PRINT("nSel:%d>=总量:%d", nSel, count);
		return;
	}
	return XVector_at(this_root->values, nSel);
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
	XStack* stack = XStack_new(sizeof(struct XBTreeNode*));
	XStack_push(stack,&this_root);
	XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_empty(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top(stack);
		XStack_pop(stack);
		if (currentNode == NULL)
			continue;
		XVector_iterator* it = XVector_begin(currentNode->nodes);
		it = XVector_iterator_add(currentNode->nodes, it);
		for ( ; it != XVector_end(currentNode->nodes); it= XVector_iterator_add(currentNode->nodes,it))
		{
			XStack_push(stack, it);
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
	XBTreeNode* Parent = XBTree_GetParent(Child);
	XBTreeNode* ParentToLChild = XBTree_GetLChild(Parent);
	XBTreeNode* ParentToRChild = XBTree_GetRChild(Parent);

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
	XBTreeNode* Parent = XBTree_GetParent(formerChild);//父节点
	if (Parent == NULL)
		return false;
	XBTreeNode** ParentPointToChild = XBTree_findChildisParent(formerChild);//父节点指向孩子指针
	if(ParentPointToChild==NULL)
		return false;
	//与新节点互相建立链接
	*ParentPointToChild = freshChild;
	XBTree_SetParent(freshChild,Parent);
	//断开旧节点指向父的指针
	XBTree_SetParent(formerChild, NULL);
	return true;
}

XBTreeNode* XBTree_SpinRR(XBTreeNode** this_root,XBTreeNode* nodes)
{
	if (isNULL(isNULLInfo(nodes, "")))
		return NULL;
	//获取将成为的根节点
	XBTreeNode* NewNode = XBTree_GetLChild(nodes);
	
	//设置NewRoot的父节点，this_root成为NewRoot的孩子
	XBTreeNode** ppThis_nodeParent = XBTree_GetTreeNode(nodes, XBTreeParent);
	//父节点
	XBTreeNode* pater = *ppThis_nodeParent;
	if (pater == NULL)//根节点改变
	{
		*this_root = NewNode;
	}
	else
	{
		*XBTree_findChildisParent(nodes) = NewNode;
	}
	XBTree_SetParent(NewNode, pater);
	*ppThis_nodeParent = NewNode;
	
	//NewRoot的右孩子交给this_root的左孩子，
	XBTreeNode** ppNewRootRightChild = XBTree_GetTreeNode(NewNode, XBTreeRChild);
	XBTree_SetLChild(nodes, *ppNewRootRightChild);
	if (*ppNewRootRightChild != NULL)
		XBTree_SetParent(*ppNewRootRightChild, nodes);

	*ppNewRootRightChild = nodes;
	return NewNode;
}

XBTreeNode* XBTree_SpinLL(XBTreeNode** this_root, XBTreeNode* nodes)
{
	if (isNULL(isNULLInfo(nodes, "")))
		return NULL;
	XBTreeNode* NewNode = XBTree_GetRChild(nodes);

	XBTreeNode** ppThis_rootParent = XBTree_GetTreeNode(nodes, XBTreeParent);
	//父节点
	XBTreeNode* pater = *ppThis_rootParent;
	if (pater == NULL)//根节点改变
	{
		*this_root = NewNode;
	}
	else
	{
		*XBTree_findChildisParent(nodes) = NewNode;
	}
	XBTree_SetParent(NewNode, pater);
	*ppThis_rootParent = NewNode;

	XBTreeNode** ppNewRootLeftChild = XBTree_GetTreeNode(NewNode, XBTreeLChild);
	XBTree_SetRChild(nodes, *ppNewRootLeftChild);
	if (*ppNewRootLeftChild != NULL)
		XBTree_SetParent(*ppNewRootLeftChild, nodes);

	*ppNewRootLeftChild = nodes;
	return NewNode;
}
