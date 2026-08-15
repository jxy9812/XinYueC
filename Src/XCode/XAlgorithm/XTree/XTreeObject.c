#include"XTreeObject.h"
#include"XContainer.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
void XTreeNode_init(XTreeNode* node, const uint8_t nodeCount, size_t treeNodeSize, const char* pvData, const size_t dataSize)
{
	if (node == NULL||nodeCount==0|| dataSize ==0)
		return;
	node->nodeSize = treeNodeSize;
	node->nodeCount = nodeCount;
	//node->dataSize = dataSize;
	//初始化孩子数组
	memset(XTreeNode_GetNodes(node), 0, sizeof(XTreeNode*) * nodeCount);
	node->parentNode = NULL;
	if (pvData)
		memcpy(XTreeNode_GetDataPtr(node), pvData, dataSize);
	else
		memset(XTreeNode_GetDataPtr(node), 0, dataSize);
}
size_t XTreeNode_typeSize(const uint8_t nodeCount)
{
	return sizeof(XTreeNode)+sizeof(struct XTreeNode*)*nodeCount;
}
XTreeNode* XTreeNode_create(const uint8_t nodeCount, const char* pvData, const size_t dataSize)
{
	return XTreeNode_create_ex(nodeCount, pvData, dataSize,
		XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE));
}

XTreeNode* XTreeNode_create_ex(const uint8_t nodeCount, const char* pvData,
	const size_t dataSize, XMemory* memory)
{
	if (nodeCount == 0 || dataSize == 0)
		return NULL;
	if (!memory)
		memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
	if (!memory || !memory->malloc)
		return NULL;
	XTreeNode* node = (XTreeNode*)memory->malloc(XTreeNode_typeSize(nodeCount) + dataSize);
	if (node)
	{
		XTreeNode_init(node,nodeCount, XTreeNode_typeSize(nodeCount),pvData,dataSize);
	}
	return node;
}
bool XTreeNode_setData(XTreeNode* this_root, const void* pvData, size_t dataSize)
{
	if(this_root==NULL||pvData==NULL)
		return false;
	memcpy((uint8_t*)(XTreeNode_GetDataPtr(this_root)), pvData, dataSize);
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
	XTreeNode_SetChild(this_root, nodeType, node);
	//((XTreeNode**)(this_root->nodes))[nodeType] = node;
	return true;
}
XTreeNode* XTreeNode_getChild(XTreeNode* this_root, const uint8_t nodeType)
{
	if(this_root==NULL)
		return NULL;
	return XTreeNode_GetChild(this_root,nodeType);
	//return ((XTreeNode**)(this_root->nodes))[nodeType];
}
void XTree_delete(XTreeNode* this_root, XTreeNodeDataDeleteMethod method,
	void* args, XMemory* memory)
{
	XTree_delete_base(this_root, XTreeNode_delete,method,args,memory);
}

void XTreeNode_delete(XTreeNode* node, XMemory* memory)
{
	if (node == NULL)
		return;
	/*if (node->nodes)
		XFree_System(node->nodes);*/
	/*if (XTreeNode_GetDataPtr(node))
		XFree_System(XTreeNode_GetDataPtr(node));*/
	if (!memory)
		memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
	if (memory && memory->free)
		memory->free(node);
	else
		XFree_System(node);
}

void XTree_delete_base(XTreeNode* this_root, XTreeNodeDeleteMethod nodeMethod,
	XTreeNodeDataDeleteMethod dataMethod, void* args, XMemory* memory)
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
			node = XTreeNode_GetChild(currentNode,i);
			if (node)
				XStack_push_base(stack, &node);
		}
		if (XTreeNode_GetDataPtr(currentNode) != NULL && dataMethod != NULL)
			dataMethod(XTreeNode_GetDataPtr(currentNode), args);
		nodeMethod(currentNode, memory);//释放当前节点
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
		XDEBUG_PRINTF("nodeType:%d>=总量:%d", nodeType, count);
		return;
	}
	return &XTreeNode_GetChild(this_root, nodeType);
	//return ((XTreeNode**)(this_root->nodes)) + nodeType;
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
		if (XTreeNode_GetChild(Parent, i) == this_root)
			//return Parent->nodes + i;
			return &XTreeNode_GetChild(Parent, i);
	}
	return NULL;
}
