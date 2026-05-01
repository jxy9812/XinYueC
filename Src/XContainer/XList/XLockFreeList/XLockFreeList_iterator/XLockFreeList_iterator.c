#include"XLockFreeList_iterator.h"
#if XLockFreeList_ON
#include "XLockFreeList.h"
#include "XLockFreeListConfig.h"
#include <stdio.h>

static inline void* unpack_ptr(size_t val)
{
    return (void*)(val & PTR_MASK);
}
XLockFreeList_iterator XLockFreeList_begin(XLockFreeList * this_list)
{
    XLockFreeList_iterator it = { 0 };
    if (this_list == NULL)
        return it;
    // --- 关键修改：正确地从 tagged_ptr_t 中解包出指针 ---
    size_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    it.node = (XLockFreeListNode*)unpack_ptr(head_tagged);
    return it;
}

XLockFreeList_iterator XLockFreeList_end(XLockFreeList* this_list)
{
    XLockFreeList_iterator it = { 0 };
    it.node = NULL;
    return it;
}

bool XLockFreeList_iterator_isEnd(const XLockFreeList_iterator* it)
{
    return it ? (it->node == NULL) : false;
}

void XLockFreeList_iterator_add(XLockFreeList* this_list, XLockFreeList_iterator* it)
{
    if (this_list == NULL || it == NULL || it->node == NULL)
        return;
    it->node = ((XLockFreeListNode*)it->node)->next;
}

bool XLockFreeList_iterator_equality(XLockFreeList_iterator* itFirst, XLockFreeList_iterator* itSecond)
{
    return itFirst->node == itSecond->node;
}

void XLockFreeList_iterator_for_each(XLockFreeList* this_list, XFor_each ForFunction, void* args)
{
    if (this_list == NULL || ForFunction == NULL)
        return;
    XLockFreeList_iterator it = XLockFreeList_begin(this_list);
    XLockFreeList_iterator end = XLockFreeList_end(this_list);
    while (!XLockFreeList_iterator_equality(&it, &end))
    {
        ForFunction(XLockFreeList_iterator_data(&it), args);
        XLockFreeList_iterator_add(this_list, &it);
    }
}

void* XLockFreeList_iterator_data(XLockFreeList_iterator* it)
{
    if (it == NULL || it->node == NULL)
        return NULL;
    return &((XLockFreeListNode*)it->node)->data;
}

#endif