#include "XHashMap_iterator.h"
#if XHashMap_ON
#include "XHashMap.h"
#include "XRedBlackTree.h"

// 获取桶数组基地址（与 XHashMap_buckets 宏一致）
#define XHashMap_Buckets(map) \
    (XContainerIsCow(map) ? (XRBTreeNode**)XContainerSharedDataPtr(map) : (XRBTreeNode**)XContainerDataPtr(map))

XHashMap_iterator XHashMap_begin(XHashMap* this_map)
{
    XHashMap_iterator it = { 0 };
    if (!this_map || XContainerCapacity(this_map) == 0) return it;
    XRBTreeNode** buckets = XHashMap_Buckets(this_map);
    if (!buckets) return it;
    for (size_t i = 0; i < XContainerCapacity(this_map); i++) {
        XRBTreeNode* node = buckets[i];
        if (node) {
            while (XBTreeNode_GetLChild(node))
                node = XBTreeNode_GetLChild(node);
            it.node = node;
            it.index = i;
            return it;
        }
    }
    it.index = XContainerCapacity(this_map);
    return it;
}

XHashMap_iterator XHashMap_end(XHashMap* this_map)
{
    XHashMap_iterator it = { 0 };
    if (this_map) it.index = XContainerCapacity(this_map);
    return it;
}

bool XHashMap_iterator_isEnd(const XHashMap_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XHashMap_iterator_add(XHashMap* this_map, XHashMap_iterator* it)
{
    if (!this_map || !it || XContainerCapacity(this_map) == 0) return;
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
    if (it->node) return;
    XRBTreeNode** buckets = XHashMap_Buckets(this_map);
    if (!buckets) return;
    for (size_t i = it->index + 1; i < XContainerCapacity(this_map); i++) {
        XRBTreeNode* node = buckets[i];
        if (node) {
            while (XBTreeNode_GetLChild(node))
                node = XBTreeNode_GetLChild(node);
            it->node = node;
            it->index = i;
            return;
        }
    }
    it->node = NULL;
    it->index = XContainerCapacity(this_map);
}

bool XHashMap_iterator_equality(const XHashMap_iterator* itFirst, const XHashMap_iterator* itSecond)
{
    return (itFirst->index == itSecond->index) && (itFirst->node == itSecond->node);
}

void XHashMap_iterator_for_each(XHashMap* this_map, XFor_each ForFunction, void* args)
{
    for_each_iterator(this_map, XHashMap, it) {
        ForFunction(XHashMap_iterator_data(&it), args);
    }
}

XPair* XHashMap_iterator_data(XHashMap_iterator* it)
{
    if (!it || !it->node) return NULL;
    return XBTreeNode_GetDataPtr(it->node);
}

#endif