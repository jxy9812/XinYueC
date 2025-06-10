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
		XBTreeNode* LChild = XBTree_GetLChild(currentNode);
		XBTreeNode* RChild = XBTree_GetRChild(currentNode);
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
			currentNode = XBTree_GetLChild(currentNode);
		}
		else
		{
			struct XBTreeNode*  XListDNode = *(struct XBTreeNode**)XStack_top_base(stack);
			XVector_push_back_base(vector, &XListDNode);
			currentNode = XBTree_GetRChild(XListDNode);
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
		
		XBTreeNode* LChild = XBTree_GetLChild(currentNode);
		XBTreeNode* RChild = XBTree_GetRChild(currentNode);
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
void* XBTree_creationNode(const size_t NodeTypeSize, const size_t nodeCount, const size_t dataCount, const size_t TypeSize)
{
#if XVector_ON
	if (ISNULL(NodeTypeSize, "节点大小不能为空"))
		return NULL;
	if (ISNULL(nodeCount, "节点数组不能为空"))
		return NULL;
	if (ISNULL(dataCount, "数据数量"))
		return NULL;
	if (ISNULL(TypeSize, "数据大小不能为空"))
		return NULL;
	XBTreeNode* nodes =(XBTreeNode*)XMemory_malloc(NodeTypeSize);
	if (ISNULL(nodes,"节点申请内存失败"))
		return NULL;
	memset(nodes,0, NodeTypeSize);
	//nodes->values= XMemory_malloc(TypeSize);//开辟内存并且置为0
	//if (ISNULL(nodes->values,"节点数据申请内存失败")))
	//{
	//	XMemory_free(nodes);
	//	return NULL;
	//}
	//申请节点数组
	nodes->nodes = XVector_create( sizeof(XBTreeNode*));
	if (ISNULL(nodes->nodes, "节点数组申请内存失败"))
	{
		XMemory_free(nodes);
		return NULL;
	}
	XVector_resize_base(nodes->nodes, nodeCount);
	XContainerSize(nodes->nodes) = nodeCount;
	//申请数据数组
	nodes->values = XVector_create(TypeSize);
	if (ISNULL(nodes->values, "数据数组申请内存失败"))
	{
		XVector_delete_base(nodes->values);
		XMemory_free(nodes);
		return NULL;
	}
	XVector_resize_base(nodes->values, dataCount);
	XContainerSize(nodes->values) = dataCount;
	return nodes;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}

XBTreeNode* XBTree_creationInsertData(const void* LPData, const size_t nodeArrySize, const size_t TypeSize)
{
	struct XBTreeNode* nodes = XBTree_creationNode(sizeof(XBTreeNode), nodeArrySize,1,TypeSize);
	if (ISNULL( nodes,"创建节点失败"))
		return NULL;
	XBTree_insertData(nodes, LPData, 0,TypeSize);
	//printf("插入的:%d\n", XBTree_GetData(nodes, 0, int));
	return nodes;
}

const bool XBTree_insertData(struct XBTreeNode* this_root, const void* LPData, const size_t index, const size_t TypeSize)
{
#if XVector_ON
	if (ISNULL(this_root,""))
		return false;
	if (ISNULL(LPData, ""))
		return false;
	if (ISNULL(TypeSize, ""))
		return false;
	//XVector_insert_base(this_root->values,index, LPData);
	void* data=XVector_at_base(this_root->values,index);
	memcpy(data, LPData, TypeSize);
	return true;
#else
	IS_ON_DEBUG(XVector_ON);
	return false;
#endif;
}

const bool XBTree_freeNode(XBTreeNode* this_root , const bool parentSetNull)
{
#if XVector_ON
	if (ISNULL(this_root, ""))
		return false;
	
	//释放数据数组
	if(this_root->values!=NULL)
		XVector_delete_base(this_root->values);
	
	if (parentSetNull)
	{
		XBTreeNode** ptr=XBTree_findChildisParent(this_root);
		//在父节点将指向此节点的指针置空NULL
		if(ptr)
			*ptr = NULL;
	}
	//释放节点数组
	if (this_root->nodes != NULL)
		XVector_delete_base(this_root->nodes);
	//释放节点
	XMemory_free(this_root);
	return true;
#else
	IS_ON_DEBUG(XVector_ON);
	return false;
#endif
}

XBTreeNode** XBTree_GetTreeNode(XBTreeNode* this_root, const size_t nSel)
{
#if XVector_ON
	if (ISNULL(this_root, ""))
		return NULL;
	size_t count = XVector_getSize_base(this_root->nodes);
	if (nSel >= count)
	{
		DEBUG_PRINTF("nSel:%d>=总量:%d", nSel, count);
		return;
	}
	return (XBTreeNode**)XVector_at_base(this_root->nodes, nSel);
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}

void* XBTree_Getdata(XBTreeNode* this_root, const size_t nSel)
{
#if XVector_ON
	if (ISNULL(this_root, ""))
		return NULL;
	size_t count = XVector_getSize_base(this_root->values);
	if (nSel >= count)
	{
		DEBUG_PRINTF("nSel:%d>=总量:%d", nSel, count);
		return NULL;
	}
	return XVector_at_base(this_root->values, nSel);
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
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
#if XStack_ON
	if (ISNULL(this_root, ""))
		return 0;
	printf("开始释放节点\n");
	size_t sum = 0;//一共释放了几个节点
	XStack* stack = XStack_create(sizeof(struct XBTreeNode*));
	XStack_push_base(stack,&this_root);
	XBTreeNode* currentNode = NULL;//当前节点指针
	while (!XStack_isEmpty_base(stack))
	{
		currentNode = *(struct XBTreeNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		if (currentNode == NULL)
			continue;
		/*XVector_iterator* it = XVector_begin(currentNode->nodes);
		it = XVector_iterator_add(currentNode->nodes, it);
		for ( ; it != XVector_end(currentNode->nodes); it= XVector_iterator_add(currentNode->nodes,it))*/
		XVector_iterator it = XVector_begin(currentNode->nodes), endIt = XVector_end(currentNode->nodes);
		XVector_iterator_add(currentNode->nodes, &it);
		for (; !XVector_iterator_equality(&it, &endIt); XVector_iterator_add(currentNode->nodes, &it))
		{
			XStack_push_base(stack, XVector_iterator_data(&it));
		}
		XBTree_freeNode(currentNode,false);//释放当前节点
		sum++;
	}
	XStack_delete_base(stack);
	return sum;
#else
	IS_ON_DEBUG(XStack_ON);
	return  0;
#endif
}

XBTreeNode** XBTree_findChildisParent(struct XBTreeNode* Child)
{
	if (ISNULL(Child, ""))
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
	//ArgIsNULL(isNULLInfo(0, "在父节点找不到孩子"));
	return NULL;
}

bool XBTree_ReplacementChildNode(XBTreeNode* formerChild, XBTreeNode* freshChild)
{
	if (ISNULL(formerChild, ""))
		return false;
	if (ISNULL(freshChild, ""))
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
	if (ISNULL(nodes, ""))
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
	if (ISNULL(nodes, ""))
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
