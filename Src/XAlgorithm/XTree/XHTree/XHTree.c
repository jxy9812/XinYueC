#include"XHTree.h"
XHTreeNode* XHTreeNode_create(const char* pvData, const size_t dataTypeSize)
{
	if (dataTypeSize == 0)
		return NULL;
	XHTreeNode* node = XMemory_malloc(sizeof(XHTreeNode));
	if (node)
		XHTreeNode_init(node, pvData, dataTypeSize);
	return node;
}

void XHTreeNode_init(XHTreeNode* node, const char* pvData, const size_t dataTypeSize)
{
	if (node == NULL || dataTypeSize == 0)
		return;
	XTreeNode_init(node, 2,pvData, dataTypeSize);
}

XHTreeNode* XHTreeNode_addChild(XHTreeNode* parent, const char* pvData, const size_t dataTypeSize)
{
	if (parent == NULL)
		return NULL;
	XHTreeNode* child =XHTreeNode_create(pvData,dataTypeSize);
	if(child==NULL)
		return NULL;
	XTreeNode_SetParent(child,parent);
	// 将新节点添加为第一个子节点
	if (XHTreeNode_GetFirstChild (parent) == NULL)
	{
		XHTreeNode_SetFirstChild(parent,child);
	}
	else 
	{
		// 找到最后一个兄弟节点
		XHTreeNode* sibling = XHTreeNode_GetFirstChild(parent);
		while (XHTreeNode_GetNextSibling(sibling) != NULL)
		{
			sibling = XHTreeNode_GetNextSibling(sibling);
		}
		XHTreeNode_SetNextSibling(sibling,child);
	}
	return child;
}

bool XHTreeNode_removeChild(XHTreeNode* parent, XEquality equality, XCompareRuleOne rule, const void* pvData,XTreeNodeDataDeleteMethod method, void* args)
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
			XHTree_delete(child, method,args);
		}
		prev = child;
		child = XHTreeNode_GetNextSibling(child);
	}
	return false;
}

//bool XHTreeNode_removeChild(XHTreeNode* parent, XHTreeNode* child)
//{
//	if (parent == NULL|| child==NULL)
//		return false;
//
//}
