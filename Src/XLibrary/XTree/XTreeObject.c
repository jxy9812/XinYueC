#include"XTreeObject.h"
#include"XContainerObject.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//#include "XBinaryTree.h"

void XTreeNode_init(XTreeNode* node, const uint8_t nodeCount, const char* pvData, const size_t dataTypeSize)
{
	if (node == NULL||nodeCount==0||dataTypeSize==0)
		return;
	node->nodes = XMemory_calloc(nodeCount,sizeof(XTreeNode*));
	if (node->nodes == NULL)
		return;
	node->nodeCount = nodeCount;
	XTreeNode_GetDataPtr(node) = XMemory_calloc(1, dataTypeSize);
	if (XTreeNode_GetDataPtr(node) == NULL)
	{
		XMemory_free(node->nodes);
		node->nodes = NULL;
		node->nodeCount = 0;
		return;
	}
	node->dataTypeSize = dataTypeSize;
	node->parentNode = NULL;
	if (pvData)
		memcpy(XTreeNode_GetDataPtr(node), pvData, dataTypeSize);
}
XTreeNode* XTreeNode_create(const uint8_t nodeCount, const char* pvData, const size_t dataTypeSize)
{
	if (nodeCount == 0 || dataTypeSize == 0)
		return NULL;
	XTreeNode* node = XMemory_malloc(sizeof(XTreeNode));
	if (node)
		XTreeNode_init(node,nodeCount,pvData,dataTypeSize);
	return node;
}
bool XTreeNode_setData(XTreeNode* this_root, const void* pvData)
{
	if(this_root==NULL||pvData==NULL)
		return false;
	memcpy((uint8_t*)(XTreeNode_GetDataPtr(this_root)), pvData, this_root->dataTypeSize);
	return true;
}
void* XTreeNode_getData(XTreeNode* this_root)
{
	if (this_root == NULL )
		return NULL;
	return (uint8_t*)XTreeNode_GetDataPtr(this_root);
}
bool XTreeNode_setNode(XTreeNode* this_root, const uint8_t nodeType, XTreeNode* node)
{
	if (this_root == NULL || this_root->nodeCount <= nodeType)
		return false;
	((XTreeNode**)(this_root->nodes))[nodeType] = node;
	return true;
}
XTreeNode* XTreeNode_getChild(XTreeNode* this_root, const uint8_t nodeType)
{
	if(this_root==NULL)
		return NULL;
	return ((XTreeNode**)(this_root->nodes))[nodeType];
}
void XTree_delete(XTreeNode* this_root, XTreeNodeDataDeleteMethod method, void* args)
{
	XTree_delete_base(this_root, XTreeNode_delete,method,args);
}

void XTreeNode_delete(XTreeNode* node)
{
	if (node == NULL)
		return;
	if (node->nodes)
		XMemory_free(node->nodes);
	if (XTreeNode_GetDataPtr(node))
		XMemory_free(XTreeNode_GetDataPtr(node));
	XMemory_free(node);
}

void XTree_delete_base(XTreeNode* this_root, XTreeNodeDeleteMethod nodeMethod, XTreeNodeDataDeleteMethod dataMethod, void* args)
{
	if (this_root == NULL)
		return;
#if XStack_ON
	if (ISNULL(this_root, ""))
		return;
	size_t sum = 0;//一共释放了几个节点
	XStack* stack = XStack_create(sizeof(struct XTreeNode*));
	XStack_push_base(stack, &this_root);
	XTreeNode* currentNode = NULL, * node = NULL;//当前节点指针
	while (!XStack_isEmpty_base(stack))
	{
		currentNode = *(XTreeNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		if (currentNode == NULL)
			continue;
		for (size_t i = 0; i < currentNode->nodeCount; i++)
		{
			node = ((XTreeNode**)(currentNode->nodes))[i];
			if (node)
				XStack_push_base(stack, &node);
		}
		if (XTreeNode_GetDataPtr(currentNode) != NULL && dataMethod != NULL)
			dataMethod(XTreeNode_GetDataPtr(currentNode), args);
		nodeMethod(currentNode);//释放当前节点
		sum++;
	}
	XStack_delete_base(stack);
	//return sum;
#else
	IS_ON_DEBUG(XStack_ON);
	return;
#endif
}

XTreeNode** XTreeNode_getChildRef(XTreeNode* this_root, const uint8_t nodeType)
{
	if (ISNULL(this_root, ""))
		return NULL;
	size_t count = this_root->nodeCount;
	if (nodeType >= count)
	{
		DEBUG_PRINTF("nodeType:%d>=总量:%d", nodeType, count);
		return;
	}
	return ((XTreeNode**)(this_root->nodes)) + nodeType;
}

XTreeNode** XTreeNode_getParentRef(XTreeNode* this_root)
{
	if (ISNULL(this_root, ""))
		return NULL;
	return &(this_root->parentNode);
}


bool XTree_ReplacementChildNode(XTreeNode* formerChild, XTreeNode* freshChild)
{
	if (ISNULL(formerChild, ""))
		return false;
	if (ISNULL(freshChild, ""))
		return false;
	XTreeNode* Parent = XTreeNode_GetParent(formerChild);//父节点
	if (Parent == NULL)
		return false;
	XTreeNode** ParentPointToChild = XTreeNode_getChildrenParentRef(formerChild);//父节点指向孩子指针
	if (ParentPointToChild == NULL)
		return false;
	//与新节点互相建立链接
	*ParentPointToChild = freshChild;
	XTreeNode_SetParent(freshChild, Parent);
	//断开旧节点指向父的指针
	XTreeNode_SetParent(formerChild, NULL);
	return true;
}
XTreeNode** XTreeNode_getChildrenParentRef(XTreeNode* this_root)
{
	if (ISNULL(this_root, ""))
		return NULL;
	XTreeNode* Parent = XTreeNode_GetParent(this_root);
	if (Parent == NULL)
		return NULL;
	for (size_t i = 0; i < this_root->nodeCount; i++)
	{
		if (XTreeNode_GetChild(Parent,i) == this_root)
			return Parent->nodes + i;
	}
	return NULL;
}