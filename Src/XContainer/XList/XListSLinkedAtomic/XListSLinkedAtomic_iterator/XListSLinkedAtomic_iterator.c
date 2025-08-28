#include"XListSLinkedAtomic_iterator.h"
#if XListSLinkedAtomic_ON
#include"XListSLinkedAtomic.h"
#include<stdio.h>

XListSLinkedAtomic_iterator XListSLinkedAtomic_begin(XListSLinkedAtomic * this_list)
{
    XListSLinkedAtomic_iterator it = { 0 };
    if (this_list == NULL)
        return it;
    it.node = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
    return it;
}

XListSLinkedAtomic_iterator XListSLinkedAtomic_end(XListSLinkedAtomic* this_list)
{
    XListSLinkedAtomic_iterator it = { 0 };
    it.node = NULL;
    return it;
}

bool XListSLinkedAtomic_iterator_isEnd(const XListSLinkedAtomic_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XListSLinkedAtomic_iterator_add(XListSLinkedAtomic* this_list, XListSLinkedAtomic_iterator* it)
{
    if (this_list == NULL || it == NULL || it->node == NULL)
        return;
    it->node = it->node->next;
}

bool XListSLinkedAtomic_iterator_equality(XListSLinkedAtomic_iterator* itFirst, XListSLinkedAtomic_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XListSLinkedAtomic_iterator_for_each(XListSLinkedAtomic* this_list, XFor_each ForFunction, void* args)
{
    if (this_list == NULL || ForFunction == NULL)
        return;
    XListSLinkedAtomic_iterator it = XListSLinkedAtomic_begin(this_list);
    XListSLinkedAtomic_iterator end = XListSLinkedAtomic_end(this_list);
    while (!XListSLinkedAtomic_iterator_equality(&it, &end))
    {
        ForFunction(XListSLinkedAtomic_iterator_data(&it), args);
        XListSLinkedAtomic_iterator_add(this_list, &it);
    }
}

void* XListSLinkedAtomic_iterator_data(XListSLinkedAtomic_iterator* it)
{
    if (it == NULL || it->node == NULL)
        return NULL;
    return &it->node->data;
}

#endif