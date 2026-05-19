#include "XSet_reverse_iterator.h"
#if XSet_ON
#include "XSet.h"
#include "XRedBlackTree.h"

#define XSet_RootPtr(set) \
    (XContainerIsCow(set) ? (XRBTreeNode**)XContainerSharedDataPtr(set) : (XRBTreeNode**)&XContainerDataPtr(set))

XSet_reverse_iterator XSet_rbegin(XSet* this_set)
{
    XSet_reverse_iterator it = { 0 };
    if (!this_set) return it;
    XRBTreeNode* root = *XSet_RootPtr(this_set);
    if (!root) return it;
    XRBTreeNode* current = root;
    while (XBTreeNode_GetRChild(current))
        current = XBTreeNode_GetRChild(current);
    it.node = current;
    return it;
}

XSet_reverse_iterator XSet_rend(XSet* this_set)
{
    XSet_reverse_iterator it = { 0 };
    return it;
}

bool XSet_reverse_iterator_isRend(const XSet_reverse_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XSet_reverse_iterator_add(XSet* this_set, XSet_reverse_iterator* it)
{
    if (!this_set || !it || !it->node) return;
    if (XBTreeNode_GetLChild(it->node)) {
        XRBTreeNode* current = XBTreeNode_GetLChild(it->node);
        while (XBTreeNode_GetRChild(current))
            current = XBTreeNode_GetRChild(current);
        it->node = current;
        return;
    }
    XRBTreeNode* current = it->node;
    XRBTreeNode* parent = XBTreeNode_GetParent(current);
    while (parent && current == XBTreeNode_GetLChild(parent)) {
        current = parent;
        parent = XBTreeNode_GetParent(parent);
    }
    it->node = parent;
}

bool XSet_reverse_iterator_equality(XSet_reverse_iterator* itFirst, XSet_reverse_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XSet_reverse_iterator_for_each(XSet* this_set, XFor_each ForFunction, void* args)
{
    for_each_reverse_iterator(this_set, XSet, it)
        ForFunction(XSet_reverse_iterator_data(&it), args);
}

void* XSet_reverse_iterator_data(XSet_reverse_iterator* it)
{
    if (!it || !it->node) return NULL;
    return XBTreeNode_GetDataPtr(it->node);
}

#endif