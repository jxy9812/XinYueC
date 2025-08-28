#include "XHashSet_iterator.h"
#if XHashSet_ON
#include "XHashSet.h"
#include"XRedBlackTree.h"
XHashSet_iterator XHashSet_begin(XHashSet* this_set)
{
    XHashSet_iterator it = { 0 };
    if (this_set == NULL || XContainerCapacity(this_set) == 0)
        return it;
    XRBTreeNode* node = NULL;
    for (size_t i = 0; i < XContainerCapacity(this_set); i++)
    {
        node = ((XRBTreeNode**)XContainerDataPtr(this_set))[i];
        if (node == NULL)
            continue;
        while (XBTreeNode_GetLChild(node) != NULL)
        {
            node = XBTreeNode_GetLChild(node);
        }
        if (node != NULL)
        {
            it.node = node;
            it.index = i;
            return it;//下一个节点找到了
        }
    }
    it.node = NULL;
    it.index = XContainerCapacity(this_set);
    return it;
}

XHashSet_iterator XHashSet_end(XHashSet* this_set)
{
    XHashSet_iterator it = { 0 };
    if (this_set == NULL || XContainerCapacity(this_set) == 0)
        return it;
    it.index = XContainerCapacity(this_set);
    return it;
}

bool XHashSet_iterator_isEnd(const XHashSet_iterator* it)
{
	return it ? (it->node == NULL) : false;
}

void XHashSet_iterator_add(XHashSet* this_set, XHashSet_iterator* it)
{
	if (this_set == NULL || it == NULL || XContainerCapacity(this_set) == 0)
		return;
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
	for (size_t i = it->index + 1; i < XContainerCapacity(this_set); i++)
	{
		it->node = ((XRBTreeNode**)XContainerDataPtr(this_set))[i];
		if (it->node == NULL)
			continue;
		while (XBTreeNode_GetLChild(it->node) != NULL)
		{
			it->node = XBTreeNode_GetLChild(it->node);
		}
		if (it->node != NULL)
		{
			it->index = i;
			return;//下一个节点找到了
		}
	}
	if (it->node == NULL)
	{
		it->node = NULL;
		it->index = XContainerCapacity(this_set);
	}
}

bool XHashSet_iterator_equality(XHashSet_iterator* itFirst, XHashSet_iterator* itSecond)
{
    return (itFirst->index == itSecond->index) && (itFirst->node == itSecond->node);
}

void XHashSet_iterator_for_each(XHashSet* this_set, XFor_each ForFunction, void* args)
{
    for_each_iterator(this_set, XHashSet, it)
    {
		//printf("index:%d\n",it.index);
        ForFunction(XHashSet_iterator_data(&it), args);
    }
}

void* XHashSet_iterator_data(XHashSet_iterator* it)
{
	if (it == NULL || it->node == NULL)
		return NULL;
	return XBTreeNode_GetDataPtr(it->node);
}

#endif