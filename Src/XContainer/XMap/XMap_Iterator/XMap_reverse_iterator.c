#include "XMap_reverse_iterator.h"
#if XMap_ON
#include"XMap.h"
#include"XVector.h"
#include"XRedBlackTree.h"

XMap_reverse_iterator XMap_rbegin(XMap* this_map)
{
	XMap_reverse_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* current = XContainerDataPtr(this_map);
	if (current == NULL) return it;
	while (XBTree_GetRChild(current) != NULL) {
		current = XBTree_GetRChild(current);
	}
	it.node = current;
	return it;
}

XMap_reverse_iterator XMap_rend(XMap* this_map)
{

	XMap_reverse_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* this_root = XContainerDataPtr(this_map);
	return it;
}

XMap_reverse_iterator* XMap_reverse_iterator_add(XMap* this_map, XMap_reverse_iterator* it)
{
	if (this_map == NULL || it == NULL || it->node == NULL)
		return;
	// 如果有左子树，找到左子树的最右节点
	if (XBTree_GetLChild(it->node) != NULL) {
		XRBTreeNode* current = XBTree_GetLChild(it->node);
		while (XBTree_GetRChild(current) != NULL) {
			current = XBTree_GetRChild(current);
		}
		it->node = current;
		return;
	}

	// 否则向上回溯，直到找到一个作为右子节点的祖先
	XRBTreeNode* current = it->node;
	XRBTreeNode* parent = XBTree_GetParent(current);
	while (parent != NULL && current == XBTree_GetLChild(parent)) {
		current = parent;
		parent = XBTree_GetParent(parent);
	}
	it->node = parent;
	return;
}

bool XMap_reverse_iterator_equality(XMap_reverse_iterator* itFirst, XMap_reverse_iterator* itSecond)
{
	return itFirst->node == itSecond->node;
}

void XMap_reverse_iterator_for_each(XMap* this_map, XFor_each ForFunction, void* args)
{
	for_each_reverse_iterator(this_map, XMap, it)
	{
		ForFunction(XMap_reverse_iterator_data(&it), args);
	}
}

XPair* XMap_reverse_iterator_data(XMap_reverse_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XBTree_GetData(it->node, 0, XPair*);
}

#endif