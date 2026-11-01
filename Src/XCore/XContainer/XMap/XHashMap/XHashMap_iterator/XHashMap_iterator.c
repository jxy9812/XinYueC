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
            it.m_node = node;
            it.m_index = i;
            return it;
        }
    }
    it.m_index = XContainerCapacity(this_map);
    return it;
}

XHashMap_iterator XHashMap_end(XHashMap* this_map)
{
    XHashMap_iterator it = { 0 };
    if (this_map) it.m_index = XContainerCapacity(this_map);
    return it;
}

bool XHashMap_iterator_isEnd(const XHashMap_iterator* it)
{
    return it ? (it->m_node == NULL) : false;
}

void XHashMap_iterator_add(XHashMap* this_map, XHashMap_iterator* it)
{
    if (!this_map || !it || XContainerCapacity(this_map) == 0) return;
    if (XBTreeNode_GetRChild(it->m_node)) {
        XRBTreeNode* current = XBTreeNode_GetRChild(it->m_node);
        while (XBTreeNode_GetLChild(current))
            current = XBTreeNode_GetLChild(current);
        it->m_node = current;
        return;
    }
    XRBTreeNode* current = it->m_node;
    XRBTreeNode* parent = XBTreeNode_GetParent(current);
    while (parent && current == XBTreeNode_GetRChild(parent)) {
        current = parent;
        parent = XBTreeNode_GetParent(parent);
    }
    it->m_node = parent;
    if (it->m_node) return;
    XRBTreeNode** buckets = XHashMap_Buckets(this_map);
    if (!buckets) return;
    for (size_t i = it->m_index + 1; i < XContainerCapacity(this_map); i++) {
        XRBTreeNode* node = buckets[i];
        if (node) {
            while (XBTreeNode_GetLChild(node))
                node = XBTreeNode_GetLChild(node);
            it->m_node = node;
            it->m_index = i;
            return;
        }
    }
    it->m_node = NULL;
    it->m_index = XContainerCapacity(this_map);
}

bool XHashMap_iterator_equality(const XHashMap_iterator* itFirst, const XHashMap_iterator* itSecond)
{
    return (itFirst->m_index == itSecond->m_index) && (itFirst->m_node == itSecond->m_node);
}

void XHashMap_iterator_for_each(XHashMap* this_map, XFor_each ForFunction, void* args)
{
    for_each_iterator(this_map, XHashMap, it) {
        ForFunction(XHashMap_iterator_data(&it), args);
    }
}

XPair* XHashMap_iterator_data(XHashMap_iterator* it)
{
    if (!it || !it->m_node) return NULL;
    return XBTreeNode_GetDataPtr(it->m_node);
}

#endif