#include "XMap_reverse_iterator.h"
#if XMap_ON
#include"XMap.h"
#include"XRedBlackTree.h"

XMap_reverse_iterator XMap_rbegin(XMap* this_map)
{
	XMap_reverse_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* current = XContainerDataPtr(this_map);
	if (current == NULL) return it;
	while (XBTreeNode_GetRChild(current) != NULL) {
		current = XBTreeNode_GetRChild(current);
	}
	it.node = current;
	return it;
}

XMap_reverse_iterator XMap_rend(XMap* this_map)
{

	XMap_reverse_iterator it = { 0 };
	//if (this_map == NULL)
		return it;
	/*XRBTreeNode* this_root = XContainerDataPtr(this_map);
	return it;*/
}

bool XMap_reverse_iterator_isRend(const XMap_reverse_iterator* it)
{
	return it ? (it->node == NULL) : false;
}

void XMap_reverse_iterator_add(XMap* this_map, XMap_reverse_iterator* it)
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
	return XBTreeNode_GetData(it->node,XPair*);
}

#endif