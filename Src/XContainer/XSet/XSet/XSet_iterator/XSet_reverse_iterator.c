#include "XSet_reverse_iterator.h"
#if XSet_ON
#include"XSet.h"
#include"XRedBlackTree.h"

XSet_reverse_iterator XSet_rbegin(XSet* this_map)
{
	XSet_reverse_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* current = XContainerSharedDataPtr(this_map);
	if (current == NULL) return it;
	while (XBTreeNode_GetRChild(current) != NULL) {
		current = XBTreeNode_GetRChild(current);
	}
	it.node = current;
	return it;
}

XSet_reverse_iterator XSet_rend(XSet* this_map)
{

	XSet_reverse_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* this_root = XContainerSharedDataPtr(this_map);
	return it;
}

bool XSet_reverse_iterator_isRend(const XSet_reverse_iterator* it)
{
	return it ? (it->node == NULL) : false;
}

XSet_reverse_iterator* XSet_reverse_iterator_add(XSet* this_map, XSet_reverse_iterator* it)
{
	if (this_map == NULL || it == NULL || it->node == NULL)
		return;
	// 如果有左子树，找到左子树的最右节点
	if (XBTreeNode_GetLChild(it->node) != NULL) {
		XRBTreeNode* current = XBTreeNode_GetLChild(it->node);
		while (XBTreeNode_GetRChild(current) != NULL) {
			current = XBTreeNode_GetRChild(current);
		}
		it->node = current;
		return;
	}

	// 否则向上回溯，直到找到一个作为右子节点的祖先
	XRBTreeNode* current = it->node;
	XRBTreeNode* parent = XBTreeNode_GetParent(current);
	while (parent != NULL && current == XBTreeNode_GetLChild(parent)) {
		current = parent;
		parent = XBTreeNode_GetParent(parent);
	}
	it->node = parent;
	return;
}

bool XSet_reverse_iterator_equality(XSet_reverse_iterator* itFirst, XSet_reverse_iterator* itSecond)
{
	return itFirst->node == itSecond->node;
}

void XSet_reverse_iterator_for_each(XSet* this_map, XFor_each ForFunction, void* args)
{
	for_each_reverse_iterator(this_map, XSet, it)
	{
		ForFunction(XSet_reverse_iterator_data(&it), args);
	}
}

void* XSet_reverse_iterator_data(XSet_reverse_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XBTreeNode_GetDataPtr(it->node);
}

#endif