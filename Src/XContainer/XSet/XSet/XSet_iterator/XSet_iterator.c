#include "XSet_iterator.h"
#if XSet_ON
#include "XSet.h"
#include "XRedBlackTree.h"

// 展开获取根节点指针地址的逻辑（与 XSet_root_ptr 相同）
#define XSet_RootPtr(set) \
    (XContainerIsCow(set) ? (XRBTreeNode**)XContainerSharedDataPtr(set) : (XRBTreeNode**)&XContainerDataPtr(set))

XSet_iterator XSet_begin(XSet* this_set)
{
    XSet_iterator it = { 0 };
    if (!this_set) return it;
    XRBTreeNode* root = *XSet_RootPtr(this_set);
    if (!root) return it;
    XRBTreeNode* current = root;
    while (XBTreeNode_GetLChild(current))
        current = XBTreeNode_GetLChild(current);
    it.node = current;
    return it;
}

XSet_iterator XSet_end(XSet* this_set)
{
    XSet_iterator it = { 0 };
    return it;
}

bool XSet_iterator_isEnd(const XSet_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XSet_iterator_add(XSet* this_set, XSet_iterator* it)
{
    if (!this_set || !it || !it->node) return;
    if (XBTreeNode_GetRChild(it->node)) {
        XRBTreeNode* current = XBTreeNode_GetRChild(it->node);
        while (XBTreeNode_GetLChild(current))
            current = XBTreeNode_GetLChild(current);
        it->node = current;
        return;
    }
    XRBTreeNode* current = it->node;
    XRBTreeNode* parent = XBTreeNode_GetParent(current);
    while (parent && current == XBTreeNode_GetRChild(parent)) {
        current = parent;
        parent = XBTreeNode_GetParent(parent);
    }
    it->node = parent;
}

bool XSet_iterator_equality(XSet_iterator* itFirst, XSet_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XSet_iterator_for_each(XSet* this_set, XFor_each ForFunction, void* args)
{
    for_each_iterator(this_set, XSet, it)
        ForFunction(XSet_iterator_data(&it), args);
}

void* XSet_iterator_data(XSet_iterator* it)
{
    if (!it || !it->node) return NULL;
    return XBTreeNode_GetDataPtr(it->node);
}

#endif