#include"XHashMap_iterator.h"
#if XHashMap_ON
#include"XHashMap.h"
#include"XRedBlackTree.h"
XHashMap_iterator XHashMap_begin(XHashMap*this_map)
{
	XHashMap_iterator it = {0};
	if (this_map == NULL|| XContainerCapacity(this_map)==0)
		return it;
	//XHashNode* node = NULL;
	XRBTreeNode* node = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_map); i++)
	{
		node= ((XRBTreeNode**)XContainerDataPtr(this_map))[i];
		if (node == NULL)
			continue;
		while (XBTreeNode_GetLChild(node) != NULL) 
		{
			node = XBTreeNode_GetLChild(node);
		}
		
		//node = ((XHashNode**)XContainerDataPtr(this_map))[i];
		if (node != NULL)
		{
			it.node = node;
			it.index = i;
			return it;//下一个节点找到了
		}
	}
	it.node = NULL;
	it.index = XContainerCapacity(this_map);
	return it;
}

XHashMap_iterator XHashMap_end(XHashMap*this_map)
{
	XHashMap_iterator it = { 0 };
	if (this_map == NULL || XContainerCapacity(this_map) == 0)
		return it;
	it.index = XContainerCapacity(this_map);
	return it;
}

bool XHashMap_iterator_isEnd(const XHashMap_iterator* it)
{
	return it ? (it->node == NULL) : false;
}

void XHashMap_iterator_add(XHashMap*this_map, XHashMap_iterator* it)
{
	//XHashMap_iterator it = { 0 };
	if (this_map == NULL || it ==NULL|| XContainerCapacity(this_map) == 0)
		return ;
	// 如果有右子树，找到右子树的最左节点
	if (XBTreeNode_GetRChild(it->node) != NULL) {
		XRBTreeNode* current = XBTreeNode_GetRChild(it->node);
		while (XBTreeNode_GetLChild(current) != NULL) {
			current = XBTreeNode_GetLChild(current);
		}
		it->node = current;
		return;
	}

	// 否则向上回溯，直到找到一个作为左子节点的祖先
	XRBTreeNode* current = it->node;
	XRBTreeNode* parent = XBTreeNode_GetParent(current);
	while (parent != NULL && current == XBTreeNode_GetRChild(parent)) {
		current = parent;
		parent = XBTreeNode_GetParent(parent);
	}
	it->node = parent;
	if (it->node)
		return;
	for (size_t i = it->index+1; i < XContainerCapacity(this_map); i++)
	{
		it->node = ((XRBTreeNode**)XContainerDataPtr(this_map))[i];
		if (it->node == NULL)
			continue;
		while (XBTreeNode_GetLChild(it->node) != NULL)
		{
			it->node = XBTreeNode_GetLChild(it->node);
		}
		if (it->node != NULL)
		{
			it->index = i;
			return ;//下一个节点找到了
		}
	}
	if (it->node == NULL)
	{
		it->node = NULL;
		it->index = XContainerCapacity(this_map);
	}
	//return;
	//XHashNode* node = curent->node;
	//if (node->next!=NULL)
	//{
	//	curent->node = node->next;
	//	return;//下一个节点找到了
	//}

	//for (size_t i = curent->index+1; i < XContainerCapacity(this_map); i++)
	//{
	//	node = ((XHashNode**)XContainerDataPtr(this_map))[i];
	//	if (node != NULL)
	//	{
	//		curent->node = node;
	//		curent->index = i;
	//		return;//下一个节点找到了
	//	}
	//}
	//curent->node = NULL;
	//curent->index= XContainerCapacity(this_map);
}

bool XHashMap_iterator_equality(const XHashMap_iterator* itFirst, const XHashMap_iterator* itSecond)
{
	return (itFirst->index == itSecond->index)&&(itFirst->node==itSecond->node);
}

void XHashMap_iterator_for_each(XHashMap*this_map, XFor_each ForFunction, void* args)
{
	for_each_iterator(this_map, XHashMap, it)
	{
		//printf("index:%d\n",it.index);
		ForFunction(XHashMap_iterator_data(&it),args);
	}
}

XPair* XHashMap_iterator_data(XHashMap_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XBTreeNode_GetData(it->node, XPair*);
	//return ((XHashNode*)(it->node))->pair;
}

#endif
