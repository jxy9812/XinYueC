/**
* @file XAnyStringView_reverse_iterator.c
* @brief XAnyStringView 反向迭代器实现
*/
#include "XAnyStringView_reverse_iterator.h"
#if XString_ON
#include "XAnyStringView.h"
#include <stdio.h>

XAnyStringView_reverse_iterator XAnyStringView_rbegin(const XAnyStringView* self)
{
    XAnyStringView_reverse_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size_and_tag == 0)
        return it;
    int64_t size = (int64_t)(self->m_size_and_tag & XAnyStringView_SizeMask);
    it.data = self->m_data_utf8 + size - 1;
    return it;
}

XAnyStringView_reverse_iterator XAnyStringView_rend(const XAnyStringView* self)
{
    XAnyStringView_reverse_iterator it = { NULL };
    (void)self;
    return it;
}

bool XAnyStringView_reverse_iterator_isRend(const XAnyStringView_reverse_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XAnyStringView_reverse_iterator_add(const XAnyStringView* self, XAnyStringView_reverse_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    if (it->data == self->m_data_utf8)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data - 1;
}

bool XAnyStringView_reverse_iterator_equality(XAnyStringView_reverse_iterator* itFirst, XAnyStringView_reverse_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XAnyStringView_reverse_iterator_for_each(const XAnyStringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_reverse_iterator(self, XAnyStringView, it)
    {
        ForFunction((void*)XAnyStringView_reverse_iterator_data(&it), args);
    }
}

const char* XAnyStringView_reverse_iterator_data(XAnyStringView_reverse_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
