/**
* @file XAnyStringView_iterator.c
* @brief XAnyStringView 正向迭代器实现
*/
#include "XAnyStringView_iterator.h"
#if XString_ON
#include "XAnyStringView.h"
#include <stdio.h>

XAnyStringView_iterator XAnyStringView_begin(const XAnyStringView* self)
{
    XAnyStringView_iterator it = { NULL };
    if (!self || !self->m_data || self->m_size_and_tag == 0)
        return it;
    it.data = self->m_data_utf8;
    return it;
}

XAnyStringView_iterator XAnyStringView_end(const XAnyStringView* self)
{
    XAnyStringView_iterator it = { NULL };
    (void)self;
    return it;
}

bool XAnyStringView_iterator_isEnd(const XAnyStringView_iterator* it)
{
    return it ? (it->data == NULL) : false;
}

void XAnyStringView_iterator_add(const XAnyStringView* self, XAnyStringView_iterator* it)
{
    if (!self || !it || !it->data)
        return;
    /* 使用 m_size_and_tag 的低位作为长度 */
    int64_t size = (int64_t)(self->m_size_and_tag & XAnyStringView_SizeMask);
    if (it->data == self->m_data_utf8 + size - 1)
    {
        it->data = NULL;
        return;
    }
    it->data = it->data + 1;
}

bool XAnyStringView_iterator_equality(XAnyStringView_iterator* itFirst, XAnyStringView_iterator* itSecond)
{
    return itFirst->data == itSecond->data;
}

void XAnyStringView_iterator_for_each(const XAnyStringView* self, XFor_each ForFunction, void* args)
{
    if (!self || !ForFunction)
        return;
    for_each_iterator(self, XAnyStringView, it)
    {
        ForFunction((void*)XAnyStringView_iterator_data(&it), args);
    }
}

const char* XAnyStringView_iterator_data(XAnyStringView_iterator* it)
{
    if (!it || !it->data)
        return NULL;
    return it->data;
}

#endif
