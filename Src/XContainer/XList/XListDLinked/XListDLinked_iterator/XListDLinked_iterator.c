#include "XListDLinked_iterator.h"
#if XListDLinked_ON
#include "XListDLinked.h"

// 辅助宏：获取头节点指针的地址（展开 XListDLinked_head_ptr 的逻辑）
#define XListDLinked_HeadPtr(list) \
    (XContainerIsCow(list) ? (XListDNode**)XContainerSharedDataPtr(list) : (XListDNode**)&XContainerDataPtr(list))

XListDLinked_iterator XListDLinked_begin(XListDLinked* this_list)
{
    XListDLinked_iterator it = { 0 };
    if (ISNULL(this_list, "")) return it;
    XListDNode** head_ptr = XListDLinked_HeadPtr(this_list);
    it.node = head_ptr ? *head_ptr : NULL;
    return it;
}

XListDLinked_iterator XListDLinked_end(XListDLinked* this_list)
{
    XListDLinked_iterator it = { 0 };
    return it;
}

bool XListDLinked_iterator_isEnd(const XListDLinked_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XListDLinked_iterator_add(XListDLinked* this_list, XListDLinked_iterator* it)
{
    if (ISNULL(this_list, "") || ISNULL(it, "")) return;
    if (!it->node) return;
    XListDNode** head_ptr = XListDLinked_HeadPtr(this_list);
    XListDNode* head = head_ptr ? *head_ptr : NULL;
    if (!head) {
        it->node = NULL;
        return;
    }
    if (it->node == head->prev) {
        it->node = NULL;
        return;
    }
    it->node = ((XListDNode*)it->node)->next;
}

bool XListDLinked_iterator_equality(XListDLinked_iterator* itFirst, XListDLinked_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XListDLinked_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args)
{
    for_each_iterator(this_list, XListDLinked, it) {
        ForFunction(XListDLinked_iterator_data(&it), args);
    }
}

void* XListDLinked_iterator_data(XListDLinked_iterator* it)
{
    if (!it || !it->node) return NULL;
    return XListDNode_DataPtr(it->node);
}

#endif