#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//前序
static XVector* BinaryTreeTraversingToXVector_Preorder(struct XBTreeNode* this_root)
{
#if XStack_ON
	XVector* vector = XVector_create(sizeof(struct XBTreeNode*));
	XStack* stack = XStack_create(sizeof(struct XBTreeNode*));
	XStack_push_base(stack, &this_root);
	struct XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_isEmpty_base(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		XBTreeNode* LChild = XBTreeNode_GetLChild(currentNode);
		XBTreeNode* RChild = XBTreeNode_GetRChild(currentNode);
		if (LChild != NULL)
			XStack_push_base(stack, &LChild);
		if (RChild != NULL)
			XStack_push_base(stack, &RChild);
		XVector_push_back_base(vector, &currentNode);
	}
	XStack_delete_base(stack);
	return vector;
#else
	IS_ON_DEBUG(XStack_ON);
	return NULL;
#endif
}
//中序
static XVector* BinaryTreeTraversingToXVector_Inorder(struct XBTreeNode* this_root)
{
#if XStack_ON
	XVector* vector = XVector_create(sizeof(struct XBTreeNode*));
	XStack* stack = XStack_create(sizeof(struct XBTreeNode*));
	struct XBTreeNode* currentNode = this_root;//当前节点指针
	while (!XStack_isEmpty_base(stack)|| currentNode!=NULL)
	{
		if (currentNode != NULL)
		{
			XStack_push_base(stack, &currentNode);
			currentNode = XBTreeNode_GetLChild(currentNode);
		}
		else
		{
			struct XBTreeNode*  XListDNode = *(struct XBTreeNode**)XStack_top_base(stack);
			XVector_push_back_base(vector, &XListDNode);
			currentNode = XBTreeNode_GetRChild(XListDNode);
			XStack_pop_base(stack);
		}
	}
	XStack_delete_base(stack);
	return vector;
#else
	IS_ON_DEBUG(XStack_ON);
	return NULL;
#endif
}
//后序
static XVector* BinaryTreeTraversingToXVector_Postorder(struct XBTreeNode* this_root)
{
#if XStack_ON
	XVector* vector = XVector_create( sizeof(struct XBTreeNode*));
	XStack* stack = XStack_create(sizeof(struct XBTreeNode*));
	XStack* stackTraversing = XStack_create(sizeof(struct XBTreeNode*));
	XStack_push_base(stack, &this_root);
	struct XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_isEmpty_base(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		XStack_push_base(stackTraversing, &currentNode);
		
		XBTreeNode* LChild = XBTreeNode_GetLChild(currentNode);
		XBTreeNode* RChild = XBTreeNode_GetRChild(currentNode);
		if (LChild != NULL)
			XStack_push_base(stack, &LChild);
		if (RChild != NULL)
			XStack_push_base(stack, &RChild);
	}
	XStack_delete_base(stack);
	XStackCopyXVector(stackTraversing, vector);
	XStack_delete_base(stackTraversing);
	return vector;
#else
	IS_ON_DEBUG(XStack_ON);
	return NULL;
#endif
}
void XBTreeNode_init(XBTreeNode* node, const uint8_t nodeCount, const uint8_t dataCount, const size_t dataTypeSize)
{
	if (node == NULL||nodeCount==0||dataCount==0||dataTypeSize==0)
		return;
	node->nodes = XMemory_calloc(nodeCount,sizeof(XBTreeNode*));
	if (node->nodes == NULL)
		return;
	node->nodeCount = nodeCount;
	node->values= XMemory_calloc(dataCount, dataTypeSize);
	if (node->values == NULL)
	{
		XMemory_free(node->nodes);
		node->nodes = NULL;
		node->nodeCount = 0;
		return;
	}
	node->valueCount = dataCount;
	node->valueTypeSize = dataTypeSize;
}
XBTreeNode* XBTreeNode_create(const uint8_t nodeCount, const uint8_t dataCount, const size_t dataTypeSize)
{
	if (nodeCount == 0 || dataCount == 0 || dataTypeSize == 0)
		return NULL;
	XBTreeNode* node = XMemory_malloc(sizeof(XBTreeNode));
	if (node)
		XBTreeNode_init(node,nodeCount,dataCount,dataTypeSize);
	return node;
}
bool XBTreeNode_setData(XBTreeNode* this_root, const uint8_t index, const void* pvData)
{
	if(this_root==NULL||pvData==NULL|| this_root->valueTypeSize<=index)
		return false;
	memcpy((uint8_t*)(this_root->values) + (this_root->valueTypeSize * index), pvData, this_root->valueTypeSize);
	return true;
}
void* XBTreeNode_getData(XBTreeNode* this_root, const uint8_t index)
{
	if (this_root == NULL || this_root->valueTypeSize <= index)
		return NULL;
	return (uint8_t*)(this_root->values) + (this_root->valueTypeSize * index);
}
bool XBTreeNode_setNode(XBTreeNode* this_root, const uint8_t nodeType, XBTreeNode* node)
{
	if (this_root == NULL || this_root->nodeCount <= nodeType)
		return false;
	((XBTreeNode**)(this_root->nodes))[nodeType] = node;
	return true;
}
XBTreeNode* XBTreeNode_getNode(XBTreeNode* this_root, const uint8_t nodeType)
{
	if(this_root==NULL)
		return NULL;
	return ((XBTreeNode**)(this_root->nodes))[nodeType];
}
void XBTree_delete(XBTreeNode* this_root)
{
	if (XBTreeNode_getNode(this_root, XBTreeLChild) == NULL && XBTreeNode_getNode(this_root, XBTreeRChild) == NULL)
	{//根节点
		if (this_root->nodes)
			XMemory_free(this_root->nodes);
		if (this_root->values)
			XMemory_free(this_root->values);
		XMemory_free(this_root);
		return;
	}
#if XStack_ON
	if (ISNULL(this_root, ""))
		return ;
	//printf("开始释放节点\n");
	size_t sum = 0;//一共释放了几个节点
	XStack* stack = XStack_create(sizeof(struct XBTreeNode*));
	XStack_push_base(stack, &this_root);
	XBTreeNode* currentNode=NULL,*node = NULL;//当前节点指针
	while (!XStack_isEmpty_base(stack))
	{
		currentNode = *(XBTreeNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		if (currentNode == NULL)
			continue;
		for (size_t i = 1; i < currentNode->nodeCount; i++)
		{
			node=((XBTreeNode**)(currentNode->nodes))[i];
			if(node)
				XStack_push_base(stack,&node );
		}
		XBTreeNode_delete(currentNode);//释放当前节点
		sum++;
	}
	XStack_delete_base(stack);
	//return sum;
#else
	IS_ON_DEBUG(XStack_ON);
	return  ;
#endif
}

XBTreeNode* XBTree_createInsertData(const void* pvData, const size_t nodeArrySize, const size_t TypeSize)
{
	XBTreeNode* nodes = XBTreeNode_create( nodeArrySize,1,TypeSize);
	if (ISNULL( nodes,"创建节点失败"))
		return NULL;
	XBTreeNode_setData(nodes,0, pvData);
	//printf("插入的:%d\n", XBTreeNode_GetParent(nodes, 0, int));
	return nodes;
}

void XBTreeNode_delete(XBTreeNode* node)
{
	if (node == NULL)
		return;
	if (node->nodes)
		XMemory_free(node->nodes);
	if (node->values)
		XMemory_free(node->values);
	XMemory_free(node);
}

XBTreeNode** XBTreeNode_getNodeRef(XBTreeNode* this_root, const uint8_t nodeType)
{
	if (ISNULL(this_root, ""))
		return NULL;
	size_t count = this_root->nodeCount;
	if (nodeType >= count)
	{
		DEBUG_PRINTF("nodeType:%d>=总量:%d", nodeType, count);
		return;
	}
	return ((XBTreeNode**)(this_root->nodes)) + nodeType;
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

XBTreeNode** XBTreeNode_getChildrenParentRef(struct XBTreeNode* this_root)
{
	if (ISNULL(this_root, ""))
		return NULL;
	XBTreeNode* Parent = XBTreeNode_GetParent(this_root);
	XBTreeNode* ParentToLChild = XBTreeNode_GetLChild(Parent);
	XBTreeNode* ParentToRChild = XBTreeNode_GetRChild(Parent);

	if (Parent == NULL)
		return NULL;
	if (ParentToLChild == this_root)
		return ((XBTreeNode**)(Parent->nodes)) + XBTreeLChild;
	if (ParentToRChild == this_root)
		return ((XBTreeNode**)(Parent->nodes)) + XBTreeRChild;
	//ArgIsNULL(isNULLInfo(0, "在父节点找不到孩子"));
	return NULL;
}

bool XBTree_ReplacementChildNode(XBTreeNode* formerChild, XBTreeNode* freshChild)
{
	if (ISNULL(formerChild, ""))
		return false;
	if (ISNULL(freshChild, ""))
		return false;
	XBTreeNode* Parent = XBTreeNode_GetParent(formerChild);//父节点
	if (Parent == NULL)
		return false;
	XBTreeNode** ParentPointToChild = XBTreeNode_getChildrenParentRef(formerChild);//父节点指向孩子指针
	if(ParentPointToChild==NULL)
		return false;
	//与新节点互相建立链接
	*ParentPointToChild = freshChild;
	XBTreeNode_SetParent(freshChild,Parent);
	//断开旧节点指向父的指针
	XBTreeNode_SetParent(formerChild, NULL);
	return true;
}

XBTreeNode* XBTree_SpinRR(XBTreeNode** this_root,XBTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	//获取将成为的根节点
	XBTreeNode* NewNode = XBTreeNode_GetLChild(nodes);
	
	//设置NewRoot的父节点，this_root成为NewRoot的孩子
	XBTreeNode** ppThis_nodeParent = XBTreeNode_getNodeRef(nodes, XBTreeParent);
	//父节点
	XBTreeNode* pater = *ppThis_nodeParent;
	if (pater == NULL)//根节点改变
	{
		*this_root = NewNode;
	}
	else
	{
		*XBTreeNode_getChildrenParentRef(nodes) = NewNode;
	}
	XBTreeNode_SetParent(NewNode, pater);
	*ppThis_nodeParent = NewNode;
	
	//NewRoot的右孩子交给this_root的左孩子，
	XBTreeNode** ppNewRootRightChild = XBTreeNode_getNodeRef(NewNode, XBTreeRChild);
	XBTreeNode_SetLChild(nodes, *ppNewRootRightChild);
	if (*ppNewRootRightChild != NULL)
		XBTreeNode_SetParent(*ppNewRootRightChild, nodes);

	*ppNewRootRightChild = nodes;
	return NewNode;
}

XBTreeNode* XBTree_SpinLL(XBTreeNode** this_root, XBTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	XBTreeNode* NewNode = XBTreeNode_GetRChild(nodes);

	XBTreeNode** ppThis_rootParent = XBTreeNode_getNodeRef(nodes, XBTreeParent);
	//父节点
	XBTreeNode* pater = *ppThis_rootParent;
	if (pater == NULL)//根节点改变
	{
		*this_root = NewNode;
	}
	else
	{
		*XBTreeNode_getChildrenParentRef(nodes) = NewNode;
	}
	XBTreeNode_SetParent(NewNode, pater);
	*ppThis_rootParent = NewNode;

	XBTreeNode** ppNewRootLeftChild = XBTreeNode_getNodeRef(NewNode, XBTreeLChild);
	XBTreeNode_SetRChild(nodes, *ppNewRootLeftChild);
	if (*ppNewRootLeftChild != NULL)
		XBTreeNode_SetParent(*ppNewRootLeftChild, nodes);

	*ppNewRootLeftChild = nodes;
	return NewNode;
}
