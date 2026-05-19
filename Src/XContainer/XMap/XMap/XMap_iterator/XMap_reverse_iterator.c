#include "XMap_reverse_iterator.h"
#if XMap_ON
#include "XMap.h"
#include "XRedBlackTree.h"

#define XMap_RootPtr(map) \
    (XContainerIsCow(map) ? (XRBTreeNode**)XContainerSharedDataPtr(map) : (XRBTreeNode**)&XContainerDataPtr(map))

XMap_reverse_iterator XMap_rbegin(XMap* this_map)
{
    XMap_reverse_iterator it = { 0 };
    if (!this_map) return it;
    XRBTreeNode* root = *XMap_RootPtr(this_map);
    if (!root) return it;
    XRBTreeNode* current = root;
    while (XBTreeNode_GetRChild(current))
        current = XBTreeNode_GetRChild(current);
    it.node = current;
    return it;
}

XMap_reverse_iterator XMap_rend(XMap* this_map)
{
    XMap_reverse_iterator it = { 0 };
    return it;
}

bool XMap_reverse_iterator_isRend(const XMap_reverse_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XMap_reverse_iterator_add(XMap* this_map, XMap_reverse_iterator* it)
{
    if (!this_map || !it || !it->node) return;
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

bool XMap_reverse_iterator_equality(XMap_reverse_iterator* itFirst, XMap_reverse_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XMap_reverse_iterator_for_each(XMap* this_map, XFor_each ForFunction, void* args)
{
    for_each_reverse_iterator(this_map, XMap, it) {
        ForFunction(XMap_reverse_iterator_data(&it), args);
    }
}

XPair* XMap_reverse_iterator_data(XMap_reverse_iterator* it)
{
    if (!it || !it->node) return NULL;
    return XBTreeNode_GetDataPtr(it->node);
}

#endif