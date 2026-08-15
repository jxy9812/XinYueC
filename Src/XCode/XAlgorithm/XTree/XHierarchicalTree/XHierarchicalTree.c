#include "XPrintf.h"
#include"XHierarchicalTree.h"
size_t XHTreeNode_typeSize()
{
	return sizeof(XHTreeNode) + sizeof(struct XTreeNode*) * 2;
}
XHTreeNode* XHTreeNode_create(const char* pvData, const size_t dataTypeSize)
{
	if (dataTypeSize == 0)
		return NULL;
	XMemory* memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
	XHTreeNode* node = memory && memory->malloc
		? (XHTreeNode*)memory->malloc(XHTreeNode_typeSize() + dataTypeSize) : NULL;
	if (node)
		XHTreeNode_init(node, XHTreeNode_typeSize(), pvData, dataTypeSize);
	return node;
}

void XHTreeNode_init(XHTreeNode* node, size_t treeNodeSize, const char* pvData, const size_t dataTypeSize)
{
	if (node == NULL || dataTypeSize == 0)
		return;
	XTreeNode_init(node, 2, treeNodeSize,pvData, dataTypeSize);
}

bool XHTreeNode_addNode(XHTreeNode* parent, XHTreeNode* child)
{
	if (parent == NULL || child == NULL)
		return false;
	XTreeNode_SetParent(child, parent);
	// 将新节点添加为第一个子节点
	if (XHTreeNode_GetFirstChild(parent) == NULL)
	{
		XHTreeNode_SetFirstChild(parent, child);
	}
	else
	{
		// 找到最后一个兄弟节点
		XHTreeNode* sibling = XHTreeNode_GetFirstChild(parent);
		while (XHTreeNode_GetNextSibling(sibling) != NULL)
		{
			sibling = XHTreeNode_GetNextSibling(sibling);
		}
		XHTreeNode_SetNextSibling(sibling, child);
	}
	return true;
}

XHTreeNode* XHTreeNode_addChild(XHTreeNode* parent, const char* pvData, const size_t dataTypeSize)
{
	if (parent == NULL)
		return NULL;
	XHTreeNode* node=XHTreeNode_create(pvData, dataTypeSize);
	if (XHTreeNode_addNode(parent, node))
		return node;
	XHTreeNode_delete(node, XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE));
	return NULL;
}

bool XHTreeNode_removeNode(XHTreeNode* node, XTreeNodeDataDeleteMethod method, void* args, XMemory* memory)
{
	if (node == NULL)
		return;
	XHTreeNode* parent = XHTreeNode_GetParent(node);
	if (parent)
	{
		XHTreeNode* prev = XHTreeNode_GetFirstChild(parent);
		if (prev == node)
		{
			XHTreeNode_SetFirstChild(parent, XHTreeNode_GetNextSibling(prev));
		}
		else
		{
			while (prev)
			{
				if (XHTreeNode_GetNextSibling(prev) == node)
				{
					XHTreeNode_SetNextSibling(prev, XHTreeNode_GetNextSibling(node));
					break;
				}
				prev = XHTreeNode_GetNextSibling(prev);
			}
		}
		
	}
	//递归释放
	XHTree_delete(node, method, args, memory);
	return true;
}

bool XHTreeNode_removeChild(XHTreeNode* parent, XEquality equality, XCompareRuleOne rule, const void* pvData,XTreeNodeDataDeleteMethod method, void* args, XMemory* memory)
{
	if(parent==NULL)
		return false;
	XHTreeNode* child = XHTreeNode_GetFirstChild(parent), *prev=NULL;
	while (child)
	{
		if (rule(equality, XTreeNode_GetDataPtr(child), pvData))
		{//找到了
			if (prev == NULL)
			{//是第一个
				XHTreeNode_SetFirstChild(parent, XHTreeNode_GetNextSibling(child));
			}
			else
			{
				XHTreeNode_SetNextSibling(prev, XHTreeNode_GetNextSibling(child));
			}
			//递归释放
			XHTree_delete(child, method,args,memory);
		}
		prev = child;
		child = XHTreeNode_GetNextSibling(child);
	}
	return false;
}

XHTreeNode* XHTreeNode_findData(XHTreeNode* parent,XEquality equality, XCompareRuleOne rule, void* pvData)
{
	if(parent==NULL)
		return NULL;
	XHTreeNode* child = XHTreeNode_GetFirstChild(parent);
	while (child)
	{
		if (rule(equality, XTreeNode_GetDataPtr(child), pvData))
		{//找到了
			break;
		}
		child = XHTreeNode_GetNextSibling(child);
	}
	return child;
}

void XHTree_print(XHTreeNode* this_root, int depth)
{
	if (this_root == NULL)
		return;
	// 缩进表示层级
	for (int i = 0; i < depth; i++) {
		XPrintf("  ");
	}
	//printf("%s\n", node->data);

	// 递归遍历子节点
	XHTreeNode* child = XHTreeNode_GetFirstChild(this_root);
	while (child != NULL)
	{
		XHTree_print(child, depth + 1);
		child = XHTreeNode_GetNextSibling(child);
	}
}

//bool XHTreeNode_removeChild(XHTreeNode* parent, XHTreeNode* child)
//{
//	if (parent == NULL|| child==NULL)
//		return false;
//
//}
