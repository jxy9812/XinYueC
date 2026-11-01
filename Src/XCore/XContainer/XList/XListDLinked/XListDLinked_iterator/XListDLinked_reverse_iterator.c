#include "XListDLinked_reverse_iterator.h"
#if XListDLinked_ON
#include "XListDLinked.h"

#define XListDLinked_HeadPtr(list) \
    (XContainerIsCow(list) ? (XListDNode**)XContainerSharedDataPtr(list) : (XListDNode**)&XContainerDataPtr(list))

XListDLinked_reverse_iterator XListDLinked_rbegin(XListDLinked* this_list)
{
    XListDLinked_reverse_iterator it = { 0 };
    if (ISNULL(this_list, "")) return it;
    XListDNode** head_ptr = XListDLinked_HeadPtr(this_list);
    XListDNode* head = head_ptr ? *head_ptr : NULL;
    it.node = head ? head->prev : NULL;
    return it;
}

XListDLinked_reverse_iterator XListDLinked_rend(XListDLinked* this_list)
{
    XListDLinked_reverse_iterator it = { 0 };
    return it;
}

bool XListDLinked_reverse_iterator_isEnd(const XListDLinked_reverse_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XListDLinked_reverse_iterator_add(XListDLinked* this_list, XListDLinked_reverse_iterator* it)
{
    if (ISNULL(this_list, "") || ISNULL(it, "")) return;
    if (!it->node) return;
    XListDNode** head_ptr = XListDLinked_HeadPtr(this_list);
    XListDNode* head = head_ptr ? *head_ptr : NULL;
    if (!head) {
        it->node = NULL;
        return;
    }
    if (it->node == head) {
        it->node = NULL;
        return;
    }
    it->node = ((XListDNode*)it->node)->prev;
}

bool XListDLinked_reverse_iterator_equality(XListDLinked_reverse_iterator* itFirst, XListDLinked_reverse_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XListDLinked_reverse_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args)
{
    for_each_reverse_iterator(this_list, XListDLinked, it) {
        ForFunction(XListDLinked_reverse_iterator_data(&it), args);
    }
}

void* XListDLinked_reverse_iterator_data(XListDLinked_reverse_iterator* it)
{
    if (!it || !it->node) return NULL;
    return XListDNode_DataPtr(it->node);
}

#endif