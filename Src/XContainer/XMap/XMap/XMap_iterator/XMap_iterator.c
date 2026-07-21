#include "XMap_iterator.h"
#if XMap_ON
#include "XMap.h"
#include "XRedBlackTree.h"

// 宏：获取根节点指针地址（与 XMap_root_ptr 行为一致）
#define XMap_RootPtr(map) \
    (XContainerIsCow(map) ? (XRBTreeNode**)XContainerSharedDataPtr(map) : (XRBTreeNode**)&XContainerDataPtr(map))

XMap_iterator XMap_begin(XMap* this_map)
{
    XMap_iterator it = { 0 };
    if (!this_map) return it;
    /* 空 map 时 m_data 为 NULL，直接返回空迭代器 */
    if (!XContainerDataPtr(this_map)) return it;
    XRBTreeNode* root = *XMap_RootPtr(this_map);
    if (!root) return it;
    XRBTreeNode* current = root;
    while (XBTreeNode_GetLChild(current))
        current = XBTreeNode_GetLChild(current);
    it.node = current;
    return it;
}

XMap_iterator XMap_end(XMap* this_map)
{
    XMap_iterator it = { 0 };
    return it;
}

bool XMap_iterator_isEnd(const XMap_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XMap_iterator_add(XMap* this_map, XMap_iterator* it)
{
    if (!this_map || !it || !it->node) return;
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

bool XMap_iterator_equality(XMap_iterator* itFirst, XMap_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XMap_iterator_for_each(XMap* this_map, XFor_each ForFunction, void* args)
{
    for_each_iterator(this_map, XMap, it) {
        ForFunction(XMap_iterator_data(&it), args);
    }
}

XPair* XMap_iterator_data(XMap_iterator* it)
{
    if (!it || !it->node) return NULL;
    return XBTreeNode_GetDataPtr(it->node);
}

#endif