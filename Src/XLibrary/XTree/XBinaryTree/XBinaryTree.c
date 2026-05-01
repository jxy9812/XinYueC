#include"XBinaryTree.h"
#include"XStack.h"
size_t XBTreeNode_typeSize()
{
	return sizeof(XBTreeNode) + sizeof(struct XTreeNode*) *3;
}
XBTreeNode* XBTreeNode_create(const char* pvData, const size_t typeSize)
{
	if (typeSize == 0)
		return NULL;
	XBTreeNode* node = XMemory_malloc(XBTreeNode_typeSize() + typeSize);
	if (node)
		XBTreeNode_init(node, XBTreeNode_typeSize(), pvData, typeSize);
	return node;
}

void XBTreeNode_init(XBTreeNode* node, size_t treeNodeSize, const char* pvData, const size_t typeSize)
{
	if (node==NULL|| typeSize == 0)
		return ;
	XTreeNode_init(node,3, treeNodeSize,pvData, typeSize);
}

//前序
static XVector* BinaryTreeTraversingToXVector_Preorder(struct XTreeNode* this_root)
{
#if XStack_ON
	XVector* vector = XVector_create(sizeof(struct XTreeNode*));
	XStack* stack = XStack_create(sizeof(struct XTreeNode*));
	XStack_push_base(stack, &this_root);
	struct XTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_isEmpty_base(stack))
	{
		currentNode = *(struct XTreeNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		XTreeNode* LChild = XBTreeNode_GetLChild(currentNode);
		XTreeNode* RChild = XBTreeNode_GetRChild(currentNode);
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
static XVector* BinaryTreeTraversingToXVector_Inorder(struct XTreeNode* this_root)
{
#if XStack_ON
	XVector* vector = XVector_create(sizeof(struct XTreeNode*));
	XStack* stack = XStack_create(sizeof(struct XTreeNode*));
	struct XTreeNode* currentNode = this_root;//当前节点指针
	while (!XStack_isEmpty_base(stack) || currentNode != NULL)
	{
		if (currentNode != NULL)
		{
			XStack_push_base(stack, &currentNode);
			currentNode = XBTreeNode_GetLChild(currentNode);
		}
		else
		{
			struct XTreeNode* XListDNode = *(struct XTreeNode**)XStack_top_base(stack);
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
static XVector* BinaryTreeTraversingToXVector_Postorder(struct XTreeNode* this_root)
{
#if XStack_ON
	XVector* vector = XVector_create(sizeof(struct XTreeNode*));
	XStack* stack = XStack_create(sizeof(struct XTreeNode*));
	XStack* stackTraversing = XStack_create(sizeof(struct XTreeNode*));
	XStack_push_base(stack, &this_root);
	struct XTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_isEmpty_base(stack))
	{
		currentNode = *(struct XTreeNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		XStack_push_base(stackTraversing, &currentNode);

		XTreeNode* LChild = XBTreeNode_GetLChild(currentNode);
		XTreeNode* RChild = XBTreeNode_GetRChild(currentNode);
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

XVector* XBTree_TraversingToXVector(XTreeNode* this_root, const enum XBTreeTraversing Traversing)
{
	if (this_root == NULL)
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

XTreeNode* XBTree_SpinRR(XTreeNode** this_root, XTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	//获取将成为的根节点
	XTreeNode* NewNode = XBTreeNode_GetLChild(nodes);
	if (NewNode == NULL) {
		// 无法执行右旋，因为 nodes 没有左子节点
		return NULL;
	}
	//设置NewRoot的父节点，this_root成为NewRoot的孩子
	XTreeNode** ppThis_nodeParent = XTreeNode_getParentRef(nodes);
	//父节点
	XTreeNode* pater = *ppThis_nodeParent;
	if (pater == NULL)//根节点改变
	{
		*this_root = NewNode;
	}
	else
	{
		*XTreeNode_getChildrenParentRef(nodes) = NewNode;
	}
	XBTreeNode_SetParent(NewNode, pater);
	*ppThis_nodeParent = NewNode;

	//NewRoot的右孩子交给this_root的左孩子，
	XTreeNode** ppNewRootRightChild = XTreeNode_getChildRef(NewNode, XBTreeRChild);
	XBTreeNode_SetLChild(nodes, *ppNewRootRightChild);
	if (*ppNewRootRightChild != NULL)
		XBTreeNode_SetParent(*ppNewRootRightChild, nodes);

	*ppNewRootRightChild = nodes;
	return NewNode;
}

XTreeNode* XBTree_SpinLL(XTreeNode** this_root, XTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	XTreeNode* NewNode = XBTreeNode_GetRChild(nodes);
	if (NewNode == NULL) {
		// 无法执行左旋，因为 nodes 没有右子节点
		return NULL;
	}
	XTreeNode** ppThis_rootParent = XTreeNode_getParentRef(nodes);
	//父节点
	XTreeNode* pater = *ppThis_rootParent;
	if (pater == NULL)//根节点改变
	{
		*this_root = NewNode;
	}
	else
	{
		*XTreeNode_getChildrenParentRef(nodes) = NewNode;
	}
	XBTreeNode_SetParent(NewNode, pater);
	*ppThis_rootParent = NewNode;

	XTreeNode** ppNewRootLeftChild = XTreeNode_getChildRef(NewNode, XBTreeLChild);
	XBTreeNode_SetRChild(nodes, *ppNewRootLeftChild);
	if (*ppNewRootLeftChild != NULL)
		XBTreeNode_SetParent(*ppNewRootLeftChild, nodes);

	*ppNewRootLeftChild = nodes;
	return NewNode;
}
