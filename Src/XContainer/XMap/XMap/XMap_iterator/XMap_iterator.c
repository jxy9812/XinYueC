#include"XMap_iterator.h"
#if XMap_ON
#include"XMap.h"
#include"XRedBlackTree.h"


XMap_iterator XMap_begin(XMap* this_map)
{
	//printf("开始\n");
	XMap_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* current = XContainerDataPtr(this_map);
	if (current == NULL) return it;
	while (XBTreeNode_GetLChild(current)!= NULL) {
		current = XBTreeNode_GetLChild(current);
	}
	it.node = current;
	return it;
}

XMap_iterator XMap_end(XMap* this_map)
{
	XMap_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* this_root = XContainerDataPtr(this_map);
	return it;
}

void XMap_iterator_add(XMap* this_map, XMap_iterator* it)
{
	if (this_map == NULL||it==NULL||it->node==NULL)
		return ;
	// 如果有右子树，找到右子树的最左节点
	if (XBTreeNode_GetRChild(it->node) != NULL) {
		XRBTreeNode* current = XBTreeNode_GetRChild(it->node);
		while (XBTreeNode_GetLChild(current) != NULL) {
			current = XBTreeNode_GetLChild(current);
		}
		it->node = current;
		return ;
	}

	// 否则向上回溯，直到找到一个作为左子节点的祖先
	XRBTreeNode* current = it->node;
	XRBTreeNode* parent = XBTreeNode_GetParent(current);
	while (parent != NULL && current == XBTreeNode_GetRChild(parent)) {
		current = parent;
		parent = XBTreeNode_GetParent(parent);
	}
	it->node = parent;
	return ;
}

bool XMap_iterator_equality(XMap_iterator* itFirst, XMap_iterator* itSecond)
{
	return itFirst->node==itSecond->node;
}

void XMap_iterator_for_each(XMap* this_map, XFor_each ForFunction, void* args)
{
	for_each_iterator(this_map,XMap,it)
	{
		ForFunction(XMap_iterator_data(&it), args);
	}
}

XPair* XMap_iterator_data(XMap_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XBTreeNode_GetData(it->node, XPair*);
}


#endif