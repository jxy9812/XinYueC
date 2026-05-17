#include"XSet_iterator.h"
#if XSet_ON
#include"XSet.h"
#include"XRedBlackTree.h"

XSet_iterator XSet_begin(XSet* this_map)
{
	//printf("开始\n");
	XSet_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* current = *(XRBTreeNode**)XContainerSharedDataPtr(this_map);
	if (current == NULL) return it;
	while (XBTreeNode_GetLChild(current)!= NULL) {
		current = XBTreeNode_GetLChild(current);
	}
	it.node = current;
	return it;
}

XSet_iterator XSet_end(XSet* this_map)
{
	XSet_iterator it = { 0 };
	if (this_map == NULL)
		return it;
	XRBTreeNode* this_root = XContainerSharedDataPtr(this_map);
	return it;
}

bool XSet_iterator_isEnd(const XSet_iterator* it)
{
	return it ? (it->node == NULL) : false;
}

void XSet_iterator_add(XSet* this_map, XSet_iterator* it)
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

bool XSet_iterator_equality(XSet_iterator* itFirst, XSet_iterator* itSecond)
{
	return itFirst->node==itSecond->node;
}

void XSet_iterator_for_each(XSet* this_map, XFor_each ForFunction, void* args)
{
	for_each_iterator(this_map,XSet,it)
	{
		ForFunction(XSet_iterator_data(&it), args);
	}
}

void* XSet_iterator_data(XSet_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XBTreeNode_GetDataPtr(it->node);
}


#endif